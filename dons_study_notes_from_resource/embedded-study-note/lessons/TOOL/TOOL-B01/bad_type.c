/* expect: compile — a struct is not an integer (semantic/type error) */
struct status { unsigned busy : 1; unsigned err : 1; };
int main(void)
{
    struct status st = { 0, 0 };
    int code = st;
    return code;
}
