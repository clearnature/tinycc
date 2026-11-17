/* C23 constexpr objects.  A scalar value must be usable in constant
   expressions such as array bounds and case labels, which is why it is
   represented the same way an enum constant is.

   An aggregate is different: gcc makes it a read-only object that still
   requires an initializer, but does not treat it as a constant expression
   (static_assert(tab[1] == 2) is rejected there), so it is an ordinary
   object with const.
   See C23-TASKS.md T3.2 and T6.5. */
#include <stdio.h>

constexpr int N = 5;
constexpr int M = N * 2;
constexpr unsigned char UC = 200;

struct Pair { int a; char b; };

constexpr int tab[3] = { 1, 2, 3 };
constexpr struct Pair pair = { 7, 'x' };
static const int *tabp = tab;

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
    printf("%d %d %d %d\n", tab[0], tab[1], tab[2], (int)sizeof tab);
    printf("%d %d %d %d\n", pair.a, (int)pair.b, (int)sizeof pair, *tabp);
    return 0;
}
