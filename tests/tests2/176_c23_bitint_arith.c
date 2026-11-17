/* C23 _BitInt arithmetic (T4.2.8-T4.2.12).  Bit-precise operands are not
   promoted: the result keeps the wider width and is truncated to it, which
   changes both the value and sizeof of the expression.  Mixing with an
   ordinary integer converts to that type and drops the width. */
#include <stdio.h>

static_assert((_BitInt(8))200 + (_BitInt(8))100 == 44, "8-bit wrap");

int bound[(_BitInt(4))20];   /* 20 truncated to 4 bits is 4 */

int main(void)
{
    _BitInt(8) a8 = 100, b8 = 100;
    _BitInt(8) s8 = -100;
    unsigned _BitInt(8) u8 = 156;
    _BitInt(16) w16 = 30000;
    _BitInt(8) m8 = 5;

    printf("%d %d\n", (int)(a8 + b8), (int)sizeof(a8 + b8));
    printf("%d %d\n", (int)(s8 + u8), (int)sizeof(s8 + u8));
    printf("%d %d\n", (int)(m8 + w16), (int)sizeof(m8 + w16));
    printf("%d %d\n", (int)(m8 + 1), (int)sizeof(m8 + 1));
    printf("%d %d %d\n", (int)(a8 * 2), (int)sizeof(a8 * 2),
           (int)(sizeof bound / sizeof bound[0]));
    return 0;
}
