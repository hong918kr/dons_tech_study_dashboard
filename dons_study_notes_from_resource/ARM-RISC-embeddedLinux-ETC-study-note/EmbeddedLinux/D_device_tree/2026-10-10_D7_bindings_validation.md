# D7. Bindings와 검증 — YAML 스키마 · dt-schema · dt_binding_check · dtbs_check

> **이 노트를 다 읽으면**: binding이 "DT 작성자 · 드라이버 · 검증기" 셋이 공유하는 계약서라는 것을 설명할 수 있다 · YAML binding의 각 블록(`$id`, `properties`, `required`, `allOf`/`$ref`, `unevaluatedProperties`, `examples`)을 읽고 직접 쓸 수 있다 · `make dt_binding_check`/`dtbs_check`와 `dt-validate`로 binding과 보드 DTB를 검증하고 에러 메시지를 해석할 수 있다 · `checkpatch.pl`·`get_maintainer.pl`로 binding 패치를 다듬어 devicetree 메일링 리스트 리뷰 흐름에 올릴 수 있다
> **왜 필요한가**: 새 센서를 보드에 붙이면 "이 compatible에 binding이 있나, 우리 DTS가 그걸 지키나"를 매번 확인해야 하고, upstream에 드라이버를 올리면 첫 리뷰 코멘트의 절반이 binding에 달린다. 면접에서는 "DT binding이 뭐고 어떻게 검증하나요?", "벤더 BSP DTS가 upstream binding과 안 맞으면 어떻게 하나요?"가 나온다
> **Don 기준 난이도**: 레지스터 맵 문서·인터페이스 스펙을 "HW와 FW 사이의 계약"으로 다뤄 본 경험은 그대로 쓸 수 있다 / json-schema 문법, dt-schema 도구 체인, 커널 커뮤니티의 리뷰 관습(DT maintainers, 패치 순서, 금지 규칙)이 새로 배울 부분
> **선행 노트**: D2(문법), D3(compatible 매칭), D4(자원 기술), D5(파일 구조·dtc) — E7(패치 제출)과 같이 보면 좋다

---

## 0. 큰 그림 — 이게 왜 필요한가

Don이 SSD 컨트롤러 bring-up을 할 때 가장 무서웠던 버그는 "HW 팀 레지스터 문서와 FW가 가정한 비트 정의가 달랐던" 종류였을 것이다. 둘 다 각자 맞다고 생각하지만 **공유하는 정의서**가 없거나 낡아서 생기는 버그다. 해결책은 늘 같다. 정의서를 하나로 만들고, 사람이 아니라 **도구가** 양쪽이 그 정의서를 지키는지 검사하게 한다(레지스터 맵에서 헤더를 자동 생성하는 것처럼).

Device Tree에서 그 정의서가 **binding**이다. D3에서 본 것처럼 DTS의 노드는 `compatible = "invensense,mpu6050"` 같은 문자열로 드라이버와 짝지어진다. 그런데 "이 compatible을 가진 노드에는 어떤 속성이 있어야 하고, 각 속성은 몇 개의 값을, 어떤 범위로 가져야 하나"는 누가 정하나? DTS 작성자(보드 BSP 엔지니어)와 드라이버 작성자는 서로 다른 사람, 심지어 다른 회사다. 둘 사이 계약서가 `Documentation/devicetree/bindings/` 아래의 binding 문서이고, 지금은 그 문서가 **기계가 읽을 수 있는 YAML(json-schema)** 이라서 `dt-schema` 도구가 DTB를 자동으로 검사한다.

```svg
<svg viewBox="0 0 680 310" xmlns="http://www.w3.org/2000/svg">
  <defs><marker id="ah1" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto-start-reverse"><path d="M0,0 L10,5 L0,10 z" fill="currentColor"/></marker></defs>
  <rect x="230" y="16" width="220" height="64" rx="8" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
  <text x="340" y="42" text-anchor="middle" font-size="14">Binding (YAML) — 계약서</text>
  <text x="340" y="64" text-anchor="middle" font-size="12">vendor,device.yaml</text>
  <rect x="20" y="150" width="200" height="64" rx="8" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
  <text x="120" y="176" text-anchor="middle" font-size="14">DTS 작성자</text>
  <text x="120" y="198" text-anchor="middle" font-size="12">보드 BSP · board.dts</text>
  <rect x="460" y="150" width="200" height="64" rx="8" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
  <text x="560" y="176" text-anchor="middle" font-size="14">드라이버 작성자</text>
  <text x="560" y="198" text-anchor="middle" font-size="12">probe() · of_match_table</text>
  <rect x="230" y="236" width="220" height="60" rx="8" fill="#d0564a" fill-opacity="0.12" stroke="#d0564a"/>
  <text x="340" y="261" text-anchor="middle" font-size="14">검증기 dt-schema</text>
  <text x="340" y="282" text-anchor="middle" font-size="12">dt-validate · dt-doc-validate</text>
  <line x1="150" y1="148" x2="250" y2="84" stroke="currentColor" marker-end="url(#ah1)"/>
  <text x="36" y="112" font-size="12">필수 속성을 다 쓴다</text>
  <line x1="530" y1="148" x2="430" y2="84" stroke="currentColor" marker-end="url(#ah1)"/>
  <text x="500" y="112" font-size="12">적힌 속성만 읽는다</text>
  <line x1="340" y1="234" x2="340" y2="84" stroke="currentColor" stroke-dasharray="5 4" marker-end="url(#ah1)"/>
  <text x="348" y="150" font-size="12">스키마로 읽음</text>
  <line x1="222" y1="214" x2="262" y2="236" stroke="currentColor" marker-end="url(#ah1)"/>
  <text x="140" y="246" font-size="12">DTB 검사</text>
</svg>
```
그림 1 — binding은 세 당사자의 계약서다. DTS는 binding이 요구하는 속성을 다 써야 하고, 드라이버는 binding에 적힌 속성만 읽어야 하며, 검증기는 binding을 스키마로 읽어서 DTB가 계약을 지키는지 자동으로 검사한다. 커널은 실행 중에 YAML을 읽지 않는다 — 계약은 빌드·리뷰 시점에만 강제된다.

이 노트의 흐름은 이렇다.

| 절 | 내용 | 손으로 하는 것 |
|---|---|---|
| 1 | binding이란, 텍스트 → YAML 역사 | 트리의 YAML/txt 개수 세기, 낡은 .txt binding 읽기 |
| 2 | YAML binding 해부 | 실제 `invensense,mpu6050.yaml` 한 줄씩 |
| 3 | 도구 설치와 `dt_binding_check` | venv에 dtschema, 읽기 전용 소스에서 `O=` 빌드 |
| 4 | 내 binding 쓰기 | `don,d7-imu.yaml` — 실제 에러 3개를 만나고 고친다 |
| 5 | DTS 검증 | 일부러 틀린 .dts → `dt-validate` 에러 해석 |
| 6 | `dtbs_check` | i.MX8MP·AM62 보드 86개, QEMU virt.dtb |
| 7 | 리뷰 규칙과 패치 흐름 | `checkpatch.pl`, `get_maintainer.pl` |

> 실행 환경: Lima VM `elinux`(aarch64, `uname -r` = `7.0.0-38-generic`, Python 3.14.4). 커널 소스는 공용 랩 `/var/lab/src/linux-6.18.55`(읽기 전용, 6.18.55 LTS). 도구: dtschema 2026.9(jsonschema 4.26.0, pylibfdt 1.7.2), yamllint 1.38.0, 시스템 `dtc` 1.7.2. 모든 산출물은 `/var/tmp/D7/` 아래에 만들었다. 시간·경로 외의 값은 실행 결과 그대로다.

---

## 1. Binding이란 무엇인가

### 1.1 직관 — "이 compatible이면 이 속성들"

binding을 한 문장으로 말하면 **"compatible 문자열 하나(또는 한 묶음)에 대해, 노드가 가져야 할 속성과 자식 노드의 규칙"** 이다. 예를 들어 센서 키트에 InvenSense IMU를 붙인다면 binding은 이렇게 말한다.

- `compatible`은 이 목록 중 하나여야 한다 (`invensense,mpu6050`, `invensense,icm20602` …).
- `reg`는 정확히 1개 (I2C 주소 또는 SPI chip select).
- `interrupts`는 정확히 1개이고 **필수**.
- `vdd-supply`, `vddio-supply`, `mount-matrix`는 있어도 된다 (선택).
- 그 밖의 속성은 **허용하지 않는다**.

Don에게 익숙한 말로는 "레지스터 맵 문서의 필드 정의표"다. 필드 이름, 폭, 허용 값, 필수 여부가 있고, 문서에 없는 비트를 쓰면 안 된다. 차이는 대상이 레지스터가 아니라 **DT 노드의 속성**이라는 것뿐이다.

### 1.2 정의 — binding이 정하는 것과 정하지 않는 것

| binding이 정하는 것 | binding이 정하지 않는 것 |
|---|---|
| 허용되는 `compatible` 값과 순서(fallback) | 드라이버가 그 HW를 어떻게 구동하는지 |
| 필수(required) / 선택 속성 | Linux 드라이버의 이름·구조 |
| 각 속성의 타입, 개수, 값 범위, enum | 소프트웨어 정책(샘플링 주기, 로그 레벨 …) |
| `*-names` 목록과 순서 (clock-names 등) | 보드마다 다른 실제 값 (그건 .dts가 정한다) |
| 허용되는 자식 노드와 그 규칙 | 런타임 동작 (커널은 YAML을 읽지 않는다) |

마지막 줄이 중요하다. binding은 **문서이자 테스트 스펙**이다. 커널 이미지에 들어가지 않고, 부팅 중에 아무도 확인하지 않는다. 그래서 binding을 어긴 DTB도 부팅은 된다 — 드라이버가 기대한 속성이 없으면 probe가 실패하거나(`-EINVAL`, `-ENODEV`), 더 나쁘게는 조용히 기본값으로 동작한다. 검증을 빌드 단계로 끌어온 것이 dt-schema다.

### 1.3 binding은 ABI다

binding은 커널 소스 안에 있지만 **Linux만의 것이 아니다**. U-Boot, FreeBSD, Zephyr, 하이퍼바이저가 같은 DTB를 읽는다. 그리고 출하된 장비의 DTB는 부트로더 파티션에 박혀서 커널만 업데이트되는 경우가 흔하다. 그래서 커널 문서 `Documentation/devicetree/bindings/ABI.rst`는 binding을 **안정된 ABI**로 다룬다.

- 새 커널은 옛 DTB에서도 동작해야 한다.
- 속성은 **추가**할 수 있지만 기존 속성의 의미를 바꾸면 안 된다. 새 속성이 없으면 드라이버는 옛 동작을 기본값으로 쓴다.
- 정말 호환되지 않는 변경이 필요하면 **compatible 문자열을 새로 만든다**. 드라이버는 둘 다 매칭한다.

실무 결론: 기존 binding에 새 **필수** 속성을 추가하는 패치는 거의 항상 반려된다. 이미 출하된 DTB가 하루아침에 "binding 위반"이 되기 때문이다.

### 1.4 역사 — 자유 텍스트 .txt에서 YAML 스키마로

처음(2010년대 초) binding은 `Documentation/devicetree/bindings/` 아래의 **자유 형식 텍스트 파일**이었다. "Required properties: …, Optional properties: …"를 사람이 영어로 썼다. 문제는 세 가지였다.

1. **기계가 못 읽는다.** DTS가 binding을 지키는지 확인하려면 리뷰어가 눈으로 비교해야 했다.
2. **예제와 본문이 어긋나도 아무도 모른다.** 아래 예제 2에서 실제로 본다.
3. 형식이 제각각이라 같은 개념(`*-supply`, `interrupts`)을 파일마다 다르게 설명했다.

그래서 Rob Herring(DT maintainer)이 **json-schema를 YAML로 쓰는 DT schema 형식**과 검증 도구 `dt-schema`(github.com/devicetree-org/dt-schema, pip 패키지 이름 `dtschema`)를 만들었고, 2019년 무렵(v5.2 전후)부터 커널에 YAML binding이 들어오기 시작했다. 지금 규칙은 `submitting-patches.rst`에 명시돼 있다: **새 binding은 모두 DT schema 형식**이고, `make dt_binding_check`를 통과해야 한다. 옛 .txt는 누군가 "Convert to DT schema" 패치를 보낼 때마다 하나씩 사라지고 있다.

### 1.5 예제 1 — 트리에 YAML과 .txt가 몇 개 남았나

무엇을 확인하나: 6.18.55 소스에서 YAML binding과 남은 .txt binding의 개수, 그리고 디렉터리별 분포.

```sh
limactl shell elinux -- bash -lc 'cd /var/lab/src/linux-6.18.55/Documentation/devicetree/bindings
  find . -name "*.yaml" | wc -l; find . -name "*.txt" | wc -l
  for d in $(ls -d */ | tr -d /); do
    echo "$d $(find $d -name "*.yaml" | wc -l) $(find $d -name "*.txt" | wc -l)"
  done | sort -k3 -n -r | head -8'
```

```text
5182
814
sound 357 100
net 248 64
pinctrl 202 47
media 216 42
clock 426 36
regulator 118 35
arm 170 33
powerpc 2 30
```

출력에서 볼 것: YAML 5182개, .txt 814개 — 전체의 약 86%가 이미 스키마다. 남은 .txt는 오래된 서브시스템(sound, net)과 거의 유지보수되지 않는 아키텍처(powerpc는 30개 중 YAML이 2개뿐)에 몰려 있다. (최상위의 `graph.txt`, `common-properties.txt` 같은 일반 설명 문서 몇 개도 .txt 개수에 포함된다.)

```svg
<svg viewBox="0 0 680 340" xmlns="http://www.w3.org/2000/svg">
  <text x="340" y="24" text-anchor="middle" font-size="14">Linux 6.18.55 — 디렉터리별 binding 형식 (YAML / txt)</text>
  <rect x="300" y="32" width="14" height="12" fill="#4a7bd0" fill-opacity="0.7"/>
  <text x="320" y="43" font-size="12">YAML</text>
  <rect x="370" y="32" width="14" height="12" fill="#e08a3c" fill-opacity="0.8"/>
  <text x="390" y="43" font-size="12">.txt</text>
  <text x="102" y="65" text-anchor="end" font-size="13">sound</text>
  <rect x="110" y="50" width="309.1" height="20" fill="#4a7bd0" fill-opacity="0.7"/>
  <rect x="419.1" y="50" width="86.6" height="20" fill="#e08a3c" fill-opacity="0.8"/>
  <text x="511.7" y="65" font-size="12">357 / 100 (22% txt)</text>
  <text x="102" y="95" text-anchor="end" font-size="13">net</text>
  <rect x="110" y="80" width="214.7" height="20" fill="#4a7bd0" fill-opacity="0.7"/>
  <rect x="324.7" y="80" width="55.4" height="20" fill="#e08a3c" fill-opacity="0.8"/>
  <text x="386.1" y="95" font-size="12">248 / 64 (21% txt)</text>
  <text x="102" y="125" text-anchor="end" font-size="13">pinctrl</text>
  <rect x="110" y="110" width="174.9" height="20" fill="#4a7bd0" fill-opacity="0.7"/>
  <rect x="284.9" y="110" width="40.7" height="20" fill="#e08a3c" fill-opacity="0.8"/>
  <text x="331.6" y="125" font-size="12">202 / 47 (19% txt)</text>
  <text x="102" y="155" text-anchor="end" font-size="13">media</text>
  <rect x="110" y="140" width="187.0" height="20" fill="#4a7bd0" fill-opacity="0.7"/>
  <rect x="297.0" y="140" width="36.4" height="20" fill="#e08a3c" fill-opacity="0.8"/>
  <text x="339.4" y="155" font-size="12">216 / 42 (16% txt)</text>
  <text x="102" y="185" text-anchor="end" font-size="13">clock</text>
  <rect x="110" y="170" width="368.8" height="20" fill="#4a7bd0" fill-opacity="0.7"/>
  <rect x="478.8" y="170" width="31.2" height="20" fill="#e08a3c" fill-opacity="0.8"/>
  <text x="516.0" y="185" font-size="12">426 / 36 (8% txt)</text>
  <text x="102" y="215" text-anchor="end" font-size="13">regulator</text>
  <rect x="110" y="200" width="102.2" height="20" fill="#4a7bd0" fill-opacity="0.7"/>
  <rect x="212.2" y="200" width="30.3" height="20" fill="#e08a3c" fill-opacity="0.8"/>
  <text x="248.5" y="215" font-size="12">118 / 35 (23% txt)</text>
  <text x="102" y="245" text-anchor="end" font-size="13">arm</text>
  <rect x="110" y="230" width="147.2" height="20" fill="#4a7bd0" fill-opacity="0.7"/>
  <rect x="257.2" y="230" width="28.6" height="20" fill="#e08a3c" fill-opacity="0.8"/>
  <text x="291.8" y="245" font-size="12">170 / 33 (16% txt)</text>
  <text x="102" y="275" text-anchor="end" font-size="13">powerpc</text>
  <rect x="110" y="260" width="1.7" height="20" fill="#4a7bd0" fill-opacity="0.7"/>
  <rect x="111.7" y="260" width="26.0" height="20" fill="#e08a3c" fill-opacity="0.8"/>
  <text x="143.7" y="275" font-size="12">2 / 30 (94% txt)</text>
  <line x1="110" y1="46" x2="110" y2="286" stroke="currentColor"/>
  <text x="340" y="314" text-anchor="middle" font-size="13">전체: YAML 5182 · txt 814 (txt 비율 13.6%) — find로 직접 센 값</text>
</svg>
```
그림 2 — .txt가 많이 남은 상위 8개 디렉터리. 막대 길이는 파일 수(같은 축척). 새로 활발한 서브시스템(clock, iio)은 거의 다 YAML이고, 남은 .txt는 오래된 영역에 몰려 있다. 참고로 iio는 YAML 348 · txt 3이다.

### 1.6 예제 2 — 텍스트 binding이 썩는 방식

무엇을 확인하나: 아직 .txt로 남은 iio binding 하나에서 본문과 예제가 같은 말을 하는지.

```sh
limactl shell elinux -- bash -lc 'cd /var/lab/src/linux-6.18.55
  ls Documentation/devicetree/bindings/iio/*/*.txt
  grep -n "compatible" Documentation/devicetree/bindings/iio/accel/lis302.txt | head -3'
```

```text
Documentation/devicetree/bindings/iio/accel/lis302.txt
Documentation/devicetree/bindings/iio/adc/xilinx-xadc.txt
8: - compatible: 		should be set to "st,lis3lv02d-spi"
15: - compatible:		should be set to "st,lis3lv02d"
68:		compatible = "st,lis302dl-spi";
```

출력에서 볼 것: 본문(8행)은 SPI 버전 compatible이 `"st,lis3lv02d-spi"`라고 하는데, 같은 파일의 예제(68행)는 `"st,lis302dl-spi"`를 쓴다. 어느 쪽이 맞는지 텍스트만으로는 알 수 없고, 아무 도구도 이걸 잡지 못한다. YAML binding에서는 예제가 **빌드되고 스키마로 검증되므로** 이런 불일치가 `make dt_binding_check`에서 바로 에러가 된다. 이 파일은 그 밖에도 `Vdd-supply`(대문자), `st,click-single-{x,y,z}`(중괄호로 여러 속성을 한 줄에 표기) 같은 지금 규칙으로는 허용되지 않는 표기를 쓴다 — 변환 패치가 오면 같이 정리될 것이다.

---

## 2. YAML binding 해부 — `invensense,mpu6050.yaml` 한 줄씩

### 2.1 실제 파일

센서 키트에서 가장 흔한 IMU binding을 그대로 본다(소스 `Documentation/devicetree/bindings/iio/imu/invensense,mpu6050.yaml`, 중간 compatible 목록 일부와 `examples`의 mount-matrix 주석은 줄였다).

```text
# SPDX-License-Identifier: (GPL-2.0 OR BSD-2-Clause)
%YAML 1.2
---
$id: http://devicetree.org/schemas/iio/imu/invensense,mpu6050.yaml#
$schema: http://devicetree.org/meta-schemas/core.yaml#

title: InvenSense MPU-6050 Six-Axis (Gyro + Accelerometer) MEMS MotionTracking Device

maintainers:
  - Jean-Baptiste Maneyrol <jean-baptiste.maneyrol@tdk.com>

description: |
  These devices support both I2C and SPI bus interfaces.

properties:
  compatible:
    oneOf:
      - enum:
          - invensense,iam20380
          - invensense,mpu6050
          - invensense,mpu9250
          ...                       # (15개 중 일부만 표시)
      - items:
          - const: invensense,icm20600
          - const: invensense,icm20602
      - items:
          - const: invensense,icm20608d
          - const: invensense,icm20608

  reg:
    maxItems: 1

  interrupts:
    maxItems: 1

  vdd-supply: true
  vddio-supply: true

  mount-matrix: true

  invensense,level-shifter:
    type: boolean
    description: |
      From ancient platform data struct: false: VLogic, true: VDD

  i2c-gate:
    $ref: /schemas/i2c/i2c-controller.yaml
    unevaluatedProperties: false

allOf:
  - $ref: /schemas/spi/spi-peripheral-props.yaml#
  - if:
      properties:
        compatible:
          contains:
            enum:
              - invensense,iam20680
              - invensense,icm20602
    then:
      properties:
        i2c-gate: false

unevaluatedProperties: false

required:
  - compatible
  - reg
  - interrupts

examples:
  - |
    #include <dt-bindings/interrupt-controller/irq.h>
    i2c {
        #address-cells = <1>;
        #size-cells = <0>;

        imu@68 {
            compatible = "invensense,mpu9250";
            reg = <0x68>;
            interrupt-parent = <&gpio3>;
            interrupts = <21 IRQ_TYPE_LEVEL_HIGH>;
            ...
        };
    };
```

```svg
<svg viewBox="0 0 680 430" xmlns="http://www.w3.org/2000/svg">
  <defs><marker id="ah2" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto-start-reverse"><path d="M0,0 L10,5 L0,10 z" fill="currentColor"/></marker></defs>
  <rect x="20" y="14" width="250" height="44" rx="4" fill="#888" fill-opacity="0.12" stroke="#888"/>
  <text x="145" y="34" text-anchor="middle" font-size="13">$id · $schema</text>
  <text x="145" y="51" text-anchor="middle" font-size="12">파일 경로 = URL, 메타스키마</text>
  <rect x="20" y="66" width="250" height="44" rx="4" fill="#888" fill-opacity="0.12" stroke="#888"/>
  <text x="145" y="86" text-anchor="middle" font-size="12">title · maintainers · description</text>
  <text x="145" y="103" text-anchor="middle" font-size="12">무슨 HW인가, 누가 책임지나</text>
  <rect x="20" y="118" width="250" height="72" rx="4" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
  <text x="145" y="140" text-anchor="middle" font-size="13">properties:</text>
  <text x="145" y="160" text-anchor="middle" font-size="12">compatible · reg · interrupts</text>
  <text x="145" y="179" text-anchor="middle" font-size="12">*-supply · vendor,prop</text>
  <rect x="20" y="198" width="250" height="44" rx="4" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
  <text x="145" y="218" text-anchor="middle" font-size="13">required:</text>
  <text x="145" y="235" text-anchor="middle" font-size="12">없으면 에러인 속성 목록</text>
  <rect x="20" y="250" width="250" height="44" rx="4" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
  <text x="145" y="270" text-anchor="middle" font-size="13">allOf: $ref · if/then</text>
  <text x="145" y="287" text-anchor="middle" font-size="12">공통 스키마 상속, 조건부 제약</text>
  <rect x="20" y="302" width="250" height="44" rx="4" fill="#d0564a" fill-opacity="0.12" stroke="#d0564a"/>
  <text x="145" y="322" text-anchor="middle" font-size="13">unevaluatedProperties: false</text>
  <text x="145" y="339" text-anchor="middle" font-size="12">목록에 없는 속성은 금지</text>
  <rect x="20" y="354" width="250" height="60" rx="4" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
  <text x="145" y="376" text-anchor="middle" font-size="13">examples:</text>
  <text x="145" y="396" text-anchor="middle" font-size="12">DTS 조각 — 빌드·검증된다</text>
  <line x1="272" y1="36" x2="330" y2="36" stroke="currentColor" marker-end="url(#ah2)"/>
  <text x="338" y="40" font-size="12">$id 경로가 파일 경로와 다르면 에러</text>
  <line x1="272" y1="88" x2="330" y2="88" stroke="currentColor" marker-end="url(#ah2)"/>
  <text x="338" y="92" font-size="12">title에 "binding", "schema" 쓰면 에러</text>
  <line x1="272" y1="154" x2="330" y2="154" stroke="currentColor" marker-end="url(#ah2)"/>
  <text x="338" y="150" font-size="12">공통 속성은 개수 제약만 (maxItems: 1)</text>
  <text x="338" y="168" font-size="12">벤더 속성은 타입과 description 필수</text>
  <line x1="272" y1="220" x2="330" y2="220" stroke="currentColor" marker-end="url(#ah2)"/>
  <text x="338" y="224" font-size="12">properties와 같은 순서로 적는다</text>
  <line x1="272" y1="272" x2="330" y2="272" stroke="currentColor" marker-end="url(#ah2)"/>
  <text x="338" y="268" font-size="12">spi-peripheral-props: spi-max-frequency 등</text>
  <text x="338" y="286" font-size="12">if compatible 이면 then 속성 금지/제한</text>
  <line x1="272" y1="324" x2="330" y2="324" stroke="currentColor" marker-end="url(#ah2)"/>
  <text x="338" y="320" font-size="12">$ref로 받은 속성까지 "평가됨"으로 친다</text>
  <text x="338" y="338" font-size="12">$ref 없으면 additionalProperties: false</text>
  <line x1="272" y1="384" x2="330" y2="384" stroke="currentColor" marker-end="url(#ah2)"/>
  <text x="338" y="380" font-size="12">dt-extract-example → dtc → dt-validate</text>
  <text x="338" y="398" font-size="12">예제가 틀리면 binding 패치가 반려된다</text>
</svg>
```
그림 3 — YAML binding의 해부도. 회색은 메타데이터, 파랑은 "무엇이 허용되나", 초록은 "어디서 상속하나", 빨강은 "그 밖은 금지", 주황은 "실행 가능한 예제"다.

### 2.2 블록별 설명

| 블록 | 하는 일 | 규칙·함정 |
|---|---|---|
| `# SPDX-License-Identifier` | 라이선스 | 새 binding은 `(GPL-2.0-only OR BSD-2-Clause)` 이중 라이선스 권장 (다른 OS도 쓰도록) |
| `$id` | 스키마의 고유 URL. `http://devicetree.org/schemas/` + bindings 기준 상대 경로 | 파일 경로와 다르면 `dt-doc-validate`가 에러 |
| `$schema` | 이 파일을 검사할 **메타스키마** (스키마의 스키마) | 거의 항상 `http://devicetree.org/meta-schemas/core.yaml#` |
| `title` | 한 줄 HW 설명 | "binding", "schema"라는 단어 금지 (4.2절에서 실제로 걸린다) |
| `maintainers` | 이 binding을 책임지는 사람 | 메일링 리스트가 아니라 사람 |
| `description` | 자세한 설명 | 블록 스칼라 기호(파이프)는 줄바꿈 보존이 필요할 때만 |
| `properties` | 허용되는 속성 각각의 제약 | 공통 속성(`reg`, `interrupts`, `*-supply`)은 타입이 이미 core 스키마에 있어 **개수만** 쓴다 |
| `required` | 필수 속성 목록 | 새 필수 속성 추가 = ABI 깨짐 (1.3절) |
| `allOf` | 다른 스키마 `$ref`, `if`/`then` 조건부 제약 | 버스 공통 속성(SPI 등)은 여기서 상속 |
| `unevaluatedProperties` / `additionalProperties` | 목록 밖 속성 금지 | 둘 중 하나는 반드시 있어야 한다 |
| `examples` | 실제 DTS 조각 (여러 개 가능) | 4칸 들여쓰기, 일반 노드 이름(`imu@68`), 실제로 빌드됨 |

### 2.3 `compatible`: enum, items, fallback

`compatible`은 binding의 열쇠다. 세 가지 패턴이 있다.

- **enum** — "이 중 하나만 단독으로": `enum: [invensense,mpu6050, invensense,mpu9250]`.
- **items** — "정확히 이 순서의 목록": `items: [const: invensense,icm20608d, const: invensense,icm20608]`. 앞이 구체적인 칩, 뒤가 **fallback**이다.
- **oneOf** — 위 패턴 여러 개 중 하나.

fallback의 뜻(D3 복습): ICM-20608-D는 ICM-20608에 기능이 조금 더해진 칩이다. DTS에 `compatible = "invensense,icm20608d", "invensense,icm20608";`라고 쓰면, 20608d를 아는 새 드라이버는 첫 문자열로 매칭하고, 모르는 옛 드라이버는 두 번째 문자열로 매칭해 기본 기능으로 동작한다. binding의 `items`는 이 순서를 강제한다 — 순서를 뒤집으면 5.4절처럼 검증 에러다.

리뷰 규칙(`writing-bindings.rst`) 요약: compatible은 **구체적으로**(와일드카드·패밀리 이름 금지, `vendor,soc1234-i2c`처럼 SoC 이름 사용, `vendor,i2c-v2` 같은 자체 버전 번호 금지), 버스 접미사(`-i2c`, `-spi`) 금지(부모 버스 노드가 이미 말해 준다), 새 기능이나 버그가 있으면 새 compatible.

### 2.4 `$ref`와 공통 스키마 — 상속

`reg`, `interrupts`, `vdd-supply`에 타입을 안 써도 되는 이유는 dt-schema 패키지 안의 **core 스키마**(`dt-core.yaml`, `interrupts.yaml`, `property-units.yaml`, `iio/iio.yaml` …)가 이미 정의해 두었기 때문이다. 예를 들어 `mount-matrix: true`는 "있어도 된다"는 뜻만이고, 타입은 dt-schema의 `iio/iio.yaml`이 정한다.

```sh
limactl shell elinux -- bash -lc 'D=/var/tmp/D7/venv/lib/python3.14/site-packages/dtschema/schemas
  grep -n -A6 "mount-matrix:" $D/iio/iio.yaml'
```

```text
43:  mount-matrix:
44-    $ref: /schemas/types.yaml#/definitions/non-unique-string-array
45-    minItems: 9
46-    maxItems: 9
47-    description:
48-      3x3 matrix specifying the sensor orientation wrt to a reference plane.
```

출력에서 볼 것: `mount-matrix`는 **문자열 9개**짜리 배열이다(`"0", "-1", "0", …`). 숫자 셀 `<0 1 0 …>`로 쓰면 5.2절처럼 타입 에러가 난다. 마찬가지로 속성 이름이 `-microvolt`, `-us`, `-hz` 같은 **단위 접미사**로 끝나면 `property-units.yaml`이 자동으로 uint32 타입을 붙여 준다 — 새 속성을 만들 때 단위 접미사를 쓰라는 리뷰 코멘트가 나오는 이유다.

버스 쪽 공통 속성은 `allOf: - $ref: /schemas/spi/spi-peripheral-props.yaml#`로 상속한다. 이 파일이 `spi-max-frequency`, `spi-cs-high`, `spi-cpha` 등을 정의하므로, SPI로도 붙는 장치는 이 `$ref`가 있어야 DTS에 `spi-max-frequency`를 쓸 수 있다.

### 2.5 `additionalProperties` vs `unevaluatedProperties`

둘 다 "목록에 없는 속성 금지"인데 차이가 있다.

- `additionalProperties: false` — **이 파일의 `properties`/`patternProperties`에 적힌 것만** 허용. `allOf`의 `$ref`로 들어온 속성은 모른다.
- `unevaluatedProperties: false` — `$ref`, `allOf`, `if/then`까지 포함해 **어디서든 한 번이라도 평가된 속성**은 허용. (json-schema 2019-09에 생긴 키워드다.)

규칙: **다른 스키마를 `$ref`로 포함하면 `unevaluatedProperties: false`, 아니면 `additionalProperties: false`**. 이걸 틀리면 어떻게 되는지 4.3절에서 실제로 본다.

---

## 3. 도구 설치와 첫 `dt_binding_check`

### 3.1 dtschema 설치 (venv)

`dt-schema`는 pip 패키지 `dtschema`로 배포된다. 실행 파일 `dt-doc-validate`(binding 파일 검사), `dt-mk-schema`(모든 binding을 하나로 합침), `dt-validate`(DTB 검사), `dt-extract-example`(예제 추출)이 같이 설치된다. yamllint가 있으면 커널 Makefile이 같이 돌린다.

```sh
limactl shell elinux -- bash -lc 'mkdir -p /var/tmp/D7 && cd /var/tmp/D7 && python3 -m venv venv
  ./venv/bin/pip install -q dtschema yamllint
  ./venv/bin/pip list 2>/dev/null | grep -iE "dtschema|yamllint|jsonschema|ruamel|pylibfdt"; dtc --version'
```

```text
dtschema                  2026.9
jsonschema                4.26.0
jsonschema-specifications 2025.9.1
pylibfdt                  1.7.2.post2
ruamel.yaml               0.19.1
yamllint                  1.38.0
Version: DTC 1.7.2
```

출력에서 볼 것: `pylibfdt`(libfdt의 Python 바인딩)가 같이 들어온다 — `dt-validate`가 DTB 바이너리를 직접 파싱하는 데 쓴다. 커널 Makefile은 `dtschema` 최소 버전(6.18에서는 `DT_SCHEMA_MIN_VERSION = 2023.9`)을 확인한다. 이후 모든 명령은 `export PATH=/var/tmp/D7/venv/bin:$PATH` 후에 실행했다.

### 3.2 예제 3 — 읽기 전용 소스에서 `O=`로 `dt_binding_check`

무엇을 확인하나: 랩 소스를 건드리지 않고(`O=` 별도 출력 디렉터리) 실제 binding 하나를 검사한다. Kbuild의 out-of-tree 빌드는 소스 트리에 아무것도 쓰지 않는다(B4·E2) — 단 소스 트리가 깨끗해야(`.config` 없음) 한다.

```sh
limactl shell elinux -- bash -lc 'export PATH=/var/tmp/D7/venv/bin:$PATH; cd /var/tmp/D7
  time make -C /var/lab/src/linux-6.18.55 O=/var/tmp/D7/build ARCH=arm64 -j2 \
       dt_binding_check DT_SCHEMA_FILES=iio/imu/invensense,mpu6050.yaml'
```

```text
make: Entering directory '/var/lab/src/linux-6.18.55'
make[1]: Entering directory '/var/tmp/D7/build'
  HOSTCC  scripts/basic/fixdep
  HOSTCC  scripts/dtc/dtc.o
  HOSTCC  scripts/dtc/flattree.o
  SCHEMA  Documentation/devicetree/bindings/processed-schema.json
  HOSTCC  scripts/dtc/fstree.o
  ...
  HOSTLD  scripts/dtc/dtc
  CHKDT   /var/lab/src/linux-6.18.55/Documentation/devicetree/bindings
  LINT    /var/lab/src/linux-6.18.55/Documentation/devicetree/bindings
  DTEX    Documentation/devicetree/bindings/iio/imu/invensense,mpu6050.example.dts
  DTC [C] Documentation/devicetree/bindings/iio/imu/invensense,mpu6050.example.dtb
make[1]: Leaving directory '/var/tmp/D7/build'
make: Leaving directory '/var/lab/src/linux-6.18.55'

real	1m0.338s
user	0m51.572s
sys	0m0.821s
```

출력에서 볼 것(중간 HOSTCC 줄 일부 생략):

- `SCHEMA` — `dt-mk-schema`가 **트리의 모든 YAML**을 하나의 `processed-schema.json`으로 합친다(이 실행에서 24.9 MB). 첫 실행 1분의 대부분이 이 단계다.
- `CHKDT` — `dt-doc-validate`가 binding 파일 자체를 메타스키마로 검사한다(`DT_SCHEMA_FILES`로 고른 파일만).
- `LINT` — yamllint (커널의 `Documentation/devicetree/bindings/.yamllint` 설정).
- `DTEX` — `examples:`를 꺼내 `.example.dts`로 만든다.
- `DTC [C]` — dtc로 컴파일하고 `[C]`(check) 표시대로 `dt-validate`까지 돌린다.
- 에러가 0개면 **아무 말도 없다.** 조용함이 통과다.
- `DT_SCHEMA_FILES`는 부분 문자열 매칭이고 `:`로 여러 개를 줄 수 있다(`DT_SCHEMA_FILES=/gpio/`처럼 디렉터리 단위도 가능). 생략하면 5000개 전부를 검사한다.

### 3.3 검증 파이프라인 한눈에

```svg
<svg viewBox="0 0 680 350" xmlns="http://www.w3.org/2000/svg">
  <defs><marker id="ah3" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto-start-reverse"><path d="M0,0 L10,5 L0,10 z" fill="currentColor"/></marker></defs>
  <text x="20" y="22" font-size="13">① binding 자체 검사 (dt_binding_check)</text>
  <rect x="20" y="32" width="120" height="44" rx="4" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
  <text x="80" y="59" text-anchor="middle" font-size="12">vendor,dev.yaml</text>
  <rect x="170" y="32" width="140" height="44" rx="4" fill="#888" fill-opacity="0.12" stroke="#888"/>
  <text x="240" y="51" text-anchor="middle" font-size="12">yamllint</text>
  <text x="240" y="68" text-anchor="middle" font-size="12">dt-doc-validate</text>
  <rect x="340" y="32" width="140" height="44" rx="4" fill="#888" fill-opacity="0.12" stroke="#888"/>
  <text x="410" y="59" text-anchor="middle" font-size="12">dt-mk-schema</text>
  <rect x="510" y="32" width="150" height="44" rx="4" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
  <text x="585" y="51" text-anchor="middle" font-size="12">processed-</text>
  <text x="585" y="68" text-anchor="middle" font-size="12">schema.json</text>
  <line x1="140" y1="54" x2="166" y2="54" stroke="currentColor" marker-end="url(#ah3)"/>
  <line x1="310" y1="54" x2="336" y2="54" stroke="currentColor" marker-end="url(#ah3)"/>
  <line x1="480" y1="54" x2="506" y2="54" stroke="currentColor" marker-end="url(#ah3)"/>
  <text x="240" y="94" text-anchor="middle" font-size="12">메타스키마로 문법 검사</text>
  <text x="410" y="94" text-anchor="middle" font-size="12">5000여 개를 합치고 고침</text>
  <text x="20" y="128" font-size="13">② binding의 examples 검사</text>
  <rect x="20" y="138" width="120" height="44" rx="4" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
  <text x="80" y="165" text-anchor="middle" font-size="12">examples:</text>
  <rect x="170" y="138" width="140" height="44" rx="4" fill="#888" fill-opacity="0.12" stroke="#888"/>
  <text x="240" y="165" text-anchor="middle" font-size="12">dt-extract-example</text>
  <rect x="340" y="138" width="140" height="44" rx="4" fill="#888" fill-opacity="0.12" stroke="#888"/>
  <text x="410" y="157" text-anchor="middle" font-size="12">dtc (cpp 포함)</text>
  <text x="410" y="174" text-anchor="middle" font-size="12">.example.dtb</text>
  <line x1="140" y1="160" x2="166" y2="160" stroke="currentColor" marker-end="url(#ah3)"/>
  <line x1="310" y1="160" x2="336" y2="160" stroke="currentColor" marker-end="url(#ah3)"/>
  <text x="20" y="226" font-size="13">③ 보드 DTB 검사 (dtbs_check, CHECK_DTBS=y)</text>
  <rect x="20" y="236" width="120" height="44" rx="4" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
  <text x="80" y="263" text-anchor="middle" font-size="12">board.dts</text>
  <rect x="170" y="236" width="140" height="44" rx="4" fill="#888" fill-opacity="0.12" stroke="#888"/>
  <text x="240" y="263" text-anchor="middle" font-size="12">cpp + dtc</text>
  <rect x="340" y="236" width="140" height="44" rx="4" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
  <text x="410" y="263" text-anchor="middle" font-size="12">board.dtb</text>
  <line x1="140" y1="258" x2="166" y2="258" stroke="currentColor" marker-end="url(#ah3)"/>
  <line x1="310" y1="258" x2="336" y2="258" stroke="currentColor" marker-end="url(#ah3)"/>
  <rect x="510" y="186" width="150" height="54" rx="4" fill="#d0564a" fill-opacity="0.12" stroke="#d0564a"/>
  <text x="585" y="209" text-anchor="middle" font-size="13">dt-validate</text>
  <text x="585" y="228" text-anchor="middle" font-size="12">-p/-s schema.json</text>
  <line x1="585" y1="78" x2="585" y2="182" stroke="currentColor" marker-end="url(#ah3)"/>
  <line x1="480" y1="160" x2="506" y2="200" stroke="currentColor" marker-end="url(#ah3)"/>
  <line x1="480" y1="258" x2="506" y2="226" stroke="currentColor" marker-end="url(#ah3)"/>
  <rect x="510" y="268" width="150" height="44" rx="4" fill="#d0564a" fill-opacity="0.12" stroke="#d0564a"/>
  <text x="585" y="295" text-anchor="middle" font-size="12">경고 목록 (stderr)</text>
  <line x1="585" y1="242" x2="585" y2="264" stroke="currentColor" marker-end="url(#ah3)"/>
  <text x="340" y="336" text-anchor="middle" font-size="12">DTB에는 타입 정보가 없다 — dt-validate가 스키마의 타입으로 바이트를 해석한다</text>
</svg>
```
그림 4 — 세 갈래의 검증. ① binding 파일은 메타스키마로 검사된 뒤 하나의 `processed-schema.json`으로 합쳐진다. ② binding의 예제와 ③ 실제 보드 DTB는 같은 `dt-validate`로 그 합본 스키마에 대해 검사된다.

맨 아래 문장이 중요하다. DTB는 "이름 + 바이트열"만 저장한다(D5). `<0x68>`이 정수인지 문자열인지 DTB만 봐서는 모른다. `dt-validate`는 **스키마에서 속성 타입을 찾아** 바이트를 디코드하고, 스키마에 없는 속성은 디코드하지 못해 `b'\x00\x00\x00 '` 같은 날 바이트로 보여 준다. 5.2절과 6.2절의 이상한 에러 메시지가 다 이것 때문이다.

---

## 4. 내 binding 쓰기 — `don,d7-imu.yaml`

### 4.1 목표 HW와 작업 방식

센서 키트에 가상의 IMU "D7"을 붙인다고 하자. 데이터시트(가상)가 말하는 것:

- 3축 가속도 + 3축 자이로, I2C(주소 0x68) 또는 SPI.
- 데이터 준비 인터럽트 핀 1개, active-low 리셋 핀 1개.
- 전원 VDD(코어), VDDIO(디지털 I/O).
- 보드에 회전되어 장착될 수 있으므로 mount-matrix.
- 기능이 줄어든 "lite" 변종이 있고, 레지스터는 완전히 호환된다.

랩 소스는 읽기 전용이므로 트리를 통째로 복사해서(`cp -a /var/lab/src/linux-6.18.55 /var/tmp/D7/linux`, 1.7 GB, 5초) 복사본에 파일을 넣고 `O=/var/tmp/D7/build2`로 검사했다. binding 원본은 Mac 쪽 `~/.elinux_scratch/D7/`에 쓰고 매번 복사본 트리의 `Documentation/devicetree/bindings/iio/imu/don,d7-imu.yaml`로 복사했다.

### 4.2 예제 4 — 첫 버전(v1)과 첫 에러: title

첫 버전은 "생각나는 대로" 썼다. 바뀌는 부분만 보이면:

```text
title: Don D7 6-axis IMU binding
...
properties:
  compatible:
    enum:
      - don,d7-imu
      - don,d7-imu-lite
  ...
  don,odr-hz:
    description: Output data rate the driver should use.
    enum: [100, 200, 400, 800]
...
allOf:
  - $ref: /schemas/spi/spi-peripheral-props.yaml#

additionalProperties: false

examples:
  - |
    ...
        d7-imu@68 {
            compatible = "don,d7-imu";
```

```sh
limactl shell elinux -- bash -lc 'export PATH=/var/tmp/D7/venv/bin:$PATH; cd /var/tmp/D7
  cp /Users/donh/.elinux_scratch/D7/don,d7-imu.v1.yaml linux/Documentation/devicetree/bindings/iio/imu/don,d7-imu.yaml
  make -C linux O=/var/tmp/D7/build2 ARCH=arm64 -j2 dt_binding_check DT_SCHEMA_FILES=iio/imu/don,d7-imu.yaml 2>&1 |
    grep -v -E "HOSTCC|HOSTLD|LEX|YACC|Entering|Leaving"'
```

```text
  SCHEMA  Documentation/devicetree/bindings/processed-schema.json
  CHKDT   /var/tmp/D7/linux/Documentation/devicetree/bindings
  LINT    /var/tmp/D7/linux/Documentation/devicetree/bindings
/var/tmp/D7/linux/Documentation/devicetree/bindings/iio/imu/don,d7-imu.yaml: title: 'Don D7 6-axis IMU binding' should not be valid under {'pattern': '([Bb]inding| [Ss]chema)'}
	hint: Everything is a binding/schema, no need to say it. Describe what hardware the binding is for.
	from schema $id: http://devicetree.org/meta-schemas/base.yaml
  DTEX    Documentation/devicetree/bindings/iio/imu/don,d7-imu.example.dts
  DTC [C] Documentation/devicetree/bindings/iio/imu/don,d7-imu.example.dtb
```

출력에서 볼 것: 에러는 **메타스키마**(`meta-schemas/base.yaml`)에서 왔다. 메타스키마는 "binding 파일이 지켜야 할 규칙"을 json-schema로 적어 둔 것이라서, 리뷰어가 늘 하던 지적("title에 binding이라고 쓰지 마")이 자동 검사가 됐다. 고침: `title: Don D7 6-axis accelerometer and gyroscope`. 다시 돌리면(v2) 출력이 조용해진다.

### 4.3 예제 5 — SPI 예제를 넣자 드러난 `additionalProperties` 함정

v2는 통과했지만 예제가 I2C뿐이었다. SPI 연결 예제를 하나 더 넣었다(v3).

```text
  - |
    spi {
        #address-cells = <1>;
        #size-cells = <0>;

        imu@0 {
            compatible = "don,d7-imu";
            reg = <0>;
            spi-max-frequency = <10000000>;
            interrupt-parent = <&gpio2>;
            interrupts = <5 1>;
        };
    };
```

```text
  SCHEMA  Documentation/devicetree/bindings/processed-schema.json
  CHKDT   /var/tmp/D7/linux/Documentation/devicetree/bindings
  LINT    /var/tmp/D7/linux/Documentation/devicetree/bindings
  DTEX    Documentation/devicetree/bindings/iio/imu/don,d7-imu.example.dts
  DTC [C] Documentation/devicetree/bindings/iio/imu/don,d7-imu.example.dtb
Documentation/devicetree/bindings/iio/imu/don,d7-imu.example.dtb: imu@0 (don,d7-imu): 'spi-max-frequency' does not match any of the regexes: '^pinctrl-[0-9]+$'
	from schema $id: http://devicetree.org/schemas/iio/imu/don,d7-imu.yaml
```

출력에서 볼 것: `allOf`로 `spi-peripheral-props.yaml`을 상속했는데도 `spi-max-frequency`가 거부됐다. 2.5절의 함정 그대로다 — `additionalProperties: false`는 이 파일의 `properties`만 보고, `$ref`로 들어온 속성은 "추가 속성"으로 본다. 허용되는 정규식으로 `^pinctrl-[0-9]+$`만 나오는 것은 도구가 모든 노드에 pinctrl 속성을 자동으로 허용해 두었기 때문이다(4.6절). 고침: `unevaluatedProperties: false`. 이 버그는 **예제가 없었다면 아무도 몰랐을 것**이다 — 예제를 "SPI·I2C 둘 다" 넣으라는 이유다.

### 4.4 리뷰어 관점의 수정 — 도구가 못 잡는 것

도구는 조용해졌지만 upstream 리뷰어라면 v2에 다음 지적을 할 것이다(전부 `writing-bindings.rst`의 규칙).

| v1·v2의 문제 | 규칙 | 고침 |
|---|---|---|
| `don,odr-hz` (드라이버가 쓸 ODR) | DT는 **HW를 기술**하지 정책·설정을 담지 않는다. ODR은 유저가 sysfs(IIO `sampling_frequency`)로 고를 일 | 속성 삭제 |
| 노드 이름 `d7-imu@68` | 노드 이름은 장치 **종류**를 나타내는 일반 이름 | `imu@68` |
| `compatible: enum: [d7-imu, d7-imu-lite]` | lite가 상위 호환이면 fallback 사용 | `items: [d7-imu-lite, d7-imu]` |
| 벤더 접두어 `don`이 미등록 | 모든 벤더 접두어는 `vendor-prefixes.yaml`에 등록 | 별도 패치로 접두어 추가 |
| description이 "for the D7 study note" | HW 설명만, OS·드라이버·용도 언급 금지 | 핀·전원 레일 설명으로 교체 |

### 4.5 예제 6 — 최종본과 vendor prefix 등록

최종 binding(84줄):

```text
# SPDX-License-Identifier: (GPL-2.0-only OR BSD-2-Clause)
%YAML 1.2
---
$id: http://devicetree.org/schemas/iio/imu/don,d7-imu.yaml#
$schema: http://devicetree.org/meta-schemas/core.yaml#

title: Don D7 6-axis accelerometer and gyroscope

maintainers:
  - Don H <don@example.com>

description:
  6-axis IMU (3-axis accelerometer + 3-axis gyroscope) with an I2C and
  an SPI interface, one data-ready interrupt output and an active-low
  reset input. VDD powers the sensor core, VDDIO the digital I/O.

properties:
  compatible:
    oneOf:
      - const: don,d7-imu
      - items:
          - const: don,d7-imu-lite
          - const: don,d7-imu

  reg:
    maxItems: 1

  interrupts:
    maxItems: 1

  vdd-supply: true
  vddio-supply: true

  reset-gpios:
    maxItems: 1

  mount-matrix: true

required:
  - compatible
  - reg
  - interrupts

allOf:
  - $ref: /schemas/spi/spi-peripheral-props.yaml#

unevaluatedProperties: false

examples:
  - |
    #include <dt-bindings/gpio/gpio.h>
    #include <dt-bindings/interrupt-controller/irq.h>
    i2c {
        #address-cells = <1>;
        #size-cells = <0>;

        imu@68 {
            compatible = "don,d7-imu";
            reg = <0x68>;
            interrupt-parent = <&gpio1>;
            interrupts = <12 IRQ_TYPE_EDGE_RISING>;
            vdd-supply = <&reg_3v3>;
            vddio-supply = <&reg_1v8>;
            reset-gpios = <&gpio1 13 GPIO_ACTIVE_LOW>;
            mount-matrix = "0", "-1", "0",
                           "1", "0", "0",
                           "0", "0", "1";
        };
    };
  - |
    #include <dt-bindings/interrupt-controller/irq.h>
    spi {
        #address-cells = <1>;
        #size-cells = <0>;

        imu@0 {
            compatible = "don,d7-imu-lite", "don,d7-imu";
            reg = <0>;
            spi-max-frequency = <10000000>;
            interrupt-parent = <&gpio2>;
            interrupts = <5 IRQ_TYPE_EDGE_RISING>;
        };
    };
...
```

복사본 트리의 `vendor-prefixes.yaml`에도 알파벳 순서 자리(`domintech` 다음)에 접두어를 넣었다(Python 스크립트로 두 줄 삽입).

```text
  "^domintech,.*":
    description: Domintech Co., Ltd.
  "^don,.*":
    description: Don D7 study lab (example only)
  "^dongwoon,.*":
    description: Dongwoon Anatech
```

```sh
limactl shell elinux -- bash -lc 'export PATH=/var/tmp/D7/venv/bin:$PATH; cd /var/tmp/D7
  make -C linux O=/var/tmp/D7/build2 ARCH=arm64 -j2 dt_binding_check \
       DT_SCHEMA_FILES=iio/imu/don,d7-imu.yaml:vendor-prefixes.yaml 2>&1 |
    grep -v -E "HOSTCC|HOSTLD|LEX|YACC|Entering|Leaving"; echo "exit=${PIPESTATUS[0]}"'
```

```text
  SCHEMA  Documentation/devicetree/bindings/processed-schema.json
  CHKDT   /var/tmp/D7/linux/Documentation/devicetree/bindings
  LINT    /var/tmp/D7/linux/Documentation/devicetree/bindings
  DTEX    Documentation/devicetree/bindings/vendor-prefixes.example.dts
  DTEX    Documentation/devicetree/bindings/iio/imu/don,d7-imu.example.dts
  DTC [C] Documentation/devicetree/bindings/vendor-prefixes.example.dtb
  DTC [C] Documentation/devicetree/bindings/iio/imu/don,d7-imu.example.dtb
exit=0
```

출력에서 볼 것: `:`로 두 파일을 같이 검사했고 둘 다 조용하다. 주의: `dt_binding_check`는 경고가 있어도 **exit 0**이다(Makefile이 `|| true`로 감싼다). CI에서 쓰려면 출력에 경고 줄이 있는지 grep해야 한다.

추출된 예제 파일을 보면 도구가 예제를 어떻게 감싸는지 알 수 있다.

```sh
limactl shell elinux -- bash -lc 'head -25 /var/tmp/D7/build2/Documentation/devicetree/bindings/iio/imu/don,d7-imu.example.dts'
```

```text

/dts-v1/;
/plugin/; // silence any missing phandle references

/{
    compatible = "foo";
    model = "foo";
    #address-cells = <1>;
    #size-cells = <1>;


    example-0 {
        #address-cells = <1>;
        #size-cells = <1>;

        
        interrupt-parent = <&fake_intc0>;
        fake_intc0: fake-interrupt-controller {
            interrupt-controller;
            #interrupt-cells = < 2 >;
        };


        #include <dt-bindings/gpio/gpio.h>
        #include <dt-bindings/interrupt-controller/irq.h>
```

출력에서 볼 것: 예제는 `/plugin/`(overlay 문법, D6)으로 감싸져서 `&gpio1`, `&reg_3v3` 같은 정의되지 않은 phandle도 에러 없이 컴파일된다. 가짜 인터럽트 컨트롤러(`#interrupt-cells = <2>`)가 주어지므로, 예제의 `interrupts`는 **2셀**로 써야 한다.

### 4.6 예제 7 — `processed-schema.json` 안에서 내 binding은 어떻게 바뀌었나

무엇을 확인하나: `dt-mk-schema`가 사람이 쓴 간결한 스키마를 검증용으로 어떻게 "고쳐 쓰는지"(fixup).

```python
# ~/.elinux_scratch/D7/d7_peek.py — processed-schema.json 안에서 내 바인딩이 어떻게 변형됐는지 본다
import json, sys
d = json.load(open(sys.argv[1]))
print("schemas in processed-schema.json:", len(d))
s = d["http://devicetree.org/schemas/iio/imu/don,d7-imu.yaml"]
print("keys:", sorted(s.keys()))
for p in ("compatible", "reg", "mount-matrix"):
    print(p, "=>", json.dumps(s["properties"][p]))
print("oneOf  =>", json.dumps(s["oneOf"]))
print("patternProperties =>", json.dumps(s["patternProperties"]))
print("allOf  =>", json.dumps(s["allOf"]))
```

```sh
limactl shell elinux -- python3 /Users/donh/.elinux_scratch/D7/d7_peek.py \
    /var/tmp/D7/build2/Documentation/devicetree/bindings/processed-schema.json
```

```text
schemas in processed-schema.json: 5253
keys: ['$filename', '$id', '$schema', 'allOf', 'oneOf', 'patternProperties', 'properties', 'required', 'title', 'type', 'unevaluatedProperties']
compatible => {"oneOf": [{"items": [{"const": "don,d7-imu"}], "maxItems": 1, "minItems": 1, "type": "array"}, {"items": [{"const": "don,d7-imu-lite"}, {"const": "don,d7-imu"}], "maxItems": 2, "minItems": 2, "type": "array"}]}
reg => {"maxItems": 1, "minItems": 1}
mount-matrix => true
oneOf  => [{"required": ["interrupts"]}, {"required": ["interrupts-extended"]}]
patternProperties => {"^pinctrl-[0-9]+$": true}
allOf  => [{"$ref": "/schemas/spi/spi-peripheral-props.yaml#"}]
```

출력에서 볼 것(`writing-schema.rst`가 설명하는 변환이 실제로 일어났다):

- 합본에는 스키마 5253개(트리의 YAML + dt-schema 패키지의 core 스키마)가 들어 있다.
- `const: don,d7-imu` 하나가 `{"type": "array", "minItems": 1, "maxItems": 1, ...}`로 바뀌었다. DT에서 문자열 속성은 항상 "문자열 배열"이기 때문이다.
- `items` 목록에는 `minItems`/`maxItems`가 목록 길이로 자동으로 붙었다 — json-schema 기본은 "가변 길이 허용"이지만 DT는 고정 길이를 원한다.
- `required: interrupts`에 `oneOf: interrupts | interrupts-extended`가 추가됐다. 둘 중 무엇으로 써도 된다(D4).
- `^pinctrl-[0-9]+$`가 자동으로 허용됐다 — 4.3절 에러 메시지에 그 정규식이 나온 이유다.

### 4.7 예제 8 — yamllint와 dt-doc-validate가 잡는 것

무엇을 확인하나: 들여쓰기 실수와 중복 키를 넣은 파일을 각 도구가 어떻게 보고하는지. (`reg:` 아래를 5칸, `vdd-supply: true`를 두 번.)

```sh
limactl shell elinux -- bash -lc 'export PATH=/var/tmp/D7/venv/bin:$PATH; cd /var/tmp/D7
  L=/var/tmp/D7/linux/Documentation/devicetree/bindings
  mkdir -p lint/iio/imu && cp /Users/donh/.elinux_scratch/D7/don,d7-imu.lintbad.yaml lint/iio/imu/don,d7-imu.yaml
  echo "### yamllint"; yamllint -f parsable -c $L/.yamllint lint/iio/imu/don,d7-imu.yaml
  echo "### dt-doc-validate"; dt-doc-validate -u $L lint/iio/imu/don,d7-imu.yaml'
```

```text
### yamllint
lint/iio/imu/don,d7-imu.yaml:26:6: [warning] wrong indentation: expected 4 but found 5 (indentation)
lint/iio/imu/don,d7-imu.yaml:32:3: [error] duplication of key "vdd-supply" in mapping (key-duplicates)
### dt-doc-validate
lint/iio/imu/don,d7-imu.yaml:32:3: found duplicate key "vdd-supply" with value "True" (original value: "True")
```

출력에서 볼 것: 들여쓰기는 yamllint만 잡는다(YAML로는 여전히 유효하므로). 커널 `.yamllint`는 2칸 들여쓰기·줄 길이 110자 경고 등을 정하고, `trailing-spaces`는 꺼 두었다. yamllint가 없으면 Makefile이 `warning: python package 'yamllint' not installed, skipping`을 출력하고 넘어가므로, binding 패치를 보내기 전에는 꼭 설치해 둔다.

---

## 5. DTS 검증 — 일부러 틀린 .dts와 `dt-validate`

### 5.1 틀린 보드 DTS

binding이 생겼으니 이제 "보드 BSP 엔지니어" 입장에서 이 IMU를 쓰는 DTS를 쓴다. 일부러 다섯 가지를 틀렸다.

```dts
/dts-v1/;
/ {
    #address-cells = <1>;
    #size-cells = <1>;
    compatible = "don,d7-board";
    model = "D7 sensor kit";

    osc: clock-24m {
        compatible = "fixed-clock";
        #clock-cells = <0>;
        clock-frequency = <24000000>;
    };

    gpio1: gpio {
        gpio-controller;
        #gpio-cells = <2>;
        interrupt-controller;
        #interrupt-cells = <2>;
    };

    i2c@30a20000 {
        compatible = "fsl,imx21-i2c";
        reg = <0x30a20000 0x10000>;
        interrupt-parent = <&gpio1>;
        interrupts = <1 4>;
        clocks = <&osc>;
        #address-cells = <1>;
        #size-cells = <0>;

        imu@68 {
            compatible = "don,d7-imu";
            reg = <0x68>;
            /* (1) interrupts 빠짐 */
            interrupt-parent = <&gpio1>;
            /* (2) 잘못된 타입: 문자열이어야 할 mount-matrix에 숫자 */
            mount-matrix = <0 1 0 1 0 0 0 0 1>;
            /* (3) enum 밖 값 */
            don,odr-hz = <500>;
            /* (4) 바인딩에 없는 속성 */
            don,fifo-watermark = <32>;
            /* (5) Linux 정책을 DT에 넣음 */
            linux,poll-ms = <10>;
        };
    };
};
```

### 5.2 예제 9 — `dt-validate`로 검사

무엇을 확인하나: 다섯 가지 실수가 각각 어떤 메시지로 나오는지. 이 검사는 `don,odr-hz`가 아직 있던 **v2 binding**(그리고 vendor prefix 등록 전)의 합본 스키마로 돌렸다 — enum 에러와 vendor prefix 에러를 같이 보기 위해서다.

```sh
limactl shell elinux -- bash -lc 'export PATH=/var/tmp/D7/venv/bin:$PATH; cd /var/tmp/D7
  dtc -I dts -O dtb -o d7_bad.dtb /Users/donh/.elinux_scratch/D7/d7_bad.dts
  dt-validate -m -s build2/Documentation/devicetree/bindings/processed-schema.json d7_bad.dtb'
```

```text
d7_bad.dtb: /: failed to match any schema with compatible: ['don,d7-board']
d7_bad.dtb: imu@68 (don,d7-imu): 'don,fifo-watermark', 'linux,poll-ms' do not match any of the regexes: '^pinctrl-[0-9]+$'
	from schema $id: http://devicetree.org/schemas/iio/imu/don,d7-imu.yaml
d7_bad.dtb: imu@68 (don,d7-imu): 'oneOf' conditional failed, one must be fixed:
	'interrupts' is a required property
	'interrupts-extended' is a required property
	from schema $id: http://devicetree.org/schemas/iio/imu/don,d7-imu.yaml
d7_bad.dtb: imu@68 (don,d7-imu): don,odr-hz:0:0: 500 is not one of [100, 200, 400, 800]
	from schema $id: http://devicetree.org/schemas/iio/imu/don,d7-imu.yaml
d7_bad.dtb: imu@68 (don,d7-imu): mount-matrix: b'\x00\x00\x00\x00\x00\x00\x00\x01\x00\x00\x00\x00\x00\x00\x00\x01\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x01' is not of type 'object', 'integer', 'array', 'boolean', 'null'
	from schema $id: http://devicetree.org/schemas/dt-core.yaml
d7_bad.dtb: imu@68 (don,d7-imu): don,fifo-watermark: b'\x00\x00\x00 ' is not of type 'object', 'integer', 'array', 'boolean', 'null'
	from schema $id: http://devicetree.org/schemas/dt-core.yaml
d7_bad.dtb: imu@68 (don,d7-imu): mount-matrix: b'\x00\x00\x00\x00\x00\x00\x00\x01\x00\x00\x00\x00\x00\x00\x00\x01\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x01' is not of type 'array'
	from schema $id: http://devicetree.org/schemas/iio/iio.yaml
d7_bad.dtb: imu@68 (don,d7-imu): 'don,fifo-watermark', 'don,odr-hz' do not match any of the regexes: '^#.*', '^(at25|bm|devbus|dmacap|dsa|exynos|fsi[ab]|gpio-fan|gpio-key|gpio|gpmc|hdmi|i2c-gpio),.*', '^(keypad|m25p|max8952|max8997|max8998|mpmc),.*', ... '^zte,.*', '^zyxel,.*'
	from schema $id: http://devicetree.org/schemas/vendor-prefixes.yaml
```

(마지막 줄은 등록된 벤더 접두어 정규식 900여 개를 전부 나열하므로 가운데를 `...`로 줄였다.)

출력에서 볼 것 — 메시지를 실수와 짝지으면:

| 실수 | 메시지 | 어느 스키마가 잡았나 |
|---|---|---|
| (1) interrupts 빠짐 | `'interrupts' is a required property` (interrupts-extended와 oneOf) | 내 binding의 `required` |
| (2) mount-matrix 숫자 | `b'\x00...\x01' is not of type 'array'` | dt-schema `iio/iio.yaml` (문자열 배열이어야 함) |
| (3) ODR 500 | `500 is not one of [100, 200, 400, 800]` | 내 binding의 `enum` |
| (4) 모르는 속성 | `'don,fifo-watermark' ... do not match` + 타입 모름 | 내 binding의 `unevaluatedProperties` + `dt-core.yaml` |
| (5) `linux,poll-ms` | 같은 "do not match" 목록에 포함 | 내 binding (`linux`는 등록된 접두어라 vendor-prefixes는 통과) |
| 보너스: `don,` 미등록 | `vendor-prefixes.yaml`의 긴 정규식 목록 | vendor-prefixes (아직 `don` 등록 전) |
| 보너스: 보드 compatible | `failed to match any schema with compatible` | `-m` 옵션 (문서화 안 된 compatible 보고) |

읽는 요령:

- 줄 머리 `imu@68 (don,d7-imu):`는 **노드 경로와 그 노드의 첫 compatible**이다. 큰 보드 DTB에서는 이걸로 노드를 찾는다.
- `from schema $id:`는 **어느 스키마의 규칙이 실패했는지**를 알려 준다. 내 binding인지, core 스키마인지, vendor-prefixes인지 보면 고칠 곳이 보인다.
- 날 바이트(`b'\x00\x00\x00 '`)가 보이면 **"이 속성의 타입을 아는 스키마가 없다"** 는 뜻이다(3.3절). `0x20`이 공백 문자로 찍혀서 `b'\x00\x00\x00 '`가 됐다 — 32다.
- `don,odr-hz:0:0:`의 `0:0`은 "첫 번째 그룹의 첫 번째 값" — DT 정수 속성은 행렬(그룹 × 셀)로 해석된다.

### 5.3 예제 10 — 고친 DTS, 그리고 fallback 순서 실수

무엇을 확인하나: (1)~(5)를 고치고 루트 compatible을 문서화된 보드(`fsl,imx8mp-evk`)로 바꾼 DTS가 최종 binding과 vendor prefix 등록 후 조용히 통과하는지, 그리고 compatible 순서를 뒤집으면 무엇이 나오는지.

IMU 노드만 보이면:

```dts
        imu@68 {
            compatible = "don,d7-imu";
            reg = <0x68>;
            interrupt-parent = <&gpio1>;
            interrupts = <12 1>;
            mount-matrix = "0", "-1", "0",
                           "1", "0", "0",
                           "0", "0", "1";
        };
```

```sh
limactl shell elinux -- bash -lc 'export PATH=/var/tmp/D7/venv/bin:$PATH; cd /var/tmp/D7
  S=build2/Documentation/devicetree/bindings/processed-schema.json
  dtc -I dts -O dtb -o d7_good.dtb /Users/donh/.elinux_scratch/D7/d7_good.dts
  dt-validate -m -s $S d7_good.dtb; echo "exit=$?"
  # compatible 순서만 뒤집은 버전: "don,d7-imu", "don,d7-imu-lite"
  dtc -I dts -O dtb -o d7_order.dtb /Users/donh/.elinux_scratch/D7/d7_order.dts
  dt-validate -m -s $S d7_order.dtb'
```

```text
exit=0
d7_order.dtb: imu@68 (don,d7-imu): compatible: 'oneOf' conditional failed, one must be fixed:
	['don,d7-imu', 'don,d7-imu-lite'] is too long
	'don,d7-imu-lite' was expected
	'don,d7-imu' was expected
	from schema $id: http://devicetree.org/schemas/iio/imu/don,d7-imu.yaml
```

출력에서 볼 것: 고친 DTB는 아무 출력 없이 통과한다. 순서를 뒤집은 쪽은 `oneOf`의 두 갈래 각각이 왜 실패했는지 보여 준다 — "단독 `don,d7-imu`라면 목록이 너무 길고", "lite+fallback이라면 첫 값은 `don,d7-imu-lite`여야 한다". 구체적인 것이 앞, 일반적인 것이 뒤다.

---

## 6. `dtbs_check` — 실제 보드 DTB 검사

### 6.1 예제 11 — i.MX8MP EVK 한 장, 그리고 86장

보드 DTB 하나만 검사하려면 `dtbs_check` 대신 **`CHECK_DTBS=y` + DTB 타깃**을 쓴다. `dtbs_check`는 설정된 모든 DTB를 빌드·검사해서 오래 걸린다. DTB 빌드에는 `.config`가 필요하므로 먼저 `defconfig`를 만든다(역시 `O=` 안에).

```sh
limactl shell elinux -- bash -lc 'export PATH=/var/tmp/D7/venv/bin:$PATH; cd /var/tmp/D7
  make -C /var/lab/src/linux-6.18.55 O=/var/tmp/D7/build ARCH=arm64 defconfig > /dev/null
  time make -C /var/lab/src/linux-6.18.55 O=/var/tmp/D7/build ARCH=arm64 -j2 CHECK_DTBS=y freescale/imx8mp-evk.dtb'
```

```text
make: Entering directory '/var/lab/src/linux-6.18.55'
make[1]: Entering directory '/var/tmp/D7/build'
  UPD     include/config/kernel.release
  HOSTCC  scripts/dtc/libfdt/fdt.o
  ...
  HOSTLD  scripts/dtc/fdtoverlay
  DTC [C] arch/arm64/boot/dts/freescale/imx8mp-evk.dtb
make[1]: Leaving directory '/var/tmp/D7/build'
make: Leaving directory '/var/lab/src/linux-6.18.55'

real	0m8.002s
user	0m7.961s
sys	0m0.709s
```

출력에서 볼 것: NXP 레퍼런스 보드 `imx8mp-evk`는 6.18.55에서 **경고 0개**다. SoC 벤더의 EVK는 binding과 함께 관리되므로 깨끗한 편이다. `DT_SCHEMA_FILES` 없이 돌리면 `DT_CHECKER_FLAGS` 기본값이 `-m`(문서화 안 된 compatible 보고)이 된다(`scripts/Makefile.dtbs`).

이번엔 서드파티 SoM·캐리어 보드까지 포함해 `freescale/imx8mp-*.dts`와 `ti/k3-am62*.dts` 전부(86개 DTB)를 검사했다.

```sh
limactl shell elinux -- bash -lc 'export PATH=/var/tmp/D7/venv/bin:$PATH
  cd /var/lab/src/linux-6.18.55/arch/arm64/boot/dts
  T=$(ls freescale/imx8mp-*.dts ti/k3-am62*.dts | sed "s/\.dts$/.dtb/" | tr "\n" " ")
  cd /var/tmp/D7
  echo $T | wc -w
  time make -C /var/lab/src/linux-6.18.55 O=/var/tmp/D7/build ARCH=arm64 -j2 CHECK_DTBS=y $T > d7_many_check.log 2>&1'
```

```text
87
real	8m30.933s
user	9m18.257s
sys	0m11.755s
```

집계는 같은 로그에서 따로 했다 (`[C]` 줄 수 = 실제로 검사된 DTB, 경고가 난 DTB 수, 경고 줄 수):

```sh
limactl shell elinux -- bash -lc 'cd /var/tmp/D7
  grep -c "DTC \[C\]" d7_many_check.log
  grep "^arch.*\.dtb: " d7_many_check.log | cut -d: -f1 | sort -u | wc -l
  grep "^arch.*\.dtb: " d7_many_check.log | wc -l'
```

```text
86
29
43
```

출력에서 볼 것: 타깃 87개 중 `imx8mp-evk.dtb`는 바로 앞에서 이미 검사돼 다시 빌드되지 않았으므로 새로 검사된 것은 86개다. 86개 중 **29개 DTB에서 43줄**의 경고(-j2로 8분 30초). 대표 줄들(각 범주에서 하나씩, 실제 로그에서 복사):

```text
arch/arm64/boot/dts/freescale/imx8mp-verdin-wifi-dev.dtb: ethernet-phy@7 (ethernet-phy-ieee802.3-c22): micrel,led-mode: b'\x00\x00\x00\x00' is not of type 'object', 'integer', 'array', 'boolean', 'null'
arch/arm64/boot/dts/freescale/imx8mp-skov-basic.dtb: switch@5f (microchip,ksz9893): pinctrl-names: ['default'] is too short
arch/arm64/boot/dts/freescale/imx8mp-kontron-bl-osm-s.dtb: rtc@52 (microcrystal,rv3028): Unevaluated properties are not allowed ('vdd-supply' was unexpected)
arch/arm64/boot/dts/freescale/imx8mp-tx8p-ml81-moduline-display-106.dtb: /: failed to match any schema with compatible: ['gocontroll,moduline-display-106', 'karo,tx8p-ml81', 'fsl,imx8mp']
arch/arm64/boot/dts/freescale/imx8mp-tx8p-ml81-moduline-display-106.dtb: / (gocontroll,moduline-display-106): hardware: b'Moduline Display V1.06\x00' is not of type 'object', 'integer', 'array', 'boolean', 'null'
```

```svg
<svg viewBox="0 0 640 230" xmlns="http://www.w3.org/2000/svg">
  <text x="320" y="24" text-anchor="middle" font-size="14">dtbs_check 경고 범주 — i.MX8MP + AM62 보드 86개, 경고 43줄</text>
  <text x="222" y="66" text-anchor="end" font-size="13">미문서화 속성 (타입 모름)</text>
  <rect x="230" y="50" width="330.0" height="22" fill="#d0564a" fill-opacity="0.7"/>
  <text x="566" y="66" font-size="12">31</text>
  <text x="222" y="100" text-anchor="end" font-size="13">pinctrl-names 개수</text>
  <rect x="230" y="84" width="63.9" height="22" fill="#d0564a" fill-opacity="0.7"/>
  <text x="300" y="100" font-size="12">6</text>
  <text x="222" y="134" text-anchor="end" font-size="13">보드 compatible 미문서화</text>
  <rect x="230" y="118" width="42.6" height="22" fill="#d0564a" fill-opacity="0.7"/>
  <text x="279" y="134" font-size="12">4</text>
  <text x="222" y="168" text-anchor="end" font-size="13">Unevaluated (vdd-supply)</text>
  <rect x="230" y="152" width="21.3" height="22" fill="#d0564a" fill-opacity="0.7"/>
  <text x="258" y="168" font-size="12">2</text>
  <line x1="230" y1="44" x2="230" y2="182" stroke="currentColor"/>
  <text x="320" y="212" text-anchor="middle" font-size="12">경고가 난 DTB 29개 / 깨끗한 DTB 57개 (같은 로그에서 grep으로 집계)</text>
</svg>
```
그림 5 — 86개 보드의 경고를 범주별로 센 결과. "미문서화 속성"(31줄)은 모두 `micrel,led-mode` 하나였고, 같은 Verdin/DHCOM PHY 설정을 공유하는 .dtsi에서 나와 여러 보드에 복제됐다. "보드 compatible 미문서화" 4줄은 한 보드(`imx8mp-tx8p-ml81-moduline-display-106`)의 4개 메시지다.

### 6.2 경고 해석 — 고칠 곳은 DTS인가 binding인가

| 경고 | 뜻 | 고칠 곳 |
|---|---|---|
| `micrel,led-mode: b'\x00\x00\x00\x00' is not of type ...` | 이 속성 타입을 정의한 스키마가 없다. `grep -rl micrel,led-mode`로 찾으면 아직 텍스트인 `net/micrel.txt`에만 있다 | binding (`micrel.txt`를 YAML로 변환) |
| `pinctrl-names: ['default'] is too short` | KSZ9893 binding이 pinctrl 상태 2개(예: default + reset)를 요구하는데 1개만 줌 | DTS (또는 binding 제약이 과하면 binding) |
| `rtc@52 ... ('vdd-supply' was unexpected)` | 보드는 RTC에 전원 레일을 연결했는데 RV3028 binding에 `vdd-supply`가 없다 | binding (HW에 실제로 VDD 핀이 있으니 binding이 불완전) |
| `failed to match any schema with compatible` | 보드 최상위 compatible이 `arm/fsl.yaml` 목록에 없다 | binding (보드 compatible 등록) |
| `hardware: b'Moduline Display V1.06\x00'` | 루트 노드에 비표준 `hardware` 속성 | DTS (속성 제거) — 리뷰를 빠져나간 경우 |

`micrel,led-mode` 줄은 1절의 ".txt가 남아 있으면 생기는 일"의 실례다 — 텍스트 binding은 dt-validate가 읽지 못하므로, 그 속성은 타입을 모르는 "미문서화" 속성이 된다.

교훈: dtbs_check 경고가 항상 "DTS가 틀렸다"는 뜻은 아니다. **binding이 HW를 덜 기술했을 수도 있다**(RTC의 `vdd-supply`). 리뷰 규칙 첫 줄 "드라이버가 지원 안 해도 binding은 HW를 완전하게 기술하라"가 지켜지지 않은 결과다. 또 하나 — `micrel,led-mode` 31줄은 사실상 **한 군데의 문제**다. 경고 수를 세기 전에 범주로 묶어야 우선순위가 보인다.

### 6.3 예제 12 — QEMU가 만든 virt.dtb는 통과할까

무엇을 확인하나: 랩의 QEMU `virt` 머신 DTB(QEMU가 런타임에 생성한 것을 덤프한 `out/virt.dtb`)를 커널 binding으로 검사하면 무엇이 나오나.

```sh
limactl shell elinux -- bash -lc 'export PATH=/var/tmp/D7/venv/bin:$PATH; cd /var/tmp/D7
  dt-validate -m -s build/Documentation/devicetree/bindings/processed-schema.json /var/lab/out/virt.dtb > d7_virt.log 2>&1
  grep "virt.dtb:" d7_virt.log | cut -c1-200'
```

```text
/var/lab/out/virt.dtb: gpio-keys (gpio-keys): 'poweroff' does not match any of the regexes: '^(button|event|key|switch|(button|event|key|switch)-[a-z0-9-]+|[a-z0-9-]+-(button|event|key|switch))$', '^p
/var/lab/out/virt.dtb: pl061@9030000 (arm,pl061): $nodename:0: 'pl061@9030000' does not match '^gpio@[0-9a-f]+$'
/var/lab/out/virt.dtb: pl031@9010000 (arm,pl031): $nodename:0: 'pl031@9010000' does not match '^rtc(@.*|-([0-9]|[1-9][0-9]+))?$'
/var/lab/out/virt.dtb: pl011@9000000 (arm,pl011): $nodename:0: 'pl011@9000000' does not match '^serial(@.*)?$'
/var/lab/out/virt.dtb: intc@8000000 (arm,cortex-a15-gic): $nodename:0: 'intc@8000000' does not match '^interrupt-controller(@[0-9a-f,]+)*$'
```

출력에서 볼 것: 5개 모두 **노드 이름** 규칙이다. QEMU는 노드 이름에 칩 이름(`pl011@...`)을 쓰는데, DT 규칙은 장치 **종류**(`serial@...`, `gpio@...`, `rtc@...`, `interrupt-controller@...`)를 쓰라고 한다. 이 규칙이 스키마의 `$nodename` 패턴으로 강제된다는 것을 볼 수 있다. 리뷰어가 가장 자주 다는 코멘트 "Node names should be generic"의 자동판이다. (QEMU DTB는 커널 트리에서 관리되는 파일이 아니므로 이 경고가 고쳐질 의무는 없다 — 부팅에도 지장 없다.)

---

## 7. 리뷰 규칙과 패치 흐름

### 7.1 리뷰어가 자주 하는 말 — 규칙 정리

`Documentation/devicetree/bindings/writing-bindings.rst`(DOs and DON'Ts)의 핵심을 Don의 센서 키트 언어로 옮기면:

| 규칙 | 나쁜 예 | 좋은 예 |
|---|---|---|
| HW를 기술하라, 정책이 아니라 | `don,odr-hz = <400>` (드라이버 설정) | ODR은 sysfs로. DT에는 핀·레일·주소만 |
| Linux·드라이버를 언급하지 마라 | `linux,poll-ms`, description "the Linux driver uses…" | "The device has a data-ready output" |
| 일반 노드 이름 | `mpu6050@68`, `pl011@9000000` | `imu@68`, `serial@9000000` |
| 구체적인 compatible + fallback | `vendor,imu`, `vendor,imu-i2c` | `vendor,d7-imu-lite`, `vendor,d7-imu` |
| 드라이버가 안 써도 HW 자원은 다 | 폴링 드라이버라서 `interrupts` 생략 | 인터럽트 핀이 있으면 binding에 넣는다 |
| 공통 속성을 다시 정의하지 마라 | `reg`에 타입·설명을 다시 씀 | `reg: maxItems: 1` |
| 단위 접미사 사용 | `don,delay = <10>` | `don,settle-time-us = <10>` |
| `*-names`는 순서 고정, 접미사 없이 | `interrupt-names = "txirq"` | `"tx"` |
| compatible에서 유추되는 것을 속성으로 만들지 마라 | `don,has-fifo;` (lite냐 아니냐로 결정됨) | compatible로 구분 |
| 노드는 드라이버를 띄우려고 만들지 마라 | MFD에 자원 없는 자식 노드 | 자원이 있는 자식만 노드로 |

### 7.2 패치 순서와 메일 흐름

`Documentation/devicetree/bindings/submitting-patches.rst`가 정한 것:

1. binding(`Documentation/` + `include/dt-bindings/`)은 **별도 패치**이고, 시리즈에서 **드라이버보다 앞**에 온다.
2. 제목 접두어는 `dt-bindings: <디렉터리>: ...` (예: `dt-bindings: iio: imu: Add Don D7 IMU`). "binding", "YAML", "Documentation"은 제목에 쓰지 않는다. (ASoC·media·regulator·SPI는 `<디렉터리>: dt-bindings: ...` 역순을 선호.)
3. `make dt_binding_check` 통과 필수.
4. 이중 라이선스 `(GPL-2.0-only OR BSD-2-Clause)`.
5. `devicetree@vger.kernel.org`와 DT maintainers를 Cc — `scripts/get_maintainer.pl`로 찾는다.
6. DTS 패치는 시리즈의 **맨 끝**. DTS는 드라이버 서브시스템 트리가 아니라 **SoC 플랫폼 트리**를 통해 머지된다.
7. DTS에 쓰는 모든 compatible은 binding에 **먼저** 문서화돼 있어야 한다(checkpatch가 경고한다 — 7.4절).

```svg
<svg viewBox="0 0 680 400" xmlns="http://www.w3.org/2000/svg">
  <defs><marker id="ah4" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto-start-reverse"><path d="M0,0 L10,5 L0,10 z" fill="currentColor"/></marker></defs>
  <rect x="20" y="16" width="200" height="104" rx="6" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
  <text x="120" y="38" text-anchor="middle" font-size="13">로컬 (Don)</text>
  <text x="120" y="60" text-anchor="middle" font-size="12">make dt_binding_check</text>
  <text x="120" y="78" text-anchor="middle" font-size="12">make CHECK_DTBS=y x.dtb</text>
  <text x="120" y="96" text-anchor="middle" font-size="12">checkpatch.pl --strict</text>
  <text x="120" y="112" text-anchor="middle" font-size="12">get_maintainer.pl</text>
  <rect x="250" y="16" width="200" height="104" rx="6" fill="#888" fill-opacity="0.12" stroke="#888"/>
  <text x="350" y="38" text-anchor="middle" font-size="13">패치 시리즈</text>
  <text x="350" y="62" text-anchor="middle" font-size="12">1/4 vendor-prefixes 추가</text>
  <text x="350" y="80" text-anchor="middle" font-size="12">2/4 dt-bindings: 새 binding</text>
  <text x="350" y="98" text-anchor="middle" font-size="12">3/4 드라이버</text>
  <text x="350" y="116" text-anchor="middle" font-size="12">4/4 arm64: dts: 보드</text>
  <line x1="220" y1="68" x2="246" y2="68" stroke="currentColor" marker-end="url(#ah4)"/>
  <rect x="480" y="16" width="180" height="104" rx="6" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
  <text x="570" y="38" text-anchor="middle" font-size="13">메일링 리스트</text>
  <text x="570" y="62" text-anchor="middle" font-size="12">devicetree@vger</text>
  <text x="570" y="80" text-anchor="middle" font-size="12">+ 서브시스템 리스트</text>
  <text x="570" y="98" text-anchor="middle" font-size="12">+ linux-kernel</text>
  <line x1="450" y1="68" x2="476" y2="68" stroke="currentColor" marker-end="url(#ah4)"/>
  <rect x="380" y="160" width="280" height="64" rx="6" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
  <text x="520" y="184" text-anchor="middle" font-size="13">자동 검사 봇</text>
  <text x="520" y="206" text-anchor="middle" font-size="12">dt_binding_check 실패 시 회신</text>
  <rect x="20" y="160" width="345" height="64" rx="6" fill="#d0564a" fill-opacity="0.12" stroke="#d0564a"/>
  <text x="192" y="184" text-anchor="middle" font-size="13">DT maintainers 리뷰</text>
  <text x="192" y="206" text-anchor="middle" font-size="12">Rob Herring · Krzysztof Kozlowski · Conor Dooley</text>
  <line x1="570" y1="122" x2="570" y2="156" stroke="currentColor" marker-end="url(#ah4)"/>
  <line x1="500" y1="122" x2="300" y2="156" stroke="currentColor" marker-end="url(#ah4)"/>
  <path d="M 120 158 C 60 140, 60 135, 100 122" fill="none" stroke="currentColor" stroke-dasharray="5 4" marker-end="url(#ah4)"/>
  <text x="24" y="146" font-size="12">v2, v3 …</text>
  <rect x="20" y="266" width="300" height="56" rx="6" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
  <text x="170" y="290" text-anchor="middle" font-size="13">Reviewed-by / Acked-by</text>
  <text x="170" y="310" text-anchor="middle" font-size="12">binding 패치에 태그가 붙는다</text>
  <line x1="185" y1="226" x2="185" y2="262" stroke="currentColor" marker-end="url(#ah4)"/>
  <rect x="360" y="252" width="300" height="44" rx="6" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
  <text x="510" y="279" text-anchor="middle" font-size="12">binding + 드라이버 → 서브시스템 트리 (IIO)</text>
  <rect x="360" y="306" width="300" height="44" rx="6" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
  <text x="510" y="333" text-anchor="middle" font-size="12">DTS → SoC 플랫폼 트리 (i.MX 등)</text>
  <line x1="320" y1="286" x2="356" y2="276" stroke="currentColor" marker-end="url(#ah4)"/>
  <line x1="320" y1="300" x2="356" y2="326" stroke="currentColor" marker-end="url(#ah4)"/>
  <text x="340" y="384" text-anchor="middle" font-size="12">모두 머지 창(merge window)에 mainline으로 모인다</text>
</svg>
```
그림 6 — binding 패치의 여정. 로컬 검사 → 순서가 정해진 시리즈 → 리스트 + 자동 봇 + DT maintainers 리뷰 → 태그를 받은 뒤 binding과 드라이버는 서브시스템 트리로, DTS는 SoC 트리로 따로 머지된다. 지적을 받으면 v2, v3로 다시 보낸다.

자동 봇에 대해: devicetree 리스트에 올라온 binding 패치에는 Rob Herring이 운영하는 봇이 `dt_binding_check`를 돌려서 실패하면 결과를 회신한다. **로컬에서 먼저 돌려 보내지 않으면 사람 리뷰 전에 봇에게 먼저 반려된다.** DT maintainers는 `MAINTAINERS` 파일의 "OPEN FIRMWARE AND FLATTENED DEVICE TREE BINDINGS" 항목에 있다.

```text
OPEN FIRMWARE AND FLATTENED DEVICE TREE BINDINGS
M:	Rob Herring <robh@kernel.org>
M:	Krzysztof Kozlowski <krzk+dt@kernel.org>
M:	Conor Dooley <conor+dt@kernel.org>
L:	devicetree@vger.kernel.org
S:	Maintained
Q:	http://patchwork.kernel.org/project/devicetree/list/
...
F:	Documentation/devicetree/
F:	arch/*/boot/dts/
F:	include/dt-bindings/
```

### 7.3 예제 13 — binding 패치에 `checkpatch.pl`

무엇을 확인하나: 최종 binding을 메일 패치 형식(From/Subject/Signed-off-by + diff)으로 만들어 checkpatch에 넣으면 무엇이 나오는지. 소스가 git 저장소가 아니어서 `diff -u`와 작은 셸 스크립트(`mkpatch.sh`)로 `git format-patch`와 같은 모양을 만들었다.

```sh
limactl shell elinux -- bash -lc 'cd /var/tmp/D7; B=Documentation/devicetree/bindings
  /Users/donh/.elinux_scratch/D7/mkpatch.sh 0001-imu.patch "dt-bindings: iio: imu: Add Don D7 IMU" \
     "Add binding for the Don D7 6-axis IMU with I2C and SPI interfaces." \
     $B/iio/imu/don,d7-imu.yaml /dev/null linux/$B/iio/imu/don,d7-imu.yaml
  head -14 0001-imu.patch; cd linux && ./scripts/checkpatch.pl ../0001-imu.patch'
```

```text
From: Don H <don@example.com>
Date: Sat, 10 Oct 2026 12:00:00 -0700
Subject: [PATCH] dt-bindings: iio: imu: Add Don D7 IMU

Add binding for the Don D7 6-axis IMU with I2C and SPI interfaces.

Signed-off-by: Don H <don@example.com>
---
diff --git a/Documentation/devicetree/bindings/iio/imu/don,d7-imu.yaml b/Documentation/devicetree/bindings/iio/imu/don,d7-imu.yaml
new file mode 100644
--- /dev/null
+++ b/Documentation/devicetree/bindings/iio/imu/don,d7-imu.yaml
@@ -0,0 +1,84 @@
+# SPDX-License-Identifier: (GPL-2.0-only OR BSD-2-Clause)
WARNING: added, moved or deleted file(s), does MAINTAINERS need updating?
#10: 
new file mode 100644

total: 0 errors, 1 warnings, 84 lines checked

NOTE: For some of the reported defects, checkpatch may be able to
      mechanically convert to the typical style using --fix or --fix-inplace.

../0001-imu.patch has style problems, please review.

NOTE: If any of the errors are false positives, please report
      them to the maintainer, see CHECKPATCH in MAINTAINERS.
```

출력에서 볼 것: 깨끗한 binding 패치에서 남는 것은 "새 파일이 생겼는데 MAINTAINERS를 고쳐야 하나?" 하나다. binding의 `maintainers:` 항목이 그 역할을 하므로, 새 드라이버 패치에서 MAINTAINERS 항목을 같이 추가하면 사라진다. checkpatch는 YAML 내용을 스키마로 검사하지는 않는다 — **스키마 검사는 `dt_binding_check`의 몫, 형식·절차 검사는 checkpatch의 몫**이다.

### 7.4 예제 14 — 섞인 패치와 문서화 안 된 compatible

무엇을 확인하나: (a) binding과 DTS를 한 패치에 섞으면, (b) binding이 트리에 없는데 DTS·드라이버가 compatible을 쓰면 checkpatch가 무엇을 말하는지. 이번엔 **원본 랩 트리**(don binding 없음)에서 돌렸다.

```sh
limactl shell elinux -- bash -lc 'cd /var/lab/src/linux-6.18.55
  echo "### --no-tree"; ./scripts/checkpatch.pl -q --no-tree /var/tmp/D7/0002-mixed.patch
  echo "### with tree"; ./scripts/checkpatch.pl -q /var/tmp/D7/0002-mixed.patch
  echo "### driver stub"; ./scripts/checkpatch.pl -q /var/tmp/D7/0003-drv.patch'
```

```text
### --no-tree
WARNING: added, moved or deleted file(s), does MAINTAINERS need updating?
#10: 
new file mode 100644

WARNING: DT binding docs and includes should be a separate patch. See: Documentation/devicetree/bindings/submitting-patches.rst

total: 0 errors, 2 warnings, 98 lines checked
### with tree
WARNING: added, moved or deleted file(s), does MAINTAINERS need updating?
#10: 
new file mode 100644

WARNING: DT binding docs and includes should be a separate patch. See: Documentation/devicetree/bindings/submitting-patches.rst

WARNING: DT compatible string "don,d7-imu" appears un-documented -- check ./Documentation/devicetree/bindings/
#111: FILE: arch/arm64/boot/dts/freescale/imx8mp-d7-kit.dts:9:
+		compatible = "don,d7-imu";

WARNING: DT compatible string vendor "don" appears un-documented -- check ./Documentation/devicetree/bindings/vendor-prefixes.yaml
#111: FILE: arch/arm64/boot/dts/freescale/imx8mp-d7-kit.dts:9:
+		compatible = "don,d7-imu";

total: 0 errors, 4 warnings, 98 lines checked
### driver stub
WARNING: added, moved or deleted file(s), does MAINTAINERS need updating?
#10: 
new file mode 100644

WARNING: DT compatible string "don,d7-imu" appears un-documented -- check ./Documentation/devicetree/bindings/
#20: FILE: drivers/iio/imu/d7_imu.c:7:
+	{ .compatible = "don,d7-imu" },

WARNING: DT compatible string vendor "don" appears un-documented -- check ./Documentation/devicetree/bindings/vendor-prefixes.yaml
#20: FILE: drivers/iio/imu/d7_imu.c:7:
+	{ .compatible = "don,d7-imu" },

total: 0 errors, 3 warnings, 10 lines checked
```

(`0002-mixed.patch`는 최종 binding + 보드 DTS 조각 `imx8mp-d7-kit.dts`를 한 패치에, `0003-drv.patch`는 `of_device_id` 테이블만 있는 10줄짜리 드라이버 스텁 `drivers/iio/imu/d7_imu.c`를 담았다. 둘 다 같은 `mkpatch.sh`로 만들었다.)

출력에서 볼 것:

- `DT binding docs and includes should be a separate patch` — 7.2절 규칙 1이 자동 검사된다.
- `appears un-documented` 두 줄은 **트리에서 binding을 grep**해서 찾기 때문에 `--no-tree`에서는 나오지 않는다. DTS(`imx8mp-d7-kit.dts`)뿐 아니라 드라이버 `.c`의 `of_device_id` 테이블에도 같은 검사가 적용된다.
- 즉 "binding 패치를 먼저, 그다음 드라이버·DTS" 순서로 트리에 들어가야 이 경고가 사라진다.

### 7.5 예제 15 — `get_maintainer.pl`로 받는 사람 찾기

```sh
limactl shell elinux -- bash -lc 'cd /var/tmp/D7/linux && ./scripts/get_maintainer.pl ../0001-imu.patch'
```

```text
Jonathan Cameron <jic23@kernel.org> (maintainer:IIO SUBSYSTEM AND DRIVERS)
David Lechner <dlechner@baylibre.com> (reviewer:IIO SUBSYSTEM AND DRIVERS)
"Nuno Sá" <nuno.sa@analog.com> (reviewer:IIO SUBSYSTEM AND DRIVERS)
Andy Shevchenko <andy@kernel.org> (reviewer:IIO SUBSYSTEM AND DRIVERS)
Rob Herring <robh@kernel.org> (maintainer:OPEN FIRMWARE AND FLATTENED DEVICE TREE BINDINGS)
Krzysztof Kozlowski <krzk+dt@kernel.org> (maintainer:OPEN FIRMWARE AND FLATTENED DEVICE TREE BINDINGS)
Conor Dooley <conor+dt@kernel.org> (maintainer:OPEN FIRMWARE AND FLATTENED DEVICE TREE BINDINGS)
Don H <don@example.com> (in file)
linux-iio@vger.kernel.org (open list:IIO SUBSYSTEM AND DRIVERS)
devicetree@vger.kernel.org (open list:OPEN FIRMWARE AND FLATTENED DEVICE TREE BINDINGS)
linux-kernel@vger.kernel.org (open list)
```

출력에서 볼 것: 파일 경로 하나(`Documentation/devicetree/bindings/iio/...`)가 **두 maintainer 그룹**에 걸린다 — IIO 서브시스템(이 binding을 결국 머지할 사람)과 DT binding maintainers(리뷰할 사람). `(in file)`은 binding의 `maintainers:`에 적은 나 자신이다. 소스가 git 저장소가 아니라서 git 이력 기반 추천은 빠졌다.

---

## 8. 커널 안에서는 무슨 일이 일어나나

### 8.1 binding은 런타임에 존재하지 않는다

커널 이미지에는 YAML도 json-schema도 없다. 부팅 시 일어나는 일은 D3·D8에서 본 것 그대로다.

1. 부트로더가 DTB를 메모리에 놓고 주소를 넘긴다(C 모듈).
2. 커널의 `unflatten_device_tree()`가 FDT를 `struct device_node` 트리로 펼친다.
3. 버스 코어가 노드마다 디바이스를 만든다(I2C 컨트롤러 드라이버가 자식 노드마다 `i2c_client` 생성).
4. 드라이버 코어가 `compatible`과 드라이버의 `of_match_table`을 비교해 `probe()`를 부른다.
5. `probe()`가 속성을 읽는다 — 여기서 binding이 **간접적으로** 다시 등장한다.

즉 binding은 "드라이버가 `probe()`에서 어떤 이름의 속성을 어떤 형태로 기대하는가"를 문서화한 것이고, 드라이버 코드의 속성 이름 문자열과 **반드시 일치해야** 한다. 일치하지 않아도 컴파일러는 모른다 — 그래서 사람(리뷰)과 dt-schema(빌드)가 지킨다.

```svg
<svg viewBox="0 0 680 330" xmlns="http://www.w3.org/2000/svg">
  <defs><marker id="ah5" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto-start-reverse"><path d="M0,0 L10,5 L0,10 z" fill="currentColor"/></marker></defs>
  <text x="105" y="22" text-anchor="middle" font-size="13">binding 속성</text>
  <text x="495" y="22" text-anchor="middle" font-size="13">드라이버가 읽는 API (D8 · H · I)</text>
  <rect x="20" y="34" width="170" height="34" rx="4" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
  <text x="105" y="56" text-anchor="middle" font-size="12">compatible</text>
  <rect x="330" y="34" width="330" height="34" rx="4" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
  <text x="495" y="56" text-anchor="middle" font-size="12">of_match_table → probe() 호출</text>
  <rect x="20" y="78" width="170" height="34" rx="4" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
  <text x="105" y="100" text-anchor="middle" font-size="12">reg</text>
  <rect x="330" y="78" width="330" height="34" rx="4" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
  <text x="495" y="100" text-anchor="middle" font-size="12">client-&gt;addr (I2C 코어가 채움)</text>
  <rect x="20" y="122" width="170" height="34" rx="4" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
  <text x="105" y="144" text-anchor="middle" font-size="12">interrupts</text>
  <rect x="330" y="122" width="330" height="34" rx="4" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
  <text x="495" y="144" text-anchor="middle" font-size="12">client-&gt;irq (I2C 코어가 매핑)</text>
  <rect x="20" y="166" width="170" height="34" rx="4" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
  <text x="105" y="188" text-anchor="middle" font-size="12">vdd-supply</text>
  <rect x="330" y="166" width="330" height="34" rx="4" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
  <text x="495" y="188" text-anchor="middle" font-size="12">devm_regulator_get(dev, "vdd")</text>
  <rect x="20" y="210" width="170" height="34" rx="4" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
  <text x="105" y="232" text-anchor="middle" font-size="12">reset-gpios</text>
  <rect x="330" y="210" width="330" height="34" rx="4" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
  <text x="495" y="232" text-anchor="middle" font-size="12">devm_gpiod_get_optional(dev, "reset", ...)</text>
  <rect x="20" y="254" width="170" height="34" rx="4" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
  <text x="105" y="276" text-anchor="middle" font-size="12">mount-matrix</text>
  <rect x="330" y="254" width="330" height="34" rx="4" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
  <text x="495" y="276" text-anchor="middle" font-size="12">iio_read_mount_matrix(dev, &amp;orient)</text>
  <line x1="192" y1="51" x2="326" y2="51" stroke="currentColor" marker-end="url(#ah5)"/>
  <line x1="192" y1="95" x2="326" y2="95" stroke="currentColor" marker-end="url(#ah5)"/>
  <line x1="192" y1="139" x2="326" y2="139" stroke="currentColor" marker-end="url(#ah5)"/>
  <line x1="192" y1="183" x2="326" y2="183" stroke="currentColor" marker-end="url(#ah5)"/>
  <line x1="192" y1="227" x2="326" y2="227" stroke="currentColor" marker-end="url(#ah5)"/>
  <line x1="192" y1="271" x2="326" y2="271" stroke="currentColor" marker-end="url(#ah5)"/>
  <text x="340" y="314" text-anchor="middle" font-size="12">접미사 규칙: 속성 "vdd-supply" ↔ 이름 "vdd", "reset-gpios" ↔ 이름 "reset"</text>
</svg>
```
그림 7 — binding의 속성 이름이 드라이버 API의 문자열 인자로 이어지는 지도. `-supply`, `-gpios` 접미사는 프레임워크가 붙이므로 드라이버는 앞부분 이름만 쓴다.

### 8.2 실제 드라이버에서 확인 — inv_mpu6050

무엇을 확인하나: 2절의 binding이 말한 속성 이름이 드라이버 코드에 그대로 있는지.

```sh
limactl shell elinux -- bash -lc 'cd /var/lab/src/linux-6.18.55
  grep -n -A6 "of_device_id inv_of_match" drivers/iio/imu/inv_mpu6050/inv_mpu_i2c.c
  grep -n "device_property\|iio_read_mount_matrix\|devm_regulator_get" drivers/iio/imu/inv_mpu6050/inv_mpu_core.c'
```

```text
200:static const struct of_device_id inv_of_match[] = {
201-	{
202-		.compatible = "invensense,mpu6050",
203-		.data = (void *)INV_MPU6050
204-	},
205-	{
206-		.compatible = "invensense,mpu6500",
1908:	st->level_shifter = device_property_read_bool(dev,
1912:		result = iio_read_mount_matrix(dev, &st->orientation);
1956:	st->vdd_supply = devm_regulator_get(dev, "vdd");
1962:	st->vddio_supply = devm_regulator_get(dev, "vddio");
```

출력에서 볼 것: binding의 `invensense,level-shifter`(boolean) ↔ `device_property_read_bool(dev, "invensense,level-shifter")`, `vdd-supply`/`vddio-supply` ↔ `devm_regulator_get(dev, "vdd"/"vddio")`, `mount-matrix` ↔ `iio_read_mount_matrix()`. `.data`로 칩 종류를 넘기는 패턴은 "compatible에서 유추되는 것은 속성으로 만들지 마라" 규칙의 드라이버 쪽 모습이다. `device_property_*`(fwnode)를 쓰면 같은 드라이버가 ACPI 시스템에서도 동작한다(D8).

### 8.3 다시 만나는 곳

| 이 노트의 개념 | 다시 나오는 곳 |
|---|---|
| compatible + fallback, `.data` | D3(매칭), G3(platform driver), H(I2C/SPI 드라이버) |
| `*-supply`, `*-gpios`, `interrupts` | D4, I(regulator·GPIO·IRQ 프레임워크), F6 |
| `device_property_read_*` 가 binding과 맞아야 함 | D8 |
| binding ABI, 새 compatible로 호환 깨기 | E7(커뮤니티·패치), P(BSP 실무) |
| dtbs_check를 CI에 | P(보드 bring-up), K(빌드 시스템) |

---

## 9. Embedded 실무에서 다시 보기

### 9.1 벤더 BSP 커널을 받았을 때

i.MX8MP 기반 센서 키트를 만든다고 하자. SoM 벤더가 준 BSP 커널(예: 6.6 기반)의 DTS에 `vendor,custom-imu-irq-mode = <2>;` 같은 속성이 잔뜩 있다. 이걸 mainline 커널로 옮길 때의 첫 작업이 `make CHECK_DTBS=y vendor/board.dtb`다. 경고 목록이 곧 **"mainline binding과 다른 곳" 목록**이고, 6.2절처럼 범주로 묶으면 "DTS를 고칠 것"과 "mainline 드라이버가 아예 지원 안 하는 것"이 갈린다. 업그레이드 견적을 낼 때 매우 쓸모 있다.

### 9.2 사내 전용 장치의 binding

사내 FPGA 블록처럼 upstream에 갈 일이 없는 장치도 binding YAML을 쓰는 것이 좋다. 비용은 YAML 파일 하나이고, 얻는 것은 (1) 보드 DTS가 바뀔 때마다 CI가 자동으로 검사, (2) 새로 온 엔지니어가 읽을 정확한 문서, (3) 나중에 upstream하기로 하면 이미 반쯤 끝난 상태다. 사내 벤더 접두어(회사 주식 티커 등)를 정해 두는 것도 이때다 — 아무 접두어나 쓰면 나중에 다른 회사와 충돌한다.

### 9.3 CI에 넣기

`dt_binding_check`와 `CHECK_DTBS=y`는 경고가 있어도 exit 0이므로(4.5절), CI에서는 출력을 검사한다. 예를 들어 센서 키트의 보드 DTB 몇 개만:

```sh
make O=out ARCH=arm64 CHECK_DTBS=y freescale/imx8mp-d7-kit.dtb 2>&1 | tee dtbs.log
! grep -q "\.dtb: " dtbs.log     # 경고 줄이 하나라도 있으면 실패
```

(이 두 줄은 패턴을 보이기 위한 것이고 보드 DTS가 실제로 없어서 실행하지 않았다. 위 `grep "\.dtb: "` 패턴 자체는 6.1절 집계에 실제로 쓴 것이다.) 기존 경고가 많은 BSP라면 "경고 수가 늘면 실패"로 시작하는 것이 현실적이다.

### 9.4 드라이버와 binding을 같이 쓸 때의 순서

Don이 센서 드라이버를 upstream한다면 실제 작업 순서는: 데이터시트에서 핀·전원 레일·인터럽트를 뽑아 **binding부터** 쓴다 → `dt_binding_check` → 드라이버는 binding에 적힌 이름만 읽는다 → 보드 DTS에 노드 추가, `CHECK_DTBS=y` → checkpatch → 시리즈(binding → 드라이버 → DTS). binding을 먼저 쓰면 "드라이버 편의를 위한 속성"을 넣고 싶은 유혹을 미리 걸러낼 수 있다.

---

## 10. 흔한 실수와 증상

| 실수 | 증상 | 원인 | 고치는 법 |
|---|---|---|---|
| `$ref`를 쓰면서 `additionalProperties: false` | 예제의 `spi-max-frequency`가 거부됨 | additionalProperties는 `$ref`로 들어온 속성을 모름 | `unevaluatedProperties: false` |
| title·description에 "binding", "driver", "Linux" | 메타스키마 에러 / 리뷰 지적 | 문서는 HW만 기술 | HW 설명으로 바꾸기 |
| 벤더 속성에 타입 없음 | `b'\x00...' is not of type` | 스키마가 타입을 몰라 바이트로 남음 | `$ref: /schemas/types.yaml#/definitions/uint32` 또는 단위 접미사 |
| 예제 interrupts를 1셀로 | 예제 dtc/검증 에러 | 예제의 가짜 인터럽트 컨트롤러는 2셀 | `<12 IRQ_TYPE_EDGE_RISING>` |
| compatible fallback 순서 뒤집음 | `is too long`, `'...-lite' was expected` | 구체적 → 일반 순서 | 구체적인 칩을 앞에 |
| 기존 binding에 required 추가 | 리뷰 반려, 옛 DTB가 경고 | ABI 깨짐 | 선택 속성 + 드라이버 기본값, 또는 새 compatible |
| 드라이버 정책을 DT 속성으로 | 리뷰 반려 ("not HW description") | DT는 HW 기술 | sysfs·모듈 파라미터·유저 설정으로 |
| `dt_binding_check` 결과를 exit code로 판단 | 경고가 있어도 CI 통과 | Makefile 규칙이 `dt-validate`의 실패를 무시하고 경고만 출력 | 로그를 grep |
| 소스 트리에 `.config`가 있는데 `O=` 빌드 | "source tree is not clean" 에러 | Kbuild가 in-tree 산출물과 섞이는 것을 막음 | `make mrproper` 후 `O=` 사용 (공용 트리면 복사본에서) |
| vendor prefix 미등록 | vendor-prefixes 정규식 목록 에러, checkpatch 경고 | 모든 접두어는 등록제 | `vendor-prefixes.yaml`에 별도 패치 |

실제 규칙은 `scripts/Makefile.dtbs`에 있다(6.18.55):

```make
cmd_dtb_check = $(if $(dtb-check-enabled),; $(DT_CHECKER) $(DT_CHECKER_FLAGS) -u $(srctree)/$(DT_BINDING_DIR) -p $(DT_TMP_SCHEMA) $@ || true)
```

---

## 11. 면접에서 이렇게 말한다

**Q.** What is a device tree binding, and why does the kernel have YAML schemas for them?

**A.** binding은 특정 compatible을 가진 노드가 가져야 할 속성·타입·개수·값 범위를 정한 계약서다. DTS 작성자, 드라이버 작성자, 다른 OS가 같은 정의를 공유한다. 예전엔 자유 텍스트라 기계 검증이 안 됐고 예제와 본문이 어긋나도 몰랐다. 지금은 json-schema를 YAML로 쓰고 dt-schema 도구가 binding 자체와 모든 DTB를 자동 검증한다.

> A binding is the contract for a compatible string: which properties a node must or may have, their types, how many entries, and allowed values. It's shared by DTS authors, driver authors and other OSes like U-Boot. The old free-text bindings couldn't be checked by tools, so the kernel moved to json-schema written in YAML. With dt-schema, `make dt_binding_check` validates the bindings and their examples, and `make dtbs_check` validates every board DTB against them.

**꼬리 질문**: "커널이 부팅 중에 binding을 검사하나요?" → 아니다. 런타임엔 YAML이 없다. 검증은 빌드·리뷰 시점에만 있다.

**Q.** How would you validate a board's device tree before sending it for review?

**A.** dtschema를 설치하고 `make CHECK_DTBS=y vendor/board.dtb`로 그 보드만 검사한다(전체는 `make dtbs_check`). 새 binding을 같이 쓰면 `make dt_binding_check DT_SCHEMA_FILES=...`도 돌린다. 경고가 있어도 exit 0이라 로그를 본다. 마지막으로 checkpatch로 문서화 안 된 compatible이 없는지 확인한다.

> I install dtschema and yamllint, then run `make CHECK_DTBS=y` on just my board's DTB, which runs dt-validate against the processed schema. If I'm adding a binding I also run `dt_binding_check` with `DT_SCHEMA_FILES` set to my file, so the binding and its examples are checked. Since the targets return success even with warnings, I read the log. Then checkpatch, which also flags compatibles that aren't documented in a binding.

**Q.** What's the difference between `additionalProperties: false` and `unevaluatedProperties: false`?

**A.** 둘 다 목록 밖 속성을 금지하지만, additionalProperties는 그 스키마 파일의 properties만 보고, unevaluatedProperties는 `$ref`나 `allOf`로 다른 스키마에서 평가된 속성까지 허용한다. 그래서 spi-peripheral-props 같은 공통 스키마를 참조하면 unevaluatedProperties를 써야 한다. 실제로 additionalProperties로 썼다가 SPI 예제의 spi-max-frequency가 거부되는 걸 겪었다.

> Both reject properties that aren't described. `additionalProperties` only looks at the properties listed in that same schema object, so anything inherited through a `$ref` counts as extra. `unevaluatedProperties` considers everything evaluated by referenced schemas too. So if the binding references something like spi-peripheral-props, you need `unevaluatedProperties: false`, otherwise `spi-max-frequency` gets rejected. I hit exactly that error when I added an SPI example to my own binding.

**Q.** Can you add a new required property to an existing binding?

**A.** 보통 안 된다. binding은 ABI이고 이미 출하된 DTB가 그 속성 없이 존재한다. 새 속성은 선택으로 넣고 드라이버는 없을 때 옛 동작을 기본값으로 쓴다. 정말 호환이 깨져야 하면 새 compatible을 만들고 드라이버가 둘 다 매칭하게 한다.

> Generally no. Bindings are treated as a stable ABI because DTBs ship in bootloaders and newer kernels must still work with them. I'd add it as an optional property and make the driver fall back to the old behaviour when it's missing. If the change is truly incompatible, the rule is to introduce a new compatible string and have the driver match both.

**Q.** What are typical review comments on a new binding?

**A.** 노드 이름은 일반 이름으로(`imu@68`, 칩 이름 금지), title·description에 Linux나 드라이버 언급 금지, 정책이 아닌 HW만 기술(ODR 같은 설정은 sysfs), compatible은 구체적으로 하고 상위 호환이면 fallback, 공통 속성 재정의 금지, 단위 접미사 사용, 벤더 속성은 타입과 설명 필수, vendor prefix 등록. 그리고 드라이버가 안 써도 HW에 있는 인터럽트·전원은 binding에 넣으라는 것.

> Generic node names, so `imu@68` rather than the part number. No mention of Linux or drivers. Describe hardware, not configuration, so things like output data rate belong in sysfs, not DT. Specific compatibles with fallbacks for compatible variants. Don't redefine common properties, use standard unit suffixes, give vendor properties a type and description, and register the vendor prefix. And describe the hardware completely, like the interrupt line, even if the driver only polls today.

**Q.** How is a binding patch submitted and who merges it?

**A.** binding은 별도 패치로 시리즈 앞에, 제목은 `dt-bindings: <dir>: ...`, `dt_binding_check` 통과 후 get_maintainer로 찾은 devicetree 리스트와 DT maintainers(Rob Herring, Krzysztof Kozlowski, Conor Dooley)를 Cc한다. 봇이 dt_binding_check를 다시 돌린다. 태그를 받으면 binding과 드라이버는 서브시스템 트리, DTS는 시리즈 맨 끝에서 SoC 플랫폼 트리로 따로 머지된다.

> The binding goes in its own patch at the start of the series, with a `dt-bindings: <dir>:` subject, after it passes `dt_binding_check`. I Cc the devicetree list and the DT maintainers from get_maintainer.pl. There's a bot that runs the checks on the list. After a Reviewed-by from a DT maintainer, the subsystem maintainer takes the binding with the driver, and the DTS patches at the end of the series go through the SoC tree.

**Q.** Your vendor BSP's DTS produces hundreds of dtbs_check warnings. What do you do?

**A.** 먼저 범주로 묶는다 — 한 .dtsi의 속성 하나가 수십 줄을 만들기도 한다(실제로 86개 보드 중 경고 43줄의 31줄이 속성 하나였다). 그다음 DTS 문제(잘못된 이름·순서), mainline binding이 HW를 덜 기술한 문제, 벤더 전용 속성(mainline 드라이버 미지원)으로 나눈다. 셋째가 업그레이드 작업량의 실체다. CI에는 "경고 수가 늘면 실패"부터 건다.

> I'd group the warnings first, because one property in a shared dtsi can produce dozens of lines. When I ran it on 86 i.MX8MP and AM62 boards, 31 of the 43 warnings came from a single PHY property. Then I'd split them into DTS mistakes, bindings that are incomplete for real hardware, and vendor-only properties that mainline drivers don't support. That last group is the real porting work. In CI I'd start with "no new warnings" rather than zero.

---

## 12. 직접 해보기

1. (개념) binding이 "ABI"라는 말의 뜻을 SSD 펌웨어의 "호스트와의 NVMe 명령 규약"에 빗대어 세 문장으로 설명해 보라. 왜 새 required 속성 추가가 금지되는가?
   정답/힌트: 출하된 DTB = 이미 판매된 호스트 드라이버. 새 커널이 옛 DTB에서 동작해야 하므로 추가만 가능, 의미 변경은 새 compatible.

2. (관찰) `DT_SCHEMA_FILES=/gpio/`로 GPIO 디렉터리 전체에 `dt_binding_check`를 돌리고 걸린 시간과 경고 수를 기록하라. 두 번째 실행은 왜 빨라지나?
   정답/힌트: `processed-schema.json`과 `.example.dtb`가 이미 있고 `if_changed`가 바뀐 것만 다시 만든다.

3. (코드) `don,d7-imu.yaml`에 `interrupt-names`를 추가해 인터럽트 핀 두 개(`"data-ready"`, `"wakeup"`)를 허용하되 `interrupts`가 1개일 때도 통과하도록 `minItems`/`maxItems`를 써라. 예제를 고쳐 `dt_binding_check`가 조용한지 확인.
   정답/힌트: `interrupts: minItems: 1, maxItems: 2`, `interrupt-names: minItems: 1, items: [const: data-ready, const: wakeup]`. names에 `irq` 접미사를 붙이지 말 것.

4. (코드) `if`/`then`으로 "`don,d7-imu-lite`는 `reset-gpios`가 없다"(lite 칩에 리셋 핀이 없다고 가정)를 표현하고, lite 노드에 `reset-gpios`를 넣은 DTS로 `dt-validate` 에러를 받아 보라.
   정답/힌트: mpu6050.yaml의 `i2c-gate: false` 패턴 그대로. `contains: const: don,d7-imu-lite` → `then: properties: reset-gpios: false`.

5. (관찰) `ti/k3-am625-beagleplay.dtb`의 경고가 0인 것을 확인한 뒤, 복사한 DTS에서 IMU나 아무 I2C 장치 노드 이름을 칩 이름으로 바꿔 다시 검사하라. 어떤 스키마가 잡는가?
   정답/힌트: 그 장치 binding에 `$nodename` 패턴이 있는 경우에만 잡힌다 — 모든 binding이 노드 이름을 강제하지는 않는다(리뷰가 대신 잡는다).

6. (관찰) `dt-validate`를 `-m` 없이, 그리고 `-l <내 binding 경로>`로 돌려 5.2절 출력과 비교하라. 무엇이 사라지나?
   정답/힌트: `-m`이 없으면 `failed to match any schema with compatible`이 사라지고, `-l`로 스키마를 제한하면 vendor-prefixes·core 스키마 에러가 줄어든다(Makefile이 `DT_SCHEMA_FILES`를 주면 `-l`을 쓰는 이유).

7. (리뷰) 6.2절의 `rtc@52 ... 'vdd-supply' was unexpected`를 고치는 binding 패치 제목과 커밋 메시지 본문 세 줄을 써 보라.
   정답/힌트: `dt-bindings: rtc: microcrystal,rv3028: Add vdd-supply` — 본문은 "칩에 VDD 핀이 있고 보드가 레일을 기술한다"는 HW 근거.

---

## 13. 용어 사전

| 용어 | 뜻 | 한 줄 설명 |
|---|---|---|
| binding | DT 노드의 규칙 문서 | compatible별 필수·선택 속성, 타입, 값 범위를 정한 계약 |
| DT schema | YAML로 쓴 json-schema binding 형식 | `Documentation/devicetree/bindings/**/*.yaml` |
| dt-schema / dtschema | 검증 도구 프로젝트 / pip 패키지 | `dt-doc-validate`, `dt-mk-schema`, `dt-validate` 포함 |
| meta-schema | 스키마의 스키마 | binding 파일 자체가 지켜야 할 규칙 (`meta-schemas/core.yaml`) |
| processed-schema.json | 모든 binding 합본 | `dt-mk-schema`가 fixup 후 만든 검증용 파일 |
| `dt_binding_check` | make 타깃 | binding 파일과 그 examples 검증 |
| `dtbs_check` / `CHECK_DTBS=y` | make 타깃 / 변수 | 보드 DTB를 스키마로 검증 |
| `DT_SCHEMA_FILES` | make 변수 | 검사할 binding 파일을 부분 문자열로 제한 (`:` 구분) |
| `$ref` | json-schema 참조 | 다른 스키마(공통 속성)를 상속 |
| `unevaluatedProperties` | json-schema 키워드 | `$ref`로 평가된 것까지 포함해 나머지 속성 금지 |
| fallback compatible | compatible 목록의 뒤쪽 문자열 | 새 칩을 모르는 드라이버도 매칭되게 하는 호환 이름 |
| vendor prefix | `vendor,` 접두어 | `vendor-prefixes.yaml`에 등록된 회사 식별자 |
| `$nodename` | 노드 이름 검사 | 일반 노드 이름 규칙(`serial@...`)을 강제하는 스키마 항목 |
| DT ABI | binding 안정성 규약 | 새 커널은 옛 DTB에서 동작해야 한다 (`ABI.rst`) |
| checkpatch.pl | 패치 스타일 검사기 | 섞인 binding 패치, 문서화 안 된 compatible 경고 |
| get_maintainer.pl | 수신자 찾기 | MAINTAINERS의 파일 패턴으로 리뷰어·리스트 출력 |

---

## 14. 요약 & 체크리스트

binding은 compatible 하나에 대한 계약서다 — DTS 작성자는 필수 속성을 다 쓰고, 드라이버는 적힌 속성만 읽고, 다른 OS도 같은 정의를 쓴다. 자유 텍스트였던 binding은 json-schema(YAML)로 바뀌어 6.18.55 트리에서 YAML 5182개 대 .txt 814개가 됐다. YAML binding은 `$id`/`$schema` → `title`/`maintainers` → `properties` → `required` → `allOf`(`$ref`, `if/then`) → `unevaluatedProperties` → `examples`로 구성된다. `make dt_binding_check`는 메타스키마로 binding을, `dt-validate`로 그 예제를 검사하고, `make CHECK_DTBS=y board.dtb`/`dtbs_check`는 보드 DTB를 모든 binding의 합본으로 검사한다. 경고는 "어느 스키마가 잡았나(`from schema $id`)"를 보고 DTS를 고칠지 binding을 고칠지 정한다. upstream에서는 binding이 별도 패치로 시리즈 맨 앞에 가고, DT maintainers 리뷰와 봇 검사를 거쳐 서브시스템 트리로, DTS는 SoC 트리로 간다. 리뷰 규칙의 핵심은 "HW를 기술하라, 정책·Linux는 넣지 마라, 일반 노드 이름, 구체적 compatible + fallback, binding은 ABI".

- [ ] binding이 무엇을 정하고 무엇을 정하지 않는지(런타임 검사 없음 포함) 설명할 수 있다
- [ ] .txt binding이 YAML로 바뀐 이유를 예(본문·예제 불일치)를 들어 설명할 수 있다
- [ ] YAML binding의 각 블록 역할을 말하고 빈 파일에서 새 binding을 쓸 수 있다
- [ ] `additionalProperties`와 `unevaluatedProperties`의 차이와 언제 무엇을 쓰는지 안다
- [ ] compatible을 enum / items(fallback) / oneOf로 표현하고 순서 규칙을 설명할 수 있다
- [ ] venv에 dtschema를 설치하고 `O=`로 `dt_binding_check DT_SCHEMA_FILES=...`를 돌릴 수 있다
- [ ] `CHECK_DTBS=y`로 보드 DTB 하나를 검사하고 경고를 범주로 묶어 해석할 수 있다
- [ ] `dt-validate` 메시지에서 노드·스키마 출처·날 바이트의 뜻을 읽을 수 있다
- [ ] binding 리뷰의 주요 DO/DON'T(정책 금지, 일반 노드 이름, 단위 접미사 …)를 예로 말할 수 있다
- [ ] binding ABI 규칙과 호환 깨는 변경 시 새 compatible을 쓰는 이유를 설명할 수 있다
- [ ] 패치 순서, 제목 접두어, checkpatch·get_maintainer 사용, 머지 경로를 설명할 수 있다
- [ ] binding 속성 이름이 드라이버의 `devm_regulator_get`·`devm_gpiod_get`·`device_property_*` 인자와 어떻게 이어지는지 말할 수 있다

---

## 참고 자료

- 커널 문서 (소스 트리 `Documentation/devicetree/bindings/`): `writing-bindings.rst`(DOs and DON'Ts), `writing-schema.rst`(스키마 형식·도구), `submitting-patches.rst`(패치 규칙), `ABI.rst`(안정성), `example-schema.yaml`(주석 달린 예제 스키마), `vendor-prefixes.yaml`, `dts-coding-style.rst`
- docs.kernel.org 같은 문서: https://docs.kernel.org/devicetree/bindings/writing-schema.html , https://docs.kernel.org/devicetree/bindings/writing-bindings.html , https://docs.kernel.org/devicetree/bindings/submitting-patches.html
- dt-schema 프로젝트: https://github.com/devicetree-org/dt-schema (core 스키마 `dtschema/schemas/`, `property-units.yaml`)
- Devicetree Specification: https://www.devicetree.org/specifications/
- json-schema: https://json-schema.org/
- 실제 binding 예: `Documentation/devicetree/bindings/iio/imu/invensense,mpu6050.yaml`, `spi/spi-peripheral-props.yaml` — elixir.bootlin.com에서 버전별로 볼 수 있다
- 드라이버 측: `drivers/iio/imu/inv_mpu6050/inv_mpu_core.c`, `inv_mpu_i2c.c`
- 빌드 규칙: `Documentation/devicetree/bindings/Makefile`, `scripts/Makefile.dtbs`, `scripts/checkpatch.pl`(DT compatible 검사)
- Bootlin "Device Tree 101" 등 Bootlin 교육 자료(bootlin.com/docs)
