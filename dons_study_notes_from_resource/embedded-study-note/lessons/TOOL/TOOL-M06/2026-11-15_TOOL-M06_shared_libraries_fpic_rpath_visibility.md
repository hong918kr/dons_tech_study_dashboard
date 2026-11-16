---
id: TOOL-M06
time: 80분
updated: 2026-11-15
---
# TOOL-M06 공유 라이브러리: -fPIC, dylib/so, rpath, 가시성

## 학습 목표

- 공유 라이브러리(shared library)가 **이름으로** 찾아진다는 것을 이해하고, 맥의 install name(`LC_ID_DYLIB`) · `LC_LOAD_DYLIB` · `LC_RPATH`(`@rpath`, `@loader_path`)와 ELF 의 `SONAME` · `NEEDED` · `RUNPATH`(`$ORIGIN`)를 `otool -L/-l`, `readelf -d` 로 읽고 고친다.
- 빌드 폴더에서만 도는 앱을 **옮겨도 도는 번들(relocatable bundle)** 로 만들고, 이미 빌드된 바이너리는 `install_name_tool` / `patchelf` 로 수술한다.
- `-fvisibility=hidden` + `FOO_API __attribute__((visibility("default")))` 로 공개 API 만 내보내고(`nm -gU`, `nm -D`), 숨기지 않았을 때 ELF 에서 **심볼 끼어들기(interposition)** 가 실제로 결과를 바꾸는 것을 재현한다.
- 구조체 크기가 바뀐 v2 라이브러리를 같은 이름으로 덮어썼을 때 생기는 **조용한 ABI 깨짐**을 보고, 런타임 ABI 확인(handshake) · SONAME/major 이름 올리기 · 심볼 버전(version script)으로 막는다.
- `-fPIC` 가 무엇을 바꾸는지(GOT/PLT), arm64 맥과 aarch64/x86_64 리눅스에서 각각 어떻게 다르게 실패하는지 실측으로 설명한다.

## 1. 문제

임베디드 리눅스 보드(Yocto/Buildroot rootfs)에서 "제 PC 에서는 되는데 보드에 설치하면 `error while loading shared libraries`", "라이브러리만 업데이트했더니 값이 이상해요" 는 아주 흔한 장애이고, 면접에서도 "rpath 와 SONAME 설명해 보세요", "왜 -fvisibility=hidden 을 쓰죠?" 로 그대로 나온다. MCU 펌웨어는 공유 라이브러리를 쓰지 않지만(Q8), 같은 팀의 리눅스 측 툴 · 테스트 하네스 · 게이트웨이 앱은 `.so` 로 배포된다.

정적 라이브러리(TOOL-B09)는 링크할 때 코드가 복사된다. 공유 라이브러리는 **이름만** 기록되고, 실행할 때 로더(맥 `dyld`, 리눅스 `ld.so`)가 그 이름으로 파일을 찾아 붙인다. 그래서 문제는 세 가지로 나뉜다: (A) 그 이름이 **어디서나 찾아지는가**, (B) 라이브러리가 **무엇을 내보내는가**, (C) 같은 이름의 새 파일이 **옛 앱과 여전히 맞는가**.

```svg
<svg viewBox="0 0 740 330" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <defs>
    <marker id="m06a1" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto">
      <path d="M0 0 L10 5 L0 10 z" fill="currentColor"/>
    </marker>
  </defs>
  <text x="10" y="20" fill="currentColor" font-weight="bold">starter: 이름에 빌드 경로가 박힘</text>
  <rect x="10" y="30" width="330" height="62" rx="6" class="f-warn" stroke="currentColor"/>
  <text x="18" y="50" fill="currentColor" font-family="monospace" font-size="12">bin/app  LC_LOAD_DYLIB</text>
  <text x="18" y="68" fill="currentColor" font-family="monospace" font-size="12">/tmp/TOOL-M06_work/starter/mac/</text>
  <text x="18" y="84" fill="currentColor" font-family="monospace" font-size="12">  build/libfoo.dylib   (LC_RPATH 없음)</text>
  <path d="M340 61 L390 61" stroke="currentColor" stroke-width="2" marker-end="url(#m06a1)"/>
  <rect x="395" y="30" width="335" height="62" rx="6" class="f-mut" stroke="currentColor"/>
  <text x="403" y="52" fill="currentColor" font-size="12">번들 이동 + 빌드 폴더 삭제 →</text>
  <text x="403" y="72" fill="currentColor" font-family="monospace" font-size="12">dyld: Library not loaded ✗</text>
  <text x="10" y="122" fill="currentColor" font-weight="bold">solution (Mac): 이름은 @rpath, 찾을 곳은 앱 기준 상대 경로</text>
  <rect x="10" y="132" width="330" height="62" rx="6" class="f-acc" stroke="currentColor"/>
  <text x="18" y="152" fill="currentColor" font-family="monospace" font-size="12">LC_LOAD_DYLIB @rpath/libfoo.dylib</text>
  <text x="18" y="170" fill="currentColor" font-family="monospace" font-size="12">LC_RPATH      @loader_path/../lib</text>
  <text x="18" y="186" fill="currentColor" font-size="12">@loader_path = app 이 있는 폴더</text>
  <path d="M340 163 L390 163" stroke="currentColor" stroke-width="2" marker-end="url(#m06a1)"/>
  <rect x="395" y="132" width="335" height="62" rx="6" class="f-ok" stroke="currentColor"/>
  <text x="403" y="152" fill="currentColor" font-family="monospace" font-size="12">/opt/foo-1.2.0/bin/../lib/</text>
  <text x="403" y="170" fill="currentColor" font-family="monospace" font-size="12">  libfoo.dylib  ✓</text>
  <text x="403" y="186" fill="currentColor" font-size="12">번들을 어디로 옮겨도 같은 상대 위치</text>
  <text x="10" y="224" fill="currentColor" font-weight="bold">solution (ELF): NEEDED 는 SONAME, 찾을 곳은 $ORIGIN</text>
  <rect x="10" y="234" width="330" height="62" rx="6" class="f-acc" stroke="currentColor"/>
  <text x="18" y="254" fill="currentColor" font-family="monospace" font-size="12">NEEDED   libfoo.so.1</text>
  <text x="18" y="272" fill="currentColor" font-family="monospace" font-size="12">RUNPATH  $ORIGIN/../lib</text>
  <text x="18" y="288" fill="currentColor" font-size="12">$ORIGIN = app 이 있는 폴더</text>
  <path d="M340 265 L390 265" stroke="currentColor" stroke-width="2" marker-end="url(#m06a1)"/>
  <rect x="395" y="234" width="335" height="62" rx="6" class="f-ok" stroke="currentColor"/>
  <text x="403" y="254" fill="currentColor" font-family="monospace" font-size="12">lib/libfoo.so.1 → libfoo.so.1.2.0 ✓</text>
  <text x="403" y="272" fill="currentColor" font-size="12">개발용 링크 libfoo.so 는 런타임에</text>
  <text x="403" y="288" fill="currentColor" font-size="12">필요 없음 (설치 패키지에 없음)</text>
  <text x="10" y="320" fill="currentColor" font-size="12">로더: 기록된 이름 → 검색 경로 대입 → 실제 파일. 이름·검색 경로 둘 다 상대적이어야 옮겨도 산다.</text>
</svg>
```

| 파일 | 내용 |
|---|---|
| `src/foo_internal.h` `src/foo_util.c` | 라이브러리 내부: `struct foo`, 파일 간 공유 헬퍼 `round_div()`(반올림), `ring_put()` — **전역이지만 API 아님** |
| `src/app.c` `src/app_util.c` | 앱. `app_util.c` 에 **같은 이름** `round_div()`(버림) — 회사 유틸을 복사해 온 흔한 상황 |
| `starter/` `solution/` `practice/` | **풀 대상**: `build.conf`(빌드 플래그), `foo.h`(공개 헤더), `foo.c`(공개 API 구현) |
| `check.sh` | 맥(ld64/dyld) + Lima VM(GNU ld 2.46 / glibc 2.43 ld.so)에서 빌드 · 설치 · 이동 · 검사 |
| `demo_mac.sh` → `demo_mac.txt`, `demo_vm.sh` → `demo_vm.txt` | 관찰용 실험 (아래 풀이의 출력은 전부 여기서 나옴) |

라이브러리 `libfoo` 는 센서 값 이동 평균 필터다: `foo_open(window)`, `foo_push(f, sample)` → 반올림 평균, `foo_get_stats(f, &st)` → `{count, min, max}`. 앱의 기대 출력:

```text
libfoo 1.2.0 (app abi 1)
avg: 10 12 13 16 21 28 22
stats: count=7 min=-5 max=40 guard=ok
app round_div(7,2)=3
```

### Part A — 옮겨도 도는 번들 만들기 (install name · rpath · SONAME)

- check.sh 는 라이브러리와 앱을 빌드 폴더에서 만든 뒤 `foo-1.2.0/bin/app`, `foo-1.2.0/lib/` 로 설치하고, 번들을 다른 곳으로 **옮기고 빌드 폴더를 지운 다음** 실행한다.
- `build.conf` 의 `MAC_LIB_LDFLAGS`, `MAC_APP_LDFLAGS`, `ELF_LIB_LDFLAGS`, `ELF_APP_LDFLAGS` 를 고쳐라. 조건: 맥 install name 은 `@rpath/...`, 앱 `LC_RPATH` 는 정확히 `@loader_path/../lib` 하나, 버전 기록 `compatibility version 1.0.0, current version 1.2.0`. ELF 는 `SONAME libfoo.so.1`, 앱 `NEEDED libfoo.so.1`, `RUNPATH $ORIGIN/../lib`(RPATH 아님). 런타임 패키지에는 개발용 링크 `libfoo.so` 가 **없다**.

### Part B — 공개 API 만 내보내기 (가시성)

- `nm -gU libfoo.dylib`(맥) / `nm -D --defined-only libfoo.so.*`(VM) 의 내보낸 심볼이 정확히 `foo_abi_check foo_close foo_get_stats foo_open foo_push foo_version` 이어야 한다. `round_div`, `ring_put` 은 두 파일이 같이 쓰므로 `static` 으로 바꿀 수 없다.
- VM 에서는 앱의 `round_div`(버림)가 라이브러리 내부 호출을 **가로채면 안 된다** — `avg:` 줄이 기대값과 같아야 한다. `foo.h` 의 `FOO_API` 와 `LIB_CFLAGS` 만 고친다.

### Part C — ABI 깨짐을 조용히 넘기지 않기

- 릴리스 담당자가 `struct foo_stats` 에 `avg` 를 **중간에 끼운** 2.0.0 을 만들면서 이름(install name / SONAME)은 그대로 두었다. check.sh 는 v2 를 `-DFOO_ABI_V2` 로 빌드해 옛 앱 옆에 같은 파일 이름으로 끼워 넣는다(`DYLD_LIBRARY_PATH` / `LD_LIBRARY_PATH`).
- 옛 앱은 엉뚱한 값을 찍거나 메모리를 덮어쓰지 말고 **종료 코드 3 으로 거부**해야 한다. v2 헤더로 다시 빌드한 앱은 v2 라이브러리와 정상 동작해야 한다. `foo.c` 의 `foo_abi_check()` 를 구현한다 (`foo.h` 의 `foo_init()` 이 앱 쪽 값을 넘겨준다).

## 2. Problem (English)

> **Ship a shared library that survives installation, hides its internals and refuses ABI mismatches.**
>
> `libfoo` (moving-average filter) is built as `libfoo.dylib` on macOS and `libfoo.so.1.2.0` on Linux, installed with its demo app as `foo-1.2.0/{bin,lib}`. Change only `build.conf`, the `FOO_API` macro in `foo.h`, and `foo_abi_check()` in `foo.c`.
>
> 1. The installed bundle must run after being moved and after the build tree is deleted. macOS: `@rpath` install name, a single `LC_RPATH @loader_path/../lib`, compatibility 1.0.0 / current 1.2.0. Linux: `SONAME libfoo.so.1`, app `NEEDED libfoo.so.1`, `RUNPATH $ORIGIN/../lib`; the runtime package does not contain the `libfoo.so` dev symlink.
> 2. Export exactly the six `foo_*` API functions. `round_div()` / `ring_put()` are shared between two library sources and must stay non-static. The app happens to define its own `round_div()`; on Linux it must not hijack the library's call (expected `avg: 10 12 13 16 21 28 22`).
> 3. A 2.0.0 build changes `struct foo_stats` but keeps the same file name. An app built against 1.x must exit with status 3 and a clear message instead of printing `min=22 max=-5 guard=CLOBBERED`; a 2.x app must still work.
>
> Follow-ups: what does `-fPIC` change and why does it fail only on some targets? Linux search order (RPATH, LD_LIBRARY_PATH, RUNPATH, cache)? Why are internal calls slower without hidden visibility? How do you version a C library ABI? Why don't Cortex-M firmware images use shared libraries?

## 3. 시작 코드

풀 파일 ① 빌드 설정 (bash 문법, check.sh 가 `source`):

<!-- code: starter/build.conf -->

풀 파일 ② 공개 헤더 — `FOO_API` 가 비어 있다:

<!-- code: starter/foo.h -->

풀 파일 ③ 공개 API 구현 — `foo_abi_check()` 가 늘 0 을 돌려준다:

<!-- code: starter/foo.c -->

앱(공통, 수정 금지). `box.guard` 는 `struct foo_stats` 바로 뒤에 놓인 앱의 다른 데이터 역할이다:

<!-- code: src/app.c -->

체커:

<!-- code: check.sh -->

```bash
bash check.sh starter      # 2/15, exit 1
bash check.sh solution     # 15/15, exit 0  (맥 8 + VM 7, 실측 약 2–4 초)
bash check.sh practice     # 20/20, exit 0
```

check.sh 는 `/tmp/TOOL-M06_work/<variant>/` (맥)와 VM 의 같은 경로에서만 작업한다. VM 은 맥 홈을 읽기 전용으로 마운트하므로 소스를 VM `/tmp` 로 복사해 빌드한다. 처음 실행 결과 (실제 출력):

```text
--- Mac (Apple ld64 + dyld) ---
FAIL A install name (otool -D) is @rpath-relative: '/tmp/TOOL-M06_work/starter/mac/build/libfoo.dylib'
FAIL A app: LC_LOAD_DYLIB=@rpath name, LC_RPATH='' (want @loader_path/../lib), no build path
PASS A installed bundle runs (build dir still present): 'libfoo 1.2.0 (app abi 1)'
FAIL A bundle moved + build dir deleted -> still runs: 'dyld[24186]: Library not loaded: /tmp/TOOL-M06_work/starter/mac/build/libfoo.dylib'
FAIL A versions recorded in app: 'compatibility version 0.0.0, current version 0.0.0'
FAIL B exported set (nm -gU) == API only: 'foo_abi_check foo_close foo_get_stats foo_open foo_push foo_version ring_put round_div'
FAIL C old app + v2 lib (DYLD_LIBRARY_PATH drop-in) refused, rc=0: 'stats: count=7 min=22 max=-5 guard=CLOBBERED'
PASS C v2 app + v2 lib still accepted: 'stats: count=7 min=-5 max=40 avg=22 guard=ok'
--- Lima VM elinux (gcc 15.2 / GNU ld 2.46 / glibc ld.so) ---
FAIL V SONAME='' (want libfoo.so.1) + ldconfig -n made link: libfoo.so.1 -> 
FAIL V app NEEDED='libfoo.so' (want the SONAME, not the dev link libfoo.so)
FAIL V app RUNPATH='/tmp/TOOL-M06_work/starter/vm/build' RPATH='' (want RUNPATH $ORIGIN/../lib)
FAIL V runtime bundle (no libfoo.so dev link) moved + build deleted -> loads: '/tmp/TOOL-M06_work/starter/vm/moved/opt/foo-1.2.0/bin/app: error while loading shared libraries: libfoo.so: cannot open shared object file: No such file or directory'
FAIL V lib's internal round_div not interposed by the app's: 'avg: 10 11 13 15 20 27 22'
FAIL V exported set (nm -D --defined-only) == API only: 'foo_abi_check foo_close foo_get_stats foo_open foo_push foo_version ring_put round_div'
FAIL V old app + v2 lib (same file name) refused, rc=0: 'stats: count=7 min=22 max=-5 guard=CLOBBERED'
2/15 passed
```

세 종류의 버그가 다 보인다. **빌드 폴더에서만 돈다**(맥: 절대 install name, ELF: SONAME 없음 + 빌드 경로 RUNPATH), **내부 헬퍼가 공개됨**(그리고 리눅스에서만 앱의 `round_div` 가 끼어들어 평균이 `11 15 20 27` 로 틀림 — 맥에서는 맞게 나온다), **ABI 깨짐이 조용함**(`min=22 max=-5`, 앱의 `guard` 까지 덮어씀, 종료 코드 0).

## 4. 힌트

<details>
<summary>힌트 1 — 맥: dylib 의 "이름" 은 링크할 때 앱에 복사된다</summary>

`-install_name` 을 안 주면 install name 은 `-o` 로 준 경로 그대로다(`otool -D`). 앱은 링크 시점에 그 문자열을 `LC_LOAD_DYLIB` 에 복사한다. 그러니 라이브러리 쪽에는 `@rpath/libfoo.dylib` 라는 "검색해서 찾아라" 이름을, 앱 쪽에는 `-Wl,-rpath,...` 로 검색 위치를 준다. 위치는 절대 경로가 아니라 **앱 파일 기준** 이어야 한다 (`@loader_path`). 버전은 `-compatibility_version`, `-current_version`.

</details>

<details>
<summary>힌트 2 — ELF: 링커가 NEEDED 에 적는 것은 SONAME 이다. 없으면 파일 이름</summary>

`-Wl,-soname,libfoo.so.1` 이 없으면 링커는 `-lfoo` 로 찾은 파일 이름 `libfoo.so`(개발용 심볼릭 링크)를 NEEDED 에 적는다. 그 링크는 `-dev` 패키지에만 있다. `ldconfig -n <dir>` 은 SONAME 을 읽어 `libfoo.so.1 → libfoo.so.1.2.0` 링크를 만든다 (SONAME 이 없으면 아무것도 안 만든다). 검색 위치는 `-Wl,-rpath,'$ORIGIN/../lib'` — 셸이 `$ORIGIN` 을 변수로 펼치지 않게 작은따옴표.

</details>

<details>
<summary>힌트 3 — 숨기기는 컴파일러에게: 기본값을 hidden 으로, 공개할 것만 default</summary>

`-fvisibility=hidden` 은 이 번역 단위의 모든 정의를 "이 모듈 밖에서는 안 보임" 으로 만든다. 그러면 `foo_open` 도 숨겨진다 — 그래서 `FOO_API` 가 `__attribute__((visibility("default")))` 이어야 한다. 앱이 같은 헤더를 include 해도 선언에 붙은 default 는 해가 없다. 숨긴 심볼은 다이내믹 심볼 표에 안 들어가므로 끼어들기 대상이 아니고, 컴파일러는 PLT 없이 직접 `bl` 한다.

</details>

<details>
<summary>힌트 4 — ABI 확인은 "앱이 컴파일될 때 본 값" vs "라이브러리가 컴파일될 때 본 값"</summary>

`foo_init()` 은 헤더의 `static inline` 이라 **앱 안에** 컴파일된다 → 앱이 본 `FOO_ABI_VERSION` 과 `sizeof(struct foo_stats)` 를 넘긴다. `foo_abi_check()` 는 **라이브러리 안에** 컴파일된다 → 라이브러리가 본 값과 비교하면 된다. 크기까지 비교하면 버전 숫자 올리는 것을 잊은 경우도 잡힌다.

</details>

## 5. 풀이

### Part A — 이름과 검색 경로

#### 맥: install name 은 `-o` 경로가 기본값이다

`demo_mac.txt` §2 (실제 출력). 상대 경로로 `-o build/libfoo.dylib` 하면 install name 도 상대 경로가 되고, 그 상대 경로는 **현재 작업 폴더(CWD) 기준**으로 해석된다:

```text
$ otool -D build/libfoo.dylib
build/libfoo.dylib:
build/libfoo.dylib
$ otool -L app_rel | grep libfoo
	build/libfoo.dylib (compatibility version 0.0.0, current version 0.0.0)
$ cd elsewhere && ./app_rel; echo "rc=$?"; cd ..
dyld[15918]: Library not loaded: build/libfoo.dylib
  Referenced from: <83659A0D-5004-32B1-987B-FA132D50D13D> /private/tmp/TOOL-M06_demo_mac/src/elsewhere/app_rel
  Reason: tried: 'build/libfoo.dylib' (no such file), '/System/Volumes/Preboot/Cryptexes/OSbuild/libfoo.dylib' (no such file), 'build/libfoo.dylib' (no such file)
rc=134
```

starter 는 check.sh 가 절대 경로 `-o` 를 써서 install name 이 `/tmp/TOOL-M06_work/starter/mac/build/libfoo.dylib` 가 됐다. 빌드 폴더가 있는 동안에는 설치된 번들도 **빌드 폴더의 dylib 를** 로드해서 돈다(PASS) — 번들 안 `lib/` 는 쓰이지도 않았다. 이게 "CI 에서는 되는데 고객 장비에서 안 됨" 의 정체다. dyld 는 abort 하므로 종료 코드 134(SIGABRT).

고친 링크 (`demo_mac.txt` §3):

```text
$ otool -l pkg/lib/libfoo.dylib | grep -A5 LC_ID_DYLIB
          cmd LC_ID_DYLIB
         name @rpath/libfoo.dylib (offset 24)
      current version 1.2.0
compatibility version 1.0.0
$ otool -l pkg/bin/app | grep -A3 LC_RPATH
          cmd LC_RPATH
         path @loader_path/../lib (offset 12)
$ DYLD_PRINT_SEARCHING=1 ./moved_pkg/bin/app 2>&1 | grep -A4 'find path "@rpath'
dyld[15957]: find path "@rpath/libfoo.dylib"
dyld[15957]:   LC_RPATH "@loader_path/../lib" from "/private/tmp/TOOL-M06_demo_mac/src/moved_pkg/bin/app"
dyld[15957]:   possible path(@path expansion): "/private/tmp/TOOL-M06_demo_mac/src/moved_pkg/bin/../lib/libfoo.dylib"
dyld[15957]:   found: dylib-from-disk: "/private/tmp/TOOL-M06_demo_mac/src/moved_pkg/bin/../lib/libfoo.dylib"
```

`DYLD_PRINT_SEARCHING=1` 이 이 맥의 dyld 에서 가장 쓸모 있는 진단이다(`DYLD_PRINT_RPATHS` 는 아무것도 안 찍었다). 세 가지 `@` 토큰:

| 토큰 | 뜻 | 언제 |
|---|---|---|
| `@executable_path` | 메인 실행 파일의 폴더 | 앱 번들 안에서 앱만 라이브러리를 쓸 때 |
| `@loader_path` | **이 로드 명령을 가진 이미지**(앱이든 dylib 든)의 폴더 | 플러그인·프레임워크처럼 로더가 바뀌어도 상대 위치가 같을 때 |
| `@rpath` | "로드 체인의 `LC_RPATH` 목록을 차례로 대입해 봐라" | 라이브러리 install name 은 거의 항상 이것 |

라이브러리는 자기가 어디 설치될지 모르므로 `@rpath/libfoo.dylib` 라고만 말하고, **앱**이 `LC_RPATH` 로 "내 기준 `../lib`" 라고 답한다. 간접 의존도 메인 실행 파일의 `LC_RPATH` 를 쓴다 — `demo_mac.txt` §10: `LC_RPATH` 가 0개인 `liba.dylib` 의 의존 `@rpath/libb.dylib` 를 앱의 `@loader_path/lib` 로 찾았다.

**버전 두 개.** `-current_version 1.2.0 -compatibility_version 1.0` 은 `LC_ID_DYLIB` 에 들어가고, 앱 링크 때 앱의 `LC_LOAD_DYLIB` 에 복사된다. 그런데 `demo_mac.txt` §5 에서 compatibility 2.0.0 을 기록한 앱에 compatibility 1.0.0 / current 1.5.0 라이브러리를 넣어도 **그냥 실행됐다**(rc=0, `-mmacosx-version-min=11.0` 로 빌드한 앱도 같음). 이 dyld(macOS 27.2)는 그 값을 강제하지 않는다. 예전 dyld 는 `Incompatible library version` 으로 거부했다고 알려져 있다(확인 권장). 그러니 버전 필드는 기록·진단용으로 쓰고, 안전장치는 Part C 의 방법으로 만든다.

**DYLD_LIBRARY_PATH 와 SIP.** `DYLD_LIBRARY_PATH` 는 install name 의 **마지막 이름(leaf)** 으로 먼저 찾는다 — `@rpath` 보다도 먼저다(check.sh Part C 의 끼워 넣기가 이것; 이동된 번들도 `DYLD_LIBRARY_PATH=dropin` 이면 v2 를 집었고, `DYLD_FALLBACK_LIBRARY_PATH` 로는 번들의 v1 을 집었다). SIP 는 보호된 시스템 바이너리를 실행할 때 `DYLD_*` 를 지운다고 문서화돼 있는데, 이 맥은 `System Integrity Protection status: enabled (Apple Internal).` 이고 실측으로 `/bin/sh` 안에서도 `DYLD_LIBRARY_PATH` 가 남아 있었고 `/usr/bin/true` 에도 `DYLD_PRINT_LIBRARIES` 가 동작했다(`demo_mac.txt` §4). 일반 고객용 맥에서는 다를 수 있다 — 스크립트로 감싼 실행에서 `DYLD_*` 가 사라지는 증상이면 SIP 를 의심(확인 권장). 하드닝 런타임(hardened runtime)으로 서명된 앱도 `DYLD_*` 를 무시한다(확인 권장).

#### ELF: 세 개의 이름

```svg
<svg viewBox="0 0 740 300" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <defs>
    <marker id="m06a2" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto">
      <path d="M0 0 L10 5 L0 10 z" fill="currentColor"/>
    </marker>
  </defs>
  <rect x="10" y="20" width="210" height="58" rx="6" class="f-mut" stroke="currentColor"/>
  <text x="20" y="42" fill="currentColor" font-family="monospace">libfoo.so</text>
  <text x="20" y="64" fill="currentColor" font-size="12">링커 이름 (-dev 패키지)</text>
  <rect x="265" y="20" width="210" height="58" rx="6" class="f-acc" stroke="currentColor"/>
  <text x="275" y="42" fill="currentColor" font-family="monospace">libfoo.so.1</text>
  <text x="275" y="64" fill="currentColor" font-size="12">SONAME (ldconfig 가 만듦)</text>
  <rect x="520" y="20" width="210" height="58" rx="6" class="f-ok" stroke="currentColor"/>
  <text x="530" y="42" fill="currentColor" font-family="monospace">libfoo.so.1.2.0</text>
  <text x="530" y="64" fill="currentColor" font-size="12">실제 파일 (real name)</text>
  <path d="M220 49 L263 49" stroke="currentColor" stroke-width="2" marker-end="url(#m06a2)"/>
  <path d="M475 49 L518 49" stroke="currentColor" stroke-width="2" marker-end="url(#m06a2)"/>
  <rect x="10" y="120" width="330" height="70" rx="6" fill="none" stroke="currentColor"/>
  <text x="20" y="142" fill="currentColor" font-weight="bold">빌드할 때: gcc ... -lfoo</text>
  <text x="20" y="162" fill="currentColor" font-size="12">① -L 경로에서 libfoo.so 를 연다</text>
  <text x="20" y="180" fill="currentColor" font-size="12">② 그 안의 SONAME 을 앱 NEEDED 에 적는다</text>
  <rect x="400" y="120" width="330" height="70" rx="6" fill="none" stroke="currentColor"/>
  <text x="410" y="142" fill="currentColor" font-weight="bold">실행할 때: ld.so</text>
  <text x="410" y="162" fill="currentColor" font-size="12">③ NEEDED libfoo.so.1 을 검색 경로에서 찾는다</text>
  <text x="410" y="180" fill="currentColor" font-size="12">④ 링크를 따라 1.2.0 파일을 매핑</text>
  <path d="M120 120 L120 80" stroke="currentColor" stroke-width="1.5" stroke-dasharray="5 3" marker-end="url(#m06a2)"/>
  <path d="M370 155 L398 155" stroke="currentColor" stroke-width="2" marker-end="url(#m06a2)"/>
  <path d="M480 120 L400 80" stroke="currentColor" stroke-width="1.5" stroke-dasharray="5 3" marker-end="url(#m06a2)"/>
  <text x="10" y="222" fill="currentColor" font-size="12">SONAME 이 없으면 ②에서 "libfoo.so" 가 적힌다 → 런타임 패키지(링커 이름 없음)에서 실패.</text>
  <text x="10" y="244" fill="currentColor" font-size="12">1.2.0 → 1.3.0 (ABI 호환): 실제 파일만 바꾸고 libfoo.so.1 링크를 옮긴다. 앱은 다시 링크하지 않는다.</text>
  <text x="10" y="266" fill="currentColor" font-size="12">2.0.0 (ABI 깨짐): SONAME 을 libfoo.so.2 로 → 옛 앱은 계속 libfoo.so.1 을 찾는다 (연습 2).</text>
  <text x="10" y="288" fill="currentColor" font-size="12">맥 대응: SONAME≈install name, NEEDED≈LC_LOAD_DYLIB, RUNPATH≈LC_RPATH, $ORIGIN≈@loader_path</text>
</svg>
```

`demo_vm.txt` §2 (VM 실제 출력):

```text
$ ln -s libfoo.so.1.2.0 build/libfoo.so && /usr/sbin/ldconfig -n build && ls -l build ...
libfoo.so -> libfoo.so.1.2.0
libfoo.so.1 -> libfoo.so.1.2.0
libfoo.so.1.2.0
$ readelf -dW build/libfoo.so.1.2.0 | grep -E "SONAME|NEEDED"
 0x0000000000000001 (NEEDED)             Shared library: [libc.so.6]
 0x000000000000000e (SONAME)             Library soname: [libfoo.so.1]
$ readelf -dW build/app | grep -E "NEEDED|RUNPATH|RPATH"
 0x0000000000000001 (NEEDED)             Shared library: [libfoo.so.1]
 0x0000000000000001 (NEEDED)             Shared library: [libc.so.6]
 0x0000000000000001 (NEEDED)             Shared library: [ld-linux-aarch64.so.1]
 0x000000000000001d (RUNPATH)            Library runpath: [$ORIGIN]
$ cd build && LD_DEBUG=libs ./app 2>&1 | grep -E "find library=libfoo|search path|trying file|calling init: .*libfoo"
	find library=libfoo.so.1 [0]; searching
	 search path=/tmp/TOOL-M06_demo_vm/src/build		(RUNPATH from file ./app)
	  trying file=/tmp/TOOL-M06_demo_vm/src/build/libfoo.so.1
	calling init: /tmp/TOOL-M06_demo_vm/src/build/libfoo.so.1
```

SONAME 이 없을 때 (`demo_vm.txt` §3): `-lfoo` 로 링크하면 `NEEDED [libfoo.so]`, 파일 경로로 링크하면 **그 경로 문자열 그대로** `NEEDED [nosoname/libfoo.so.1.2.0]` 가 적혔다 — 후자는 CWD 상대 경로라 더 나쁘다.

**ld.so 검색 순서** (glibc `man ld.so` 기준, 아래 두 줄은 실측):

1. `DT_RPATH` — 단, `DT_RUNPATH` 가 있으면 무시
2. `LD_LIBRARY_PATH` (setuid 프로그램은 무시)
3. `DT_RUNPATH` — **그 파일의 직접 의존에만** 적용
4. `/etc/ld.so.cache` (ldconfig 가 만든 캐시)
5. 기본 경로 (`/lib`, `/usr/lib`, aarch64 Ubuntu 는 `/usr/lib/aarch64-linux-gnu` 등 멀티아치 경로)

`demo_vm.txt` §4: 같은 디렉터리를 `--enable-new-dtags`(RUNPATH, Ubuntu gcc 기본값 — check.sh 의 `-rpath` 가 RUNPATH 로 나온 이유)와 `--disable-new-dtags`(RPATH)로 넣고 `LD_LIBRARY_PATH` 에 다른 복사본을 두었더니: RUNPATH 앱은 `b (LD_LIBRARY_PATH dir)`, RPATH 앱은 `a (rpath dir)`. §4b: 앱만 RUNPATH 를 갖고 `liba.so → libb.so` 가 같은 폴더에 있을 때, RUNPATH 앱은 `libb.so: cannot open shared object file`(rc=127), RPATH 앱은 `a()=3`. 맥의 `@rpath` 는 로드 체인의 rpath 를 쓰므로 같은 구조가 성공했다(§10). 즉 **ELF 에서 RUNPATH 는 전이되지 않는다** — 플러그인이 또 다른 `.so` 를 쓰면 플러그인 자신에게도 `$ORIGIN` RUNPATH 를 줘야 한다.

check.sh 가 런타임 패키지에 개발용 링크를 일부러 빼는 것은 실제 배포판 패키징(`libfoo1` vs `libfoo-dev`)과 같다. starter 앱은 `libfoo.so` 를 찾다가 죽었다.

### Part B — 가시성과 끼어들기

#### 숨기지 않으면 무엇이 보이나

`demo_mac.txt` §6 — 같은 소스를 `-fvisibility=hidden` 없이 빌드하면 내부 헬퍼까지 8개가 나간다. 숨기고 `FOO_API` 를 붙이면:

```text
$ nm -m moved_pkg/lib/libfoo.dylib | grep -E "round_div|ring_put|foo_open"
000000000000047c (__TEXT,__text) external _foo_open
000000000000056c (__TEXT,__text) non-external (was a private external) _ring_put
0000000000000544 (__TEXT,__text) non-external (was a private external) _round_div
$ dyld_info -exports moved_pkg/lib/libfoo.dylib
moved_pkg/lib/libfoo.dylib [arm64]:
    -exports:
        offset      symbol
        0x00000410  _foo_abi_check
        0x00000534  _foo_close
        0x00000520  _foo_get_stats
        0x0000047C  _foo_open
        0x000004CC  _foo_push
        0x00000538  _foo_version
```

"was a private external" — 컴파일러가 `.o` 에 hidden(Mach-O 용어로 private extern)으로 표시했고, ld64 가 최종 이미지에서 지역 심볼로 내렸다는 뜻. 링크 단계에서는 두 `.o` 사이에서 여전히 전역이라 `foo.c → round_div()` 호출이 연결된다 — "파일 사이에서는 보이고, 모듈 밖에서는 안 보임" 이 hidden 의 정확한 의미다. ELF 도 같다: `readelf -s foo.o` 에서 `foo_open` 은 `GLOBAL DEFAULT`, 숨긴 쪽은 `.so` 의 `nm -D` 에서 사라진다. 크기도 줄었다: `size` 로 `text` 3015 → 2489 바이트 (`.dynsym`/`.dynstr`/해시 표가 작아짐, `demo_vm.txt` §6).

#### 끼어들기: 리눅스에서만 결과가 바뀐 이유

```svg
<svg viewBox="0 0 740 300" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <defs>
    <marker id="m06a3" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto">
      <path d="M0 0 L10 5 L0 10 z" fill="currentColor"/>
    </marker>
  </defs>
  <text x="10" y="18" fill="currentColor" font-weight="bold">ELF, 기본 가시성 (starter)</text>
  <rect x="10" y="28" width="170" height="54" rx="6" class="f-mut" stroke="currentColor"/>
  <text x="18" y="48" fill="currentColor" font-family="monospace" font-size="12">libfoo: foo_push</text>
  <text x="18" y="68" fill="currentColor" font-family="monospace" font-size="12">bl round_div@plt</text>
  <path d="M180 55 L228 55" stroke="currentColor" stroke-width="2" marker-end="url(#m06a3)"/>
  <rect x="232" y="28" width="200" height="54" rx="6" fill="none" stroke="currentColor"/>
  <text x="240" y="48" fill="currentColor" font-size="12">PLT/GOT → ld.so 가 전역 범위</text>
  <text x="240" y="68" fill="currentColor" font-size="12">에서 이름으로 찾음: app 이 먼저</text>
  <path d="M432 55 L480 55" stroke="currentColor" stroke-width="2" marker-end="url(#m06a3)"/>
  <rect x="484" y="28" width="246" height="54" rx="6" class="f-warn" stroke="currentColor"/>
  <text x="492" y="48" fill="currentColor" font-family="monospace" font-size="12">app 의 round_div (버림)</text>
  <text x="492" y="68" fill="currentColor" font-family="monospace" font-size="12">avg: 10 11 13 15 20 27 22 ✗</text>
  <text x="10" y="118" fill="currentColor" font-weight="bold">ELF, -fvisibility=hidden (solution)</text>
  <rect x="10" y="128" width="170" height="54" rx="6" class="f-acc" stroke="currentColor"/>
  <text x="18" y="148" fill="currentColor" font-family="monospace" font-size="12">libfoo: foo_push</text>
  <text x="18" y="168" fill="currentColor" font-family="monospace" font-size="12">bl round_div</text>
  <path d="M180 155 L480 155" stroke="currentColor" stroke-width="2" marker-end="url(#m06a3)"/>
  <text x="200" y="146" fill="currentColor" font-size="12">직접 호출, .dynsym 에 없음</text>
  <rect x="484" y="128" width="246" height="54" rx="6" class="f-ok" stroke="currentColor"/>
  <text x="492" y="148" fill="currentColor" font-family="monospace" font-size="12">libfoo 의 round_div (반올림)</text>
  <text x="492" y="168" fill="currentColor" font-family="monospace" font-size="12">avg: 10 12 13 16 21 28 22 ✓</text>
  <text x="10" y="218" fill="currentColor" font-weight="bold">Mach-O 2단계 이름 공간 (two-level namespace, 기본)</text>
  <text x="10" y="240" fill="currentColor" font-size="12">심볼 = (이미지, 이름) 쌍. dylib 안 자기 함수 호출은 ld64 가 직접 bl → starter 도 맥에서는 ✓.</text>
  <text x="10" y="262" fill="currentColor" font-size="12">-flat_namespace 로 링크한 dylib 만 stub 을 거쳐 app 의 같은 이름을 집는다 (실측 foo(2)=201).</text>
  <text x="10" y="284" fill="currentColor" font-size="12">막는 방법(ELF): hidden · -fno-semantic-interposition · -Bsymbolic · version script</text>
</svg>
```

ELF 의 규칙: 공유 라이브러리 안에서 **default 가시성 전역 함수**를 부르면, 컴파일러(`-fPIC`)는 "실행 시 다른 모듈의 같은 이름이 이길 수 있다" 고 가정하고 PLT 를 거쳐 부른다. ld.so 는 이름을 **전역 범위(global scope)** 에서 찾는데, 순서는 실행 파일이 먼저, 그다음 로드 순서의 라이브러리들이다. 앱이 같은 이름을 정의했고 그 이름이 라이브러리 `.dynsym` 에 있으니, 링커는 앱의 `round_div` 를 앱의 `.dynsym` 에 내보낸다(`nm -D ip/app` → `T helper`) — 그리고 라이브러리의 호출이 거기로 간다. `demo_vm.txt` §5 의 최소 재현:

```text
lib built default: foo(2)=201 helper(2)=200 | foo calls: bl	480 <helper@plt>
lib built hidden: foo(2)=7 helper(2)=200 | foo calls: bl	580 <helper>
lib built nosemint: foo(2)=7 helper(2)=200 | foo calls: bl	5a0 <helper>
lib built bsymbolic: foo(2)=7 helper(2)=200 | foo calls: bl	5a0 <helper>
```

맥(`demo_mac.txt` §7): 기본 링크는 `bl _helper`(직접) → `foo(2)=7`, 라이브러리를 `-Wl,-flat_namespace` 로 링크하면 `foo(2)=201`. 그래서 starter 는 **맥에서는 맞고 리눅스에서 틀린다**.

같은 오브젝트를 정적으로 링크하면? `demo_vm.txt` §9: `multiple definition of 'round_div'` 링크 에러. 정적 링크에서는 충돌이 드러나고, 동적 링크에서는 조용히 한쪽이 이긴다. 가시성은 정적 링크의 이름 충돌을 해결하지 못한다(hidden 도 `.o` 사이에서는 전역) — 진짜 해결은 내부 헬퍼에 접두사(`foo__round_div`)를 붙이는 것이고, 가시성은 그 위에 거는 두 번째 안전장치다.

#### -fPIC 은 어디서 실제로 필요한가

`-fPIC` 은 "이 코드가 어느 주소에 놓여도 동작하게, 그리고 다른 모듈의 심볼은 GOT 로 간접 접근하게" 생성하라는 뜻이다. 실측:

| 환경 | `-fPIC` 없이 `.so` 링크 | 근거 |
|---|---|---|
| arm64 맥 (Apple clang 21) | 문제 자체가 없음: `-fno-pic` 을 줘도 `-mrelocation-model pic`, `.o` 바이트 동일 | `demo_mac.txt` §1 |
| aarch64 Ubuntu gcc 15 (기본 `--enable-default-pie`) | 기본 `-fPIE` `.o` → 링크 성공 (외부 데이터는 이미 `R_AARCH64_ADR_GOT_PAGE`) | `demo_vm.txt` §1 |
| aarch64, `-fno-pic` + 외부 변수 | **실패**: `relocation R_AARCH64_ADR_PREL_PG_HI21 against symbol 'ext' which may bind externally can not be used when making a shared object; recompile with -fPIC` | §1 |
| aarch64, `-fno-pic` + 자기 전역 변수 | 성공 (ADRP 는 PC 상대 → 그대로 동작) — 하지만 그 변수는 끼어들기/복사 재배치 의미론이 깨질 수 있음 (확인 권장) | §1 |
| x86_64 (VM 크로스 gcc), `-fno-pic` | **실패**: `relocation R_X86_64_PC32 against symbol 'counter' can not be used when making a shared object; recompile with -fPIC` | §1 |
| x86_64 `-fPIC` | 성공 | §1 |

즉 "PIC 없이 `.so` 를 만들면 에러" 는 타깃과 기본 PIE 설정에 따라 다르다. 확실한 규칙: **공유 라이브러리에 들어갈 오브젝트는 항상 `-fPIC`** (정적 라이브러리를 나중에 `.so` 에 넣을 계획이면 그 `.a` 도). `LIB_CFLAGS` 의 `-fPIC` 은 맥에서는 아무것도 바꾸지 않지만 같은 값을 VM gcc 도 쓰므로 유지한다.

### Part C — ABI 깨짐: 이름이 같으면 로더는 모른다

```svg
<svg viewBox="0 0 740 250" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <text x="10" y="18" fill="currentColor" font-weight="bold">앱(1.x 헤더로 컴파일)이 보는 메모리: box = { struct foo_stats st; uint32_t guard; }</text>
  <text x="140" y="44" fill="currentColor" font-family="monospace" font-size="12">+0</text>
  <text x="260" y="44" fill="currentColor" font-family="monospace" font-size="12">+4</text>
  <text x="380" y="44" fill="currentColor" font-family="monospace" font-size="12">+8</text>
  <text x="500" y="44" fill="currentColor" font-family="monospace" font-size="12">+12</text>
  <text x="10" y="74" fill="currentColor" font-size="12">앱의 해석</text>
  <rect x="130" y="52" width="120" height="34" class="f-acc" stroke="currentColor"/>
  <rect x="250" y="52" width="120" height="34" class="f-acc" stroke="currentColor"/>
  <rect x="370" y="52" width="120" height="34" class="f-acc" stroke="currentColor"/>
  <rect x="490" y="52" width="120" height="34" class="f-mut" stroke="currentColor"/>
  <text x="140" y="74" fill="currentColor" font-family="monospace" font-size="12">count</text>
  <text x="260" y="74" fill="currentColor" font-family="monospace" font-size="12">min</text>
  <text x="380" y="74" fill="currentColor" font-family="monospace" font-size="12">max</text>
  <text x="500" y="74" fill="currentColor" font-family="monospace" font-size="12">guard</text>
  <text x="10" y="124" fill="currentColor" font-size="12">v2 lib 가 씀</text>
  <rect x="130" y="102" width="120" height="34" class="f-ok" stroke="currentColor"/>
  <rect x="250" y="102" width="120" height="34" class="f-warn" stroke="currentColor"/>
  <rect x="370" y="102" width="120" height="34" class="f-warn" stroke="currentColor"/>
  <rect x="490" y="102" width="120" height="34" class="f-warn" stroke="currentColor"/>
  <text x="140" y="124" fill="currentColor" font-family="monospace" font-size="12">count=7</text>
  <text x="260" y="124" fill="currentColor" font-family="monospace" font-size="12">avg=22</text>
  <text x="380" y="124" fill="currentColor" font-family="monospace" font-size="12">min=-5</text>
  <text x="500" y="124" fill="currentColor" font-family="monospace" font-size="12">max=40</text>
  <text x="10" y="166" fill="currentColor" font-family="monospace" font-size="12">결과: stats: count=7 min=22 max=-5 guard=CLOBBERED   (rc=0, 경고 없음)</text>
  <text x="10" y="192" fill="currentColor" font-size="12">로더가 확인하는 것은 이름과 심볼 존재뿐. foo_get_stats 라는 이름은 그대로라 바인딩은 성공한다.</text>
  <text x="10" y="214" fill="currentColor" font-size="12">sizeof(struct foo_stats): 앱 12 B, v2 16 B → *out = f-&gt;st 가 guard 를 덮어씀.</text>
  <text x="10" y="236" fill="currentColor" font-size="12">foo_init() 은 앱 안(inline), foo_abi_check() 는 lib 안 → 두 시점의 값을 비교.</text>
</svg>
```

해결은 세 층이다:

1. **런타임 확인(handshake)** — 이 문제의 Part C. `foo_init()` 이 헤더의 `static inline` 이라 앱이 본 `FOO_ABI_VERSION` / `sizeof` 가 앱 바이너리에 상수로 박히고, 라이브러리는 자기 값과 비교한다. solution 실제 출력:
   ```text
   PASS C old app + v2 lib (DYLD_LIBRARY_PATH drop-in) refused, rc=3: 'libfoo 2.0.0: ABI mismatch: app abi 1 (stats 12 B), lib abi 2 (stats 16 B)'
   PASS C v2 app + v2 lib still accepted: 'stats: count=7 min=-5 max=40 avg=22 guard=ok'
   ```
   크기 비교는 "버전 숫자 올리는 것을 잊음" 도 잡는다. 플러그인 API(예: 리눅스 커널 모듈의 vermagic, GStreamer 플러그인의 버전 필드)들이 비슷한 확인을 한다.
2. **이름 올리기** — ABI 가 깨지면 SONAME `libfoo.so.2` / install name `@rpath/libfoo.2.dylib`. 그러면 옛 앱은 옛 파일을 계속 찾고, 두 버전이 한 폴더에 공존한다(연습 2). 이게 정석이고 1 은 실수를 잡는 그물이다.
3. **ABI 를 작게 설계** — 불투명 타입(`typedef struct foo foo_t;`, 앱은 `sizeof(struct foo)` 를 모름 → 내부 필드는 마음대로 바꿔도 됨), 구조체는 끝에만 필드 추가 + 크기 필드(`size_t size` 를 첫 멤버로 두는 Win32 `cbSize` 패턴), 공개 구조체 대신 getter. 이 라이브러리의 `struct foo` 는 불투명이라 안전하고, 공개된 `struct foo_stats` 만 문제가 됐다.

ABI 를 깨는 변경 목록: 공개 구조체의 필드 추가/순서/타입, enum 값 변경, 함수 시그니처(인자 타입·개수), 공개 심볼 삭제/이름 변경, 헤더의 `static inline`/매크로가 쓰는 내부 상수, 호출 규약(`-mfloat-abi`, 구조체 반환). CI 에서는 `abidiff`(libabigail) 같은 도구로 두 `.so` 의 ABI 를 비교한다 — 이 맥과 VM 에는 설치돼 있지 않아 실행하지 않았다.

## 6. 정답 코드

빌드 설정 — 다섯 값:

<!-- code: solution/build.conf diff=starter/build.conf -->

공개 헤더 — `FOO_API` 한 줄:

<!-- code: solution/foo.h diff=starter/foo.h -->

ABI 확인:

<!-- code: solution/foo.c diff=starter/foo.c -->

```text
$ bash check.sh solution
--- Mac (Apple ld64 + dyld) ---
PASS A install name (otool -D) is @rpath-relative: '@rpath/libfoo.dylib'
PASS A app: LC_LOAD_DYLIB=@rpath name, LC_RPATH='@loader_path/../lib' (want @loader_path/../lib), no build path
PASS A installed bundle runs (build dir still present): 'libfoo 1.2.0 (app abi 1)'
PASS A bundle moved + build dir deleted -> still runs: 'libfoo 1.2.0 (app abi 1)'
PASS A versions recorded in app: 'compatibility version 1.0.0, current version 1.2.0'
PASS B exported set (nm -gU) == API only: 'foo_abi_check foo_close foo_get_stats foo_open foo_push foo_version'
PASS C old app + v2 lib (DYLD_LIBRARY_PATH drop-in) refused, rc=3: 'libfoo 2.0.0: ABI mismatch: app abi 1 (stats 12 B), lib abi 2 (stats 16 B)'
PASS C v2 app + v2 lib still accepted: 'stats: count=7 min=-5 max=40 avg=22 guard=ok'
--- Lima VM elinux (gcc 15.2 / GNU ld 2.46 / glibc ld.so) ---
PASS V SONAME='libfoo.so.1' (want libfoo.so.1) + ldconfig -n made link: libfoo.so.1 -> libfoo.so.1.2.0
PASS V app NEEDED='libfoo.so.1' (want the SONAME, not the dev link libfoo.so)
PASS V app RUNPATH='$ORIGIN/../lib' RPATH='' (want RUNPATH $ORIGIN/../lib)
PASS V runtime bundle (no libfoo.so dev link) moved + build deleted -> loads: 'libfoo 1.2.0 (app abi 1)'
PASS V lib's internal round_div not interposed by the app's: 'avg: 10 12 13 16 21 28 22'
PASS V exported set (nm -D --defined-only) == API only: 'foo_abi_check foo_close foo_get_stats foo_open foo_push foo_version'
PASS V old app + v2 lib (same file name) refused, rc=3: 'libfoo 2.0.0: ABI mismatch: app abi 1 (stats 12 B), lib abi 2 (stats 16 B)'
15/15 passed
```

왜 각 줄이 필요한가:

| 값 | 없으면 |
|---|---|
| `-install_name @rpath/libfoo.dylib` | install name = `-o` 경로 → 빌드 폴더에 묶임 |
| `-Wl,-rpath,@loader_path/../lib` | `@rpath` 를 대입할 후보가 0개 → 어디서도 못 찾음 |
| `-compatibility_version 1.0 -current_version 1.2.0` | `0.0.0` 기록. 강제되지는 않지만(§5) 진단·패키징 도구가 읽는다 |
| `-Wl,-soname,libfoo.so.1` | NEEDED = `libfoo.so` → 개발용 링크 없는 장비에서 실패, ABI 메이저 표시 불가 |
| `-Wl,-rpath,'$ORIGIN/../lib'` | 시스템 경로에만 설치 가능 (또는 빌드 경로 RUNPATH 가 보안·이식성 문제) |
| `-fvisibility=hidden` + `FOO_API` | 내부 헬퍼 공개 → 끼어들기, PLT 경유 호출, 앱이 내부 함수에 의존 → 영영 못 바꿈 |
| `foo_abi_check` 비교 | 같은 이름의 v2 가 조용히 메모리를 덮어씀 |

## 7. 자주 하는 실수

1. **`-install_name` 없이 배포** — install name 이 빌드 머신 경로. 개발자 맥에서는 그 경로가 있으니 버그가 안 보인다. 배포 전 `otool -L` 로 `/Users/`, `/tmp/`, 상대 경로가 없는지 CI 에서 grep.
2. **`-rpath` 를 빌드 폴더 절대 경로로** — CMake 의 빌드 트리 RPATH 가 바로 이것(설치 시 교체됨 — CMake 세부는 TOOL-M04/M05, 확인 권장). 그대로 설치되면 장비에서 안 돌고, 남의 쓰기 가능한 경로를 가리키면 라이브러리 심기(hijack) 보안 문제.
3. **`$ORIGIN` 을 큰따옴표로** — `"-Wl,-rpath,$ORIGIN/../lib"` 는 셸이 빈 문자열로 펼쳐 `/../lib` 가 된다. 작은따옴표, Makefile 이면 `$$ORIGIN`.
4. **`install_name_tool` 명령 하나에 여러 옵션** — 하나라도 실패하면 **전부 적용 안 됨**. 실측: `-change ... -add_rpath @loader_path/../lib` 에서 rpath 가 이미 있으면 `option "-add_rpath @loader_path/../lib" would duplicate path, file already has LC_RPATH for: @loader_path/../lib`, rc=1, `-change` 도 반영 안 됨(`demo_mac.txt` §8).
5. **`-fvisibility=hidden` 만 켜고 `FOO_API` 를 안 붙임** — 내보낸 심볼이 0개 → 앱 링크가 `Undefined symbols` 로 실패. 반대로 `FOO_API` 만 붙이고 `-fvisibility=hidden` 을 안 켜면 아무 효과 없음(전부 기본이 default).
6. **"맥에서 잘 되니까"** — 2단계 이름 공간 덕분에 맥에서는 끼어들기가 안 일어난다. 리눅스 타깃은 같은 코드에서 `avg` 가 틀렸다. 크로스 플랫폼 라이브러리는 리눅스에서도 테스트.
7. **ABI 를 깨고 SONAME 을 안 올림** — 링크도 로드도 성공하고 값만 틀린다. 공개 구조체를 바꿨으면 SONAME/install name 메이저를 올리고, 헤더에 `FOO_ABI_VERSION` + 크기 확인을 둔다.
8. **compatibility_version 이 지켜 줄 거라 믿음** — 이 macOS 27.2 dyld 는 2.0.0 을 요구하는 앱에 1.0.0 라이브러리를 그냥 붙였다(§5).

## 8. 면접 Q&A

<details>
<summary>Q1. -fPIC 은 무엇을 바꾸고, 빠뜨리면 어떻게 되나요?</summary>

위치 독립 코드(position-independent code): 코드가 어느 주소에 매핑되어도 고칠 필요가 없도록 PC 상대 주소를 쓰고, **선점 가능한(preemptible) 심볼** — 다른 모듈에 있거나 다른 모듈이 대체할 수 있는 심볼 — 은 GOT(Global Offset Table)에서 주소를 읽고, 함수는 PLT 를 거쳐 부른다. 그래야 `.text` 페이지를 수정 없이 여러 프로세스가 공유한다. 빠뜨리면 링커가 `.text` 에 들어갈 수 없는 재배치를 만나 거부한다 — 실측: x86_64 `R_X86_64_PC32 ... recompile with -fPIC`, aarch64 는 외부 변수에서만 `R_AARCH64_ADR_PREL_PG_HI21 ... which may bind externally`. Ubuntu gcc 는 기본이 `-fPIE` 라 aarch64 에서는 대부분 통과해 버리고, arm64 맥은 항상 PIC(`-fno-pic` 무시)라 문제 자체가 없다. 결론: `.so` 용 오브젝트는 항상 `-fPIC`.

</details>

<details>
<summary>Q2. 맥에서 install name, @rpath, @loader_path, @executable_path 를 설명해 보세요.</summary>

install name 은 dylib 의 `LC_ID_DYLIB` 에 든 "나는 이 이름으로 찾아져야 한다" 는 문자열이고, 앱을 링크할 때 그대로 앱의 `LC_LOAD_DYLIB` 에 복사된다. 기본값은 `-o` 경로(상대면 CWD 기준 — 실측 `Library not loaded: build/libfoo.dylib`). `@rpath/x.dylib` 는 "로드 체인의 `LC_RPATH` 들을 차례로 대입" 하라는 뜻이고, `LC_RPATH` 에는 보통 `@loader_path/../lib`(로드 명령을 가진 이미지 기준) 또는 `@executable_path/../Frameworks`(메인 실행 파일 기준)를 쓴다. 플러그인 dylib 이 자기 의존을 찾을 때는 `@loader_path` 가 맞다(플러그인 위치 기준). 이미 빌드된 파일은 `install_name_tool -id / -change / -add_rpath / -delete_rpath` 로 고친다. 진단은 `otool -L`, `otool -l`, `DYLD_PRINT_SEARCHING=1`.

</details>

<details>
<summary>Q3. SONAME 이 뭐고, libfoo.so / libfoo.so.1 / libfoo.so.1.2.0 은 각각 누가 쓰나요?</summary>

SONAME 은 `.so` 의 `DT_SONAME` 항목으로 "ABI 호환 단위의 이름" 이다(`-Wl,-soname,libfoo.so.1`). 링커는 빌드 때 `-lfoo` 로 **링커 이름** `libfoo.so`(개발용 링크)를 열고, 그 안의 SONAME 을 앱의 `DT_NEEDED` 에 적는다. 실행 때 ld.so 는 SONAME 이름의 파일(`libfoo.so.1`, ldconfig 가 만든 링크)을 찾고, 그것이 **실제 파일** `libfoo.so.1.2.0` 을 가리킨다. 호환 업데이트(1.3.0)는 실제 파일과 링크만 바꾸면 앱 재링크 없이 적용되고, 호환 깨짐(2.0.0)은 SONAME `libfoo.so.2` 로 공존한다. SONAME 이 없으면 NEEDED 에 `libfoo.so`(또는 링크에 쓴 경로 문자열)가 들어간다 — 실측 둘 다.

</details>

<details>
<summary>Q4. 리눅스에서 라이브러리 검색 순서는? RPATH 와 RUNPATH 의 차이는?</summary>

glibc: `DT_RPATH`(RUNPATH 가 없을 때만) → `LD_LIBRARY_PATH` → `DT_RUNPATH` → `/etc/ld.so.cache` → 기본 경로. 차이 두 가지 (둘 다 실측): ① RPATH 는 `LD_LIBRARY_PATH` 보다 먼저라 사용자가 덮어쓸 수 없고, RUNPATH 는 덮어쓸 수 있다(`a (rpath dir)` vs `b (LD_LIBRARY_PATH dir)`). ② RUNPATH 는 그 파일의 **직접 의존**에만 쓰이고, RPATH 는 의존의 의존에도 쓰인다(`libb.so: cannot open shared object file` vs `a()=3`). 최신 툴체인은 `--enable-new-dtags` 가 기본이라 `-rpath` 가 RUNPATH 가 된다(이 VM 의 gcc 15). 맥 dyld 는 `DYLD_LIBRARY_PATH`(leaf 이름, `@rpath` 보다 먼저 — 실측) → install name/`@rpath` 확장 → `DYLD_FALLBACK_LIBRARY_PATH` 순으로 본다(세부 순서는 `man dyld` 확인 권장). 디버깅: `LD_DEBUG=libs`, `ldd`(신뢰할 수 없는 바이너리에는 쓰지 말 것 — ldd 가 프로그램 로더를 실행할 수 있다는 경고가 man 페이지에 있다), 맥 `DYLD_PRINT_SEARCHING`.

</details>

<details>
<summary>Q5. -fvisibility=hidden 을 왜 쓰나요? static 이나 version script 와 무엇이 다른가요?</summary>

① ABI 표면 축소: 내보낸 것은 누군가 쓰게 되고, 쓰면 영원히 못 바꾼다(starter 는 `round_div`, `ring_put` 까지 노출). ② 끼어들기 차단: ELF 에서 숨긴 함수는 다른 모듈이 대체할 수 없다. ③ 성능·크기: 컴파일러가 선점 불가능을 알기 때문에 PLT 대신 직접 `bl`(실측 `bl 580 <helper>`), 데이터는 GOT 대신 PC 상대 접근; `.dynsym` 이 작아져 로드(심볼 해시 검색)가 빨라지고 크기도 줄었다(text 3015 → 2489). `static` 은 **파일 하나** 안으로 가두지만, hidden 은 **모듈(라이브러리) 하나** 안으로 가둔다 — 여러 `.c` 가 공유하는 헬퍼에 필요. version script(`local: *;`)도 내보내기를 막지만 링크 단계에서 결정되므로 컴파일러는 여전히 GOT/PLT 코드를 만든다(링커가 일부 완화) — 그래서 보통 가시성으로 숨기고, version script 는 심볼 버전 표시에 쓴다(연습 3). 맥 대응: `-exported_symbols_list`.

</details>

<details>
<summary>Q6. 심볼 끼어들기(interposition)는 버그인가요, 기능인가요? 맥은 왜 다르죠?</summary>

ELF 의 의도된 기능이다: `LD_PRELOAD` 로 `malloc` 을 디버그용으로 바꾸거나(메모리 추적), 테스트에서 함수를 대체한다. 단점은 이번 문제처럼 **의도치 않은 같은 이름**이 라이브러리 내부 호출을 가로채는 것, 그리고 모든 내부 호출이 PLT 를 거쳐야 하는 비용. 막는 방법(실측 4종): hidden, `-fno-semantic-interposition`(컴파일러가 자기 정의를 그대로 쓴다고 가정), `-Wl,-Bsymbolic`(링커가 자기 정의에 바인딩), version script `local:`. 맥은 2단계 이름 공간이라 심볼을 (dylib, 이름) 쌍으로 묶어 같은 이름의 다른 이미지가 끼어들지 못한다 — 의도적 대체는 `DYLD_INSERT_LIBRARIES` + `__DATA,__interpose` 섹션 방식(이번에는 실행하지 않음, 확인 권장). `-flat_namespace` 로 링크하면 ELF 처럼 동작한다(실측 `foo(2)=201`).

</details>

<details>
<summary>Q7. C 라이브러리의 ABI 호환은 어떻게 관리하나요?</summary>

규칙: 호환 깨짐 → 메이저(SONAME/install name) 올림, 기능 추가(심볼 추가) → 마이너, 버그 수정 → 패치. 설계: 불투명 핸들, 공개 구조체는 끝에만 추가하고 크기 필드 포함, enum 값 고정, 예약 필드. 검출: 헤더 `static inline` 의 런타임 handshake(이번 Part C: 앱이 본 버전·크기를 라이브러리가 비교 → `ABI mismatch ... app abi 1 (stats 12 B), lib abi 2 (stats 16 B)`), 심볼 버전(`foo_open@@FOO_1.0` — 같은 SONAME 안에서 옛 시그니처와 새 시그니처를 같이 제공 가능, glibc 가 쓰는 방식), CI 에서 `abidiff`(libabigail, 미설치·미실행). 맥 `compatibility_version` 은 이 dyld 에서 강제되지 않았으므로 의존하지 않는다.

</details>

<details>
<summary>Q8. Cortex-M 펌웨어는 왜 공유 라이브러리를 안 쓰나요? 임베디드 리눅스에서는 언제 쓰나요?</summary>

MCU 는 보통 MMU 가 없어 프로세스마다 같은 물리 페이지를 공유하는 구조 자체가 없고, 코드는 Flash 에서 제자리 실행(XIP)되며 링커 스크립트로 주소가 고정된다(TOOL-M01). 동적 로더·심볼 표·GOT 간접 접근은 Flash/RAM/사이클만 먹는다. 그래서 정적 링크 + `--gc-sections`(TOOL-M13)가 기본이다. 예외적으로 런타임 로딩을 지원하는 RTOS 기능(예: Zephyr LLEXT, NuttX 모듈)이나 no-MMU 리눅스용 FDPIC 같은 방식이 있다(세부는 확인 권장). 임베디드 리눅스에서는 쓰는 이유가 분명하다: rootfs 용량(실측: 정적 hello 711,880 B vs 동적 70,448 B, 대신 `libc.so.6` 1,788,240 B 를 모두가 공유), 여러 프로세스의 `.text` 페이지 공유로 RAM 절약, 라이브러리만 교체하는 보안 업데이트, `dlopen` 플러그인(코덱·센서 드라이버·GStreamer 류). 주의: aarch64 GNU ld 는 기본 최대 페이지 크기 64 KB 로 정렬해 작은 `.so` 도 70,264 B 였고 `-Wl,-z,max-page-size=4096` 이면 8,824 B, `-s` 까지 6,128 B (`demo_vm.txt` §9) — 4 KB 페이지 커널 전용 이미지라면 의미 있는 절약(다른 페이지 크기 커널 호환성은 잃음).

</details>

## 9. 추가 연습

`practice/build.conf` 는 solution 에 세 가지를 더했다. `bash check.sh practice` 하나로 전부 검증한다.

### 연습 1 — 소스 없이 고치기: install_name_tool

벤더가 준 패키지(`bin/app`, `lib/libfoo.dylib`)가 빌드 머신 절대 경로를 가리키고(`-dynamiclib` 만, rpath 없음) 다시 링크할 수 없다. `fixup_legacy <app> <lib>` 함수를 작성해 옮겨도 돌게 하라. 서명은 유효해야 하고 라이브러리 id 도 `@rpath/libfoo.dylib` 로.

<details>
<summary>답 보기</summary>

```bash
fixup_legacy() {
	local old
	old=$(otool -D "$2" | sed -n 2p)
	install_name_tool -id @rpath/libfoo.dylib "$2"
	install_name_tool -change "$old" @rpath/libfoo.dylib -add_rpath @loader_path/../lib "$1"
}
```

앱의 `LC_LOAD_DYLIB` 문자열 = 라이브러리의 옛 id 이므로 `otool -D` 로 읽어 `-change` 에 쓴다. 서명: 이 맥에서 링커가 만든 ad-hoc 서명(`flags=0x20002(adhoc,linker-signed)`)은 `install_name_tool` 이 수정 후 **알아서 다시 서명**했다 — `codesign -v` 통과, 플래그 동일(`demo_mac.txt` §8). Apple Silicon 은 서명이 깨진 바이너리를 실행하지 않으므로, Developer ID 로 서명된 바이너리를 고쳤다면 `codesign --force -s <identity>` 로 다시 서명해야 한다(확인 권장). 리눅스 대응은 `patchelf --set-soname`, `--replace-needed libfoo.so libfoo.so.1`, `--set-rpath '$ORIGIN/../lib'` — VM 에서 실측 후 이동한 번들이 돌았다(`demo_vm.txt` §8).

</details>

### 연습 2 — 1.x 와 2.x 를 나란히 설치

ABI 2 라이브러리를 **새 이름**으로 내서, 한 번들의 `lib/` 에 v1 · v2 가 같이 있고 옛 앱과 새 앱이 각자 맞는 것을 쓰게 하라 (맥과 ELF 둘 다).

<details>
<summary>답 보기</summary>

메이저를 이름에 넣는다. 맥: v1 `-install_name @rpath/libfoo.1.dylib`, v2 `-install_name @rpath/libfoo.2.dylib -compatibility_version 2.0 -current_version 2.0.0`. ELF: v2 `-Wl,-soname,libfoo.so.2`. check.sh 실제 출력:

```text
PASS P2 Mac side by side: lib/{libfoo.1.dylib libfoo.2.dylib}, old app + new app both run
PASS P2 ELF side by side: lib/{libfoo.so.1 libfoo.so.1.2.0 libfoo.so.2 libfoo.so.2.0.0}, old app + new app both run
```

이 방식에서는 Part C 의 handshake 가 발동할 일이 없다 — 옛 앱은 애초에 `libfoo.so.1` 만 찾는다. 배포판이 `libssl3` 와 `libssl1.1` 을 같이 둘 수 있는 이유가 이것이다. 맥에서도 Part A 의 install name 규칙(`@rpath/...`)은 그대로라 A 체크는 통과한다.

</details>

### 연습 3 — 심볼 버전 붙이기 (version script)

ELF 라이브러리의 공개 심볼에 `FOO_1.0` 버전을 붙여라. 라이브러리는 `foo_open@@FOO_1.0` 을 내보내고, 앱은 `FOO_1.0` 을 요구해야 한다. 내보낸 집합은 여전히 API 6개.

<details>
<summary>답 보기</summary>

`practice/foo.map`:

```text
FOO_1.0 {
	global:
		foo_abi_check; foo_open; foo_push; foo_get_stats; foo_close; foo_version;
	local:
		*;
};
```

`ELF_LIB_LDFLAGS="-shared -Wl,-soname,libfoo.so.1 -Wl,--version-script=foo.map"`. 실측(`demo_vm.txt` §7): `readelf --dyn-syms` 에 `foo_open@@FOO_1.0`(`@@` = 기본 버전), 버전 정의 심볼 `FOO_1.0` (ABS), 앱 쪽은 `UND foo_open@FOO_1.0 (4)` 와 `readelf -V` 의 `File: libfoo.so.1 ... Name: FOO_1.0`. 쓸모: 나중에 `foo_open` 의 시그니처를 바꿔야 하면 옛 구현을 `foo_open@FOO_1.0`, 새 구현을 `foo_open@@FOO_2.0` 으로 **같은 SONAME 안에** 둘 수 있다(`.symver` 지시어 — 이 레슨에서는 실행 안 함, 확인 권장). 또 버전 정의는 있지만 `FOO_1.0` 이 없는 라이브러리(`FOO_0.9` 만)로 돌리면 ld.so 가 `version 'FOO_1.0' not found (required by ./vs/app)` 로 **로드 시점에** 거부했다(rc=1, glibc 의 `GLIBC_2.34 not found` 와 같은 메커니즘). 버전 정보가 **아예 없는** 라이브러리면 `no version information available` 경고만 내고 실행됐다 — 버전을 붙이는 첫 릴리스부터 의미가 생긴다. check: `PASS P3 version script: lib exports 'foo_open@@FOO_1.0', app needs 'FOO_1.0'`.

</details>

### 한 번에 검증

<!-- code: practice/build.conf diff=solution/build.conf -->

```bash
bash check.sh practice     # 20/20 passed
```

```text
PASS A install name (otool -D) is @rpath-relative: '@rpath/libfoo.1.dylib'
...                                   (solution 과 같은 맥 7줄 PASS)
PASS P1 prebuilt app fixed with install_name_tool, moved, runs: 'libfoo 1.2.0 (app abi 1)'
PASS P1 signatures valid (codesign -v) and library id fixed to @rpath/libfoo.dylib
PASS P2 Mac side by side: lib/{libfoo.1.dylib libfoo.2.dylib}, old app + new app both run
...                                   (solution 과 같은 VM 7줄 PASS)
PASS P3 version script: lib exports 'foo_open@@FOO_1.0', app needs 'FOO_1.0'
PASS P2 ELF side by side: lib/{libfoo.so.1 libfoo.so.1.2.0 libfoo.so.2 libfoo.so.2.0.0}, old app + new app both run
20/20 passed
```

## 10. 핵심 정리

- 공유 라이브러리는 **이름**으로 연결된다. 맥: dylib 의 install name(`LC_ID_DYLIB`, 기본값 = `-o` 경로!) → 앱 `LC_LOAD_DYLIB` + `LC_RPATH`. ELF: `SONAME` → 앱 `NEEDED` + `RUNPATH`. 이름은 `@rpath/...` / SONAME, 검색 경로는 `@loader_path/../lib` / `$ORIGIN/../lib` 로 상대화해야 옮겨도 산다.
- ELF 세 이름: `libfoo.so`(링커, -dev) → `libfoo.so.1`(SONAME, ldconfig) → `libfoo.so.1.2.0`(실제). SONAME 없으면 NEEDED 에 `libfoo.so` 가 박힌다.
- 검색 순서(glibc): RPATH(RUNPATH 없을 때) → `LD_LIBRARY_PATH` → RUNPATH(직접 의존만) → ld.so.cache → 기본 경로. 맥: `DYLD_LIBRARY_PATH` 가 `@rpath` 보다 먼저. 진단: `LD_DEBUG=libs`, `ldd`, `readelf -d` / `DYLD_PRINT_SEARCHING=1`, `otool -L/-l`.
- 이미 빌드된 파일: `install_name_tool -id/-change/-add_rpath`(한 옵션 실패 = 전부 취소, ad-hoc 서명은 자동 재서명됨) / `patchelf --set-soname/--replace-needed/--set-rpath`.
- `-fvisibility=hidden` + `FOO_API __attribute__((visibility("default")))` = 모듈 안에서는 전역, 밖에서는 안 보임. 공개 표면 축소 + ELF 끼어들기 차단 + 직접 호출. 맥은 2단계 이름 공간이라 끼어들기가 기본으로 없다 → "맥에서는 되는데 리눅스에서 값이 틀림".
- `-fPIC`: `.so` 용 오브젝트는 항상. arm64 맥은 항상 PIC, aarch64 는 외부 데이터에서, x86_64 는 전역 접근에서 `recompile with -fPIC`.
- ABI 깨짐은 로더가 모른다(이름만 확인). 메이저를 이름에(SONAME/install name), 헤더 inline handshake(버전 + sizeof), 불투명 타입, 심볼 버전. 맥 `compatibility_version` 은 이 dyld 에서 강제되지 않았다.
- MCU 펌웨어 = 정적 링크(MMU·로더 없음, XIP). 임베디드 리눅스 = rootfs/RAM 공유 · 부분 업데이트 · `dlopen` 플러그인 때문에 `.so`. aarch64 의 64 KB 정렬은 작은 `.so` 를 부풀린다.

## 참고자료

- Apple `man ld` (`-install_name`, `-rpath`, `-current_version`, `-compatibility_version`, `-flat_namespace`, `-exported_symbols_list`), `man dyld` (`DYLD_LIBRARY_PATH`, `DYLD_PRINT_*`, `@rpath` / `@loader_path`), `man install_name_tool`, `man otool`, `dyld_info` — 이 맥의 로컬 man 페이지
- Apple, *Dynamic Library Programming Topics* (install names, run-path dependent libraries): <https://developer.apple.com/library/archive/documentation/DeveloperTools/Conceptual/DynamicLibraries/>
- glibc `ld.so(8)` — 검색 순서, `LD_LIBRARY_PATH`, `LD_DEBUG`, `$ORIGIN`: <https://man7.org/linux/man-pages/man8/ld.so.8.html>, `ldconfig(8)`, `ldd(1)` (보안 경고)
- Ulrich Drepper, *How To Write Shared Libraries* (가시성, PLT/GOT 비용, 버전 관리): <https://www.akkadia.org/drepper/dsohowto.pdf>
- GCC Wiki — Visibility: <https://gcc.gnu.org/wiki/Visibility>, GNU ld manual — VERSION 명령 / `-soname` / `-rpath` / `--enable-new-dtags` / `-Bsymbolic`: <https://sourceware.org/binutils/docs/ld/>
- Program Library HOWTO — Shared Libraries (real name / soname / linker name): <https://tldp.org/HOWTO/Program-Library-HOWTO/shared-libraries.html>
- patchelf: <https://github.com/NixOS/patchelf>, libabigail `abidiff`: <https://sourceware.org/libabigail/>
- 관련 레슨: TOOL-B04 (오브젝트 파일·nm), TOOL-B05 (링크 에러·weak), TOOL-B09 (정적 라이브러리·링크 순서), TOOL-B14 (크로스 컴파일·트리플), TOOL-M01 (링커 스크립트), TOOL-M04/M05 (CMake·툴체인 파일, 설치 RPATH), TOOL-M13 (크기 다이어트), TOOL-A06 (재현 가능한 빌드), TOOL-A11 (ELF 파서 — 동적 섹션 직접 읽기)
