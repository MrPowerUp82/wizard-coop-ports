#include "online_menu.hpp"

#include "batch_renderer.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>

namespace arcana::online {
namespace {

using native::rgba;
using sdl::Align;
using MenuResult = sdl::OnlinePort::MenuResult;

constexpr std::uint32_t kBg = rgba(10, 10, 22), kPanel = rgba(18, 20, 36, 220), kSel = rgba(40, 44, 80, 240);
constexpr std::uint32_t kText = rgba(236, 240, 255), kMuted = rgba(150, 160, 190), kGold = rgba(255, 214, 110), kDanger = rgba(255, 140, 120);
constexpr const char* kCampaignIds[3] = {"quick", "classic", "endless"};
constexpr const char* kCampaignTitles[3] = {"Ritual rápido", "Ritual clássico", "Ritual infinito"};
struct CurseText { const char* id; const char* title; const char* desc; };
// server/curses.js CURSES.
constexpr CurseText kCurses[6] = {
  {"swarm", "Enxame", "Ondas 35% maiores e mais inimigos na tela"},
  {"frenzy", "Frenesi", "Inimigos 15% mais rápidos"},
  {"brittle", "Fragilidade", "Você recebe 25% mais dano"},
  {"famine", "Fome", "Corações e curas restauram metade"},
  {"tyrant", "Tirania", "Guardiões com 40% mais vida"},
  {"nobility", "Nobreza sombria", "Cada chamado de elite traz duas elites"},
};
constexpr const char* kCharacterNames[4] = {"Azul", "Vermelho", "Verde", "Roxo"};
constexpr int kMaxRoomRows = 6;
constexpr int kCreateRows = 10; // visibility, ritual, 6 curses, create, back

const char* campaignTitle(const std::string& id) {
  for (int i = 0; i < 3; ++i) if (id == kCampaignIds[i]) return kCampaignTitles[i];
  return kCampaignTitles[0];
}
double steadyMs() { return std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now().time_since_epoch()).count(); }
int rankOf(const Profile& p, const char* id) {
  const auto it = p.upgrades.rank.find(id);
  return it == p.upgrades.rank.end() ? 0 : it->second;
}
bool is(std::uint32_t pressed, std::uint32_t action) { return (pressed & action) != 0; }
int wrap(int value, int count) { return count <= 0 ? 0 : (value % count + count) % count; }

void row(sdl::BatchRenderer& b, float x, float y, float w, float s, bool selected, const char* label, const char* value = nullptr,
         std::uint32_t valueColor = 0) {
  b.rect(x, y, w, 42 * s, selected ? kSel : kPanel);
  if (selected) b.frame(x, y, w, 42 * s, 3 * s, kGold);
  b.text(x + 20 * s, y + 8 * s, label, 22 * s, selected ? kText : kMuted);
  if (value) b.text(x + w - 20 * s, y + 10 * s, value, 20 * s, valueColor ? valueColor : selected ? kGold : kMuted, Align::Right);
}

} // namespace

OnlineMenu::OnlineMenu(OnlineMenuConfig config) : config_(std::move(config)), now_(steadyMs) {}

void OnlineMenu::openMenu(const Profile& profile) {
  url_ = !config_.forceUrl && !profile.prefs.server.empty() ? profile.prefs.server : config_.url;
  const UrlCheck check = checkServerUrl(url_, config_.allowInsecure);
  urlOk_ = check == UrlCheck::Ok;
  message_.clear();
  if (check == UrlCheck::Invalid) message_ = "Endereço do servidor inválido: " + url_;
  if (check == UrlCheck::InsecureBlocked) message_ = "Conexões sem TLS (ws://) só são permitidas para localhost. Use --insecure-ws para testes.";
  rooms_ = urlOk_ ? std::make_unique<RoomList>(url_, config_.transport) : nullptr;
  endlessUnlocked_ = rankOf(profile, "endless") > 0;
  name_ = profile.prefs.name;
  campaign_ = 0;
  for (int i = 0; i < 3; ++i) if (profile.prefs.campaign == kCampaignIds[i]) campaign_ = i;
  if (campaign_ == 2 && !endlessUnlocked_) campaign_ = 0;
  index_ = 0;
  page_ = profile.prefs.name.empty() ? Page::Name : Page::Home;
  if (page_ == Page::Name) entry_ = TextEntry(TextKind::Name);
}

EntryRequest OnlineMenu::baseEntry(const Profile& profile) const {
  EntryRequest e;
  e.name = profile.prefs.name.empty() ? "Arcanista" : profile.prefs.name;
  e.color = profile.prefs.characters[0];
  e.campaign = profile.prefs.campaign;
  e.meta = profile.upgrades;
  e.loadout.weapon = rankOf(profile, "arsenal") > 0 ? profile.prefs.weapon : "";
  e.loadout.special = rankOf(profile, "secondSpell") > 0 ? profile.prefs.special : 0;
  return e;
}

void OnlineMenu::connect(EntryRequest entry) {
  if (!urlOk_) return;
  session_ = std::make_unique<Session>(SessionConfig{url_, std::move(entry), config_.transport});
  session_->start(now_());
  message_.clear();
  index_ = 0;
  page_ = Page::Connecting;
}

void OnlineMenu::backHome() {
  session_.reset();
  page_ = Page::Home;
  index_ = 0;
  if (rooms_) rooms_->refresh(now_());
}

int OnlineMenu::visibleRooms() const {
  return rooms_ ? std::min(kMaxRoomRows, static_cast<int>(rooms_->rooms().rooms.size())) : 0;
}

MenuResult OnlineMenu::updateMenu(std::uint32_t pressed, std::uint32_t, const sdl::TextInput& text, Profile& profile) {
  const double now = now_();
  if (rooms_ && page_ == Page::Home) rooms_->update(now);
  if (session_) session_->update(now);
  if (!config_.bot.empty()) updateBot(profile);
  switch (page_) {
    case Page::Name:
    case Page::Code: return updateText(pressed, text, profile);
    case Page::Home: return updateHome(pressed, profile);
    case Page::Create: updateCreate(pressed, profile); return MenuResult::Stay;
    case Page::Connecting:
    case Page::Lobby: return updateSession(pressed);
    case Page::Playing: return MenuResult::Stay;
  }
  return MenuResult::Stay;
}

MenuResult OnlineMenu::updateHome(std::uint32_t pressed, Profile& profile) {
  if (is(pressed, sdl::ActCancel) || is(pressed, sdl::ActPause)) return MenuResult::Title;
  const int rooms = visibleRooms(), rows = rooms + 4;
  if (is(pressed, sdl::ActUp)) index_ = wrap(index_ - 1, rows);
  if (is(pressed, sdl::ActDown)) index_ = wrap(index_ + 1, rows);
  index_ = std::clamp(index_, 0, rows - 1);
  if (!is(pressed, sdl::ActConfirm)) return MenuResult::Stay;
  if (index_ < rooms) {
    EntryRequest e = baseEntry(profile);
    e.action = "join";
    e.room = rooms_->rooms().rooms[static_cast<std::size_t>(index_)].code;
    connect(std::move(e));
  } else if (index_ == rooms) {
    page_ = Page::Create;
    index_ = 0;
  } else if (index_ == rooms + 1) {
    entry_ = TextEntry(TextKind::RoomCode);
    page_ = Page::Code;
  } else if (index_ == rooms + 2) {
    entry_ = TextEntry(TextKind::Name, profile.prefs.name);
    page_ = Page::Name;
  } else {
    return MenuResult::Title;
  }
  return MenuResult::Stay;
}

void OnlineMenu::updateCreate(std::uint32_t pressed, Profile& profile) {
  if (is(pressed, sdl::ActCancel) || is(pressed, sdl::ActPause)) { page_ = Page::Home; index_ = 0; return; }
  if (is(pressed, sdl::ActUp)) index_ = wrap(index_ - 1, kCreateRows);
  if (is(pressed, sdl::ActDown)) index_ = wrap(index_ + 1, kCreateRows);
  const bool change = is(pressed, sdl::ActConfirm) || is(pressed, sdl::ActLeft) || is(pressed, sdl::ActRight);
  if (!change) return;
  if (index_ == 0) open_ = !open_;
  else if (index_ == 1) do campaign_ = (campaign_ + 1) % 3; while (campaign_ == 2 && !endlessUnlocked_);
  else if (index_ < 8) curses_[static_cast<std::size_t>(index_ - 2)] = !curses_[static_cast<std::size_t>(index_ - 2)];
  else if (index_ == 8 && is(pressed, sdl::ActConfirm)) {
    EntryRequest e = baseEntry(profile);
    e.action = "create";
    e.visibility = open_ ? "open" : "closed";
    e.campaign = kCampaignIds[campaign_];
    for (int i = 0; i < 6; ++i) if (curses_[static_cast<std::size_t>(i)]) e.curses.push_back(kCurses[i].id);
    connect(std::move(e));
  } else if (index_ == 9 && is(pressed, sdl::ActConfirm)) {
    page_ = Page::Home;
    index_ = 0;
  }
}

MenuResult OnlineMenu::updateText(std::uint32_t pressed, const sdl::TextInput& text, Profile& profile) {
  entry_.type(text.typed);
  for (int i = 0; i < text.backspaces; ++i) entry_.backspace();
  if (is(pressed, sdl::ActUp)) entry_.move(0, -1);
  if (is(pressed, sdl::ActDown)) entry_.move(0, 1);
  if (is(pressed, sdl::ActLeft)) entry_.move(-1, 0);
  if (is(pressed, sdl::ActRight)) entry_.move(1, 0);
  if (is(pressed, sdl::ActConfirm)) entry_.press();
  const bool submit = entry_.takeSubmit() || (text.submit && entry_.valid());
  const bool back = is(pressed, sdl::ActCancel) || is(pressed, sdl::ActPause);
  if (page_ == Page::Name) {
    if (submit) {
      std::string name = entry_.value();
      name.erase(0, name.find_first_not_of(' '));
      name.erase(name.find_last_not_of(' ') + 1);
      profile.prefs.name = name_ = name;
      page_ = Page::Home;
      index_ = 0;
    } else if (back) {
      if (profile.prefs.name.empty()) return MenuResult::Title;
      page_ = Page::Home;
    }
  } else if (submit) {
    EntryRequest e = baseEntry(profile);
    e.action = "join";
    e.room = entry_.value();
    connect(std::move(e));
  } else if (back) {
    page_ = Page::Home;
  }
  return MenuResult::Stay;
}

MenuResult OnlineMenu::updateSession(std::uint32_t pressed) {
  if (auto notice = session_->takeNotice()) message_ = *notice;
  switch (session_->status()) {
    case SessionStatus::Closed:
      message_ = session_->failure().empty() ? "Você saiu da sala." : session_->failure();
      backHome();
      return MenuResult::Stay;
    case SessionStatus::Playing:
      page_ = Page::Playing;
      return MenuResult::Play;
    case SessionStatus::Lobby:
      if (page_ != Page::Lobby) { page_ = Page::Lobby; index_ = 0; }
      break;
    default: break;
  }
  if (is(pressed, sdl::ActCancel) || is(pressed, sdl::ActPause)) {
    session_->leave();
    message_.clear();
    backHome();
    return MenuResult::Stay;
  }
  if (page_ != Page::Lobby) return MenuResult::Stay;
  constexpr int rows = 3; // character, start, leave
  if (is(pressed, sdl::ActUp)) index_ = wrap(index_ - 1, rows);
  if (is(pressed, sdl::ActDown)) index_ = wrap(index_ + 1, rows);
  if (is(pressed, sdl::ActLeft) || is(pressed, sdl::ActRight)) {
    // Next character nobody else in the room uses.
    const int dir = is(pressed, sdl::ActLeft) ? -1 : 1;
    for (int step = 1; step < 4; ++step) {
      const int c = wrap(session_->color() + dir * step, 4);
      const auto& players = session_->lobby().players;
      const bool taken = std::any_of(players.begin(), players.end(), [&](const LobbyPlayer& p) { return p.color == c && p.id != session_->playerId(); });
      if (!taken) { session_->selectCharacter(c); break; }
    }
  }
  if (is(pressed, sdl::ActConfirm)) {
    if (index_ == 1) session_->startMatch();
    else if (index_ == 2) { session_->leave(); message_.clear(); backHome(); }
  }
  return MenuResult::Stay;
}

void OnlineMenu::updateBot(Profile& profile) {
  const double now = now_();
  if (page_ == Page::Name) { profile.prefs.name = name_ = config_.bot == "create" ? "Bot anfitrião" : "Bot convidado"; page_ = Page::Home; }
  if (page_ == Page::Home && !session_ && now >= botNextTry_) {
    botNextTry_ = now + 2000;
    EntryRequest e = baseEntry(profile);
    if (config_.bot == "create") {
      e.action = "create";
      e.visibility = "open";
      connect(std::move(e));
    } else if (rooms_ && !rooms_->rooms().rooms.empty()) {
      e.action = "join";
      e.room = rooms_->rooms().rooms.front().code;
      connect(std::move(e));
    }
  }
  if (page_ == Page::Lobby && session_->isHost() && static_cast<int>(session_->lobby().players.size()) >= config_.botPlayers) session_->startMatch();
}

bool OnlineMenu::frame(double dt, Vec2 input, GameState& out) {
  if (!session_) return false;
  const double now = now_();
  session_->update(now);
  return session_->frame(now, dt, input, out);
}

const std::string& OnlineMenu::localId() const {
  static const std::string none;
  return session_ ? session_->playerId() : none;
}
bool OnlineMenu::reconnecting() const { return session_ && session_->status() == SessionStatus::Reconnecting; }
bool OnlineMenu::closed() const { return !session_ || session_->status() == SessionStatus::Closed; }
std::optional<std::string> OnlineMenu::takeNotice() { return session_ ? session_->takeNotice() : std::nullopt; }
void OnlineMenu::choosePower(const std::string& id) { if (session_) session_->choosePower(id); }
void OnlineMenu::reroll() { if (session_) session_->reroll(); }
void OnlineMenu::special() { if (session_) session_->special(); }
void OnlineMenu::dash(Vec2 direction) { if (session_) session_->dash(direction); }
void OnlineMenu::signal(const char* kind, std::optional<Vec2> at) { if (session_) session_->signal(kind, at); }

void OnlineMenu::leave() {
  if (session_) {
    message_ = session_->status() == SessionStatus::Closed ? session_->failure() : "";
    session_->leave();
  }
  backHome();
}

// ---- Rendering -------------------------------------------------------------------------------

void OnlineMenu::renderMenu(sdl::BatchRenderer& b, float width, float height, float s) {
  b.rect(0, 0, width, height, kBg);
  b.text(width * 0.5f, height * 0.05f, "Jogar online", 52 * s, kGold, Align::Center);
  switch (page_) {
    case Page::Name:
    case Page::Code: renderText(b, width, height, s); break;
    case Page::Home: renderHome(b, width, height, s); break;
    case Page::Create: renderCreate(b, width, height, s); break;
    case Page::Connecting:
      b.text(width * 0.5f, height * 0.4f, "Conectando ao servidor...", 28 * s, kText, Align::Center);
      b.text(width * 0.5f, height * 0.4f + 40 * s, url_, 18 * s, kMuted, Align::Center);
      b.text(width * 0.5f, height * 0.4f + 80 * s, "B / Esc: cancelar", 18 * s, kMuted, Align::Center);
      break;
    case Page::Lobby: renderLobby(b, width, height, s); break;
    case Page::Playing: break;
  }
  if (!message_.empty()) b.textWrapped(width * 0.5f - 400 * s, height - 90 * s, 800 * s, message_, 18 * s, kDanger, 2, Align::Center);
}

void OnlineMenu::renderHome(sdl::BatchRenderer& b, float width, float height, float s) {
  const float w = 620 * s, x = width * 0.5f - w * 0.5f;
  float y = height * 0.17f;
  b.text(x, y, "Salas abertas", 24 * s, kText);
  y += 36 * s;
  const int rooms = visibleRooms();
  const char* status = !rooms_ ? "Servidor indisponível."
                     : !rooms_->error().empty() ? rooms_->error().c_str()
                     : !rooms_->loaded() ? "Buscando salas..."
                     : rooms == 0 ? "Nenhuma sala aberta agora. Crie uma!" : nullptr;
  if (status) { b.text(x + 20 * s, y + 8 * s, status, 20 * s, kMuted); y += 48 * s; }
  char label[96], value[96];
  for (int i = 0; i < rooms; ++i, y += 48 * s) {
    const auto& r = rooms_->rooms().rooms[static_cast<std::size_t>(i)];
    std::snprintf(label, sizeof label, "%s · %s", r.code.c_str(), r.host.c_str());
    std::snprintf(value, sizeof value, "%d/4 · %s%s", r.count, campaignTitle(r.campaign), r.running ? " · em jogo" : "");
    row(b, x, y, w, s, index_ == i, label, value);
  }
  y += 12 * s;
  const char* actions[4] = {"Criar sala", "Entrar com código", "Nome", "Voltar"};
  for (int i = 0; i < 4; ++i, y += 48 * s) row(b, x, y, w, s, index_ == rooms + i, actions[i], i == 2 ? name_.c_str() : nullptr);
  if (rooms_ && rooms_->loaded() && rooms_->error().empty()) {
    std::snprintf(label, sizeof label, "Servidor: %d/%d salas em uso", rooms_->rooms().used, rooms_->rooms().max);
    b.text(width * 0.5f, y + 8 * s, label, 16 * s, kMuted, Align::Center);
  }
}

void OnlineMenu::renderCreate(sdl::BatchRenderer& b, float width, float height, float s) {
  const float w = 620 * s, x = width * 0.5f - w * 0.5f;
  float y = height * 0.15f;
  b.text(width * 0.5f, y, "Criar sala", 28 * s, kText, Align::Center);
  y += 44 * s;
  row(b, x, y, w, s, index_ == 0, "Visibilidade", open_ ? "Aberta (aparece na lista)" : "Fechada (só com o código)");
  y += 46 * s;
  row(b, x, y, w, s, index_ == 1, "Ritual", kCampaignTitles[campaign_]);
  y += 46 * s;
  for (int i = 0; i < 6; ++i, y += 46 * s)
    row(b, x, y, w, s, index_ == 2 + i, kCurses[i].title, curses_[static_cast<std::size_t>(i)] ? "Ativa" : "—",
        curses_[static_cast<std::size_t>(i)] ? kDanger : 0);
  row(b, x, y, w, s, index_ == 8, "Criar sala");
  y += 46 * s;
  row(b, x, y, w, s, index_ == 9, "Voltar");
  if (index_ >= 2 && index_ < 8) b.text(width * 0.5f, y + 56 * s, kCurses[index_ - 2].desc, 18 * s, kMuted, Align::Center);
}

void OnlineMenu::renderText(sdl::BatchRenderer& b, float width, float height, float s) {
  const bool code = entry_.kind() == TextKind::RoomCode;
  b.text(width * 0.5f, height * 0.16f, code ? "Código da sala" : "Seu nome", 30 * s, kText, Align::Center);
  const float bw = 460 * s, bx = width * 0.5f - bw * 0.5f, by = height * 0.24f;
  b.rect(bx, by, bw, 56 * s, kPanel);
  b.frame(bx, by, bw, 56 * s, 2 * s, entry_.valid() ? kGold : kMuted);
  const bool blink = std::fmod(now_() / 500.0, 2.0) < 1.0;
  const std::string shown = entry_.value() + (blink ? "_" : " ");
  b.text(width * 0.5f, by + 10 * s, shown, 30 * s, kText, Align::Center);
  const int cols = TextEntry::kColumns;
  const float key = 54 * s, gap = 6 * s, gx = width * 0.5f - (cols * key + (cols - 1) * gap) * 0.5f, gy = by + 80 * s;
  for (int i = 0; i < entry_.keyCount(); ++i) {
    const float kx = gx + static_cast<float>(i % cols) * (key + gap), ky = gy + static_cast<float>(i / cols) * (key + gap);
    const bool sel = i == entry_.cursor();
    b.rect(kx, ky, key, key, sel ? kSel : kPanel);
    if (sel) b.frame(kx, ky, key, key, 3 * s, kGold);
    b.text(kx + key * 0.5f, ky + 14 * s, entry_.keyLabel(i), 20 * s, sel ? kText : kMuted, Align::Center);
  }
  b.text(width * 0.5f, height - 130 * s, "Teclado: digite · Enter confirma · Esc volta   |   Controle: A tecla · B volta", 16 * s, kMuted, Align::Center);
}

void OnlineMenu::renderLobby(sdl::BatchRenderer& b, float width, float height, float s) {
  const auto& lobby = session_->lobby();
  const float w = 620 * s, x = width * 0.5f - w * 0.5f;
  float y = height * 0.15f;
  char line[160];
  std::snprintf(line, sizeof line, "Sala %s · %s", session_->room().c_str(), lobby.visibility == "open" ? "aberta" : "fechada");
  b.text(width * 0.5f, y, line, 30 * s, kText, Align::Center);
  y += 40 * s;
  std::snprintf(line, sizeof line, "%s · %d maldição(ões)", campaignTitle(lobby.campaign), static_cast<int>(lobby.curses.size()));
  b.text(width * 0.5f, y, line, 20 * s, kMuted, Align::Center);
  y += 44 * s;
  for (const auto& p : lobby.players) {
    b.rect(x, y, w, 40 * s, kPanel);
    b.rect(x, y, 6 * s, 40 * s, native::playerColor(p.color));
    std::snprintf(line, sizeof line, "%s (%s)%s%s%s", p.name.c_str(), kCharacterNames[std::clamp(p.color, 0, 3)],
                  p.id == lobby.hostId ? " · anfitrião" : "", p.id == session_->playerId() ? " · você" : "", p.connected ? "" : " · desconectado");
    b.text(x + 20 * s, y + 8 * s, line, 20 * s, p.connected ? kText : kMuted);
    y += 46 * s;
  }
  y += 12 * s;
  row(b, x, y, w, s, index_ == 0, "Personagem", kCharacterNames[std::clamp(session_->color(), 0, 3)]);
  y += 48 * s;
  row(b, x, y, w, s, index_ == 1, session_->isHost() ? "Iniciar partida" : "Aguardando o anfitrião iniciar");
  y += 48 * s;
  row(b, x, y, w, s, index_ == 2, "Sair da sala");
}

} // namespace arcana::online
