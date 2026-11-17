/* __STDC_VERSION__ follows -std= (FLAGS adds -std=c23), and C89 must not
   define it at all.  See C23-TASKS.md T0.1/T0.2. */
#include <stdio.h>

int main(void)
{
#ifdef __STDC_VERSION__
    printf("%ld\n", (long)__STDC_VERSION__);
#else
    printf("undefined\n");
#endif
    return 0;
}
