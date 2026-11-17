/* C23 _BitInt(N) limits (T4.5).  This implementation supports N <= 64 by
   mapping N to the smallest standard integer type that holds it, so wider
   types are rejected explicitly rather than silently truncated.  gcc does
   support wider types; the message states our limit.
   The other two checks are the standard ones: N must be a positive integer
   constant expression. */
int a = sizeof(_BitInt(65));

int b = sizeof(_BitInt(128));

int c = sizeof(unsigned _BitInt(1000));

int d = sizeof(_BitInt(0));
