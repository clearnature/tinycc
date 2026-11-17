/* C23 #embed: the file's bytes replace the directive as a comma separated
   list of integer constants.  The data file is next to this test.
   See C23-TASKS.md T1.5. */
#include <stdio.h>

static const unsigned char blob[] = {
#embed "164_c23_embed.bin"
};

int main(void)
{
    int i;
    for (i = 0; i < (int)sizeof blob; i++)
        printf("%02X", blob[i]);
    printf(" size=%d\n", (int)sizeof blob);
    return 0;
}
