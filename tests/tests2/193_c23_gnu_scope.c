/* C23 scoped attributes: [[gnu::packed]] and friends.  These used to be
   rejected as an unknown attribute, which was both a conformance gap and a
   hazard: once an unknown attribute only warns (as it does in gcc), silently
   dropping gnu::packed would change a struct's layout without a word.  The
   scope is now mapped onto the same parser that handles
   __attribute__((packed)), so both spellings produce the same object.  A name
   tcc does not implement is ignored with gcc's warning; tests/c23-features.py
   checks that wording, since this file is compared against a -w build.
   See C23-TASKS.md T8.3 and T8.4. */
#include <stdio.h>

struct [[gnu::packed]] packed_s {
    char c;
    int i;
};

struct plain_s {
    char c;
    int i;
};

struct [[gnu::aligned(16)]] aligned_s {
    char c;
};

int main(void)
{
    printf("%d %d %d %d\n", (int)sizeof(struct packed_s),
           (int)sizeof(struct plain_s), (int)_Alignof(struct aligned_s),
           __has_c_attribute(gnu::packed));
    return 0;
}
