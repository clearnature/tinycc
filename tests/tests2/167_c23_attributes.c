/* C23 attribute specifiers.  tcc has analysis behind noreturn only; the
   others are accepted so that real headers and [[nodiscard]] style code
   compile.  The subscript cases matter: "a[b[0]]" must still lex as two
   subscripts, which is why [[ is only recognised in attribute position.
   See C23-TASKS.md T2.5. */
#include <stdio.h>

[[nodiscard]] int f(void) { return 1; }
[[deprecated("use f")]] void old(void);
[[noreturn]] void die(void);

struct S {
    [[maybe_unused]] int a;
};

int arr[4] = {10, 20, 30, 40};
int i0 = 1;

int main(void)
{
    int b[2] = {1, 2};
    struct S s = {0};
    [[maybe_unused]] int unused_var = 3;
    printf("%d %d %d %d\n", f(), arr[b[0]], b[i0], s.a);
    return 0;
}
