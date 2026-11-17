/* Predefined limit macros that gcc and clang also define.
   Headers shipped with those compilers use them (gcc's own omp.h needs
   __UINTPTR_MAX__), and #if requires them to be integer constant
   expressions, so they are written as literals per ABI rather than being
   computed from the types.

   The assertions are self-checking: each macro is compared with the limit of
   the type it describes, so the test is valid on any target rather than
   pinning 64-bit values.  See C23-TASKS.md T7.2. */
#include <stdio.h>
#include <limits.h>
#include <stddef.h>
#include <stdint.h>

/* they must work in #if, which a cast would not allow */
#if __INT_MAX__ != 0x7fffffff
#error __INT_MAX__ wrong
#endif
#if __UINTPTR_MAX__ < __INTPTR_MAX__
#error __UINTPTR_MAX__ must be at least __INTPTR_MAX__
#endif

int main(void)
{
    int ok = 1;

    ok &= __SIZE_MAX__ == (size_t)-1;
    ok &= __PTRDIFF_MAX__ == PTRDIFF_MAX;
    ok &= __INTPTR_MAX__ == INTPTR_MAX;
    ok &= __UINTPTR_MAX__ == UINTPTR_MAX;
    ok &= __INTMAX_MAX__ == INTMAX_MAX;
    ok &= __UINTMAX_MAX__ == UINTMAX_MAX;
    ok &= __INT8_MAX__ == INT8_MAX;
    ok &= __INT16_MAX__ == INT16_MAX;
    ok &= __INT32_MAX__ == INT32_MAX;
    ok &= __INT64_MAX__ == INT64_MAX;
    ok &= __UINT8_MAX__ == UINT8_MAX;
    ok &= __UINT32_MAX__ == UINT32_MAX;
    ok &= __UINT64_MAX__ == UINT64_MAX;
    ok &= __SCHAR_MAX__ == SCHAR_MAX;
    ok &= __SHRT_MAX__ == SHRT_MAX;
    ok &= __INT_MAX__ == INT_MAX;
    ok &= __LONG_MAX__ == LONG_MAX;
    ok &= __LONG_LONG_MAX__ == LLONG_MAX;
    ok &= __CHAR_BIT__ == CHAR_BIT;

    /* Only the verdict is printed: each macro was compared against the limit
       of its own type above, so a "1" already means the values follow this
       target's ABI, and the output stays the same on 32- and 64-bit targets. */
    printf("%d\n", ok);
    return !ok;
}
