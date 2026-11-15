/* libbar.a member 1: bar_mid() calls BACK into libfoo.a (foo_leaf) */
int foo_leaf(int x);

int bar_mid(int x)
{
	return foo_leaf(x) * 10;
}
