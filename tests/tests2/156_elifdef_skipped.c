/* #elifdef used to be dropped silently when it appeared inside a skipped
   block: the skip scanner only looked for #else/#elif/#endif, so neither
   branch was compiled and there was no diagnostic.  It is implemented now
   (C23), so this asserts the branch actually taken.
   See C23-TASKS.md T0.4 and T1.3. */
#include <stdio.h>

#define B 1

int main(void)
{
#ifdef A
    printf("A\n");
#elifdef B
    printf("B\n");
#else
    printf("none\n");
#endif
    return 0;
}
