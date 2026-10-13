#pragma once
// Semantic-version precedence (semver.org §11), dependency-free so it can be
// unit-tested on the host: tools/test_semver.cpp.
//   * MAJOR.MINOR.PATCH compared numerically
//   * a prerelease (-rc.1) sorts BEFORE its release
//   * prerelease identifiers compare dot by dot: numeric < alphanumeric,
//     numbers numerically, text lexically, more fields > fewer
//   * build metadata (+dev.3.gabc) is ignored
#include <stdlib.h>
#include <string.h>

namespace semver {

struct Parsed { long n[3]; const char* pre; size_t preLen; bool ok; };

inline Parsed parse(const char* s) {
    Parsed p = {{0, 0, 0}, "", 0, false};
    if (!s) return p;
    const char* end = s + strcspn(s, "+");              // drop build metadata
    const char* c = s;
    for (int i = 0; i < 3; i++) {
        if (c >= end || *c < '0' || *c > '9') return p;
        char* next;
        p.n[i] = strtol(c, &next, 10);
        c = next;
        if (i < 2) { if (*c != '.') return p; c++; }
    }
    if (c < end) {
        if (*c != '-' || c + 1 >= end) return p;
        p.pre = c + 1;
        p.preLen = (size_t)(end - (c + 1));
    }
    p.ok = true;
    return p;
}

inline bool isNumeric(const char* s, size_t n) {
    if (!n) return false;
    for (size_t i = 0; i < n; i++) if (s[i] < '0' || s[i] > '9') return false;
    return true;
}

inline int cmpPre(const char* a, size_t al, const char* b, size_t bl) {
    size_t ia = 0, ib = 0;
    for (;;) {
        size_t ea = ia; while (ea < al && a[ea] != '.') ea++;
        size_t eb = ib; while (eb < bl && b[eb] != '.') eb++;
        const char* x = a + ia; size_t xl = ea - ia;
        const char* y = b + ib; size_t yl = eb - ib;
        bool nx = isNumeric(x, xl), ny = isNumeric(y, yl);
        int c;
        if (nx && ny) {
            long vx = strtol(x, nullptr, 10), vy = strtol(y, nullptr, 10);
            c = (vx > vy) - (vx < vy);
        } else if (nx != ny) {
            c = nx ? -1 : 1;
        } else {
            size_t m = xl < yl ? xl : yl;
            c = memcmp(x, y, m);
            if (!c) c = (xl > yl) - (xl < yl);
            c = (c > 0) - (c < 0);
        }
        if (c) return c;
        bool moreA = ea < al, moreB = eb < bl;
        if (!moreA || !moreB) return moreA == moreB ? 0 : (moreA ? 1 : -1);
        ia = ea + 1; ib = eb + 1;
    }
}

// true if a has higher precedence than b. An unparseable a is never newer;
// an unparseable b (corrupt local version) loses to any valid a.
inline bool isNewer(const char* a, const char* b) {
    Parsed x = parse(a), y = parse(b);
    if (!x.ok) return false;
    if (!y.ok) return true;
    for (int i = 0; i < 3; i++)
        if (x.n[i] != y.n[i]) return x.n[i] > y.n[i];
    if (!x.preLen || !y.preLen) return !x.preLen && y.preLen;
    return cmpPre(x.pre, x.preLen, y.pre, y.preLen) > 0;
}

}  // namespace semver
