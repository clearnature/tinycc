/* Options that turn on a whole instrumentation pass must not be accepted in
   silence: tcc emits no sanitizer, no LTO and no OpenMP runtime, so a build
   that passes -fsanitize=address would otherwise look checked while nothing
   was checked.  Those warn unconditionally; an unknown -f flag that merely
   does not apply (or does not matter) stays quiet unless -Wunsupported is
   given, because build systems pass plenty of them.  See C23-TASKS.md T8.5.

   The Makefile passes -fsanitize=address to this file, and the .expect is
   hand written rather than generated: gcc implements the option, so it would
   not warn, and the point here is precisely that tcc does. */
#include <stdio.h>

int main(void)
{
    puts("built");
    return 0;
}
