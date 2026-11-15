/* libfoo.a member 1: foo_entry() calls INTO libbar.a */
int bar_mid(int x);

int foo_entry(int x)
{
	return bar_mid(x) + 1;
}
