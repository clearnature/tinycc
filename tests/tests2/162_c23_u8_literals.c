/* C23 u8 character and string literals.  A u8 character constant has
   type unsigned char (sizeof 1), the string form is a plain char array.
   See C23-TASKS.md T1.2. */
#include <stdio.h>

int main(void)
{
    printf("%d %d %d\n", (int)sizeof(u8'a'), (int)u8'a', (int)u8'0');
    printf("%s %d\n", u8"hi", (int)sizeof(u8"hi"));
    return 0;
}
