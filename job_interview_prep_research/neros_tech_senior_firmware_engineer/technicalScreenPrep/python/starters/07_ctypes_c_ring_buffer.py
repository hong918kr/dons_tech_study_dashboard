"""07 · C ring buffer 를 pytest + ctypes 로 테스트하기 — starter

피검 코드: ../../onsitePrep/code/ring_buffer.c (온사이트용 C 링버퍼 그대로). static 함수라서
c/rb_shim.c 가 #include 하고 non-static 래퍼(shim_*)를 export 한다.

보여 주려는 것
- session fixture 에서 cc 로 공유 라이브러리를 한 번만 빌드 (tmp_path_factory). cc 가 없으면 skip
- ctypes.Structure 로 C 구조체를 그대로 표현 → head/tail 을 테스트에서 직접 UINT32_MAX 근처로 세팅
- argtypes/restype 선언 (안 하면 int 로 가정해서 size_t·포인터가 깨진다)
- 같은 테스트를 Python 레퍼런스 모델과 비교 → "C 를 Python 으로 테스트한다"는 FW 테스트 팀의 일상
"""
import ctypes
import random
import shutil
import subprocess
from collections import deque
from pathlib import Path

import pytest

SHIM = Path(__file__).resolve().parents[2] / "c" / "rb_shim.c"
U8, U32, SZ = ctypes.c_uint8, ctypes.c_uint32, ctypes.c_size_t


class RB(ctypes.Structure):
    # TODO: onsitePrep/code/ring_buffer.c 의 rb_t 와 똑같이 (buf[16], head, tail)
    _fields_ = []


@pytest.fixture(scope="session")
def lib(tmp_path_factory):
    cc = shutil.which("cc") or shutil.which("gcc") or shutil.which("clang")
    if not cc:
        pytest.skip("C compiler not found")
    so = tmp_path_factory.mktemp("c") / "librb.so"
    subprocess.run([cc, "-std=c11", "-Wall", "-Wextra", "-Werror", "-O2", "-shared", "-fPIC",
                    str(SHIM), "-o", str(so)], check=True)
    L = ctypes.CDLL(str(so))
    P = ctypes.POINTER(RB)
    sig = {
        "shim_sizeof": ([], SZ), "shim_capacity": ([], U32), "shim_init": ([P], None),
        "shim_count": ([P], U32), "shim_push": ([P, U8], ctypes.c_int),
        "shim_pop": ([P, ctypes.POINTER(U8)], ctypes.c_int),
        "shim_peek": ([P, ctypes.POINTER(U8)], ctypes.c_int),
        "shim_write": ([P, ctypes.c_char_p, SZ], SZ),
        "shim_read": ([P, ctypes.c_char_p, SZ], SZ),
        "shim_selftest": ([], ctypes.c_int),
    }
    for name, (args, res) in sig.items():
        fn = getattr(L, name)
        fn.argtypes, fn.restype = args, res
    return L


@pytest.fixture
def rb(lib):
    if ctypes.sizeof(RB) != lib.shim_sizeof():          # 작은 구조체를 C 에 넘기면 메모리를 덮어쓴다
        pytest.fail("RB layout != rb_t — _fields_ 부터 맞출 것")
    r = RB()
    lib.shim_init(ctypes.byref(r))
    return r


def pop(lib, r):
    v = U8()
    ok = lib.shim_pop(ctypes.byref(r), ctypes.byref(v))
    return v.value if ok else None


def write(lib, r, data):
    return lib.shim_write(ctypes.byref(r), bytes(data), len(data))


def read(lib, r, n):
    out = ctypes.create_string_buffer(n)
    got = lib.shim_read(ctypes.byref(r), out, n)
    return out.raw[:got]


# ------------------------------------------------------------------ tests
def test_struct_layout_matches_c(lib):
    assert ctypes.sizeof(RB) == lib.shim_sizeof()        # 레이아웃이 틀리면 나머지 테스트는 의미 없다
    assert lib.shim_capacity() == 16


def test_empty(lib, rb):
    assert lib.shim_count(ctypes.byref(rb)) == 0
    assert pop(lib, rb) is None


def test_all_16_slots_usable_then_full(lib, rb):
    pytest.skip("TODO: 16개 push 성공, 17번째 실패, pop 순서")


@pytest.mark.parametrize("start", [0, 5, 0xFFFFFFFF - 3, 0xFFFFFFFF])
def test_index_wrap_at_2_pow_32(lib, rb, start):
    pytest.skip("TODO: rb.head = rb.tail = start 로 세팅하고 10개 push/pop → 32비트 wrap 검증")


@pytest.mark.parametrize("wlen, rlen", [(11, 7), (16, 16), (3, 5), (20, 1)])
def test_bulk_write_read_matches_model(lib, rb, wlen, rlen):
    pytest.skip("TODO: write/read 헬퍼로 300바퀴, deque 모델과 비교")


def test_random_ops_against_model(lib, rb):
    pytest.skip("TODO (보너스)")
