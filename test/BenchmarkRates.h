#pragma once
#include <algorithm>
#include <cmath>

// Acceptance is fixed by the requested render rate, never by achieved FPS.
// A slower source may repeat pictures; it does not lower the render target.
struct BenchmarkRates {
    double layerTarget;
    bool renderPassed, layerPassed;
    bool passed() const { return renderPassed && layerPassed; }
};

inline BenchmarkRates benchmarkRates(double requested, double rendered, double source, double shown)
{
    const auto meets = [](double actual, double target) {
        return std::isfinite(actual) && std::isfinite(target) && target > 0 && actual >= .95 * target;
    };
    const double target = std::min(source, requested);
    return {target, meets(rendered, requested), meets(shown, target)};
}
