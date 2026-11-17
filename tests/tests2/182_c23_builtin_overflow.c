/* GNU __builtin_{add,sub,mul}_overflow and their _p forms.

   The operation is carried out in infinite precision and then tested against
   the range of the type pointed to by the third argument, which may be
   narrower than both operands; the wrapped result is stored either way.  The
   _p forms take an expression whose type is the destination and only report.

   Expectations come from gcc 16.2.0: the whole matrix (every destination
   type against every operand type, boundary values) was diffed against gcc
   before this file was reduced to the interesting corners. */
#include <stdio.h>

int main(void)
{
    signed char sc;
    unsigned char uc;
    short ss;
    unsigned short us;
    int si;
    unsigned su;
    long long ll;
    unsigned long long ull;
    int ov;

    /* the flag says whether the exact value fits, the stored value wraps */
    ov = __builtin_add_overflow(100, 100, &sc);
    printf("%d %d\n", ov, (int)sc);
    ov = __builtin_add_overflow(100, 27, &sc);
    printf("%d %d\n", ov, (int)sc);
    ov = __builtin_sub_overflow(-1, 1, &sc);
    printf("%d %d\n", ov, (int)sc);
    ov = __builtin_sub_overflow(-100, 100, &sc);
    printf("%d %d\n", ov, (int)sc);
    ov = __builtin_mul_overflow(16, 8, &sc);
    printf("%d %d\n", ov, (int)sc);

    /* a narrow destination with wide operands */
    ov = __builtin_add_overflow(70000, 0, &sc);
    printf("%d %d\n", ov, (int)sc);
    ov = __builtin_mul_overflow(0x100000000LL, 1, &uc);
    printf("%d %d\n", ov, (int)uc);
    ov = __builtin_add_overflow(-1, 1, &uc);
    printf("%d %d\n", ov, (int)uc);
    ov = __builtin_add_overflow(-5, 0, &uc);
    printf("%d %d\n", ov, (int)uc);

    /* 64-bit corners */
    ov = __builtin_add_overflow(0x7fffffffffffffffLL, 1, &ll);
    printf("%d %lld\n", ov, ll);
    ov = __builtin_sub_overflow(-0x7fffffffffffffffLL, 2, &ll);
    printf("%d %lld\n", ov, ll);
    ov = __builtin_mul_overflow(-1LL, -1LL, &ll);
    printf("%d %lld\n", ov, ll);
    ov = __builtin_mul_overflow(-3037000500LL, 3037000500LL, &ll);
    printf("%d %lld\n", ov, ll);
    ov = __builtin_add_overflow(0xffffffffffffffffULL, 2, &ull);
    printf("%d %llu\n", ov, ull);
    ov = __builtin_mul_overflow(0xffffffffULL, 0xffffffffULL, &ull);
    printf("%d %llu\n", ov, ull);
    ov = __builtin_mul_overflow(0x100000000ULL, 0x100000000ULL, &ull);
    printf("%d %llu\n", ov, ull);

    /* mixed signedness and width */
    ov = __builtin_add_overflow(4000000000u, -1, &si);
    printf("%d %d\n", ov, si);
    ov = __builtin_add_overflow(-1, 1, &su);
    printf("%d %u\n", ov, su);
    ov = __builtin_sub_overflow(0u, 1u, &su);
    printf("%d %u\n", ov, su);
    ov = __builtin_add_overflow(0x100000000ULL, -1LL, &si);
    printf("%d %d\n", ov, si);
    ov = __builtin_add_overflow(30000, 30000, &ss);
    printf("%d %d\n", ov, (int)ss);
    ov = __builtin_add_overflow(60000, 60000, &us);
    printf("%d %d\n", ov, (int)us);

    /* the _p forms report without storing */
    printf("%d %d %d\n",
           __builtin_add_overflow_p(200, 200, (signed char)0),
           __builtin_add_overflow_p(1, 1, (signed char)0),
           __builtin_sub_overflow_p(0u, 1u, (unsigned)0));
    printf("%d %d %d\n",
           __builtin_mul_overflow_p(0x10000u, 0x10000u, (unsigned)0),
           __builtin_add_overflow_p(0x7fffffffffffffffLL, 0, (long long)0),
           __builtin_add_overflow_p(0x7fffffffffffffffLL, 1, (long long)0));
    printf("%d\n", (int)sizeof(__builtin_add_overflow(1, 1, &si)));
    return 0;
}
