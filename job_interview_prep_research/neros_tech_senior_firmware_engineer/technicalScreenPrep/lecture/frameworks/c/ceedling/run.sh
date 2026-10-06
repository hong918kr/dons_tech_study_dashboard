#!/usr/bin/env bash
# Ceedling 0.31.1 을 c/_vendor/gems 에 설치된 것으로 실행 (전역 설치 없음)
set -euo pipefail
cd "$(dirname "$0")"
export GEM_HOME="$PWD/../_vendor/gems" GEM_PATH="$PWD/../_vendor/gems"
[ -x "$GEM_HOME/bin/ceedling" ] || { gem install --no-document rake -v 13.0.6 && gem install --no-document ceedling -v 0.31.1 && gem uninstall rake -v ">13.0.6" -x -I || true; }
"$GEM_HOME/bin/ceedling" "${@:-test:all}"
