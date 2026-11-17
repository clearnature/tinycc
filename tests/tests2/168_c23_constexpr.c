/* C23 constexpr objects.  The value must be usable in constant expressions
   such as array bounds and case labels, which is why it is represented the
   same way an enum constant is.
   See C23-TASKS.md T3.2. */
#include <stdio.h>

constexpr int N = 5;
constexpr int M = N * 2;
constexpr unsigned char UC = 200;

int arr[N];

static void check(int v)
{
    switch (v) {
    case N: puts("N"); break;
    case M: puts("M"); break;
    default: puts("other"); break;
    }
}

int main(void)
{
    check(N);
    check(M);
    check(0);
    printf("%d %d %d %d\n", N, M, (int)UC, (int)(sizeof arr / sizeof arr[0]));
    return 0;
}
