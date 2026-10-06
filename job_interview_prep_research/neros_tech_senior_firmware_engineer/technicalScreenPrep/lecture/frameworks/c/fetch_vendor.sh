#!/usr/bin/env bash
# C 테스트 프레임워크 소스를 c/_vendor/ 에 받는다 (git 무시). cmake 없이 Makefile 로 직접 빌드한다.
set -euo pipefail
cd "$(dirname "$0")"
mkdir -p _vendor && cd _vendor
clone() { [ -d "$1" ] || git -c advice.detachedHead=false clone -q --depth 1 --branch "$2" "$3" "$1"; echo "$1 @ $2"; }
clone unity      v2.6.0  https://github.com/ThrowTheSwitch/Unity.git
clone cmock      v2.5.3  https://github.com/ThrowTheSwitch/CMock.git
clone cpputest   v4.0    https://github.com/cpputest/cpputest.git
clone googletest v1.15.2 https://github.com/google/googletest.git
