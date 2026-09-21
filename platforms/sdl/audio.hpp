#pragma once

#include "arcana/native/static_vector.hpp"

#include <SDL.h>

#include <array>
#include <chrono>
#include <cstdint>

namespace arcana::sdl {

// Port of the web client's src/audio.js + src/music.js. Everything is synthesized, as in the
// browser: WebAudio oscillators, noise, biquad filters and exponential ramps become a small
// software synth running in the SDL audio callback (so PC, Switch and Vita sound the same and no
// audio file is shipped). The generative soundtrack is sequenced sample-accurately in the same
// callback. The game thread only enqueues commands; nothing allocates after init().
enum class Sound : std::uint8_t {
  Shoot, Hit, Kill, Gem, Coin, Heart, Crystal, Level, Power, Hurt, Warning, Boom, Chain, Familiar,
  Special, Boss, Stage, BossDown, Elite, Chest, Magnet, Revive, Phoenix, Victory, Defeat, Click,
  Signal, Encounter, TeamCombo, Convergence, Loop, Count
};
enum class Mood : std::uint8_t { Silent, Menu, Horde, Boss, Fury, Victory, Defeat };

class Audio {
public:
  static constexpr int kSampleRate = 48000;

  // Opens the default output device. Returns false (and stays silent) when there is none.
  bool init();
  void shutdown();

  void play(Sound sound);
  void music(Mood mood, int phase);
  void setMuted(bool muted);
  [[nodiscard]] bool muted() const { return muted_; }

  // Offline rendering (tests / --audio-demo): no device, the caller pulls mono float samples.
  void initOffline();
  void render(float* out, int frames);
  void pumpCommands();

  static const char* name(Sound sound);

private:
  enum class Wave : std::uint8_t { Sine, Triangle, Square, Saw, Noise };
  enum class Filter : std::uint8_t { None, LowPass, HighPass };

  struct VoiceDesc {
    Wave wave{Wave::Sine};
    float from{440}, to{440}, rampSeconds{};  // oscillator frequency, exponential ramp
    float startGain{}, peakGain{0.1f}, attack{}, endGain{0.001f}, duration{0.1f};
    Filter filter{Filter::None};
    float cutoffFrom{}, cutoffTo{}, cutoffSeconds{};
    float delay{}, stopAfter{0.12f};
    bool music{};
  };

  struct Voice {
    bool active{};
    Wave wave{};
    Filter filter{};
    bool music{};
    std::int64_t start{}, end{};
    double phase{};
    float freq{}, freqMul{1};
    int freqSamples{};
    float gain{}, gainMulA{1}, gainMulB{1};
    int attackSamples{}, decaySamples{};
    float cutoff{}, cutoffMul{1};
    int cutoffSamples{};
    float b0{}, b1{}, b2{}, a1{}, a2{}, z1{}, z2{};
    int coefCountdown{};
  };

  struct Command { std::uint8_t type{}; Sound sound{}; float arg{1}; Mood mood{}; int phase{}; };

  static void callback(void* self, Uint8* stream, int bytes);
  void mix(float* out, int frames);
  void startVoice(const VoiceDesc& d);
  void tone(Wave wave, float from, float to, float duration, float gain, float delay = 0);
  void noise(float duration, float gain, float from, float to, float delay = 0);
  void arpeggio(std::initializer_list<float> notes, float step, Wave wave, float gain);
  void trigger(Sound sound, float arg);
  void scheduleMusicStep(std::int64_t at);
  void musicVoice(Wave wave, int midi, std::int64_t at, float duration, float gain, float attack, float filter);
  void updateFilter(Voice& v);
  float noiseSample();

  SDL_AudioDeviceID device_{};
  SDL_AudioSpec spec_{};
  bool offline_{};
  bool muted_{};
  std::array<Voice, 72> voices_{};
  std::int64_t clock_{};   // samples rendered so far (audio thread)
  std::uint32_t noiseState_{0x12345678u};

  // Game-thread side.
  native::StaticVector<Command, 128> pending_;
  std::array<double, static_cast<std::size_t>(Sound::Count)> lastPlayed_ = [] { std::array<double, static_cast<std::size_t>(Sound::Count)> a{}; a.fill(-1e9); return a; }();
  double comboAt_{-10};
  int combo_{};
  std::chrono::steady_clock::time_point epoch_{std::chrono::steady_clock::now()};
  Mood wantedMood_{Mood::Silent};
  int wantedPhase_{-1};

  // Audio-thread music state.
  Mood mood_{Mood::Silent};
  int phase_{};
  std::int64_t step_{}, nextStep_{};
  bool mutedAudio_{};
  native::StaticVector<float, 4096> scratch_;
};

} // namespace arcana::sdl
