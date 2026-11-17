/* GNU builtin subset: __builtin_bswap16/32/64 and __builtin_prefetch.
   The result type (hence sizeof) and the values must match gcc, the
   argument must be evaluated exactly once, and prefetch yields void
   while still evaluating every argument. */
#include <stdio.h>

static int calls;
static int dummy;

static unsigned long long counted(unsigned long long v)
{
    calls++;
    return v;
}

static void *pcount(void)
{
    calls++;
    return &dummy;
}

int main(void)
{
    unsigned short s = 0x3412;
    unsigned int i = 0x78563412;
    unsigned long long l = 0xefcdab8967452301ULL;

    printf("%d %d %d\n", (int)sizeof(__builtin_bswap16(s)),
           (int)sizeof(__builtin_bswap32(i)),
           (int)sizeof(__builtin_bswap64(l)));
    printf("%u %u %llu\n", (unsigned)__builtin_bswap16(s),
           (unsigned)__builtin_bswap32(i),
           (unsigned long long)__builtin_bswap64(l));
    printf("%u %u %llu\n", (unsigned)__builtin_bswap16(0xffff),
           (unsigned)__builtin_bswap32(-1),
           (unsigned long long)__builtin_bswap64(0));

    calls = 0;
    if (__builtin_bswap32(counted(0x11223344u)) == 0x44332211u && calls == 1)
        printf("bswap once\n");

    calls = 0;
    __builtin_prefetch(pcount(), 1, 2);
    __builtin_prefetch(&dummy);
    printf("prefetch once %d\n", calls);
    printf("prefetch void %d\n", (__builtin_prefetch(&dummy), 7));
    return 0;
}
