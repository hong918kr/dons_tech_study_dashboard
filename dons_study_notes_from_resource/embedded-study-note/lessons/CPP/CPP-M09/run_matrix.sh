#!/bin/bash
# CPP-M09 run_matrix.sh — demo_relocate.cpp 를 libc++(맥) / libstdc++(Lima VM elinux) × exceptions on/off 로 돌린다
set -e
: "${SDKROOT:=/Library/Developer/CommandLineTools/SDKs/MacOSX26.4.sdk}"; export SDKROOT
HERE="$(cd "$(dirname "$0")" && pwd)"
T="$(mktemp -d)"
c++ -std=c++20 -O2 -fno-exceptions -fno-rtti "$HERE/demo_relocate.cpp" -o "$T/off" && "$T/off"
c++ -std=c++20 -O2                           "$HERE/demo_relocate.cpp" -o "$T/on"  && "$T/on"
tar -cf - -C "$HERE" demo_relocate.cpp | limactl shell elinux -- bash -c '
  D=$(mktemp -d); tar -xf - -C "$D"; cd "$D"
  g++ -std=c++20 -O2 -fno-exceptions -fno-rtti demo_relocate.cpp -o off && ./off
  g++ -std=c++20 -O2                           demo_relocate.cpp -o on  && ./on'
