// Host unit test for src/weather/semver.h (the OTA "is this newer?" decision).
//   clang++ -std=c++17 -I src/weather tools/test_semver.cpp -o /tmp/test_semver && /tmp/test_semver
#include "semver.h"
#include <cstdio>

static int fails = 0;
static void expect(const char* a, const char* b, bool want) {
    bool got = semver::isNewer(a, b);
    if (got != want) { std::printf("FAIL isNewer(%s, %s) = %d, want %d\n", a, b, got, want); fails++; }
}

int main() {
    // core ordering
    expect("1.0.1", "1.0.0", true);
    expect("1.0.0", "1.0.1", false);
    expect("1.0.0", "1.0.0", false);
    expect("1.10.0", "1.9.9", true);          // numeric, not lexical
    expect("2.0.0", "1.99.99", true);
    // prereleases (semver 11.4)
    expect("1.0.0", "1.0.0-rc.1", true);       // release beats its prerelease
    expect("1.0.0-rc.1", "1.0.0", false);
    expect("1.0.0-rc.2", "1.0.0-rc.1", true);
    expect("1.0.0-rc.10", "1.0.0-rc.2", true); // numeric identifier
    expect("1.0.0-beta", "1.0.0-alpha", true);
    expect("1.0.0-alpha.1", "1.0.0-alpha", true);  // more fields wins
    expect("1.0.0-alpha.beta", "1.0.0-alpha.1", true);  // alnum > numeric
    expect("1.0.1-rc.1", "1.0.0", true);
    // build metadata ignored: a dev build on top of 1.2.0 equals 1.2.0
    expect("1.2.0", "1.2.0+dev.3.gabc1234", false);
    expect("1.2.1", "1.2.0+dev.3.gabc1234.dirty", true);
    expect("1.2.0+anything", "1.2.0", false);
    // dev builds with no tag
    expect("1.0.0", "0.0.0+dev.5194405", true);
    // malformed input never triggers an install
    expect("", "1.0.0", false);
    expect("garbage", "1.0.0", false);
    expect("1.0", "0.9.0", false);
    expect("1.0.0-", "0.9.0", false);
    expect("v1.0.0", "0.9.0", false);
    expect(nullptr, "0.9.0", false);
    // corrupt local version loses to any valid release
    expect("1.0.0", "unknown", true);
    if (fails) { std::printf("%d failure(s)\n", fails); return 1; }
    std::printf("semver: all tests passed\n");
    return 0;
}
