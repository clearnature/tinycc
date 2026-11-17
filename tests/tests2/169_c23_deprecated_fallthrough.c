/* [[deprecated]] must warn at each use; [[fallthrough]] must be accepted as
   a statement and not change control flow.
   See C23-TASKS.md T3.3. */
#include <stdio.h>

[[deprecated("use new_fn")]] int old_fn(void) { return 1; }
int new_fn(void) { return 2; }

[[deprecated]] int old_var = 7;

int f(int x)
{
    int r = 0;
    switch (x) {
    case 1:
        r = 1;
        [[fallthrough]];
    case 2:
        r += 2;
        break;
    default:
        r = 9;
    }
    return r;
}

int main(void)
{
    printf("%d %d %d %d\n", old_fn(), new_fn(), old_var, f(1));
    return 0;
}
