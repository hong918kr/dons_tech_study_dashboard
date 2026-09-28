#!/bin/bash
# 더블클릭하면 Verkada 인터뷰 준비 전체 목차가 브라우저에서 열린다.
cd "$(dirname "$0")" || exit 1
open index.html
