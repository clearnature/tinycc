/* GNU bit builtins: __builtin_popcount/l/ll, __builtin_clz/l/ll,
   __builtin_ctz/l/ll, plus the ffs/parity/clrsb family that tcc's
   lib/builtin.c already provides.  Values are compared against gcc;
   the undefined cases (ctz(0), clz(0)) are deliberately left out. */
#include <stdio.h>

int main(void)
{
    unsigned u = 0xf0f0u;
    unsigned long ul = 0xff00ul;
    unsigned long long ull = 0xff0000000000ULL;

    printf("%d %d %d\n", (int)sizeof(__builtin_popcount(u)),
           (int)sizeof(__builtin_popcountl(ul)),
           (int)sizeof(__builtin_popcountll(ull)));
    printf("%d %d %d\n", __builtin_popcount(u), __builtin_popcountl(ul),
           __builtin_popcountll(ull));
    printf("%d %d %d\n", __builtin_popcount(0), __builtin_popcount(~0u),
           __builtin_popcountll(~0ULL));
    printf("%d %d %d\n", __builtin_clz(1u), __builtin_clzl(1ul),
           __builtin_clzll(1ULL));
    printf("%d %d %d\n", __builtin_clz(0x80000000u), __builtin_clzl(0x80000000ul),
           __builtin_clzll(1ULL << 63));
    printf("%d %d %d\n", __builtin_ctz(1u), __builtin_ctzl(0x100ul),
           __builtin_ctzll(1ULL << 40));
    printf("%d %d %d\n", __builtin_ctz(0x80000000u), __builtin_ctzll(0x8000000000000000ULL),
           __builtin_clz(0x00010000u));
    printf("%d %d %d\n", __builtin_ffs(0), __builtin_ffs(0x100),
           __builtin_ffsll(1LL << 40));
    printf("%d %d\n", __builtin_parity(7u), __builtin_parityll(0xffULL));
    printf("%d %d\n", __builtin_clrsb(0), __builtin_clrsb(-1));
    /* results are ordinary ints, usable in arithmetic */
    printf("%d\n", __builtin_popcount(0xffffu) * 2 + __builtin_clz(1u));
    return 0;
}
