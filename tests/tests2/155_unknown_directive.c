/* An unknown preprocessing directive is an error, not a warning: it used
   to be dropped silently, which is how #embed produced empty arrays.
   See C23-TASKS.md T0.3. */
#foobar
int main(void)
{
    return 0;
}
