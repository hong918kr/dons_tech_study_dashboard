/* libfoo.a member 2: foo_leaf() is needed only BY libbar.a (back-reference) */
int foo_leaf(int x)
{
	return x + 2;
}
