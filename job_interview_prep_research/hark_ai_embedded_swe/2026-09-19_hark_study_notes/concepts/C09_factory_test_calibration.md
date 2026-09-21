# C09. Factory Test & Calibration — 라인에서 1대당 몇십 초 안에 검사하고, 보정하고, 신원을 부여하고, 잠근다

> **이 노트를 다 읽으면**: SMT부터 FATP까지 제조 흐름과 EVT/DVT/PVT/MP 단계를 그림으로 설명할 수 있다 · 테스트 모드 진입과 factory 명령 프로토콜을 설계하고 C로 파서를 짤 수 있다 · 센서/마이크/배터리/RF 캘리브레이션 원리와 cal 데이터 저장 구조(CRC, 버전, 이중 사본)를 설명할 수 있다 · provisioning·추적성·출하 전 잠금을 Don의 Apple factory test-node 경험과 엮어 말할 수 있다
> **JD 연결**: "Develop factory test and calibration firmware for manufacturing" / Bonus: "Experience shipping consumer electronics through EVT/DVT/PVT milestones"
> **Don 기준 난이도**: bring-up → NPI → MP 흐름, factory test-node 설계, 스트레스 시나리오, yield 논의는 이미 강점 / 센서·마이크·배터리 캘리브레이션의 수학, cal 데이터 포맷, 키·인증서 provisioning, 스타트업에서 처음부터 라인을 세팅하는 관점은 새로 정리한다

---

## 0. 큰 그림

Factory firmware는 "제품 FW의 부록"이 아니라 **제품이 세상에 나오기 위한 유일한 관문**이다. 라인에서 1대당 쓸 수 있는 시간은 스테이션마다 수십 초에서 몇 분이고, 그 안에 (1) 조립 불량을 잡고, (2) 부품 편차를 보정하고, (3) 기기에 고유한 신원(시리얼, MAC, 키)을 부여하고, (4) 보안을 잠가야 한다. 이 모든 결과는 서버(MES)에 기록되어 나중에 필드 불량이 나면 역추적된다.

```
  부품 입고(IQC)
       |
       v
 +-----------+   +-----------+   +---------------+   +------------+   +----------------+
 |   SMT     |-->| AOI/X-ray |-->| Board program |-->| ICT        |-->| Board FCT +    |
 | (솔더 인쇄 |   | (광학/엑스 |   | (SWD/JTAG 로  |   | (bed of    |   | board-level cal|
 |  부품 실장 |   |  레이 검사)|   |  test FW 기록)|   |  nails)    |   | (RF conducted) |
 |  리플로우) |   +-----------+   +---------------+   +------------+   +-------+--------+
 +-----------+                                                                 |
                                                                               v
 +-------------------------------- FATP (Final Assembly, Test & Pack) ---------------------+
 |  조립 -> 기능 테스트(버튼/LED/햅틱) -> 센서 cal(IMU/ALS) -> 오디오(마이크/스피커 챔버)   |
 |       -> RF radiated (shield box) -> 배터리/충전 -> run-in(장시간 동작) -> provisioning |
 |       -> 출하 FW 기록 + 잠금 -> 최종 검사(OQC) -> 포장                                    |
 +-----------------------------------------------------------------------------------------+
       |                     각 스테이션: 결과·로그·cal 값 -> MES (serial 기준)
       v
    출하 --> 필드 --> RMA/불량 분석 시 MES 기록으로 역추적
```

FW 엔지니어가 만드는 것은 대략 다음과 같다.

- 기기 쪽: 테스트 모드, factory 명령 처리기, 센서/라디오 측정 루틴, cal 데이터 저장·로드, provisioning 명령, 잠금 명령
- 호스트 쪽(보통 test engineering 팀과 공동): 스테이션 스크립트가 쓰는 명령 사양, 로그 형식, limit 파일, 타임아웃 정책
- 제품 FW 쪽: cal 데이터를 읽어 적용하고, cal이 없거나 깨졌을 때 안전하게 동작하는 코드

> Don 경험과 연결: Apple에서 factory test-node 아키텍처를 설계하고 bring-up → NPI → MP를 경험한 것은 이 JD 항목과 거의 1:1이다. 스타트업(Hark)은 이 인프라가 아직 없을 가능성이 높다. "처음부터 무엇을 어떤 순서로 세우겠다"를 말할 수 있으면 가장 큰 차별화 포인트가 된다.

---

## 1. 제품 개발 단계 — Proto, EVT, DVT, PVT, MP

### 1.1 단계별 정의

회사마다 이름과 경계는 조금씩 다르다(예: P0/P1/P2 proto, EVT1/EVT2, DVT2 등). 공통된 의미는 다음과 같다.

| 단계 | 이름 | 질문 | 수량(예시) | 제조 방식 | FW가 준비할 것 |
|---|---|---|---|---|---|
| Proto | Prototype | 이 구조가 동작하나? | 수~수십 대 | 수작업, 개발 보드 조합 | bring-up, 드라이버 기본 동작 |
| EVT | Engineering Validation Test | 설계가 전기적·기능적으로 사양을 만족하나? | 수십~수백 대 | 목표 공장, 초기 fixture | 모든 주변장치 드라이버, 기본 test 명령, 전력 측정, 초기 cal 방법 탐색 |
| DVT | Design Validation Test | 실제 외형·재료로 신뢰성·인증을 통과하나? | 수백~수천 대 | 양산 공정에 가깝게 | cal 알고리즘 확정, 테스트 커버리지, 규격 인증(FCC/CE, BT SIG)용 test mode, 신뢰성 시험(낙하, 온도, ESD) 대응 |
| PVT | Production Validation Test | 양산 라인이 목표 속도·수율로 돌아가나? | 수천 대 이상 | 실제 라인, 실제 UPH | test time 최적화, limit 확정, provisioning·잠금 절차 확정, 출하 FW |
| MP | Mass Production (ramp) | 안정적으로 대량 생산 | 계속 | 양산 | 수율 모니터링, 라인 이슈 대응, 테스트 시간 추가 단축, OTA로 출하 FW 갱신 |

### 1.2 단계마다 factory FW에 요구되는 변화

```
Proto     EVT                 DVT                     PVT                    MP
  |--------|--------------------|-----------------------|----------------------|------>
  bring-up  "모든 걸 측정"          "무엇을 테스트할지 결정"    "빠르고 안정적으로"        "수율·시간 최적화"
            - raw 값 덤프           - limit 초안 (DVT 데이터)  - limit 확정, GR&R        - 테스트 삭제/병합
            - 수동 명령             - cal 알고리즘 확정        - UPH 목표 달성           - 필드 불량 → 테스트 추가
            - 디버그 포트 열림       - 인증용 RF test mode      - provisioning, 잠금       - 라인 FW 버전 관리
```

- **EVT**에서 FW의 역할은 "하드웨어가 사양대로인지 판단할 데이터를 최대한 많이 뽑는 것"이다. 테스트 시간은 중요하지 않다.
- **DVT**에서는 EVT/DVT 데이터 분포를 보고 **limit(합격 범위)**을 정한다. cal이 필요한 항목과 필요 없는 항목도 이때 결정한다.
- **PVT**에서는 테스트 시간이 곧 비용이다. 병렬화, 불필요한 대기 제거, 여러 측정을 한 명령으로 묶는 최적화를 한다.
- **MP**에서 필드 불량이 나오면 "이걸 라인에서 잡을 수 있었나?"를 묻고 테스트를 추가한다. 반대로 한 번도 fail이 없던 테스트는 삭제 후보가 된다.

> Don 경험과 연결: Apple에서 새 무선 칩을 EVT에서 MP까지 끌고 가며 인터페이스(PCIe/I2C/SPMI/RFFE) 장애를 root cause한 경험은 "EVT 데이터로 무엇을 테스트할지 결정하는" DVT 단계의 판단과 같다. 스트레스 시나리오로 latent defect를 MP 전에 잡은 사례를 이 단계 흐름 위에 놓고 설명한다.

---

## 2. 테스트 모드 — 기기가 "시험받는 상태"로 들어가는 법

### 2.1 두 가지 FW 전략

| 전략 | 설명 | 장점 | 단점 |
|---|---|---|---|
| 별도 factory FW 이미지 | 라인 앞단에서 test FW를 굽고, 마지막에 출하 FW로 교체 | 제품 FW와 분리, 테스트 기능을 마음껏 넣음, 제품 FW에 공격 표면이 남지 않음 | 이미지 교체 시간, 두 FW의 드라이버가 달라지면 "테스트는 통과했는데 제품 FW에선 고장" |
| 제품 FW + factory mode | 같은 이미지가 조건에 따라 테스트 명령을 받음 | 테스트한 코드 = 출하 코드, 교체 시간 없음 | 테스트 기능이 출하 기기에 남음 → 반드시 잠금·인증 필요 |
| 혼합 | 보드 단계는 test FW, FATP 이후는 제품 FW + factory mode | 현실적인 절충 | 관리할 것이 두 개 |

현업에서는 혼합이 흔하다. 보드 단계(ICT/board FCT)는 flash가 비어 있으니 SWD로 test FW를 넣고, 조립 후에는 제품 FW를 올린 뒤 factory mode로 나머지를 한다. 어느 경우든 **드라이버 코드는 공유**해야 "테스트에서 검증한 것이 실제로 출하되는 것"이 된다.

### 2.2 factory mode 진입 방법

| 방법 | 동작 | 장점 | 위험 |
|---|---|---|---|
| GPIO strap / test point | fixture의 pogo pin이 특정 핀을 리셋 시점에 low로 | 확실, 소비자가 우연히 못 들어감 | 조립 후 test point 접근 불가일 수 있음 |
| 부팅 직후 시리얼 매직 시퀀스 | 부팅 후 N ms 안에 UART로 특정 문자열 수신 | 추가 하드웨어 불필요 | 부팅 시간 증가, 공격자가 재현 가능 |
| USB 특수 descriptor/명령 | USB 연결 시 vendor 명령으로 진입 | 완제품에서도 가능 | 출하 후에도 열려 있으면 공격 표면 |
| 제조 상태 플래그 | "factory 미완료" 플래그(OTP/보호 영역)가 켜져 있으면 factory mode | 라인 흐름과 자연스럽게 맞음 | 마지막 단계에서 플래그를 확실히 끄고 검증해야 함 |
| 서명된 토큰 | 서버가 기기 고유 ID에 서명한 토큰을 보내야 진입 | 출하 후(RMA)에도 안전하게 재진입 | 인프라 필요 |

실무 규칙: **"factory 미완료 상태 플래그 + 물리적 조건"**으로 진입하게 하고, 출하 직전에 플래그를 끈다. 출하 후에는 서명된 토큰으로만 제한된 진단 명령을 허용한다.

### 2.3 테스트 모드에서 FW가 해야 할 일

- 제품 동작(사용자 UI, 자동 절전, OTA 체크, 무선 광고 등)을 **멈춘다**. 측정에 노이즈가 된다. 예: 전류 측정 중 BLE advertising이 켜져 있으면 값이 튄다.
- watchdog은 끄지 말고, 긴 측정 중에도 feed한다. 테스트 중 hang도 결함 정보다.
- 모든 명령은 **결정적(deterministic)이고 시간 제한이 있어야** 한다. 스테이션은 타임아웃으로 fail을 판정한다.
- 전력 측정용 상태(`sleep` 명령: 모든 주변장치 끄고 deep sleep 진입, 특정 핀으로 깨움)를 제공한다. 배터리 기기의 sleep 전류는 라인에서 꼭 재는 항목이다(C05 연결).

---

## 3. Factory 명령 프로토콜

### 3.1 설계 원칙

스테이션 PC(또는 Don이 말하는 test node)가 기기에 명령을 보내고 결과를 파싱해서 limit과 비교한다. 프로토콜은 **사람이 읽을 수 있으면서 기계가 파싱하기 쉬워야** 한다.

| 원칙 | 이유 |
|---|---|
| 한 줄 명령, 한 줄(또는 명확히 끝나는) 응답 | 스테이션 스크립트가 단순해짐 |
| 응답은 `OK`/`ERR <code>`로 시작 | 파싱 실패와 기능 실패를 구분 |
| 측정값은 raw + 단위 명시(`key=value`) | limit 비교는 스테이션이 한다. FW가 pass/fail을 판정하지 않는 편이 limit 변경에 유연 |
| 프로토콜 버전 명령(`VER`) | FW 업데이트로 명령이 바뀌어도 스테이션이 호환성 확인 |
| 명령마다 최대 실행 시간 문서화 | 타임아웃 설정의 근거 |
| 에코/프롬프트 끄기 옵션 | 사람이 쓸 땐 편하지만 기계 파싱에 방해 |
| 부작용 있는 명령(OTP 쓰기, 잠금)은 확인 인자 필요 | 스크립트 버그로 인한 사고 방지 |

전송 계층은 보통 UART(보드 단계, test point)나 USB CDC-ACM(완제품)이다. 무선만 있는 완제품이라면 BLE GATT 서비스로 같은 명령을 싣기도 한다. 바이너리 프로토콜(길이+CRC 프레임)은 대량 데이터(오디오 캡처, 로그 덤프)에만 쓰고, 기본은 텍스트가 디버깅에 유리하다.

예시 세션:

```
host> VER
dev < OK proto=2 fw=1.4.0+12 hw=DVT2 sn=UNSET
host> IMU READ 100
dev < OK ax=-0.012 ay=0.004 az=1.003 gx=0.21 gy=-0.35 gz=0.08 unit=g,dps n=100
host> MIC LEVEL 0 500
dev < OK ch=0 dbfs=-26.4 n_ms=500
host> CAL WRITE mic0_gain_mdb=-400
dev < OK
host> SN SET HK1A23456789 CONFIRM
dev < OK
host> FOO
dev < ERR 2 unknown_command
```

### 3.2 C 구현 — 줄 단위 파서 + 디스패치 테이블

UART ISR(또는 USB 수신 콜백)이 바이트를 넣고, 태스크가 줄 단위로 처리하는 구조다. 호스트 PC에서도 컴파일해서 단위 테스트할 수 있게 입출력을 분리했다.

```c
/* factory_cmd.c -- line-based factory command interpreter (host-testable) */
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <stdbool.h>

#define LINE_MAX     128
#define ARGV_MAX     8
#define PROTO_VER    2

enum fc_err {
    FC_OK = 0,
    FC_ERR_OVERFLOW = 1,
    FC_ERR_UNKNOWN  = 2,
    FC_ERR_ARGS     = 3,
    FC_ERR_HW       = 4,
    FC_ERR_LOCKED   = 5,
};

typedef int (*fc_handler_t)(int argc, char *argv[]);

struct fc_cmd {
    const char  *name;
    int          min_args;        /* not counting the command name itself */
    fc_handler_t fn;
    const char  *help;
};

/* ---- output: single choke point, so the transport can be swapped (UART, USB, RTT) ---- */
static void fc_reply(const char *s)
{
    fputs(s, stdout);
    fputs("\r\n", stdout);
}

static bool g_factory_locked = false;   /* set from OTP lifecycle state on real HW */

/* ---- handlers (hardware calls stubbed) ---- */
static int cmd_ver(int argc, char *argv[])
{
    (void)argc; (void)argv;
    char buf[80];
    snprintf(buf, sizeof(buf), "OK proto=%d fw=1.4.0+12 hw=DVT2", PROTO_VER);
    fc_reply(buf);
    return FC_OK;
}

static int cmd_imu_read(int argc, char *argv[])
{
    (void)argc;
    long n = strtol(argv[2], NULL, 10);            /* argv: IMU READ <n> */
    if (n <= 0 || n > 1000) {
        return FC_ERR_ARGS;
    }
    /* real code: average n samples from the IMU driver */
    fc_reply("OK ax=-0.012 ay=0.004 az=1.003 unit=g");
    return FC_OK;
}

static int cmd_sn_set(int argc, char *argv[])
{
    if (g_factory_locked) {
        return FC_ERR_LOCKED;
    }
    if (argc < 4 || strcmp(argv[3], "CONFIRM") != 0) {  /* SN SET <sn> CONFIRM */
        return FC_ERR_ARGS;
    }
    if (strlen(argv[2]) != 12) {
        return FC_ERR_ARGS;
    }
    /* real code: write to OTP / protected partition, then read back and compare */
    fc_reply("OK");
    return FC_OK;
}

static const struct fc_cmd cmd_table[] = {
    { "VER",      0, cmd_ver,      "VER" },
    { "IMU READ", 1, cmd_imu_read, "IMU READ <n>" },
    { "SN SET",   2, cmd_sn_set,   "SN SET <sn> CONFIRM" },
};

/* Split in place on spaces. Returns argc. */
static int tokenize(char *line, char *argv[], int max)
{
    int argc = 0;
    char *save = NULL;
    for (char *tok = strtok_r(line, " \t", &save);
         tok != NULL && argc < max;
         tok = strtok_r(NULL, " \t", &save)) {
        argv[argc++] = tok;
    }
    return argc;
}

/* Match "IMU READ" style multi-word command names against argv[0..]. Returns words used. */
static int match_name(const char *name, int argc, char *argv[])
{
    int words = 0;
    const char *p = name;
    while (*p != '\0') {
        const char *sp = strchr(p, ' ');
        size_t len = sp ? (size_t)(sp - p) : strlen(p);
        if (words >= argc || strlen(argv[words]) != len || strncmp(argv[words], p, len) != 0) {
            return 0;
        }
        words++;
        p = sp ? sp + 1 : p + len;
    }
    return words;
}

static void fc_execute(char *line)
{
    char *argv[ARGV_MAX];
    int argc = tokenize(line, argv, ARGV_MAX);
    if (argc == 0) {
        return;                                    /* empty line: ignore */
    }
    for (size_t i = 0; i < sizeof(cmd_table) / sizeof(cmd_table[0]); i++) {
        int used = match_name(cmd_table[i].name, argc, argv);
        if (used == 0) {
            continue;
        }
        int rc = (argc - used < cmd_table[i].min_args) ? FC_ERR_ARGS
                                                      : cmd_table[i].fn(argc, argv);
        if (rc != FC_OK) {
            char buf[48];
            snprintf(buf, sizeof(buf), "ERR %d %s", rc, cmd_table[i].help);
            fc_reply(buf);
        }
        return;
    }
    fc_reply("ERR 2 unknown_command");
}

/* ---- byte feed: call from the RX task (not the ISR) for each received byte ---- */
static char   s_line[LINE_MAX];
static size_t s_len;
static bool   s_overflow;

void fc_feed(char c)
{
    if (c == '\r') {
        return;                                    /* accept CRLF or LF */
    }
    if (c == '\n') {
        if (s_overflow) {
            fc_reply("ERR 1 line_too_long");
        } else {
            s_line[s_len] = '\0';
            fc_execute(s_line);
        }
        s_len = 0;
        s_overflow = false;
        return;
    }
    if (s_len < LINE_MAX - 1) {
        s_line[s_len++] = c;
    } else {
        s_overflow = true;                         /* keep discarding until newline */
    }
}

#ifdef HOST_TEST
int main(void)
{
    const char *script = "VER\nIMU READ 100\nSN SET HK1A23456789 CONFIRM\nFOO\nIMU READ\n";
    for (const char *p = script; *p != '\0'; p++) {
        fc_feed(*p);
    }
    return 0;
}
#endif
```

한 줄씩 설계 의도를 본다.

- `fc_reply()` 하나로 출력을 모은다. 보드 단계는 UART, 완제품은 USB CDC, 디버깅 때는 SEGGER RTT로 바꿀 수 있다. 스테이션 로그와 FW 로그가 섞이지 않도록 **일반 로그 출력은 factory mode에서 끄거나 다른 채널로** 보낸다. 섞이면 파서가 깨진다(가장 흔한 라인 문제 중 하나).
- `cmd_table`: 명령 추가가 테이블 한 줄로 끝난다. `help` 문자열을 ERR 응답에 붙여서 스테이션 로그만 봐도 올바른 사용법이 보인다.
- `min_args` 검사: 인자 부족을 핸들러마다 반복 검사하지 않는다. 예시의 마지막 줄 `IMU READ`(인자 없음)는 `ERR 3`을 받는다.
- `SN SET ... CONFIRM`: 되돌릴 수 없는 쓰기(OTP)는 확인 인자를 요구한다. 스크립트 오타로 시리얼을 잘못 굽는 사고를 줄인다.
- `g_factory_locked`: 잠금 이후엔 쓰기 계열 명령을 거부한다. 실제로는 OTP의 lifecycle 상태에서 읽는다.
- `fc_feed()`: 줄이 너무 길면 버퍼를 넘치게 하지 않고 개행까지 버린 뒤 에러를 낸다. 이 함수는 ISR이 아니라 RX 태스크에서 부른다. ISR은 링버퍼(S01 참고)에 바이트만 넣는다. `strtok_r`은 재진입 가능 버전이다.
- `#ifdef HOST_TEST`: `cc -DHOST_TEST factory_cmd.c && ./a.out`로 PC에서 바로 돌려 볼 수 있다. factory 명령 처리기는 **호스트 단위 테스트가 가능해야** 라인 투입 전에 회귀를 잡는다.

### 3.3 Zephyr라면 shell 서브시스템

Zephyr에는 이미 명령 해석기(shell)가 있다. `SHELL_CMD_REGISTER`로 등록하면 UART/USB/RTT 백엔드를 그대로 쓴다.

```c
#include <zephyr/kernel.h>
#include <zephyr/shell/shell.h>
#include <errno.h>
#include <stdlib.h>

static int cmd_imu_read(const struct shell *sh, size_t argc, char **argv)
{
    long n = strtol(argv[1], NULL, 10);
    if (n <= 0 || n > 1000) {
        shell_print(sh, "ERR 3 IMU READ <n>");
        return -EINVAL;
    }
    /* sample the IMU n times via the sensor API, then: */
    shell_print(sh, "OK ax=%d ay=%d az=%d unit=mg n=%ld", -12, 4, 1003, n);
    return 0;
}

SHELL_STATIC_SUBCMD_SET_CREATE(sub_imu,
    SHELL_CMD_ARG(read, NULL, "read <n>: average n samples", cmd_imu_read, 2, 0),
    SHELL_SUBCMD_SET_END
);
SHELL_CMD_REGISTER(imu, &sub_imu, "IMU factory commands", NULL);
```

- `SHELL_CMD_ARG(..., 2, 0)`: 필수 인자 2개(명령 이름 포함), 선택 인자 0개. 부족하면 shell이 자동으로 에러를 낸다.
- 스테이션 자동화용으로는 에코·색상·프롬프트를 끄는 설정을 확인한다(`CONFIG_SHELL_VT100_COLORS=n` 등, 버전마다 옵션 이름 확인). 기계 파싱은 여전히 `OK`/`ERR` 접두사 규칙을 지킨다.

---

## 4. Fixture, 스테이션, 테스트 시간

### 4.1 테스트 종류 비교

| 항목 | ICT (In-Circuit Test) | Board FCT (Functional Circuit Test) | FATP 시스템 테스트 |
|---|---|---|---|
| 대상 | 부품 실장된 PCBA, 전원 인가 전/후 | 전원 켠 PCBA + test FW | 완제품 |
| 방법 | bed-of-nails로 net마다 저항·커패시턴스·다이오드 측정, 일부 boundary scan | pogo pin + 계측기 + FW 명령 | USB/무선 명령, 챔버, 로봇, 카메라 |
| 잡는 것 | 쇼트, 오픈, 잘못된 부품 값, 누락 | 전원 레일 전압, 클럭, 버스 통신, 센서 응답, 전류 | 조립 불량(커넥터, 안테나 접촉), 음향, radiated RF, 배터리 |
| FW 역할 | 거의 없음 (JTAG boundary scan 제외) | test FW, 명령 처리기 | factory mode, cal, provisioning |
| test point 필요 | 많음 | 중간 | 없음 (외부 포트만) |

웨어러블급 소형 기기는 보드에 test point를 넣을 공간이 거의 없어서 ICT 커버리지가 낮다. 그만큼 **FW 기반 기능 테스트의 비중이 커진다**. EVT 회로도 리뷰 때 "factory에 필요한 test point(전원 레일, SWD, UART, 부트 strap)"를 요구하는 것도 FW 엔지니어의 일이다(C10 회로도 읽기 연결).

### 4.2 Fixture 구성

```
   Station PC (test sequencer, limits, MES client)
        |  USB/Ethernet
        +------------------+------------------+-------------------+
        |                  |                  |                   |
   +----v----+       +-----v-----+      +-----v------+      +-----v-----+
   | DMM /   |       | Prog. PSU |      | RF tester  |      | Audio      |
   | SMU     |       | (전류 측정)|      | (신호 발생/ |      | analyzer + |
   +----+----+       +-----+-----+      |  분석)      |      | ref mic/spk|
        |                  |            +-----+------+      +-----+-----+
        |   relay matrix   |                  | 동축/shield box     | 음향 챔버
        +--------+---------+                  |                    |
                 |                            |                    |
          +------v----------------------------v--------------------v-----+
          |  Fixture: 기구물이 DUT 를 고정, pogo pin 이 test point 에 접촉   |
          |  DUT <--UART/USB--> Station PC (factory 명령)                  |
          +---------------------------------------------------------------+
```

- **DUT(Device Under Test)**는 스테이션의 "명령을 받는 계측 대상"이다. FW는 측정을 돕는 쪽(자극 생성, 상태 설정, 내부 센서 값 보고)이고, 판정 기준이 되는 측정은 외부 계측기가 한다.
- **golden unit**: 알려진 좋은 기기. 매 교대(shift) 시작 때 golden unit을 돌려 fixture 자체가 정상인지 확인한다. pogo pin 마모, 케이블 손상, 계측기 드리프트를 잡는다.
- **fixture 보정**: RF 케이블 손실, 음향 챔버 특성 같은 fixture 고유 오차는 golden unit이나 기준 장비로 측정해 스테이션 설정에 넣는다.

### 4.3 테스트 시간과 UPH

```
라인 목표: 하루 20,000대, 2교대 x 10시간 가동
  -> 필요 UPH = 20,000 / 20 = 1,000 units/hour
  -> takt time = 3600 s / 1000 = 3.6 s/unit

어떤 스테이션의 테스트 시간이 72 s 라면:
  -> 필요한 병렬 fixture 수 = 72 / 3.6 = 20 개 (+ 여유, 재시험 비율 고려)
```

테스트 시간 1초 단축 = fixture 수와 공간·계측기 비용 절감이다. FW가 줄일 수 있는 부분:

| 낭비 | 개선 |
|---|---|
| 부팅 대기 (full boot 후 명령 수락) | factory mode에서는 무선 스택 초기화 생략, 명령 처리기 먼저 기동 |
| 센서 안정화 고정 대기 (`sleep 2s`) | 안정화 여부를 FW가 판단해 준비되면 응답 |
| 명령마다 왕복 | 여러 측정을 한 명령으로 묶기 (`SENSORS ALL`) |
| 순차 측정 | 독립 측정 병렬화 (IMU 샘플링하면서 마이크 캡처) |
| 느린 링크 | UART 115200 → 1M baud 또는 USB, 큰 데이터는 바이너리 |
| cal 후 재측정 검증 | 필요한 항목만 검증, 나머지는 통계로 모니터링 |

### 4.4 수율과 통계 — 용어를 정확히

| 용어 | 정의 | 메모 |
|---|---|---|
| FPY (First Pass Yield) | 첫 시도에 통과한 비율 | 재시험 전 기준. 라인 건강의 핵심 지표 |
| Final yield | 재시험·수리 후 최종 통과 비율 | FPY보다 높음 |
| RTY (Rolled Throughput Yield) | 각 스테이션 FPY의 곱 | 스테이션 5개가 각 98%면 약 90% |
| Retest policy | fail 시 몇 번 재시험 허용하나 | 재시험 통과가 많으면 테스트 불안정(flaky) 신호 |
| False fail (overkill) | 좋은 기기를 불량 판정 | 수율 손실, 비용 |
| Escape (underkill) | 불량 기기를 통과 판정 | 필드 불량, 브랜드 손상. 훨씬 비쌈 |
| Cpk | 공정 능력 지수: min(USL − μ, μ − LSL) / 3σ | 흔히 1.33 이상을 목표로 함 (회사마다 다름) |
| GR&R | Gauge Repeatability & Reproducibility | 측정 시스템 자체의 변동. 같은 기기를 여러 번, 여러 fixture에서 측정 |
| Guard band | 측정 불확도만큼 limit을 좁히는 여유 | escape를 줄이는 대신 false fail 증가 |

**limit 설정**: DVT/PVT 데이터 분포(평균, 표준편차)와 설계 사양을 둘 다 본다. 사양보다 분포가 훨씬 좁다면 limit을 분포 기준으로 좁혀서 "사양 안이지만 이상한 기기"(다른 결함의 전조)를 잡을 수 있다. 이것이 Don이 말한 "스트레스 시나리오로 latent defect를 잡는" 관점과 같다.

> Don 경험과 연결: test-node 아키텍처를 설계할 때 "커버리지 vs 테스트 시간"을 어떻게 맞췄는지가 면접 딥다이브 질문으로 나온다(context 4.4). 위 표의 용어(FPY, escape, GR&R, takt)를 써서 답하면 스타트업 면접관에게 양산 경험이 즉시 전달된다.

---

## 5. 캘리브레이션 — 부품 편차를 숫자로 지운다

### 5.1 원리

모든 센서는 `측정값 = gain × 참값 + offset + 노이즈 + (온도 등 의존항)` 형태의 오차를 가진다. 캘리브레이션은 **알려진 자극(reference)을 주고, 측정하고, 보정 계수를 계산해 저장하고, 제품 FW가 매번 적용**하는 과정이다.

```
  알려진 자극          DUT 측정           계수 계산             저장              필드에서 적용
 (1 g, 94 dB SPL, --> raw 값 N개 평균 --> gain, offset,  -->  cal 영역에 기록 --> corrected =
  기준 전압, 기준      (노이즈 제거)       trim code          (CRC, 버전)          (raw - off) x gain
  RF 신호)                                 limit 검사           + MES 업로드
                                           (계수가 비정상이면
                                            하드웨어 불량)
```

**cal 계수 자체도 테스트 항목이다.** 오프셋이 허용 범위를 넘으면 보정할 게 아니라 불량(부품 손상, 조립 스트레스)으로 판정한다.

**2점 선형 캘리브레이션** (가장 기본):

```
 기준 x1 -> raw r1,  기준 x2 -> raw r2
 gain   = (x2 - x1) / (r2 - r1)
 offset = x1 - gain x r1
 적용:  x = gain x raw + offset
```

### 5.2 종류별 정리

| 대상 | 무엇을 보정 | 라인에서의 방법 | 저장 값 예 | 메모 |
|---|---|---|---|---|
| 가속도계 | 축별 offset, scale, (축 간 misalignment) | 여러 자세(보통 6면)에 놓고 ±1 g 측정 | offset[3], scale[3] | 소비자 기기는 offset만 하는 경우도 많음 |
| 자이로 | zero-rate bias | 정지 상태 평균 | bias[3] | 온도 의존이 커서 필드에서 추가 추정 |
| 자력계 | hard/soft iron | 라인보다는 필드(사용자 동작)에서 | | 조립 후 주변 금속 영향 |
| 마이크 | 감도(gain), 채널 간 매칭, 위상 | 음향 챔버에서 94 dB SPL @ 1 kHz 기준음 | gain trim(mdB), 위상 차 | 빔포밍·wake word 성능에 직접 영향 |
| 스피커 | 주파수 응답, 최대 출력 보호 | 챔버에서 sweep, 기준 마이크로 측정 | EQ 계수, 보호 파라미터 | 스피커 앰프 보호 알고리즘용 |
| 배터리 / fuel gauge | 전류 센스 offset, 전압 ADC, 셀 모델 파라미터 | 알려진 전류/전압 인가, 벤더 파라미터 이미지 기록 | CC offset, 보드 오프셋 | 방법은 게이지 칩 벤더마다 다름 |
| RF | 크리스털 주파수 오차(XO trim), Tx 출력 전력, RSSI offset | 신호 분석기/발생기와 conducted 또는 radiated 측정 | cap trim code, 채널별 power table | 대부분 칩 벤더 도구·절차에 따름 |
| 온도 센서 | offset | 환경 온도와 비교 | offset | 기기 발열 직후 측정 금지 |
| 근접/조도(ALS) | 크로스토크 baseline, 커버 유리 투과율 | 차광 상태와 기준 광원 | baseline, gain | 커버 유리 로트마다 차이 |
| 햅틱(LRA) | 공진 주파수, back-EMF 파라미터 | 드라이버 IC 자동 캘리브레이션 기능 실행 | 드라이버 레지스터 값 | 예: 일부 햅틱 드라이버 IC가 auto-cal 제공 |
| 터치/정전 센서 | baseline | 무접촉 상태 측정 | baseline | 환경(습도) 영향 |

### 5.3 가속도계 6면 캘리브레이션

각 축을 +1 g(위)와 −1 g(아래) 방향으로 한 번씩 두면 6개 자세가 나온다. 축 x에 대해:

```
  a_plus  = x 축이 위를 볼 때 평균 raw (참값 +1 g)
  a_minus = x 축이 아래를 볼 때 평균 raw (참값 -1 g)

  offset_x = (a_plus + a_minus) / 2          <- 두 측정의 중간점이 0 g 위치
  scale_x  = (a_plus - a_minus) / 2          <- 1 g 당 raw count
  보정:  a_x[g] = (raw_x - offset_x) / scale_x
```

라인에서는 기기를 로봇/회전 fixture가 자세를 바꾸며 측정한다. 자세를 바꿀 때마다 **정지·안정화**를 기다려야 하므로 테스트 시간이 커진다. 그래서 "offset만 1자세에서 측정(z 축 +1 g, x·y 0 g 가정)"으로 줄이는 절충을 DVT 데이터로 검토한다. 자이로 bias는 같은 정지 구간에서 동시에 측정한다.

### 5.4 마이크 감도 캘리브레이션

- 기준: **94 dB SPL = 1 Pa (RMS)**, 1 kHz 사인파. 디지털 MEMS 마이크 감도는 보통 "94 dB SPL에서 몇 dBFS"로 표시된다(예: −26 dBFS, 부품에 따라 다름). 부품 간 감도 편차는 데이터시트상 ±1 dB 수준이 흔하다.
- 절차: 음향 챔버(외부 소음 차단)에서 기준 스피커가 94 dB SPL을 만든다(기준 마이크로 레벨을 먼저 맞춰 둠). DUT가 N ms 캡처 → RMS 계산 → dBFS 보고(`MIC LEVEL` 명령). 스테이션이 목표값과의 차이를 계산해 gain trim을 기록한다.
- 멀티 마이크(빔포밍): 채널 간 감도 **차이**와 위상 매칭이 절대 감도보다 중요하다. 음향 포트(케이스 구멍)나 메시가 막히면 특정 채널만 감도가 떨어지므로, 이것이 조립 불량 검출 항목이 된다.
- FW 쪽 구현: I2S/PDM DMA 버퍼(C03)에서 RMS를 계산한다. dBFS = 20 × log10(RMS / full-scale RMS). 사인파 full-scale 기준인지 DC full-scale 기준인지 정의를 스테이션과 맞춘다(3 dB 차이가 나는 흔한 혼동).

### 5.5 RF 캘리브레이션 (개념만)

- **XO trim**: 크리스털 부하 커패시턴스 편차로 주파수가 수 ppm~수십 ppm 틀어진다. 칩 내부 cap trim 레지스터를 조정해 목표 주파수에 맞추고 code를 저장한다. BLE/Wi-Fi는 주파수 허용 오차 사양이 있으므로 필수에 가깝다.
- **Tx power**: 채널/대역별 출력 전력을 측정해 power table을 조정한다. 규제 한계(FCC 등)를 넘지 않으면서 링크 마진을 확보한다.
- **conducted vs radiated**: 보드 단계에서는 RF 커넥터/test point로 직접(conducted), 완제품에서는 shield box 안에서 안테나로(radiated) 측정한다. radiated는 안테나 조립 불량을 잡는다.
- 계측 장비와 절차는 무선 칩 벤더의 factory 도구(보통 NDA)와 전문 테스터(LitePoint, Rohde & Schwarz, Keysight 등)를 쓴다. FW의 역할은 벤더의 test mode(연속 송신, 특정 채널 수신, 패킷 카운트 등) 명령을 factory 명령 체계에 연결하는 것이다.
- 규격 인증(FCC/CE)용 시험에도 같은 test mode가 쓰인다. BLE는 Direct Test Mode(DTM, Bluetooth Core Spec에 정의)가 표준이다.

> Don 경험과 연결: Apple에서 무선 칩셋 통합을 하며 factory test-node를 설계한 경험은 이 절에 해당한다. 벤더 test mode를 호스트 인터페이스(PCIe/UART) 너머에서 구동하고, 결과를 스테이션이 판정하는 구조를 이미 알고 있다. 면접에서는 칩 벤더 도구를 "블랙박스로 쓰는 것"과 "제품 FW의 명령 체계와 통합하는 것"의 차이를 말하면 좋다.

---

## 6. Cal 데이터 저장 — 절대 잃어버리면 안 되는 데이터

### 6.1 어디에 저장하나

| 위치 | 장점 | 단점 | 적합한 데이터 |
|---|---|---|---|
| OTP / eFuse | 절대 안 지워짐, 변조 불가 | 용량 작음, 한 번만 쓰기, 수정 불가 | 시리얼, MAC, 키 해시, lifecycle, 작은 trim code |
| 전용 보호 flash 파티션 | 용량 충분, 수정 가능(재cal) | OTA·버그로 지워질 위험 → 쓰기 보호, 별도 파티션 필요 | 센서/마이크/RF cal 테이블 |
| 칩 벤더 지정 영역 | 벤더 도구와 호환 (예: 무선 칩 cal 파일) | 포맷 비공개일 수 있음 | RF cal |
| 일반 설정 저장소 (NVS/settings) | 구현 쉬움 | factory reset에 같이 지워질 위험 | 사용하지 말 것 (최소한 분리) |
| MES / 클라우드 백업 | 기기가 망가져도 serial로 복구 | 기기에서 즉시 사용 불가 | 모든 cal 값의 사본 |

**원칙**: cal 데이터는 사용자 데이터·설정과 **다른 파티션**에 두고, factory reset과 OTA가 절대 건드리지 않게 한다. 그리고 모든 값을 MES에도 올려서 RMA 시 재기록할 수 있게 한다.

### 6.2 레이아웃 — 이중 사본 + 헤더

```
 cal partition (예: 2 x 4 KB erase page)
 +-------------------------------+-------------------------------+
 | copy A (page 0)               | copy B (page 1)               |
 | hdr: magic ver len seq crc    | hdr: magic ver len seq crc    |
 | payload (v2 struct)           | payload (v2 struct)           |
 +-------------------------------+-------------------------------+
 읽기: 두 사본 중 (magic OK && crc OK) 인 것 중 seq 가 큰 것 사용
 쓰기: seq 가 작은(또는 깨진) 쪽 page 를 erase -> 기록 -> read-back 검증
       -> 전원 차단 시에도 다른 사본이 온전히 남음
```

### 6.3 C 구현 — 버전·CRC·이중 사본·마이그레이션

```c
/* caldata.c -- versioned, CRC-protected, dual-copy calibration record */
#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include <stdbool.h>

#define CAL_MAGIC        0x314C4143u       /* bytes "CAL1" in little-endian memory */
#define CAL_VER_CURRENT  2u
#define CAL_PAGE_SIZE    4096u

/* Flash abstraction (hypothetical board layer). addr is partition-relative. */
extern int nv_read(uint32_t addr, void *buf, size_t len);
extern int nv_erase_page(uint32_t addr);
extern int nv_write(uint32_t addr, const void *buf, size_t len);

struct cal_hdr {
    uint32_t magic;
    uint16_t version;       /* layout version of the payload */
    uint16_t length;        /* bytes of payload that follow */
    uint32_t seq;           /* bigger = newer; picks between copy A and B */
    uint32_t crc32;         /* CRC-32 over payload only */
};

struct cal_v1 {                              /* EVT layout */
    int16_t  acc_offset[3];                  /* raw LSB */
    uint16_t acc_scale[3];                   /* raw LSB per g */
    int16_t  gyro_bias[3];                   /* raw LSB */
    int8_t   xo_trim;
    uint8_t  _rsv[3];
};

struct cal_v2 {                              /* DVT layout: adds microphones + trace info */
    int16_t  acc_offset[3];
    uint16_t acc_scale[3];
    int16_t  gyro_bias[3];
    int8_t   xo_trim;
    uint8_t  flags;                          /* bit0: mic cal valid */
    int16_t  mic_gain_mdb[2];                /* milli-dB trim per mic channel */
    uint32_t station_id;
    uint32_t cal_time_unix;
};

_Static_assert(sizeof(struct cal_hdr) == 16, "header layout is part of the factory contract");
_Static_assert(sizeof(struct cal_v1) == 22, "v1 layout frozen");
_Static_assert(sizeof(struct cal_v2) == 32, "v2 layout frozen");

#define CAL_FLAG_MIC_VALID  0x01u

/* Bitwise CRC-32 (IEEE 802.3, reflected, poly 0xEDB88320). Small; use a table or HW CRC if speed matters. */
static uint32_t crc32_ieee(const uint8_t *p, size_t n)
{
    uint32_t crc = 0xFFFFFFFFu;
    while (n--) {
        crc ^= *p++;
        for (int k = 0; k < 8; k++) {
            crc = (crc >> 1) ^ (0xEDB88320u & (uint32_t)-(int32_t)(crc & 1u));
        }
    }
    return ~crc;
}

/* Safe defaults: product FW must still run (degraded) with no calibration. */
static void cal_defaults(struct cal_v2 *c)
{
    memset(c, 0, sizeof(*c));
    for (int i = 0; i < 3; i++) {
        c->acc_scale[i] = 16384;             /* nominal LSB/g for a +-2 g range, datasheet value */
    }
}

/* Read one copy; returns true if valid and fills out (migrated to v2). */
static bool cal_read_copy(uint32_t page_addr, struct cal_hdr *hdr_out, struct cal_v2 *out)
{
    uint8_t buf[sizeof(struct cal_hdr) + 64];
    struct cal_hdr h;

    if (nv_read(page_addr, &h, sizeof(h)) != 0 || h.magic != CAL_MAGIC) {
        return false;
    }
    if (h.length > sizeof(buf) - sizeof(h)) {
        return false;                                         /* corrupt length */
    }
    if (nv_read(page_addr + sizeof(h), buf, h.length) != 0 ||
        crc32_ieee(buf, h.length) != h.crc32) {
        return false;
    }

    cal_defaults(out);
    if (h.version == 1 && h.length == sizeof(struct cal_v1)) {
        struct cal_v1 v1;
        memcpy(&v1, buf, sizeof(v1));
        memcpy(out->acc_offset, v1.acc_offset, sizeof(v1.acc_offset));
        memcpy(out->acc_scale,  v1.acc_scale,  sizeof(v1.acc_scale));
        memcpy(out->gyro_bias,  v1.gyro_bias,  sizeof(v1.gyro_bias));
        out->xo_trim = v1.xo_trim;                            /* mic stays uncalibrated: flag clear */
    } else if (h.version == 2 && h.length == sizeof(struct cal_v2)) {
        memcpy(out, buf, sizeof(*out));
    } else {
        return false;                                         /* unknown future version */
    }
    *hdr_out = h;
    return true;
}

/* Load the newest valid copy. Returns 0 on success, -1 if running on defaults. */
int cal_load(struct cal_v2 *out, uint32_t *seq_out)
{
    struct cal_hdr ha, hb;
    struct cal_v2 a, b;
    bool va = cal_read_copy(0, &ha, &a);
    bool vb = cal_read_copy(CAL_PAGE_SIZE, &hb, &b);

    if (va && (!vb || ha.seq >= hb.seq)) {
        *out = a; *seq_out = ha.seq; return 0;
    }
    if (vb) {
        *out = b; *seq_out = hb.seq; return 0;
    }
    cal_defaults(out);
    *seq_out = 0;
    return -1;                               /* report "uncalibrated" in logs + telemetry */
}

/* Store a new record into the older/broken copy, then verify by reading back. */
int cal_store(const struct cal_v2 *in)
{
    struct cal_v2 cur;
    uint32_t cur_seq;
    struct cal_hdr ha;
    struct cal_v2 tmp;
    bool a_valid = cal_read_copy(0, &ha, &tmp);

    (void)cal_load(&cur, &cur_seq);

    /* Write to the page that is NOT holding the current newest record. */
    uint32_t target = (a_valid && ha.seq == cur_seq) ? CAL_PAGE_SIZE : 0;

    struct cal_hdr h = {
        .magic   = CAL_MAGIC,
        .version = CAL_VER_CURRENT,
        .length  = sizeof(*in),
        .seq     = cur_seq + 1,
        .crc32   = crc32_ieee((const uint8_t *)in, sizeof(*in)),
    };

    if (nv_erase_page(target) != 0 ||
        nv_write(target + sizeof(h), in, sizeof(*in)) != 0 ||
        nv_write(target, &h, sizeof(h)) != 0) {      /* header last: a torn write leaves no magic */
        return -1;
    }

    struct cal_hdr vh;
    struct cal_v2 check;
    if (!cal_read_copy(target, &vh, &check) || memcmp(&check, in, sizeof(check)) != 0) {
        return -2;                                    /* station must FAIL this unit */
    }
    return 0;
}
```

핵심 설계 포인트:

- **헤더를 마지막에 쓴다**: payload를 먼저 쓰고 magic이 든 헤더를 나중에 쓰면, 도중에 전원이 나갔을 때 magic이 없어 그 사본은 무효가 되고 다른 사본이 살아 있다. C07 image trailer의 "magic을 마지막에 쓴다"와 같은 원리다. (flash write 단위와 순서 보장은 칩마다 확인한다.)
- **`_Static_assert`로 레이아웃을 고정**: cal 구조체는 스테이션 소프트웨어, MES, 제품 FW가 공유하는 **계약(contract)**이다. 누가 필드를 하나 추가하다 패딩이 바뀌면 컴파일 단계에서 막는다. 필드는 큰 타입부터 또는 명시적 예약 바이트로 정렬해 컴파일러 패딩에 의존하지 않는다.
- **버전별 읽기 경로(마이그레이션)**: EVT에서 v1으로 cal한 기기에 DVT FW(v2)를 올려도 동작해야 한다. 없는 필드는 기본값 + `flags` 비트로 "미보정"을 표시한다. 제품 FW는 flag를 보고 보정 없이 동작하거나 기능을 제한한다.
- **알 수 없는 미래 버전은 거부**: 옛 FW가 새 레이아웃을 잘못 해석하는 것보다 기본값으로 동작하는 편이 안전하다. 대신 이 상황을 로그로 남긴다.
- **CRC는 payload만**: 헤더의 crc 필드 자체를 CRC 계산에서 빼는 가장 단순한 방법이다. CRC는 우연한 손상만 막는다. cal 데이터 변조가 보안 문제라면(예: RF 출력 제한 우회) 쓰기 보호 파티션 + 잠금으로 막는다.
- **read-back 검증 실패는 테스트 fail**: 라인에서 쓰고 확인하지 않은 cal은 없느니만 못하다. 스테이션은 cal 값을 MES에 올리기 전에 기기에서 다시 읽은 값과 비교한다.
- `crc32_ieee`의 `(uint32_t)-(int32_t)(crc & 1u)`는 최하위 비트가 1이면 0xFFFFFFFF, 0이면 0을 만드는 분기 없는 마스크다. 입력 "123456789"에 대한 표준 check 값은 0xCBF43926이다. 단위 테스트로 이 값을 확인한다.

### 6.4 Zephyr라면

- 보호 파티션을 devicetree `fixed-partitions`에 따로 정의하고(예: `factory_partition`), Flash Map API(`flash_area_open`, `flash_area_read`, `flash_area_write`, `flash_area_erase`)로 접근한다.
- 설정 서브시스템(`settings_save_one`)은 편하지만 사용자 설정과 섞이지 않도록 별도 backend/파티션을 쓰거나 raw 파티션 방식을 쓴다.
- 기기 고유 ID는 `hwinfo_get_device_id()`로 읽는다(칩의 factory ID, 예: nRF의 FICR DEVICEID). 제품 시리얼과는 다른 값이다. 둘의 매핑을 MES에 기록한다.

---

## 7. Provisioning — 기기에 신원을 부여한다

### 7.1 무엇을 넣나

| 항목 | 출처 | 저장 | 비고 |
|---|---|---|---|
| 제품 시리얼 | MES가 발급 | OTP 또는 보호 영역, 레이저 각인과 일치 | 모든 추적성의 키 |
| BT/Wi-Fi MAC 주소 | 회사가 IEEE에서 받은 OUI 블록에서 할당 | OTP 또는 무선 칩 영역 | 중복 발급 방지는 MES 책임. 일부 칩은 칩 자체 주소 사용 |
| 기기 개인키 | **기기 안에서 생성** (또는 secure element 내부) | secure element / TrustZone secure storage | 밖으로 나오지 않음 |
| 기기 인증서 | factory CA가 기기의 CSR에 서명 | 보호 영역 | 클라우드 인증(mTLS), 기기 attestation |
| 부트 키 해시, lifecycle | 보안 절차 | OTP | C07과 연결 |
| 지역/SKU 설정 | MES 주문 정보 | 보호 영역 | 규제(RF 채널, 출력) 영향 |

### 7.2 키·인증서 provisioning 흐름

```
  DUT (factory mode)                  Station                         Factory CA / HSM (+ MES)
       |  KEYGEN                          |                                    |
       |<---------------------------------|                                    |
       |  기기 안에서 ECC P-256 키쌍 생성    |                                    |
       |  (개인키는 SE/secure storage 안)   |                                    |
       |  CSR(공개키 + serial) ------------>|                                    |
       |                                  |---- CSR + serial + station id ---->|
       |                                  |                                    | 정책 검사(중복 serial?)
       |                                  |<------- device certificate --------| 발급 기록
       |<------------ CERT WRITE ---------|                                    |
       |  인증서 저장 + 공개키 일치 확인      |                                    |
       |  ATTEST(nonce) -> 서명 ------------>| 인증서로 서명 검증 (end-to-end 확인) |
       |                                  |---- PASS + cert fingerprint ------->| MES 기록
```

- **개인키를 기기 밖에서 만들어 주입하지 않는다.** 주입 방식은 스테이션 PC나 네트워크에서 키가 유출될 수 있다. 부득이 주입해야 한다면 암호화된 채널과 HSM 기반 도구를 쓴다.
- secure element(예: Microchip ATECC608, NXP SE050) 벤더는 **사전 provisioning 서비스**(칩 공장에서 키와 인증서를 미리 넣어 줌)를 제공하기도 한다. 이 경우 라인에서는 인증서 체인 확인만 한다.
- 스마트홈 표준 Matter는 모든 기기에 **DAC(Device Attestation Certificate)**를 요구하고, PAI/PAA로 이어지는 체인을 사용한다. 이런 요구는 provisioning 설계를 표준이 정해 주는 예다.
- factory CA의 발급 권한은 라인 네트워크에서만, 발급 수량 한도와 감사 로그를 두고 운영한다. 공장이 외주(ODM/EMS)일 때 특히 중요하다.

---

## 8. 추적성 (Traceability)과 MES

### 8.1 MES가 기록하는 것

MES(Manufacturing Execution System)는 라인의 데이터베이스이자 교통 경찰이다.

```
 serial HK1A23456789
   +-- PCBA serial  PB-7731-0042   (SMT lot, 리플로우 프로파일, AOI 결과)
   +-- 주요 부품 lot: SoC, MCU, flash, 배터리 셀, 마이크, 안테나
   +-- station 기록 (시간순)
   |     ICT        PASS  fixture#3  2026-11-02 08:14
   |     BOARD_FCT  FAIL  fixture#7  (I2C_IMU_WHOAMI timeout)
   |     BOARD_FCT  PASS  fixture#2  (retest)
   |     AUDIO_CAL  PASS  mic0=-0.42dB mic1=+0.31dB
   |     RF_RAD     PASS  ...
   |     PROVISION  PASS  cert fp=3A:9F:...
   |     LOCK       PASS  approtect=on fuse=secure
   +-- FW 버전: test FW 0.9.3, 출하 FW 1.4.0+12
   +-- cal 데이터 사본 (전체 값)
```

- **route control(interlock)**: 이전 스테이션을 PASS하지 않은 기기는 다음 스테이션이 시작을 거부한다. 스테이션이 serial을 읽고 MES에 "이 기기가 여기 올 자격이 있나?"를 묻는다. FW는 serial을 빠르고 확실하게 보고해야 한다(`VER` 응답에 sn 포함).
- **재시험 기록**: 위 예시처럼 retest로 통과한 기기는 필드 불량 가능성이 더 높을 수 있다. 재시험 통과율은 fixture 문제와 제품 문제를 가르는 데이터다.
- **필드 불량 역추적**: 반품 기기의 serial로 같은 부품 lot, 같은 fixture, 같은 날 생산된 기기를 찾아 영향 범위를 정한다.

### 8.2 FW가 로그로 남겨야 할 것

- 명령/응답 원문(스테이션이 저장), FW 버전과 빌드 해시, 측정 raw 값(판정 결과만이 아니라), 타임아웃이 난 명령과 그 시점의 FW 상태(에러 코드), 리셋 원인
- 로그는 **파싱 가능한 형식**(key=value 또는 JSON 한 줄)으로. 나중에 수십만 대 데이터를 분석할 때 차이가 크다.

> Don 경험과 연결: SSD 양산에서 telemetry와 error reporting을 설계한 경험은 "기기가 스스로 진단 정보를 구조화해서 내보내는 설계"다. 라인의 MES 로그와 필드의 telemetry를 같은 스키마로 설계하면 "라인에서 본 분포와 필드 불량을 serial로 조인"할 수 있다. 면접에서 이 연결을 말하면 시스템 관점이 드러난다.

---

## 9. 출하 전 잠금 (End-of-line lock)

라인 마지막에서 기기를 "공장 상태"에서 "소비자 상태"로 바꾼다. 이 단계는 되돌릴 수 없는 작업이 많아서 **순서와 검증**이 핵심이다.

```
 1. 모든 기능 테스트·cal·provisioning PASS 확인 (MES route check)
 2. 출하 FW 기록 (서명된 이미지, 운영 키) -> 버전 확인
 3. cal/provisioning 파티션 쓰기 보호 설정
 4. secure boot 활성화: 운영 키 해시 OTP, 보안 카운터 초기값, (필요 시) 개발 키 폐기
 5. factory mode 비활성화: lifecycle 플래그 "shipped" 로 전환
 6. 디버그 포트 잠금 (APPROTECT / RDP / JTAG disable fuse, 칩마다 다름)
 7. 검증: 재부팅 -> 출하 FW 정상 부팅, factory 명령 거부 확인, SWD 접속 실패 확인
 8. MES 에 LOCK PASS 기록 -> 포장
```

- **4~6은 비가역**이다. 순서를 잘못 잡으면(예: 디버그를 먼저 잠그고 나서 FW 기록이 실패) 수리 불가능한 기기가 된다.
- 7번 검증을 빼먹는 경우가 많다. "잠금 명령이 OK를 반환했다"와 "실제로 잠겼다"는 다르다. 스테이션이 디버거로 접속을 시도해서 **실패하는 것을 확인**해야 한다.
- 개발 중 기기(EVT/DVT)는 잠그지 않거나 개발 키로 잠근다. 출하용 잠금 절차는 PVT에서 처음부터 끝까지 리허설한다.
- **RMA 경로**: 잠긴 기기를 분석하려면 서명된 토큰 기반 인증 디버그나, 제한된 진단 명령을 미리 설계해 둔다. 없으면 반품 분석이 불가능해진다.

---

## 10. Don의 Apple factory test-node 경험을 이 JD에 연결하는 법

Hark는 1세대 하드웨어를 만드는 스타트업이다. 면접관이 가장 듣고 싶은 것은 "이 사람이 오면 우리 팩토리 FW와 테스트 인프라를 처음부터 세울 수 있나"다. 아래 틀로 경험을 재구성한다(내부 기밀 수치나 Apple 고유 도구 이름은 말하지 않고 일반화한다).

| Apple 경험 (일반화) | Hark에서의 의미 | 면접에서 쓸 표현 |
|---|---|---|
| 새 무선 칩 bring-up → NPI → MP | EVT부터 MP까지 FW와 테스트가 함께 성숙하는 흐름을 이미 겪음 | "I've carried new silicon from first power-on to mass production." |
| factory test-node 아키텍처 설계 | 스테이션 구성, 명령 체계, 커버리지와 시간의 균형 | "I designed how test nodes exercise the chip and what each node owns." |
| 스트레스 시나리오로 latent defect 사전 검출 | limit을 사양이 아닌 분포로 조이기, run-in 설계 | "We caught marginal parts before MP by stressing interfaces, not just checking pass/fail at nominal." |
| 인터페이스 장애 root cause (PCIe/I2C/SPMI/RFFE) | 라인 fail 분석: fixture 문제 vs 제품 문제 구분 | "When a station fails, the first question is whether it's the fixture, the test, or the unit." |
| SSD 양산 FW, telemetry | 라인 로그와 필드 telemetry를 같은 스키마로 | "I design the factory log and field telemetry so they can be joined by serial." |

스타트업 관점으로 "처음 90일에 할 일"을 말할 수 있으면 좋다.

1. EVT 전에 회로도 리뷰로 test point·strap·디버그 포트 확보
2. factory 명령 프로토콜 v1과 cal 데이터 계약(struct + 버전 규칙)을 문서로 고정
3. EVT에서 모든 raw 측정을 덤프하는 스크립트로 데이터 수집 시작
4. DVT 데이터로 limit·cal 필요 항목 결정, CM(위탁 생산업체) 테스트 엔지니어와 스테이션 매핑
5. PVT 전에 provisioning·잠금 절차를 end-to-end 리허설

---

## 11. 흔한 실수와 증상

| 실수 | 증상 | 원인 | 고치는 법 |
|---|---|---|---|
| factory mode에서 일반 로그가 같은 UART로 출력 | 스테이션 파싱 에러, 간헐 fail | 응답 줄 사이에 로그가 끼어듦 | factory mode에서 로그 채널 분리 또는 억제, 응답 접두사 규칙 |
| 측정 중 무선/주기 태스크가 동작 | 전류·마이크·RF 값이 튀어 재시험 증가 | 제품 동작이 factory mode에서 꺼지지 않음 | 테스트 모드 진입 시 제품 서비스 정지 목록 관리 |
| cal 데이터를 사용자 설정과 같은 저장소에 | factory reset 후 센서 오차, 마이크 레벨 이상 | 초기화가 cal까지 지움 | 전용 쓰기 보호 파티션, MES 백업 |
| cal 구조체에 필드 추가하며 레이아웃 변경 | 구버전 cal 기기에서 엉뚱한 값 | 버전 필드/마이그레이션 없음, 패딩 변화 | 버전 + 길이 헤더, `_Static_assert`, 버전별 읽기 경로 |
| cal 계수 limit 검사 없음 | 파손 센서가 "보정되어" 출하 | 계수 범위를 테스트 항목으로 안 봄 | 계수 자체에 limit 설정 |
| 고정 sleep으로 안정화 대기 | 테스트 시간 과다 또는 간헐 fail | 기기마다 안정화 시간 다름 | FW가 안정화 판단 후 응답, 타임아웃은 스테이션 |
| 잠금 결과 미검증 | 필드에서 SWD로 flash 덤프 가능 | 잠금 명령 OK만 확인 | 재부팅 후 디버거 접속 실패를 스테이션이 확인 |
| 개인키를 스테이션 PC에서 생성·주입 | 키 유출 위험, 감사 불합격 | 편의상 결정 | 기기 내부 생성 + CSR, 또는 SE 사전 provisioning |
| golden unit 점검 없음 | 어느 날부터 특정 fixture만 fail 급증 | pogo pin 마모, 케이블 손상 | 교대마다 golden unit 실행, fixture별 수율 모니터링 |
| 마이크 dBFS 정의 불일치 | 모든 기기가 약 3 dB 차이로 fail | 사인파 full-scale vs DC full-scale 기준 혼동 | 계산식을 스테이션과 문서로 합의, golden unit으로 교차 확인 |

---

## 12. 면접에서 이렇게 말한다

**Q.** Walk me through how you'd build factory test firmware for a new wearable.

**A.** 드라이버를 제품 FW와 공유하는 factory mode(또는 test FW)를 만들고, 한 줄 명령·`OK/ERR` 응답의 버전된 프로토콜을 정의한다. EVT에선 raw 데이터를 최대한 뽑고, DVT 데이터로 limit과 cal 항목을 정하고, PVT에서 테스트 시간을 줄이고 provisioning·잠금을 리허설한다. cal 데이터는 버전·CRC·이중 사본으로 보호 파티션에 저장하고 MES에 백업한다.

> "I'd start with a factory mode that reuses the product drivers, so what we test is what we ship, and a simple versioned line protocol — one command per line, replies starting with OK or ERR, raw values as key=value so the station applies limits. In EVT we dump everything; in DVT we use the distributions to set limits and decide what needs calibration; in PVT we cut test time and rehearse provisioning and lock end to end. Calibration goes into a write-protected partition with a version, a CRC and two copies, and every value is also stored in the MES by serial."

**Q.** What's the difference between EVT, DVT, and PVT?

**A.** EVT는 설계가 전기·기능적으로 동작하는지, DVT는 실제 외형·재료로 신뢰성과 인증을 통과하는지, PVT는 양산 라인이 목표 속도와 수율로 도는지를 검증한다. FW 입장에선 EVT는 데이터 수집, DVT는 limit·cal 확정, PVT는 시간 최적화와 잠금 절차 확정이다.

> "EVT asks whether the design works electrically and functionally. DVT asks whether the production-intent design, with real enclosures and materials, passes reliability and certification. PVT asks whether the real line can build it at target rate and yield. For firmware, EVT is about collecting data, DVT is about fixing limits and calibration methods, and PVT is about test time, provisioning, and the lock sequence."

**Q.** How do you store calibration data so it survives OTA updates and power loss?

**A.** 제품 파티션·사용자 설정과 분리된 쓰기 보호 파티션에 헤더(magic, 버전, 길이, 시퀀스, CRC)가 붙은 레코드를 두 사본으로 둔다. 새 값은 오래된 사본에 쓰고 헤더를 마지막에 쓴 뒤 다시 읽어 확인한다. 읽을 때는 유효한 사본 중 시퀀스가 큰 것을 쓰고, 버전별 읽기 경로로 옛 레이아웃도 지원한다. 모든 값은 MES에도 저장한다.

> "It lives in its own write-protected partition that OTA and factory reset never touch. Each record has a magic, a layout version, a length, a sequence number and a CRC, and I keep two copies. A new record goes into the older copy, header written last, then read back and compared. At boot I take the valid copy with the highest sequence, and a version-specific reader migrates old layouts. And every value is backed up in the MES, so an RMA unit can be restored."

**Q.** How would you calibrate microphone sensitivity on the line?

**A.** 음향 챔버에서 94 dB SPL, 1 kHz 기준음을 만들고 기기가 일정 시간 캡처해 RMS를 dBFS로 보고한다. 스테이션이 목표 감도와의 차이로 gain trim을 계산해 기록하고, 다시 측정해 검증한다. 멀티 마이크는 채널 간 매칭이 더 중요하고, 트림 값이 너무 크면 음향 포트 막힘 같은 조립 불량으로 fail시킨다.

> "In an acoustic chamber we play a 1 kHz tone at 94 dB SPL, which is 1 pascal, calibrated with a reference mic. The device captures a few hundred milliseconds and reports the RMS level in dBFS. The station computes the gain trim against the target sensitivity, writes it, and re-measures. For a mic array, channel-to-channel matching matters more than absolute level, and if the required trim is out of range we fail the unit — that usually means a blocked acoustic port or a bad assembly, not something to calibrate away."

**Q.** How do you balance test coverage against test time?

**A.** 테스트 시간은 곧 fixture 수와 비용이다(takt time 계산). 필드·DVT 데이터로 각 테스트가 실제로 잡은 불량을 보고, 한 번도 fail이 없던 항목은 샘플링으로 돌리거나 다른 테스트에 흡수한다. FW 쪽에선 부팅 경로 단축, 측정 병렬화, 명령 묶기, 고정 대기 제거로 시간을 줄인다. escape 비용이 크면 커버리지를 우선한다.

> "Test time converts directly into fixtures and floor space, so I start from the takt time. Then I look at what each test has actually caught in DVT and PVT: tests that never fail become sampling audits or get merged, and tests that catch field-relevant defects stay even if they're slow. On the firmware side I cut time by booting straight into the command handler, running independent measurements in parallel, batching commands, and replacing fixed delays with readiness checks. An escape costs far more than a second of test time, so coverage wins when it's close."

**Q.** How do you provision device keys and certificates securely?

**A.** 개인키는 기기(또는 secure element) 안에서 생성하고 CSR만 밖으로 보낸다. factory CA가 serial을 확인하고 인증서를 발급하면 기기에 기록하고, nonce 서명으로 end-to-end 검증한 뒤 MES에 fingerprint를 남긴다. CA는 라인 네트워크 전용, 발급 한도와 감사 로그를 둔다.

> "The private key is generated inside the device or its secure element and never leaves it. The device sends a CSR, the factory CA checks the serial against the MES and issues a certificate, and we write it back and prove it end to end by having the device sign a nonce that the station verifies. The CA is reachable only from the line network, with issuance limits and audit logs, which matters a lot when the factory is a contract manufacturer."

**Q.** Tell me about your factory test experience at Apple.

**A.** (기밀 수치 없이) 새 무선 칩을 bring-up에서 MP까지 끌고 가면서 test-node 아키텍처를 설계했다. 각 노드가 무엇을 책임지는지, 스트레스 시나리오로 nominal pass/fail에서 안 보이는 marginal 부품을 어떻게 잡았는지, fail이 나면 fixture·테스트·기기 중 무엇의 문제인지 가르는 절차를 말한다.

> "I owned the factory test-node architecture for a new wireless chipset, from bring-up through NPI to mass production. I defined what each test node exercised and owned, and added stress scenarios on the host interfaces so marginal units failed on the line instead of in the field. When a node failed, we had a clear triage path — fixture, test, or unit — backed by structured logs. That's exactly the kind of infrastructure a first-generation device needs, and I'd bring the same discipline here from EVT onward."

---

## 13. 직접 해보기

### 13.1 실습 1 — factory 명령 파서를 PC에서 돌리고 테스트하기

```sh
# 3.2 절 코드를 factory_cmd.c 로 저장
cc -std=c11 -Wall -Wextra -DHOST_TEST -D_POSIX_C_SOURCE=200809L factory_cmd.c -o fc
./fc
# 기대 출력: VER 응답, IMU 응답, SN OK, ERR 2 unknown_command, ERR 3 IMU READ <n>

# 긴 줄 / CR 만 있는 입력 / 빈 줄 등 엣지 케이스를 script 문자열에 추가해 보기
```

`_POSIX_C_SOURCE`는 `strtok_r` 선언을 위해 필요하다(플랫폼에 따라 없어도 된다).

### 13.2 실습 2 — cal 레코드 코드에 가짜 flash를 붙여 전원 차단 시뮬레이션

1. 6.3절 코드와 함께 `nv_read`/`nv_write`/`nv_erase_page`를 8 KB 배열로 구현한다(erase는 0xFF로 채움, write는 기존 비트와 AND 해서 flash의 1→0 특성을 흉내).
2. `nv_write`에 "N번째 호출에서 절반만 쓰고 멈춤" 스위치를 넣어 `cal_store` 도중 전원 차단을 흉내 낸다.
3. 이후 `cal_load`가 항상 이전 사본 또는 새 사본 중 하나를 온전히 반환하는지 확인한다.
4. `crc32_ieee("123456789")`가 0xCBF43926인지 확인한다.

```sh
cc -std=c11 -Wall -Wextra caldata.c fake_flash.c test_cal.c -o test_cal && ./test_cal
```

### 13.3 실습 3 — nRF52840 DK + Zephyr shell로 factory 명령 만들기

```sh
# shell 샘플을 기반으로 3.3 절의 imu 명령을 추가
west build -b nrf52840dk/nrf52840 zephyr/samples/subsys/shell/shell_module -d build_shell
west flash -d build_shell
# 시리얼 터미널 (115200) 에서:
#   uart:~$ imu read 100
#   uart:~$ hwinfo devid          (CONFIG_HWINFO_SHELL=y 필요, 기기 고유 ID)
```

- `prj.conf`에 `CONFIG_HWINFO=y`, `CONFIG_HWINFO_SHELL=y`를 추가해 칩 고유 ID를 읽어 본다. 이 값을 제품 시리얼과 매핑하는 것이 MES의 일이다.
- 센서가 없으면 nRF52840의 내부 온도 센서(`nordic,nrf-temp` 호환 노드, `CONFIG_SENSOR=y`)를 읽는 `temp read` 명령을 만들고, offset cal 값을 settings에 저장·적용해 본다.

### 13.4 실습 4 — UICR에 "시리얼" 쓰고 읽기 (OTP 흉내)

nRF52840의 UICR에는 고객용 레지스터(CUSTOMER[n])가 있다. erase 없이는 한 번만 쓸 수 있으므로(erase는 전체 UICR 지우기) OTP처럼 연습할 수 있다. 주소는 nRF52840 Product Specification의 UICR 절에서 확인한다(CUSTOMER[0]은 UICR 기준 오프셋 0x080).

```sh
nrfjprog --memrd 0x10001080 --n 16             # 비어 있으면 0xFFFFFFFF
nrfjprog --memwr 0x10001080 --val 0x31414B48   # 예시 값
nrfjprog --memrd 0x10001080 --n 4
# 다시 다른 값을 써 보면 1->0 비트만 바뀌는 것 관찰 (flash 특성)
# nrfjprog 는 nrfutil 로 대체되는 중이므로 설치된 도구에 맞춰 명령 확인
```

주의: UICR에는 APPROTECT, 리셋 핀 설정(PSELRESET), NFC 핀 설정 같은 중요한 레지스터도 있다. CUSTOMER 영역 외에는 건드리지 않는다.

---

## 14. 용어 사전

| 용어 | 뜻 | 한 줄 설명 |
|---|---|---|
| NPI | New Product Introduction | 설계에서 양산까지 넘기는 전체 과정 |
| EVT / DVT / PVT | Engineering / Design / Production Validation Test | 설계 동작 / 양산 설계 신뢰성 / 라인 능력 검증 단계 |
| MP | Mass Production | 양산, ramp |
| CM / EMS / ODM | Contract Manufacturer 등 | 위탁 생산 업체 |
| SMT | Surface Mount Technology | 부품 실장 공정 |
| AOI | Automated Optical Inspection | 카메라 기반 실장 검사 |
| ICT | In-Circuit Test | bed-of-nails로 net 단위 전기 검사 |
| FCT | Functional (Circuit) Test | 전원 켜고 기능 동작 검사 |
| FATP | Final Assembly, Test & Pack | 최종 조립·테스트·포장 |
| OQC | Outgoing Quality Control | 출하 전 샘플 품질 검사 |
| DUT | Device Under Test | 시험 대상 기기 |
| fixture | 치구 | DUT를 고정하고 pogo pin/계측기를 연결하는 기구 |
| golden unit | 기준 기기 | fixture·스테이션 정상 여부 확인용 |
| UPH / takt time | Units Per Hour / 1대당 허용 시간 | 라인 속도 지표 |
| FPY / RTY | First Pass / Rolled Throughput Yield | 첫 시도 수율 / 스테이션 수율의 곱 |
| Cpk | Process capability index | limit 대비 분포 여유 |
| GR&R | Gauge Repeatability & Reproducibility | 측정 시스템 변동 평가 |
| guard band | 판정 여유 | 측정 불확도만큼 limit 축소 |
| escape / false fail | 불량 통과 / 양품 불합격 | 테스트 오류의 두 방향 |
| MES | Manufacturing Execution System | 라인 데이터·route 관리 시스템 |
| run-in / burn-in | 장시간 동작 시험 | 초기 고장(infant mortality) 선별 |
| provisioning | 신원 부여 | 시리얼, MAC, 키, 인증서 기록 |
| CSR | Certificate Signing Request | 공개키 + 신원 정보로 인증서 발급 요청 |
| DAC | Device Attestation Certificate | Matter 기기 인증서 |
| DTM | Direct Test Mode | BLE RF 시험용 표준 모드 |
| XO trim | 크리스털 주파수 보정 | 부하 커패시턴스 조정 code |
| dBFS | dB relative to Full Scale | 디지털 오디오 레벨 단위 |
| 94 dB SPL | 1 Pa 음압 | 마이크 감도 측정 기준 |
| RMA | Return Merchandise Authorization | 반품 분석 흐름 |

---

## 15. 요약 & 체크리스트

Factory FW의 목표는 라인이 정한 시간 안에 조립 불량을 잡고(test), 부품 편차를 보정하고(calibration), 신원을 부여하고(provisioning), 보안을 잠그는(lock) 것이다. 이를 위해 제품 드라이버를 재사용하는 factory mode, 버전된 텍스트 명령 프로토콜, 버전·CRC·이중 사본으로 보호된 cal 데이터, 기기 내부 키 생성 기반 provisioning, 되돌릴 수 없는 작업을 마지막에 두고 검증까지 하는 잠금 절차가 필요하다. 모든 결과는 serial 기준으로 MES에 남아 필드 불량을 역추적할 수 있어야 한다. EVT는 데이터 수집, DVT는 limit·cal 확정, PVT는 시간·절차 확정, MP는 수율 최적화라는 흐름 위에서 Don의 Apple test-node 경험을 "처음부터 세울 수 있는 사람"의 이야기로 풀어 낸다.

- [ ] SMT → AOI → ICT → board FCT → FATP → OQC 흐름을 그리고 각 단계에서 FW의 역할을 말할 수 있다
- [ ] EVT/DVT/PVT/MP의 질문과 FW 산출물을 표로 설명할 수 있다
- [ ] factory mode 진입 방법 3가지와 각각의 보안 위험을 말할 수 있다
- [ ] 한 줄 명령·`OK/ERR` 응답 프로토콜의 설계 원칙과 파서 구조(디스패치 테이블, 오버플로 처리)를 C로 설명할 수 있다
- [ ] 2점 선형 cal, 가속도계 6면 cal, 마이크 94 dB SPL cal을 식으로 설명할 수 있다
- [ ] cal 데이터 레코드(magic, version, length, seq, CRC, 이중 사본, 헤더 마지막 쓰기)를 화이트보드에 그릴 수 있다
- [ ] 키·인증서 provisioning 흐름(기기 내부 키 생성, CSR, factory CA, nonce 검증)을 설명할 수 있다
- [ ] FPY, RTY, takt time, Cpk, GR&R, escape vs false fail을 정확히 정의할 수 있다
- [ ] 출하 전 잠금 순서와 "잠금 검증"이 왜 필요한지 설명할 수 있다
- [ ] Apple test-node 경험을 기밀 없이 영어 60초로 말할 수 있다

---

## 참고 자료

- [Zephyr: Shell 서브시스템](https://docs.zephyrproject.org/latest/services/shell/index.html)
- [Zephyr: Flash map (flash_area API)](https://docs.zephyrproject.org/latest/services/storage/flash_map/flash_map.html)
- [Zephyr: Settings 서브시스템](https://docs.zephyrproject.org/latest/services/storage/settings/index.html)
- [Zephyr: Hardware Information (hwinfo)](https://docs.zephyrproject.org/latest/hardware/peripherals/hwinfo.html)
- [Nordic nRF52840 Product Specification (UICR, FICR, APPROTECT)](https://docs.nordicsemi.com/bundle/ps_nrf52840/page/keyfeatures_html5.html)
- [Bluetooth Core Specification (Direct Test Mode 포함)](https://www.bluetooth.com/specifications/specs/core-specification/)
- [Matter: Connectivity Standards Alliance](https://csa-iot.org/all-solutions/matter/)
- [Microchip ATECC608 제품 페이지](https://www.microchip.com/en-us/product/atecc608b)
- NXP EdgeLock SE050 secure element — nxp.com에서 "SE050" 검색 (제품 페이지 주소가 자주 바뀜)
- [PSA Certified (기기 보안·lifecycle 개념)](https://www.psacertified.org/)
- [CRC 카탈로그 (CRC-32 check 값 확인)](https://reveng.sourceforge.io/crc-catalogue/all.htm)
- [NIST/SEMATECH e-Handbook: Process Capability (Cpk)](https://www.itl.nist.gov/div898/handbook/pmc/section1/pmc16.htm)
