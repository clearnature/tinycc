/* C23 _BitInt(N): the width has to be an integer constant expression.  A
   floating argument is not one, and gcc's wording for that case differs from
   the "is not a positive integer constant expression" it uses for a value
   that is constant but out of range, so tcc makes the same distinction.
   See C23-TASKS.md T6.9.  tcc reports one error per run, so the
   non-constant and out-of-range cases live in separate files. */
int a = sizeof(_BitInt(2.5));
