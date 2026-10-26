# 🛩️ R03 Betaflight / INAV 내부 구조와 테스트 포인트
> Betaflight master 소스를 직접 열어 boot → scheduler → gyro/filter/PID → mixer → DShot, CRSF → RX → failsafe, MSP/CLI/blackbox, unit test · SITL · CI까지 실제 경로와 함수 이름으로 정리하고, INAV의 HITL(MSP_SIMULATOR)과 함께 "Betaflight 포크를 어떻게 테스트하나"를 FTE 관점에서 설계한다.

---

## 0. 이 단원의 근거

이 단원의 경로 · 함수 이름 · 숫자는 **직접 clone한 소스**에서 확인했다. Gemini 대화와 K05 계획서에 있던 경로 중 틀린 것은 본문에서 `> ⚠️ Gemini 정정:` 으로 바로잡았다.

| 저장소 | clone한 commit | commit 날짜 | 비고 |
|---|---|---|---|
| `betaflight/betaflight` (master) | `d593400a05a5e142c0f6f83cc45c237ef078c634` | 2026-10-25 03:53 +1100 | `--depth 1`, 빌드 스크립트 실행 안 함 |
| `iNavFlight/inav` (master) | `bc76329fcffff96f5a52881bcdc0ff65f7426e37` | 2026-10-23 01:14 -0500 | `--depth 1`, 빌드 스크립트 실행 안 함 |

- master는 계속 바뀐다. 릴리스 브랜치(예: `4.5-maintenance`)는 경로 · 이름이 조금 다를 수 있다 → Neros 포크가 어느 버전 기반인지는 [추정]도 못 한다. 포크 버전을 알게 되면 같은 grep을 그 태그에서 다시 돌릴 것
- 코드 인용은 짧게(20줄 미만) 잘랐다. 전체는 해당 파일에서 직접 읽는다
- MSP 클라이언트 예제: [../code/msp_client.py](../code/msp_client.py) (표준 라이브러리만, `python3 -I msp_client.py` 로 자체 테스트 8개 통과 확인)

연결 단원: 시스템 전체 그림은 [R02](2026-10-24_R02_drone_system_and_open_source_map.md), CRSF · DShot · MSP 프로토콜 자체는 [R05](2026-10-24_R05_rc_link_protocols_elrs_edgetx.md), PX4/ArduPilot 비교는 [R04](2026-10-24_R04_px4_ardupilot_and_their_test_ecosystems.md), 테스트 피라미드는 [R07](2026-10-24_R07_test_methodology_pyramid.md), 벤치 하드웨어는 [R08](2026-10-24_R08_hil_bench_design_instruments.md), 플랫폼 설계는 [R09](2026-10-24_R09_scalable_reusable_test_platform.md).

---

## 1. 요약

- Betaflight는 RTOS가 없다. `main()` → `initPhase1/2/3()` → `run()` 의 무한 루프가 `scheduler()` 를 계속 부른다
- 스케줄러는 **gyro에 lock된 시간 슬롯** 방식이다. 매 gyro 주기(STM32 F4/F7/H7/G4 기본 125 µs = 8 kHz)마다 `TASK_GYRO` → (조건부) `TASK_FILTER` → (조건부) `TASK_PID` 를 **스케줄러 밖에서 먼저** 실행한다. 이 셋은 `TASK_PRIORITY_REALTIME = -1`
- 남는 시간에 나머지 task를 **dynamic priority = 1 + staticPriority × 나이(주기 수)** 로 골라 한 개만 실행한다. 단 "다음 gyro 슬롯 전에 끝날 것 같은" task만 고른다 (예상 실행 시간 + guard margin)
- PID task 안에서 `subTaskRcCommand` → `subTaskPidController` → `subTaskMotorUpdate`(mixer + DShot 출력) → `subTaskPidSubprocesses`(blackbox 기록) 순서로 돈다. 즉 **blackbox는 PID 루프 안에서 기록**된다
- CRSF는 UART **RX 인터럽트 콜백** `crsfDataReceive()` 에서 바이트 단위로 조립된다. 프레임 경계는 sync byte가 아니라 **시간 간격**(`frameTimeNeededUs`, 기본 1748 µs)으로 잡는다. CRC는 CRC-8/DVB-S2(poly 0xD5), type+payload 범위
- RX loss 판정: 패킷 없음 150 ms(`RXLOSS_TRIGGER_INTERVAL`) → stage 1(채널 hold/rxfail 값) → `failsafe_delay`(기본 1.5 s) 후 stage 2(`failsafePhase_e` 상태 머신). failsafe 검사는 RX task가 아니라 **스케줄러 본체에서 10 ms마다** 돈다
- 테스트 자산은 이미 많다: googletest 기반 unit test 100여 개(`src/test/unit/*.cc`), `make test`, GitHub Actions CI(`make checks` + `make test-all` + 전 타깃 빌드), SITL 플랫폼(`src/platform/SIMULATOR`), 그리고 **upstream에 들어온 Python SITL end-to-end harness** `src/test/sitl/sitl_harness.py`
- INAV는 Betaflight 계열이지만 스케줄러 · 내비게이션이 다르고, **MSP_SIMULATOR(0x201F)** 로 진짜 FC 보드에 센서 값을 넣는 **HITL**을 공식 지원한다. Betaflight에는 같은 MSP 명령이 없다

---

## 2. 왜 중요한가 (FTE 관점)

- Neros Flight SW 공고가 "Betaflight internals — modes, RC/mixer path, or a maintained fork"를 우대하고, Test 조직이 SITL/HITL을 맡는다고 적었다 [확인됨 공고, K05]. 그러니 Betaflight 포크의 **내부 타이밍 · 경로를 아는 테스트 엔지니어**가 필요하다 [추정]
- 온사이트에서 Michael이 물은 "최우선 task가 무엇이고 왜인가"는 이 단원 4절의 `scheduler()` 코드 그 자체다 [K05]
- FTE가 Betaflight에서 측정할 것은 대부분 **이미 펌웨어 안에 계측점이 있다**: `tasks` CLI(task별 rate/max/avg/late), `status`(CPU · cycle time · arming disable flags), `debug_mode`(SCHEDULER_DETERMINISM, RX_TIMING, FAILSAFE, DSHOT_RPM_ERRORS 등), blackbox. 새 계측을 만들기 전에 이걸 읽을 줄 알아야 한다
- 포크를 유지하는 회사의 가장 큰 위험은 **upstream rebase 때 생기는 조용한 회귀**다(타이밍 · failsafe · 설정 마이그레이션). 이걸 잡는 게 FTE의 핵심 가치 중 하나다 [추정]

---

## 3. 전체 구조 — boot에서 루프까지

### 3.1 boot 순서 (실제 함수)

`src/main/main.c` 의 `main()` 은 짧다.

```c
int main(int argc, char * argv[])
{
    ...
    systemInit();
    initPhase1();
    initPhase2();
    ...                      // USB MSC / VCP 처리
    initPhase3();
    ...
    run();                   // never returns
}

void FAST_CODE run(void)
{
    while (true) {
        scheduler();
    }
}
```

`initPhase1/2/3()` 는 `src/main/fc/init.c` 에 있다. 확인한 주요 호출:

| 단계 | 함수 (init.c 안) | 하는 일 |
|---|---|---|
| initPhase1 | `tasksInitData()` | task 구조체 초기화 (다른 init이 task를 건드리기 전에) |
| initPhase1 | `initEEPROM()`, `readEEPROM()` | 설정(parameter group) 로드, `SYSTEM_STATE_CONFIG_LOADED` |
| initPhase2 | `mixerInit()`, `motorDevInit()` | mixer · 모터 드라이버(DShot 등), `SYSTEM_STATE_MOTORS_READY` |
| initPhase3 | `sensorsAutodetect()` | gyro/acc/baro/mag 감지. 실패 시 처리 |
| initPhase3 | `gyroInitFilters()`, `pidInit()`, `mixerInitProfile()` | 필터 · PID 계수 준비 |
| initPhase3 | `imuInit()`, `failsafeInit()`, `rxInit()` | 자세 추정 · failsafe · 수신기 |
| initPhase3 | `blackboxInit()`, `batteryInit()`, `mspInit()` | 로그 · 배터리 · MSP |
| initPhase3 | `setArmingDisabled(ARMING_DISABLED_BOOT_GRACE_TIME)` | 부팅 직후 arming 금지 |
| initPhase3 | `tasksInit()` | task 활성화 · 주기 재설정, gyro 있으면 `schedulerEnableGyro()` (tasks.c) |
| initPhase3 끝 | `systemState \|= SYSTEM_STATE_READY` | 준비 완료 |

### 3.2 구조 그림

```text
 power-on
    │
    ▼
 main()  [src/main/main.c]
    │  systemInit()
    │  initPhase1()  ── tasksInitData, EEPROM 읽기
    │  initPhase2()  ── mixerInit, motorDevInit
    │  initPhase3()  ── sensors, filters, PID, rx, failsafe, blackbox, msp, tasksInit
    ▼
 run(): while(true) scheduler();     [src/main/scheduler/scheduler.c]
    │
    │  ┌───────────── gyro 슬롯마다 (기본 125 µs) ─────────────┐
    │  │ 1) 슬롯 경계까지 busy-wait (cycle counter)              │
    │  │ 2) TASK_GYRO   taskGyroSample()   (항상)               │
    │  │ 3) TASK_FILTER taskFiltering()    (gyroFilterReady)    │
    │  │ 4) TASK_PID    taskMainPidLoop()  (pidLoopReady)       │
    │  │ 5) rxFrameCheck()  ← 새 RX 프레임 있나 (ELRS 고려)       │
    │  │ 6) 10 ms마다 failsafeCheckDataFailurePeriod()          │
    │  │              + failsafeUpdateState()                    │
    │  │ 7) gyro EXTI와 skew 보정 (GYRO_LOCK_COUNT=50)            │
    │  └────────────────────────────────────────────────────────┘
    │  남은 시간 > CHECK_GUARD_MARGIN_US(2 µs) 이면:
    │     모든 non-realtime task의 dynamicPriority 갱신
    │     → "시간 안에 끝날" 가장 높은 task 1개 실행 (RX, SERIAL, OSD, ...)
    ▼
 (다시 scheduler())
```

> ⚠️ Gemini 정정: Gemini는 "자체 협동형(Cooperative) 스케줄러가 8 kHz로 SPI 자이로를 읽는다"까지만 말했다. 실제로는 (1) gyro/filter/PID는 **스케줄러 선택 로직 밖에서** 슬롯마다 먼저 돈다(`TASK_PRIORITY_REALTIME = -1`, 주석 "Task will be run outside the scheduler logic"), (2) 스케줄러가 **gyro 인터럽트(EXTI) 시각에 맞춰 자기 타이밍을 끌어당긴다**(gyro lock). 8 kHz는 STM32 F4/F7/H7/G4/H5 계열의 기본값(`TASK_GYROPID_DESIRED_PERIOD 125`)이고, 그 외 MCU는 1 kHz, SITL은 100 µs다.

---

## 4. 스케줄러 — 실제 코드로 보기

파일: `src/main/scheduler/scheduler.h`, `src/main/scheduler/scheduler.c` (862줄), task 테이블은 `src/main/fc/tasks.c`.

### 4.1 우선순위 enum과 task 구조

```c
typedef enum {
    TASK_PRIORITY_REALTIME = -1, // Task will be run outside the scheduler logic
    TASK_PRIORITY_LOWEST = 1,
    TASK_PRIORITY_LOW = 2,
    TASK_PRIORITY_MEDIUM = 3,
    TASK_PRIORITY_MEDIUM_HIGH = 4,
    TASK_PRIORITY_HIGH = 5,
    TASK_PRIORITY_MAX = 255
} taskPriority_e;
```

`task_attribute_t` 는 정적 정보(`taskName`, `checkFunc`, `taskFunc`, `desiredPeriodUs`, `staticPriority`), `task_t` 는 실행 상태(`dynamicPriority`, `taskAgePeriods`, `lastExecutedAtUs`, `anticipatedExecutionTime`, 통계)를 갖는다.

- `checkFunc` 가 있으면 **event-driven** task (예: RX의 `rxUpdateCheck`, OSD의 `osdUpdateCheck`)
- 없으면 **time-driven** task — 나이 = (지금 − 마지막 실행) / 주기

### 4.2 task 테이블 (tasks.c, 확인한 값)

`DEFINE_TASK(name, subName, checkFunc, taskFunc, period, priority)` 로 `task_attributes[]` 를 만든다.

| ID | 이름 | 함수 | 기본 주기 | 우선순위 | 비고 |
|---|---|---|---|---|---|
| TASK_GYRO | GYRO | `taskGyroSample` | `TASK_GYROPID_DESIRED_PERIOD` (F4/F7/H7: 125 µs) | REALTIME | `tasksInit()` 에서 `gyro.sampleLooptime` 로 재설정 |
| TASK_FILTER | FILTER | `taskFiltering` | 같은 값 | REALTIME | `gyro.targetLooptime` |
| TASK_PID | PID | `taskMainPidLoop` | 같은 값 | REALTIME | `gyro.targetLooptime` (= gyro × `pid_process_denom`) |
| TASK_RX | RX | `taskUpdateRxMain` (check: `rxUpdateCheck`) | 33 Hz (fallback) | HIGH | event-driven, 3단계 상태 머신 |
| TASK_DISPATCH | DISPATCH | `dispatchProcess` | 1000 Hz | HIGH | 지연 실행 큐 |
| TASK_SYSTEM | SYSTEM/LOAD | `taskSystemLoad` | 10 Hz | MEDIUM_HIGH | CPU load 계산 |
| TASK_MAIN | SYSTEM/UPDATE | `taskMain` | 1000 Hz | MEDIUM_HIGH | SD/flash 비동기 처리 |
| TASK_ACCEL | ACC | `taskUpdateAccelerometer` | 1000 Hz (acc rate로 재설정) | MEDIUM | |
| TASK_ATTITUDE | ATTITUDE | `imuUpdateAttitude` | 100 Hz | MEDIUM | |
| TASK_BATTERY_VOLTAGE | BATTERY_VOLTAGE | `batteryUpdateVoltage` | `SLOW_VOLTAGE_TASK_FREQ_HZ` | MEDIUM | |
| TASK_BATTERY_CURRENT | BATTERY_CURRENT | `batteryUpdateCurrentMeter` | 50 Hz | MEDIUM | |
| TASK_BATTERY_ALERTS | BATTERY_ALERTS | `taskBatteryAlerts` | 5 Hz | MEDIUM | |
| TASK_GPS | GPS | `gpsUpdate` | `TASK_GPS_RATE` | MEDIUM | |
| TASK_GPS_RESCUE | GPS_RESCUE | `taskGpsRescue` | `TASK_GPS_RESCUE_RATE_HZ` | MEDIUM | |
| TASK_SERIAL | SERIAL | `taskHandleSerial` | 100 Hz (`serial_update_rate_hz` 로 재설정) | LOW | **MSP 처리**. 시간 예산 검사에서 예외 |
| TASK_TELEMETRY | TELEMETRY | `taskTelemetry` | 250 Hz | LOW | |
| TASK_OSD | OSD | `osdUpdate` (check: `osdUpdateCheck`) | `OSD_FRAMERATE_DEFAULT_HZ` | LOW | event-driven |
| TASK_BEEPER | BEEPER | `beeperUpdate` | 100 Hz | LOW | |
| TASK_ESC_SENSOR | ESC_SENSOR | `escSensorProcess` | 100 Hz | LOW | |
| TASK_STACK_CHECK | STACKCHECK | `taskStackCheck` | 10 Hz | LOWEST | |
| TASK_VTXCTRL | VTXCTRL | `vtxUpdate` | 5 Hz | LOWEST | |
| TASK_ALTHOLD / TASK_POSHOLD | ALTHOLD / POSHOLD | `updateAltHold` / `updatePosHold` | 설정값 | LOW | 최신 master에 들어온 위치 유지 기능 |

> ⚠️ Gemini 정정: Gemini 표와 K05는 "Betaflight는 GPS 위치 유지가 없다 → INAV가 한다"고 했다. 지금 master에는 `TASK_ALTHOLD`, `TASK_POSHOLD`, `TASK_GPS_RESCUE`, `flight/pos_hold_multirotor.c`, `flight/autopilot_*.c`, 그리고 flight plan(`flight/flight_plan_nav.c`)까지 들어와 있다. "Betaflight = 순수 수동"은 오래된 설명이다. 다만 성숙도 · 적용 범위는 INAV/PX4와 다르다 [확인 필요: 릴리스 버전별 포함 여부].

### 4.3 핵심 1 — realtime 슬롯 (scheduler.c `scheduler()`)

```c
if (gyroEnabled) {
    // Realtime gyro/filtering/PID tasks get complete priority
    task_t *gyroTask = getTask(TASK_GYRO);
    nowCycles = getCycleCounter();
    nextTargetCycles = lastTargetCycles + desiredPeriodCycles;
    schedLoopRemainingCycles = cmpTimeCycles(nextTargetCycles, nowCycles);
    ...
    if (schedLoopRemainingCycles < schedLoopStartCycles) {
        while (schedLoopRemainingCycles > 0) {      // busy-wait to slot edge
            nowCycles = getCycleCounter();
            schedLoopRemainingCycles = cmpTimeCycles(nextTargetCycles, nowCycles);
        }
        taskExecutionTimeUs += schedulerExecuteTask(gyroTask, currentTimeUs);
        if (gyroFilterReady()) { ... schedulerExecuteTask(getTask(TASK_FILTER), ...); }
        if (pidLoopReady())    { ... schedulerExecuteTask(getTask(TASK_PID), ...); }
        rxFrameCheck(currentTimeUs, ...);
```

읽는 법:

- 시간 단위는 µs가 아니라 **CPU cycle counter**다. 그래서 jitter가 작다
- 슬롯 경계 직전(`schedLoopStartCycles`, 1~12 µs 사이에서 자동 조정)부터 **busy-wait** 한다. CPU를 버리더라도 시작 시각을 맞춘다
- 한 gyro 주기 이상 늦으면(`schedLoopRemainingCycles < -desiredPeriodCycles`) 주석대로 "configurator가 USB로 붙어 serial task가 비결정적일 때" 생기는 상황이고, 정수배 주기만큼 건너뛰어 복구한다 → **테스트 포인트: configurator/MSP 트래픽이 gyro 슬롯을 깨는가**

### 4.4 gyro / filter / PID 분주 (core.c)

```c
FAST_CODE void taskGyroSample(timeUs_t currentTimeUs)
{
    gyroUpdate();
    if (pidUpdateCounter % activePidLoopDenom == 0) {
        pidUpdateCounter = 0;
    }
    pidUpdateCounter++;
}
FAST_CODE bool gyroFilterReady(void)
{   return (pidUpdateCounter % activePidLoopDenom == 0); }
FAST_CODE bool pidLoopReady(void)
{   return ((pidUpdateCounter % activePidLoopDenom) == (activePidLoopDenom / 2)); }
```

- `activePidLoopDenom` 은 CLI `pid_process_denom` (기본 `DEFAULT_PID_PROCESS_DENOM 1`). denom=2면 gyro 8 kHz, PID 4 kHz
- denom ≥ 2일 때 filter와 PID가 **서로 다른 gyro 슬롯**에 배치된다 (filter는 나머지 0, PID는 denom/2). 한 슬롯에 몰리지 않게 부하를 나눈 것
- denom = 1이면 둘 다 매 슬롯에 같이 돈다

### 4.5 핵심 2 — 남는 시간의 task 선택

```c
if (task->attribute->checkFunc) {                 // event-driven
    if (task->dynamicPriority > 0) {
        task->taskAgePeriods = 1 + (cmpTimeUs(currentTimeUs, task->lastSignaledAtUs) / task->attribute->desiredPeriodUs);
        task->dynamicPriority = 1 + task->attribute->staticPriority * task->taskAgePeriods;
    } else if (task->attribute->checkFunc(currentTimeUs, ...)) {
        task->lastSignaledAtUs = currentTimeUs;
        task->taskAgePeriods = 1;
        task->dynamicPriority = 1 + task->attribute->staticPriority;
    } ...
} else {                                          // time-driven
    task->taskAgePeriods = (cmpTimeUs(currentTimeUs, task->lastExecutedAtUs) / task->attribute->desiredPeriodUs);
    if (task->taskAgePeriods > 0) {
        task->dynamicPriority = 1 + task->attribute->staticPriority * task->taskAgePeriods;
    }
}
```

- **dynamic priority = 1 + static × age**. 오래 못 돈 task는 우선순위가 계속 커진다 → starvation 방지
- 실행 조건(아래)도 있다. 가장 높은 dynamicPriority라도 **남은 cycle 안에 끝날 것 같지 않으면** 건너뛴다

```c
if ((taskRequiredTimeCycles < schedLoopRemainingCycles) ||
    ((scheduleCount & SCHED_TASK_DEFER_MASK) == 0) ||
    ((task - tasks) == TASK_SERIAL)) {
    selectedTaskDynamicPriority = task->dynamicPriority;
    selectedTask = task;
}
```

| 장치 | 코드 이름 | 의미 | 테스트할 때 |
|---|---|---|---|
| 예상 실행 시간 | `anticipatedExecutionTime` (>> `TASK_EXEC_TIME_SHIFT`=7) | 최대값을 잡고 천천히 감쇠 | 피크가 큰 task는 오래 "무겁다"고 기억됨 |
| guard margin | `taskGuardCycles` (3~6 µs, 자동 조정) | 예상 시간에 여유를 더함 | late task가 많으면 커진다 |
| defer mask | `SCHED_TASK_DEFER_MASK 0x07` | 8번에 1번은 시간 검사 무시하고 실행 | 긴 task도 언젠가는 돈다 |
| SERIAL 예외 | `TASK_SERIAL` | 시간 예산 무시 | **MSP 처리가 gyro 슬롯을 밀 수 있는 유일한 문** |
| RX/OSD 가속 | `TASK_AGE_EXPEDITE_RX` = `rxRelaxDeterminism` | 여러 번 밀리면 예상 시간을 0.9배씩 줄임 | RX가 굶는지 볼 때 |
| SITL 클램프 | `TASK_EXEC_TIME_CLAMP_US` (SITL: 100) | OS preemption 스파이크가 예상치를 망치지 않게 | SITL 타이밍은 실보드와 다르다 |

### 4.6 RX task는 3단계 상태 머신

`tasks.c` 의 `taskUpdateRxMain()` 은 한 번에 다 하지 않고 `RX_STATE_CHECK` → `RX_STATE_MODES` → `RX_STATE_UPDATE` 로 나눠, 단계별 실행 시간을 따로 추정해 `schedulerSetNextStateTime()` 으로 알려준다. 무거운 RX 처리가 gyro 슬롯을 깨지 않게 하려는 설계다.

| 상태 | 호출 | 하는 일 |
|---|---|---|
| RX_STATE_CHECK | `processRx()` (core.c) | 채널 계산 · failsafe 갱신 (`calculateRxChannelsAndUpdateFailsafe`) |
| RX_STATE_MODES | `processRxModes()` | 스위치 → 모드(ARM, ANGLE, FAILSAFE ...) |
| RX_STATE_UPDATE | `updateRcCommands()`, `updateArmingStatus()` | rcCommand 계산, arming 가능 여부 갱신 |

### 4.7 스케줄러 계측 — 테스트 엔지니어의 창

- CLI `tasks`: `task_statistics = ON` 일 때 헤더가 `Task list rate/hz max/us avg/us maxload avgload total/ms late run reqd/us` (STM32 F4 이상은 `USE_LATE_TASK_STATISTICS` 라서 late/run/reqd 열이 나옴). 각 행 `%02d - (%15s)` 형식. 마지막에 `Check Functions (RX, ...)` 와 `Total` 행. 출력 후 각 task의 max 값을 리셋한다(`schedulerResetTaskMaxExecutionTime`) → **두 번 연속 읽어서 두 번째 값을 구간 측정값으로 쓴다**
- `debug_mode = SCHEDULER_DETERMINISM`: gyro cycle time, late task ID, lateness, gyro skew, min/max gyro period, 표준편차(0.01 µs 단위)를 debug[0..7]에 넣는다 → blackbox로 기록
- `debug_mode = TIMING_ACCURACY`: CPU load, 초당 late task 수, 초당 총 lateness, late 비율(0.1%)
- `debug_task = <task id>` + `DEBUG_TASK`: 특정 task의 rate · max · avg · 예상 vs 실제 실행 시간 · late count
- arming 연동: `updateArmingStatus()` 에서 `getCpuPercentageLate() > schedulerConfig()->cpuLatePercentageLimit` (기본 `CPU_LOAD_LATE_LIMIT 10` = 1.0%)이면 `ARMING_DISABLED_LOAD`. 즉 **late task 비율이 높으면 아예 arming이 막힌다**

> ⚠️ Gemini 정정: Gemini는 "tasks 우선순위 검증(자이로·PID 태스크가 8 kHz 주기를 놓치지 않는지)"을 FW Test 포인트로 들었다. 방향은 맞지만, 실제로 볼 숫자는 `tasks` 의 GYRO/PID `rate/hz` 와 `late` 열, `status` 의 `CPU:%d%%, cycle time: %d, GYRO rate: %d, RX rate: %d, System rate: %d` 행, 그리고 `ARMING_DISABLED_LOAD` 다. K05의 "[확인 필요: 출력 형식]"은 위 형식으로 확인됨.

---

## 5. 제어 경로 — gyro → filter → PID → mixer → motor

### 5.1 경로 그림

```text
 IMU (SPI, EXTI 인터럽트)
   │ gyroUpdate()                      sensors/gyro.c        [TASK_GYRO]
   ▼
 sampleSum 누적 (downsample)
   │ gyroFiltering() → filterGyro()    sensors/gyro.c, gyro_filter_impl.c   [TASK_FILTER]
   │   ├ updateDshotTelemetry()        (bidir DShot eRPM 디코드, taskFiltering 안)
   │   ├ rpmFilterRun()                flight/rpm_filter.c   (모터 RPM 고조파 notch)
   │   ├ notchFilter1 / notchFilter2 / lowpassFilter (static)
   │   └ dynNotchFilter()              flight/dyn_notch_filter.c (FFT 기반)
   ▼
 gyro.gyroADCf[3]
   │ taskMainPidLoop()                 fc/core.c             [TASK_PID]
   │   ├ subTaskRcCommand() → processRcCommand()     fc/rc.c  (rates, feedforward, rc smoothing)
   │   ├ subTaskPidController() → pidController()   flight/pid.c (+ runaway takeoff 검사)
   │   ├ subTaskMotorUpdate() → mixTable() → writeMotors()   flight/mixer.c → drivers/motor.c
   │   └ subTaskPidSubprocesses() → blackboxUpdate()          blackbox/blackbox.c
   ▼
 DShot 프레임 (DMA 또는 bitbang)        drivers/dshot.c, platform/STM32/pwm_output_dshot*.c, dshot_bitbang*.c
   ▼
 ESC → 모터
```

### 5.2 필터 순서 (gyro_filter_impl.c, 확인)

1. downsample: `gyro.sampleSum / sampleCount` (또는 lowpass2로 downsample)
2. `rpmFilterRun(gyroADCfVec)` — `USE_RPM_FILTER` 일 때, 3축 함께
3. static notch 1 → static notch 2 → software lowpass (`notchFilter1ApplyFn`, `notchFilter2ApplyFn`, `lowpassFilterApplyFn`)
4. dynamic notch: `dynNotchPush()` 로 FFT 입력을 쌓고 `dynNotchFilter()` 적용
5. 이후 `gyroFiltering()` 에서 overflow 검사, yaw spin recovery 검사, IMU용 pt1 다운샘플

CLI 이름(`fc/parameter_names.h`): `gyro_lpf1_type`, `gyro_lpf1_static_hz`, `dyn_notch_count`, `dyn_notch_q`, `dyn_notch_min_hz`, `dyn_notch_max_hz`, `rpm_filter_harmonics`, `rpm_filter_weights`, `rpm_filter_q`, `rpm_filter_min_hz`, `rpm_filter_fade_range_hz`, `rpm_filter_lpf_hz`.

### 5.3 RPM filter가 DShot에 기대는 방식

`rpm_filter.c` 의 `rpmFilterUpdate()`:

- `useDshotTelemetry` 가 꺼져 있거나 harmonics가 0이면 **아무것도 안 한다** → bidir DShot이 꺼지면 RPM 필터가 조용히 비활성
- 매 호출마다 notch 몇 개씩만 갱신한다(`notchUpdatesPerIteration`) — 모터 × 고조파를 순환
- 주파수 = (harmonic index + 1) × `getMotorFrequencyHz(motor)`, `minHz` 근처에서 `fadeRangeHz` 로 페이드아웃
- `schedulerGetCycleTimeMultiplier()` 로 실제 gyro 주기 보정 → **스케줄러 gyro lock이 틀어지면 필터 주파수도 틀어진다**

FTE 관점: "bidir DShot 텔레메트리 오류율 → RPM 필터 품질 → 모터 노이즈 → PID D-term"으로 이어지는 **연쇄 회귀**가 있다. `debug_mode = DSHOT_RPM_ERRORS` 가 모터별 invalid %를 준다(`getDshotTelemetryMotorInvalidPercent`).

### 5.4 RC 경로와 feedforward/smoothing (fc/rc.c)

- `updateRcRefreshRate()`: RX 프레임 간격을 측정한다. 드라이버가 준 `lastRcFrameTimeUs` 를 우선 사용. 간격은 `RX_INTERVAL_MIN_US ~ RX_INTERVAL_MAX_US` (약 1000 Hz ~ 15 Hz) 로 제한
- `processRcCommand()`: 새 RX 데이터가 있으면 `shouldUpdateSmoothing()` → `updateFeedforwardFilters()`, `rcSmoothingSetFilterCutoffs()` → 축별 `applyRates()` (Betaflight/RaceFlight/KISS/Actual/Quick rates) → `calculateFeedforward()` → 매 PID 루프마다 `processRcSmoothingFilter()`
- 즉 **RX 레이트가 바뀌면 smoothing cutoff가 자동으로 바뀐다**. ELRS 패킷 레이트(50~1000 Hz) 변경 테스트가 PID 입력 품질에 직결된다
- 관련 CLI: `rc_smoothing`, `rc_smoothing_auto_factor`, `rc_smoothing_setpoint_cutoff`, `feedforward_*`, 조회 명령 `rc_smoothing_info`
- debug: `RX_TIMING`(프레임 간격 0.01 ms, 유효 여부, 링크 품질), `RC_SMOOTHING`, `RC_SMOOTHING_RATE`

### 5.5 mixer (flight/mixer.c `mixTable()`)

1. `calculateThrottleAndCurrentMotorEndpoints()` — throttle 범위 결정
2. crash flip 모드면 거기서 끝 (`applyCrashFlipModeToMotors`)
3. PID sum을 `pidSumLimit` 로 제한 후 `PID_MIXER_SCALING` 으로 스케일
4. 축별 mix → airmode/EzLanding 등 `applyMixerAdjustment()`
5. `applyMixToMotors()` — throttle 더하고, thrust linearization, 범위 제한. **`failsafeIsActive()` 면 DShot의 예약 구간(명령 영역)에 들어가지 않게** `disarmMotorOutput` 으로 내림
6. `writeMotors()` 로 드라이버에 전달

### 5.6 DShot (drivers/dshot.c)

```c
FAST_CODE uint16_t prepareDshotPacket(dshotProtocolControl_t *pcb)
{
    packet = (pcb->value << 1) | (pcb->requestTelemetry ? 1 : 0);
    unsigned csum = 0, csum_data = packet;
    for (int i = 0; i < 3; i++) { csum ^= csum_data; csum_data >>= 4; }
#ifdef USE_DSHOT_TELEMETRY
    if (useDshotTelemetry) { csum = ~csum; }   // bidirectional: inverted CRC
#endif
    csum &= 0xf;
    packet = (packet << 4) | csum;
```

| 항목 | 값 (소스 확인) | 출처 |
|---|---|---|
| 프레임 | 11비트 값 + 1비트 telemetry request + 4비트 CRC = 16비트 | `prepareDshotPacket` |
| CRC | 12비트를 4비트씩 XOR. bidir면 반전 | 위 코드 |
| throttle 범위 | 48 ~ 2047 (`DSHOT_MIN_THROTTLE`, `DSHOT_MAX_THROTTLE`). 0~47은 명령 | `drivers/dshot.h` |
| 명령 | `DSHOT_CMD_MOTOR_STOP=0`, `SPIN_DIRECTION_NORMAL=20`, `REVERSED=21`, ..., `DSHOT_CMD_MAX=47` | `drivers/dshot_command.h` |
| 프로토콜 enum | DSHOT150 / DSHOT300 / DSHOT600 / PROSHOT1000. **DSHOT1200은 주석으로 "removed"** | `drivers/motor_types.h` |
| bidir eRPM 디코드 | GCR 값 `eeem mmmm mmmm` → period → eRPM×100. `0x0fff` = 0 rpm | `dshot_decode_eRPM_telemetry_value` |
| EDT | extended DShot telemetry(온도 · 전압 · 전류 등)는 타입 비트로 구분 | `dshot_decode_telemetry_value` |
| 텔레메트리 품질 | 1초 창, 100 ms 버킷 (`USE_DSHOT_TELEMETRY_STATS`) | `dshot.h` |
| 출력 드라이버 | timer+DMA: `src/platform/STM32/pwm_output_dshot.c`, `_hal.c` / bitbang: `dshot_bitbang.c`, `_ll.c`, `_stdperiph.c`, 디코드 `src/main/drivers/dshot_bitbang_decode.c` | |
| CLI | `motor_pwm_protocol`, `dshot_bidir`, `dshot_burst`, 조회 `dshot_telemetry_info`, ESC 설정 `dshotprog` | `cli/settings.c`, `cli.c` |

arming과의 연결: `tryArm()` 에서 DShot beacon 명령 직후(`DSHOT_BEACON_GUARD_DELAY_US`)에는 arming을 미루고, bidir이면 EDT 활성화 명령을 보내고, crash flip 스위치에 따라 `DSHOT_CMD_SPIN_DIRECTION_*` 을 보낸다. 관련 arming 금지 플래그: `ARMING_DISABLED_DSHOT_TELEM`, `ARMING_DISABLED_DSHOT_BITBANG`, `ARMING_DISABLED_MOTOR_PROTOCOL`.

> ⚠️ Gemini 정정: Gemini는 "16비트 = 11비트 + 4비트 XOR CRC"라고 썼는데 telemetry request 1비트가 빠졌다. 정확히는 11 + 1 + 4 = 16비트. DShot 프로토콜 자체의 타이밍(비트 폭 등)은 [R05](2026-10-24_R05_rc_link_protocols_elrs_edgetx.md)에서 다룬다.

---

## 6. RX 경로 — CRSF 파싱부터 failsafe까지

### 6.1 CRSF 수신 (rx/crsf.c, rx/crsf_protocol.h)

| 항목 | 값 | 출처 |
|---|---|---|
| baud | `CRSF_BAUDRATE 420000` (기본). `USE_CRSF_OFFICIAL_SPEC` 이면 416666 | `crsf_protocol.h` |
| sync/주소 | `CRSF_SYNC_BYTE 0xC8` = `CRSF_ADDRESS_FLIGHT_CONTROLLER 0xC8` | `crsf_protocol.h` |
| 최대 프레임 | `CRSF_FRAME_SIZE_MAX 64` | `crsf_protocol.h` |
| RC 프레임 | `CRSF_FRAMETYPE_RC_CHANNELS_PACKED 0x16`, payload 22바이트 (11비트 × 16채널) | `crsf_protocol.h` |
| subset RC | `0x17` (10/11/12/13비트 해상도 선택) | `crsfFrameStatus` |
| 링크 통계 | `0x14` LINK_STATISTICS, `0x1C` RX, `0x1D` TX | |
| MSP over CRSF | `0x7A` MSP_REQ, `0x7B` MSP_RESP, `0x7C` MSP_WRITE | |
| CRC | `crsfFrameCRC()`: `crc8_dvb_s2` over **type + payload** (주소 · 길이 제외) | `crsf.c` |
| 명령 프레임 CRC | `crsfFrameCmdCRC()`: poly 0xBA 추가 CRC | `crsf.c` |
| 프레임 시간 | `CRSF_TIME_NEEDED_PER_FRAME_US 1748`, 프레임 간 최소 `6667 µs`(150 Hz) 주석 | `crsf.c` |
| 채널 → µs | 0x16: `0.62477 × raw + 881` (172→988, 992→1500, 1811→2012 µs) | `crsfReadRawRC` |

`crsfDataReceive(uint16_t c, void *data)` 는 **UART RX ISR 콜백**이다. 핵심 로직:

```c
if (cmpTimeUs(currentTimeUs, crsfFrameStartAtUs) > frameTimeNeededUs) {
    // character after max frame time -> must be start of a new frame
    crsfFramePosition = 0;
}
...
if (crsfFramePosition == CRSF_FRAME_LENGTH_ADDRESS + CRSF_FRAME_LENGTH_FRAMELENGTH &&
    !crsfFrameLengthIsValid()) {
    crsfFramePosition = 0;          // drop invalid declared length
    return;
}
...
if (crsfFramePosition >= fullFrameLength) {
    crsfFramePosition = 0;
    const uint8_t crc = crsfFrameCRC();
    if (crc == crsfFrame.bytes[fullFrameLength - 1]) { ... switch (frame.type) ... }
```

- 프레임 시작은 **바이트 사이 시간 간격**으로 판단한다. 1748 µs 이상 쉬었다가 들어온 바이트는 새 프레임의 첫 바이트
- 길이 필드가 범위 밖이면 즉시 버린다 (버퍼 오버런 방지 — `TestOversizedFrameLengthIsRejected` 가 이걸 검증)
- CRC가 맞고, RC 프레임이며, 주소가 FC(0xC8)일 때만 `memcpy` 후 `crsfFrameDone = true` (주석: 플래그보다 먼저 복사해야 race가 없다)
- 채널 언팩은 ISR이 아니라 나중에 `crsfFrameStatus()` 에서 한다 (RX task 쪽)
- CRSF v3(`USE_CRSF_V3`)는 baud 협상(`TASK_SPEED_NEGOTIATION`)과 오류 카운트 기반 baud fallback이 있다

> ⚠️ Gemini 정정: Gemini는 "가짜 Sync 바이트 주입 시 1바이트만 버리고 재동기화(Resync)하는 상태 머신"이 핵심이라고 했다. Betaflight의 CRSF 수신기는 그렇게 동작하지 **않는다**. sync 바이트를 찾아 1바이트씩 미는 대신 **inter-byte gap(시간)** 으로 프레임을 끊고, 길이 검사 + CRC 실패 시 그 프레임 전체를 버린다. 1바이트 resync는 PC 쪽 파서(테스트 도구, 로그 분석기)를 짤 때 좋은 기법이지, Betaflight 펌웨어의 동작 설명은 아니다. 따라서 **FC를 대상으로 한 파서 견고성 테스트**는 "가짜 0xC8 주입"보다 "프레임 중간 끊김 · gap 타이밍 · 길이 필드 변조 · CRC 오류 · 연속 프레임 사이 간격 부족"을 설계해야 한다.

### 6.2 RX 프레임 확인과 신호 상실 (rx/rx.c)

| 상수 | 값 | 의미 |
|---|---|---|
| `RXLOSS_TRIGGER_INTERVAL` | 150 ms | 이 시간 동안 유효 프레임이 없으면 `rxSignalReceived = false` |
| `RX_FRAME_RECHECK_INTERVAL` | 50 ms (주석) | 그 뒤 재검사 간격 |
| `MAX_INVALID_PULSE_TIME_MS` | 300 ms | 채널 하나가 나쁘면 이 동안 마지막 값 hold, 그 뒤 flight channel invalid |
| `DELAY_1500_MS` | 1.5 s | (rx.c 내 사용) |

`rxFrameCheck()` 는 매 gyro 슬롯마다 스케줄러가 직접 부른다(4.3 코드). provider가 SERIAL/MSP/SPI/UDP면 `rcFrameStatusFn()` 결과를 보고 `setLinkQuality()` 를 갱신한다. `detectAndApplySignalLossBehaviour()` 가 stage 1 동작을 만든다:

- 정상 or stage 1: 나쁜 채널은 300 ms hold, 그 뒤 AUX는 `rxfail` 설정값, flight channel은 invalid 처리
- stage 2(`failsafeIsActive()`): throttle은 `failsafe_throttle`, roll/pitch/yaw는 `midrc`, AUX는 rxfail 값

### 6.3 failsafe 상태 머신 (flight/failsafe.c, failsafe.h)

```c
typedef enum {
    FAILSAFE_IDLE = 0,
    FAILSAFE_RX_LOSS_DETECTED,
    FAILSAFE_LANDING,
    FAILSAFE_LANDED,
    FAILSAFE_RX_LOSS_MONITORING,
    FAILSAFE_RX_LOSS_RECOVERED,
    FAILSAFE_GPS_RESCUE,
    FAILSAFE_AUTOPILOT      // continuing an autopilot mission under rx loss
} failsafePhase_e;
```

```text
                    (armed, link down after failsafe_delay)
 FAILSAFE_IDLE ──────────────────────────────┬──────────────────────────────┐
   │  throttle low ≥ failsafe_throttle_low_delay                             │
   │  (또는 KILL 스위치)                       ▼                              │
   │                              FAILSAFE_RX_LOSS_DETECTED                 │
   │                                │ procedure 선택 (failsafeStartProcedure)│
   │                ┌───────────────┼───────────────┬──────────────┐        │
   │                ▼               ▼               ▼              ▼        │
   │        FAILSAFE_LANDING  (DROP_IT)    FAILSAFE_GPS_RESCUE  FAILSAFE_AUTOPILOT
   │          landing_time 초과    │               │  실패/착륙       │
   │                └──────────────┴──────►  FAILSAFE_LANDED  ◄────┘
   └──────────────────────────────────────────►    │ disarm(DISARM_REASON_FAILSAFE)
                                                    │ setArmingDisabled(ARMING_DISABLED_FAILSAFE)
                                                    ▼
                                        FAILSAFE_RX_LOSS_MONITORING
                                                    │ 유효 RX가 receivingRxDataPeriod 동안 지속
                                                    ▼
                                        FAILSAFE_RX_LOSS_RECOVERED → FAILSAFE_IDLE
                                        (중간 단계에서 링크 복구되면 바로 RECOVERED로)
```

| 설정 (PG 기본값) | 값 | 의미 |
|---|---|---|
| `failsafe_delay` | 15 (×0.1 s = 1.5 s) | stage 1 길이. 이 동안 링크 복구되면 조종 회복 |
| `failsafe_procedure` | `FAILSAFE_PROCEDURE_DROP_IT` | 기본은 **모터 끄고 떨어뜨림** (AUTO_LANDING, GPS_RESCUE 선택 가능) |
| `failsafe_throttle` | 1000 | 착륙 절차용 throttle |
| `failsafe_throttle_low_delay` | 100 (×0.1 s = 10 s) | 이 시간 이상 throttle low였으면 바로 disarm (JustDisarm) |
| `failsafe_landing_time` | 60 s | AUTO_LANDING 최대 시간 |
| `failsafe_switch_mode` | STAGE1 | BOXFAILSAFE 스위치 동작 (STAGE1 / KILL / STAGE2) |
| `failsafe_recovery_delay` | 5 (×0.1 s = 500 ms), RACE_PRO 빌드 1 | 복구로 인정하기 위한 연속 유효 RX 시간 |
| `failsafe_stick_threshold` | 30 % | GPS Rescue 탈출 stick 편차 |

타이밍 연결 고리:

- `failsafeCheckDataFailurePeriod()` 는 주석 그대로 "runs directly from scheduler, every 10ms" (`PERIOD_RXDATA_FAILURE 10`). `validRxDataReceivedAt` 이후 `rxDataFailurePeriod`(= `failsafe_delay` × 100 ms)가 지나면 `rxLinkState = FAILSAFE_RXLINK_DOWN` + `ARMING_DISABLED_RX_FAILSAFE`
- 그래서 **RF 끊김 → stage 2 진입**까지 이론 시간 ≈ 마지막 유효 프레임 + `failsafe_delay`(1.5 s), 검사 해상도 10 ms. (150 ms `RXLOSS_TRIGGER_INTERVAL` 은 stage 1 진입과 OSD `RXLOSS` 표시 쪽)
- 이 숫자들이 그대로 **failsafe 타이밍 테스트의 합격 기준 후보**다 (9.5절)

> ⚠️ Gemini 정정: Gemini는 "RF 감쇠기로 끊었을 때 정확히 0.5초 뒤에 모터 제어 신호가 0으로 떨어지는지"를 예로 들었다. Betaflight 기본값으로는 0.5초가 아니다. 기본 `failsafe_delay` 1.5 s 뒤 stage 2, 기본 procedure DROP_IT → `FAILSAFE_LANDED` → `disarm()`. 또 throttle low가 10 s 이상이었다면 stage 1을 건너뛰고 바로 disarm. 그리고 DShot 출력은 "0"이 아니라 disarm 값(`DSHOT_CMD_MOTOR_STOP` 영역)이다. 합격 기준은 **포크의 실제 설정값에서 계산**해야 한다.

### 6.4 upstream unit test가 이미 검증하는 failsafe 시나리오

`src/test/unit/flight_failsafe_unittest.cc` 의 TEST 이름(확인): `TestFailsafeInitialState`, `TestFailsafeStartMonitoring`, `TestFailsafeFirstArmedCycle`, `TestFailsafeNotActivatedWhenReceivingData`, `TestFailsafeDetectsRxLossAndStartsLanding`, `TestFailsafeCausesLanding`, `TestFailsafeDetectsRxLossAndJustDisarms`, `TestFailsafeSwitchModeKill`, `TestFailsafeSwitchModeStage1OrStage2Drop`, `TestFailsafeSwitchModeStage2Land`, `TestFailsafeNotActivatedWhenDisarmedAndRXLossIsDetected`, `TestFailsafeGpsRescueProcedure*`, 그리고 `FlightFailsafeAutopilotTest` fixture의 정책별 테스트 11개. 포크에서 failsafe를 바꾸면 **이 파일이 첫 방어선**이다.

---

## 7. arming 안전장치

### 7.1 arming disable flags (fc/runtime_config.h)

비트 순서 = CLI/MSP 이름 순서(`armingDisableFlagNames[]`, `STATIC_ASSERT` 로 개수 일치 강제).

| bit | enum | CLI 이름 | 언제 켜지나 (요약) |
|---|---|---|---|
| 0 | NO_GYRO | NOGYRO | gyro 없음 |
| 1 | FAILSAFE | FAILSAFE | failsafe LANDED 이후 복구 전 |
| 2 | RX_FAILSAFE | RXLOSS | RX 링크 down / 부팅 후 유효 RX 부족 |
| 3 | NOT_DISARMED | NOT_DISARMED | RX 복귀 시 ARM 스위치가 이미 켜짐 |
| 4 | BOXFAILSAFE | BOXFAILSAFE | failsafe 스위치 켜짐 |
| 5 | RUNAWAY_TAKEOFF | RUNAWAY | runaway takeoff 감지 후 |
| 6 | CRASH_DETECTED | CRASH | crash 감지 |
| 7 | THROTTLE | THROTTLE | throttle이 low가 아님 |
| 8 | ANGLE | ANGLE | 기울어짐 (`isUpright()` false) |
| 9 | BOOT_GRACE_TIME | BOOTGRACE | 부팅 직후 유예 |
| 10 | NOPREARM | NOPREARM | PREARM 스위치 필요 |
| 11 | LOAD | LOAD | late task 비율 > `cpuLatePercentageLimit` |
| 12 | CALIBRATING | CALIB | 센서 캘리브레이션 중 |
| 13 | CLI | CLI | CLI 모드 |
| 14 | CMS_MENU | CMS | OSD 메뉴 |
| 15 | BST | BST | |
| 16 | MSP | MSP | MSP로 arming 금지 (`MSP_SET_ARMING_DISABLED` 99) |
| 17 | PARALYZE | PARALYZE | paralyze 모드 |
| 18 | GPS | GPS | GPS Rescue 설정인데 fix 부족 |
| 19 | RESC | RESCUE_SW | Rescue 스위치 켜짐 |
| 20 | DSHOT_TELEM | DSHOT_TELEM | bidir 텔레메트리 필요한데 없음 |
| 21 | REBOOT_REQUIRED | REBOOT_REQD | 재부팅 필요한 설정 변경 |
| 22 | DSHOT_BITBANG | DSHOT_BBANG | bitbang DShot 문제 |
| 23 | ACC_CALIBRATION | NO_ACC_CAL | acc 캘리브레이션 필요 |
| 24 | MOTOR_PROTOCOL | MOTOR_PROTO | 모터 프로토콜 설정 문제 |
| 25 | CRASHFLIP | FLIP_SWITCH | crash flip 관련 |
| 26 | ALTHOLD | ALT_HOLD_SW | Alt hold 스위치 켠 채 arming |
| 27 | POSHOLD | POS_HOLD_SW | Pos hold 스위치 켠 채 arming |
| 28 | AUTOPILOT | AUTOPILOT_SW | Autopilot 스위치 켠 채 arming |
| 29 | ARM_SWITCH | ARM_SWITCH | 다른 플래그가 하나라도 있으면 항상 켜짐 (마지막이어야 함) |

### 7.2 arming 흐름 (fc/core.c)

- `updateArmingStatus()` 는 RX task의 `RX_STATE_UPDATE` 단계에서 주기적으로 호출된다. 위 플래그들을 set/unset
- `tryArm()`: `updateArmingStatus()` → `isArmingDisabled()` 가 false일 때만 진행 → DShot beacon 가드 → bidir EDT 활성 → spin direction → launch control → 실제 arm
- `disarm(flightLogDisarmReason_e reason)`: 이유 enum(`fc/core.h`) — `DISARM_REASON_ARMING_DISABLED 0`, `FAILSAFE 1`, `THROTTLE_TIMEOUT 2`, `STICKS 3`, `SWITCH 4`, `CRASH_PROTECTION 5`, `RUNAWAY_TAKEOFF 6`, `GPS_RESCUE 7`, `SERIAL_COMMAND 8`, `LANDING 9`, `CRASHFLIP 10`, `LAUNCH_ABORT 11`, `SYSTEM 255`. 이 이유는 blackbox의 `FLIGHT_LOG_EVENT_DISARM (15)` 이벤트로 남는다 → **테스트 판정에 그대로 쓸 수 있다**
- runaway takeoff: `subTaskPidController()` 안. PID sum과 gyro rate가 임계값을 넘는 상태가 `RUNAWAY_TAKEOFF_ACTIVATE_DELAY` 동안 지속되면 `disarm(DISARM_REASON_RUNAWAY_TAKEOFF)`
- USB 링크 끊김: `taskHandleSerial()` 의 `checkUsbLinkLost()` 가 끊긴 VCP 입력을 버린다. 주석 "so a queued MSP_SET_MOTOR cannot restart the motors" — 모터 테스트 중 케이블이 빠질 때의 안전장치

upstream unit test `arming_prevention_unittest.cc`: `CalibrationPowerOnGraceAngleThrottleArmSwitch`, `ArmingGuardRadioLeftOnAndArmed`, `Prearm`, `RadioTurnedOnAtAnyTimeArmed`, 3D 모드 3개, `GPSRescueWithoutFixPreventsArm`, `GPSRescueSwitchPreventsArm`, `ParalyzeOnAtBoot`, `Paralyze`.

---

## 8. 관측 · 제어 인터페이스 — MSP, CLI, blackbox

### 8.1 MSP (msp/msp.c, msp_serial.c, msp_protocol*.h)

프레이밍 (확인):

| 버전 | 요청 형식 | 체크섬 |
|---|---|---|
| v1 | `$M<` size(u8) cmd(u8) payload | size, cmd, payload의 XOR |
| v1 응답 | `$M>` (오류는 `$M!`). size=255면 **JUMBO** — u16 size가 뒤따름 (`JUMBO_FRAME_SIZE_LIMIT 255`) | 같음 |
| v2 native | `$X<` flags(u8) cmd(u16 LE) size(u16 LE) payload | `crc8_dvb_s2` over flags..payload |
| v2 over v1 | v1 프레임 cmd = 255 (`MSP_V2_FRAME_ID`), payload 안에 v2 헤더+payload+crc8 | 두 체크섬 다 |

수신 상태 머신(`mspSerialProcessReceivedData`)의 상태: `MSP_IDLE`, `MSP_HEADER_START`, `MSP_HEADER_M`, `MSP_HEADER_X`, `MSP_HEADER_V1`, `MSP_HEADER_V2_OVER_V1`, `MSP_HEADER_V2_NATIVE`, payload/checksum 상태, `MSP_COMMAND_RECEIVED`. v2 size가 `MSP_PORT_INBUF_SIZE` 보다 크면 IDLE로 버린다.

주요 명령 ID (msp_protocol.h, API `1.49`):

| ID | 이름 | 방향 | payload (확인한 것) | FTE 용도 |
|---|---|---|---|---|
| 1 | MSP_API_VERSION | out | protocol, major, minor | 포크 · 도구 호환성 확인 |
| 2 | MSP_FC_VARIANT | out | "BTFL" 등 4글자 | 포크 식별 |
| 3 | MSP_FC_VERSION | out | | 버전 기록 |
| 5 | MSP_BUILD_INFO | out | | 빌드 추적 |
| 68 | MSP_REBOOT | in | | 테스트 간 초기화 |
| 99 | MSP_SET_ARMING_DISABLED | in | | 벤치 안전 잠금 |
| 101 | MSP_STATUS | out | u16 PID 주기(µs), u16 I2C 오류, u16 센서 bitmask, u32 mode flags, u8 PID profile, u16 CPU load, u16 0, u8 extra flag byte 수 + bytes, u8 arming flag 수, u32 **arming disable flags**, u8 config state, u16 CPU temp, ... | 상태 폴링의 기본 |
| 150 | MSP_STATUS_EX | out | 위와 같고 2바이트 자리에 profile 개수 · rate profile | |
| 104 | MSP_MOTOR | out | u16 × 8 (외부 단위, 1000~2000) | 모터 출력 관찰 |
| 105 | MSP_RC | out | u16 × 채널 수 (µs) | RC 입력 확인 |
| 108 | MSP_ATTITUDE | out | s16 roll(0.1°), s16 pitch(0.1°), s16 yaw(°) | 자세 |
| 112 | MSP_PID | out | | 설정 확인 |
| 119 | MSP_BOXIDS | out | 모드 permanent ID | 모드 매핑 |
| 139 | MSP_MOTOR_TELEMETRY | out | 모터별 RPM · 패킷 통계 · 온도 | bidir DShot 확인 |
| 200 | MSP_SET_RAW_RC | in | u16 × N | **RC 주입** (조건 있음, 아래) |
| 214 | MSP_SET_MOTOR | in | u16 × 모터 수 | **모터 테스트** (조건 있음, 아래) |
| 250 | MSP_EEPROM_WRITE | in | | 설정 저장 |
| 254 | MSP_DEBUG | out | debug[] | debug_mode 값 읽기 |
| 0x3012 | MSP2_CLI_COMMAND | v2 | CLI 명령 문자열 | **MSP로 CLI 실행** (`USE_MSP_CLI_COMMAND`) |
| 0x3010 / 0x3011 | MSP2_CLI_SETTING / _INFO | v2 | | 설정 하나씩 읽기/쓰기 |
| 0x3003 | MSP2_SEND_DSHOT_COMMAND | v2 | | DShot 명령 |

주의할 동작 (msp.c 확인):

- `MSP_SET_RAW_RC` 는 `USE_RX_MSP` 일 때 `rxMspFrameReceive()` 로 들어간다. **RX provider가 MSP일 때만** 실제 조종 입력이 된다. 일반 CRSF 기체에선 `msp_override` 모드(`rx/msp_override.c`, BOXMSPOVERRIDE + `msp_override_channels_mask`)를 써야 일부 채널만 덮어쓸 수 있다
- `MSP_SET_MOTOR` 는 `motor_disarmed[i]` 에 쓴다 → **disarm 상태의 모터 테스트 경로**다. 비행 중 모터 제어 수단이 아니다
- `taskHandleSerial()`: armed 상태에서는 `MSP_SKIP_NON_MSP_DATA` — arm 후 CLI 진입 같은 비-MSP 데이터는 무시

> ⚠️ Gemini 정정: Gemini 표는 MSP를 "현재 센서 값·태스크 상태·모터 값을 조회/강제 제어하는 프로토콜"이라 했다. 조회는 맞지만 "강제 제어"는 조건부다: SET_MOTOR는 disarm 상태 전용, SET_RAW_RC는 MSP RX provider 또는 MSP override 모드에서만 효과가 있다. HIL 하네스는 이 조건을 config로 먼저 맞춰야 한다.

### 8.2 Python MSP 클라이언트 (요약)

전체 코드: [../code/msp_client.py](../code/msp_client.py). 핵심만:

```python
def crc8_dvb_s2(data, crc=0):
    for b in data:
        crc ^= b
        for _ in range(8):
            crc = ((crc << 1) ^ 0xD5) & 0xFF if crc & 0x80 else (crc << 1) & 0xFF
    return crc

def encode_v1(cmd, payload=b"", direction=b"<"):
    body = bytes([len(payload), cmd]) + payload
    return b"$M" + direction + body + bytes([xor8(body)])

def encode_v2(cmd, payload=b"", flags=0, direction=b"<"):
    body = struct.pack("<BHH", flags, cmd, len(payload)) + payload
    return b"$X" + direction + body + bytes([crc8_dvb_s2(body)])
```

- 자체 테스트가 확인하는 것: CRC-8/DVB-S2 check 값(`"123456789"` → `0xBC`), `MSP_STATUS` v1 요청 바이트(`$M<\x00\x65\x65`), v2 분할 수신, v2-over-v1, CRC 오류 프레임 폐기 후 다음 프레임 생존, `$` 가 섞인 쓰레기 속 프레임 복구, JUMBO 응답, socketpair 위 가짜 FC와 end-to-end(STATUS의 arming flag 디코드 → `["RXLOSS", "BOOTGRACE"]`)
- PC 쪽 파서는 1바이트 resync 방식을 쓴다 (6.1의 Gemini 정정과 대비: 이건 **도구 쪽** 설계)
- Betaflight SITL에 붙이려면 TCP 5761 (upstream harness와 같은 포트, 9절)

### 8.3 CLI를 테스트 인터페이스로 (cli/cli.c)

확인한 명령 목록(일부): `diff`, `dump`, `get`, `set`, `save`, `defaults`, `status`, `tasks`, `version`, `feature`, `serial`, `aux`, `rxfail`, `rxrange`, `motor`, `mixer`, `mmix`, `dshot_telemetry_info`, `dshotprog`, `rc_smoothing_info`, `resource`, `timer`, `dma`, `flash_info`, `flash_erase`, `flash_read`, `sd_info`, `gyroregisters`, `bind_rx`, `batch`, `profile`, `rateprofile`, `battery_profile`, `simplified_tuning`, `signature`, `board_name`, `manufacturer_id`, `mcu_id`, `exit`.

| 명령 | 테스트 용도 |
|---|---|
| `version` | 빌드 · 보드 식별 (`# <FW> / <target> (<board id>) <version> ... MSP API: x.y`) |
| `status` | MCU/클럭, 스택, CONFIG 상태 · 마지막 reset 원인, SPI/I2C 장치 수와 **오류 카운터**, gyro 상태, GPS, `CPU:%d%%, cycle time:..., GYRO rate:..., RX rate:..., System rate:...`, `Arming disable flags: ...` |
| `tasks` | task별 rate/max/avg/load/late (4.7절) |
| `diff` / `diff all` | 기본값과 다른 설정만 출력 → **설정 스냅샷 · 비교** |
| `dump` | 전체 설정 출력 |
| `get <name>` / `set <name> = <v>` | 개별 파라미터 조작 |
| `batch start` / `batch end` | 여러 명령을 묶어 적용 (복원 스크립트) |
| `motor <index> <value>` | disarm 상태 모터 테스트 (프롭 제거) |
| `dshot_telemetry_info` | bidir DShot 수신 품질 |
| `rc_smoothing_info` | 감지된 RX 레이트 · cutoff |
| `gyroregisters` | gyro 레지스터 덤프 (보드 bring-up) |

CLI 모드에 들어가 있으면 `ARMING_DISABLED_CLI` 이고, blackbox도 `cliMode` 일 때 안 쓴다(`subTaskPidSubprocesses`). 즉 **CLI로 측정하는 동안의 시스템은 비행 상태와 다르다**. 비행 중 계측은 MSP(`MSP_STATUS`, `MSP_DEBUG`)나 blackbox로 해야 한다. MSP v2 `MSP2_CLI_COMMAND`(0x3012)는 MSP 연결을 끊지 않고 CLI 출력을 받는 길이다(응답 플래그 `TRUNCATED`, `REFUSED` 존재).

### 8.4 blackbox (blackbox/)

| 항목 | 확인 내용 | 위치 |
|---|---|---|
| 기록 시점 | PID task 안 `subTaskPidSubprocesses()` → `blackboxUpdate()` | `fc/core.c` |
| 장치 | `BLACKBOX_DEVICE_NONE/FLASH/SDCARD/SERIAL/VIRTUAL` | `blackbox.h` |
| 샘플 비율 | `blackbox_sample_rate` 기본 `BLACKBOX_RATE_QUARTER` (PID 루프의 1/4). ONE/HALF/QUARTER/8TH/16TH | `blackbox.c` |
| 프레임 종류 | `H`(헤더), `I`(intra, 키프레임), `P`(inter, 예측 차분), `S`(slow), `G`/`H`(GPS), `E`(event) | `blackboxWrite('I')` 등 |
| I 간격 | `blackboxIInterval = 32 * 1000 / targetPidLooptime` (I-frame 약 32 ms마다) | `blackbox.c` |
| 메인 필드 | `loopIteration`, `time`, `axisP/I/D/F/S`, `rcCommand`, `setpoint`, `vbatLatest`, `amperageLatest`, `baroAlt`, `magADC`, `rssi`, `gyroADC`, `gyroUnfilt`, `accSmooth`, `imuQuaternion`, `debug`, `motor`, `eRPM`, `servo` | `blackboxMainFields[]` |
| slow 필드 | `flightModeFlags`, `stateFlags`, `failsafePhase`, `rxSignalReceived`, `rxFlightChannelsValid` | |
| 이벤트 | `SYNC_BEEP 0`, `INFLIGHT_ADJUSTMENT 13`, `LOGGING_RESUME 14`, `DISARM 15`, `FLIGHTMODE 30`, `LOG_END 255` | `blackbox.h` |
| 필드 끄기 | `blackbox_disable_pids`, `_rc`, `_setpoint`, `_bat`, `_mag`, `_alt`, `_rssi`, `_gyro`, ... | `cli/settings.c` |
| 가상 장치 | `blackbox_virtual.c`: SITL에서 `LOG00001.BFL` 형식 파일로 저장 | `USE_BLACKBOX_VIRTUAL` (SITL target.h) |
| 헤더 문구 | `H Product:Blackbox flight data recorder by Nicholas Sherlock` | |

도구:

- **Blackbox Explorer** (`betaflight/blackbox-log-viewer`): 웹/데스크톱 뷰어. 그래프 · 스펙트럼 · 영상 동기화
- **blackbox-tools** (`betaflight/blackbox-tools`): `blackbox_decode LOG00001.TXT` → `LOG00001.01.csv` (로그 안 여러 비행이면 .01, .02 ...). 옵션 `--merge-gps`, `--unit-rotation raw|deg/s|rad/s`, `--unit-frame-time us|s`, `--unit-vbat`, `--unit-amperage`, `--unit-acceleration` 등. `blackbox_render` 는 PNG 오버레이 [WebFetch로 README 확인]
- Python: 이름이 알려진 파서 라이브러리(`orangebox` 등)가 있으나 이번엔 확인 안 함 [확인 필요]. 안전한 길은 `blackbox_decode` CSV → pandas/표준 csv

FTE에 중요한 성질:

- blackbox는 **PID 루프와 같은 타임라인**이다. `time` 필드 간격 자체가 루프 jitter 측정값이 된다 (단, sample rate로 솎아진 간격)
- `failsafePhase`, `rxSignalReceived` 가 slow frame에, disarm 이유가 event에 남는다 → failsafe 테스트를 **로그만으로 판정**할 수 있다
- `debug` 필드 + `debug_mode` 로 원하는 내부값(SCHEDULER_DETERMINISM, RX_TIMING, DSHOT_RPM_ERRORS ...)을 같이 기록

> ⚠️ Gemini 정정: Gemini는 "125 µs마다 ... 값이 이 메모리에 빼곡히 저장"이라고 했다. 기본은 PID 루프의 1/4(`BLACKBOX_RATE_QUARTER`)이고, 8 kHz PID면 2 kHz 기록이다. 또 "SPI NOR flash 16~128 MB"는 보드마다 다르다(SD 카드 보드도 있다) [확인 필요: 보드별]. 로그 확장자는 flash 다운로드 시 `.BBL`/`.TXT` 류, SITL 가상 장치는 `.BFL` 이다.

---

## 9. 테스트 인프라 — unit test · SITL · CI

### 9.1 unit test (googletest)

| 항목 | 확인 내용 |
|---|---|
| 위치 | 테스트 소스 `src/test/unit/*.cc`, **Makefile은 `src/test/Makefile`** (unit 폴더 안이 아님) |
| 프레임워크 | googletest, `lib/test/gtest` 에 포함 (`GTEST_DIR = ../../lib/test/gtest`) |
| 컴파일러 | `clang-18` / `clang++-18`, 없으면 `clang` 으로 fallback. `-O0 -g -DUNIT_TEST` |
| 테스트 발견 | `TEST_SRCS = $(sort $(wildcard $(TEST_DIR)/*.cc))` → 파일 이름 = 테스트 이름. `.cc.txt` 로 이름을 바꾸면 **비활성** (예: `flight_mixer_unittest.cc.txt`, `battery_unittest.cc.txt`) |
| 테스트별 설정 | `<name>_SRC` (같이 컴파일할 펌웨어 .c), `<name>_DEFINES`, `<name>_INCLUDE_DIRS`, `<name>_EXPAND`(타깃별 반복), `<name>_BLACKLIST` |
| 루트 명령 | `make test` (target 독립 테스트 + `check-pg-ids`), `make test-all` (타깃별 확장 포함), `make test-representative`, `make junittest` (`--gtest_output=xml:...`), `make test_<name>` (하나만), `make test_help` |
| 파일 수 | `src/test/unit` 에 111개 항목 (`.cc` 테스트 약 90여 개 + 지원 파일) |
| 정적 함수 테스트 | 펌웨어 쪽 `STATIC_UNIT_TESTED` 매크로 — UNIT_TEST 빌드에선 static이 풀려 테스트에서 접근 가능 (예: `crsfDataReceive`, `crsfFrameCRC`, `queueAdd`) |
| macOS | Makefile이 `OSFAMILY = macosx` 를 구분해 링커 옵션을 바꾼다 |

FC 기능별로 이미 있는 테스트 (일부): `scheduler_unittest`, `rx_crsf_unittest`, `rx_rx_unittest`, `rx_ranges_unittest`, `rx_msp_override_unittest`, `rx_spi_expresslrs_unittest`, `link_quality_unittest`, `flight_failsafe_unittest`, `arming_prevention_unittest`, `pid_unittest`, `motor_output_unittest`, `flight_imu_unittest`, `dyn_notch_filter_unittest`, `common_filter_unittest`, `sensor_gyro_unittest`, `blackbox_unittest`, `blackbox_encoding_unittest`, `msp_serial_unittest`, `cli_unittest`, `pg_unittest`, `telemetry_crsf_unittest`, `telemetry_crsf_msp_unittest`, `osd_unittest`, `sitl_gyro_unittest`.

### 9.2 예제 해부 — scheduler_unittest.cc

이 테스트는 **시간을 가짜로 만든다**. 실제 task 함수 대신 "실행되면 가짜 시계를 X µs 전진"시키는 stub을 넣는다.

```cpp
const int TEST_GYRO_SAMPLE_TIME = 10;
const int TEST_PID_LOOP_TIME = 58;
uint32_t simulatedTime = 0;
uint32_t micros(void) { return simulatedTime; }
uint32_t getCycleCounter(void) { return simulatedTime * 10; }
void taskGyroSample(timeUs_t) { simulatedTime += TEST_GYRO_SAMPLE_TIME; taskGyroRan = true; }
void taskMainPidLoop(timeUs_t) { simulatedTime += TEST_PID_LOOP_TIME; taskPidRan = true; }
bool rxUpdateCheck(timeUs_t, timeDelta_t) { simulatedTime += TEST_UPDATE_RX_CHECK_TIME; return false; }
```

```cpp
TEST(SchedulerUnittest, TestSingleTask)
{
    schedulerInit();
    for (int taskId = 0; taskId < TASK_COUNT; ++taskId) {
        setTaskEnabled(static_cast<taskId_e>(taskId), false);
    }
    setTaskEnabled(TASK_ACCEL, true);
    tasks[TASK_ACCEL].lastExecutedAtUs = 1000;
    simulatedTime = 2050;
    scheduler();
    EXPECT_EQ(unittest_scheduler_selectedTask, &tasks[TASK_ACCEL]);
    EXPECT_EQ(1050, tasks[TASK_ACCEL].taskLatestDeltaTimeUs);
```

- `scheduler.c` 는 `UNIT_TEST` 일 때 `readSchedulerLocals()` 로 선택된 task를 전역(`unittest_scheduler_selectedTask`)에 노출한다 — **테스트 가능성을 위해 펌웨어에 넣은 훅**
- Makefile: `scheduler_unittest_SRC := scheduler/scheduler.c common/crc.c common/streambuf.c unit/scheduler_stubs.c`, `scheduler_unittest_DEFINES := USE_OSD=`
- 다른 TEST: `TestPriorites`, `TestQueue*`, `TestSchedulerInit`, `TestScheduleEmptyQueue`, `TestTwoTasks`, `TestPriorityBump`, `TestGyroTask`

### 9.3 예제 해부 — rx_crsf_unittest.cc

- `CRC`: 테이블 CRC(`crc8tab`)와 `crc8_dvb_s2` 결과 비교
- `TestCapturedData`, `TestCapturedSubsetData`: **실제 캡처한 바이트열**을 넣고 채널 값 확인
- `TestOversizedFrameLengthIsRejected`: 주소 0xC8, 길이 64(`CRSF_FRAME_SIZE_MAX`), 타입 0x16 뒤 0을 채워 넣고 `crsfFrameDone` 이 false인지 확인 → 길이 검사 회귀 방지
- `TestValidFrameLengthBoundsAreReceived`, `TestInvalidFrameLengthBoundsAreRejectedAndRecover`: 경계값과 복구

### 9.4 테스트 하나 추가하는 법 (포크에서)

1. `src/test/unit/<기능>_unittest.cc` 생성. `extern "C" { #include ... }` 로 펌웨어 헤더, `#include "gtest/gtest.h"`
2. 링크에 필요한 외부 함수는 같은 파일에 stub으로 정의 (`micros()`, `millis()`, 다른 모듈 함수). scheduler 테스트처럼 **가짜 시간**을 두면 타이밍 로직을 결정적으로 시험할 수 있다
3. `src/test/Makefile` 에 `<기능>_unittest_SRC := $(USER_DIR)/...c` 와 필요하면 `_DEFINES := USE_XXX=` 추가
4. 테스트할 static 함수는 펌웨어 쪽에서 `STATIC_UNIT_TESTED` 로 선언
5. `make test_<기능>_unittest` 로 하나만 실행 → `make test` 전체
6. CI는 `make EXTRA_FLAGS=-Werror test-all` 이므로 경고도 실패다

### 9.5 SITL — 어디에, 무엇을, 어떻게

> ⚠️ Gemini 정정: Gemini · K05는 SITL 위치를 `src/main/target/SITL/` 로 적었다. 현재 master에서 SITL은 **`src/platform/SIMULATOR/`** 아래다: `sitl.c`, `udplink.c`, `sitl_gyro.h`, `target/SITL/target.h`, `target/SITL/target.mk`, `target/SITL/README.md`, `mk/SITL.mk`, `link/sitl.ld`. `src/main/target/` 에는 공통 헤더(`common_pre.h` 등)만 남아 있다. (INAV는 여전히 `src/main/target/SITL/` 이다 — 10절)

| 항목 | 확인 내용 | 출처 |
|---|---|---|
| 빌드 | `make TARGET=SITL` → `obj/main/betaflight_SITL.elf` | `target/SITL/README.md`, harness docstring |
| 루프 주기 | `TASK_GYROPID_DESIRED_PERIOD 100` (10 kHz 목표), `SCHEDULER_DELAY_LIMIT 1`, `TASK_EXEC_TIME_CLAMP_US 100` | `target.h` |
| 가상 센서 | `USE_VIRTUAL_GYRO`, `_ACC`, `_MAG`, `_BARO`, `_GPS`, `USE_VIRTUAL_LED` | `target.h` |
| 기본 bridge | `ENABLE_GAZEBO_BRIDGE 1` (기본 Gazebo). X-Plane/RealFlight용 config는 0으로 둔다 | `target.h` 주석 |
| 자세 추정 | 실제 Mahony 추정기를 가상 센서로 돌림. `-DSITL_ATTITUDE_DIRECT` 면 FDM 쿼터니언 직접 사용 | `target.h` |
| UDP 포트 | 9001 PWM_RAW out (RealFlight용 "RF9"), 9002 PWM out (Gazebo), 9003 STATE in (FDM), 9004 RC in | `sitl.c` |
| UART | `UARTx` → `tcp://127.0.0.1:576x` (README). harness는 MSP에 5761 사용 | README, harness |
| RX | `ENABLE_RX_UDP 1` → 기본 RX 기능 `FEATURE_RX_UDP` (UDP 9004의 `rc_packet`), X-Plane config는 MSP RX | `target.h` 주석 |
| 설정 저장 | `eeprom.bin` (EEPROM_SIZE 32768), `CONFIG_IN_FILE` 로 `--config <file>` 일회 실행 지원 | `target.h`, `main.c` |
| blackbox | `USE_BLACKBOX_VIRTUAL` → 파일 | `target.h` |
| CI | `Makefile` 의 `CI_COMMON_TARGETS` 에 `SITL_X_PLANE`, `PREVIEW_TARGETS` 에 `SITL` | `Makefile` |

`rc_packet` / `fdm_packet` 형식(harness 기준): RC는 `struct.pack("<d16H", timestamp, *channels)` (double 타임스탬프 + 16채널 u16), FDM은 double 18개(가상 GPS 모드에선 위치에 lon/lat/alt, 속도에 ENU). 모터 출력은 UDP 9002의 `servo_packet`(정규화 값).

**Gazebo / RealFlight / X-Plane 정리**:

- Gazebo: README는 오래된 "gazebo 8 + ArduCopterPlugin(iris_arducopter_demo.world)" 안내다 (Ubuntu 16.04 기준). 즉 **ArduPilot용 Gazebo 플러그인 프로토콜을 빌려 쓴다**. 최신 Gazebo(gz-sim)에서 그대로 되는지는 [확인 필요]
- RealFlight: `PORT_PWM_RAW 9001` 로그 문구 "RF9" — RealFlight 연동용 raw 출력이 있다. 실제 RealFlight 쪽 bridge 프로그램 · 절차는 [확인 필요]
- X-Plane: `SITL_X_PLANE` config가 CI 대상. config는 `src/config` submodule(`betaflight/config` 저장소)에서 온다. 정확한 빌드 명령(`make CONFIG=SITL_X_PLANE` 형태로 추정)은 [확인 필요]

> ⚠️ Gemini 정정: Gemini는 SITL을 "make TARGET=SITL로 PC용 프로그램처럼 빌드해서 Python이 UDP로 가상 센서 값과 조종 명령을 넣는다"고 했다. 이건 대체로 맞다. 다만 Gemini가 "Betaflight에는 이런 게 부족하니 ArduPilot autotest를 참고해 만들어야 한다"는 뉘앙스였다면, 지금 upstream에는 이미 **`src/test/sitl/sitl_harness.py`** (3900여 줄)가 있다. 출발점으로 이걸 먼저 읽는 게 맞다.

### 9.6 upstream SITL harness (src/test/sitl/sitl_harness.py)

docstring 요약(확인):

- RC를 UDP 9004로 50 Hz 송신 (`RcFeed`, `stop_stream()` 으로 **RX loss 흉내**)
- FDM 상태를 UDP 9003으로 송신 (`FdmFeed`, 자체 `MotionModel` / `WingMotionModel` 물리 모델, `SensorErrors` 로 센서 오류 주입)
- 모터 출력을 UDP 9002에서 수신 (`MotorFeed`, trace 기록)
- MSP v1 over TCP 5761로 상태 조회 (`Msp` 클래스 — modes, arming disable flags), `StatusPoller` 스레드
- `--config <file>` 로 시나리오별 `eeprom.bin` 준비
- 상수: `MSP_STATUS 101`, `MSP_RAW_GPS 106`, `MSP_ATTITUDE 108`, `MSP_BOXIDS 119`, `MSP_ACC_CALIBRATION 205`, `MSP_DEBUG 254`, box ID `BOX_ARM 0`, `BOX_ALTHOLD 3`, `BOX_POSHOLD 11`, `BOX_FAILSAFE 27`, `BOX_GPSRESCUE 46`, `BOX_AUTOPILOT 56`, `BOX_LAUNCH 58`
- 시나리오 예: `scenario_rx_loss(policy)` (DISABLE / CONTINUE / LAND), `scenario_geofence(action)`, `scenario_geofence_rth_rxloss`, `scenario_rescue_*` (GPS loss, heading recovery, near home ...), `scenario_mission_*`, `scenario_wing_*` (autoland, go-around, loiter wind ...)
- 사용: `sitl_harness.py --binary obj/main/betaflight_SITL.elf --scenario all`
- `wait_for(description, predicate, timeout)` 로 조건 대기, 실패 메시지는 측정값을 포함

이 harness가 CI에서 자동으로 도는지: `.github/workflows/*.yml` 를 grep했을 때 `sitl_harness` 호출은 **없었다**. 즉 upstream에선 수동 실행 도구로 보인다 [추정]. 포크에서 이걸 CI에 넣는 것이 FTE의 첫 작업 후보다.

### 9.7 Betaflight 자체 CI (.github/workflows)

| 파일 | 트리거 | 하는 일 |
|---|---|---|
| `pr.yml` | PR → master, `*-maintenance` | `ci.yml` 호출 |
| `push.yml` | push → master, `*-maintenance` | `ci.yml` 호출 |
| `ci.yml` | workflow_call | `setup`(config submodule, `make configs`, SDK별 타깃 매트릭스 생성 `make targets-ci-grouped-print`) → `sdk-pipeline`(SDK별 `ci-sdk.yml` 로 타깃 빌드) + `test` 잡 → `result` |
| `ci.yml` test 잡 | | `apt-get install libblocksruntime-dev clang-18` → `make EXTRA_FLAGS=-Werror checks` → `make EXTRA_FLAGS=-Werror test-all` |
| `ci-sdk.yml` | | SDK(툴체인)별 빌드 |
| `build-release.yml` | | 릴리스 빌드 |
| `pr-cloud-comment.yml`, `stale.yaml`, `auto-close.yml`, `no-response.yml` | | PR 코멘트 · 이슈 관리 |

요약: upstream CI = **정적 검사 + unit test 전부 + 전 타깃 컴파일**. SITL 시나리오 · HIL · 타이밍 회귀는 CI에 없다. → 포크를 지키는 사내 CI가 채워야 할 빈칸이다 [추정].

---

## 10. INAV — 무엇이 다르고, 왜 HITL이 중요한가

### 10.1 구조 차이 (소스 확인)

| 항목 | Betaflight | INAV |
|---|---|---|
| 빌드 | make (`make TARGET=...`) | **CMake** (+ ninja). SITL: `cmake -DSITL=ON .. && make` (`docs/development/Building SITL.md`) |
| task 테이블 | `fc/tasks.c` `task_attributes[]` + `DEFINE_TASK` | `fc/fc_tasks.c` `cfTasks[]` (지정 초기화 구조체) |
| 우선순위 | `REALTIME = -1` (선택 로직 밖, gyro 슬롯) | `TASK_PRIORITY_IDLE 0`, `LOW 1`, `MEDIUM 3`, `MEDIUM_HIGH 4`, `HIGH 5`, **`REALTIME 18`** — 선택 로직 안에서 "overdue면 즉시" (`forcedRealTimeTask`) |
| 루프 주기 | 8 kHz (STM32 F4 이상 기본) | `looptime` 기본 500 µs (2 kHz), `settings.yaml` |
| 설정 정의 | `cli/settings.c` 표 | `fc/settings.yaml` (설명 · 기본값 포함, 문서 생성) |
| 내비게이션 | alt/pos hold, GPS rescue, flight plan (최근) | `src/main/navigation/`: `navigation.c`, `navigation_multicopter.c`, `navigation_fixedwing.c`, `navigation_pos_estimator*.c`, `rth_trackback.c`, `navigation_geozone.c`, VTOL · rover/boat |
| 로직 | | Programming Framework (`src/main/programming/`) |
| MSP | v1/v2 | v1/v2 + INAV 전용 v2 명령 (`msp_protocol_v2_inav.h`) |
| SITL 위치 | `src/platform/SIMULATOR` | `src/main/target/SITL/` (+ `sim/`: `realFlight.c`, `xplane.c`, `simHelper.c`, `soap_client.c`) |
| SITL 시뮬레이터 | Gazebo(기본), X-Plane/RealFlight config | **RealFlight, X-Plane**, fl2sim(블랙박스 재생) — `--sim=xp\|rf`, `--simip`, `--simport`(X-Plane 기본 49000), `--chanmap`, `--path` |
| SITL UART | UART1 → TCP 5761 (`576x`) | UART1 → TCP **5760**, UART2 → 5761 ... |
| unit test | make + googletest, `src/test/unit/*.cc` | CMake, `src/test/unit/*.cc` (+ `CMakeLists.txt`), `ninja check` |
| CI | GitHub Actions: checks + test-all + 빌드 | GitHub Actions: 타깃 분할 빌드(ninja ci), **SITL 빌드(Linux x86/aarch64, macOS, Windows)**, `test` 잡 `cmake -DTOOLCHAIN=none -G Ninja .. && ninja check`, size report, **`msp.yml` = MSP C 코드 ↔ 스펙 일치 검사**(`src/utils/check_msp.py`) |

### 10.2 HITL — MSP_SIMULATOR (fc/fc_msp.c)

- ID: `MSP_SIMULATOR 0x201F` (`msp/msp_protocol_v2_inav.h`). Betaflight 저장소에는 이 이름이 없다 (grep 결과 0건)
- 처리: `mspProcessSimulatorCommand()` — 첫 바이트 = simulator MSP 버전(`SIMULATOR_MSP_VERSION_2` 또는 `_3`), 다음 = flags(v2: u8, v3: u16)
- flags (`fc/runtime_config.h`): `HITL_ENABLE`, `HITL_SIMULATE_BATTERY`, `HITL_MUTE_BEEPER`, `HITL_USE_IMU`, `HITL_HAS_NEW_GPS_DATA`, `HITL_EXT_BATTERY_VOLTAGE`, `HITL_AIRSPEED`, `HITL_EXTENDED_FLAGS`, `HITL_GPS_TIMEOUT`, `HITL_PITOT_FAILURE`, `HITL_CURRENT_SENSOR`, `HITL_SIM_RC_INPUT`, `HITL_RANGEFINDER`, `HITL_FAILSAFE_TRIGGERED`, `HITL_SITL_MODE`
- 주입되는 값(`readMspSimulatorValues`): GPS(fix, 위성 수, lat/lon/alt, 지상 속도 · 코스, NED 속도), 자세(roll/pitch/yaw) 또는 IMU, **acc(1/1000 G), gyro(1/16 dps)**, baro 압력, mag, 배터리 전압, airspeed, rangefinder, 전류, RC 입력
- 응답: 안정화된 roll/pitch/yaw/throttle 출력(disarm이면 throttle −500), debug 값 순환, 자세, OSD 화면 데이터 (`mspWriteSimulatorOSD`)
- flags=0이 오면 HITL 해제 + disarm, `HITL_ENABLE` 없이 0 아닌 flag면 재부팅
- 대표 플러그인: `RomanLut/INAV-X-Plane-HITL`, `Scavanger/INAV-X-Plane-XITL` (docs/SITL/SITL.md 링크)

```text
 X-Plane (물리 · 시각)
    │ 플러그인 (UDP/USB-serial)
    ▼
 PC ── MSP_SIMULATOR(0x201F) 요청: gyro/acc/GPS/baro/mag/RC ──► 실제 INAV FC 보드
    ◄── 응답: 안정화 출력(roll/pitch/yaw/thr), 자세, OSD ────────┘
    │
    ▼  X-Plane의 기체 조종면/모터에 적용
```

### 10.3 왜 HITL이 테스트 엔지니어에게 중요한가

- SITL은 **PC 컴파일러 · PC 스케줄러** 위에서 돈다 (Betaflight SITL은 OS preemption 때문에 `TASK_EXEC_TIME_CLAMP_US` 까지 따로 둔다). 타이밍 · 메모리 · 인터럽트 · DMA 문제는 SITL에서 안 보인다
- HITL은 **진짜 MCU, 진짜 빌드 바이너리, 진짜 스케줄러**에 가짜 센서 값을 넣는다. 그래서 "릴리스 바이너리 그대로" 내비게이션 · failsafe 로직을 반복 실행할 수 있다
- 대가: 센서 드라이버(SPI gyro, UART GPS)와 필터 앞단은 **우회된다** (값이 MSP로 바로 `gyro.gyroADCf` 등에 들어감). 그래서 HITL 하나로 끝나지 않고 드라이버는 bench(HIL) 측정으로 보완해야 한다
- Betaflight 포크에 같은 기능이 필요하면: INAV의 `MSP_SIMULATOR` 설계를 참고해 포크 전용 MSP v2 명령을 만드는 것이 가장 짧은 길이다 [추정]. 넣을 경우 **릴리스 빌드에서는 컴파일 제외**(INAV도 `USE_SIMULATOR` 류 조건부 [확인 필요: 정확한 매크로])가 안전 요구사항이다

> ⚠️ Gemini 정정: Gemini는 Betaflight HIL에서 센서 주입은 "보조 MCU/FPGA로 SPI DRDY를 8 kHz로 흉내 내야 한다"고 했다. 그건 드라이버까지 포함한 완전 HIL의 경우다. INAV처럼 MSP로 센서 값을 넣는 HITL은 그런 하드웨어 없이 된다(드라이버 우회라는 한계 포함). Betaflight 쪽은 upstream에 MSP 센서 주입이 없으므로, 포크에 넣거나 SITL을 쓰거나 SPI 에뮬레이션을 하는 세 선택지가 있다 — 비용 · 충실도 비교는 [R08](2026-10-24_R08_hil_bench_design_instruments.md).

> ⚠️ Gemini 정정: Gemini 요약표는 INAV 상위 보드 연동을 "MSP/일부 MAVLink"로 적었다. INAV 저장소에는 `src/main/mavlink/` 폴더, `docs/Mavlink.md`, `src/test/mavlink`, `mavlink_unittest.cc` 가 있다. MAVLink 지원이 "일부"인지 "상당"인지는 기능 목록을 따로 봐야 한다 [확인 필요].

---

## 11. SSD 검증에서는 이랬다 → 드론(Betaflight)에서는 이렇다

| 주제 | SSD 검증에서는 (Don 경험 범위) | Betaflight 포크에서는 |
|---|---|---|
| 상태 조회 채널 | NVMe telemetry, UART 디버그 FW | MSP(`MSP_STATUS`, `MSP_DEBUG`), CLI(`status`, `tasks`), blackbox |
| 내부 계측 | health monitoring · shmoo용 debug FW를 따로 빌드 | 릴리스 FW에 이미 `debug_mode` 수십 종 + `tasks` late 통계가 들어 있음 |
| 시간 기준 | 커맨드 latency, QoS 꼬리(p99) | gyro 슬롯(125 µs) jitter, late task %, RX→motor latency |
| 오류 처리 | error reporting/handling scheme | failsafe 상태 머신, arming disable flags, disarm reason |
| 환경 스트레스 | 챔버 온도 · 전원 시퀀스 · stress로 latent defect 발굴 | 진동 · RF 손실 · 배터리 sag · 버스 오류(`status` 의 SPI/I2C error count) |
| 시퀀스 자동화 | UART 테스트 시퀀스 자동화 SDK (다른 엔지니어가 사용) | MSP/CLI 시퀀스 + SITL harness 시나리오, 같은 구조로 플랫폼화 |
| 설정 | FW 설정 · 펌웨어 업그레이드 호환 | parameter group(PG) 버전 + `diff all` 호환, `check-pg-ids` |
| 회귀 판정 | 로그 · telemetry 비교 | blackbox CSV 지표 비교 (rate, jitter, 오차, 이벤트) |
| 위험 | 데이터 손실 | 모터 회전 · 추락 → 안전 인터록이 먼저 |

공통점: "펌웨어 안에 이미 있는 계측점을 찾아 자동 판정으로 엮는다"는 일의 모양은 같다. 다른 점: 드론은 **실시간 루프 하나**가 모든 걸 지배하고, 테스트가 그 루프를 방해하면(USB/MSP 트래픽, CLI 모드) 측정 대상 자체가 바뀐다.

---

## 12. FTE — Betaflight 포크를 무엇을, 어떻게 테스트하나

### 12.1 층별 전략

```text
 ┌───────────── 층 ─────────────┬──────── 무엇을 ───────────────────┬──── 도구 ────────────────┐
 │ L0 정적 · 빌드                │ -Werror, 전 타깃 빌드, 크기 회귀   │ make checks, CI matrix   │
 │ L1 unit (googletest)          │ 파서, failsafe, arming, scheduler │ make test / test-all     │
 │ L2 SITL                       │ 모드 전환, RX loss 정책, rescue    │ sitl_harness.py + MSP    │
 │ L3 HITL (센서값 주입, 포크 기능) │ 실제 MCU에서 같은 시나리오          │ MSP v2 커스텀 [추정]      │
 │ L4 HIL bench (프롭 없음)       │ CRSF 주입→DShot 응답, 타이밍, 전원  │ USB-UART, 로직 분석기     │
 │ L5 비행 · 블랙박스 회귀        │ 튜닝 · 노이즈 · 실제 failsafe      │ blackbox_decode + 지표    │
 └───────────────────────────────┴───────────────────────────────────┴──────────────────────────┘
```

자세한 피라미드 논리는 [R07](2026-10-24_R07_test_methodology_pyramid.md).

### 12.2 unit — 포크 변경에 테스트를 붙이는 규칙

- 포크에서 바꾼 `.c` 파일마다 대응 `_unittest.cc` 가 있는지 확인. 없으면 9.4 절차로 추가
- 파서류(CRSF, MSP, 포크 전용 링크 프로토콜): 캡처 데이터 테스트(`TestCapturedData` 방식), 길이 경계, CRC 오류, 분할 수신, **inter-byte gap** 경계(CRSF는 시간으로 프레임을 끊으므로 `microsISR()` stub으로 시간 조작)
- failsafe/arming: upstream 테스트 이름을 그대로 회귀 목록으로 삼고, 포크 정책(예: 특정 기체는 DROP 대신 LAND)마다 테스트 추가
- `make junittest` 로 JUnit XML → CI 대시보드

### 12.3 SITL — 시나리오 기반 회귀

- upstream `sitl_harness.py` 를 포크 CI 잡으로: `make TARGET=SITL` → `--scenario all` → 종료 코드로 pass/fail
- 포크 기능용 시나리오 추가 패턴: `base_config(extra)` 로 설정 → `boot_and_arm()` → 자극(RC 변경, `rc.stop_stream()`, FDM에 오류) → `wait_for()` 로 MSP 상태(arming flags, mode) 또는 모터 trace 판정
- SITL에서 **보면 안 되는 것**: µs 단위 타이밍, CPU load, late task. SITL target이 일부러 클램프를 걸어 둔 영역이다

### 12.4 HIL — MSP / CRSF 주입

- CRSF 주입: USB-UART를 FC의 RX UART에 연결하고 420000 baud로 `0xC8 len 0x16 [22B] crc` 프레임을 정해진 주기(예: 500 Hz)로 송신. 수신기 없이 "조종기 → FC" 구간을 재현. 프레임 생성기는 [R05](2026-10-24_R05_rc_link_protocols_elrs_edgetx.md)
- 부정 테스트: 프레임 사이 gap을 `frameTimeNeededUs` 보다 짧게/길게, 길이 필드 변조, CRC 오류, 바이트 중간 끊김, 주기 지터 → `MSP_STATUS` 의 arming flags · `MSP_RC` 값 · `rc_smoothing_info` 로 판정
- MSP 관찰: [../code/msp_client.py](../code/msp_client.py) 로 `MSP_STATUS` 폴링. **주의: 폴링 자체가 SERIAL task 부하** → 폴링 주기를 바꿔 가며 `tasks` late 값이 변하는지 먼저 측정 (관찰자 효과)
- DShot 출력: 로직 분석기로 모터 핀 디코드. 프롭 제거, 전류 제한 전원 — 안전 문구는 [R08](2026-10-24_R08_hil_bench_design_instruments.md). 장비 구매는 사기 전에 Don과 예산 확인

### 12.5 blackbox 기반 회귀 지표

`blackbox_decode` CSV에서 뽑을 수 있는 지표 후보:

| 지표 | 계산 | 회귀 신호 |
|---|---|---|
| 루프 간격 | `time` 차분의 평균 · 표준편차 · max (sample rate 감안) | 스케줄러 회귀 |
| 스케줄러 결정성 | `debug_mode=SCHEDULER_DETERMINISM` 의 debug[7] 표준편차, late task ID 분포 | 새 task/드라이버가 슬롯 침범 |
| 추종 오차 | `setpoint` − `gyroADC` RMS (축별, 같은 stick 입력 재생 시) | PID/필터 변경 |
| 노이즈 | `gyroUnfilt` vs `gyroADC` 스펙트럼, 모터 대역 피크 | RPM/dyn notch 회귀 |
| D-term 노이즈 | `axisD` RMS | 필터 회귀 · 모터 온도 위험 |
| 모터 포화 | `motor` 가 최대값에 붙은 비율 | mixer/airmode 회귀 |
| eRPM 품질 | `eRPM` 결측 · `DSHOT_RPM_ERRORS` | bidir DShot 회귀 |
| RX 타이밍 | `debug_mode=RX_TIMING` 프레임 간격 분포 | RX 드라이버/링크 회귀 |
| failsafe | slow frame `failsafePhase`, `rxSignalReceived` 전이 시각, DISARM 이벤트 reason | 안전 회귀 |

비교는 같은 입력(SITL 재생 또는 HIL에서 같은 CRSF 시퀀스)으로 두 바이너리를 돌려 지표 차이에 허용 범위를 둔다. 실비행 로그끼리 비교는 바람 · 배터리 차이 때문에 통계적으로만 쓴다.

### 12.6 task 타이밍 검사 (`tasks` CLI)

1. `set task_statistics = ON`, `save` (STM32 F4 이상은 late 열 포함)
2. 안정 상태에서 `tasks` 한 번(max 리셋) → N초 대기 → `tasks` 다시 → 파싱
3. 판정 예: GYRO/PID `rate/hz` 가 목표의 ±1% 안, `late` 증가분 / `run` 증가분 < 기준, `Total` load < 기준, `status` 의 `Arming disable flags` 에 `LOAD` 없음
4. 부하 조건별 반복: configurator 연결/미연결, OSD on/off, blackbox on/off, MSP 폴링 10/50/100 Hz
5. CLI 모드는 비행 상태와 다르므로(blackbox 정지, `ARMING_DISABLED_CLI`) **최종 판정은 blackbox의 `SCHEDULER_DETERMINISM` 으로 교차 확인**

### 12.7 latency (RX → motor)

- 경로: CRSF 마지막 바이트(UART ISR) → `crsfFrameDone` → 다음 gyro 슬롯의 `rxFrameCheck()` → RX task 3단계(최대 3번의 스케줄링 기회) → PID 루프의 `processRcCommand()` (rc smoothing 필터 지연 포함) → `mixTable()` → DShot 프레임
- 측정: 로직 분석기 ch1 = CRSF TX(주입), ch2 = DShot 모터 출력. 계단 입력 후 DShot throttle 값이 바뀌는 첫 프레임까지. 수천 번 반복 → 히스토그램
- 해석 주의: rc smoothing은 **필터**라서 "첫 변화"와 "90% 도달"이 다르다. 두 지표를 따로 기록. K05의 latency 표와 연결

### 12.8 failsafe 타이밍

| 케이스 | 자극 | 기대 (upstream 기본값 기준) | 측정 |
|---|---|---|---|
| 짧은 끊김 | CRSF 100 ms 중단 | stage 1 미진입 또는 hold, 비행 유지 | blackbox `rxSignalReceived` |
| stage 1 | 300 ms 중단 | `RXLOSS`, 채널 hold/rxfail, 복구 시 즉시 조종 | OSD/MSP flags |
| stage 2 | 2 s 중단 | 약 1.5 s(`failsafe_delay`) 뒤 stage 2, DROP_IT → disarm | DISARM 이벤트 reason=FAILSAFE, 모터 핀 |
| throttle low 장기 | throttle low 10 s 후 끊김 | stage 1 없이 바로 disarm | 위와 같음 |
| 복구 | 끊김 후 재개 | 500 ms(`failsafe_recovery_delay`) 유효 RX 후 `ARMING_DISABLED_FAILSAFE` 해제 | MSP_STATUS |
| 스위치 | BOXFAILSAFE on (KILL 모드) | 즉시 LANDED → disarm | |
| 쓰레기 데이터 | CRC 오류 프레임만 계속 | 유효 프레임 없음과 같게 취급 | |

포크 설정값이 다르면 기대값을 **설정에서 계산**한다(`diff all` 로 읽은 값). 테스트 코드가 숫자를 하드코딩하면 설정 변경 때마다 깨진다.

### 12.9 arming 안전

- 각 arming disable flag를 **하나씩 의도적으로 세우고** arm 시도가 거부되는지 (예: throttle 올린 채, 기울인 채, CLI 모드, failsafe 스위치, 부팅 직후, `MSP_SET_ARMING_DISABLED`)
- arm 상태에서 `MSP_SET_MOTOR` 가 무시되는지, USB 분리 시 disarm 상태 모터가 멈추는지(`checkUsbLinkLost`)
- runaway takeoff: 모터 방향/순서를 의도적으로 틀린 설정(SITL 또는 프롭 없는 벤치)에서 `DISARM_REASON_RUNAWAY_TAKEOFF` 발생
- 판정은 MSP_STATUS의 arming flags(비트 순서 7.1)와 blackbox DISARM 이벤트

### 12.10 설정 마이그레이션과 `diff` 호환

- Betaflight 설정은 parameter group(PG) 단위이고 각 PG에 버전 번호가 있다 (예: `PG_REGISTER_WITH_RESET_TEMPLATE(failsafeConfig_t, failsafeConfig, PG_FAILSAFE_CONFIG, 2)`, `pidConfig ... 4`, `gyroConfig ... 10`). 구조가 바뀌면 버전을 올리고, 버전이 다르면 그 PG는 **기본값으로 리셋**된다 [확인 필요: 정확한 리셋 규칙은 `pg/pg.c`, `config/config_eeprom.c`]
- unit test 쪽 `check-pg-ids`(`check_pg_ids.awk`)가 PG ID 중복을 막는다. INAV는 아예 `pg-version-check.yml` CI가 있다
- 테스트: (1) 구버전 FW에서 `diff all` 저장 → (2) 신버전 flash → (3) `diff all` 재적용(batch) → (4) 다시 `diff all` 해서 비교. 차이가 나는 줄 = 이름 변경 · 범위 변경 · 삭제된 설정 → 릴리스 노트와 대조
- EEPROM 직접 업그레이드(재적용 없이)도 따로: 어떤 PG가 리셋됐는지 `diff` 로 확인. **failsafe/arming 관련 PG가 조용히 기본값으로 돌아가는 것**이 가장 위험

### 12.11 maintained fork의 upstream rebase 회귀 전략 [추정 · 설계 제안]

```text
 upstream master ──●──●──●──●──●──►
                         \ (정기 merge/rebase, 예: 릴리스 태그 단위)
 fork main      ──●──●────●(merge)──►
                            │
                 ┌──────────┴───────────────────────────────┐
                 │ 1. L0/L1: checks + test-all (upstream 그대로)│
                 │ 2. fork 전용 unit test                     │
                 │ 3. SITL harness 전체 + fork 시나리오        │
                 │ 4. HIL 야간: tasks 타이밍, latency 히스토그램 │
                 │ 5. blackbox 지표 diff vs 직전 fork 릴리스    │
                 │ 6. diff all 마이그레이션 검사               │
                 └──────────────────────────────────────────┘
```

- 포크 변경을 **작게 · 경계에** 둔다(새 파일, 새 MSP v2 ID 영역, `#if ENABLE_<FORK>_*`). upstream 파일 수정은 목록으로 관리
- upstream이 바꾼 파일과 포크가 바꾼 파일의 교집합 = rebase 위험 목록 → 그 파일들에 대한 테스트를 우선 실행
- "기준선(baseline) 바이너리"를 아티팩트로 보관해, 같은 하네스에서 old/new를 나란히 돌린다
- upstream의 새 테스트(예: 최근 들어온 SITL harness, failsafe autopilot 테스트)는 포크에도 그대로 가져온다 — 공짜 커버리지
- 플랫폼화 관점은 [R09](2026-10-24_R09_scalable_reusable_test_platform.md)

---

## 13. 테스트 체크리스트

- [ ] `make test` / `make test-all` 이 포크에서 경고 없이 통과 (`EXTRA_FLAGS=-Werror`)
- [ ] 포크가 바꾼 C 파일마다 대응 unit test 존재
- [ ] CRSF: 정상, 경계 길이(2, 62), 과대 길이, CRC 오류, gap 경계, subset 0x17 프레임
- [ ] MSP: v1/v2/v2-over-v1/JUMBO, 오류 응답 `$M!`, 미지원 명령
- [ ] scheduler: GYRO/PID rate가 목표 ±1% (`tasks`), late % < `cpu_late_limit`, `LOAD` flag 없음
- [ ] 부하 조건(configurator 연결, OSD, blackbox, MSP 폴링)별 타이밍 비교
- [ ] RX → DShot latency 분포 (첫 변화 / 90% 도달) 기준선 대비
- [ ] failsafe 7개 케이스 (12.8) 설정값 기반 기대치로 판정
- [ ] arming disable flag 각각 재현 → arm 거부 확인
- [ ] `MSP_SET_MOTOR` armed 상태 무시, USB 분리 시 모터 정지
- [ ] bidir DShot on/off에서 RPM filter 활성 여부와 `dshot_telemetry_info` 오류율
- [ ] blackbox 지표 diff (루프 간격, 추종 오차, 노이즈, eRPM 결측) 허용 범위 안
- [ ] `diff all` 구버전 → 신버전 재적용 후 차이 목록이 릴리스 노트와 일치
- [ ] failsafe/arming 관련 PG가 업그레이드 후 기본값으로 리셋되지 않았는지
- [ ] SITL harness 전체 시나리오 + 포크 시나리오 pass
- [ ] 위험 작업(모터 회전)은 프롭 제거 · 전류 제한 · 물리 kill 스위치 확인 후

---

## 14. 직접 해 볼 것 (읽기 순서 · 파일 경로)

이미 clone한 위치: `/private/tmp/claude-501/fte_src/betaflight`, `/private/tmp/claude-501/fte_src/inav` (임시 폴더라 지워질 수 있음 → 필요하면 다시 `git clone --depth 1`).

1. `src/main/main.c` → `run()` (5분)
2. `src/main/scheduler/scheduler.h` → 우선순위 enum, `task_t` (15분)
3. `src/main/scheduler/scheduler.c` `scheduler()` 503~847줄 — realtime 슬롯, dynamic priority, 시간 예산 (1시간, 4.3~4.5절을 옆에 두고)
4. `src/main/fc/tasks.c` — `task_attributes[]`, `taskUpdateRxMain()`, `tasksInit()` (30분)
5. `src/main/fc/core.c` 1416~1625줄 — `taskMainPidLoop()` 와 sub-task들, 그다음 `updateArmingStatus()` 357줄~, `tryArm()` 637줄~ (1시간)
6. `src/main/sensors/gyro.c` `gyroFiltering()` + `src/main/sensors/gyro_filter_impl.c` + `src/main/flight/rpm_filter.c` (40분)
7. `src/main/fc/rc.c` `processRcCommand()`, `updateRcRefreshRate()` (30분)
8. `src/main/flight/mixer.c` `mixTable()`, `applyMixToMotors()` (30분)
9. `src/main/drivers/dshot.c` `prepareDshotPacket()`, telemetry 디코드 (20분)
10. `src/main/rx/crsf_protocol.h` → `src/main/rx/crsf.c` `crsfDataReceive()`, `crsfFrameStatus()` → `src/main/rx/rx.c` `rxFrameCheck()`, `detectAndApplySignalLossBehaviour()` (1시간)
11. `src/main/flight/failsafe.h` → `failsafe.c` `failsafeUpdateState()` (40분, 6.3절 그림 직접 다시 그려 보기)
12. `src/main/msp/msp_serial.c` 상태 머신 → `msp.c` 의 `MSP_STATUS`, `MSP_SET_RAW_RC`, `MSP_SET_MOTOR` case (40분)
13. `src/main/cli/cli.c` `cliTasks()` 6677줄, `cliStatus()` (20분)
14. `src/main/blackbox/blackbox.c` 필드 정의 199줄~, `blackboxUpdate()` (30분)
15. `src/test/Makefile` 상단 주석 + `scheduler_unittest_SRC` → `src/test/unit/scheduler_unittest.cc`, `rx_crsf_unittest.cc`, `flight_failsafe_unittest.cc` (1시간)
16. `src/platform/SIMULATOR/target/SITL/target.h`, `sitl.c` 포트 정의 200줄 근처 → `src/test/sitl/sitl_harness.py` 의 `RcFeed`, `Msp`, `scenario_rx_loss` (1시간)
17. `.github/workflows/ci.yml` (10분)
18. INAV: `src/main/fc/fc_tasks.c` `cfTasks[]` → `src/main/scheduler/scheduler.c` realtime 분기 → `src/main/fc/fc_msp.c` `mspProcessSimulatorCommand()` 4708줄 → `docs/SITL/SITL.md` → `.github/workflows/ci.yml` test 잡 (1.5시간)
19. 실습(하드웨어 없음): Lima VM(Linux)에서 `make test_scheduler_unittest`, `make test_rx_crsf_unittest` 실행. macOS 직접 빌드는 clang 버전 차이로 실패할 수 있음 [확인 필요]
20. 실습(하드웨어 없음): `make TARGET=SITL` → `sitl_harness.py --scenario rx_...` 하나 실행 → `code/msp_client.py` 로 TCP 5761에 붙어 `MSP_STATUS` 읽기
21. 실습(하드웨어 필요 — 사기 전에 Don과 예산 확인): FC + USB-UART CRSF 주입 + 로직 분석기 latency 히스토그램. 프롭 장착 금지, 전류 제한 전원

---

## 15. 확인 안 된 것

- Neros 포크가 기반으로 한 Betaflight 버전 · 브랜치, 포크가 바꾼 영역 [추정 불가]
- 이 단원의 경로 · 줄 번호는 2026-10-25 master 기준. `4.5-maintenance` 등 릴리스 브랜치의 차이 [확인 필요]
- Betaflight SITL의 Gazebo 연동이 최신 Gazebo(gz-sim)에서 동작하는지, RealFlight 연동 절차 [확인 필요]
- `SITL_X_PLANE` 정확한 빌드 명령 (`make CONFIG=SITL_X_PLANE` 추정, `src/config` submodule 필요) [확인 필요]
- `sitl_harness.py` 가 upstream CI 어디선가 자동 실행되는지 — workflows grep 결과 없음 [추정: 수동]
- PG 버전 불일치 시 리셋 규칙의 정확한 동작 (`pg/pg.c`, `config/config_eeprom.c` 미독) [확인 필요]
- blackbox 저장 장치 용량 · 확장자는 보드별 [확인 필요]
- Python blackbox 파서 라이브러리(orangebox 등) 이름 · 상태 [확인 필요]
- INAV의 MSP_SIMULATOR를 릴리스 빌드에서 제외하는 정확한 매크로 [확인 필요]
- INAV MAVLink 지원 범위 [확인 필요]
- macOS에서 Betaflight unit test가 바로 도는지 [확인 필요]
- DShot 비트 타이밍 수치, ELRS 패킷 레이트별 실제 latency → [R05](2026-10-24_R05_rc_link_protocols_elrs_edgetx.md) 에서 다룸

---

## 16. 참고 링크

소스 (clone 기준):

- Betaflight: https://github.com/betaflight/betaflight — commit `d593400a05a5e142c0f6f83cc45c237ef078c634` (2026-10-25 03:53 +1100)
- INAV: https://github.com/iNavFlight/inav — commit `bc76329fcffff96f5a52881bcdc0ff65f7426e37` (2026-10-23 01:14 -0500)
- Betaflight config 저장소 (submodule `src/config`): https://github.com/betaflight/config
- Blackbox Explorer: https://github.com/betaflight/blackbox-log-viewer
- blackbox-tools (`blackbox_decode`, `blackbox_render`): https://github.com/betaflight/blackbox-tools (README WebFetch로 확인)
- Betaflight Configurator: https://github.com/betaflight/betaflight-configurator
- INAV X-Plane HITL 플러그인: https://github.com/RomanLut/INAV-X-Plane-HITL , XITL: https://github.com/Scavanger/INAV-X-Plane-XITL (INAV `docs/SITL/SITL.md` 의 링크)
- Extended DShot telemetry 설명 (dshot.c 주석 링크): https://github.com/bird-sanctuary/extended-dshot-telemetry

이 단원에서 직접 읽은 파일 (Betaflight): `src/main/main.c`, `src/main/fc/init.c`, `src/main/scheduler/scheduler.h`, `src/main/scheduler/scheduler.c`, `src/main/pg/scheduler.c`, `src/main/fc/tasks.c`, `src/main/fc/core.c`, `src/main/fc/core.h`, `src/main/fc/rc.c`, `src/main/fc/runtime_config.h`, `src/main/fc/runtime_config.c`, `src/main/fc/parameter_names.h`, `src/main/sensors/gyro.c`, `src/main/sensors/gyro_filter_impl.c`, `src/main/flight/rpm_filter.c`, `src/main/flight/mixer.c`, `src/main/flight/pid.c`, `src/main/flight/failsafe.h`, `src/main/flight/failsafe.c`, `src/main/rx/crsf_protocol.h`, `src/main/rx/crsf.h`, `src/main/rx/crsf.c`, `src/main/rx/rx.c`, `src/main/common/crc.c`, `src/main/common/crc.h`, `src/main/drivers/dshot.c`, `src/main/drivers/dshot.h`, `src/main/drivers/dshot_command.h`, `src/main/drivers/motor_types.h`, `src/main/msp/msp.h`, `src/main/msp/msp.c`, `src/main/msp/msp_serial.h`, `src/main/msp/msp_serial.c`, `src/main/msp/msp_protocol.h`, `src/main/msp/msp_protocol_v2_betaflight.h`, `src/main/cli/cli.c`, `src/main/cli/settings.c`, `src/main/blackbox/blackbox.h`, `src/main/blackbox/blackbox.c`, `src/main/blackbox/blackbox_virtual.c`, `src/platform/STM32/include/platform/platform.h`, `src/platform/SIMULATOR/target/SITL/target.h`, `src/platform/SIMULATOR/target/SITL/README.md`, `src/platform/SIMULATOR/sitl.c`, `src/test/Makefile`, `src/test/unit/scheduler_unittest.cc`, `src/test/unit/rx_crsf_unittest.cc`, `src/test/unit/flight_failsafe_unittest.cc`, `src/test/unit/arming_prevention_unittest.cc`, `src/test/sitl/sitl_harness.py`, `Makefile`, `.gitmodules`, `.github/workflows/ci.yml`, `pr.yml`, `push.yml`.

(INAV): `src/main/fc/fc_tasks.c`, `src/main/scheduler/scheduler.h`, `src/main/scheduler/scheduler.c`, `src/main/fc/fc_msp.c`, `src/main/fc/runtime_config.h`, `src/main/msp/msp_protocol_v2_inav.h`, `src/main/fc/settings.yaml`, `docs/SITL/SITL.md`, `docs/development/Building SITL.md`, `.github/workflows/ci.yml`, `.github/workflows/msp.yml`.

---

## 부록 A. Gemini · K05 경로 정정 목록

| 출처 | 원래 적힌 것 | 실제 (2026-10-25 master) |
|---|---|---|
| Gemini, K05 | `src/main/target/SITL/` | `src/platform/SIMULATOR/` (`target/SITL/target.h`, `sitl.c`, `udplink.c`) |
| Gemini | `src/test/unit/` + "Makefile" | 테스트 소스는 `src/test/unit/*.cc`, Makefile은 `src/test/Makefile`. 루트 `make test` 가 위임 |
| Gemini | `src/main/msp/` (일반) | 핵심 파일 `msp.c`(명령 처리), `msp_serial.c`(프레이밍), `msp_protocol.h`, `msp_protocol_v2_betaflight.h`, `msp_protocol_v2_common.h` |
| K05 | task 테이블 "tasks.c의 배열, scheduler.h 우선순위 enum" [확인 필요] | 확인됨: `task_attributes[]` (tasks.c 404줄), `taskPriority_e` · `taskId_e` (scheduler.h) |
| K05 | "gyro · filter · PID task가 실시간(gyro 동기)" [확인 필요] | 확인됨: `TASK_PRIORITY_REALTIME = -1`, scheduler()가 gyro 슬롯마다 직접 실행, EXTI skew 보정 |
| K05 | "다른 task는 우선순위 + 대기 시간 기준" [확인 필요] | 확인됨: `dynamicPriority = 1 + staticPriority × taskAgePeriods` + 예상 실행 시간이 남은 슬롯에 들어갈 때만 |
| K05 | `rc.c` (rc smoothing) | `src/main/fc/rc.c` (`processRcCommand`, `rcSmoothingSetFilterCutoffs`, `calculateFeedforward`) |
| K05 | mixer | `src/main/flight/mixer.c` `mixTable()` (+ `mixer_init.c`, `mixer_tricopter.c`) |
| K05 | `drivers/dshot*` | `src/main/drivers/dshot.c`, `dshot_command.c`, `dshot_bitbang_decode.c`, `motor.c` + 플랫폼 출력 `src/platform/STM32/pwm_output_dshot.c`, `pwm_output_dshot_hal.c`, `dshot_bitbang*.c` |
| K05 | `blackbox/` | `src/main/blackbox/blackbox.c`, `blackbox_encoding.c`, `blackbox_io.c`, `blackbox_virtual.c`, `blackbox_fielddefs.h` |
| K05 | CLI `tasks` [확인 필요: 출력 형식] | 확인됨 (4.7절): `rate/hz max/us avg/us maxload avgload total/ms late run reqd/us` |
| Gemini | INAV `src/main/navigation/` | 확인됨 |
| Gemini | "Betaflight = GPS 위치 유지 없음" | master에 ALTHOLD/POSHOLD/GPS_RESCUE/flight plan task 존재 |
| Gemini | CRSF "1바이트 resync" 펌웨어 동작 | Betaflight는 inter-byte 시간 간격 + 길이 검사 + CRC로 프레임 단위 폐기 |
| Gemini | DShot "11비트 + 4비트 CRC" | 11 + telemetry 1 + CRC 4 = 16비트, bidir는 CRC 반전 |
| Gemini | 블랙박스 "125 µs마다 저장" | 기본 `BLACKBOX_RATE_QUARTER` (PID 루프의 1/4) |
| Gemini | failsafe "0.5초 뒤 모터 0" | 기본 `failsafe_delay` 1.5 s → DROP_IT → disarm. 설정값으로 계산해야 함 |
| 새로 발견 | — | `src/test/sitl/sitl_harness.py` (upstream SITL end-to-end harness, MSP TCP 5761, UDP 9001~9004) |
