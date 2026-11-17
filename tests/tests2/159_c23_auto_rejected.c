/* C23 makes 'auto' a type specifier that takes its type from the
   initializer.  Inferring it is not implemented yet, so the form must be
   rejected instead of quietly declaring an int (which is what happened
   before, giving sizeof 4 for an auto double).
   See C23-TASKS.md T0.6 and FLAGS in the Makefile. */
int main(void)
{
    auto x = 1.5;
    return (int)sizeof x;
}
