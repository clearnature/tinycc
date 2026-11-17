/* #ident and #sccs are GCC directives tcc does not implement.  They must
   stay non-fatal (real headers use them) but must still be reported
   instead of vanishing without a word.  See C23-TASKS.md T0.3. */
#include <stdio.h>

#ident "tcc test"
#sccs "tcc test"

int main(void)
{
    printf("ident ok\n");
    return 0;
}
