// Duplicates are numbered, never "copy": "Layer" gives "Layer 2", then "Layer 3" (the next number no other one has).
#include "Naming.h"
#include <cstdio>

int main()
{
    int failures = 0;
    auto check = [&](bool ok, const char *why) {
        if (!ok) { ++failures; std::printf("FAIL naming: %s\n", why); }
    };
    check(numberedName("Layer", {"Layer"}) == "Layer 2", "a first copy is 2");
    check(numberedName("Layer", {"Layer", "Layer 2"}) == "Layer 3", "the next free number");
    check(numberedName("Layer 2", {"Layer 2"}) == "Layer 3", "a numbered name counts on from its number");
    check(numberedName("Layer 4", {"Layer 4", "Layer 5"}) == "Layer 6", "gaps are not reused below the last");
    check(numberedName("Fixture", {}) == "Fixture 2", "a name not taken still gets its number");
    check(numberedName("Preset 3 x", {"Preset 3 x"}) == "Preset 3 x 2", "a name ending in a non-number keeps it");
    check(numberedName("", {}) == "2", "an empty name is a number");
    std::printf("naming: %s (%d failure%s)\n", failures ? "FAIL" : "OK", failures, failures == 1 ? "" : "s");
    return failures ? 1 : 0;
}
