/*
난이도: Hard (시스템 아키텍처, 보안, 기능 안전)
이 단계는 OTA 시스템 전체의 안정성, 보안, 그리고 기능 안전을 보장하기 위한 아키텍처 수준의 설계를 요구합니다. 하드웨어, 소프트웨어, 네트워크, 그리고 안전 표준에 대한 종합적인 이해가 필요합니다.

36. A/B 파티션 업데이트 아키텍처 설계: 펌웨어 업데이트 실패 시에도 시스템 복구가 가능한 A/B 파티션 구조를 설계하세요. 현재 실행 중인 활성(Active) 파티션과 업데이트를 위한 비활성(Inactive) 파티션을 두고, 업데이트 완료 후 부팅 파티션을 전환하는 전체 로직을 설명하세요.
37. 부트로더의 핵심 로직 구현: 시스템 부팅 시, NVM에 저장된 플래그를 확인하여 A와 B 파티션 중 어느 쪽으로 부팅할지 결정하는 부트로더의 핵심 로직을 의사 코드로 작성하세요. 애플리케이션 무결성 검사(CRC/서명) 후 유효한 애플리케이션으로 점프(jump)하는 과정을 포함해야 합니다.
38. 자동 롤백(Rollback) 메커니즘: 새로 업데이트된 펌웨어(B 파티션)가 부팅 후 일정 시간 내에 "정상 동작" 신호(소프트웨어 워치독 pat 등)를 부트로더 영역에 기록하지 못하면, 시스템이 자동으로 재부팅하여 이전 버전(A 파티션)으로 복귀하는 롤백 메커니즘을 설계하고 구현하세요.
39. 보안 부트(Secure Boot) 체인 설계: 하드웨어 RoT(Root of Trust)에서 시작하여 1차 부트로더(PBL), 2차 부트로더(SBL), 그리고 최종 애플리케이션까지, 각 단계의 이미지를 암호학적 서명으로 검증하며 부팅하는 보안 부트 체인을 설명하세요.
40. 업데이트 패키지 종속성 관리: 여러 ECU에 대한 업데이트가 포함된 패키지를 처리할 때, ECU 간의 업데이트 순서 종속성(예: 게이트웨이 업데이트 선행)을 해석하고 순차적으로 업데이트를 진행하는 마스터 OTA 컨트롤러 로직을 설계하세요.
41. 암호화된 펌웨어 이미지 처리: 대칭키(예: AES-256)로 암호화된 펌웨어 이미지를 수신하여, HSM(Hardware Security Module)을 사용하여 안전하게 복호화한 후 플래시에 쓰는 과정을 구현하세요. 키가 펌웨어에 노출되지 않도록 설계해야 합니다.
42. 기능 안전성(ISO 26262)과 OTA: OTA 업데이트 과정이 차량의 기능 안전성에 미치는 영향을 설명하세요. 특히, 업데이트 중 '안전 상태(Safe State)'를 유지하고, ASIL 등급이 다른 소프트웨어 컴포넌트 간의 '간섭으로부터의 자유(Freedom from Interference)'를 보장하기 위한 아키텍처적 고려사항은 무엇인가요?
43. 데이터 오프로딩(Data Offloading) 전략: 차량이 Wi-Fi에 연결되었을 때는 대용량 SOTA 업데이트를 다운로드하고, 셀룰러(5G/LTE) 연결 시에는 중요한 FOTA 패치나 보안 업데이트만 다운로드하도록 하는 지능형 데이터 오프로딩 정책을 구현하세요.
44. 서비스 지향 아키텍처(SOA) 기반 OTA: AUTOSAR Adaptive Platform 환경에서, OTA 에이전트가 'UpdateManager' 서비스로 동작하고, 다른 애플리케이션들이 이 서비스에 업데이트 가능 여부를 쿼리(query)하고 진행 상태를 구독(subscribe)하는 SOA 기반의 OTA 시스템을 설계하세요.
45. 증분(Incremental) 업데이트 최적화: 컨테이너 기반의 SOTA 환경에서, 전체 컨테이너 이미지를 교체하는 대신 변경된 레이어(layer)나 파일만 전송하여 업데이트 크기와 시간을 최소화하는 증분 업데이트 전략을 설계하세요.
46. UDS Authentication (0x29) 서비스 구현: 기존의 Seed-Key 방식(0x27)보다 강화된 보안을 제공하는 UDS 인증 서비스(0x29)의 PKI(공개 키 기반 구조) 인증서 교환 및 양방향 인증 과정을 설명하고, 그 핵심 로직을 의사 코드로 작성하세요.
47. 네트워크 대역폭 관리: OTA 다운로드가 진행되는 동안 ADAS나 인포테인먼트와 같은 다른 네트워크 트래픽에 미치는 영향을 최소화하기 위해, QoS(Quality of Service) 정책을 적용하여 OTA 트래픽의 우선순위를 동적으로 조절하는 로직을 설명하세요.
48. 디지털 트윈(Digital Twin)을 이용한 OTA 검증: 실제 차량에 배포하기 전에, OTA 업데이트 패키지를 차량의 디지털 트윈 환경에 먼저 적용하여 잠재적인 문제를 사전에 시뮬레이션하고 검증하는 CI/CD 파이프라인의 개념을 설명하세요.
49. 동적 메모리 할당 없는 OTA 에이전트: malloc/free를 전혀 사용하지 않고, 오직 정적 메모리, 스택, 그리고 고정된 메모리 풀만을 사용하여 전체 OTA 다운로드 및 플래싱 과정을 처리하는 에이전트를 설계하세요. 이는 MISRA C 규격 준수와 결정론적 동작에 중요합니다.
50. SecOC(Secure On-board Communication)와 OTA: OTA 업데이트를 통해 새로운 SecOC 키를 안전하게 배포하고 활성화하는 과정을 설명하세요. 키 배포 중 도청이나 재전송 공격을 어떻게 방지할 수 있는지 논하세요.
*/

/*
난이도: Hard (시스템 아키텍처, 보안, 기능 안전)
이 단계는 OTA 시스템 전체의 안정성, 보안, 그리고 기능 안전을 보장하기 위한 아키텍처 수준의 설계를 요구합니다. 하드웨어, 소프트웨어, 네트워크, 그리고 안전 표준에 대한 종합적인 이해가 필요합니다.
*/

#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>

/*
36. A/B 파티션 업데이트 아키텍처 설계
   설명: 펌웨어 업데이트 실패 시에도 시스템 복구가 가능한 A/B 파티션 구조를 설계합니다.
   개념: 활성/비활성 파티션, 업데이트 후 부팅 파티션 전환.
   함수 시그니처: void ab_update(bool update_success);
   예시 입력: ab_update(true);
   예시 출력: active_partition == 1 (B)
*/

/*
36번 A/B 파티션 업데이트 아키텍처에 대해 자세히 설명드리고, 좀 더 실제 임베디드 환경에 가까운 예시 코드를 소개하겠습니다.

36. A/B 파티션 업데이트 아키텍처란?
개념 설명
    A/B 파티션 구조는 펌웨어 업데이트의 신뢰성과 복구성을 높이기 위해 널리 사용됩니다.
    A 파티션: 현재 실행 중인(Active) 펌웨어가 저장된 영역
    B 파티션: 업데이트를 위한 비활성(Inactive) 영역
    업데이트 시 B 파티션에 새 펌웨어를 다운로드하고, 검증이 끝나면 부팅 플래그를 B로 전환합니다.
    만약 B 파티션 부팅에 실패하면, 다시 A 파티션으로 롤백할 수 있습니다.
    이 구조는 업데이트 실패 시에도 장치가 벽돌(Bricking)되는 것을 방지합니다.
동작 흐름
    1. 정상 동작 중: A 파티션에서 부팅 및 실행
    2. 업데이트 시작: B 파티션에 새 펌웨어 다운로드
    3. 다운로드 및 검증 완료: 부트 플래그를 B로 변경
    4. 재부팅: B 파티션에서 부팅 시도
    5. 정상 동작 신호 감지: B 파티션이 정상 동작하면 B를 활성화로 유지
    6. 비정상/실패: 정상 신호가 없으면 부트로더가 A 파티션으로 롤백


실제 임베디드 환경에서의 구조 예시

*/
// 파티션 정보 및 부트 플래그 (NVM/Flash에 저장)
typedef enum { PART_A = 0, PART_B = 1 } partition_t;
typedef struct {
    partition_t active_partition;
    partition_t update_target;
    uint32_t    update_status; // 0: none, 1: downloading, 2: verifying, 3: ready
} BootFlag;

// NVM에 저장된 플래그 시뮬레이션
static BootFlag nvm_flag = { PART_A, PART_B, 0 };

// 파티션 무결성 검사 (실제 환경에서는 CRC/서명 검증)
// 함수 포인터로 선언하여 테스트에서 동적으로 교체 가능하게 수정
typedef bool (*verify_partition_func_t)(partition_t part);
bool verify_partition_impl(partition_t part) {
    // 예시: 항상 true 반환 (실제론 CRC/서명 체크)
    return true;
}
verify_partition_func_t verify_partition = verify_partition_impl;

// 부트로더에서 파티션 선택 및 롤백 처리
partition_t select_boot_partition(void) {
    // 업데이트가 완료되어 B로 전환된 경우
    if (nvm_flag.active_partition == PART_B) {
        // B 파티션 무결성 검사
        if (verify_partition(PART_B)) {
            return PART_B;
        } else {
            // B가 손상되었으면 A로 롤백
            nvm_flag.active_partition = PART_A;
            return PART_A;
        }
    } else {
        // 기본적으로 A에서 부팅
        return PART_A;
    }
}


// 업데이트 완료 후 부트 플래그 전환
void complete_update_and_switch(void) {
    nvm_flag.active_partition = PART_B;
    nvm_flag.update_status = 0;
}

// 롤백 처리 (B 부팅 실패 시)
void rollback_to_A(void) {
    nvm_flag.active_partition = PART_A;
    nvm_flag.update_status = 0;
}
/*

요약

A/B 파티션 구조는 OTA 업데이트의 안전성과 복구성을 보장합니다.
부트 플래그와 무결성 검사를 통해 안전하게 파티션을 전환하고, 실패 시 롤백합니다.
실제 임베디드 환경에서는 NVM/Flash에 플래그와 상태를 저장하고, CRC/서명 검증을 반드시 수행해야 합니다.
이 구조를 적용하면, 업데이트 실패에도 장치가 항상 정상 부팅할 수 있습니다!
*/



/*
37. 부트로더의 핵심 로직 구현
   설명: 부팅 시 NVM 플래그를 확인해 부팅 파티션 결정, 무결성 검사 후 점프.
   함수 시그니처: int bootloader_main(int nvm_flag, bool app_valid_A, bool app_valid_B);
   예시 입력: bootloader_main(1, true, true)
   예시 출력: return 1 (B 파티션 부팅)
*/
int bootloader_main(int nvm_flag, bool app_valid_A, bool app_valid_B) {
    int boot_part = 0;
    if (nvm_flag == 1 && app_valid_B) boot_part = 1;
    else if (app_valid_A) boot_part = 0;
    else boot_part = -1; // 부팅 실패
    // 실제로는 해당 파티션으로 점프
    return boot_part;
}

/*
38. 자동 롤백(Rollback) 메커니즘
   설명: B 파티션 부팅 후 일정 시간 내 정상 신호 없으면 A로 복귀.
   함수 시그니처: int rollback_check(bool booted_B, bool watchdog_pat);
   예시 입력: rollback_check(true, false)
   예시 출력: return 0 (A로 롤백)
*/
int rollback_check(bool booted_B, bool watchdog_pat) {
    if (booted_B && !watchdog_pat) return 0; // 롤백 to A
    return 1; // 유지
}

/*
39. 보안 부트(Secure Boot) 체인 설계
   설명: 각 단계의 이미지를 암호학적 서명으로 검증하며 부팅.
   함수 시그니처: int secure_boot(bool pbl_ok, bool sbl_ok, bool app_ok);
   예시 입력: secure_boot(true, true, true)
   예시 출력: return 0 (성공)
*/
int secure_boot(bool pbl_ok, bool sbl_ok, bool app_ok) {
    if (!pbl_ok) return -1;
    if (!sbl_ok) return -2;
    if (!app_ok) return -3;
    return 0;
}

/*
40. 업데이트 패키지 종속성 관리
   설명: ECU 간 업데이트 순서 종속성을 해석하고 순차적으로 업데이트.
   함수 시그니처: int update_sequence(const char* ecus[], int order[], int count);
   예시 입력: ecus={"GW","ADAS"}, order={0,1}, count=2
   예시 출력: return 0 (성공)
*/
int update_sequence(const char* ecus[], int order[], int count) {
    // 실제로는 종속성 그래프 해석 필요, 여기선 순서대로만 체크
    for (int i = 0; i < count; ++i) {
        if (order[i] != i) return -1;
    }
    return 0;
}

/*
41. 암호화된 펌웨어 이미지 처리
   설명: AES-256으로 암호화된 이미지를 HSM으로 복호화 후 플래시에 저장.
   함수 시그니처: int decrypt_and_flash(const uint8_t* enc, size_t len, uint8_t* out);
   예시 입력: enc={1,2,3}, len=3
   예시 출력: out={1,2,3}, return 0
*/
int decrypt_and_flash(const uint8_t* enc, size_t len, uint8_t* out) {
    // 실제로는 HSM 연동, 여기선 더미 복호화
    memcpy(out, enc, len);
    return 0;
}

/*
42. 기능 안전성(ISO 26262)과 OTA
   설명: 업데이트 중 안전 상태 유지, ASIL 등급별 간섭 방지 아키텍처 고려.
   함수 시그니처: const char* explain_iso26262_ota(void);
   예시 출력: "업데이트 중 안전상태, ASIL 간섭방지"
*/
const char* explain_iso26262_ota(void) {
    return "업데이트 중 안전상태, ASIL 간섭방지";
}

/*
43. 데이터 오프로딩(Data Offloading) 전략
   설명: Wi-Fi 연결 시 대용량 SOTA, 셀룰러 시 FOTA만 다운로드.
   함수 시그니처: const char* data_offloading_policy(bool wifi, bool cellular);
   예시 입력: wifi=true, cellular=false
   예시 출력: "SOTA"
*/
const char* data_offloading_policy(bool wifi, bool cellular) {
    if (wifi) return "SOTA";
    if (cellular) return "FOTA";
    return "NONE";
}

/*
44. SOA 기반 OTA 설계
   설명: OTA 에이전트가 UpdateManager 서비스로 동작, 쿼리/구독 지원.
   함수 시그니처: const char* explain_soa_ota(void);
   예시 출력: "UpdateManager 서비스, 쿼리/구독 지원"
*/
const char* explain_soa_ota(void) {
    return "UpdateManager 서비스, 쿼리/구독 지원";
}

/*
45. 증분(Incremental) 업데이트 최적화
   설명: 변경된 레이어/파일만 전송하여 업데이트 크기/시간 최소화.
   함수 시그니처: int incremental_update(const uint8_t* old, const uint8_t* patch, size_t len, uint8_t* out);
   예시 입력: old={1,2,3}, patch={0,1,0}, len=3
   예시 출력: out={1,3,3}, return 0
*/
int incremental_update(const uint8_t* old, const uint8_t* patch, size_t len, uint8_t* out) {
    for (size_t i = 0; i < len; ++i)
        out[i] = old[i] + patch[i];
    return 0;
}

/*
46. UDS Authentication (0x29) 서비스 구현
   설명: PKI 인증서 교환 및 양방향 인증 의사 코드.
   함수 시그니처: int uds_authenticate(const char* cert, const char* challenge, const char* response);
   예시 입력: cert="CERT", challenge="CH", response="CH"
   예시 출력: return 0 (성공)
*/
int uds_authenticate(const char* cert, const char* challenge, const char* response) {
    // 실제로는 PKI 인증서 검증, challenge-response 비교
    if (strcmp(challenge, response) == 0) return 0;
    return -1;
}

/*
47. 네트워크 대역폭 관리
   설명: QoS 정책으로 OTA 트래픽 우선순위 동적 조절.
   함수 시그니처: int set_ota_qos(int base_priority, bool ota_active);
   예시 입력: base_priority=5, ota_active=true
   예시 출력: return 10
*/
int set_ota_qos(int base_priority, bool ota_active) {
    return ota_active ? base_priority + 5 : base_priority;
}

/*
48. 디지털 트윈을 이용한 OTA 검증
   설명: 디지털 트윈 환경에 OTA 패키지 적용, 사전 검증.
   함수 시그니처: const char* explain_digital_twin_ota(void);
   예시 출력: "디지털 트윈에서 사전 검증"
*/
const char* explain_digital_twin_ota(void) {
    return "디지털 트윈에서 사전 검증";
}

/*
49. 동적 메모리 할당 없는 OTA 에이전트
   설명: malloc/free 없이 정적 메모리, 스택, 고정 메모리 풀만 사용.
   함수 시그니처: int static_ota_agent(void);
   예시 출력: return 0
*/
int static_ota_agent(void) {
    static uint8_t pool[1024];
    pool[0] = 1;
    return pool[0] == 1 ? 0 : -1;
}

/*
50. SecOC와 OTA
   설명: OTA로 SecOC 키를 안전하게 배포, 도청/재전송 공격 방지.
   함수 시그니처: const char* explain_secoc_ota(void);
   예시 출력: "암호화/인증, nonce로 재전송 방지"
*/
const char* explain_secoc_ota(void) {
    return "암호화/인증, nonce로 재전송 방지";
}

// ------------------- Test code -------------------
#define LABEL_WIDTH 48
void test_result(const char* label, int ok) {
    printf("%-*s %s\n", LABEL_WIDTH, label, ok ? "[PASS]" : "[FAIL]");
}

int main(void) {
    // 36. A/B 파티션 업데이트 아키텍처
    // 1. 기본 부팅 (A)
    nvm_flag.active_partition = PART_A;
    test_result("36. 기본 부팅 (A)", select_boot_partition() == PART_A);

    // 2. 업데이트 완료 후 B로 전환
    complete_update_and_switch();
    test_result("36. 업데이트 후 B 부팅", select_boot_partition() == PART_B);

    // 3. B 파티션 무결성 실패 시 롤백
    // 함수 포인터를 임시로 false 반환 함수로 교체
    bool verify_partition_false(partition_t part) { return false; }
    verify_partition_func_t orig_verify = verify_partition;
    verify_partition = verify_partition_false;
    nvm_flag.active_partition = PART_B;
    test_result("36. B 무결성 실패 롤백", select_boot_partition() == PART_A);
    verify_partition = orig_verify; // 원복





    // 37. 부트로더 핵심 로직
    test_result("37. bootloader_main (B)", bootloader_main(1, true, true) == 1);
    test_result("37. bootloader_main (A)", bootloader_main(0, true, false) == 0);
    test_result("37. bootloader_main (fail)", bootloader_main(1, false, false) == -1);

    // 38. 자동 롤백
    test_result("38. rollback_check (rollback)", rollback_check(true, false) == 0);
    test_result("38. rollback_check (keep)", rollback_check(true, true) == 1);

    // 39. 보안 부트 체인
    test_result("39. secure_boot (all ok)", secure_boot(true, true, true) == 0);
    test_result("39. secure_boot (fail PBL)", secure_boot(false, true, true) == -1);

    // 40. 업데이트 패키지 종속성 관리
    const char* ecus[] = {"GW", "ADAS"};
    int order[] = {0, 1};
    test_result("40. update_sequence (ok)", update_sequence(ecus, order, 2) == 0);
    int bad_order[] = {1, 0};
    test_result("40. update_sequence (fail)", update_sequence(ecus, bad_order, 2) == -1);

    // 41. 암호화된 펌웨어 이미지 처리
    uint8_t enc[3] = {1,2,3}, dec[3] = {0};
    test_result("41. decrypt_and_flash", decrypt_and_flash(enc, 3, dec) == 0 && dec[2] == 3);

    // 42. 기능 안전성(ISO 26262)과 OTA
    test_result("42. explain_iso26262_ota", strcmp(explain_iso26262_ota(), "업데이트 중 안전상태, ASIL 간섭방지") == 0);

    // 43. 데이터 오프로딩 전략
    test_result("43. data_offloading_policy (wifi)", strcmp(data_offloading_policy(true, false), "SOTA") == 0);
    test_result("43. data_offloading_policy (cellular)", strcmp(data_offloading_policy(false, true), "FOTA") == 0);

    // 44. SOA 기반 OTA 설계
    test_result("44. explain_soa_ota", strcmp(explain_soa_ota(), "UpdateManager 서비스, 쿼리/구독 지원") == 0);

    // 45. 증분 업데이트 최적화
    uint8_t old[3] = {1,2,3}, patch[3] = {0,1,0}, out[3] = {0};
    test_result("45. incremental_update", incremental_update(old, patch, 3, out) == 0 && out[1] == 3);

    // 46. UDS Authentication 서비스
    test_result("46. uds_authenticate (ok)", uds_authenticate("CERT", "CH", "CH") == 0);
    test_result("46. uds_authenticate (fail)", uds_authenticate("CERT", "CH", "NO") == -1);

    // 47. 네트워크 대역폭 관리
    test_result("47. set_ota_qos (active)", set_ota_qos(5, true) == 10);
    test_result("47. set_ota_qos (inactive)", set_ota_qos(5, false) == 5);

    // 48. 디지털 트윈 OTA 검증
    test_result("48. explain_digital_twin_ota", strcmp(explain_digital_twin_ota(), "디지털 트윈에서 사전 검증") == 0);

    // 49. 동적 메모리 할당 없는 OTA 에이전트
    test_result("49. static_ota_agent", static_ota_agent() == 0);

    // 50. SecOC와 OTA
    test_result("50. explain_secoc_ota", strcmp(explain_secoc_ota(), "암호화/인증, nonce로 재전송 방지") == 0);

    return 0;
}