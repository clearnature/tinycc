/* GNU builtin subset: __builtin_object_size and __builtin_assume_aligned.
   A whole array named directly is answered exactly; an opaque pointer gets
   gcc's "unknown object" answer.  Neither builtin may evaluate the pointer
   argument of object_size, and assume_aligned has to return it unchanged. */
#include <stdio.h>

static char garr[7];
static int calls;

static char *counted(void)
{
    calls++;
    return garr;
}

int main(void)
{
    char buf[10];
    struct S { char pad; int arr[3]; } s;
    char *p = buf;

    printf("%d %d\n", (int)sizeof(__builtin_object_size(buf, 0)),
           (int)sizeof(__builtin_assume_aligned(buf, 8)));
    printf("%llu %llu %llu %llu\n",
           (unsigned long long)__builtin_object_size(buf, 0),
           (unsigned long long)__builtin_object_size(buf, 1),
           (unsigned long long)__builtin_object_size(buf, 2),
           (unsigned long long)__builtin_object_size(buf, 3));
    printf("%llu\n", (unsigned long long)__builtin_object_size(garr, 0));
    /* an opaque pointer is unknown: -1 for the maximum variants, 0 for the
       minimum ones */
    printf("%llu %llu\n", (unsigned long long)__builtin_object_size(p, 0),
           (unsigned long long)__builtin_object_size(p, 2));

    /* the pointer argument of object_size is a compile-time query: it must
       not be evaluated */
    calls = 0;
    (void)__builtin_object_size(counted(), 0);
    printf("object_size calls %d\n", calls);

    /* assume_aligned returns its pointer and does evaluate the arguments */
    printf("same %d\n", __builtin_assume_aligned(buf + 1, 4) == buf + 1);
    printf("same3 %d\n", __builtin_assume_aligned(buf, 8, 0) == buf);
    calls = 0;
    if (__builtin_assume_aligned(counted(), 4) == garr && calls == 1)
        printf("assume_aligned once\n");

    /* a constant result can size an array, as with gcc */
    {
        char sized[__builtin_object_size(buf, 0)];
        printf("%d\n", (int)sizeof sized);
    }
    printf("%d\n", (int)sizeof s);
    return 0;
}
