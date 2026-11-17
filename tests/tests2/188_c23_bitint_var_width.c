/* C23 _BitInt(N): a variable width is not an integer constant expression.
   Same message category as the floating case in 187, from the other path
   (the expression parses fine but is not constant).
   See C23-TASKS.md T6.9. */
static int n = 8;

int a = sizeof(_BitInt(n));
