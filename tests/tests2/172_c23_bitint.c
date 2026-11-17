/* C23 _BitInt(N) for N <= 64: the value is represented by the smallest
   standard integer type that holds N bits, so sizeof matches C23, and
   assignment, casts and printing behave as they do for that type.
   Not covered here (C23-TASKS.md T4.2): the exact N-bit wrap-around and
   the promoted width of an expression such as a + b.
   See C23-TASKS.md T4.1. */
#include <stdio.h>

int main(void)
{
    _BitInt(8) a = 120;
    unsigned _BitInt(8) ub = 250;
    _BitInt(16) s16 = -300;
    _BitInt(32) s32 = 70000;
    unsigned _BitInt(32) u32 = 4000000000u;
    _BitInt(64) s64 = -5000000000LL;

    printf("%d %d %d %d %u %lld\n", (int)a, (int)ub, (int)s16, (int)s32,
           (unsigned)u32, (long long)s64);

    printf("%d %d %d %d %d %d\n",
           (int)sizeof(_BitInt(2)), (int)sizeof(_BitInt(9)),
           (int)sizeof(_BitInt(16)), (int)sizeof(_BitInt(17)),
           (int)sizeof(_BitInt(33)), (int)sizeof(_BitInt(64)));
    printf("%d %d %d\n", (int)sizeof(unsigned _BitInt(1)),
           (int)sizeof(unsigned _BitInt(12)), (int)(_BitInt(8))100);
    return 0;
}
