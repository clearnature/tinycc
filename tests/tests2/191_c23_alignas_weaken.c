/* C23 6.7.5: an alignas specifier may only increase the alignment of the
   object it is applied to.  gcc diagnoses the weakening case with
   "_Alignas specifiers cannot reduce alignment of 'x'", and tcc now says the
   same.  The __attribute__((aligned(1))) spelling stays accepted, as it is in
   gcc (tcc's own tcctest.c relies on it), which is why the specifier and the
   attribute have to be distinguished.
   See C23-TASKS.md T8.1. */
alignas(1) int x;
