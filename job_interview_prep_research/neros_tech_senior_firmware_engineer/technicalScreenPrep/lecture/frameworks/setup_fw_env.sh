#!/usr/bin/env bash
# L02 프레임워크 예제용 Python 3.12 환경 (.venv-fw). 시스템 Python(3.9)은 건드리지 않는다.
#   bash lecture/frameworks/setup_fw_env.sh
# - uv 로 독립 실행형 Python 3.12 를 technicalScreenPrep/.uv-python/ 에 받는다 (git 무시, 지우면 끝)
# - py-micro-hil 은 PyPI 메타데이터가 RPi.GPIO/spidev 를 무조건 요구해서 macOS 에선 --no-deps 로 설치
set -euo pipefail
cd "$(dirname "$0")/../.."
python3 -m venv .venv-uv >/dev/null 2>&1 || true
.venv-uv/bin/pip install -q uv
export UV_PYTHON_INSTALL_DIR="$PWD/.uv-python"
.venv-uv/bin/uv python install 3.12
.venv-uv/bin/uv venv --python 3.12 .venv-fw
.venv-uv/bin/uv pip install --python .venv-fw/bin/python -q pytest robotframework behave openhtf \
    pyserial PyYAML pymodbus smbus2 jinja2 termcolor
.venv-uv/bin/uv pip install --python .venv-fw/bin/python -q --no-deps py-micro-hil
.venv-fw/bin/python -c "import robot, behave, openhtf, py_micro_hil; print('fw env ok')"
