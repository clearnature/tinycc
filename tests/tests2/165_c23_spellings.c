/* C23 spellings and nullptr: bool/true/false, static_assert, alignas,
   alignof, thread_local, typeof_unqual.  nullptr is a real void * value, so
   its size is the pointer size and it can be compared with any pointer.
   See C23-TASKS.md T2.1-T2.4. */
#include <stdio.h>

static_assert(sizeof(int) == 4, "int is 4 bytes");

int main(void)
{
    bool b = true, c = false;
    void *p = nullptr;
    int *q = nullptr;
    const int ci = 3;
    typeof_unqual(ci) u = 4;
    alignas(16) char buf[4];
    int x = 7;

    u = 5;
    (void)buf;
    printf("%d %d %d %d %d %d\n", b, c, p == nullptr, q == nullptr,
           (int)alignof(double), u);
    printf("%d\n", (int)sizeof(typeof_unqual(ci)));
    printf("%d %d\n", (int)sizeof(nullptr), (int)sizeof(void *));
    q = &x;
    printf("%d %d %d\n", q == nullptr, p == nullptr, nullptr ? 1 : 0);
    printf("%d %d\n", nullptr == 0, (void *)0 == nullptr);
    return 0;
}
