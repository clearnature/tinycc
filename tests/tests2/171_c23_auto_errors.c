/* C23 auto: no initializer, more than one declarator, and an elaborated
   declarator are all rejected.  A file-scope auto is rejected too, which
   is stricter than gcc: C23 forbids it, gcc accepts it as a global.
   See C23-TASKS.md T3.1. */
int main(void)
{
    auto x;
    return 0;
}
