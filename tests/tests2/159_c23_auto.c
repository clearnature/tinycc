/* C23 auto type inference.  The declared type comes from the initializer,
   and the value is stored in an object of that type.
   See C23-TASKS.md T3.1. */
#include <stdio.h>

int main(void)
{
    int n = 3;
    auto i = 1;          /* int */
    auto d = 1.5;        /* double */
    auto p = &n;         /* int * */
    auto s = "hi";       /* char * */
    struct S { int a, b; } s1 = {4, 5};
    auto s2 = s1;        /* struct S */

    *p = 7;
    printf("%d %.1f %d %s %d %d %d %d\n", i, d, *p, s, s2.a, s2.b,
           (int)sizeof i, (int)sizeof d);
    return 0;
}
