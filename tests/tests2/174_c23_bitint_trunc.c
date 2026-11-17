/* C23 _BitInt(N) conversion truncation (T4.2.4-T4.2.6).  A value converted
   to a _BitInt(N) is reduced to N bits: signed values are sign-extended
   from bit N-1, unsigned values are zero-extended.  All four paths are
   covered: cast, local initializer, local assignment, global initializer.
   Arithmetic (a + b) is still container-width; that is T4.2.8-T4.2.12. */
#include <stdio.h>

unsigned _BitInt(4) g = 100;   /* global initializer -> 4 */

int main(void)
{
    _BitInt(4) a = 9;            /* signed: 9 -> -7 */
    _BitInt(4) b = -9;           /* -9 fits */
    unsigned _BitInt(4) u;       /* assignment */
    unsigned _BitInt(4) v = 15;
    _BitInt(12) c = 3000;        /* -> -1096 */
    _BitInt(8) d = 300;          /* -> 44 */

    u = 100;                     /* -> 4 */
    printf("%d %d %u %u %d %d %u\n", (int)a, (int)b, (unsigned)u,
           (unsigned)v, (int)c, (int)d, (unsigned)g);
    printf("%d %d\n", (int)(unsigned _BitInt(4))100,   /* cast -> 4 */
           (int)(_BitInt(4))9);                        /* -> -7 */
    return 0;
}
