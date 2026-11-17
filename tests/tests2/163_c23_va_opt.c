/* C23 __VA_OPT__: its content is kept only when the variable argument is
   non-empty, and the parentheses are dropped.
   See C23-TASKS.md T1.4. */
#include <stdio.h>

#define LOG(fmt, ...) printf(fmt __VA_OPT__(,) __VA_ARGS__)
#define OPT(x, ...) printf("%d" __VA_OPT__("-%d") "\n", x __VA_OPT__(, __VA_ARGS__))
#define STR(...) puts("a" __VA_OPT__("b"))

int main(void)
{
    LOG("no args\n");
    LOG("one %d\n", 7);
    LOG("%d %d\n", 1, 2);
    OPT(1);
    OPT(2, 3);
    STR();
    STR(1);
    return 0;
}
