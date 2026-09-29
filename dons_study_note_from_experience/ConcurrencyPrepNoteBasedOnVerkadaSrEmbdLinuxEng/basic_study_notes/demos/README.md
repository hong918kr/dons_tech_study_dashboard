# 노트에 실린 실측 프로그램

스터디 노트(`../`)에 인용된 출력들을 만들어 낸 프로그램이다. 직접 돌려보면
노트의 숫자가 왜 그렇게 나오는지 눈으로 확인할 수 있다.

```sh
cc -std=c11 -O2 -Wall -Wextra -pthread race.c -o /tmp/race && /tmp/race
```

| 파일 | 노트 | 보여주는 것 |
|---|---|---|
| `race.c` `race_v.c` `race_fix.c` | B3 | `counter++` 경쟁, `volatile`로도 안 고쳐짐, mutex로 해결 |
| `torn.c` | B3 | 구조체 찢김 (위도/경도가 섞인다) |
| `toctou.c` | B3 | 모든 함수가 락을 써도 생기는 race condition |
| `interleave.c` `interleave_y.c` | B1 | 실행 순서가 몇 가지나 관측되는가 |
| `argbug.c` `argfix.c` `detach.c` `mainexit.c` | B2 | 스레드 인자 전달 함정, join 없이 끝날 때 |
| `hoist.c` `hoist2.c` `v0.c` `v1.c` `v2.c` | A3·B6 | `-O2`가 대기 루프를 지운다 → volatile → atomic |
| `uaf.c` | A3·B9 | join 전에 free → ASan `heap-use-after-free` |
| `ub.c` `crash.c` | A3 | UBSan 출력, SIGSEGV exit=139 |
| `fd.c` `fds.c` `nb2.c` `sig.c` | A4 | 파일 디스크립터, 논블로킹 EAGAIN, 시그널 종료 |
| `c1_ring.c` | C1 | 링버퍼 인덱스 산술과 eviction |
| `c2_bsearch.c` | C2 | 이진 탐색 손 추적, lower/upper bound |
| `c3_prefix.c` | C3 | 누적합·사다리꼴 적분, float vs double 오차 |
| `deque_check.c` | C4 | 단조 덱 vs brute force (0 불일치, 50배 빠름) |
| `bucket_check.c` | C5 | 시간 버킷 vs brute force, 메모리 96B vs 3.2MB |
| `hash_check.c` `probe_check.c` `cluster_check.c` | C6 | 해시 분포, 탐사 길이, 클러스터링 실측 |
| `heap_check.c` `bug_demo.c` | C7 | 힙·top-k 검증, sift-down 버그 재현 |
| `a1.c` `a2.c` | A1·A2 | 구조체 크기·정렬, 비트 pack/unpack |
| `snippets_c6.c` `snippets_c7.c` | C6·C7 | 노트에 실린 스니펫 모음 |

나머지(`bench.c`, `hello.c`, `t.c` 등)는 중간 확인용이다.
