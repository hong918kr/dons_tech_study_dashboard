"""04 · HIL 리그용 conftest.py — 모범답안

요구사항 (problems/04_hil_conftest.md):
1. --rig 옵션: none(기본) | sim. 그 밖의 값은 usage error
2. @pytest.mark.hw 테스트는 --rig=none 이면 skip (이유에 "no rig")
3. rig fixture: session scope — 리그는 세션에 한 번만 연다(전원·프로브 초기화가 비싸다). 끝나면 power_off → close
4. dut fixture: 테스트마다 reset_dut() 후 rig.dut 를 준다. 테스트 본체가 실패하면 rig.save_log(tmp_path/"dut.log")
5. 실패 여부를 fixture 가 알 수 있게 pytest_runtest_makereport hookwrapper 로 report 를 item 에 붙인다
"""
import pytest

from lib.fakerig import FakeRig


def pytest_addoption(parser):
    parser.addoption("--rig", choices=["none", "sim"], default="none",
                     help="which HIL rig to use (none = skip hardware tests)")


def pytest_configure(config):
    config.addinivalue_line("markers", "hw: needs a HIL rig (--rig=sim)")


def pytest_collection_modifyitems(config, items):
    if config.getoption("--rig") != "none":
        return
    skip_hw = pytest.mark.skip(reason="no rig (run with --rig=sim)")
    for item in items:
        if "hw" in item.keywords:
            item.add_marker(skip_hw)


@pytest.hookimpl(hookwrapper=True)
def pytest_runtest_makereport(item, call):
    outcome = yield
    rep = outcome.get_result()
    setattr(item, f"rep_{rep.when}", rep)        # item.rep_setup / rep_call / rep_teardown


@pytest.fixture(scope="session")
def rig(request):
    r = FakeRig(request.config.getoption("--rig"))
    r.power_on()
    try:
        yield r
    finally:                                     # 테스트가 터져도 전원은 끈다
        r.power_off()
        r.close()


@pytest.fixture
def dut(rig, request, tmp_path):
    rig.reset_dut()                              # 테스트 사이 상태 누수 차단
    yield rig.dut
    rep = getattr(request.node, "rep_call", None)
    if rep is not None and rep.failed:
        rig.save_log(tmp_path / "dut.log")       # 실패한 테스트만 증거를 남긴다
