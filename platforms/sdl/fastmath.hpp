#pragma once
// Visual-only trigonometry. The PSP's Allegrex has no sin/cos instructions (libm does them in
// software), and the renderer needs hundreds per frame for circles, particles and poses. This
// polynomial is ~1e-3 accurate, invisible at screen scale, and a few multiply-adds everywhere.
// Never use it in the simulation: gameplay keeps std:: math so every platform plays the same.
#include <cmath>

namespace arcana::sdl {

inline void fastSinCos(float a, float& s, float& c) {
  constexpr float kInvTau = 0.15915494309f, kTau = 6.28318530718f, kPi = 3.14159265359f, kHalfPi = 1.57079632679f;
  a -= kTau * std::floor(a * kInvTau + 0.5f); // [-pi, pi]
  // sin on [-pi/2, pi/2] after folding; cos from the complementary angle.
  auto sinFolded = [&](float x) {
    if (x > kHalfPi) x = kPi - x;
    else if (x < -kHalfPi) x = -kPi - x;
    const float x2 = x * x;
    return x * (1.0f + x2 * (-0.16666667f + x2 * (0.0083333310f + x2 * -0.00019840874f)));
  };
  s = sinFolded(a);
  float b = a + kHalfPi;
  if (b > kPi) b -= kTau;
  c = sinFolded(b);
}
inline float fastSin(float a) { float s, c; fastSinCos(a, s, c); return s; }
inline float fastCos(float a) { float s, c; fastSinCos(a, s, c); return c; }

} // namespace arcana::sdl
