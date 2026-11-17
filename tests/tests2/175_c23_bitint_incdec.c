/* C23 _BitInt(N) compound assignment and inc/dec go through vstore, so the
   truncation added for conversions applies here too (T4.2.7).
   Note a++ from 7 in a signed 4-bit type is -8: 8 does not fit. */
#include <stdio.h>

int main(void)
{
    _BitInt(4) a = 7;
    unsigned _BitInt(4) u = 15;
    _BitInt(4) b = 3;

    a++;
    u++;
    b += 9;
    printf("%d %u %d", (int)a, (unsigned)u, (int)b);

    a += 8;
    u -= 1;
    b *= 3;
    printf(" | %d %u %d\n", (int)a, (unsigned)u, (int)b);
    return 0;
}
