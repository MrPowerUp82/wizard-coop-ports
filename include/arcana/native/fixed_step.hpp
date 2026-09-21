#pragma once
#include <algorithm>

namespace arcana::native {

// Fixed simulation clock: render cadence can vary without changing game speed.
class FixedStep {
public:
  explicit FixedStep(double hz = 60.0, int maxCatchUpSteps = 4)
      : step_(1.0 / hz), maxCatchUpSteps_(maxCatchUpSteps) {}

  template <class Fn>
  int advance(double frameSeconds, Fn&& update) {
    accumulator_ += std::clamp(frameSeconds, 0.0, step_ * maxCatchUpSteps_);
    int steps = 0;
    while (accumulator_ >= step_ && steps < maxCatchUpSteps_) {
      update(step_);
      accumulator_ -= step_;
      ++steps;
    }
    if (steps == maxCatchUpSteps_ && accumulator_ >= step_) accumulator_ = 0.0;
    return steps;
  }

  [[nodiscard]] double alpha() const noexcept { return accumulator_ / step_; }
  [[nodiscard]] double stepSeconds() const noexcept { return step_; }
  void reset() noexcept { accumulator_ = 0.0; }

private:
  double step_;
  double accumulator_{};
  int maxCatchUpSteps_;
};

} // namespace arcana::native
