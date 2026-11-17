/* ISO C23 7.20 checked integer arithmetic: tcc ships stdckdint.h because the
   C library often does not (glibc 2.43 has stdbit.h but no stdckdint.h).
   The macros wrap __builtin_{add,sub,mul}_overflow, so the flag says whether
   the exact value fits the destination type and the destination receives the
   wrapped result either way.
   See C23-TASKS.md T6.7. */
#include <stdckdint.h>
#include <stdio.h>

int main(void)
{
    int r = 0, a = 2, b = 3, ov;
    unsigned u = 0;
    signed char sc = 0;
    long long ll = 0;

    ov = ckd_add(&r, a, b);
    printf("%d %d\n", ov, r);
    ov = ckd_add(&r, 2000000000, 2000000000);
    printf("%d %d\n", ov, r);
    ov = ckd_sub(&u, 0u, 1u);
    printf("%d %u\n", ov, u);
    ov = ckd_mul(&r, 65536, 65536);
    printf("%d %d\n", ov, r);
    ov = ckd_add(&sc, 100, 100);
    printf("%d %d\n", ov, (int)sc);
    ov = ckd_add(&ll, 0x7fffffffffffffffLL, 1);
    printf("%d %lld\n", ov, ll);
    ov = ckd_mul(&ll, 0xffffffffLL, 0xffffffffLL);
    printf("%d %lld\n", ov, ll);

    /* the macros yield bool */
    printf("%d %d\n", (int)sizeof(ckd_add(&r, a, b)), ckd_add(&r, a, b) == 0);
    printf("%ld\n", (long)__STDC_VERSION_STDCKDINT_H__);
    return 0;
}
