/* expect (with bad_dup_b.c): link — same global defined in two translation units */
int g_boot_count = 0;
int main(void) { return g_boot_count; }
