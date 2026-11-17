/* A directive hidden in a skipped block must still be diagnosed.  The
   skip scanner used to look only for #else/#elif/#endif and drop
   everything else, so #elifdef produced no diagnostic at all and neither
   branch got compiled.  See C23-TASKS.md T0.4. */
#define B 1

#ifdef A
int aaa;
#elifdef B
int bbb;
#endif

int main(void)
{
    return 0;
}
