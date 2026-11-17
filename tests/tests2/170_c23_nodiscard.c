/* C23 [[nodiscard]]: dropping the result of a nodiscard call warns, while
   the (void) cast, using the value, and calls in nested positions do not.
   See C23-TASKS.md T3.3. */
#include <stdio.h>

[[nodiscard]] int f(void) { return 1; }
int plain(void) { return 2; }

int main(void)
{
    (void)f();          /* suppressed by the void cast */
    printf("%d\n", f()); /* value used */
    plain();            /* not nodiscard */
    f();                /* warns */
    return 0;
}
