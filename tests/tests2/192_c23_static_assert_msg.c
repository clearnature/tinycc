/* C23 static_assert message reporting: gcc repeats the message in its
   original spelling, in double quotes, with '"' and '\' escaped and other
   non-printable characters as three digit octal escapes.  tcc now prints the
   same, which also makes the no-message and empty-message forms match:
   "static assertion failed" and 'static assertion failed: ""'.
   See C23-TASKS.md T8.2.  tcc reports one error per run, so the forms live
   in separate files; this one covers the escaped spelling. */
static_assert(0, "quote:\" tab:\t end");
