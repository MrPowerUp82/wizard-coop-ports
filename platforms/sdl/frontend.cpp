#include "frontend.hpp"
#include "arcana/data.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <new>
#include <optional>

namespace arcana::sdl {
namespace {

using native::rgba;
using native::withAlpha;
using native::playerColor;
using Clock = std::chrono::steady_clock;

double msSince(Clock::time_point t) { return std::chrono::duration<double, std::milli>(Clock::now() - t).count(); }

struct CampaignEntry { const char* id; const char* title; const char* detail; };
constexpr CampaignEntry kCampaigns[] = {
  {"quick", "Ritual rápido", "6 fases de 2 minutos. Ideal para uma sessão curta."},
  {"classic", "Ritual clássico", "6 fases de 5 minutos, progressão mais lenta."},
  {"endless", "Ritual infinito", "As fases se repetem cada vez mais difíceis."},
};
// server/meta.js META_UPGRADES (costs live in src/profile.cpp).
struct UpgradeText { const char* id; const char* title; const char* desc; };
constexpr UpgradeText kUpgrades[] = {
  {"vigor", "Vigor", "+6 de vida máxima por grau (até +30)"},
  {"might", "Potência", "+3% de dano por grau (até +15%)"},
  {"celerity", "Celeridade", "Ataques 3% mais rápidos por grau (até 12%)"},
  {"stride", "Agilidade", "+4% de velocidade de movimento por grau (até +12%)"},
  {"ward", "Égide", "+0,5 de armadura por grau (até +2)"},
  {"reach", "Alcance", "+15 de raio de coleta por grau (até +45)"},
  {"wisdom", "Sabedoria", "+3% de experiência por grau (até +15%)"},
  {"greed", "Ganância", "+10% de moedas por grau (até +30%)"},
  {"channel", "Canalização", "Começa com +20% de carga especial por grau (até 60%)"},
  {"reroll", "Destino", "+1 troca de poderes por partida"},
  {"pact", "Pacto familiar", "Começa a partida com um Familiar arcano invocado"},
  {"phoenix", "Fênix", "Renasce uma vez por partida com 50% da vida"},
  {"arsenal", "Arsenal", "Desbloqueio: escolha a arma inicial antes da partida"},
  {"secondSpell", "Segundo feitiço", "Desbloqueio: um especial alternativo para cada personagem"},
  {"endless", "Ritual infinito", "Desbloqueio: os seis reinos se repetem cada vez mais difíceis"},
};
constexpr int kShopRows = static_cast<int>(std::size(kUpgrades)) + 1; // + respec
constexpr const char* kStartingWeapons[] = {"orbit", "aura", "chain", "runes", "familiar"};
constexpr const char* kAltSpecials[4] = {"Tempestade de granizo", "Égide flamejante", "Florescer", "Eclipse"};

// src/menu.js characterNames/characterEffects and server/weapons.js SPECIALS.
constexpr const char* kCharacterNames[4] = {"Azul", "Vermelho", "Verde", "Roxo"};
constexpr const char* kCharacterEffects[4] = {"Desacelera inimigos", "Explode em área", "Atravessa 3 inimigos", "Lâmina larga, até 2 alvos"};
constexpr const char* kCharacterSpecials[4] = {"Nova glacial", "Meteoro", "Jardim de espinhos", "Passo lunar"};

struct PowerText { const char* id; const char* desc; };
constexpr PowerText kPowerText[] = {
  {"arcane", "+25% de dano mágico"}, {"haste", "Ataques 12% mais rápidos"},
  {"vitality", "+22 de vida máxima e cura 30"}, {"swiftness", "+12% de velocidade"},
  {"multishot", "+1 projétil por ataque"}, {"magnet", "+55 de alcance de coleta"},
  {"armor", "Reduz o dano recebido"}, {"orbit", "Orbes giram ao seu redor e ferem quem tocam"},
  {"aura", "Queima inimigos próximos a cada meio segundo"}, {"chain", "Um raio salta entre inimigos próximos"},
  {"runes", "Deixa runas que explodem ao serem pisadas"}, {"familiar", "Invoca um espírito que caça inimigos com o seu elemento"},
  {"shatter", "Inimigos lentos explodem em 4 estilhaços ao morrer"}, {"burn", "Bolas de fogo deixam o chão queimando"},
  {"ricochet", "Espinhos saltam para o próximo inimigo"}, {"boomerang", "Lâminas voltam até você e ferem de novo"},
  {"bond", "Você e aliados próximos atacam 8% mais rápido"}, {"lifelink", "Aliados perto de você recuperam 2 de vida a cada 2s"},
  {"guardian", "Ressuscita aliados 50% mais rápido e com mais vida"},
  {"constellation", "Evolução: +2 orbes maiores com o dobro de dano"}, {"sanctuary", "Evolução: aura maior que desacelera e cura aliados"},
  {"tempest", "Evolução: raios a cada segundo saltando 8 vezes"}, {"minefield", "Evolução: três runas maiores por vez"},
  {"covenant", "Evolução: familiar ataca mais rápido, mais forte e em mais alvos"},
  {"avalanche", "Evolução: estilhaços dobram para 8 e ferem mais"}, {"hellfire", "Evolução: chão em chamas maior, mais longo e mais quente"},
  {"bramble", "Evolução: espinhos atravessam +2 inimigos e ferem 15% mais"}, {"fullmoon", "Evolução: lâminas voltam maiores e com 60% mais dano"},
  {"stormrunes", "Evolução: cada explosão de runa lança um raio em 3 inimigos"}, {"solarcrown", "Evolução: orbes incendeiam inimigos e ferem 30% mais"},
};

const char* powerDescription(const std::string& id) {
  for (const auto& p : kPowerText) if (id == p.id) return p.desc;
  return "";
}
const char* kindLabel(const std::string& kind) {
  if (kind == "weapon") return "ARMA";
  if (kind == "signature") return "ASSINATURA";
  if (kind == "coop") return "CO-OP";
  if (kind == "evolution") return "EVOLUÇÃO";
  return "PASSIVO";
}
// The core's data table keeps ASCII titles; the UI shows the accented names from the web version.
const char* powerTitle(const std::string& id) {
  struct T { const char* id; const char* title; };
  static constexpr T titles[] = {
    {"haste", "Cadência"}, {"multishot", "Disparo múltiplo"}, {"armor", "Armadura rúnica"}, {"shatter", "Estilhaço glacial"},
    {"burn", "Chão em chamas"}, {"bond", "Elo arcano"}, {"lifelink", "Vínculo vital"}, {"guardian", "Guardião"},
    {"constellation", "Constelação"}, {"sanctuary", "Santuário"},
  };
  for (const auto& t : titles) if (id == t.id) return t.title;
  const auto it = powerDefs().find(id);
  return it == powerDefs().end() ? id.c_str() : it->second.title.c_str();
}

std::uint32_t accentColor(int phase) {
  static constexpr std::uint32_t colors[6] = {rgba(131, 223, 170), rgba(140, 223, 255), rgba(255, 172, 112), rgba(184, 223, 124), rgba(255, 224, 155), rgba(211, 164, 255)};
  return colors[std::clamp(phase, 0, 5)];
}

constexpr const char* kIds[cfg::MAX_PLAYERS] = {"p1", "p2", "p3", "p4"};
constexpr std::uint32_t kPanel = rgba(10, 12, 22, 215);
constexpr std::uint32_t kText = rgba(236, 240, 255);
constexpr std::uint32_t kMuted = rgba(150, 160, 190);
constexpr std::uint32_t kGold = rgba(255, 214, 110);

void meter(BatchRenderer& b, float x, float y, float w, float h, double ratio, std::uint32_t fill) {
  b.rect(x, y, w, h, rgba(4, 6, 12, 220));
  b.rect(x + 1, y + 1, (w - 2) * static_cast<float>(std::clamp<double>(ratio, 0.0, 1.0)), h - 2, fill);
}

void formatClock(char* out, std::size_t n, double seconds) {
  const int s = std::max(0, static_cast<int>(std::ceil(seconds)));
  std::snprintf(out, n, "%d:%02d", s / 60, s % 60);
}

constexpr const char* kDeveloper = "MrPowerUp82";
constexpr double kSplashSeconds = 2.4;

std::uint8_t alpha8(float a) { return static_cast<std::uint8_t>(std::clamp(a, 0.0f, 1.0f) * 255); }
float ramp(double t, double from, double to) { return std::clamp(static_cast<float>((t - from) / (to - from)), 0.0f, 1.0f); }

// The MrPowerUp82 seal: rune circle, inscribed triangle, crescent over the axis and a four-point
// star. Drawn from primitives (no texture), so it stays crisp from the PSP's 480x272 up to 4K.
// `reveal` 0 -> 1 traces it in; `alpha` fades the whole seal.
void drawSeal(BatchRenderer& b, float cx, float cy, float r, float reveal, float alpha, double time) {
  constexpr float kPi = 3.14159265f, kTop = -kPi / 2;
  const float inner = ramp(reveal, 0.35, 0.8) * alpha, core = ramp(reveal, 0.6, 1.0) * alpha;
  const std::uint32_t blue = rgba(110, 130, 255, alpha8(0.9f * alpha)), pale = rgba(200, 212, 255, alpha8(inner));
  const float w = std::max(1.0f, r * 0.03f);

  b.setAdditive(true);
  b.glow(cx, cy, r * 1.7f, rgba(70, 60, 210), 0.5f * alpha * reveal);
  b.setAdditive(false);

  // Rings trace themselves from the top, in opposite directions.
  const float sweep = 2 * kPi * ramp(reveal, 0.0, 0.6);
  b.arc(cx, cy, r, r, kTop, kTop + sweep, blue, w);
  b.arc(cx, cy, r * 0.7f, r * 0.7f, kTop - sweep, kTop, blue, w * 0.8f);
  b.dashedCircle(cx, cy, r * 0.85f, r * 0.07f, r * 0.05f, static_cast<float>(time) * r * 0.08f, rgba(130, 120, 255, alpha8(0.6f * inner)), w * 1.8f);

  b.beginPath();
  for (int i = 0; i < 3; ++i) b.lineTo(cx + std::cos(kTop + i * 2 * kPi / 3) * r * 0.7f, cy + std::sin(kTop + i * 2 * kPi / 3) * r * 0.7f);
  b.stroke(w * 0.7f, pale, true);
  b.line(cx, cy - r * 1.15f, cx, cy + r * 1.15f, w * 0.6f, pale);

  // Crescent cradling a moon above, a sealed orb below.
  b.arc(cx, cy - r * 0.47f, r * 0.14f, r * 0.14f, kPi * 0.1f, kPi * 0.9f, pale, w * 1.6f);
  b.circle(cx, cy - r * 0.5f, r * 0.06f, pale, w * 0.8f);
  b.circle(cx, cy + r * 0.5f, r * 0.12f, pale, w * 0.8f);
  b.circle(cx, cy + r * 0.5f, r * 0.055f, pale);

  // Four-point star at the heart.
  b.setAdditive(true);
  b.glow(cx, cy, r * 0.45f, rgba(150, 170, 255), core);
  b.setAdditive(false);
  b.beginPath();
  for (int i = 0; i < 8; ++i) {
    const float a = kTop + i * kPi / 4;
    const float len = i % 2 ? r * 0.05f : (i % 4 == 0 ? r * 0.32f : r * 0.17f);
    b.lineTo(cx + std::cos(a) * len, cy + std::sin(a) * len);
  }
  b.fill(rgba(235, 240, 255, alpha8(core)));
}

const char* signalText(const std::string& kind) {
  // src/feedback.js SIGNAL_TEXT.
  if (kind == "here") return "venham aqui!";
  if (kind == "help") return "preciso de ajuda!";
  if (kind == "danger") return "cuidado!";
  if (kind == "look") return "olhem ali!";
  return "sinal";
}

} // namespace

Frontend::Frontend(FrontendOptions options) : options_(std::move(options)) {
  if (options_.autoplay) {
    for (int i = 0; i < cfg::MAX_PLAYERS; ++i) joined_[static_cast<std::size_t>(i)] = i < std::clamp(options_.autoplayPlayers, 1, cfg::MAX_PLAYERS);
  }
  for (int i = 0; i < 3; ++i) if (options_.campaign == kCampaigns[i].id) campaign_ = i;
  if (options_.startImmediately || options_.autoplay) startRun();
  else if (options_.openShop) screen_ = Screen::Shop;
  else if (options_.openCredits) screen_ = Screen::Credits;
  else if (options_.splash) screen_ = Screen::Splash;
}

bool Frontend::anyPressed(Action a) const {
  for (int i = 0; i < cfg::MAX_PLAYERS; ++i) if (pressed(i, a)) return true;
  return false;
}

void Frontend::readEdges(const InputFrame& input) {
  for (std::size_t i = 0; i < edges_.size(); ++i) {
    const auto& pad = input.pads[i];
    std::uint32_t held = pad.connected ? pad.held : 0;
    // Sticks double as menu directions.
    if (pad.y < -0.6f) held |= ActUp;
    if (pad.y > 0.6f) held |= ActDown;
    if (pad.x < -0.6f) held |= ActLeft;
    if (pad.x > 0.6f) held |= ActRight;
    edges_[i].pressed = held & ~edges_[i].held;
    edges_[i].held = held;
  }
}

Player* Frontend::playerForSlot(int slot) {
  const std::string& id = localIds_[static_cast<std::size_t>(slot)];
  if (id.empty()) return nullptr;
  const auto it = state_.players.find(id);
  return it == state_.players.end() ? nullptr : &it->second;
}

Player* Frontend::chooser(int* slotOut) {
  // Same rule as the web client's chooserOf(): the first living local player with choices decides.
  for (int slot = 0; slot < cfg::MAX_PLAYERS; ++slot) {
    Player* p = playerForSlot(slot);
    if (p && p->alive && !p->pendingPowers.empty()) { if (slotOut) *slotOut = slot; return p; }
  }
  return nullptr;
}

void Frontend::startRun() {
  depositRun(); // leaving a run early (restart) still banks its coins
  // Rebuild in place: GameState is ~200 KB and an assignment from a temporary would put a second
  // copy on the (small, on Vita) main-thread stack. Prvalue + placement new is guaranteed elision.
  state_.~GameState();
  new (&state_) GameState(createGameState(kCampaigns[std::clamp(campaign_, 0, 2)].id));
  // Permanent upgrades apply to everyone; the loadout is player 1's, as in the web client.
  const Loadout loadout{weapon_, special_};
  static constexpr const char* names[4] = {"Arcanista 1", "Arcanista 2", "Arcanista 3", "Arcanista 4"};
  for (int slot = 0; slot < cfg::MAX_PLAYERS; ++slot) {
    if (!joined_[static_cast<std::size_t>(slot)]) continue;
    Player p = createPlayer(kIds[slot], names[slot], character_[static_cast<std::size_t>(slot)], &profile_.upgrades, slot == 0 ? &loadout : nullptr);
    p.x = (slot % 2 ? 60 : -60) * (slot > 0 ? 1 : 0);
    p.y = (slot >= 2 ? 60 : 0);
    state_.players[p.id] = std::move(p);
  }
  onlineMatch_ = false;
  localIds_ = {kIds[0], kIds[1], kIds[2], kIds[3]};
  resetRunView();
  profile_.prefs.characters = character_;
  profile_.prefs.campaign = kCampaigns[std::clamp(campaign_, 0, 2)].id;
  profile_.prefs.weapon = weapon_;
  profile_.prefs.special = special_;
  saveProfileNow();
  screen_ = Screen::Playing;
}

void Frontend::resetRunView() {
  clock_.reset();
  choiceKey_.clear();
  choiceIndex_ = 0;
  camera_ = {};
  camera_.zoom = 0; // snap on the first rendered frame
  anim_.reset();
  announce_ = {}; toast_ = {};
  feedbackPrimed_ = false;
  feedbackEventId_ = 0;
  hurtFlash_ = 0;
  lastHp_.fill(-1);
  sampled_ = {};
  hazardCount_ = 0;
  overSoundPlayed_ = false;
  deposited_ = false;
  earned_ = 0;
  overTime_ = 0;
}

bool Frontend::update(double frameSeconds, const InputFrame& rawInput) {
  InputFrame input = rawInput;
  if (options_.autoplay && inGame()) autoplayInput(input);
  readEdges(input);
  menuTime_ += frameSeconds;
  fpsAccum_ += frameSeconds; ++fpsFrames_;
  if (fpsAccum_ >= 0.5) { fps_ = fpsFrames_ / fpsAccum_; fpsAccum_ = 0; fpsFrames_ = 0; }
  if (anyPressed(ActDebug)) options_.showPerf = !options_.showPerf;

  switch (screen_) {
    case Screen::Splash: updateSplash(frameSeconds); break;
    case Screen::Credits:
      if (pressed(0, ActConfirm) || pressed(0, ActCancel) || pressed(0, ActPause)) { screen_ = Screen::Title; sfx(Sound::Click); }
      break;
    case Screen::Title:
      updateTitle();
      if (quitRequested_) return false;
      break;
    case Screen::Shop: updateShop(); announce_.age += frameSeconds; toast_.age += frameSeconds; break;
    case Screen::Online: updateOnlineMenu(input); announce_.age += frameSeconds; toast_.age += frameSeconds; break;
    case Screen::Playing: updatePlaying(frameSeconds, input); break;
    case Screen::Paused: updatePaused(); break;
    case Screen::Over: updateOver(); break;
  }
  updateMusic();
  if (inGame() || screen_ == Screen::Over) {
    // Animation time stops while paused or choosing a power, exactly like the web client (online the
    // match keeps running on the server, so it never stops there).
    anim_.update(state_, frameSeconds, screen_ == Screen::Paused || (screen_ == Screen::Playing && chooser() && !onlineMatch_));
    if (anim_.hits() > 0) sfx(Sound::Hit);
    if (anim_.kills() > 0) sfx(Sound::Kill);
    announce_.age += frameSeconds;
    toast_.age += frameSeconds;
    hurtFlash_ = std::max<double>(0.0, hurtFlash_ - frameSeconds);
  }
  return true;
}

native::StaticVector<Frontend::TitleItem, 8> Frontend::titleItems() const {
  native::StaticVector<TitleItem, 8> items;
  items.push_back(TitleItem::Play);
  if (online_) items.push_back(TitleItem::Online);
  items.push_back(TitleItem::Campaign);
  if (unlocked("arsenal")) items.push_back(TitleItem::Weapon);
  if (unlocked("secondSpell")) items.push_back(TitleItem::Special);
  items.push_back(TitleItem::Shop);
  items.push_back(TitleItem::Credits);
  items.push_back(TitleItem::Quit);
  return items;
}

bool Frontend::unlocked(const char* id) const {
  const auto it = profile_.upgrades.rank.find(id);
  return it != profile_.upgrades.rank.end() && it->second > 0;
}

void Frontend::cycleCampaign() {
  // The endless ritual is sold in the Grimório (server/meta.js `endless` unlock).
  do campaign_ = (campaign_ + 1) % 3; while (campaign_ == 2 && !unlocked("endless"));
}

void Frontend::updateSplash(double frameSeconds) {
  splashTime_ += frameSeconds;
  bool skip = false;
  for (const auto& e : edges_) skip |= (e.pressed & (ActConfirm | ActCancel | ActPause)) != 0;
  if (skip || splashTime_ >= kSplashSeconds) screen_ = Screen::Title;
}

void Frontend::updateTitle() {
  const auto items = titleItems();
  const int count = static_cast<int>(items.size());
  menuIndex_ = std::clamp(menuIndex_, 0, count - 1);
  if (pressed(0, ActUp)) { menuIndex_ = (menuIndex_ + count - 1) % count; sfx(Sound::Click); }
  if (pressed(0, ActDown)) { menuIndex_ = (menuIndex_ + 1) % count; sfx(Sound::Click); }
  for (int slot = 1; slot < (options_.compact ? 1 : cfg::MAX_PLAYERS); ++slot) {
    if (pressed(slot, ActConfirm) && !joined_[static_cast<std::size_t>(slot)]) {
      joined_[static_cast<std::size_t>(slot)] = true;
      if (characterTaken(slot, character_[static_cast<std::size_t>(slot)])) cycleCharacter(slot, 1, true);
      sfx(Sound::Signal);
    }
    if (joined_[static_cast<std::size_t>(slot)] && pressed(slot, ActLeft)) cycleCharacter(slot, -1);
    if (joined_[static_cast<std::size_t>(slot)] && pressed(slot, ActRight)) cycleCharacter(slot, 1);
    if (pressed(slot, ActCancel)) joined_[static_cast<std::size_t>(slot)] = false;
  }
  if (pressed(0, ActLeft)) cycleCharacter(0, -1);
  if (pressed(0, ActRight)) cycleCharacter(0, 1);
  if (!pressed(0, ActConfirm)) return;
  sfx(Sound::Click);
  switch (items[static_cast<std::size_t>(menuIndex_)]) {
    case TitleItem::Play: startRun(); break;
    case TitleItem::Online: enterOnline(); break;
    case TitleItem::Campaign: cycleCampaign(); break;
    case TitleItem::Weapon: {
      // Nenhuma -> each starting weapon -> Nenhuma.
      int index = -1;
      for (int i = 0; i < 5; ++i) if (weapon_ == kStartingWeapons[i]) index = i;
      weapon_ = index + 1 < 5 ? kStartingWeapons[index + 1] : "";
      break;
    }
    case TitleItem::Special: special_ = 1 - special_; break;
    case TitleItem::Shop: screen_ = Screen::Shop; shopIndex_ = 0; respecArmed_ = false; break;
    case TitleItem::Credits: screen_ = Screen::Credits; break;
    case TitleItem::Quit: quitRequested_ = true; break;
  }
}

void Frontend::updateShop() {
  if (pressed(0, ActCancel) || pressed(0, ActPause)) { screen_ = Screen::Title; sfx(Sound::Click); return; }
  if (pressed(0, ActUp)) { shopIndex_ = (shopIndex_ + kShopRows - 1) % kShopRows; respecArmed_ = false; sfx(Sound::Click); }
  if (pressed(0, ActDown)) { shopIndex_ = (shopIndex_ + 1) % kShopRows; respecArmed_ = false; sfx(Sound::Click); }
  if (!pressed(0, ActConfirm)) return;
  if (shopIndex_ == kShopRows - 1) {
    // Refunds are one press away from wiping every upgrade: ask twice.
    if (profile_.invested <= 0) { sfx(Sound::Warning); return; }
    if (!respecArmed_) { respecArmed_ = true; sfx(Sound::Warning); return; }
    const int refund = respec(profile_);
    respecArmed_ = false;
    if (campaign_ == 2) campaign_ = 0;
    weapon_.clear(); special_ = 0;
    saveProfileNow();
    std::snprintf(scratch_, sizeof scratch_, "%d moedas devolvidas ao Grimório", refund);
    announce(scratch_, kGold, true);
    sfx(Sound::Coin);
    return;
  }
  if (buyUpgrade(profile_, kUpgrades[shopIndex_].id)) {
    saveProfileNow();
    sfx(Sound::Chest);
  } else {
    sfx(Sound::Warning);
  }
}

void Frontend::depositRun() {
  // Local players share coins in a run; bank them once (src/main.js depositCoins).
  if (deposited_ || state_.players.empty() || options_.autoplay) return;
  deposited_ = true;
  int coins = 0;
  if (onlineMatch_) { if (const Player* me = playerForSlot(0)) coins = me->coins; } // online: only your own coins
  else for (const auto& [_, p] : state_.players) coins = std::max(coins, p.coins);
  earned_ = coins;
  deposit(profile_, coins);
  observeCodex(profile_, state_, playerForSlot(0));
  saveProfileNow();
}

void Frontend::saveProfileNow() {
  if (!profilePath_.empty()) saveProfileAtomic(profile_, profilePath_);
}

void Frontend::setProfilePath(std::string path) {
  profilePath_ = std::move(path);
  profile_ = loadProfile(profilePath_);
  const auto& prefs = profile_.prefs;
  character_ = prefs.characters;
  // Saves edited by hand (or from older versions) may repeat characters: keep them distinct.
  for (int slot = 1; slot < cfg::MAX_PLAYERS; ++slot)
    for (int other = 0; other < slot; ++other)
      if (character_[static_cast<std::size_t>(other)] == character_[static_cast<std::size_t>(slot)]) {
        for (int c = 0; c < 4; ++c)
          if (std::none_of(character_.begin(), character_.begin() + slot, [&](int used) { return used == c; })) { character_[static_cast<std::size_t>(slot)] = c; break; }
      }
  for (int i = 0; i < 3; ++i) if (prefs.campaign == kCampaigns[i].id) campaign_ = i;
  if (campaign_ == 2 && !unlocked("endless")) campaign_ = 0;
  weapon_ = unlocked("arsenal") ? prefs.weapon : "";
  special_ = unlocked("secondSpell") ? prefs.special : 0;
  if (audio_) audio_->setMuted(prefs.muted);
}

void Frontend::setAudio(Audio* audio) {
  audio_ = audio;
  if (audio_ && !profilePath_.empty()) audio_->setMuted(profile_.prefs.muted);
}

bool Frontend::characterTaken(int slot, int character) const {
  for (int other = 0; other < cfg::MAX_PLAYERS; ++other)
    if (other != slot && joined_[static_cast<std::size_t>(other)] && character_[static_cast<std::size_t>(other)] == character) return true;
  return false;
}

void Frontend::cycleCharacter(int slot, int direction, bool includeCurrent) {
  // Like the web picker: characters in use by another local player are skipped.
  int& current = character_[static_cast<std::size_t>(slot)];
  for (int step = includeCurrent ? 0 : 1; step < 4; ++step) {
    const int candidate = ((current + direction * step) % 4 + 4) % 4;
    if (characterTaken(slot, candidate)) continue;
    if (candidate != current) sfx(Sound::Click);
    current = candidate;
    return;
  }
}

void Frontend::updatePlaying(double frameSeconds, const InputFrame& input) {
  if (onlineMatch_) { updateOnlinePlaying(frameSeconds, input); return; }
  if (anyPressed(ActPause)) { screen_ = Screen::Paused; pauseIndex_ = 0; return; }

  int chooserSlot = -1;
  if (Player* c = chooser(&chooserSlot)) {
    for (auto& [_, p] : state_.players) p.input = {};
    updateChooser(*c, chooserSlot);
    clock_.reset();
    return;
  }

  for (int slot = 0; slot < cfg::MAX_PLAYERS; ++slot) {
    Player* p = playerForSlot(slot);
    if (!p) continue;
    const auto& pad = input.pads[static_cast<std::size_t>(slot)];
    p->input.x = p->alive ? pad.x : 0;
    p->input.y = p->alive ? pad.y : 0;
    if (!p->alive) continue;
    if (pressed(slot, ActSpecial)) activateSpecial(state_, p->id, random_);
    if (pressed(slot, ActDash)) {
      const bool still = pad.x == 0 && pad.y == 0;
      activateDash(state_, p->id, still ? Vec2{p->moveX, p->moveY} : Vec2{pad.x, pad.y});
    }
  }

  const auto t0 = Clock::now();
  timings_.steps = clock_.advance(frameSeconds, [&](double dt) {
    if (state_.over || chooser()) return;
    updateGame(state_, dt, random_);
  });
  timings_.updateMs = msSince(t0);
  leashPlayers();
  observeEvents();

  if (state_.over) {
    overTime_ += frameSeconds;
    if (overTime_ > 1.2) { screen_ = Screen::Over; pauseIndex_ = 0; depositRun(); }
  } else {
    overTime_ = 0;
  }
}

void Frontend::leashPlayers() {
  // Shared-screen co-op: nobody may leave the view the camera can show at its widest zoom.
  // Half extents match renderWorld()'s minimum zoom (0.55 of a 720p-tall 16:9 view).
  constexpr real halfW = 1280.0 / (2 * 0.55) - 70, halfH = 720.0 / (2 * 0.55) - 80;
  real minX = 1e30, maxX = -1e30, minY = 1e30, maxY = -1e30;
  int alive = 0;
  for (const auto& [_, p] : state_.players) {
    minX = std::min(minX, p.x); maxX = std::max(maxX, p.x); minY = std::min(minY, p.y); maxY = std::max(maxY, p.y);
    alive += p.alive ? 1 : 0;
  }
  if (state_.players.size() < 2 || alive == 0) return;
  const real cx = (minX + maxX) * 0.5, cy = (minY + maxY) * 0.5;
  for (auto& [_, p] : state_.players) {
    p.x = std::clamp(p.x, cx - halfW, cx + halfW);
    p.y = std::clamp(p.y, cy - halfH, cy + halfH);
  }
}

void Frontend::updateChooser(Player& p, int slot) {
  std::string key;
  for (const auto& id : p.pendingPowers) { key += id; key += ','; }
  if (key != choiceKey_) { choiceKey_ = key; choiceIndex_ = 0; }
  const int count = static_cast<int>(p.pendingPowers.size());
  if (pressed(slot, ActLeft) || pressed(slot, ActUp)) { choiceIndex_ = (choiceIndex_ + count - 1) % count; sfx(Sound::Click); }
  if (pressed(slot, ActRight) || pressed(slot, ActDown)) { choiceIndex_ = (choiceIndex_ + 1) % count; sfx(Sound::Click); }
  if (pressed(slot, ActAlt)) {
    int players = 0;
    for (bool j : joined_) players += j ? 1 : 0;
    if (rerollPowers(p, random_, players > 1)) sfx(Sound::Click);
    return;
  }
  if (pressed(slot, ActConfirm) || (options_.autoplay && menuTime_ > 0)) {
    const std::string id = p.pendingPowers[static_cast<std::size_t>(std::clamp(choiceIndex_, 0, count - 1))];
    applyPower(p, id);
    sfx(Sound::Power);
    choiceKey_.clear();
  }
}

void Frontend::updatePaused() {
  constexpr int items = 4;
  if (pressed(0, ActUp)) { pauseIndex_ = (pauseIndex_ + items - 1) % items; sfx(Sound::Click); }
  if (pressed(0, ActDown)) { pauseIndex_ = (pauseIndex_ + 1) % items; sfx(Sound::Click); }
  if (anyPressed(ActPause) || anyPressed(ActCancel)) { screen_ = Screen::Playing; clock_.reset(); return; }
  if (!anyPressed(ActConfirm)) return;
  if (pauseIndex_ == 0) { screen_ = Screen::Playing; clock_.reset(); }
  else if (pauseIndex_ == 1) startRun();
  else if (pauseIndex_ == 2) {
    if (audio_) { audio_->setMuted(!audio_->muted()); profile_.prefs.muted = audio_->muted(); saveProfileNow(); sfx(Sound::Click); }
  }
  else { depositRun(); screen_ = Screen::Title; }
}

void Frontend::updateOver() {
  if (onlineMatch_) {
    if (options_.autoplay || anyPressed(ActConfirm) || anyPressed(ActCancel)) leaveOnlineMatch();
    return;
  }
  if (options_.autoplay) { startRun(); return; }
  if (anyPressed(ActConfirm)) startRun();
  else if (anyPressed(ActCancel)) screen_ = Screen::Title;
}

void Frontend::autoplayInput(InputFrame& input) {
  // Bots kite: flee the local crowd, drift in a circle, grab nearby gems. Enough to soak-test
  // every system (levels, powers, bosses, specials) without a controller.
  int slot = 0;
  for (int i = 0; i < cfg::MAX_PLAYERS; ++i) {
    auto& pad = input.pads[static_cast<std::size_t>(i)];
    Player* p = playerForSlot(i);
    if (!p) continue;
    pad.connected = true;
    if (options_.debugCharge && p->specialCooldown <= 0) p->specialCharge = cfg::SPECIAL_MAX;
    const double t = state_.time * 0.6 + slot++ * 1.7;
    double x = std::cos(t) * 0.6, y = std::sin(t) * 0.6;
    for (const auto& e : state_.enemies) {
      const double dx = p->x - e.x, dy = p->y - e.y, d2 = dx * dx + dy * dy;
      if (d2 < 260.0 * 260.0 && d2 > 1) { const double w = 9000.0 / d2; x += dx * w / 100; y += dy * w / 100; }
    }
    const double len = std::hypot(x, y);
    if (len > 1) { x /= len; y /= len; }
    pad.x = static_cast<float>(x); pad.y = static_cast<float>(y);
    pad.held = p->specialCharge >= cfg::SPECIAL_MAX && std::fmod(state_.time, 1.0) < 0.5 ? static_cast<std::uint32_t>(ActSpecial) : 0u;
  }
}

void Frontend::setOnline(std::unique_ptr<OnlinePort> online) {
  online_ = std::move(online);
  if (online_ && options_.startOnline) enterOnline();
}

bool Frontend::wantsTextInput() const { return screen_ == Screen::Online && online_ && online_->wantsText(); }

void Frontend::enterOnline() {
  screen_ = Screen::Online;
  online_->openMenu(profile_);
}

void Frontend::updateOnlineMenu(const InputFrame& input) {
  const std::string name = profile_.prefs.name;
  const auto result = online_->updateMenu(edges_[0].pressed, edges_[0].held, input.text, profile_);
  if (profile_.prefs.name != name) saveProfileNow();
  if (result == OnlinePort::MenuResult::Title) { screen_ = Screen::Title; sfx(Sound::Click); }
  else if (result == OnlinePort::MenuResult::Play) startOnlineRun();
}

void Frontend::startOnlineRun() {
  state_.~GameState();
  new (&state_) GameState();
  onlineMatch_ = true;
  onlineMenuOpen_ = false;
  sentChoiceKey_.clear();
  localIds_ = {online_->localId(), "", "", ""};
  resetRunView();
  screen_ = Screen::Playing;
}

void Frontend::leaveOnlineMatch() {
  depositRun();
  online_->leave();
  onlineMatch_ = false;
  onlineMenuOpen_ = false;
  screen_ = Screen::Online;
}

void Frontend::updateOnlinePlaying(double frameSeconds, const InputFrame& input) {
  // No pause online: Esc/Start opens a small menu over the match, which keeps running.
  if (anyPressed(ActPause)) { onlineMenuOpen_ = !onlineMenuOpen_; pauseIndex_ = 0; sfx(Sound::Click); }
  else if (onlineMenuOpen_) {
    if (pressed(0, ActUp) || pressed(0, ActDown)) { pauseIndex_ = 1 - pauseIndex_; sfx(Sound::Click); }
    if (pressed(0, ActCancel)) onlineMenuOpen_ = false;
    else if (pressed(0, ActConfirm)) {
      if (pauseIndex_ == 1) { leaveOnlineMatch(); return; }
      onlineMenuOpen_ = false;
    }
  }
  const auto& pad = input.pads[0];
  // `real` is float on the PSP build: keep brace-init free of double -> float narrowing.
  Vec2 move{onlineMenuOpen_ ? real{0} : static_cast<real>(pad.x), onlineMenuOpen_ ? real{0} : static_cast<real>(pad.y)};
  if (const Player* me = playerForSlot(0); me && me->alive && !onlineMenuOpen_ && !state_.over) {
    if (!me->pendingPowers.empty()) {
      updateOnlineChooser(*me);
      move = {};
    } else {
      sentChoiceKey_.clear();
      if (pressed(0, ActSpecial)) online_->special();
      if (pressed(0, ActDash)) online_->dash(pad.x == 0 && pad.y == 0 ? Vec2{me->moveX, me->moveY} : Vec2{pad.x, pad.y});
      sendSignals(*me);
    }
  }
  const auto t0 = Clock::now();
  online_->frame(frameSeconds, move, state_);
  timings_.updateMs = msSince(t0);
  timings_.steps = 0;
  observeEvents();
  if (auto notice = online_->takeNotice()) announce(*notice, kMuted, true);
  if (online_->closed()) { leaveOnlineMatch(); return; }
  if (state_.over) {
    overTime_ += frameSeconds;
    if (overTime_ > 1.2) { screen_ = Screen::Over; depositRun(); }
  } else {
    overTime_ = 0;
  }
}

void Frontend::updateOnlineChooser(const Player& me) {
  std::string key;
  for (const auto& id : me.pendingPowers) { key += id; key += ','; }
  if (key != choiceKey_) { choiceKey_ = key; choiceIndex_ = 0; }
  if (key == sentChoiceKey_) return; // already answered: waiting for the server to apply it
  const int count = static_cast<int>(me.pendingPowers.size());
  if (pressed(0, ActLeft) || pressed(0, ActUp)) { choiceIndex_ = (choiceIndex_ + count - 1) % count; sfx(Sound::Click); }
  if (pressed(0, ActRight) || pressed(0, ActDown)) { choiceIndex_ = (choiceIndex_ + 1) % count; sfx(Sound::Click); }
  if (pressed(0, ActAlt)) {
    if (me.rerolls > 0) { online_->reroll(); sentChoiceKey_ = key; sfx(Sound::Click); }
    return;
  }
  if (pressed(0, ActConfirm) || options_.autoplay) {
    online_->choosePower(me.pendingPowers[static_cast<std::size_t>(std::clamp(choiceIndex_, 0, count - 1))]);
    sentChoiceKey_ = key;
    sfx(Sound::Power);
  }
}

void Frontend::sendSignals(const Player& me) {
  // Keyboard Q/E/X/C, or hold Alt (X/Y on a gamepad) and press a direction (web: Q/E/X and a click).
  const char* kind = nullptr;
  if (pressed(0, ActSignalHere)) kind = "here";
  else if (pressed(0, ActSignalHelp)) kind = "help";
  else if (pressed(0, ActSignalDanger)) kind = "danger";
  else if (pressed(0, ActSignalLook)) kind = "look";
  else if (edges_[0].held & ActAlt) {
    if (pressed(0, ActUp)) kind = "here";
    else if (pressed(0, ActLeft)) kind = "help";
    else if (pressed(0, ActRight)) kind = "danger";
    else if (pressed(0, ActDown)) kind = "look";
  }
  if (!kind) return;
  std::optional<Vec2> at;
  if (std::strcmp(kind, "look") == 0) {
    // There is no mouse click in the world: "look there" points 300 units ahead of where you face.
    const real len = std::max<real>(real(1e-6), std::hypot(me.moveX, me.moveY));
    at = Vec2{me.x + me.moveX / len * 300, me.y + me.moveY / len * 300};
  }
  online_->signal(kind, at);
  sfx(Sound::Signal);
}

native::StaticVector<const Player*, cfg::MAX_PLAYERS> Frontend::hudPlayers() const {
  native::StaticVector<const Player*, cfg::MAX_PLAYERS> list;
  auto find = [&](const std::string& id) -> const Player* {
    if (id.empty()) return nullptr;
    const auto it = state_.players.find(id);
    return it == state_.players.end() ? nullptr : &it->second;
  };
  if (!onlineMatch_) {
    for (const auto& id : localIds_) list.push_back(find(id)); // fixed corners per local slot
    return list;
  }
  // Online: you first, then allies by character.
  list.push_back(find(localIds_[0]));
  for (int c = 0; c < 4; ++c)
    for (const auto& [id, p] : state_.players)
      if (id != localIds_[0] && p.color == c && !list.full()) list.push_back(&p);
  return list;
}

void Frontend::renderOnlineOverlay(BatchRenderer& b, float width, float height) {
  const float s = uiScale(height);
  if (online_->reconnecting()) {
    const char* text = "Reconectando ao servidor...";
    const float w = b.textWidth(text, 22 * s) + 40 * s;
    b.rect(width * 0.5f - w * 0.5f, height * 0.5f - 24 * s, w, 48 * s, rgba(8, 10, 20, 220));
    b.text(width * 0.5f, height * 0.5f - 12 * s, text, 22 * s, kGold, Align::Center);
  }
  if (!onlineMenuOpen_) return;
  b.rect(0, 0, width, height, rgba(0, 0, 0, 140));
  b.text(width * 0.5f, height * 0.28f, "A partida continua", 40 * s, kText, Align::Center);
  const char* items[2] = {"Continuar", "Sair da sala"};
  for (int i = 0; i < 2; ++i) {
    const float y = height * 0.4f + i * 60 * s;
    const bool sel = i == pauseIndex_;
    b.rect(width * 0.5f - 200 * s, y, 400 * s, 50 * s, sel ? rgba(40, 44, 80, 240) : rgba(18, 20, 36, 220));
    if (sel) b.frame(width * 0.5f - 200 * s, y, 400 * s, 50 * s, 3 * s, kGold);
    b.text(width * 0.5f, y + 11 * s, items[i], 24 * s, sel ? kText : kMuted, Align::Center);
  }
}

// ---------------------------------------------------------------------------------------------
// Rendering

void Frontend::render(BatchRenderer& batch, float width, float height) {
  lastBatch_ = batch.stats(); // previous frame: this one is still being built
  batch.begin();
  if (screen_ == Screen::Splash) {
    renderSplash(batch, width, height);
  } else if (screen_ == Screen::Title) {
    renderTitle(batch, width, height);
  } else if (screen_ == Screen::Credits) {
    renderCredits(batch, width, height);
  } else if (screen_ == Screen::Shop) {
    renderShop(batch, width, height);
  } else if (screen_ == Screen::Online) {
    online_->renderMenu(batch, width, height, uiScale(height));
    renderFeedback(batch, width, height);
  } else {
    renderWorld(batch, width, height);
    renderHud(batch, width, height);
    renderFeedback(batch, width, height);
    if (screen_ == Screen::Playing && chooser()) renderChooser(batch, width, height);
    if (screen_ == Screen::Playing && onlineMatch_) renderOnlineOverlay(batch, width, height);
    if (screen_ == Screen::Paused) renderPause(batch, width, height);
    if (screen_ == Screen::Over) renderOver(batch, width, height);
  }
  if (options_.showPerf) renderPerf(batch, width, height);
  batch.flush();
}

void Frontend::renderWorld(BatchRenderer& b, float width, float height) {
  // Shared co-op camera: frame every player (downed ones too, so allies can find and revive them).
  real minX = 1e30, maxX = -1e30, minY = 1e30, maxY = -1e30;
  bool any = false;
  for (const auto& [_, p] : state_.players) {
    if (onlineMatch_ && p.id != localIds_[0]) continue; // online: the camera follows you only
    minX = std::min(minX, p.x); maxX = std::max(maxX, p.x); minY = std::min(minY, p.y); maxY = std::max(maxY, p.y);
    any = true;
  }
  if (!any) minX = maxX = minY = maxY = 0;
  const float base = height / 720.0f * (options_.compact ? 1.2f : 1.0f);
  const float want = std::clamp(std::min(width / static_cast<float>(maxX - minX + 560), height / static_cast<float>(maxY - minY + 400)), base * 0.55f, base);
  const float targetX = static_cast<float>((minX + maxX) * 0.5), targetY = static_cast<float>((minY + maxY) * 0.5);
  if (camera_.zoom <= 0) { camera_.zoom = want; camera_.x = targetX; camera_.y = targetY; }
  camera_.x += (targetX - camera_.x) * 0.2f;
  camera_.y += (targetY - camera_.y) * 0.2f;
  camera_.zoom += (want - camera_.zoom) * 0.05f;

  const auto t0 = Clock::now();
  drawWorld(b, state_, anim_, WorldView{width, height, camera_.x, camera_.y, camera_.zoom});
  buildMs_ = msSince(t0);
}

void Frontend::announce(std::string text, std::uint32_t color, bool toast) {
  Banner& banner = toast ? toast_ : announce_;
  banner.text = std::move(text);
  banner.color = color;
  banner.age = 0;
  banner.life = toast ? 3.0 : 2.8;
}

void Frontend::observeEvents() {
  // Port of src/feedback.js: simulation events become announcements and toasts.
  static constexpr std::uint32_t danger = rgba(255, 107, 94), gold = rgba(255, 211, 107);
  if (!feedbackPrimed_) {
    for (const auto& e : state_.events) feedbackEventId_ = std::max(feedbackEventId_, e.id);
    feedbackPrimed_ = true;
  }
  const auto& def = phases()[static_cast<std::size_t>(std::clamp(state_.phase, 0, 5))];
  for (const auto& e : state_.events) {
    if (e.id <= feedbackEventId_) continue;
    feedbackEventId_ = e.id;
    const std::string& k = e.kind;
    if (k == "boss") announce(def.bossName + " despertou!", danger);
    else if (k == "stage") announce(e.stage == 3 ? "Fúria final do guardião!" : "O guardião entrou em fúria!", danger);
    else if (k == "bossDown") announce(def.bossName + " caiu!", gold);
    else if (k == "elite") announce("Uma elite surgiu - derrote-a para ganhar um baú", gold);
    else if (k == "ring") announce("Enxame! Abra caminho", danger);
    else if (k == "chest") announce("Baú compartilhado: todos recebem um poder", gold, true);
    else if (k == "altar") announce("Altar opcional: defenda por 15s para ganhar um poder", gold);
    else if (k == "altarComplete") announce("Altar purificado! Poder e moedas para todos", gold);
    else if (k == "altarExpired") announce("O altar se apagou. A campanha continua.", kMuted, true);
    else if (k == "combo" && e.variant == 1) announce("Combo em equipe! Especiais carregados", gold, true);
    else if (k == "convergence") announce("Convergência!", gold);
    else if (k == "encounter" && state_.encounter) {
      const auto& kind = state_.encounter->kind;
      announce(kind == "merchant" ? "Um mercador errante chegou - troque moedas por um poder"
             : kind == "shrine" ? "Um santuário amaldiçoado oferece um pacto" : "Um ladrão fugiu com um baú - alcance-o!", gold);
    }
    else if (k == "merchantSale") announce("Negócio fechado: escolha um poder", gold, true);
    else if (k == "shrineAccepted") announce("Pacto aceito! Poder para todos - inimigos mais fortes neste reino", danger);
    else if (k == "thiefDown") announce("Ladrão derrubado! O tesouro é seu", gold);
    else if (k == "thiefEscaped") announce("O ladrão escapou com o tesouro.", kMuted, true);
    else if (k == "loop") { std::snprintf(scratch_, sizeof scratch_, "Volta %d: os reinos despertam mais fortes", state_.loop + 1); announce(scratch_, danger); }
    else if (k == "phoenix") announce("Fênix! Um arcanista renasceu", gold);
    else if (k == "signal" && onlineMatch_ && e.player != localIds_[0]) {
      std::snprintf(scratch_, sizeof scratch_, "%s: %s", e.name.c_str(), signalText(e.text));
      announce(scratch_, playerColor(e.color), true);
    }
    playEventSound(e);
  }
  // src/feedback.js: hazards, and per-player changes (split-screen players share one sound per frame).
  if (state_.hazards.size() > hazardCount_) sfx(Sound::Warning);
  hazardCount_ = state_.hazards.size();
  for (int slot = 0; slot < cfg::MAX_PLAYERS; ++slot) {
    const Player* p = playerForSlot(slot);
    if (!p) continue;
    Sampled& prev = sampled_[static_cast<std::size_t>(slot)];
    if (prev.hp >= 0) {
      if (p->hp < prev.hp - 0.5 && p->alive) sfx(Sound::Hurt);
      if (p->level > prev.level) sfx(Sound::Level);
      else if (p->xp > prev.xp) sfx(Sound::Gem);
      if (p->hp > prev.hp + 10 && p->level == prev.level) sfx(Sound::Heart);
      if (p->coins > prev.coins) sfx(Sound::Coin);
      if (p->specialCharge - prev.charge >= 20) sfx(Sound::Crystal);
      if (p->castCount > prev.cast) sfx(Sound::Shoot);
    }
    prev = {p->hp, p->xp, p->specialCharge, p->level, p->coins, p->castCount};
  }
  for (int slot = 0; slot < cfg::MAX_PLAYERS; ++slot) {
    const Player* p = playerForSlot(slot);
    if (!p) continue;
    double& last = lastHp_[static_cast<std::size_t>(slot)];
    if (last >= 0 && p->alive && p->hp < last - 0.5) { hurtFlash_ = 0.35; anim_.shake(4); }
    last = p->hp;
  }
}

void Frontend::playEventSound(const Event& e) {
  // Positional events only sound when a local player is within 800 units (feedback.js `near`).
  bool near = e.x == 0 && e.y == 0;
  for (const auto& [_, p] : state_.players) {
    const double dx = p.x - e.x, dy = p.y - e.y;
    if (dx * dx + dy * dy < 800.0 * 800.0) near = true;
  }
  const std::string& k = e.kind;
  if (k == "boss") sfx(Sound::Boss);
  else if (k == "stage" || k == "shrineAccepted") sfx(Sound::Stage);
  else if (k == "bossDown") sfx(Sound::BossDown);
  else if (k == "elite" || k == "altar") sfx(Sound::Elite);
  else if (k == "ring") sfx(Sound::Warning);
  else if (k == "chest" || k == "altarComplete" || k == "merchantSale" || k == "thiefDown") sfx(Sound::Chest);
  else if (k == "combo") { if (e.variant == 1) sfx(Sound::TeamCombo); else if (near) sfx(Sound::Chain); }
  else if (k == "convergence") sfx(Sound::Convergence);
  else if (k == "encounter") sfx(Sound::Encounter);
  else if (k == "loop") sfx(Sound::Loop);
  else if (k == "phoenix") sfx(Sound::Phoenix);
  else if (k == "signal") sfx(Sound::Signal);
  else if (!near) return;
  else if (k == "evade") sfx(Sound::Shoot);
  else if (k == "magnet") sfx(Sound::Magnet);
  else if (k == "boom") sfx(Sound::Boom);
  else if (k == "chain") sfx(Sound::Chain);
  else if (k == "familiar") sfx(Sound::Familiar);
  else if (k == "revive") sfx(Sound::Revive);
  else if (k == "special") sfx(Sound::Special);
}

void Frontend::updateMusic() {
  if (!audio_) return;
  // music.js moodFor(): menu, horde, guardian, fury (third stage), victory, defeat.
  Mood mood = Mood::Menu;
  if (screen_ == Screen::Playing || screen_ == Screen::Over) {
    if (state_.over) mood = state_.victory ? Mood::Victory : Mood::Defeat;
    else {
      mood = Mood::Horde;
      for (const auto& e : state_.enemies) if (e.boss && e.hp > 0) { mood = e.stage == 3 ? Mood::Fury : Mood::Boss; break; }
    }
  }
  audio_->music(mood, mood == Mood::Menu ? 0 : state_.phase);
  if (state_.over && !overSoundPlayed_ && (screen_ == Screen::Playing || screen_ == Screen::Over)) { overSoundPlayed_ = true; sfx(state_.victory ? Sound::Victory : Sound::Defeat); }
}

void Frontend::renderFeedback(BatchRenderer& b, float width, float height) {
  const float s = uiScale(height);
  // Low-health pulse and hurt flash: red gradients creeping in from the screen edges.
  bool low = false;
  for (const auto& [_, p] : state_.players) if (p.alive && p.hp / std::max(1.0, p.maxHp) < 0.3) low = true;
  float intensity = static_cast<float>(hurtFlash_ / 0.35) * 0.55f;
  if (low) intensity = std::max(intensity, 0.25f + 0.15f * static_cast<float>(std::sin(menuTime_ * 5)));
  if (intensity > 0.01f) {
    const auto red = rgba(200, 20, 30, static_cast<std::uint8_t>(std::clamp(intensity, 0.0f, 1.0f) * 255)), clear = rgba(200, 20, 30, 0);
    const float ew = width * 0.16f, eh = height * 0.2f;
    b.rectGradient(0, 0, width, eh, red, red, clear, clear);
    b.rectGradient(0, height - eh, width, eh, clear, clear, red, red);
    b.rectGradient(0, 0, ew, height, red, clear, clear, red);
    b.rectGradient(width - ew, 0, ew, height, clear, red, red, clear);
  }
  auto drawBanner = [&](const Banner& banner, float y, float px) {
    if (banner.age >= banner.life || banner.text.empty()) return;
    const float fadeIn = static_cast<float>(std::min<double>(1.0, banner.age / 0.2)), fadeOut = static_cast<float>(std::min<double>(1.0, (banner.life - banner.age) / 0.5));
    const float alpha = std::min(fadeIn, fadeOut);
    const float w = b.textWidth(banner.text, px) + 40 * s, h = px * 1.6f;
    const float drop = (1 - fadeIn) * -10 * s;
    b.rect(width * 0.5f - w * 0.5f, y + drop, w, h, rgba(8, 10, 20, static_cast<std::uint8_t>(200 * alpha)));
    b.rect(width * 0.5f - w * 0.5f, y + drop + h - 3 * s, w, 3 * s, withAlpha(banner.color, static_cast<std::uint8_t>(255 * alpha)));
    b.text(width * 0.5f, y + drop + px * 0.25f, banner.text, px, withAlpha(banner.color, static_cast<std::uint8_t>(255 * alpha)), Align::Center);
  };
  drawBanner(announce_, 100 * s, 26 * s);
  drawBanner(toast_, height - 150 * s, 20 * s);
}

void Frontend::renderHud(BatchRenderer& b, float width, float height) {
  const float s = uiScale(height);
  const int phase = std::clamp(state_.phase, 0, 5);
  const auto& def = phases()[static_cast<std::size_t>(phase)];

  // Phase + timer, top centre (top-right on the compact screen, beside player 1's panel).
  const float cx = options_.compact ? width - 200 * s : width * 0.5f;
  b.rect(cx - 190 * s, 8 * s, 380 * s, 58 * s, kPanel);
  std::snprintf(scratch_, sizeof scratch_, "Fase %d/6 · %s", phase + 1, def.name.c_str());
  b.text(cx, 12 * s, scratch_, 22 * s, accentColor(phase), Align::Center);
  if (state_.phaseStatus == "horde") {
    char clock[16];
    formatClock(clock, sizeof clock, phaseDuration(state_) - state_.phaseTime);
    std::snprintf(scratch_, sizeof scratch_, "%s  ·  %d inimigos", clock, static_cast<int>(state_.enemies.size()));
    b.text(cx, 38 * s, scratch_, 20 * s, kText, Align::Center);
  } else if (state_.phaseStatus == "boss") {
    b.text(cx, 38 * s, def.bossName, 20 * s, kGold, Align::Center);
  }

  // Boss health bar.
  for (const auto& e : state_.enemies) {
    if (!e.boss || e.hp <= 0) continue;
    if (options_.compact) meter(b, 10 * s, 104 * s, width - 20 * s, 12 * s, e.hp / std::max(1.0, e.maxHp), rgba(235, 80, 90));
    else meter(b, cx - 300 * s, 72 * s, 600 * s, 14 * s, e.hp / std::max(1.0, e.maxHp), rgba(235, 80, 90));
    break;
  }

  // One panel per player, in the four corners (online: you top-left, allies in the other corners).
  const auto hud = hudPlayers();
  for (std::size_t index = 0; index < hud.size(); ++index) {
    const Player* p = hud[index];
    if (!p) continue;
    const int slot = static_cast<int>(index);
    const float pw = 310 * s, ph = 86 * s;
    const float x = (slot % 2 == 0) ? 10 * s : width - pw - 10 * s;
    const float y = (slot < 2) ? 10 * s : height - ph - 10 * s;
    b.rect(x, y, pw, ph, kPanel);
    b.rect(x, y, 4 * s, ph, playerColor(p->color));
    if (onlineMatch_) std::snprintf(scratch_, sizeof scratch_, "%s  Nv %d", p->name.c_str(), p->level);
    else std::snprintf(scratch_, sizeof scratch_, "P%d  Nv %d", slot + 1, p->level);
    b.text(x + 12 * s, y + 4 * s, scratch_, 20 * s, playerColor(p->color));
    std::snprintf(scratch_, sizeof scratch_, "%d abates · %d moedas", p->stats.kills, p->coins);
    b.text(x + pw - 8 * s, y + 7 * s, scratch_, 15 * s, kMuted, Align::Right);
    if (!p->alive) {
      b.text(x + 12 * s, y + 34 * s, "Caído - aproxime-se para reviver", 16 * s, rgba(255, 140, 140));
      continue;
    }
    meter(b, x + 12 * s, y + 32 * s, pw - 24 * s, 14 * s, p->hp / std::max(1.0, p->maxHp), rgba(90, 225, 120));
    std::snprintf(scratch_, sizeof scratch_, "%d/%d", static_cast<int>(std::ceil(p->hp)), static_cast<int>(p->maxHp));
    b.text(x + pw * 0.5f, y + 31 * s, scratch_, 14 * s, kText, Align::Center);
    meter(b, x + 12 * s, y + 51 * s, pw - 24 * s, 8 * s, p->xp / std::max(1, xpNeeded(p->level)), rgba(110, 170, 255));
    const bool ready = p->specialCharge >= cfg::SPECIAL_MAX && p->specialCooldown <= 0;
    meter(b, x + 12 * s, y + 64 * s, (pw - 24 * s) * 0.62f, 12 * s, p->specialCharge / cfg::SPECIAL_MAX, ready ? kGold : rgba(90, 215, 180));
    const bool dashReady = p->dashCooldown <= 0;
    const float dx = x + 12 * s + (pw - 24 * s) * 0.66f, dw = (pw - 24 * s) * 0.34f;
    meter(b, dx, y + 64 * s, dw, 12 * s, 1 - p->dashCooldown / cfg::DASH_COOLDOWN, dashReady ? rgba(200, 210, 255) : rgba(90, 96, 130));
    b.text(dx + dw * 0.5f, y + 62 * s, "esquiva", 12 * s, dashReady ? rgba(10, 12, 22) : kText, Align::Center);
  }

  if (state_.phaseStatus == "transition") {
    const float cx = width * 0.5f;
    b.rect(0, height * 0.38f, width, 90 * s, rgba(0, 0, 0, 150));
    const int next = std::clamp(state_.phase + 1, 0, 5);
    std::snprintf(scratch_, sizeof scratch_, "%s derrotado!", def.bossName.c_str());
    b.text(cx, height * 0.38f + 8 * s, scratch_, 34 * s, kGold, Align::Center);
    std::snprintf(scratch_, sizeof scratch_, "A seguir: %s", phases()[static_cast<std::size_t>(next)].name.c_str());
    b.text(cx, height * 0.38f + 50 * s, scratch_, 22 * s, kText, Align::Center);
  }
}

void Frontend::renderChooser(BatchRenderer& b, float width, float height) {
  int slot = 0;
  const Player* p = chooser(&slot);
  if (!p) return;
  const float s = uiScale(height);
  b.rect(0, 0, width, height, rgba(4, 6, 14, 170));
  if (onlineMatch_) std::snprintf(scratch_, sizeof scratch_, "Você subiu para o nível %d - escolha um poder", p->level);
  else std::snprintf(scratch_, sizeof scratch_, "P%d subiu para o nível %d - escolha um poder", slot + 1, p->level);
  b.text(width * 0.5f, height * (options_.compact ? 0.08f : 0.16f), scratch_, (options_.compact ? 22 : 30) * s, playerColor(p->color), Align::Center);

  const int count = static_cast<int>(p->pendingPowers.size());
  const float gap = (options_.compact ? 10 : 24) * s;
  const float cardW = std::min(300 * s, (width - 20 * s - gap * static_cast<float>(count - 1)) / static_cast<float>(std::max(1, count)));
  const float cardH = 250 * s;
  const float total = count * cardW + (count - 1) * gap;
  float x = width * 0.5f - total * 0.5f;
  const float y = height * (options_.compact ? 0.2f : 0.28f);
  for (int i = 0; i < count; ++i, x += cardW + gap) {
    const auto& id = p->pendingPowers[static_cast<std::size_t>(i)];
    const auto it = powerDefs().find(id);
    const bool selected = i == choiceIndex_;
    b.rect(x, y, cardW, cardH, selected ? rgba(34, 40, 70, 245) : rgba(16, 18, 32, 235));
    b.frame(x, y, cardW, cardH, selected ? 4 * s : 2 * s, selected ? playerColor(p->color) : rgba(70, 76, 110));
    const std::string kind = it == powerDefs().end() ? "" : it->second.kind;
    b.text(x + 16 * s, y + 14 * s, kindLabel(kind), 15 * s, kind == "evolution" ? kGold : kMuted);
    b.text(x + 16 * s, y + 36 * s, powerTitle(id), (options_.compact ? 20 : 26) * s, kText);
    const int rank = rankOf(*p, id), max = it == powerDefs().end() ? 1 : it->second.max;
    if (max > 1) {
      for (int r = 0; r < max; ++r)
        b.rect(x + 16 * s + r * 22 * s, y + 74 * s, 16 * s, 8 * s, r < rank ? playerColor(p->color) : r == rank ? kGold : rgba(50, 54, 80));
    }
    b.textWrapped(x + 16 * s, y + 98 * s, cardW - 32 * s, powerDescription(id), 19 * s, rgba(205, 212, 235), 5);
  }
  const auto& btn = options_.buttons;
  if (options_.compact) std::snprintf(scratch_, sizeof scratch_, "%s: confirmar · %s: trocar opções (%d)", btn.confirm, btn.alt, p->rerolls);
  else std::snprintf(scratch_, sizeof scratch_, "Direcional: escolher · %s: confirmar · %s: trocar opções (%d)", btn.confirm, btn.alt, p->rerolls);
  b.text(width * 0.5f, y + cardH + 26 * s, scratch_, 20 * s, kMuted, Align::Center);
}

void Frontend::renderTitle(BatchRenderer& b, float width, float height) {
  const float s = uiScale(height);
  b.rect(0, 0, width, height, rgba(10, 10, 22));
  // Slowly drifting sprites behind the menu.
  for (int i = 0; i < 14; ++i) {
    const double t = menuTime_ * 0.15 + i * 0.9;
    const float x = static_cast<float>(std::fmod(i * 173.0 + menuTime_ * (18 + i * 3), width + 200.0)) - 100;
    const float y = height * (0.15f + 0.7f * static_cast<float>(std::fmod(i * 0.37, 1.0))) + static_cast<float>(std::sin(t) * 20 * s);
    b.icon(static_cast<native::SpriteId>(4 + i % 22), x, y, (60 + (i % 3) * 20) * s, rgba(255, 255, 255, 50));
  }
  b.text(width * 0.5f, height * 0.05f, "ARCANA SURVIVORS", 58 * s, kGold, Align::Center);
  if (!options_.compact) b.text(width * 0.5f, height * 0.05f + 64 * s, "Sobreviva às hordas, sozinho ou com até 4 arcanistas", 22 * s, kMuted, Align::Center);

  const auto items = titleItems();
  const float mx = options_.compact ? width - 528 * s : width * 0.5f - 260 * s, my = height * (options_.compact ? 0.19f : 0.25f);
  // Rows tighten when every unlock is shown, so the hints never reach the join slots (or the PSP's edge).
  const float bottom = height - (options_.compact ? 46 : 222) * s;
  const float step = std::min(48 * s, (bottom - my) / static_cast<float>(items.size())), rowH = step - 6 * s;
  const float ty = (rowH - 26 * s) * 0.5f, vy = (rowH - 22 * s) * 0.5f;
  const char* hint = "";
  for (std::size_t i = 0; i < items.size(); ++i) {
    const bool sel = static_cast<int>(i) == menuIndex_;
    const float y = my + static_cast<float>(i) * step;
    b.rect(mx, y, 520 * s, rowH, sel ? rgba(40, 44, 80, 240) : rgba(18, 20, 36, 220));
    if (sel) b.frame(mx, y, 520 * s, rowH, 3 * s, kGold);
    const std::uint32_t color = sel ? kText : kMuted;
    const char* value = nullptr;
    switch (items[i]) {
      case TitleItem::Play: b.text(mx + 20 * s, y + ty, "Jogar", 24 * s, sel ? kGold : kMuted); if (sel) hint = options_.compact ? "Começa com o arcanista ao lado." : "Começa com os arcanistas prontos abaixo."; break;
      case TitleItem::Online:
        b.text(mx + 20 * s, y + ty, "Jogar online", 24 * s, sel ? kGold : kMuted);
        if (sel) hint = "Co-op pela internet, junto com quem joga no navegador.";
        break;
      case TitleItem::Campaign:
        b.text(mx + 20 * s, y + ty, "Ritual", 24 * s, color);
        value = kCampaigns[campaign_].title;
        if (sel) hint = unlocked("endless") ? kCampaigns[campaign_].detail : "O Ritual infinito é desbloqueado no Grimório.";
        break;
      case TitleItem::Weapon:
        b.text(mx + 20 * s, y + ty, "Arma inicial", 24 * s, color);
        value = weapon_.empty() ? "Nenhuma (sorteio normal)" : powerTitle(weapon_);
        if (sel) hint = "Arsenal: o Jogador 1 começa com esta arma.";
        break;
      case TitleItem::Special:
        b.text(mx + 20 * s, y + ty, "Especial", 24 * s, color);
        value = special_ ? kAltSpecials[character_[0]] : kCharacterSpecials[character_[0]];
        if (sel) hint = "Segundo feitiço: especial alternativo do Jogador 1.";
        break;
      case TitleItem::Shop:
        b.text(mx + 20 * s, y + ty, "Grimório", 24 * s, color);
        std::snprintf(scratch_, sizeof scratch_, "%d moedas", profile_.coins);
        b.text(mx + 500 * s, y + vy, scratch_, 20 * s, kGold, Align::Right);
        if (sel) hint = "Melhorias permanentes compradas com as moedas das partidas.";
        break;
      case TitleItem::Credits: b.text(mx + 20 * s, y + ty, "Créditos", 24 * s, color); if (sel) hint = "Quem criou o Arcana Survivors."; break;
      case TitleItem::Quit: b.text(mx + 20 * s, y + ty, "Sair", 24 * s, color); break;
    }
    if (value) b.text(mx + 500 * s, y + vy, value, 20 * s, sel ? kGold : kMuted, Align::Right);
  }
  const float below = my + static_cast<float>(items.size()) * step;
  const float hx = options_.compact ? mx + 260 * s : width * 0.5f;
  b.text(hx, below + 4 * s, hint, (options_.compact ? 15 : 18) * s, kMuted, Align::Center);
  const auto& btn = options_.buttons;
  if (options_.compact) std::snprintf(scratch_, sizeof scratch_, "Esquerda/Direita: personagem · %s: escolher", btn.confirm);
  else if (std::strcmp(btn.join, btn.confirm) == 0)
    std::snprintf(scratch_, sizeof scratch_, "Esquerda/Direita: personagem · %s: escolher/entrar · %s: sair", btn.confirm, btn.leave);
  else std::snprintf(scratch_, sizeof scratch_, "Esquerda/Direita: personagem · %s: escolher · controles: %s entra, %s sai", btn.confirm, btn.join, btn.leave);
  b.text(hx, below + 28 * s, scratch_, 15 * s, rgba(120, 130, 160), Align::Center);

  // Signature: under player 1's slot on the compact screen, top-right corner elsewhere.
  if (options_.compact) {
    b.text(10 * s, height - 36 * s, "Desenvolvido por", 13 * s, rgba(120, 130, 160));
    b.text(10 * s, height - 20 * s, "MrPowerUp82 · 2026", 13 * s, rgba(150, 160, 210));
  } else {
    const float tw = b.textWidth("Desenvolvido por MrPowerUp82 · 2026", 15 * s);
    drawSeal(b, width - tw - 30 * s, 22 * s, 11 * s, 1, 0.8f, menuTime_);
    b.text(width - 12 * s, 13 * s, "Desenvolvido por MrPowerUp82 · 2026", 15 * s, rgba(150, 160, 210), Align::Right);
  }

  // Join slots (compact: only player 1, left of the menu).
  const float cw = options_.compact ? mx - 20 * s : 236 * s, ch = 164 * s, sy = options_.compact ? my : height - ch - 10 * s;
  for (int slot = 0; slot < (options_.compact ? 1 : cfg::MAX_PLAYERS); ++slot) {
    const float sx = options_.compact ? 10 * s + cw / 2 : width * 0.5f + (slot - 1.5f) * (cw + 14 * s);
    const bool in = joined_[static_cast<std::size_t>(slot)];
    const int c = character_[static_cast<std::size_t>(slot)];
    const std::uint32_t color = in ? playerColor(c) : rgba(50, 50, 70);
    b.rect(sx - cw / 2, sy, cw, ch, in ? rgba(24, 28, 48, 240) : rgba(14, 14, 24, 200));
    b.frame(sx - cw / 2, sy, cw, ch, 2 * s, color);
    std::snprintf(scratch_, sizeof scratch_, "P%d", slot + 1);
    b.text(sx - cw / 2 + 10 * s, sy + 6 * s, scratch_, 18 * s, in ? kText : kMuted);
    if (!in) {
      b.icon(static_cast<native::SpriteId>(slot), sx, sy + 70 * s, 76 * s, rgba(255, 255, 255, 40));
      std::snprintf(scratch_, sizeof scratch_, std::strcmp(btn.join, btn.confirm) == 0 ? "Aperte %s para entrar" : "Aperte %s no controle", btn.join);
      b.text(sx, sy + ch - 44 * s, scratch_, 16 * s, kMuted, Align::Center);
      continue;
    }
    // Portrait bobbing like the web preview, flanked by the switch hints.
    const float bob = static_cast<float>(std::sin(menuTime_ * 3 + slot)) * 3 * s;
    b.icon(static_cast<native::SpriteId>(c), sx, sy + 58 * s + bob, 84 * s);
    b.text(sx - cw / 2 + 14 * s, sy + 44 * s, "<", 26 * s, color);
    b.text(sx + cw / 2 - 14 * s, sy + 44 * s, ">", 26 * s, color, Align::Right);
    b.text(sx, sy + 96 * s, kCharacterNames[c], 22 * s, color, Align::Center);
    b.text(sx, sy + 121 * s, kCharacterEffects[c], 14 * s, rgba(205, 212, 235), Align::Center);
    std::snprintf(scratch_, sizeof scratch_, "Especial: %s", slot == 0 && special_ ? kAltSpecials[c] : kCharacterSpecials[c]);
    b.text(sx, sy + 140 * s, scratch_, 14 * s, kMuted, Align::Center);
  }
}

void Frontend::renderSplash(BatchRenderer& b, float width, float height) {
  const float s = uiScale(height);
  b.rect(0, 0, width, height, rgba(6, 6, 16));
  const double t = splashTime_;
  const float fade = ramp(kSplashSeconds - t, 0.0, 0.45);
  const float r = std::min(height * 0.3f, width * 0.22f), cx = width * 0.5f, cy = height * 0.44f;
  drawSeal(b, cx, cy, r, ramp(t, 0.0, 0.9), fade, menuTime_);

  // The name crosses the seal, as on the banner art; never wider than 80% of the screen.
  const float nameA = ramp(t, 0.45, 0.85) * fade;
  const float px = std::min(r * 0.42f, width * 0.8f / std::max(1.0f, b.textWidth(kDeveloper, 1)));
  b.textOutlined(cx, cy - px * 0.55f, kDeveloper, px, rgba(236, 222, 190, alpha8(nameA)), rgba(8, 8, 24, alpha8(nameA)), Align::Center);
  b.text(cx, cy + r * 1.22f, "apresenta", 22 * s, withAlpha(kMuted, alpha8(ramp(t, 0.9, 1.2) * fade)), Align::Center);
}

void Frontend::renderCredits(BatchRenderer& b, float width, float height) {
  const float s = uiScale(height);
  const bool c = options_.compact;
  b.rect(0, 0, width, height, rgba(10, 10, 22));
  const float cx = width * 0.5f, sealR = (c ? 26 : 58) * s;
  drawSeal(b, cx, (c ? 8 : 22) * s + sealR, sealR, 1, 1, menuTime_);
  float y = (c ? 70 : 150) * s;
  b.text(cx, y, "ARCANA SURVIVORS", (c ? 32 : 44) * s, kGold, Align::Center);
  y += (c ? 40 : 58) * s;
  b.text(cx, y, "Criado e desenvolvido por", (c ? 15 : 18) * s, kMuted, Align::Center);
  y += (c ? 18 : 24) * s;
  b.text(cx, y, kDeveloper, (c ? 26 : 32) * s, rgba(200, 212, 255), Align::Center);
  y += (c ? 40 : 58) * s;
  struct Row { const char* label; const char* value; };
  static constexpr Row rows[] = {
    {"Programação", "MrPowerUp82"},
    {"Port nativo C++ / SDL2", "MrPowerUp82"},
    {"Plataformas", "Nintendo Switch · PS Vita · PSP · Windows · Linux"},
    {"Fonte", "DejaVu Sans"},
  };
  for (const auto& row : rows) {
    b.text(cx, y, row.label, (c ? 14 : 16) * s, kMuted, Align::Center);
    b.text(cx, y + (c ? 15 : 19) * s, row.value, (c ? 18 : 21) * s, kText, Align::Center);
    y += (c ? 40 : 58) * s;
  }
  b.text(cx, y + 4 * s, "© 2026 MrPowerUp82 · Licença MIT", (c ? 14 : 16) * s, rgba(120, 130, 160), Align::Center);
  std::snprintf(scratch_, sizeof scratch_, "%s: voltar", options_.buttons.cancel);
  b.text(cx, height - (c ? 20 : 40) * s, scratch_, 15 * s, rgba(120, 130, 160), Align::Center);
}

void Frontend::renderShop(BatchRenderer& b, float width, float height) {
  const float s = uiScale(height);
  b.rect(0, 0, width, height, rgba(10, 10, 22));
  b.text(width * 0.5f, 16 * s, "Grimório", 44 * s, kGold, Align::Center);
  std::snprintf(scratch_, sizeof scratch_, "%d moedas para gastar", profile_.coins);
  b.text(width * 0.5f, 68 * s, scratch_, 22 * s, kText, Align::Center);
  const bool compact = options_.compact;
  const float w = std::min(1120 * s, width - 16 * s), x = width * 0.5f - w * 0.5f, rowH = 34 * s, top = (compact ? 96 : 104) * s;
  // Compact screens show a window of rows around the selection and the description below.
  const int visible = compact ? std::max(3, static_cast<int>((height - top - 60 * s) / rowH)) : kShopRows;
  const int first = std::clamp(shopIndex_ - visible / 2, 0, std::max(0, kShopRows - visible));
  for (int i = first; i < std::min(kShopRows, first + visible); ++i) {
    const float y = top + static_cast<float>(i - first) * rowH;
    const bool sel = i == shopIndex_;
    b.rect(x, y, w, rowH - 3 * s, sel ? rgba(40, 44, 80, 240) : rgba(18, 20, 36, 220));
    if (sel) b.frame(x, y, w, rowH - 3 * s, 2 * s, kGold);
    if (i == kShopRows - 1) {
      if (respecArmed_) std::snprintf(scratch_, sizeof scratch_, "Aperte %s de novo para devolver %d moedas e zerar as melhorias", options_.buttons.confirm, profile_.invested);
      else std::snprintf(scratch_, sizeof scratch_, "Redistribuir melhorias · devolver %d moedas", profile_.invested);
      b.text(width * 0.5f, y + 6 * s, scratch_, 18 * s, profile_.invested > 0 ? (respecArmed_ ? rgba(255, 140, 120) : kText) : kMuted, Align::Center);
      continue;
    }
    const auto& up = kUpgrades[i];
    const auto it = profile_.upgrades.rank.find(up.id);
    const int rank = it == profile_.upgrades.rank.end() ? 0 : it->second;
    int max = 0;
    while (nextUpgradeCost(up.id, max) >= 0) ++max;
    const int cost = nextUpgradeCost(up.id, rank);
    const bool affordable = cost >= 0 && profile_.coins >= cost;
    b.text(x + 14 * s, y + 5 * s, up.title, 19 * s, sel ? kText : rgba(205, 212, 235));
    for (int r = 0; r < max; ++r)
      b.rect(x + 200 * s + r * 18 * s, y + 11 * s, 13 * s, 10 * s, r < rank ? kGold : rgba(50, 54, 80));
    if (!compact) b.text(x + 300 * s, y + 7 * s, up.desc, 16 * s, kMuted);
    if (cost < 0) b.text(x + w - 14 * s, y + 6 * s, "MÁX", 18 * s, rgba(141, 255, 204), Align::Right);
    else {
      std::snprintf(scratch_, sizeof scratch_, "%d", cost);
      b.icon(native::SpriteId::Coin, x + w - 20 * s, y + 15 * s, 22 * s);
      b.text(x + w - 36 * s, y + 6 * s, scratch_, 18 * s, affordable ? kGold : rgba(150, 110, 110), Align::Right);
    }
  }
  if (compact) {
    const float by = top + static_cast<float>(visible) * rowH + 4 * s;
    if (shopIndex_ < kShopRows - 1) b.text(width * 0.5f, by, kUpgrades[shopIndex_].desc, 17 * s, kText, Align::Center);
    std::snprintf(scratch_, sizeof scratch_, "%s: comprar · %s: voltar", options_.buttons.confirm, options_.buttons.cancel);
    b.text(width * 0.5f, by + 24 * s, scratch_, 15 * s, rgba(120, 130, 160), Align::Center);
  } else {
    std::snprintf(scratch_, sizeof scratch_, "%s: comprar · %s: voltar · as melhorias valem para todos os jogadores", options_.buttons.confirm, options_.buttons.cancel);
    b.text(width * 0.5f, top + kShopRows * rowH + 8 * s, scratch_, 16 * s, rgba(120, 130, 160), Align::Center);
  }
  renderFeedback(b, width, height);
}

void Frontend::renderPause(BatchRenderer& b, float width, float height) {
  const float s = uiScale(height);
  b.rect(0, 0, width, height, rgba(0, 0, 0, 160));
  b.text(width * 0.5f, height * 0.25f, "Pausado", 52 * s, kText, Align::Center);
  const char* items[4] = {"Continuar", "Reiniciar ritual", audio_ && audio_->muted() ? "Som: desligado" : "Som: ligado", "Menu principal"};
  for (int i = 0; i < 4; ++i) {
    const float y = height * 0.4f + i * 60 * s;
    const bool sel = i == pauseIndex_;
    b.rect(width * 0.5f - 200 * s, y, 400 * s, 50 * s, sel ? rgba(40, 44, 80, 240) : rgba(18, 20, 36, 220));
    if (sel) b.frame(width * 0.5f - 200 * s, y, 400 * s, 50 * s, 3 * s, kGold);
    b.text(width * 0.5f, y + 11 * s, items[i], 24 * s, sel ? kText : kMuted, Align::Center);
  }
}

void Frontend::renderOver(BatchRenderer& b, float width, float height) {
  const float s = uiScale(height);
  b.rect(0, 0, width, height, rgba(0, 0, 0, 180));
  b.text(width * 0.5f, height * 0.22f, state_.victory ? "Vitória!" : "Derrota", 60 * s, state_.victory ? kGold : rgba(255, 110, 110), Align::Center);
  char clock[16];
  formatClock(clock, sizeof clock, state_.time);
  std::snprintf(scratch_, sizeof scratch_, "Tempo %s · Fase %d/6", clock, state_.phase + 1);
  b.text(width * 0.5f, height * 0.36f, scratch_, 26 * s, kText, Align::Center);
  float y = height * 0.45f;
  const auto hud = hudPlayers();
  for (std::size_t index = 0; index < hud.size(); ++index) {
    const Player* p = hud[index];
    if (!p) continue;
    if (onlineMatch_) std::snprintf(scratch_, sizeof scratch_, "%s · nível %d · %d abates · %d de dano · %d moedas", p->name.c_str(), p->level,
                                    p->stats.kills, static_cast<int>(p->stats.damage), p->coins);
    else std::snprintf(scratch_, sizeof scratch_, "P%d · nível %d · %d abates · %d de dano · %d moedas", static_cast<int>(index) + 1, p->level,
                       p->stats.kills, static_cast<int>(p->stats.damage), p->coins);
    b.text(width * 0.5f, y, scratch_, 22 * s, playerColor(p->color), Align::Center);
    y += 34 * s;
  }
  if (!profilePath_.empty()) {
    std::snprintf(scratch_, sizeof scratch_, "+%d moedas guardadas no Grimório · total %d", earned_, profile_.coins);
    b.text(width * 0.5f, height * 0.72f, scratch_, 24 * s, kGold, Align::Center);
  }
  if (onlineMatch_) std::snprintf(scratch_, sizeof scratch_, "%s ou %s: voltar ao menu online", options_.buttons.confirm, options_.buttons.cancel);
  else std::snprintf(scratch_, sizeof scratch_, "%s: jogar de novo · %s: menu principal", options_.buttons.confirm, options_.buttons.cancel);
  b.text(width * 0.5f, height * 0.8f, scratch_, 22 * s, kMuted, Align::Center);
}

void Frontend::renderPerf(BatchRenderer& b, float width, float height) {
  const float s = uiScale(height);
  const auto st = lastBatch_;
  std::snprintf(scratch_, sizeof scratch_, "%.0f fps  frame %.2fms  sim %.2fms (%d)  mundo %.2fms  draw %d/%dv  E%d S%d D%d",
                fps_, timings_.frameMs, timings_.updateMs, timings_.steps, buildMs_, st.drawCalls, st.vertices,
                static_cast<int>(state_.enemies.size()), static_cast<int>(state_.shots.size()), static_cast<int>(state_.gems.size()));
  const float w = b.textWidth(scratch_, 16 * s) + 16 * s;
  b.rect(width * 0.5f - w * 0.5f, height - 28 * s, w, 24 * s, rgba(0, 0, 0, 190));
  b.text(width * 0.5f, height - 26 * s, scratch_, 16 * s, rgba(160, 255, 170), Align::Center);
}

} // namespace arcana::sdl
