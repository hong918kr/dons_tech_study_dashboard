/* expect: preprocess — function-like macro called with the wrong number of arguments */
#define REG_FIELD(reg, shift, width)  (((reg) >> (shift)) & ((1u << (width)) - 1u))
int main(void)
{
    unsigned sr = 0x30u;
    return (int)REG_FIELD(sr, 4);
}
