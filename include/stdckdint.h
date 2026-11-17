/* ISO C23: 7.20 checked integer arithmetic.
   tcc ships this header because the C library often does not: glibc 2.43
   has stdbit.h but no stdckdint.h, so a C23 program that includes it fails
   to build with tcc alone.  The three macros are thin wrappers over the
   overflow builtins, which tcc implements in libtcc1 (see lib/builtin.c).

   The result is stored through the first argument, which must be a pointer
   to an integer type; a null pointer constant is not accepted here, which
   matches the header gcc ships. */

#ifndef _STDCKDINT_H
#define _STDCKDINT_H

#define __STDC_VERSION_STDCKDINT_H__ 202311L

#define ckd_add(r, a, b) ((_Bool)__builtin_add_overflow(a, b, r))
#define ckd_sub(r, a, b) ((_Bool)__builtin_sub_overflow(a, b, r))
#define ckd_mul(r, a, b) ((_Bool)__builtin_mul_overflow(a, b, r))

/* Let a C library extend the header when it has its own copy. */
#if !defined(_LIBC_STDCKDINT_H) && __has_include_next(<stdckdint.h>)
# include_next <stdckdint.h>
#endif

#endif /* stdckdint.h */
