#!/bin/bash
# ARM-M01 demo_abi.sh — 같은 C 를 두 ABI 로 컴파일해 호출 규약 차이를 asm 으로 본다.
# usage: bash demo_abi.sh   (lesson 디렉터리에서; 결과는 demo_abi_apple_vs_linux.txt)
set -e
cd "$(dirname "$0")"
export SDKROOT=${SDKROOT:-/Library/Developer/CommandLineTools/SDKs/MacOSX26.4.sdk}
for t in arm64-apple-macos14 aarch64-linux-gnu; do
  echo "=================== --target=$t  (clang -S -O2)"
  clang --target=$t -S -O2 -fno-asynchronous-unwind-tables -o - demo_abi.c \
    | grep -v '^\s*\.\(loh\|cfi\|p2align\|section\|build_version\|subsections\|file\|ident\|addrsig\|type\|size\|text\)' \
    | grep -v '^Lloh\|^\.Lfunc\|End function\|kill:'
done
