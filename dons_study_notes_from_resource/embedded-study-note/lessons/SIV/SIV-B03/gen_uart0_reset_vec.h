/* generated from UART0 regmap by siv_reset_gen - do not edit */
static const reset_vec_t UART0_reset_vec[] = {
    { "ID", 0x000u, 0x55410100u, 0xFFFFFFFFu },
    { "CTRL", 0x004u, 0x00000000u, 0xFFFFFFFFu },
    { "BAUD", 0x008u, 0x0000001Au, 0xFFFFFFFFu },
    /* skip TXDATA: no checkable bits */
    /* skip RXDATA: read side effect */
    { "FIFOCFG", 0x014u, 0x00000804u, 0xFFFFFFFFu },
    { "STATUS", 0x018u, 0x00000005u, 0xFFFFFFEFu },
    { "INT_RAW", 0x01Cu, 0x00000000u, 0xFFFFFFFFu },
    { "INT_EN", 0x020u, 0x00000000u, 0xFFFFFFFFu },
    { "ERRCNT", 0x024u, 0x00000000u, 0xFFFFFFFFu },
    /* skip TIMER: no checkable bits */
    { "DBG", 0x02Cu, 0x00000000u, 0x00FFFFFFu },
};
