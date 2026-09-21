#include "audio.hpp"

#include <algorithm>
#include <cmath>

namespace arcana::sdl {
namespace {

constexpr float kMaster = 0.32f;  // audio.js master gain
constexpr float kMusicBus = 0.9f; // music.js bus gain
constexpr double kPi = 3.14159265358979323846;

// audio.js THROTTLE (seconds between repeats of the same sound).
float throttleFor(Sound s) {
  switch (s) {
    case Sound::Shoot: return 0.09f; case Sound::Hit: return 0.045f; case Sound::Kill: return 0.05f;
    case Sound::Gem: return 0.03f; case Sound::Coin: return 0.06f; case Sound::Hurt: return 0.15f;
    case Sound::Boom: return 0.08f; case Sound::Chain: return 0.1f; case Sound::Warning: return 0.3f;
    case Sound::Familiar: return 0.18f; case Sound::Signal: return 0.25f; case Sound::TeamCombo: return 0.2f;
    default: return 0;
  }
}

// music.js: realm roots (MIDI), minor progression i-VI-III-VII, and the victory progression.
constexpr int kRoots[6] = {57, 50, 52, 53, 55, 48};
constexpr int kProgression[4][3] = {{0, 3, 7}, {-4, 0, 3}, {3, 7, 10}, {-2, 2, 5}};
constexpr int kVictory[4][3] = {{0, 4, 7}, {5, 9, 12}, {7, 11, 14}, {0, 4, 7}};
struct MoodDef { float bpm, pad, arp, kick, bass, hat, lead; bool major; };
constexpr MoodDef kMoods[7] = {
  {60, 0, 0, 0, 0, 0, 0, false},                              // silent
  {70, 0.05f, 0, 0, 0, 0, 0, false},                          // menu
  {96, 0.035f, 0.022f, 0.05f, 0.03f, 0, 0, false},            // horde
  {128, 0.03f, 0.024f, 0.08f, 0.05f, 0.018f, 0, false},       // boss
  {148, 0.03f, 0.026f, 0.09f, 0.06f, 0.024f, 0.02f, false},   // fury
  {84, 0.05f, 0.02f, 0, 0, 0, 0, true},                       // victory
  {60, 0.035f, 0, 0, 0, 0, 0, false},                         // defeat
};

float midiHz(int midi) { return 440.0f * std::pow(2.0f, (static_cast<float>(midi) - 69) / 12.0f); }

// PolyBLEP residual: removes most aliasing from the square/saw discontinuities.
float polyBlep(float t, float dt) {
  if (t < dt) { t /= dt; return t + t - t * t - 1; }
  if (t > 1 - dt) { t = (t - 1) / dt; return t * t + t + t + 1; }
  return 0;
}

std::array<float, 1025> makeSine() {
  std::array<float, 1025> t{};
  for (std::size_t i = 0; i < t.size(); ++i) t[i] = static_cast<float>(std::sin(2 * kPi * static_cast<double>(i) / 1024.0));
  return t;
}
const std::array<float, 1025> kSine = makeSine();

float rampMul(float from, float to, int samples) {
  if (samples <= 0 || from <= 0 || to <= 0) return 1;
  return std::pow(to / from, 1.0f / static_cast<float>(samples));
}

} // namespace

const char* Audio::name(Sound s) {
  static constexpr const char* names[] = {"shoot", "hit", "kill", "gem", "coin", "heart", "crystal", "level", "power", "hurt",
    "warning", "boom", "chain", "familiar", "special", "boss", "stage", "bossDown", "elite", "chest", "magnet", "revive",
    "phoenix", "victory", "defeat", "click", "signal", "encounter", "teamCombo", "convergence", "loop"};
  return names[static_cast<std::size_t>(s)];
}

// ---- device ------------------------------------------------------------------------------------

bool Audio::init(int sampleRate) {
  sr_ = static_cast<float>(sampleRate);
  if (SDL_WasInit(SDL_INIT_AUDIO) == 0 && SDL_InitSubSystem(SDL_INIT_AUDIO) != 0) return false;
  SDL_AudioSpec want{};
  want.freq = sampleRate;
  want.format = AUDIO_S16SYS;
  want.channels = 2;
  want.samples = 1024;
  want.callback = &Audio::callback;
  want.userdata = this;
  device_ = SDL_OpenAudioDevice(nullptr, 0, &want, &spec_, SDL_AUDIO_ALLOW_CHANNELS_CHANGE);
  if (!device_) return false;
  if (spec_.freq != sampleRate) { SDL_CloseAudioDevice(device_); device_ = 0; return false; }
  scratch_.resize(std::min<std::size_t>(scratch_.capacity(), spec_.samples * 2u));
  SDL_PauseAudioDevice(device_, 0);
  return true;
}

void Audio::initOffline(int sampleRate) { offline_ = true; sr_ = static_cast<float>(sampleRate); }

void Audio::shutdown() {
  if (device_) SDL_CloseAudioDevice(device_);
  device_ = 0;
}

void Audio::callback(void* userdata, Uint8* stream, int bytes) {
  auto* self = static_cast<Audio*>(userdata);
  const int channels = self->spec_.channels;
  const int frames = bytes / (channels * static_cast<int>(sizeof(Sint16)));
  auto* out = reinterpret_cast<Sint16*>(stream);
  self->pumpCommands(); // SDL holds the device lock during the callback
  int done = 0;
  while (done < frames) {
    const int n = std::min(frames - done, static_cast<int>(self->scratch_.capacity()));
    self->scratch_.resize(static_cast<std::size_t>(n));
    self->mix(self->scratch_.data(), n);
    for (int i = 0; i < n; ++i) {
      const auto s = static_cast<Sint16>(std::clamp(self->scratch_[static_cast<std::size_t>(i)], -1.0f, 1.0f) * 32767.0f);
      for (int c = 0; c < channels; ++c) out[(done + i) * channels + c] = s;
    }
    done += n;
  }
}

void Audio::render(float* out, int frames) {
  pumpCommands();
  mix(out, frames);
}

// ---- game thread -------------------------------------------------------------------------------

void Audio::play(Sound sound) {
  if (muted_ || (!device_ && !offline_)) return;
  const double now = std::chrono::duration<double>(std::chrono::steady_clock::now() - epoch_).count();
  auto& last = lastPlayed_[static_cast<std::size_t>(sound)];
  if (const float t = throttleFor(sound); t > 0 && now - last < t) return;
  last = now;
  float arg = 1;
  if (sound == Sound::Gem) {
    // Consecutive gems climb in pitch (audio.js combo).
    combo_ = now - comboAt_ < 0.6 ? std::min(combo_ + 1, 16) : 0;
    comboAt_ = now;
    arg = 1 + static_cast<float>(combo_) * 0.05f;
  }
  if (device_) SDL_LockAudioDevice(device_);
  pending_.push_back(Command{0, sound, arg, Mood::Silent, 0});
  if (device_) SDL_UnlockAudioDevice(device_);
}

void Audio::music(Mood mood, int phase) {
  if (mood == wantedMood_ && phase == wantedPhase_) return;
  wantedMood_ = mood; wantedPhase_ = phase;
  if (!device_ && !offline_) return;
  if (device_) SDL_LockAudioDevice(device_);
  pending_.push_back(Command{1, Sound::Click, 1, muted_ ? Mood::Silent : mood, phase});
  if (device_) SDL_UnlockAudioDevice(device_);
}

void Audio::setMuted(bool muted) {
  muted_ = muted;
  if (!device_ && !offline_) return;
  if (device_) SDL_LockAudioDevice(device_);
  pending_.push_back(Command{2, Sound::Click, muted ? 1.0f : 0.0f, Mood::Silent, 0});
  pending_.push_back(Command{1, Sound::Click, 1, muted ? Mood::Silent : wantedMood_, wantedPhase_});
  if (device_) SDL_UnlockAudioDevice(device_);
}

// ---- audio thread ------------------------------------------------------------------------------

void Audio::pumpCommands() {
  for (const auto& c : pending_) {
    if (c.type == 0) { if (!mutedAudio_) trigger(c.sound, c.arg); }
    else if (c.type == 2) {
      mutedAudio_ = c.arg > 0.5f;
      if (mutedAudio_) for (auto& v : voices_) v.active = false;
    } else {
      const Mood next = c.mood;
      if (next == mood_ && c.phase == phase_) continue;
      // Changing mood restarts on a bar line so layers enter in time.
      if (next != mood_) { step_ = 0; nextStep_ = clock_ + static_cast<std::int64_t>(0.05f * sr_); }
      mood_ = next; phase_ = std::max(0, c.phase);
    }
  }
  pending_.clear();
}

float Audio::noiseSample() {
  noiseState_ ^= noiseState_ << 13; noiseState_ ^= noiseState_ >> 17; noiseState_ ^= noiseState_ << 5;
  return static_cast<float>(noiseState_) / 2147483648.0f - 1.0f;
}

void Audio::updateFilter(Voice& v) {
  // RBJ biquad, Q = 1/sqrt(2).
  const float w = static_cast<float>(2 * kPi) * std::clamp(v.cutoff, 20.0f, sr_ * 0.45f) / sr_;
  const float cw = std::cos(w), alpha = std::sin(w) * 0.70710678f;
  const float a0 = 1 + alpha;
  if (v.filter == Filter::LowPass) {
    v.b0 = (1 - cw) / 2 / a0; v.b1 = (1 - cw) / a0; v.b2 = v.b0;
  } else {
    v.b0 = (1 + cw) / 2 / a0; v.b1 = -(1 + cw) / a0; v.b2 = v.b0;
  }
  v.a1 = -2 * cw / a0; v.a2 = (1 - alpha) / a0;
}

void Audio::startVoice(const VoiceDesc& d) {
  // Free voice, else steal the one closest to its end.
  Voice* slot = nullptr;
  for (auto& v : voices_) if (!v.active) { slot = &v; break; }
  if (!slot) {
    slot = &voices_[0];
    for (auto& v : voices_) if (v.end < slot->end) slot = &v;
  }
  Voice& v = *slot;
  v = Voice{};
  v.active = true; v.wave = d.wave; v.filter = d.filter; v.music = d.music;
  v.start = clock_ + static_cast<std::int64_t>(d.delay * sr_);
  v.end = v.start + static_cast<std::int64_t>(d.stopAfter * sr_);
  v.freq = d.from;
  v.freqSamples = static_cast<int>(d.rampSeconds * sr_);
  v.freqMul = rampMul(d.from, std::max(20.0f, d.to), v.freqSamples);
  v.attackSamples = static_cast<int>(d.attack * sr_);
  v.decaySamples = std::max(1, static_cast<int>((d.duration - d.attack) * sr_));
  if (v.attackSamples > 0) { v.gain = d.startGain; v.gainMulA = rampMul(d.startGain, d.peakGain, v.attackSamples); }
  else v.gain = d.peakGain;
  v.gainMulB = rampMul(d.peakGain, d.endGain, v.decaySamples);
  if (d.filter != Filter::None) {
    v.cutoff = d.cutoffFrom;
    v.cutoffSamples = static_cast<int>(d.cutoffSeconds * sr_);
    v.cutoffMul = rampMul(d.cutoffFrom, std::max(40.0f, d.cutoffTo), v.cutoffSamples);
    updateFilter(v);
  }
}

void Audio::tone(Wave wave, float from, float to, float duration, float gain, float delay) {
  VoiceDesc d;
  d.wave = wave; d.from = from; d.to = to; d.rampSeconds = duration;
  d.peakGain = gain; d.endGain = 0.001f; d.duration = duration;
  d.delay = delay; d.stopAfter = duration + 0.02f;
  startVoice(d);
}

void Audio::noise(float duration, float gain, float from, float to, float delay) {
  VoiceDesc d;
  d.wave = Wave::Noise;
  d.peakGain = gain; d.endGain = 0.001f; d.duration = duration;
  d.filter = Filter::LowPass; d.cutoffFrom = from; d.cutoffTo = to; d.cutoffSeconds = duration;
  d.delay = delay; d.stopAfter = duration + 0.02f;
  startVoice(d);
}

void Audio::arpeggio(std::initializer_list<float> notes, float step, Wave wave, float gain) {
  int n = 0;
  for (float note : notes) tone(wave, note, note, step * 1.8f, gain, static_cast<float>(n++) * step);
}

void Audio::trigger(Sound s, float arg) {
  using W = Wave;
  switch (s) {
    case Sound::Shoot: tone(W::Triangle, 900, 520, 0.05f, 0.035f); break;
    case Sound::Hit: noise(0.05f, 0.05f, 2600, 900); break;
    case Sound::Kill: tone(W::Square, 320, 110, 0.07f, 0.03f); break;
    case Sound::Gem: tone(W::Sine, 760 * arg, 1180 * arg, 0.06f, 0.05f); break;
    case Sound::Coin: arpeggio({1320, 1760}, 0.045f, W::Square, 0.03f); break;
    case Sound::Heart: arpeggio({520, 780}, 0.08f, W::Sine, 0.1f); break;
    case Sound::Crystal: arpeggio({660, 990, 1320}, 0.05f, W::Sine, 0.06f); break;
    case Sound::Level: arpeggio({523, 659, 784, 1046}, 0.08f, W::Triangle, 0.1f); break;
    case Sound::Power: arpeggio({784, 1175}, 0.07f, W::Sine, 0.09f); break;
    case Sound::Hurt: noise(0.12f, 0.16f, 900, 150); tone(W::Saw, 180, 90, 0.12f, 0.06f); break;
    case Sound::Warning: tone(W::Square, 440, 430, 0.09f, 0.04f); break;
    case Sound::Boom: noise(0.35f, 0.22f, 1200, 60); tone(W::Sine, 120, 40, 0.3f, 0.18f); break;
    case Sound::Chain: noise(0.12f, 0.07f, 6000, 2500); break;
    case Sound::Familiar: tone(W::Sine, 880, 1320, 0.07f, 0.03f); break;
    case Sound::Special: tone(W::Saw, 200, 1400, 0.35f, 0.08f); noise(0.3f, 0.08f, 4000, 500); break;
    case Sound::Boss: tone(W::Saw, 70, 45, 1.2f, 0.2f); noise(1, 0.12f, 500, 80); break;
    case Sound::Stage: tone(W::Square, 110, 55, 0.6f, 0.14f); noise(0.5f, 0.16f, 1500, 100); break;
    case Sound::BossDown: noise(1.2f, 0.25f, 2000, 50); arpeggio({392, 523, 659, 784, 1046}, 0.11f, W::Triangle, 0.12f); break;
    case Sound::Elite: arpeggio({220, 294, 220}, 0.12f, W::Saw, 0.07f); break;
    case Sound::Chest: arpeggio({659, 880, 1109, 1319}, 0.06f, W::Triangle, 0.1f); break;
    case Sound::Magnet: tone(W::Sine, 300, 1500, 0.4f, 0.08f); break;
    case Sound::Revive: arpeggio({392, 494, 587, 784}, 0.1f, W::Sine, 0.12f); break;
    case Sound::Phoenix: arpeggio({523, 784, 1046, 1568}, 0.09f, W::Triangle, 0.14f); noise(0.6f, 0.08f, 5000, 800); break;
    case Sound::Victory: arpeggio({523, 659, 784, 1046, 784, 1046, 1319}, 0.13f, W::Triangle, 0.13f); break;
    case Sound::Defeat: arpeggio({392, 330, 262, 196}, 0.18f, W::Sine, 0.12f); break;
    case Sound::Click: tone(W::Sine, 900, 700, 0.04f, 0.05f); break;
    case Sound::Signal: arpeggio({988, 1319}, 0.06f, W::Sine, 0.08f); break;
    case Sound::Encounter: arpeggio({392, 523, 659}, 0.09f, W::Triangle, 0.08f); break;
    case Sound::TeamCombo: arpeggio({659, 988, 1319}, 0.05f, W::Square, 0.05f); noise(0.2f, 0.06f, 6000, 1500); break;
    case Sound::Convergence: tone(W::Saw, 110, 1760, 0.5f, 0.08f); noise(0.6f, 0.14f, 3000, 60); break;
    case Sound::Loop: arpeggio({262, 392, 523, 784, 1046}, 0.12f, W::Triangle, 0.12f); break;
    case Sound::Count: break;
  }
}

// ---- music (music.js) --------------------------------------------------------------------------

void Audio::musicVoice(Wave wave, int midi, std::int64_t at, float duration, float gain, float attack, float filter) {
  VoiceDesc d;
  d.wave = wave; d.from = d.to = midiHz(midi);
  d.startGain = 0.0001f; d.peakGain = gain * kMusicBus; d.attack = attack; d.endGain = 0.0001f; d.duration = duration;
  if (filter > 0) { d.filter = Filter::LowPass; d.cutoffFrom = d.cutoffTo = filter; }
  d.stopAfter = duration + 0.05f;
  d.delay = static_cast<float>(at - clock_) / sr_;
  d.music = true;
  startVoice(d);
}

void Audio::scheduleMusicStep(std::int64_t at) {
  const MoodDef& m = kMoods[static_cast<std::size_t>(mood_)];
  const float sixteenth = 60.0f / m.bpm / 4.0f;
  const auto bar = static_cast<std::size_t>(step_ / 16);
  const int* base = m.major ? kVictory[bar % 4] : kProgression[bar % 4];
  const int root = kRoots[phase_ % 6];
  const int chord[3] = {base[0] + root, base[1] + root, base[2] + root};
  const int beat = static_cast<int>(step_ % 16);
  const float delay = static_cast<float>(at - clock_) / sr_;
  if (m.pad > 0 && beat == 0)
    for (int note : chord) musicVoice(Wave::Triangle, note, at, sixteenth * 16 * 0.98f, m.pad, 0.6f, 1400);
  if (m.arp > 0 && step_ % 2 == 0) {
    static constexpr int pattern[8] = {0, 1, 2, 1, 0, 2, 1, 2};
    const int note = chord[pattern[(step_ / 2) % 8]] + 12 + (mood_ == Mood::Fury && beat >= 8 ? 12 : 0);
    musicVoice(Wave::Triangle, note, at, sixteenth * 1.8f, m.arp, 0.01f, 3200);
  }
  if (m.bass > 0 && step_ % 4 != 3) musicVoice(Wave::Saw, chord[0] - 24, at, sixteenth * 1.6f, m.bass, 0.01f, 420);
  if (m.kick > 0 && beat % 4 == 0) {
    VoiceDesc d;
    d.wave = Wave::Sine; d.from = 120; d.to = 42; d.rampSeconds = 0.16f;
    d.peakGain = m.kick * kMusicBus; d.endGain = 0.0001f; d.duration = 0.2f; d.stopAfter = 0.22f; d.delay = delay; d.music = true;
    startVoice(d);
  }
  if (m.hat > 0 && step_ % 2 == 1) {
    VoiceDesc d;
    d.wave = Wave::Noise; d.filter = Filter::HighPass; d.cutoffFrom = d.cutoffTo = 7000;
    d.peakGain = m.hat * kMusicBus; d.endGain = 0.0001f; d.duration = 0.05f; d.stopAfter = 0.06f; d.delay = delay; d.music = true;
    startVoice(d);
  }
  if (m.lead > 0 && beat % 8 == 6) musicVoice(Wave::Square, chord[2] + 24, at, sixteenth * 3, m.lead, 0.01f, 2400);
  nextStep_ = at + static_cast<std::int64_t>(sixteenth * sr_);
  ++step_;
}

// ---- mixer -------------------------------------------------------------------------------------

void Audio::mix(float* out, int frames) {
  std::fill(out, out + frames, 0.0f);
  const std::int64_t blockStart = clock_, blockEnd = clock_ + frames;
  // Schedule every sixteenth that begins inside this block; its voices start sample-accurately.
  if (!mutedAudio_) while (mood_ != Mood::Silent && nextStep_ < blockEnd) scheduleMusicStep(std::max(nextStep_, blockStart));

  for (auto& v : voices_) {
    if (!v.active || v.start >= blockEnd) continue;
    const int from = static_cast<int>(std::max<std::int64_t>(0, v.start - blockStart));
    const int to = static_cast<int>(std::min<std::int64_t>(frames, v.end - blockStart));
    for (int i = from; i < to; ++i) {
      const float dt = v.freq / sr_;
      float s;
      switch (v.wave) {
        case Wave::Sine: {
          const float p = v.phase * 1024.0f;
          const int k = std::min(1023, static_cast<int>(p));
          const float f = p - static_cast<float>(k);
          s = kSine[static_cast<std::size_t>(k)] + (kSine[static_cast<std::size_t>(k) + 1] - kSine[static_cast<std::size_t>(k)]) * f;
          break;
        }
        case Wave::Triangle: s = v.phase < 0.5f ? 4 * v.phase - 1 : 3 - 4 * v.phase; break;
        case Wave::Square: {
          float t2 = v.phase + 0.5f; if (t2 >= 1) t2 -= 1;
          s = (v.phase < 0.5f ? 1.0f : -1.0f) + polyBlep(v.phase, dt) - polyBlep(t2, dt);
          break;
        }
        case Wave::Saw: s = 2 * v.phase - 1 - polyBlep(v.phase, dt); break;
        default: s = noiseSample(); break;
      }
      v.phase += dt;
      if (v.phase >= 1) v.phase -= 1;
      if (v.filter != Filter::None) {
        const float y = v.b0 * s + v.z1;
        v.z1 = v.b1 * s - v.a1 * y + v.z2;
        v.z2 = v.b2 * s - v.a2 * y;
        s = y;
        if (v.cutoffSamples > 0) {
          v.cutoff *= v.cutoffMul; --v.cutoffSamples;
          if (--v.coefCountdown <= 0) { updateFilter(v); v.coefCountdown = 16; }
        }
      }
      out[i] += s * v.gain;
      if (v.freqSamples > 0) { v.freq *= v.freqMul; --v.freqSamples; }
      if (v.attackSamples > 0) { v.gain *= v.gainMulA; --v.attackSamples; }
      else if (v.decaySamples > 0) { v.gain *= v.gainMulB; --v.decaySamples; }
    }
    if (v.end <= blockEnd) v.active = false;
  }
  for (int i = 0; i < frames; ++i) out[i] *= kMaster;
  clock_ = blockEnd;
}

} // namespace arcana::sdl
