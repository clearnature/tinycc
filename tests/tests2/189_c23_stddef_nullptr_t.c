/* C23 adds nullptr_t and the unreachable macro to <stddef.h>, as the
   stddef.h gcc ships does; tcc's copy had neither.  nullptr_t is the type of
   nullptr, so it is a pointer-sized void pointer here.
   See C23-TASKS.md T6.12. */
#include <stddef.h>
#include <stdio.h>

static void takes_ptr(int *p)
{
    puts(p == nullptr ? "null" : "not null");
}

int main(void)
{
    nullptr_t n = nullptr;
    int x = 1;
    int *p = &x;

    printf("%d %d %d\n", (int)sizeof(nullptr_t), (int)sizeof n,
           (int)sizeof(nullptr));
    takes_ptr(n);
    takes_ptr(p);
    if (!n)
        puts("nullptr_t is falsy when null");
    else
        unreachable();
    printf("%ld\n", (long)__STDC_VERSION_STDDEF_H__);
    return 0;
}
