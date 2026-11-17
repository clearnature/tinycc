/* C23 spelling of _Thread_local.  A linked executable is used because
   tcc's -run does not support thread-local storage, which is unrelated to
   the spelling (the Makefile sets NORUN for this case).
   See C23-TASKS.md T2.3. */
#include <stdio.h>

thread_local int tls_var = 7;

int main(void)
{
    printf("%d\n", tls_var);
    return 0;
}
