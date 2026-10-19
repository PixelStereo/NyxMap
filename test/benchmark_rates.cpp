#include "BenchmarkRates.h"
#include <cstdio>
#include <limits>

int main()
{
    int failures = 0;
    auto check = [&](bool ok, const char *why) {
        if (!ok) { ++failures; std::printf("FAIL benchmark_rates: %s\n", why); }
    };
    check(!benchmarkRates(60, 30, 60, 30).passed(), "a render bottleneck must not lower the layer target");
    check(!benchmarkRates(60, 30, 24, 24).passed(), "full source delivery cannot hide a missed render target");
    check(!benchmarkRates(60, 60, 60, 30).passed(), "full render rate cannot hide decoder drops");
    check(benchmarkRates(60, 60, 24, 24).passed(), "24 fps source can repeat at 60 Hz");
    check(benchmarkRates(30, 30, 60, 30).passed(), "60 fps source only needs 30 deliveries at 30 Hz");
    check(benchmarkRates(60, 57, 60, 57).passed(), "documented five-percent tolerance includes the boundary");
    check(!benchmarkRates(60, 56.9, 60, 60).passed(), "render below tolerance fails independently");
    check(!benchmarkRates(60, 60, 60, 56.9).passed(), "layer below tolerance fails independently");
    check(!benchmarkRates(0, 0, 60, 0).passed(), "zero target cannot pass");
    check(!benchmarkRates(60, std::numeric_limits<double>::quiet_NaN(), 60, 60).passed(), "missing render rate fails");
    return failures ? 1 : 0;
}
