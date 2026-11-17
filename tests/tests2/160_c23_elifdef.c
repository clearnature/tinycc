/* C23 #elifdef / #elifndef.  gcc accepts them in every dialect, so no
   standard gate here.  Prints which branch was taken.
   See C23-TASKS.md T1.3. */
#include <stdio.h>

#define B 1

int main(void)
{
#ifdef A
    printf("A\n");
#elifdef B
    printf("B\n");
#elifndef C
    printf("C\n");
#else
    printf("none\n");
#endif

#ifndef D
    printf("noD\n");
#elifdef B
    printf("D-or-B\n");
#endif
    return 0;
}
