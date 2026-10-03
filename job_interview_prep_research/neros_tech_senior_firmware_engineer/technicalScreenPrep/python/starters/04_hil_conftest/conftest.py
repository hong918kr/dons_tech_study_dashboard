"""04 · HIL 리그용 conftest.py — starter

problems/04_hil_conftest.md 의 요구사항 5개를 채운다. 채점: python3 python/run.py 04
(채점기 python/graders/test_04_hil_conftest.py 가 이 파일을 임시 폴더에 복사해 pytester 로 여러 번 돌린다)
다 채우면 아래 TODO/NotImplementedError 표시를 지울 것 — 남아 있으면 채점기가 '아직 안 품'으로 실패 처리한다.
"""
import pytest

from lib.fakerig import FakeRig


def pytest_addoption(parser):
    # TODO 1: --rig 옵션 (choices none/sim, default none)
    raise NotImplementedError


def pytest_configure(config):
    # TODO 2a: "hw" marker 등록 (--strict-markers 에서도 통과하게)
    pass


def pytest_collection_modifyitems(config, items):
    # TODO 2b: --rig=none 이면 hw 테스트에 skip marker (reason 에 "no rig")
    pass


# TODO 5: pytest_runtest_makereport hookwrapper — item.rep_call 을 남겨서 fixture 가 실패 여부를 알게


@pytest.fixture(scope="session")
def rig(request):
    # TODO 3: FakeRig(이름) → power_on → yield → (실패해도) power_off, close
    raise NotImplementedError


@pytest.fixture
def dut(rig, request, tmp_path):
    # TODO 4: reset_dut → yield rig.dut → 테스트 본체가 실패했으면 rig.save_log(tmp_path / "dut.log")
    raise NotImplementedError
