# --- solution ---
def hammingWeight(n: int) -> int:          # LC 191 Number of 1 Bits
    count = 0
    while n:
        n &= n - 1                          # 가장 낮은 1 비트 하나 지우기
        count += 1
    return count


def reverseBits(n: int) -> int:            # LC 190 Reverse Bits (32비트)
    out = 0
    for _ in range(32):
        out = (out << 1) | (n & 1)
        n >>= 1
    return out
# --- end ---


def test_lc191_190():
    assert hammingWeight(0b1011) == 3 and hammingWeight(0) == 0 and hammingWeight(0xFFFFFFFF) == 32
    assert reverseBits(0b00000010100101000001111010011100) == 964176192
    assert reverseBits(1) == 0x80000000
