"""
코딩 문제 데이터 (AlgoExpert 스타일).
첫 세트: 임베디드 C 기본 연산(basic ops) 10문제.

각 문제: id, num, title, difficulty, category, description(markdown),
         signature, starter(C), harness(C: CHECK 매크로 사용하는 main).

최종 컴파일 소스 = code_runner.COMMON_HEADER + 사용자코드 + harness
harness 안에서 CHECK(cond, "msg", ...) 로 검사하고 DONE() 으로 결과 출력.
"""
from __future__ import annotations

import json
from pathlib import Path

# 직접 만든 샘플 문제(임베디드 기본기). CHECK/DONE 매크로 사용 → COMMON_HEADER 필요.
AUTHORED: list[dict] = [
    {
        "id": "01-set-bit",
        "num": 1,
        "title": "Set Bit",
        "difficulty": "Easy",
        "category": "Embedded C · Bit Ops",
        "description": """주어진 8비트 값 `value` 의 `pos` 번째 비트(0부터 시작, LSB 기준)를 **1로 설정**해 반환하세요. 나머지 비트는 그대로 둡니다.

```
set_bit(0b0000_0000, 0)  ->  0b0000_0001  (0x01)
set_bit(0b0000_0000, 7)  ->  0b1000_0000  (0x80)
set_bit(0b1111_1111, 3)  ->  0b1111_1111  (이미 1)
```

- `0 <= pos <= 7`""",
        "signature": "uint8_t set_bit(uint8_t value, uint8_t pos)",
        "starter": "uint8_t set_bit(uint8_t value, uint8_t pos) {\n    // TODO\n    return value;\n}\n",
        "harness": r"""int main(void){
    CHECK(set_bit(0x00,0)==0x01, "set_bit(0x00,0) = 0x%02X (expected 0x01)", set_bit(0x00,0));
    CHECK(set_bit(0x00,7)==0x80, "set_bit(0x00,7) = 0x%02X (expected 0x80)", set_bit(0x00,7));
    CHECK(set_bit(0x0F,4)==0x1F, "set_bit(0x0F,4) = 0x%02X (expected 0x1F)", set_bit(0x0F,4));
    CHECK(set_bit(0xFF,3)==0xFF, "set_bit(0xFF,3) = 0x%02X (expected 0xFF)", set_bit(0xFF,3));
    DONE();
    return 0;
}""",
    },
    {
        "id": "02-clear-bit",
        "num": 2,
        "title": "Clear Bit",
        "difficulty": "Easy",
        "category": "Embedded C · Bit Ops",
        "description": """8비트 값 `value` 의 `pos` 번째 비트를 **0으로 클리어**해 반환하세요.

```
clear_bit(0xFF, 0)  ->  0xFE
clear_bit(0x80, 7)  ->  0x00
clear_bit(0x00, 3)  ->  0x00  (이미 0)
```

- `0 <= pos <= 7`""",
        "signature": "uint8_t clear_bit(uint8_t value, uint8_t pos)",
        "starter": "uint8_t clear_bit(uint8_t value, uint8_t pos) {\n    // TODO\n    return value;\n}\n",
        "harness": r"""int main(void){
    CHECK(clear_bit(0xFF,0)==0xFE, "clear_bit(0xFF,0) = 0x%02X (expected 0xFE)", clear_bit(0xFF,0));
    CHECK(clear_bit(0x80,7)==0x00, "clear_bit(0x80,7) = 0x%02X (expected 0x00)", clear_bit(0x80,7));
    CHECK(clear_bit(0x00,3)==0x00, "clear_bit(0x00,3) = 0x%02X (expected 0x00)", clear_bit(0x00,3));
    CHECK(clear_bit(0x1F,4)==0x0F, "clear_bit(0x1F,4) = 0x%02X (expected 0x0F)", clear_bit(0x1F,4));
    DONE();
    return 0;
}""",
    },
    {
        "id": "03-toggle-bit",
        "num": 3,
        "title": "Toggle Bit",
        "difficulty": "Easy",
        "category": "Embedded C · Bit Ops",
        "description": """8비트 값 `value` 의 `pos` 번째 비트를 **반전(toggle)** 해 반환하세요.

```
toggle_bit(0x00, 0)  ->  0x01
toggle_bit(0xFF, 0)  ->  0xFE
toggle_bit(0x0F, 7)  ->  0x8F
```

- `0 <= pos <= 7`""",
        "signature": "uint8_t toggle_bit(uint8_t value, uint8_t pos)",
        "starter": "uint8_t toggle_bit(uint8_t value, uint8_t pos) {\n    // TODO\n    return value;\n}\n",
        "harness": r"""int main(void){
    CHECK(toggle_bit(0x00,0)==0x01, "toggle_bit(0x00,0) = 0x%02X (expected 0x01)", toggle_bit(0x00,0));
    CHECK(toggle_bit(0xFF,0)==0xFE, "toggle_bit(0xFF,0) = 0x%02X (expected 0xFE)", toggle_bit(0xFF,0));
    CHECK(toggle_bit(0x0F,7)==0x8F, "toggle_bit(0x0F,7) = 0x%02X (expected 0x8F)", toggle_bit(0x0F,7));
    CHECK(toggle_bit(0xAA,1)==0xA8, "toggle_bit(0xAA,1) = 0x%02X (expected 0xA8)", toggle_bit(0xAA,1));
    DONE();
    return 0;
}""",
    },
    {
        "id": "04-is-bit-set",
        "num": 4,
        "title": "Is Bit Set",
        "difficulty": "Easy",
        "category": "Embedded C · Bit Ops",
        "description": """8비트 값 `value` 의 `pos` 번째 비트가 1이면 `true`, 아니면 `false` 를 반환하세요.

```
is_bit_set(0x01, 0)  ->  true
is_bit_set(0x80, 7)  ->  true
is_bit_set(0x0F, 4)  ->  false
```

- `0 <= pos <= 7`""",
        "signature": "bool is_bit_set(uint8_t value, uint8_t pos)",
        "starter": "bool is_bit_set(uint8_t value, uint8_t pos) {\n    // TODO\n    return false;\n}\n",
        "harness": r"""int main(void){
    CHECK(is_bit_set(0x01,0)==true,  "is_bit_set(0x01,0) = %d (expected 1)", is_bit_set(0x01,0));
    CHECK(is_bit_set(0x80,7)==true,  "is_bit_set(0x80,7) = %d (expected 1)", is_bit_set(0x80,7));
    CHECK(is_bit_set(0x0F,4)==false, "is_bit_set(0x0F,4) = %d (expected 0)", is_bit_set(0x0F,4));
    CHECK(is_bit_set(0xAA,3)==true,  "is_bit_set(0xAA,3) = %d (expected 1)", is_bit_set(0xAA,3));
    DONE();
    return 0;
}""",
    },
    {
        "id": "05-count-set-bits",
        "num": 5,
        "title": "Count Set Bits (Popcount)",
        "difficulty": "Easy",
        "category": "Embedded C · Bit Ops",
        "description": """32비트 부호없는 정수 `n` 에서 **1인 비트의 개수**를 반환하세요.

```
count_set_bits(0)           ->  0
count_set_bits(0xFF)        ->  8
count_set_bits(0xFFFFFFFF)  ->  32
count_set_bits(0b1010)      ->  2
```""",
        "signature": "int count_set_bits(uint32_t n)",
        "starter": "int count_set_bits(uint32_t n) {\n    // TODO\n    return 0;\n}\n",
        "harness": r"""int main(void){
    CHECK(count_set_bits(0u)==0,           "count_set_bits(0) = %d (expected 0)", count_set_bits(0u));
    CHECK(count_set_bits(0xFFu)==8,         "count_set_bits(0xFF) = %d (expected 8)", count_set_bits(0xFFu));
    CHECK(count_set_bits(0xFFFFFFFFu)==32,  "count_set_bits(0xFFFFFFFF) = %d (expected 32)", count_set_bits(0xFFFFFFFFu));
    CHECK(count_set_bits(0xAu)==2,          "count_set_bits(0xA) = %d (expected 2)", count_set_bits(0xAu));
    CHECK(count_set_bits(0x80000001u)==2,   "count_set_bits(0x80000001) = %d (expected 2)", count_set_bits(0x80000001u));
    DONE();
    return 0;
}""",
    },
    {
        "id": "06-swap-no-temp",
        "num": 6,
        "title": "Swap Without Temp",
        "difficulty": "Easy",
        "category": "Embedded C · Pointers",
        "description": """포인터로 전달된 두 정수 `*a`, `*b` 의 값을 **서로 교환**하세요. (임시 변수 없이 푸는 게 정석이지만, 결과만 맞으면 됩니다.)

```
int x=3, y=7; swap(&x,&y);  ->  x==7, y==3
```""",
        "signature": "void swap(int *a, int *b)",
        "starter": "void swap(int *a, int *b) {\n    // TODO\n}\n",
        "harness": r"""int main(void){
    int x=3, y=7;   swap(&x,&y);
    CHECK(x==7 && y==3, "swap(3,7) -> x=%d y=%d (expected 7,3)", x, y);
    int a=-5, b=100; swap(&a,&b);
    CHECK(a==100 && b==-5, "swap(-5,100) -> a=%d b=%d (expected 100,-5)", a, b);
    int p=42, q=42;  swap(&p,&q);
    CHECK(p==42 && q==42, "swap(42,42) -> p=%d q=%d (expected 42,42)", p, q);
    DONE();
    return 0;
}""",
    },
    {
        "id": "07-is-power-of-two",
        "num": 7,
        "title": "Is Power of Two",
        "difficulty": "Easy",
        "category": "Embedded C · Bit Ops",
        "description": """32비트 부호없는 정수 `n` 이 **2의 거듭제곱**이면 `true`, 아니면 `false`. (0은 2의 거듭제곱이 아님)

```
is_power_of_two(1)  ->  true   (2^0)
is_power_of_two(16) ->  true
is_power_of_two(0)  ->  false
is_power_of_two(6)  ->  false
```

힌트: `n & (n-1)`""",
        "signature": "bool is_power_of_two(uint32_t n)",
        "starter": "bool is_power_of_two(uint32_t n) {\n    // TODO\n    return false;\n}\n",
        "harness": r"""int main(void){
    CHECK(is_power_of_two(1u)==true,   "is_power_of_two(1) = %d (expected 1)", is_power_of_two(1u));
    CHECK(is_power_of_two(16u)==true,  "is_power_of_two(16) = %d (expected 1)", is_power_of_two(16u));
    CHECK(is_power_of_two(0u)==false,  "is_power_of_two(0) = %d (expected 0)", is_power_of_two(0u));
    CHECK(is_power_of_two(6u)==false,  "is_power_of_two(6) = %d (expected 0)", is_power_of_two(6u));
    CHECK(is_power_of_two(0x80000000u)==true, "is_power_of_two(2^31) = %d (expected 1)", is_power_of_two(0x80000000u));
    DONE();
    return 0;
}""",
    },
    {
        "id": "08-swap-endian",
        "num": 8,
        "title": "Swap Endianness (32-bit)",
        "difficulty": "Medium",
        "category": "Embedded C · Bit Ops",
        "description": """32비트 값의 **바이트 순서를 뒤집어** 반환하세요 (little ↔ big endian).

```
swap_endian(0x12345678)  ->  0x78563412
swap_endian(0x000000FF)  ->  0xFF000000
```""",
        "signature": "uint32_t swap_endian(uint32_t n)",
        "starter": "uint32_t swap_endian(uint32_t n) {\n    // TODO\n    return n;\n}\n",
        "harness": r"""int main(void){
    CHECK(swap_endian(0x12345678u)==0x78563412u, "swap_endian(0x12345678) = 0x%08X (expected 0x78563412)", swap_endian(0x12345678u));
    CHECK(swap_endian(0x000000FFu)==0xFF000000u, "swap_endian(0x000000FF) = 0x%08X (expected 0xFF000000)", swap_endian(0x000000FFu));
    CHECK(swap_endian(0x00000000u)==0x00000000u, "swap_endian(0) = 0x%08X (expected 0)", swap_endian(0x00000000u));
    CHECK(swap_endian(0xAABBCCDDu)==0xDDCCBBAAu, "swap_endian(0xAABBCCDD) = 0x%08X (expected 0xDDCCBBAA)", swap_endian(0xAABBCCDDu));
    DONE();
    return 0;
}""",
    },
    {
        "id": "09-reverse-bits",
        "num": 9,
        "title": "Reverse Bits (8-bit)",
        "difficulty": "Medium",
        "category": "Embedded C · Bit Ops",
        "description": """8비트 값의 **비트 순서를 반전**해 반환하세요 (MSB↔LSB).

```
reverse_bits(0b0000_0001)  ->  0b1000_0000  (0x80)
reverse_bits(0b1010_0000)  ->  0b0000_0101  (0x05)
```""",
        "signature": "uint8_t reverse_bits(uint8_t n)",
        "starter": "uint8_t reverse_bits(uint8_t n) {\n    // TODO\n    return n;\n}\n",
        "harness": r"""int main(void){
    CHECK(reverse_bits(0x01)==0x80, "reverse_bits(0x01) = 0x%02X (expected 0x80)", reverse_bits(0x01));
    CHECK(reverse_bits(0xA0)==0x05, "reverse_bits(0xA0) = 0x%02X (expected 0x05)", reverse_bits(0xA0));
    CHECK(reverse_bits(0xFF)==0xFF, "reverse_bits(0xFF) = 0x%02X (expected 0xFF)", reverse_bits(0xFF));
    CHECK(reverse_bits(0x00)==0x00, "reverse_bits(0x00) = 0x%02X (expected 0x00)", reverse_bits(0x00));
    CHECK(reverse_bits(0x12)==0x48, "reverse_bits(0x12) = 0x%02X (expected 0x48)", reverse_bits(0x12));
    DONE();
    return 0;
}""",
    },
    {
        "id": "10-extract-bits",
        "num": 10,
        "title": "Extract Bitfield",
        "difficulty": "Medium",
        "category": "Embedded C · Bit Ops",
        "description": """`value` 에서 `start` 비트부터 `len` 비트를 **추출**해 LSB 쪽으로 정렬한 값을 반환하세요.

```
extract_bits(0b1111_0000, 4, 4)  ->  0b1111  (0x0F)
extract_bits(0xABCD, 4, 8)       ->  0xBC
extract_bits(0xFF, 0, 3)         ->  0x07
```

- `0 <= start`, `1 <= len`, `start + len <= 32`""",
        "signature": "uint32_t extract_bits(uint32_t value, int start, int len)",
        "starter": "uint32_t extract_bits(uint32_t value, int start, int len) {\n    // TODO\n    return 0;\n}\n",
        "harness": r"""int main(void){
    CHECK(extract_bits(0xF0u,4,4)==0x0Fu,   "extract_bits(0xF0,4,4) = 0x%X (expected 0xF)", extract_bits(0xF0u,4,4));
    CHECK(extract_bits(0xABCDu,4,8)==0xBCu, "extract_bits(0xABCD,4,8) = 0x%X (expected 0xBC)", extract_bits(0xABCDu,4,8));
    CHECK(extract_bits(0xFFu,0,3)==0x07u,   "extract_bits(0xFF,0,3) = 0x%X (expected 0x7)", extract_bits(0xFFu,0,3));
    CHECK(extract_bits(0xFFFFFFFFu,0,32)==0xFFFFFFFFu, "extract_bits(all,0,32) = 0x%X (expected 0xFFFFFFFF)", extract_bits(0xFFFFFFFFu,0,32));
    CHECK(extract_bits(0x12345678u,8,8)==0x56u, "extract_bits(0x12345678,8,8) = 0x%X (expected 0x56)", extract_bits(0x12345678u,8,8));
    DONE();
    return 0;
}""",
    },
]

def _load_imported() -> list[dict]:
    """import_problems.py 가 만든 data/problems/imported.json 로드."""
    from config import PROBLEMS_DIR

    f = PROBLEMS_DIR / "imported.json"
    if not f.exists():
        return []
    try:
        return json.loads(f.read_text(encoding="utf-8"))
    except Exception:
        return []


def _samples() -> list[dict]:
    from code_runner import COMMON_HEADER

    out = []
    for p in AUTHORED:
        q = dict(p)
        q["category"] = "샘플"
        q.setdefault("lang", "c")
        q.setdefault("prelude", COMMON_HEADER)
        q.setdefault("range", "")
        q.setdefault("expected_output", "")
        q.setdefault("source_file", "")
        out.append(q)
    return out


def _load_written_solution(pid: str) -> str:
    """직접 작성·검증한 솔루션(data/problems/written_solutions/<id>.c)."""
    from config import PROBLEMS_DIR

    f = PROBLEMS_DIR / "written_solutions" / f"{pid}.c"
    return f.read_text(encoding="utf-8") if f.exists() else ""


def _build() -> list[dict]:
    # 가져온 실제 문제가 있으면 그것만 쭉 나열. 없으면 샘플로 폴백.
    imported = _load_imported()
    problems = imported if imported else _samples()
    # 직접 작성·검증한 솔루션 파일이 있으면 항상 우선 적용(없을 때 보강 포함)
    for p in problems:
        written = _load_written_solution(p["id"])
        if written.strip():
            p["solution"] = written
    return problems


PROBLEMS: list[dict] = _build()


def reload_problems() -> None:
    global PROBLEMS, _BY_ID
    PROBLEMS = _build()
    _BY_ID = {p["id"]: p for p in PROBLEMS}


_BY_ID = {p["id"]: p for p in PROBLEMS}


def get_problem(pid: str) -> dict | None:
    return _BY_ID.get(pid)
