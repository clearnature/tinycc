/* C23 [[fallthrough]] placement: it is only valid inside a switch and must
   be followed by a case or default label.  The warning and the error below
   match gcc's wording; the correct use stays silent.
   See C23-TASKS.md T3.3. */
#include <stdio.h>

int ok(int x)
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

int warns(int x)
{
    switch (x) {
    case 1:
        [[fallthrough]];   /* not preceding a case label */
    }
    return 0;
}

int main(void)
{
    printf("%d %d\n", ok(1), warns(1));
    return 0;
}
