/* expect: link — every stage up to .o is fine; the C runtime start code needs main */
int crc_step(int acc, int byte) { return (acc << 1) ^ byte; }
