/* uses alias to allow building with gcc/clang */
#ifdef __TINYC__
#define	BUILTIN(x)	__builtin_##x
#define	BUILTINN(x)	"__builtin_" # x
#else
#define	BUILTIN(x)	__tcc_builtin_##x
#define	BUILTINN(x)	"__tcc_builtin_" # x
#endif

/* ---------------------------------------------- */
/* This file implements:
 * __builtin_ffs
 * __builtin_clz
 * __builtin_ctz
 * __builtin_clrsb
 * __builtin_popcount
 * __builtin_parity
 * for int, long and long long
 */

static const unsigned char table_1_32[] = {
     0,  1, 28,  2, 29, 14, 24,  3, 30, 22, 20, 15, 25, 17,  4,  8, 
    31, 27, 13, 23, 21, 19, 16,  7, 26, 12, 18,  6, 11,  5, 10,  9
};
static const unsigned char table_2_32[32] = {
    31, 22, 30, 21, 18, 10, 29,  2, 20, 17, 15, 13,  9,  6, 28,  1,
    23, 19, 11,  3, 16, 14,  7, 24, 12,  4,  8, 25,  5, 26, 27,  0
};
static const unsigned char table_1_64[] = {
     0,  1,  2, 53,  3,  7, 54, 27,  4, 38, 41,  8, 34, 55, 48, 28,
    62,  5, 39, 46, 44, 42, 22,  9, 24, 35, 59, 56, 49, 18, 29, 11,
    63, 52,  6, 26, 37, 40, 33, 47, 61, 45, 43, 21, 23, 58, 17, 10,
    51, 25, 36, 32, 60, 20, 57, 16, 50, 31, 19, 15, 30, 14, 13, 12
};
static const unsigned char table_2_64[] = {
    63, 16, 62,  7, 15, 36, 61,  3,  6, 14, 22, 26, 35, 47, 60,  2,
     9,  5, 28, 11, 13, 21, 42, 19, 25, 31, 34, 40, 46, 52, 59,  1,
    17,  8, 37,  4, 23, 27, 48, 10, 29, 12, 43, 20, 32, 41, 53, 18,
    38, 24, 49, 30, 44, 33, 54, 39, 50, 45, 55, 51, 56, 57, 58,  0
};

#define FFSI(x) \
    return table_1_32[((x & -x) * 0x077cb531u) >> 27] + (x != 0);
#define FFSL(x) \
    return table_1_64[((x & -x) * 0x022fdd63cc95386dull) >> 58] + (x != 0);
#define CTZI(x) \
    return table_1_32[((x & -x) * 0x077cb531u) >> 27];
#define CTZL(x) \
    return table_1_64[((x & -x) * 0x022fdd63cc95386dull) >> 58];
#define CLZI(x)   \
    x |= x >> 1;  \
    x |= x >> 2;  \
    x |= x >> 4;  \
    x |= x >> 8;  \
    x |= x >> 16; \
    return table_2_32[(x * 0x07c4acddu) >> 27];
#define CLZL(x)   \
    x |= x >> 1;  \
    x |= x >> 2;  \
    x |= x >> 4;  \
    x |= x >> 8;  \
    x |= x >> 16; \
    x |= x >> 32; \
    return table_2_64[x * 0x03f79d71b4cb0a89ull >> 58];
#define POPCOUNTI(x, m)                                                   \
    x = x - ((x >> 1) & 0x55555555);                                      \
    x = (x & 0x33333333) + ((x >> 2) & 0x33333333);                       \
    x = (x + (x >> 4)) & 0xf0f0f0f;                                       \
    return ((x * 0x01010101) >> 24) & m; 
#define POPCOUNTL(x, m)                                                   \
    x = x - ((x >> 1) & 0x5555555555555555ull);                           \
    x = (x & 0x3333333333333333ull) + ((x >> 2) & 0x3333333333333333ull); \
    x = (x + (x >> 4)) & 0xf0f0f0f0f0f0f0full;                            \
    return ((x * 0x0101010101010101ull) >> 56) & m;

/* Returns one plus the index of the least significant 1-bit of x,
   or if x is zero, returns zero. */
int BUILTIN(ffs) (int x) { FFSI(x) }
int BUILTIN(ffsll) (long long x) { FFSL(x) }
#if __SIZEOF_LONG__ == 4
int BUILTIN(ffsl) (long x) __attribute__((alias(BUILTINN(ffs))));
#else
int BUILTIN(ffsl) (long x) __attribute__((alias(BUILTINN(ffsll))));
#endif

/* Returns the number of leading 0-bits in x, starting at the most significant
   bit position. If x is 0, the result is undefined.  */
int BUILTIN(clz) (unsigned int x) { CLZI(x) }
int BUILTIN(clzll) (unsigned long long x) { CLZL(x) }
#if __SIZEOF_LONG__ == 4
int BUILTIN(clzl) (unsigned long x) __attribute__((alias(BUILTINN(clz))));
#else
int BUILTIN(clzl) (unsigned long x) __attribute__((alias(BUILTINN(clzll))));
#endif

/* Returns the number of trailing 0-bits in x, starting at the least
   significant bit position. If x is 0, the result is undefined. */
int BUILTIN(ctz) (unsigned int x) { CTZI(x) }
int BUILTIN(ctzll) (unsigned long long x) { CTZL(x) }
#if __SIZEOF_LONG__ == 4
int BUILTIN(ctzl) (unsigned long x) __attribute__((alias(BUILTINN(ctz))));
#else
int BUILTIN(ctzl) (unsigned long x) __attribute__((alias(BUILTINN(ctzll))));
#endif

/* Returns the number of leading redundant sign bits in x, i.e. the number
   of bits following the most significant bit that are identical to it.
   There are no special cases for 0 or other values. */
int BUILTIN(clrsb) (int x) { if (x < 0) x = ~x; x <<= 1; CLZI(x) }
int BUILTIN(clrsbll) (long long x) { if (x < 0) x = ~x; x <<= 1; CLZL(x) }
#if __SIZEOF_LONG__ == 4
int BUILTIN(clrsbl) (long x) __attribute__((alias(BUILTINN(clrsb))));
#else
int BUILTIN(clrsbl) (long x) __attribute__((alias(BUILTINN(clrsbll))));
#endif

/* Returns the number of 1-bits in x.*/
int BUILTIN(popcount) (unsigned int x) { POPCOUNTI(x, 0x3f) }
int BUILTIN(popcountll) (unsigned long long x) { POPCOUNTL(x, 0x7f) }
#if __SIZEOF_LONG__ == 4
int BUILTIN(popcountl) (unsigned long x) __attribute__((alias(BUILTINN(popcount))));
#else
int BUILTIN(popcountl ) (unsigned long x) __attribute__((alias(BUILTINN(popcountll))));
#endif

/* Returns the parity of x, i.e. the number of 1-bits in x modulo 2. */
int BUILTIN(parity) (unsigned int x) { POPCOUNTI(x, 0x01) }
int BUILTIN(parityll) (unsigned long long x) { POPCOUNTL(x, 0x01) }
#if __SIZEOF_LONG__ == 4
int BUILTIN(parityl) (unsigned long x) __attribute__((alias(BUILTINN(parity))));
#else
int BUILTIN(parityl) (unsigned long x) __attribute__((alias(BUILTINN(parityll))));
#endif

#ifndef __TINYC__
#if defined(__GNUC__) && (__GNUC__ >= 6)
/* gcc overrides alias from __builtin_ffs... to ffs.. so use assembly code */
__asm__(".globl  __builtin_ffs");
__asm__(".set __builtin_ffs,__tcc_builtin_ffs");
__asm__(".globl  __builtin_ffsl");
__asm__(".set __builtin_ffsl,__tcc_builtin_ffsl");
__asm__(".globl  __builtin_ffsll");
__asm__(".set __builtin_ffsll,__tcc_builtin_ffsll");
#else
int __builtin_ffs(int x) __attribute__((alias("__tcc_builtin_ffs")));
int __builtin_ffsl(long x) __attribute__((alias("__tcc_builtin_ffsl")));
int __builtin_ffsll(long long x) __attribute__((alias("__tcc_builtin_ffsll")));
#endif
int __builtin_clz(unsigned int x) __attribute__((alias("__tcc_builtin_clz")));
int __builtin_clzl(unsigned long x) __attribute__((alias("__tcc_builtin_clzl")));
int __builtin_clzll(unsigned long long x) __attribute__((alias("__tcc_builtin_clzll")));
int __builtin_ctz(unsigned int x) __attribute__((alias("__tcc_builtin_ctz")));
int __builtin_ctzl(unsigned long x) __attribute__((alias("__tcc_builtin_ctzl")));
int __builtin_ctzll(unsigned long long x) __attribute__((alias("__tcc_builtin_ctzll")));
int __builtin_clrsb(int x) __attribute__((alias("__tcc_builtin_clrsb")));
int __builtin_clrsbl(long x) __attribute__((alias("__tcc_builtin_clrsbl")));
int __builtin_clrsbll(long long x) __attribute__((alias("__tcc_builtin_clrsbll")));
int __builtin_popcount(unsigned int x) __attribute__((alias("__tcc_builtin_popcount")));
int __builtin_popcountl(unsigned long x) __attribute__((alias("__tcc_builtin_popcountl")));
int __builtin_popcountll(unsigned long long x) __attribute__((alias("__tcc_builtin_popcountll")));
int __builtin_parity(unsigned int x) __attribute__((alias("__tcc_builtin_parity")));
int __builtin_parityl(unsigned long x) __attribute__((alias("__tcc_builtin_parityl")));
int __builtin_parityll(unsigned long long x) __attribute__((alias("__tcc_builtin_parityll")));
#endif

/* ---------------------------------------------- */
/* This file also implements the __builtin_{add,sub,mul}_overflow family,
 * which tcc's front end turns into a call here: the operation has to be
 * carried out in infinite precision and then compared against the range of
 * the destination type, which needs 65-bit (add/sub) and 128-bit (mul)
 * intermediates that the compiler has no type for.
 *
 * The front end passes the operands as raw 64-bit patterns, sign- or
 * zero-extended according to their own type, and packs the types into
 * 'meta':
 *      bit 0      a is signed
 *      bit 1      b is signed
 *      bits 8-14  width of the result in bits
 *      bit 15     the result is signed
 * The operand widths are not needed: once extended, the pattern is the
 * exact value.  'res' is NULL for the _p forms, which only report.
 */

static int ovf_signed(int meta, int shift)
{
    return (meta >> shift) & 1;
}

/* largest value of an integer type of w bits */
static unsigned long long ovf_max(unsigned long long w, int s)
{
    if (w == 64)
        return s ? 0x7fffffffffffffffULL : ~0ULL;
    return s ? ((1ULL << (w - 1)) - 1) : ((1ULL << w) - 1);
}

/* 2^64 - 2^(w-1): the smallest value that still fits a signed type when it
   is expressed as u - 2^64 */
static unsigned long long ovf_neg_thresh(int w)
{
    return ~((1ULL << (w - 1)) - 1);
}

static void ovf_store(void *res, unsigned long long u, int w, int s)
{
    if (!res)
        return;
    switch (w) {
    case 8:  if (s) *(signed char *)res = (signed char)u;
             else   *(unsigned char *)res = (unsigned char)u; break;
    case 16: if (s) *(short *)res = (short)u;
             else   *(unsigned short *)res = (unsigned short)u; break;
    case 32: if (s) *(int *)res = (int)u;
             else   *(unsigned int *)res = (unsigned int)u; break;
    default: if (s) *(long long *)res = (long long)u;
             else   *(unsigned long long *)res = (unsigned long long)u; break;
    }
}

/* Does the exact value 'u + hi * 2^64' (hi is -2..1) fit the type w/s?
   The low 64 bits are in u, the part above them in hi, so this is exact:
   two operands below 2^64 need at most 65 bits for their sum. */
static int ovf_fits(unsigned long long u, int hi, int w, int s)
{
    if (hi > 0)
        return 0;                       /* >= 2^64, too large for any type */
    if (hi < -1)
        return 0;                       /* <= -2^64-1, too small as well */
    if (hi == 0)
        return u <= ovf_max(w, s);
    /* hi == -1: the value is u - 2^64, so it is negative */
    return s && u >= ovf_neg_thresh(w);
}

static int ovf_apply(int is_sub, unsigned long long a, unsigned long long b,
                     void *res, int meta)
{
    int sa = ovf_signed(meta, 0), sb = ovf_signed(meta, 1);
    int wt = (meta >> 8) & 0x7f, st = ovf_signed(meta, 15);
    /* the operands arrive sign- or zero-extended, so bit 63 already holds
       their sign whenever their type is signed */
    int na = sa && (a >> 63) != 0;
    int nb = sb && (b >> 63) != 0;
    unsigned long long u;
    int hi, fits;

    if (is_sub) {
        /* (A - B) - (na - nb)*2^64 = u - borrow*2^64 - (na - nb)*2^64 */
        u = a - b;
        hi = nb - na - (a < b);
    } else {
        /* (A + B) - (na + nb)*2^64 = u + carry*2^64 - (na + nb)*2^64 */
        u = a + b;
        hi = (u < a) - na - nb;
    }
    fits = ovf_fits(u, hi, wt, st);
    ovf_store(res, u, wt, st);
    return !fits;
}

/* 64x64 -> 128 bit product */
static void ovf_mul128(unsigned long long x, unsigned long long y,
                       unsigned long long *phi, unsigned long long *plo)
{
    unsigned long long x0 = x & 0xffffffffULL, x1 = x >> 32;
    unsigned long long y0 = y & 0xffffffffULL, y1 = y >> 32;
    unsigned long long p0 = x0 * y0;
    unsigned long long p1 = x0 * y1;
    unsigned long long p2 = x1 * y0;
    unsigned long long p3 = x1 * y1;
    unsigned long long mid = (p0 >> 32) + (p1 & 0xffffffffULL) + (p2 & 0xffffffffULL);

    *plo = (p0 & 0xffffffffULL) | (mid << 32);
    *phi = p3 + (p1 >> 32) + (p2 >> 32) + (mid >> 32);
}

static int ovf_mul(unsigned long long a, unsigned long long b,
                   void *res, int meta)
{
    int sa = ovf_signed(meta, 0), sb = ovf_signed(meta, 1);
    int wt = (meta >> 8) & 0x7f, st = ovf_signed(meta, 15);
    int na = sa && (a >> 63) != 0;
    int nb = sb && (b >> 63) != 0;
    unsigned long long ma = na ? -a : a;   /* magnitudes are below 2^64 */
    unsigned long long mb = nb ? -b : b;
    unsigned long long hi, lo;
    int neg, fits;

    ovf_mul128(ma, mb, &hi, &lo);
    neg = (na ^ nb) && (hi | lo) != 0;
    if (!neg) {
        fits = hi == 0 && lo <= ovf_max(wt, st);
    } else {
        /* -|p| fits a signed type when |p| <= 2^(w-1) (that is INT_MIN) */
        fits = st && hi == 0
               && lo <= (wt == 64 ? 0x8000000000000000ULL
                                  : (1ULL << (wt - 1)));
        /* store the two's complement form */
        lo = ~lo + 1;
    }
    ovf_store(res, lo, wt, st);
    return !fits;
}

int __tcc_add_overflow(unsigned long long a, unsigned long long b,
                       void *res, int meta)
{
    return ovf_apply(0, a, b, res, meta);
}

int __tcc_sub_overflow(unsigned long long a, unsigned long long b,
                       void *res, int meta)
{
    return ovf_apply(1, a, b, res, meta);
}

int __tcc_mul_overflow(unsigned long long a, unsigned long long b,
                       void *res, int meta)
{
    return ovf_mul(a, b, res, meta);
}
