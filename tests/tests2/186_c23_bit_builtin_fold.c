/* The bit builtins (popcount and friends) are ordinary functions in libtcc1,
   so a constant argument used to stay a call and could not appear in a
   constant expression.  A constant is now folded, which makes the usual
   static_assert / enum / array-bound uses work, and the folded value must
   agree with what the runtime function returns for a volatile argument.
   See C23-TASKS.md T6.8. */
#include <stdio.h>

static_assert(__builtin_popcount(0xffu) == 8, "popcount");
static_assert(__builtin_parity(7u) == 1, "parity");
static_assert(__builtin_ffs(0x100u) == 9, "ffs");
static_assert(__builtin_clz(1u) == 31, "clz");
static_assert(__builtin_ctz(0x80u) == 7, "ctz");
static_assert(__builtin_clrsb(0) == 31, "clrsb");
static_assert(__builtin_popcountll(0xff0000000000ULL) == 8, "popcountll");
static_assert(__builtin_ctzll(1ULL << 40) == 40, "ctzll");

enum { BITS = __builtin_popcount(0xf0fu) };

static int bound[__builtin_popcount(0x1fu)];

/* a narrow argument is converted to the parameter type before folding */
static_assert(__builtin_popcount((unsigned char)0xff) == 8, "narrow arg");

static volatile unsigned vr;
static volatile unsigned long long vl;

int main(void)
{
    unsigned u = 0x12345678u;
    unsigned long long w = 0x0f0f0f0f0f0f0f0fULL;

    printf("%d %d\n", BITS, (int)(sizeof bound / sizeof bound[0]));
    printf("%d %d\n", __builtin_popcount(u), __builtin_popcount(u));
    vr = u;
    printf("%d\n", __builtin_popcount(vr));
    printf("%d %d\n", __builtin_clz(u), __builtin_ctz(u));
    printf("%d %d\n", __builtin_popcountll(w), __builtin_parityll(w));
    vl = w;
    printf("%d %d\n", __builtin_clzll(vl), __builtin_clrsbll(vl));
    printf("%d %d\n", __builtin_ffs(u), __builtin_ffsll(1ULL << 40));
    return 0;
}
