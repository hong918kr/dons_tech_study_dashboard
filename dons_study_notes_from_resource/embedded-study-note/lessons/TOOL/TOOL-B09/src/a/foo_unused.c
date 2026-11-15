/* libfoo.a member 3: nobody references it -> must NOT be pulled in */
const char foo_unused_banner[] = "foo_unused: you should never see me in the final binary";

int foo_unused(int x)
{
	return x * 3;
}
