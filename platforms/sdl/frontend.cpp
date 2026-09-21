#include "frontend.hpp"
#include "arcana/data.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <new>

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
constexpr int kTitleItems = 4; // campaigns + quit

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

std::uint32_t floorColor(int phase) {
  static constexpr std::uint32_t colors[6] = {rgba(16, 30, 24), rgba(16, 24, 36), rgba(34, 18, 16), rgba(22, 28, 18), rgba(30, 27, 20), rgba(20, 14, 32)};
  return colors[std::clamp(phase, 0, 5)];
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
  b.rect(x + 1, y + 1, (w - 2) * static_cast<float>(std::clamp(ratio, 0.0, 1.0)), h - 2, fill);
}

void formatClock(char* out, std::size_t n, double seconds) {
  const int s = std::max(0, static_cast<int>(std::ceil(seconds)));
  std::snprintf(out, n, "%d:%02d", s / 60, s % 60);
}

} // namespace

Frontend::Frontend(FrontendOptions options) : options_(std::move(options)) {
  if (options_.autoplay) {
    for (int i = 0; i < cfg::MAX_PLAYERS; ++i) joined_[static_cast<std::size_t>(i)] = i < std::clamp(options_.autoplayPlayers, 1, cfg::MAX_PLAYERS);
  }
  for (int i = 0; i < 3; ++i) if (options_.campaign == kCampaigns[i].id) menuIndex_ = i;
  if (options_.startImmediately || options_.autoplay) startRun();
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
  const auto it = state_.players.find(kIds[slot]);
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
  // Rebuild in place: GameState is ~200 KB and an assignment from a temporary would put a second
  // copy on the (small, on Vita) main-thread stack. Prvalue + placement new is guaranteed elision.
  state_.~GameState();
  new (&state_) GameState(createGameState(kCampaigns[std::clamp(menuIndex_, 0, 2)].id));
  static constexpr const char* names[4] = {"Arcanista 1", "Arcanista 2", "Arcanista 3", "Arcanista 4"};
  for (int slot = 0; slot < cfg::MAX_PLAYERS; ++slot) {
    if (!joined_[static_cast<std::size_t>(slot)]) continue;
    Player p = createPlayer(kIds[slot], names[slot], slot);
    p.x = (slot % 2 ? 60 : -60) * (slot > 0 ? 1 : 0);
    p.y = (slot >= 2 ? 60 : 0);
    state_.players[p.id] = std::move(p);
  }
  clock_.reset();
  choiceKey_.clear();
  choiceIndex_ = 0;
  camera_ = {};
  camera_.zoom = 0; // snap on the first rendered frame
  screen_ = Screen::Playing;
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
    case Screen::Title:
      updateTitle();
      if (screen_ == Screen::Title && menuIndex_ == kTitleItems - 1 && pressed(0, ActConfirm)) return false;
      break;
    case Screen::Playing: updatePlaying(frameSeconds, input); break;
    case Screen::Paused: updatePaused(); break;
    case Screen::Over: updateOver(); break;
  }
  return true;
}

void Frontend::updateTitle() {
  if (pressed(0, ActUp)) menuIndex_ = (menuIndex_ + kTitleItems - 1) % kTitleItems;
  if (pressed(0, ActDown)) menuIndex_ = (menuIndex_ + 1) % kTitleItems;
  for (int slot = 1; slot < cfg::MAX_PLAYERS; ++slot) {
    if (pressed(slot, ActConfirm)) joined_[static_cast<std::size_t>(slot)] = true;
    if (pressed(slot, ActCancel)) joined_[static_cast<std::size_t>(slot)] = false;
  }
  if (pressed(0, ActConfirm) && menuIndex_ < 3) startRun();
}

void Frontend::updatePlaying(double frameSeconds, const InputFrame& input) {
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

  if (state_.over) {
    overTime_ += frameSeconds;
    if (overTime_ > 1.2) { screen_ = Screen::Over; pauseIndex_ = 0; }
  } else {
    overTime_ = 0;
  }
}

void Frontend::leashPlayers() {
  // Shared-screen co-op: nobody may leave the view the camera can show at its widest zoom.
  // Half extents match renderWorld()'s minimum zoom (0.55 of a 720p-tall 16:9 view).
  constexpr double halfW = 1280.0 / (2 * 0.55) - 70, halfH = 720.0 / (2 * 0.55) - 80;
  double minX = 1e30, maxX = -1e30, minY = 1e30, maxY = -1e30;
  int alive = 0;
  for (const auto& [_, p] : state_.players) {
    minX = std::min(minX, p.x); maxX = std::max(maxX, p.x); minY = std::min(minY, p.y); maxY = std::max(maxY, p.y);
    alive += p.alive ? 1 : 0;
  }
  if (state_.players.size() < 2 || alive == 0) return;
  const double cx = (minX + maxX) * 0.5, cy = (minY + maxY) * 0.5;
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
  if (pressed(slot, ActLeft) || pressed(slot, ActUp)) choiceIndex_ = (choiceIndex_ + count - 1) % count;
  if (pressed(slot, ActRight) || pressed(slot, ActDown)) choiceIndex_ = (choiceIndex_ + 1) % count;
  if (pressed(slot, ActAlt)) {
    int players = 0;
    for (bool j : joined_) players += j ? 1 : 0;
    rerollPowers(p, random_, players > 1);
    return;
  }
  if (pressed(slot, ActConfirm) || (options_.autoplay && menuTime_ > 0)) {
    const std::string id = p.pendingPowers[static_cast<std::size_t>(std::clamp(choiceIndex_, 0, count - 1))];
    applyPower(p, id);
    choiceKey_.clear();
  }
}

void Frontend::updatePaused() {
  constexpr int items = 3;
  if (pressed(0, ActUp)) pauseIndex_ = (pauseIndex_ + items - 1) % items;
  if (pressed(0, ActDown)) pauseIndex_ = (pauseIndex_ + 1) % items;
  if (anyPressed(ActPause) || anyPressed(ActCancel)) { screen_ = Screen::Playing; clock_.reset(); return; }
  if (!anyPressed(ActConfirm)) return;
  if (pauseIndex_ == 0) { screen_ = Screen::Playing; clock_.reset(); }
  else if (pauseIndex_ == 1) startRun();
  else screen_ = Screen::Title;
}

void Frontend::updateOver() {
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

// ---------------------------------------------------------------------------------------------
// Rendering

void Frontend::render(BatchRenderer& batch, float width, float height) {
  lastBatch_ = batch.stats(); // previous frame: this one is still being built
  batch.begin();
  if (screen_ == Screen::Title) {
    renderTitle(batch, width, height);
  } else {
    renderWorld(batch, width, height);
    renderHud(batch, width, height);
    if (screen_ == Screen::Playing && chooser()) renderChooser(batch, width, height);
    if (screen_ == Screen::Paused) renderPause(batch, width, height);
    if (screen_ == Screen::Over) renderOver(batch, width, height);
  }
  if (options_.showPerf) renderPerf(batch, width, height);
  batch.flush();
}

void Frontend::renderBackground(BatchRenderer& b, float width, float height, const native::Camera& cam) {
  b.rect(0, 0, width, height, floorColor(state_.phase));
  // A world-anchored grid gives a sense of motion for the price of ~20 quads.
  const float spacing = 160.0f * cam.zoom;
  const float ox = std::fmod(width * 0.5f - cam.x * cam.zoom, spacing);
  const float oy = std::fmod(height * 0.5f - cam.y * cam.zoom, spacing);
  const auto grid = withAlpha(accentColor(state_.phase), 18);
  for (float x = ox < 0 ? ox + spacing : ox; x < width; x += spacing) b.rect(x, 0, 2, height, grid);
  for (float y = oy < 0 ? oy + spacing : oy; y < height; y += spacing) b.rect(0, y, width, 2, grid);
}

void Frontend::renderWorld(BatchRenderer& b, float width, float height) {
  // Shared co-op camera: frame every player (downed ones too, so allies can find and revive them).
  double minX = 1e30, maxX = -1e30, minY = 1e30, maxY = -1e30;
  bool any = false;
  for (const auto& [_, p] : state_.players) {
    minX = std::min(minX, p.x); maxX = std::max(maxX, p.x); minY = std::min(minY, p.y); maxY = std::max(maxY, p.y);
    any = true;
  }
  if (!any) minX = maxX = minY = maxY = 0;
  const float base = height / 720.0f;
  const float want = std::clamp(std::min(width / static_cast<float>(maxX - minX + 560), height / static_cast<float>(maxY - minY + 400)), base * 0.55f, base);
  const float targetX = static_cast<float>((minX + maxX) * 0.5), targetY = static_cast<float>((minY + maxY) * 0.5);
  if (camera_.zoom <= 0) { camera_.zoom = want; camera_.x = targetX; camera_.y = targetY; }
  camera_.x += (targetX - camera_.x) * 0.2f;
  camera_.y += (targetY - camera_.y) * 0.2f;
  camera_.zoom += (want - camera_.zoom) * 0.05f;
  camera_.width = width; camera_.height = height; camera_.viewportX = 0; camera_.viewportY = 0;

  renderBackground(b, width, height, camera_);
  const auto t0 = Clock::now();
  native::buildRenderQueue(state_, camera_, queue_);
  buildMs_ = msSince(t0);
  b.queue(queue_);
}

void Frontend::renderHud(BatchRenderer& b, float width, float height) {
  const float s = height / 720.0f;
  const int phase = std::clamp(state_.phase, 0, 5);
  const auto& def = phases()[static_cast<std::size_t>(phase)];

  // Phase + timer, top centre.
  const float cx = width * 0.5f;
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
    meter(b, cx - 300 * s, 72 * s, 600 * s, 14 * s, e.hp / std::max(1.0, e.maxHp), rgba(235, 80, 90));
    break;
  }

  // One panel per player, in the four corners.
  for (int slot = 0; slot < cfg::MAX_PLAYERS; ++slot) {
    const Player* p = playerForSlot(slot);
    if (!p) continue;
    const float pw = 310 * s, ph = 86 * s;
    const float x = (slot % 2 == 0) ? 10 * s : width - pw - 10 * s;
    const float y = (slot < 2) ? 10 * s : height - ph - 10 * s;
    b.rect(x, y, pw, ph, kPanel);
    b.rect(x, y, 4 * s, ph, playerColor(p->color));
    std::snprintf(scratch_, sizeof scratch_, "P%d  Nv %d", slot + 1, p->level);
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
  const float s = height / 720.0f;
  b.rect(0, 0, width, height, rgba(4, 6, 14, 170));
  std::snprintf(scratch_, sizeof scratch_, "P%d subiu para o nível %d - escolha um poder", slot + 1, p->level);
  b.text(width * 0.5f, height * 0.16f, scratch_, 30 * s, playerColor(p->color), Align::Center);

  const int count = static_cast<int>(p->pendingPowers.size());
  const float cardW = 300 * s, cardH = 250 * s, gap = 24 * s;
  const float total = count * cardW + (count - 1) * gap;
  float x = width * 0.5f - total * 0.5f;
  const float y = height * 0.28f;
  for (int i = 0; i < count; ++i, x += cardW + gap) {
    const auto& id = p->pendingPowers[static_cast<std::size_t>(i)];
    const auto it = powerDefs().find(id);
    const bool selected = i == choiceIndex_;
    b.rect(x, y, cardW, cardH, selected ? rgba(34, 40, 70, 245) : rgba(16, 18, 32, 235));
    b.frame(x, y, cardW, cardH, selected ? 4 * s : 2 * s, selected ? playerColor(p->color) : rgba(70, 76, 110));
    const std::string kind = it == powerDefs().end() ? "" : it->second.kind;
    b.text(x + 16 * s, y + 14 * s, kindLabel(kind), 15 * s, kind == "evolution" ? kGold : kMuted);
    b.text(x + 16 * s, y + 36 * s, powerTitle(id), 26 * s, kText);
    const int rank = rankOf(*p, id), max = it == powerDefs().end() ? 1 : it->second.max;
    if (max > 1) {
      for (int r = 0; r < max; ++r)
        b.rect(x + 16 * s + r * 22 * s, y + 74 * s, 16 * s, 8 * s, r < rank ? playerColor(p->color) : r == rank ? kGold : rgba(50, 54, 80));
    }
    b.textWrapped(x + 16 * s, y + 98 * s, cardW - 32 * s, powerDescription(id), 19 * s, rgba(205, 212, 235), 5);
  }
  std::snprintf(scratch_, sizeof scratch_, "Direcional escolhe · A confirmar · X/Y trocar opções (%d)", p->rerolls);
  b.text(width * 0.5f, y + cardH + 26 * s, scratch_, 20 * s, kMuted, Align::Center);
}

void Frontend::renderTitle(BatchRenderer& b, float width, float height) {
  const float s = height / 720.0f;
  b.rect(0, 0, width, height, rgba(10, 10, 22));
  // Slowly drifting sprites behind the menu.
  for (int i = 0; i < 14; ++i) {
    const double t = menuTime_ * 0.15 + i * 0.9;
    const float x = static_cast<float>(std::fmod(i * 173.0 + menuTime_ * (18 + i * 3), width + 200.0)) - 100;
    const float y = height * (0.15f + 0.7f * static_cast<float>(std::fmod(i * 0.37, 1.0))) + static_cast<float>(std::sin(t) * 20 * s);
    b.icon(static_cast<native::SpriteId>(4 + i % 22), x, y, (60 + (i % 3) * 20) * s, rgba(255, 255, 255, 50));
  }
  b.text(width * 0.5f, height * 0.12f, "ARCANA SURVIVORS", 64 * s, kGold, Align::Center);
  b.text(width * 0.5f, height * 0.12f + 72 * s, "Sobreviva às hordas, sozinho ou com até 4 arcanistas", 22 * s, kMuted, Align::Center);

  const float mx = width * 0.5f - 260 * s, my = height * 0.36f;
  for (int i = 0; i < kTitleItems; ++i) {
    const bool sel = i == menuIndex_;
    const float y = my + i * 62 * s;
    b.rect(mx, y, 520 * s, 54 * s, sel ? rgba(40, 44, 80, 240) : rgba(18, 20, 36, 220));
    if (sel) b.frame(mx, y, 520 * s, 54 * s, 3 * s, kGold);
    const char* title = i < 3 ? kCampaigns[i].title : "Sair";
    b.text(mx + 20 * s, y + 12 * s, title, 26 * s, sel ? kText : kMuted);
  }
  if (menuIndex_ < 3) b.text(width * 0.5f, my + kTitleItems * 62 * s + 6 * s, kCampaigns[menuIndex_].detail, 20 * s, kMuted, Align::Center);

  // Join slots.
  const float sy = height - 130 * s;
  for (int slot = 0; slot < cfg::MAX_PLAYERS; ++slot) {
    const float sx = width * 0.5f + (slot - 1.5f) * 170 * s;
    const bool in = joined_[static_cast<std::size_t>(slot)];
    b.rect(sx - 75 * s, sy, 150 * s, 100 * s, in ? rgba(24, 28, 48, 240) : rgba(14, 14, 24, 200));
    b.frame(sx - 75 * s, sy, 150 * s, 100 * s, 2 * s, in ? playerColor(slot) : rgba(50, 50, 70));
    b.icon(static_cast<native::SpriteId>(slot), sx, sy + 42 * s, 64 * s, in ? 0xffffffffu : rgba(255, 255, 255, 50));
    std::snprintf(scratch_, sizeof scratch_, in ? "P%d pronto" : "P%d: aperte A", slot + 1);
    b.text(sx, sy + 76 * s, scratch_, 16 * s, in ? playerColor(slot) : kMuted, Align::Center);
  }
}

void Frontend::renderPause(BatchRenderer& b, float width, float height) {
  const float s = height / 720.0f;
  b.rect(0, 0, width, height, rgba(0, 0, 0, 160));
  b.text(width * 0.5f, height * 0.25f, "Pausado", 52 * s, kText, Align::Center);
  static constexpr const char* items[3] = {"Continuar", "Reiniciar ritual", "Menu principal"};
  for (int i = 0; i < 3; ++i) {
    const float y = height * 0.4f + i * 60 * s;
    const bool sel = i == pauseIndex_;
    b.rect(width * 0.5f - 200 * s, y, 400 * s, 50 * s, sel ? rgba(40, 44, 80, 240) : rgba(18, 20, 36, 220));
    if (sel) b.frame(width * 0.5f - 200 * s, y, 400 * s, 50 * s, 3 * s, kGold);
    b.text(width * 0.5f, y + 11 * s, items[i], 24 * s, sel ? kText : kMuted, Align::Center);
  }
}

void Frontend::renderOver(BatchRenderer& b, float width, float height) {
  const float s = height / 720.0f;
  b.rect(0, 0, width, height, rgba(0, 0, 0, 180));
  b.text(width * 0.5f, height * 0.22f, state_.victory ? "Vitória!" : "Derrota", 60 * s, state_.victory ? kGold : rgba(255, 110, 110), Align::Center);
  char clock[16];
  formatClock(clock, sizeof clock, state_.time);
  std::snprintf(scratch_, sizeof scratch_, "Tempo %s · Fase %d/6", clock, state_.phase + 1);
  b.text(width * 0.5f, height * 0.36f, scratch_, 26 * s, kText, Align::Center);
  float y = height * 0.45f;
  for (int slot = 0; slot < cfg::MAX_PLAYERS; ++slot) {
    const Player* p = playerForSlot(slot);
    if (!p) continue;
    std::snprintf(scratch_, sizeof scratch_, "P%d · nível %d · %d abates · %d de dano · %d moedas", slot + 1, p->level, p->stats.kills,
                  static_cast<int>(p->stats.damage), p->coins);
    b.text(width * 0.5f, y, scratch_, 22 * s, playerColor(p->color), Align::Center);
    y += 34 * s;
  }
  b.text(width * 0.5f, height * 0.8f, "A jogar de novo · B menu principal", 22 * s, kMuted, Align::Center);
}

void Frontend::renderPerf(BatchRenderer& b, float width, float height) {
  const float s = height / 720.0f;
  const auto st = lastBatch_;
  std::snprintf(scratch_, sizeof scratch_, "%.0f fps  frame %.2fms  sim %.2fms (%d)  queue %.2fms  draw %d/%dv  E%d S%d D%d",
                fps_, timings_.frameMs, timings_.updateMs, timings_.steps, buildMs_, st.drawCalls, st.vertices,
                static_cast<int>(state_.enemies.size()), static_cast<int>(state_.shots.size()), static_cast<int>(state_.gems.size()));
  const float w = b.textWidth(scratch_, 16 * s) + 16 * s;
  b.rect(width * 0.5f - w * 0.5f, height - 28 * s, w, 24 * s, rgba(0, 0, 0, 190));
  b.text(width * 0.5f, height - 26 * s, scratch_, 16 * s, rgba(160, 255, 170), Align::Center);
}

} // namespace arcana::sdl
