"""테스트가 검사할 '피검 코드(code under test)' 모음.

테스트 파일은 구현을 직접 import 하지 않고 load("ringbuf") 로 가져온다.
run.py 가 환경변수 TS_IMPL_RINGBUF=lib.mutants.ringbuf_m3 처럼 바꿔 끼우면
같은 테스트가 버그 심은 버전(mutant)을 상대로 돈다 → 테스트가 버그를 잡는지 채점.
"""
import importlib
import os


def load(name):
    """name = "ringbuf" | "sensor_driver" → 모듈. 환경변수로 mutant 교체 가능."""
    return importlib.import_module(os.environ.get(f"TS_IMPL_{name.upper()}", f"lib.{name}"))
