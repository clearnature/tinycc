/* C23 digit separators, in decimal, hex, binary, floats and exponents.
   See C23-TASKS.md T1.1. */
#include <stdio.h>

int main(void)
{
    printf("%d %d %d %lu %d\n", 1'000, 0x1'F, 0b1010'1010,
           (unsigned long)1'000'000, 0'123);
    printf("%.3f %.3f %.3f\n", 1'000.5, 1.5e1'0, 0x1'F.8p0);
    return 0;
}
