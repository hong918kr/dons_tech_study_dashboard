# ⌨️ D3 · 10-05 (월) 드릴 — bytes · struct · 비트 필드 · CRC / C 엔디언 · 레지스터 · 파싱

> 매일 아침 20분 + 저녁 20분. **정답을 보지 않고** 빈 파일에 친다. 브라우저: [드릴 트레이너](../site/drills.html) (Day 3 탭) · 에디터: `python3 drills/drill.py new 3` → `python3 drills/drill.py check 3`. 틀린 항목은 다음 날 다시.

## Python 10개

| ID | 주제 | 칠 것 |
|---|---|---|
| P3-01 | bytes → hex 문자열 | to_hex(b"\x01\xab") → "01 AB" (공백 구분, 대문자). |
| P3-02 | hex 문자열 → bytes | from_hex("AA 05 01") → b"\xaa\x05\x01". bytes.fromhex 사용. |
| P3-03 | int.from_bytes / to_bytes | b[off:off+2]를 little-endian u16으로 읽는 u16le(b, off), v를 big-endian 4바이트로 pack_u32be(v). |
| P3-04 | struct.unpack | 헤더 4바이트 "<BBH" (sync, type, length)를 튜플로 parse_header(b). 4바이트 미만이면 ValueError. |
| P3-05 | struct.pack | 명령 프레임: cmd_id(u8), value(i16), seq(u32), little-endian. build_cmd(cmd_id, value, seq). |
| P3-06 | 비트 필드 읽기 | reg에서 shift 위치부터 width 비트를 꺼내는 get_field(reg, shift, width). |
| P3-07 | 비트 필드 쓰기 | reg의 [shift, shift+width) 자리를 val로 바꾼 값을 돌려주는 set_field(reg, shift, width, val). val이 넘치면 ValueError. |
| P3-08 | XOR 체크섬 | 모든 바이트를 XOR 한 값 xor_sum(data). |
| P3-09 | CRC-8 (poly 0x07) | MSB-first CRC-8/SMBUS crc8(data). check 값: crc8(b"123456789") == 0xF4. |
| P3-10 | bytearray 슬라이스 대입 | buf[off:]를 data로 덮어쓰고(길이 유지) buf를 돌려주는 patch(buf, off, data). 넘치면 ValueError. |

### P3-01 · bytes → hex 문자열

to_hex(b"\x01\xab") → "01 AB" (공백 구분, 대문자).

정답:

```python
def to_hex(b):
    return " ".join(f"{x:02X}" for x in b)
```

### P3-02 · hex 문자열 → bytes

from_hex("AA 05 01") → b"\xaa\x05\x01". bytes.fromhex 사용.

정답:

```python
def from_hex(s):
    return bytes.fromhex(s)
```

### P3-03 · int.from_bytes / to_bytes

b[off:off+2]를 little-endian u16으로 읽는 u16le(b, off), v를 big-endian 4바이트로 pack_u32be(v).

정답:

```python
def u16le(b, off):
    return int.from_bytes(b[off:off + 2], "little")


def pack_u32be(v):
    return v.to_bytes(4, "big")
```

### P3-04 · struct.unpack

헤더 4바이트 "<BBH" (sync, type, length)를 튜플로 parse_header(b). 4바이트 미만이면 ValueError.

정답:

```python
import struct

HDR = struct.Struct("<BBH")


def parse_header(b):
    if len(b) < HDR.size:
        raise ValueError("short header")
    return HDR.unpack_from(b)
```

### P3-05 · struct.pack

명령 프레임: cmd_id(u8), value(i16), seq(u32), little-endian. build_cmd(cmd_id, value, seq).

정답:

```python
import struct


def build_cmd(cmd_id, value, seq):
    return struct.pack("<BhI", cmd_id, value, seq)
```

### P3-06 · 비트 필드 읽기

reg에서 shift 위치부터 width 비트를 꺼내는 get_field(reg, shift, width).

정답:

```python
def get_field(reg, shift, width):
    return (reg >> shift) & ((1 << width) - 1)
```

### P3-07 · 비트 필드 쓰기

reg의 [shift, shift+width) 자리를 val로 바꾼 값을 돌려주는 set_field(reg, shift, width, val). val이 넘치면 ValueError.

정답:

```python
def set_field(reg, shift, width, val):
    mask = (1 << width) - 1
    if val & ~mask:
        raise ValueError("value does not fit")
    return (reg & ~(mask << shift)) | (val << shift)
```

### P3-08 · XOR 체크섬

모든 바이트를 XOR 한 값 xor_sum(data).

정답:

```python
def xor_sum(data):
    x = 0
    for b in data:
        x ^= b
    return x
```

### P3-09 · CRC-8 (poly 0x07)

MSB-first CRC-8/SMBUS crc8(data). check 값: crc8(b"123456789") == 0xF4.

정답:

```python
def crc8(data, poly=0x07):
    crc = 0
    for b in data:
        crc ^= b
        for _ in range(8):
            if crc & 0x80:
                crc = ((crc << 1) ^ poly) & 0xFF
            else:
                crc = (crc << 1) & 0xFF
    return crc
```

### P3-10 · bytearray 슬라이스 대입

buf[off:]를 data로 덮어쓰고(길이 유지) buf를 돌려주는 patch(buf, off, data). 넘치면 ValueError.

정답:

```python
def patch(buf, off, data):
    if off + len(data) > len(buf):
        raise ValueError("out of bounds")
    buf[off:off + len(data)] = data
    return buf
```

## C 10개

| ID | 주제 | 칠 것 |
|---|---|---|
| C3-01 | little-endian 읽기 | p[0]이 하위 바이트인 u16을 읽는 rd_le16(p). |
| C3-02 | big-endian 쓰기 | v를 big-endian 4바이트로 쓰는 wr_be32(p, v). |
| C3-03 | 비트 필드 읽기 (width 32 안전) | get_field(reg, shift, width). width==32일 때 1u<<32 UB를 피할 것. |
| C3-04 | 비트 필드 쓰기 (read-modify-write) | set_field(reg, shift, width, val): 그 자리만 바꾼 값을 돌려준다 (width < 32 가정). |
| C3-05 | CRC-8 (poly 0x07) | crc8(data, n) — MSB-first, init 0. check: "123456789" → 0xF4. |
| C3-06 | 합 체크섬 (8비트 2의 보수) | 모든 바이트 합 + cs == 0 (mod 256)이 되게 하는 checksum8(data, n). |
| C3-07 | 헤더 파싱 (packed struct 없이) | hdr_t {sync, type, len(u16 LE)}. 4바이트 미만이거나 sync != 0xAA면 false. parse_hdr(b, n, out). |
| C3-08 | volatile 레지스터 RMW | REG32(addr) 매크로, 그리고 volatile 레지스터에 mask 비트를 set/clear 하는 reg_set / reg_clear. |
| C3-09 | 12비트 부호 확장 | ADC 12비트 2의 보수 raw를 int16_t로 sext12(raw). |
| C3-10 | snprintf hex dump | 바이트를 "AA 01 FF" 형태로 out에 쓰는 hex_dump(b, n, out, cap). 반환값은 쓴 길이. |

### C3-01 · little-endian 읽기

p[0]이 하위 바이트인 u16을 읽는 rd_le16(p).

정답:

```c
uint16_t rd_le16(const uint8_t *p)
{
    return (uint16_t)(p[0] | (p[1] << 8));
}
```

### C3-02 · big-endian 쓰기

v를 big-endian 4바이트로 쓰는 wr_be32(p, v).

정답:

```c
void wr_be32(uint8_t *p, uint32_t v)
{
    p[0] = (uint8_t)(v >> 24);
    p[1] = (uint8_t)(v >> 16);
    p[2] = (uint8_t)(v >> 8);
    p[3] = (uint8_t)v;
}
```

### C3-03 · 비트 필드 읽기 (width 32 안전)

get_field(reg, shift, width). width==32일 때 1u<<32 UB를 피할 것.

정답:

```c
uint32_t get_field(uint32_t reg, unsigned shift, unsigned width)
{
    uint32_t mask = (width >= 32) ? 0xFFFFFFFFu : ((1u << width) - 1u);
    return (reg >> shift) & mask;
}
```

### C3-04 · 비트 필드 쓰기 (read-modify-write)

set_field(reg, shift, width, val): 그 자리만 바꾼 값을 돌려준다 (width < 32 가정).

정답:

```c
uint32_t set_field(uint32_t reg, unsigned shift, unsigned width, uint32_t val)
{
    uint32_t mask = ((1u << width) - 1u) << shift;
    return (reg & ~mask) | ((val << shift) & mask);
}
```

### C3-05 · CRC-8 (poly 0x07)

crc8(data, n) — MSB-first, init 0. check: "123456789" → 0xF4.

정답:

```c
uint8_t crc8(const uint8_t *data, size_t n)
{
    uint8_t crc = 0;
    while (n--) {
        crc ^= *data++;
        for (int i = 0; i < 8; i++)
            crc = (crc & 0x80) ? (uint8_t)((crc << 1) ^ 0x07) : (uint8_t)(crc << 1);
    }
    return crc;
}
```

### C3-06 · 합 체크섬 (8비트 2의 보수)

모든 바이트 합 + cs == 0 (mod 256)이 되게 하는 checksum8(data, n).

정답:

```c
uint8_t checksum8(const uint8_t *data, size_t n)
{
    uint8_t sum = 0;
    for (size_t i = 0; i < n; i++)
        sum += data[i];
    return (uint8_t)(0u - sum);
}
```

### C3-07 · 헤더 파싱 (packed struct 없이)

hdr_t {sync, type, len(u16 LE)}. 4바이트 미만이거나 sync != 0xAA면 false. parse_hdr(b, n, out).

정답:

```c
typedef struct {
    uint8_t  sync;
    uint8_t  type;
    uint16_t len;
} hdr_t;

bool parse_hdr(const uint8_t *b, size_t n, hdr_t *out)
{
    if (n < 4 || b[0] != 0xAA)
        return false;
    out->sync = b[0];
    out->type = b[1];
    out->len  = (uint16_t)(b[2] | (b[3] << 8));
    return true;
}
```

### C3-08 · volatile 레지스터 RMW

REG32(addr) 매크로, 그리고 volatile 레지스터에 mask 비트를 set/clear 하는 reg_set / reg_clear.

정답:

```c
#define REG32(addr) (*(volatile uint32_t *)(uintptr_t)(addr))

void reg_set(volatile uint32_t *reg, uint32_t mask)   { *reg |= mask; }
void reg_clear(volatile uint32_t *reg, uint32_t mask) { *reg &= ~mask; }
```

### C3-09 · 12비트 부호 확장

ADC 12비트 2의 보수 raw를 int16_t로 sext12(raw).

정답:

```c
int16_t sext12(uint16_t raw)
{
    raw &= 0x0FFF;
    return (raw & 0x0800) ? (int16_t)(raw - 0x1000) : (int16_t)raw;
}
```

### C3-10 · snprintf hex dump

바이트를 "AA 01 FF" 형태로 out에 쓰는 hex_dump(b, n, out, cap). 반환값은 쓴 길이.

정답:

```c
size_t hex_dump(const uint8_t *b, size_t n, char *out, size_t cap)
{
    size_t len = 0;
    if (cap)
        out[0] = '\0';
    for (size_t i = 0; i < n && len + 3 < cap; i++)
        len += (size_t)snprintf(out + len, cap - len, i ? " %02X" : "%02X", b[i]);
    return len;
}
```

## 체크

- [ ] Python 10개를 정답 안 보고 → `python3 drills/drill.py check 3` 에서 ✓ 10
- [ ] C 10개를 정답 안 보고 → ✓ 10
- [ ] 틀린 것 ID를 적어 두고 다음 날 아침 첫 5분에 다시
