/* C23 __has_c_attribute: the value is the year and month in which the
   standard attribute was adopted, and 0 for a name this implementation does
   not know.  Both the plain and the __name__ spelling are accepted, it works
   in #if and (as an extension gcc also has) in ordinary expressions, and
   defined(__has_c_attribute) is true.

   A scoped name such as gnu::packed answers 0: tcc has no attribute
   namespace, so claiming support would be wrong.  gcc answers 1 there,
   which is why that case is not asserted here (see C23-SUPPORT.md §6.2). */
#include <stdio.h>

#if !defined(__has_c_attribute)
#error __has_c_attribute must be defined
#endif

#if __has_c_attribute(deprecated) != 202311
#error deprecated should be 202311
#endif

#if __has_c_attribute(__nodiscard__) != 202311
#error the __name__ spelling should work too
#endif

#if __has_c_attribute(no_such_attribute)
#error an unknown attribute must be 0
#endif

static int in_expr(void)
{
    return __has_c_attribute(fallthrough) == 202311;
}

int main(void)
{
    printf("%d %d %d\n", __has_c_attribute(deprecated),
           __has_c_attribute(noreturn), __has_c_attribute(maybe_unused));
    printf("%d %d\n", __has_c_attribute(unsequenced),
           __has_c_attribute(reproducible));
    printf("%d %d\n", __has_c_attribute(no_such_attribute),
           __has_c_attribute(deprecated) == 202311);
    printf("%d\n", in_expr());
    return 0;
}
