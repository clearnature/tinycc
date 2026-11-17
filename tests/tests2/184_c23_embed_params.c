/* C23 #embed parameters and __has_embed.

   limit(N) truncates the resource, prefix(...) and suffix(...) are emitted
   verbatim around the bytes (so a missing comma is the programmer's problem,
   exactly as with gcc), and prefix/suffix are not emitted at all when the
   resource contributes no bytes.  __has_embed reports FOUND / EMPTY /
   NOT_FOUND, and limit's argument is a preprocessing constant expression.

   The resource is 65 66 67 68 0 255 128, shared with 164_c23_embed.c.
   See C23-TASKS.md T6.2 and T6.3. */
#include <stdio.h>

#if !defined(__STDC_EMBED_FOUND__) || !defined(__STDC_EMBED_EMPTY__) \
    || !defined(__STDC_EMBED_NOT_FOUND__)
#error the __STDC_EMBED_* constants must be defined
#endif

#if __has_embed("164_c23_embed.bin") != __STDC_EMBED_FOUND__
#error the resource exists, so it must be FOUND
#endif

#if __has_embed("no_such_file.bin") != __STDC_EMBED_NOT_FOUND__
#error a missing resource must be NOT_FOUND
#endif

#if __has_embed("164_c23_embed.bin" limit(0)) != __STDC_EMBED_EMPTY__
#error limit(0) leaves nothing, so it must be EMPTY
#endif

#if __has_embed("164_c23_embed.bin" limit(3)) != __STDC_EMBED_FOUND__
#error a truncated but non-empty resource is still FOUND
#endif

#if __has_embed("164_c23_embed.bin" limit(1 + 1)) != __STDC_EMBED_FOUND__
#error limit takes a constant expression
#endif

static const unsigned char all[] = {
#embed "164_c23_embed.bin"
};
static const unsigned char two[] = {
#embed "164_c23_embed.bin" limit(2)
};
static const unsigned char framed[] = {
#embed "164_c23_embed.bin" limit(3) prefix(0xAA,) suffix(,0xBB)
};
static const unsigned char many[] = {
#embed "164_c23_embed.bin" limit(100)
};

int main(void)
{
    printf("%d\n", (int)sizeof all);
    printf("%d %d %d\n", (int)two[0], (int)two[1], (int)sizeof two);
    printf("%d %d %d %d %d\n", (int)framed[0], (int)framed[1], (int)framed[2],
           (int)framed[4], (int)sizeof framed);
    printf("%d\n", (int)sizeof many);
    printf("%d %d %d\n", __STDC_EMBED_NOT_FOUND__, __STDC_EMBED_FOUND__,
           __STDC_EMBED_EMPTY__);
    return 0;
}
