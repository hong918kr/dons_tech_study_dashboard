# K7. 라이선스와 SBOM — GPL/LGPL 의무, 커널 모듈 라이선스, 소스 공개, SPDX/CycloneDX

> **이 노트를 다 읽으면**: 임베디드 이미지 한 장에 든 컴포넌트(TF-A · U-Boot · 커널 · BusyBox · libc · 앱)의 라이선스와 출하 시 의무를 표로 정리할 수 있다 · "독점 커널 모듈을 실어도 되나?"에 modpost 에러와 taint 플래그를 직접 보여 주며 답할 수 있다 · SPDX 2.3 SBOM을 손으로 쓰고 pyspdxtools · NTIA 체커로 검증하고, CycloneDX SBOM과 VEX를 만들 수 있다 · 회사의 오픈소스 컴플라이언스 파이프라인(OSRB → 빌드 → SBOM → CI 게이트 → 소스 릴리스 → CVE 감시)을 설계할 수 있다
> **왜 필요한가**: "BusyBox를 넣어 출하하면 고객에게 무엇을 줘야 하나?", "독점 드라이버를 커널 모듈로 내도 되나?", "SBOM 만들어 본 적 있나?"는 제품 출시를 해 본 Embedded Linux 엔지니어에게 나오는 질문이고, 실무에서는 출하 직전 법무 리뷰 · 고객사의 SBOM 요구 · EU CRA 대응으로 다시 나온다
> **Don 기준 난이도**: 빌드 산출물 · 버전 관리 · 양산 릴리스 프로세스는 SSD 펌웨어에서 익숙하다 / 새로 배울 부분은 **오픈소스 라이선스가 "릴리스 산출물"에 무엇을 추가로 요구하는가**, 그리고 커널이 그 규칙을 코드로 강제하는 방식(modpost, taint)
> **선행 노트**: E4(커널 모듈 기초), K1(rootfs 구조), K2(BusyBox). 빌드 시스템이 라이선스 정보를 뽑아내는 메커니즘은 K3(Buildroot), K4·K5(Yocto), K6(Debian/L4T)와 짝을 이룬다 — 이 노트는 **라이선스 의무와 SBOM 자체**를 다룬다

> **주의 — 법률 자문이 아니다.** 이 노트는 엔지니어 관점의 정리다. 라이선스 해석과 규제 대응의 최종 판단은 회사 법무 · OSRB(Open Source Review Board)가 한다. 엔지니어의 일은 "무엇이 들어 있고, 어떤 라이선스이고, 소스를 재현할 수 있는가"를 **정확한 데이터로** 법무에게 주는 것이다.

---

## 0. 큰 그림 — 이미지 한 장은 수십 개 라이선스의 묶음이다

SSD 펌웨어를 출하할 때 Don이 챙긴 릴리스 산출물은 대개 "바이너리 + 릴리스 노트 + 버전 태그"였다. 코드는 거의 다 회사 것이고, 서드파티 코드가 있어도 상용 계약으로 들어온 것이었다.

Embedded Linux 장비는 정반대다. 센서 키트를 단 로봇의 이미지를 열어 보면 회사가 쓴 코드는 앱 몇 개뿐이고, 부트로더 · 커널 · init · 셸 · C 라이브러리 · 네트워크 스택 · Python 런타임까지 **대부분이 남이 쓴 오픈소스**다. 그리고 그 각각이 서로 다른 조건(라이선스)으로 "써도 된다"고 허락한 것이다. 조건을 지키지 않고 출하하면 그 허락이 없는 상태로 배포한 것이 된다.

```svg
<svg viewBox="0 0 680 470" xmlns="http://www.w3.org/2000/svg"><defs><marker id="ah1" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto-start-reverse"><path d="M0,0 L10,5 L0,10 z" fill="currentColor"/></marker></defs><text x="165.0" y="22.0" font-size="13" text-anchor="middle" font-weight="bold">이미지 안의 컴포넌트</text><text x="500.0" y="22.0" font-size="13" text-anchor="middle" font-weight="bold">라이선스 → 출하 때 할 일</text><rect x="20" y="36" width="290" height="38" rx="6" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/><text x="165.0" y="59.5" font-size="13" text-anchor="middle">TF-A v2.15 (BL1·BL2·BL31)</text><line x1="312" y1="55" x2="338" y2="55" stroke="currentColor" stroke-width="1.4" marker-end="url(#ah1)"/><rect x="342" y="36" width="318" height="38" rx="6" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/><text x="501.0" y="59.2" font-size="12" text-anchor="middle">BSD-3-Clause → 저작권 고지 포함</text><rect x="20" y="82" width="290" height="38" rx="6" fill="#d0564a" fill-opacity="0.12" stroke="#d0564a"/><text x="165.0" y="105.5" font-size="13" text-anchor="middle">U-Boot v2026.10</text><line x1="312" y1="101" x2="338" y2="101" stroke="currentColor" stroke-width="1.4" marker-end="url(#ah1)"/><rect x="342" y="82" width="318" height="38" rx="6" fill="#d0564a" fill-opacity="0.12" stroke="#d0564a"/><text x="501.0" y="105.2" font-size="12" text-anchor="middle">GPL-2.0-or-later 대부분 → 소스 제공</text><rect x="20" y="128" width="290" height="38" rx="6" fill="#d0564a" fill-opacity="0.12" stroke="#d0564a"/><text x="165.0" y="151.6" font-size="13" text-anchor="middle">Linux 6.18.55 + in-tree 모듈</text><line x1="312" y1="147" x2="338" y2="147" stroke="currentColor" stroke-width="1.4" marker-end="url(#ah1)"/><rect x="342" y="128" width="318" height="38" rx="6" fill="#d0564a" fill-opacity="0.12" stroke="#d0564a"/><text x="501.0" y="151.2" font-size="12" text-anchor="middle">GPL-2.0-only → 소스 + .config + 패치</text><rect x="20" y="174" width="290" height="38" rx="6" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/><text x="165.0" y="197.6" font-size="13" text-anchor="middle">벤더 out-of-tree .ko</text><line x1="312" y1="193" x2="338" y2="193" stroke="currentColor" stroke-width="1.4" marker-end="url(#ah1)"/><rect x="342" y="174" width="318" height="38" rx="6" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/><text x="501.0" y="197.2" font-size="12" text-anchor="middle">회색지대 → GPL-only 심볼 금지, taint</text><rect x="20" y="220" width="290" height="38" rx="6" fill="#888" fill-opacity="0.12" stroke="#888"/><text x="165.0" y="243.6" font-size="13" text-anchor="middle">linux-firmware 블롭</text><line x1="312" y1="239" x2="338" y2="239" stroke="currentColor" stroke-width="1.4" marker-end="url(#ah1)"/><rect x="342" y="220" width="318" height="38" rx="6" fill="#888" fill-opacity="0.12" stroke="#888"/><text x="501.0" y="243.2" font-size="12" text-anchor="middle">파일별 라이선스(WHENCE) → 조건 확인</text><rect x="20" y="266" width="290" height="38" rx="6" fill="#d0564a" fill-opacity="0.12" stroke="#d0564a"/><text x="165.0" y="289.6" font-size="13" text-anchor="middle">BusyBox 1.37 (init, sh, 유틸)</text><line x1="312" y1="285" x2="338" y2="285" stroke="currentColor" stroke-width="1.4" marker-end="url(#ah1)"/><rect x="342" y="266" width="318" height="38" rx="6" fill="#d0564a" fill-opacity="0.12" stroke="#d0564a"/><text x="501.0" y="289.2" font-size="12" text-anchor="middle">GPL-2.0-only → 소스 + 설정 제공</text><rect x="20" y="312" width="290" height="38" rx="6" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/><text x="165.0" y="335.6" font-size="13" text-anchor="middle">glibc 2.43 / musl</text><line x1="312" y1="331" x2="338" y2="331" stroke="currentColor" stroke-width="1.4" marker-end="url(#ah1)"/><rect x="342" y="312" width="318" height="38" rx="6" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/><text x="501.0" y="335.2" font-size="12" text-anchor="middle">LGPL-2.1+ → 교체 가능하게 / musl MIT</text><rect x="20" y="358" width="290" height="38" rx="6" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/><text x="165.0" y="381.6" font-size="13" text-anchor="middle">우리 앱 (센서 퓨전, 프로토콜)</text><line x1="312" y1="377" x2="338" y2="377" stroke="currentColor" stroke-width="1.4" marker-end="url(#ah1)"/><rect x="342" y="358" width="318" height="38" rx="6" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/><text x="501.0" y="381.2" font-size="12" text-anchor="middle">Proprietary → 공개 의무 없음</text><rect x="20" y="404" width="290" height="38" rx="6" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/><text x="165.0" y="427.6" font-size="13" text-anchor="middle">오픈소스 라이브러리 (Python 등)</text><line x1="312" y1="423" x2="338" y2="423" stroke="currentColor" stroke-width="1.4" marker-end="url(#ah1)"/><rect x="342" y="404" width="318" height="38" rx="6" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/><text x="501.0" y="427.2" font-size="12" text-anchor="middle">MIT·BSD·Apache-2.0 → 고지문</text><text x="340.0" y="464.0" font-size="12" text-anchor="middle">빨강 = strong copyleft · 주황 = weak copyleft·회색지대 · 초록 = permissive · 파랑 = 자사 코드</text></svg>
```
그림 1 — 이 노트의 랩 이미지(QEMU virt: TF-A → U-Boot → Linux 6.18.55 → BusyBox initramfs)에 앱과 라이브러리를 얹은 "센서 키트 이미지"의 컴포넌트별 라이선스와 출하 의무. 색은 의무의 강도다.

이 노트의 핵심 문장 다섯 개를 먼저 적어 둔다.

- **의무는 "배포(distribution)"에서 생긴다.** 사내에서 빌드해 쓰기만 하면 GPL의 소스 제공 의무는 거의 생기지 않는다. 장비를 고객에게 **팔거나 넘기는 순간** 펌웨어를 배포한 것이다 (AGPL은 네트워크 사용도 포함 — §1.5).
- **GPL 컴포넌트를 출하하면 "대응 소스 전체(complete corresponding source)"를 줄 수 있어야 한다.** 원본 tarball 링크가 아니라, **우리가 실제로 빌드한 그 버전 + 우리 패치 + `.config` + 빌드 스크립트**다.
- **커널은 독점 모듈을 "허용"하지만 도와주지 않는다.** `MODULE_LICENSE("Proprietary")` 모듈은 `EXPORT_SYMBOL_GPL` 심볼을 쓸 수 없고(빌드 단계에서 에러), 로드하면 커널이 taint된다. 이것이 법적 판단을 대신하지는 않는다.
- **SBOM(Software Bill of Materials)은 이미지의 BOM이다.** 하드웨어 BOM이 부품 번호 · 제조사 · 수량을 적듯, SBOM은 컴포넌트 이름 · 버전 · 공급자 · 식별자(PURL/CPE) · 라이선스 · 관계를 기계가 읽는 형식(SPDX, CycloneDX)으로 적는다.
- **SBOM은 라이선스와 보안 두 가지에 쓰인다.** 라이선스 컴플라이언스(무엇을 공개해야 하나), 그리고 취약점 관리(새 CVE가 나왔을 때 우리 제품 중 어디가 영향받나). 규제(EU CRA 등)가 SBOM을 요구하는 이유는 두 번째다.

### 0.1 실습 환경

모든 명령은 Mac의 Lima VM `elinux`(Ubuntu 26.04, `uname -r` = `7.0.0-38-generic`, aarch64)에서 실행했다. 커널 모듈은 공용 랩의 Linux 6.18.55 빌드 트리(`/var/lab/out/linux-build`)로 빌드하고 QEMU 게스트(`/var/lab/out/Image`, BusyBox initramfs)에서 `insmod`했다. Python 도구는 `/var/tmp/K7/k7_venv`에 pip로 설치했다.

```sh
python3 -m venv /var/tmp/K7/k7_venv
/var/tmp/K7/k7_venv/bin/pip install spdx-tools reuse cyclonedx-bom ntia-conformance-checker ply gitpython
/var/tmp/K7/k7_venv/bin/pip list | grep -i -E "spdx|reuse|cyclone|ntia|ply|git"
```

```text
cyclonedx-bom             7.5.0
cyclonedx-python-lib      11.12.0
gitdb                     4.0.12
GitPython                 3.2.1
ntia_conformance_checker  6.0.0
ply                       3.11
reuse                     6.2.0
spdx-python-model         0.0.6
spdx-tools                0.8.5
```

이 노트에서 **실행하지 않은 것**은 따로 표시한다: Buildroot `make legal-info`, Yocto `create-spdx` · `cve-check`(Don의 "무거운 빌드 금지" 조건 — 메커니즘은 K3 · K4 · K5 참조), linux-firmware의 `WHENCE` 파일(VM에 linux-firmware 패키지가 없다), SPDX 3.0 문서 생성. 규제 관련 날짜는 공개 자료로 확인한 것만 적었다.

---

## 1. 라이선스 계열 — 무엇을 허락하고 무엇을 요구하나

### 1.1 직관 — 라이선스는 "사용 조건이 붙은 허락"이다

저작권법상 남의 코드를 복제 · 수정 · 배포하려면 저작권자의 허락이 필요하다. 오픈소스 라이선스는 "이 조건을 지키면 허락한다"는 문서다. 조건은 크게 두 종류다.

- **고지(attribution)**: "저작권 표시와 라이선스 문구를 지워지지 않게 같이 줘라." — 모든 라이선스에 있다.
- **상호주의(copyleft)**: "이 코드를 (수정해) 배포하면, 받은 사람에게도 같은 조건으로 소스를 줘라." — 어디까지가 "이 코드"인지(경계)가 계열마다 다르다.

```svg
<svg viewBox="0 0 680 300" xmlns="http://www.w3.org/2000/svg"><defs><marker id="ah2" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto-start-reverse"><path d="M0,0 L10,5 L0,10 z" fill="currentColor"/></marker></defs><rect x="15" y="30" width="155" height="210" rx="6" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/><text x="92.5" y="91.2" font-size="12" text-anchor="middle" font-weight="bold">Permissive</text><text x="92.5" y="107.2" font-size="12" text-anchor="middle">MIT · BSD · ISC</text><text x="92.5" y="123.2" font-size="12" text-anchor="middle">Apache-2.0</text><text x="92.5" y="139.2" font-size="12" text-anchor="middle">경계: 없음</text><text x="92.5" y="155.2" font-size="12" text-anchor="middle">의무: 고지문 유지</text><text x="92.5" y="171.2" font-size="12" text-anchor="middle">Apache: NOTICE</text><text x="92.5" y="187.2" font-size="12" text-anchor="middle">+ 특허 라이선스</text><rect x="180" y="30" width="155" height="210" rx="6" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/><text x="257.5" y="91.2" font-size="12" text-anchor="middle" font-weight="bold">Weak copyleft</text><text x="257.5" y="107.2" font-size="12" text-anchor="middle">LGPL-2.1 · 3.0</text><text x="257.5" y="123.2" font-size="12" text-anchor="middle">MPL-2.0</text><text x="257.5" y="139.2" font-size="12" text-anchor="middle">경계: 라이브러리</text><text x="257.5" y="155.2" font-size="12" text-anchor="middle">(MPL: 파일 단위)</text><text x="257.5" y="171.2" font-size="12" text-anchor="middle">의무: 그 부분 소스</text><text x="257.5" y="187.2" font-size="12" text-anchor="middle">+ relink 허용</text><rect x="345" y="30" width="155" height="210" rx="6" fill="#d0564a" fill-opacity="0.12" stroke="#d0564a"/><text x="422.5" y="99.2" font-size="12" text-anchor="middle" font-weight="bold">Strong copyleft</text><text x="422.5" y="115.2" font-size="12" text-anchor="middle">GPL-2.0 · 3.0</text><text x="422.5" y="131.2" font-size="12" text-anchor="middle">경계: 결합 저작물</text><text x="422.5" y="147.2" font-size="12" text-anchor="middle">의무: 전체 소스</text><text x="422.5" y="163.2" font-size="12" text-anchor="middle">+ 빌드 스크립트</text><text x="422.5" y="179.2" font-size="12" text-anchor="middle">v3: 설치 정보</text><rect x="510" y="30" width="155" height="210" rx="6" fill="#d0564a" fill-opacity="0.12" stroke="#d0564a"/><text x="587.5" y="99.2" font-size="12" text-anchor="middle" font-weight="bold">Network copyleft</text><text x="587.5" y="115.2" font-size="12" text-anchor="middle">AGPL-3.0</text><text x="587.5" y="131.2" font-size="12" text-anchor="middle">경계: 네트워크</text><text x="587.5" y="147.2" font-size="12" text-anchor="middle">원격 사용자에게도</text><text x="587.5" y="163.2" font-size="12" text-anchor="middle">의무: 소스 제공</text><text x="587.5" y="179.2" font-size="12" text-anchor="middle">임베디드는 보통 금지</text><text x="340.0" y="20.0" font-size="13" text-anchor="middle" font-weight="bold">의무가 미치는 범위가 넓어지는 순서</text><line x1="30" y1="262" x2="650" y2="262" stroke="currentColor" stroke-width="1.4" marker-end="url(#ah2)"/><text x="340.0" y="285.0" font-size="12" text-anchor="middle">왼쪽 → 오른쪽: 내 코드가 공개 의무에 끌려 들어갈 가능성이 커진다</text></svg>
```
그림 2 — 라이선스 계열 스펙트럼. 오른쪽으로 갈수록 copyleft 의무가 미치는 경계가 넓어진다.

| 계열 | 대표 SPDX ID | copyleft 경계 | 출하 시 핵심 의무 | 임베디드에서 흔한 예 |
|---|---|---|---|---|
| Permissive | `MIT`, `BSD-2-Clause`, `BSD-3-Clause`, `ISC`, `Apache-2.0` | 없음 | 저작권 · 라이선스 고지 유지. Apache-2.0은 NOTICE 파일 전달, 변경 표시 | TF-A, musl, libfdt(BSD 선택 시), Python 라이브러리 다수 |
| Weak copyleft | `LGPL-2.1-or-later`, `LGPL-3.0-only`, `MPL-2.0` | LGPL: 라이브러리<br>MPL: 파일 | 라이브러리(파일)의 소스 + 수정분. LGPL은 사용자가 라이브러리를 바꿔 끼울 수 있게 | glibc, libstdc++(예외 조항 있음), systemd 라이브러리 일부 |
| Strong copyleft | `GPL-2.0-only`, `GPL-2.0-or-later`, `GPL-3.0-or-later` | 결합 저작물 전체 | 대응 소스 전체 + 빌드 스크립트 + 고지 + (제공 방식) | Linux 커널, U-Boot, BusyBox |
| Network copyleft | `AGPL-3.0-only` | 결합 저작물 + 네트워크 사용 | 네트워크로 상호작용하는 사용자에게도 소스 제공 | 서버형 소프트웨어 — 장비 펌웨어에는 보통 금지 목록 |

### 1.2 Permissive — MIT · BSD · Apache-2.0

MIT와 BSD는 사실상 "고지만 남겨라"다. BSD-3-Clause는 여기에 "우리 이름으로 홍보하지 마라" 조항이 붙는다. **바이너리로 배포할 때도 고지는 문서나 화면 등 어딘가에 실어야 한다** — 이 부분을 놓치는 회사가 많다. BSD-3-Clause 조항 2가 바로 그것이다. 랩의 TF-A 소스에서 직접 확인한다.

```sh
sed -n 1,20p /var/lab/src/tfa/docs/license.rst
```

```text
License
=======

The software is provided under a BSD-3-Clause license (below). Contributions to
this project are accepted under the same license with developer sign-off as
described in the :ref:`Contributor's Guide`.

::

    Copyright (c) [XXXX-]YYYY, <OWNER>. All rights reserved.

    Redistribution and use in source and binary forms, with or without modification,
    are permitted provided that the following conditions are met:

    -  Redistributions of source code must retain the above copyright notice, this
    list of conditions and the following disclaimer.

    -  Redistributions in binary form must reproduce the above copyright notice,
    this list of conditions and the following disclaimer in the documentation
    and/or other materials provided with the distribution.
```

출력에서 볼 것: "Redistributions in binary form must reproduce ... in the documentation and/or other materials" — TF-A가 플래시에 들어가 출하되면, 소스 공개 의무는 없지만 **이 고지를 제품 문서나 설정 화면의 "오픈소스 라이선스" 페이지에 넣어야 한다**.

Apache-2.0은 permissive지만 두 가지가 더 있다.

- **특허 라이선스(3조)**: 기여자가 자기 기여분에 걸린 특허 사용을 허락한다. 대신 그 코드에 대해 특허 소송을 걸면 허락이 종료된다(patent retaliation). 회사 법무가 Apache-2.0을 MIT보다 선호하는 이유다.
- **NOTICE 파일(4조 d항)**: 원본에 `NOTICE` 파일이 있으면 배포물에 그 내용을 함께 실어야 한다. 바이너리만 출하해도 마찬가지다. 그리고 수정한 파일에는 수정했다는 표시를 해야 한다(4조 b항).

TF-A는 자체 코드가 BSD-3-Clause이지만 다른 프로젝트 코드를 가져와 쓴다. 같은 `docs/license.rst`의 뒷부분이 그 목록이다(발췌).

```text
-  The libfdt source code is disjunctively dual licensed
   (GPL-2.0+ OR BSD-2-Clause). It is used by this project under the terms of
   the BSD-2-Clause license. Any contributions to this code must be made under
   the terms of both licenses.
...
-  The zlib source code is licensed under the Zlib license, which is a
   permissive license compatible with BSD-3-Clause.
```

출력에서 볼 것: `GPL-2.0+ OR BSD-2-Clause` 같은 **dual license(OR)** 는 받는 쪽이 하나를 고를 수 있다는 뜻이다. TF-A는 BSD 쪽을 골랐다. SPDX 식에서 `OR`는 선택, `AND`는 둘 다 지켜야 함, `WITH`는 예외 조항 추가다.

### 1.3 GPL — 2.0-only vs 2.0-or-later vs 3.0

GPL의 핵심은 "이 프로그램 또는 이것을 기반으로 한 저작물(work based on the Program)을 바이너리로 배포하면 대응 소스를 제공하라"다. 엔지니어가 구분해야 할 버전 차이는 세 가지다.

| 항목 | GPL-2.0-only | GPL-2.0-or-later | GPL-3.0 |
|---|---|---|---|
| 의미 | 2판 조건으로만 | 2판 또는 이후 판 중 받는 쪽이 선택 | 3판 조건 |
| 대표 | Linux 커널(전체 COPYING), BusyBox | U-Boot 대부분, 커널 파일 일부 | bash 4+, GCC 본체, coreutils 등 GNU 도구 |
| 소스 제공 방식 | 3조: 소스 동봉, 또는 3년 유효한 서면 제안(written offer) | 2.0과 동일 | 6조: 동봉, 서면 제안(3년 이상 · 예비 부품 지원 기간 중 긴 쪽), 네트워크 서버 제공 등 |
| 설치 정보 | 요구 없음 | 요구 없음(2판 선택 시) | **"User Product"면 Installation Information 제공** |
| 특허 | 명시 조항 없음 | — | 명시적 특허 라이선스 |

**GPLv3의 Installation Information (anti-tivoization)** — 이게 임베디드 회사들이 GPLv3를 피하는 가장 큰 이유다. GPLv3 6조는 "User Product"(소비자용 제품 등)에 GPLv3 코드를 넣어 배포하면, 사용자가 **수정한 버전을 그 기기에 설치해 돌릴 수 있게 하는 데 필요한 정보**(서명 키, 인증 절차 등)까지 주라고 요구한다. 이름은 TiVo가 GPL 소프트웨어를 쓰면서 서명된 이미지만 부팅되게 막은 사례에서 왔다.

Don이 SSD에서 다뤘던 secure boot를 떠올리면 충돌이 바로 보인다. 로봇이나 카메라는 서명된 이미지만 부팅하도록 잠그는 게 보안 요구사항이다(L 모듈의 secure boot 노트). 그 이미지 안에 GPLv3 프로그램이 있으면 "사용자가 수정한 그 프로그램을 설치할 방법"을 줘야 한다. 그래서 많은 회사가 **장비에 들어가는 유저스페이스에서 GPLv3 · LGPLv3를 금지 목록에 넣는다** — BusyBox ash를 bash 대신 쓰는 이유 중 하나다. (GPLv3도 ROM처럼 아무도 — 제조사조차 — 수정할 수 없는 경우는 예외로 둔다. 이 판단은 법무 몫이다.)

> GPL-2.0과 GPL-3.0은 서로 호환되지 않는다. GPL-2.0-only 코드와 GPL-3.0 코드를 한 프로그램으로 결합해 배포할 수 없다. GPL-2.0-or-later 코드는 3판을 선택하면 GPL-3.0 코드와 결합할 수 있다.

### 1.4 LGPL-2.1 — 동적 링크와 relink 의무

LGPL은 "라이브러리는 copyleft, 그걸 쓰는 앱은 아니어도 된다"는 절충이다. 단 조건이 있다. LGPL-2.1 6조의 핵심은 **사용자가 라이브러리를 수정된 버전으로 바꿔서 앱과 다시 결합할 수 있어야 한다**는 것이다.

- **동적 링크(shared library)**: 앱이 실행 시 기기의 `libc.so.6`을 쓴다. 사용자는 `.so`를 바꿔 끼우면 된다. 의무는 LGPL 라이브러리의 소스(+ 우리 수정분) 제공, 고지, 그리고 역공학 금지 조항을 넣지 않는 것.
- **정적 링크(static)**: 라이브러리 코드가 앱 바이너리 안에 박힌다. 사용자가 바꿔 끼우려면 **앱의 오브젝트 파일(.o)이나 소스**가 있어야 다시 링크할 수 있다. 그래서 독점 앱을 glibc에 정적 링크해 출하하면 앱의 오브젝트 파일을 제공해야 하는 상황이 된다.

직접 확인한다. 같은 앱을 glibc 동적, glibc 정적, musl 정적으로 빌드하고 무엇이 바이너리에 들어갔는지 본다.

```c
// k7_app.c: toy "sensor app" used to compare glibc dynamic, glibc static, musl static
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <gnu/libc-version.h>

int main(int argc, char **argv)
{
	const char *who = argc > 1 ? argv[1] : "imu0";
	char buf[64];

	if (snprintf(buf, sizeof(buf), "sensor=%s ok", who) < 0) {
		perror("snprintf");
		exit(1);
	}
	printf("%s (len=%zu)\n", buf, strlen(buf));
#ifdef __GLIBC__
	printf("libc: glibc %s\n", gnu_get_libc_version());
#endif
	return 0;
}
```

musl 버전(`k7_app_musl.c`)은 `gnu/libc-version.h` 없이 같은 문자열만 찍는 10줄짜리다.

```sh
gcc -Wall -Wextra -O2 k7_app.c -o k7_app_dyn
gcc -Wall -Wextra -O2 -static k7_app.c -o k7_app_static
musl-gcc -Wall -Wextra -O2 -static k7_app_musl.c -o k7_app_musl
./k7_app_dyn && ./k7_app_static && ./k7_app_musl
ls -l k7_app_*
for f in k7_app_*; do echo "== $f"; file -b $f | cut -c1-80; readelf -d $f | grep NEEDED; done
```

```text
sensor=imu0 ok (len=14)
libc: glibc 2.43
sensor=imu0 ok (len=14)
libc: glibc 2.43
sensor=imu0 ok (len=14), libc: musl
70792 k7_app_dyn
80256 k7_app_musl
782104 k7_app_static
== k7_app_dyn
ELF 64-bit LSB pie executable, ARM aarch64, version 1 (SYSV), dynamically linked
 0x0000000000000001 (NEEDED)             Shared library: [libc.so.6]
 0x0000000000000001 (NEEDED)             Shared library: [ld-linux-aarch64.so.1]
== k7_app_musl
ELF 64-bit LSB executable, ARM aarch64, version 1 (SYSV), statically linked, wit
   (no NEEDED: libc code is inside the binary)
== k7_app_static
ELF 64-bit LSB executable, ARM aarch64, version 1 (GNU/Linux), statically linked
   (no NEEDED: libc code is inside the binary)
```

`ls` 출력은 `awk`로 크기와 이름만 남겼고, NEEDED가 없을 때는 스크립트가 괄호 문구를 찍게 했다. 바이너리 안에 정의된 함수 수와 glibc 배너 문자열도 본다.

```sh
for f in k7_app_dyn k7_app_static k7_app_musl; do
  printf "%-15s defined funcs=%5d\n" $f $(nm --defined-only $f | grep -c " [Tt] ")
done
strings k7_app_static | grep -m1 "GNU C Library"
```

```text
k7_app_dyn      defined funcs=    9
k7_app_static   defined funcs= 1069
k7_app_musl     defined funcs=   77
GNU C Library (Ubuntu GLIBC 2.43-2ubuntu2.4) stable release version 2.43.
```

출력에서 볼 것: 동적 버전은 `NEEDED libc.so.6` — glibc는 별도 파일로 기기에 있고 사용자가 바꿔 끼울 수 있다. 정적 glibc 버전은 함수 1069개가 바이너리 안에 있다 — **LGPL 코드가 우리 앱 파일 안에 들어갔다**. 이 상태로 독점 앱을 출하하면 relink 가능한 형태(오브젝트 파일 등)를 제공해야 한다. musl은 MIT라서 정적으로 넣어도 고지만 하면 된다. 그래서 정적 바이너리가 필요한 임베디드 도구(initramfs의 복구 도구 등)는 musl을 쓰는 경우가 많다.

Ubuntu가 각 패키지에 싣는 DEP-5 형식 저작권 파일에서도 같은 정보를 읽을 수 있다.

```sh
sed -n 1,6p /usr/share/doc/libc6/copyright; sed -n 5,8p /usr/share/doc/musl/copyright
```

```text
Format: https://www.debian.org/doc/packaging-manuals/copyright-format/1.0/
Upstream-Name: GNU C Library
Source: https://sourceware.org/git/glibc.git

Files: *
Copyright: 1991-2026 Free Software Foundation, Inc.
Files: *
Copyright: 2005-2020 Rich Felker <dalias@aerifal.cx>
           2005-2020 Many other contributors
License: MIT
```

(glibc 파일의 6번째 줄 다음이 `License: LGPL-2.1+`다 — 위 명령의 범위 밖이라 잘렸다. 첫 실행 결과에서 확인했다.) Debian/Ubuntu 기반 이미지(Jetson L4T 등)에서는 이 `copyright` 파일들이 라이선스 정보의 1차 출처다 — K6 참조.

### 1.5 MPL-2.0과 AGPL — 짧게

- **MPL-2.0**: copyleft 경계가 **파일**이다. MPL 파일을 수정하면 그 파일의 소스를 공개하고, 같은 프로그램의 다른 파일(우리 독점 코드)은 공개 의무가 없다. 임베디드에서는 일부 라이브러리에서 만난다.
- **AGPL-3.0**: GPL-3.0 + "네트워크로 이 프로그램과 상호작용하는 사용자에게도 소스를 제공하라". 장비가 웹 관리 화면을 띄우는데 그 서버가 AGPL이면 출하 여부와 무관하게 의무가 생길 수 있다. 대부분의 회사가 deny 목록에 둔다.

### 1.6 흔한 함정

- "오픈소스니까 공짜로 마음대로 써도 된다" — 사용료가 없을 뿐 조건이 있다.
- "수정 안 했으니 소스 안 줘도 된다" — GPL은 **수정 여부와 무관하게** 바이너리를 배포하면 소스 제공 의무가 있다. 수정 안 했으면 upstream 소스를 그대로 주면 될 뿐이다.
- "GitHub 링크를 주면 된다" — 그 링크가 우리가 빌드한 정확한 버전, 우리 패치, 설정을 담고 3년간 유지돼야 한다. upstream 링크는 대개 이를 만족하지 못한다.
- `GPL-2.0`이라고만 적힌 SPDX ID — 예전 SPDX 목록에서 `GPL-2.0`은 "only"를 뜻했고 지금은 deprecated다. 새로 쓸 때는 `GPL-2.0-only` / `GPL-2.0-or-later`로 명확히 쓴다. 커널은 예전 ID도 유효하다고 명시한다(§3.5).

---

## 2. 출하할 때 무엇을 줘야 하나 — 소스 제공 의무

먼저 전체 판단 흐름을 그림으로 본다. 아래 절들은 이 트리의 각 가지를 실제 파일과 명령으로 확인하는 것이다.

```svg
<svg viewBox="0 0 680 560" xmlns="http://www.w3.org/2000/svg"><defs><marker id="ah3" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto-start-reverse"><path d="M0,0 L10,5 L0,10 z" fill="currentColor"/></marker></defs><rect x="190" y="10" width="300" height="40" rx="6" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/><text x="340.0" y="34.5" font-size="13" text-anchor="middle">이 컴포넌트를 장비에 넣어 출하하나?</text><rect x="20" y="80" width="200" height="50" rx="6" fill="#888" fill-opacity="0.12" stroke="#888"/><text x="120.0" y="101.2" font-size="12" text-anchor="middle">아니오 (사내 도구만)</text><text x="120.0" y="117.2" font-size="12" text-anchor="middle">배포 아님 → 의무 거의 없음</text><line x1="250" y1="50" x2="140" y2="78" stroke="currentColor" stroke-width="1.4" marker-end="url(#ah3)"/><text x="180.0" y="66.0" font-size="12" text-anchor="middle">No</text><line x1="400" y1="50" x2="470" y2="78" stroke="currentColor" stroke-width="1.4" marker-end="url(#ah3)"/><text x="452.0" y="66.0" font-size="12" text-anchor="middle">Yes</text><rect x="360" y="80" width="230" height="40" rx="6" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/><text x="475.0" y="104.5" font-size="13" text-anchor="middle">SPDX ID로 계열 판별</text><rect x="20" y="170" width="180" height="60" rx="6" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/><text x="110.0" y="188.2" font-size="12" text-anchor="middle">Permissive</text><text x="110.0" y="204.2" font-size="12" text-anchor="middle">고지문·NOTICE를</text><text x="110.0" y="220.2" font-size="12" text-anchor="middle">문서/UI에 포함</text><rect x="220" y="170" width="200" height="60" rx="6" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/><text x="320.0" y="196.2" font-size="12" text-anchor="middle">LGPL · MPL</text><text x="320.0" y="212.2" font-size="12" text-anchor="middle">LGPL: 정적 링크인가?</text><rect x="440" y="170" width="220" height="60" rx="6" fill="#d0564a" fill-opacity="0.12" stroke="#d0564a"/><text x="550.0" y="196.2" font-size="12" text-anchor="middle">GPL-2.0 / GPL-3.0</text><text x="550.0" y="212.2" font-size="12" text-anchor="middle">소스 공개 대상</text><line x1="400" y1="122" x2="110" y2="168" stroke="currentColor" stroke-width="1.4" marker-end="url(#ah3)"/><line x1="450" y1="122" x2="320" y2="168" stroke="currentColor" stroke-width="1.4" marker-end="url(#ah3)"/><line x1="520" y1="122" x2="550" y2="168" stroke="currentColor" stroke-width="1.4" marker-end="url(#ah3)"/><rect x="220" y="270" width="200" height="80" rx="6" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/><text x="320.0" y="290.2" font-size="12" text-anchor="middle">예 (static)</text><text x="320.0" y="306.2" font-size="12" text-anchor="middle">내 앱 오브젝트 파일도</text><text x="320.0" y="322.2" font-size="12" text-anchor="middle">제공 → 사용자가</text><text x="320.0" y="338.2" font-size="12" text-anchor="middle">새 LGPL로 relink</text><rect x="220" y="370" width="200" height="70" rx="6" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/><text x="320.0" y="393.2" font-size="12" text-anchor="middle">아니오 (shared)</text><text x="320.0" y="409.2" font-size="12" text-anchor="middle">LGPL 부분 소스 +</text><text x="320.0" y="425.2" font-size="12" text-anchor="middle">.so 교체 허용</text><line x1="320" y1="232" x2="320" y2="268" stroke="currentColor" stroke-width="1.4" marker-end="url(#ah3)"/><path d="M410,232 L410,252 L430,252 L430,405 L422,405" fill="none" stroke="currentColor" stroke-width="1.4" marker-end="url(#ah3)"/><path d="M235,232 L235,252 L110,252 L110,268" fill="none" stroke="currentColor" stroke-width="1.4" marker-end="url(#ah3)"/><rect x="440" y="270" width="220" height="100" rx="6" fill="#d0564a" fill-opacity="0.12" stroke="#d0564a"/><text x="550.0" y="292.2" font-size="12" text-anchor="middle">수정 여부와 무관하게</text><text x="550.0" y="308.2" font-size="12" text-anchor="middle">대응 소스 전체</text><text x="550.0" y="324.2" font-size="12" text-anchor="middle">(패치 · .config ·</text><text x="550.0" y="340.2" font-size="12" text-anchor="middle">빌드 스크립트) + 고지</text><text x="550.0" y="356.2" font-size="12" text-anchor="middle">+ 서면 제안 3년 or 동봉</text><line x1="550" y1="232" x2="550" y2="268" stroke="currentColor" stroke-width="1.4" marker-end="url(#ah3)"/><rect x="440" y="400" width="220" height="90" rx="6" fill="#d0564a" fill-opacity="0.12" stroke="#d0564a"/><text x="550.0" y="425.2" font-size="12" text-anchor="middle">GPLv3면 추가로</text><text x="550.0" y="441.2" font-size="12" text-anchor="middle">Installation Information</text><text x="550.0" y="457.2" font-size="12" text-anchor="middle">(서명 키 or 해제 방법)</text><text x="550.0" y="473.2" font-size="12" text-anchor="middle">→ secure boot와 충돌</text><line x1="550" y1="372" x2="550" y2="398" stroke="currentColor" stroke-width="1.4" marker-end="url(#ah3)"/><rect x="20" y="270" width="180" height="70" rx="6" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/><text x="110.0" y="293.2" font-size="12" text-anchor="middle">MPL-2.0</text><text x="110.0" y="309.2" font-size="12" text-anchor="middle">수정한 MPL 파일만</text><text x="110.0" y="325.2" font-size="12" text-anchor="middle">소스 공개</text><rect x="20" y="500" width="640" height="46" rx="6" fill="#888" fill-opacity="0.12" stroke="#888"/><text x="340.0" y="527.2" font-size="12" text-anchor="middle">어느 쪽이든: SBOM에 기록 → 라이선스 텍스트 모음 → 소스 릴리스 패키지 → 법무(OSRB) 확인</text><line x1="110" y1="342" x2="110" y2="498" stroke="currentColor" stroke-width="1.4" marker-end="url(#ah3)"/><line x1="320" y1="442" x2="320" y2="498" stroke="currentColor" stroke-width="1.4" marker-end="url(#ah3)"/><line x1="550" y1="492" x2="550" y2="498" stroke="currentColor" stroke-width="1.4" marker-end="url(#ah3)"/></svg>
```
그림 3 — 출하 의무 판단 트리(엔지니어링 관점의 단순화). LGPL은 링크 방식에 따라, GPL은 버전(v3의 설치 정보)에 따라 할 일이 달라진다. 어느 가지든 끝은 SBOM · 라이선스 텍스트 · 소스 릴리스 패키지다.

### 2.1 "대응 소스 전체"의 정의

GPL-2.0 3조는 바이너리를 배포할 때 소스를 주는 방법을 정하고, "complete source code"를 이렇게 정의한다: 실행 파일에 대해 **그것이 포함한 모든 모듈의 소스 코드 + 관련 인터페이스 정의 파일 + 컴파일과 설치를 제어하는 스크립트**. 임베디드 기기로 번역하면 이렇다.

| 컴포넌트 | 소스 릴리스에 넣을 것 | 빠뜨리기 쉬운 것 |
|---|---|---|
| Linux 커널 | 출하 버전의 전체 커널 소스 + 우리 패치(BSP 벤더 패치 포함) | `.config`, 우리 DT(`.dts`), out-of-tree **GPL** 모듈 소스 |
| U-Boot | 소스 + 패치 + `defconfig` | 보드 파일, 환경 변수 기본값을 바꾼 패치 |
| BusyBox | 소스 + `.config` | applet 선택 설정 — 설정이 다르면 다른 프로그램이다 |
| glibc 등 LGPL | 소스 + 패치 | 정적 링크 시 앱 오브젝트(§1.4) |
| 빌드 방법 | 빌드 스크립트, 툴체인 버전 정보 | Yocto 레이어 · Buildroot 설정 · `Makefile` 래퍼 |

**`.config`가 왜 소스인가?** 같은 커널 소스라도 `.config`가 다르면 다른 바이너리가 나온다. 랩 커널은 `CONFIG_IKCONFIG`가 켜져 있어서 바이너리 안에 설정이 들어 있다. 실제로 뽑아서 빌드 트리의 `.config`와 비교해 본다.

```sh
cd /var/lab/src/linux-6.18.55
sh scripts/extract-ikconfig /var/lab/out/Image > /tmp/K7/k7_extracted.config
wc -l /tmp/K7/k7_extracted.config; head -5 /tmp/K7/k7_extracted.config
diff <(grep -v "^#" /tmp/K7/k7_extracted.config | grep .) \
     <(grep -v "^#" /var/lab/out/linux-build/.config | grep .) && echo "identical to build tree .config (non-comment lines)"
grep -E "CONFIG_MODULE_SIG=|CONFIG_IKCONFIG" /tmp/K7/k7_extracted.config
```

```text
11748 /tmp/K7/k7_extracted.config
#
# Automatically generated file; DO NOT EDIT.
# Linux/arm64 6.18.55 Kernel Configuration
#
CONFIG_CC_VERSION_TEXT="gcc (Ubuntu 15.2.0-16ubuntu1) 15.2.0"
identical to build tree .config (non-comment lines)
CONFIG_IKCONFIG=y
CONFIG_IKCONFIG_PROC=y
```

출력에서 볼 것: 바이너리에서 꺼낸 설정이 빌드 트리의 `.config`와 같다. 소스 릴리스에는 이 파일이 들어가야 하고, 툴체인 버전(`CONFIG_CC_VERSION_TEXT`)도 함께 적어 두면 재현성이 좋아진다. `CONFIG_MODULE_SIG`는 꺼져 있다(그래서 §4의 바이너리 수정 실험이 가능했다).

### 2.2 제공 방법 — 동봉 vs 서면 제안

GPL-2.0 3조는 세 가지 중 하나를 고르라고 한다.

1. **(a) 동봉**: 바이너리와 함께 기계가 읽는 소스를 준다(같은 매체, 또는 같은 다운로드 페이지에서 동시에).
2. **(b) 서면 제안(written offer)**: "요청하면 소스를 실비로 주겠다"는 서면 약속을 **최소 3년** 유효하게 동봉한다. 제3자 누구에게나 유효해야 한다.
3. **(c) 받은 제안 전달**: 비상업적 재배포에서만 쓸 수 있다. 장비 제조사는 해당 없음.

실무에서는 대개 "제품 문서/설정 화면에 고지 + 서면 제안 문구, 그리고 회사 오픈소스 포털에 펌웨어 버전별 소스 tarball 게시"를 같이 한다. 서면 제안만 하고 요청이 오면 그때 소스를 만들려고 하면, 3년 전 빌드를 재현할 수 없어 곤란해진다. **출하 시점에 소스 릴리스 패키지를 만들어 아카이브**하는 게 정석이다.

### 2.3 라이선스 텍스트와 고지

- 이미지에 포함된 모든 컴포넌트의 라이선스 원문과 저작권 표시를 모아 **사용자가 볼 수 있는 곳**에 둔다: 매뉴얼 부록, 웹 UI의 "Open Source Licenses" 페이지, 기기 안 `/usr/share/licenses/` 같은 디렉터리.
- 빌드 시스템이 대부분 모아 준다(§6): Buildroot `legal-info/licenses/`, Yocto `tmp/deploy/licenses/`.
- 화면이 없는 센서 장비라면 문서 부록 + 웹 포털이 일반적이다.

### 2.4 BusyBox를 출하하면 무엇을 줘야 하나 — 실제 파일로

```sh
cd /var/lab/src/busybox-1.37.0
head -12 LICENSE
grep -rl "Licensed under GPLv2" . --include=*.c | wc -l
```

```text
--- A note on GPL versions

BusyBox is distributed under version 2 of the General Public License (included
in its entirety, below).  Version 2 is the only version of this license which
this version of BusyBox (or modified versions derived from this one) may be
distributed under.

------------------------------------------------------------------------
		    GNU GENERAL PUBLIC LICENSE
		       Version 2, June 1991

 Copyright (C) 1989, 1991 Free Software Foundation, Inc.
550
```

출력에서 볼 것: BusyBox는 **GPL-2.0-only**("Version 2 is the only version")이고, 소스 파일 550개가 SPDX 태그 대신 "Licensed under GPLv2" 문장으로 표시한다(§3.4 차트에서 BusyBox가 "태그 없음"으로 나오는 이유). 그러므로 BusyBox를 넣어 출하하면: (1) 우리가 쓴 BusyBox 1.37.0 소스 + 우리 패치 + **우리 `.config`**, (2) GPL-2.0 원문과 저작권 고지, (3) 동봉 또는 3년 서면 제안. BusyBox는 과거 실제 GPL 집행 소송이 여러 번 있었던 프로젝트라 법무가 특히 꼼꼼히 본다.

U-Boot는 최상위 `Licenses/README`에서 조건을 설명한다(발췌).

```sh
sed -n 1,9p /var/lab/src/u-boot/Licenses/README; ls /var/lab/src/u-boot/Licenses | head -8
```

```text
SPDX-License-Identifier: GPL-2.0

  U-Boot is Free Software.  It is copyrighted by Wolfgang Denk and
many others who contributed code (see the actual source code and the
git commit messages for details).  You can redistribute U-Boot and/or
modify it under the terms of version 2 of the GNU General Public
License as published by the Free Software Foundation.  Most of it can
also be distributed, at your option, under any later version of the
GNU General Public License -- see individual files for exceptions.
Exceptions
OFL.txt
README
bsd-2-clause.txt
bsd-3-clause.txt
bzip2-1.0.6.txt
eCos-2.0.txt
gpl-2.0.txt
```

출력에서 볼 것: U-Boot 전체는 GPL-2.0, "대부분"은 or-later다. 같은 README는 U-Boot의 jump table을 쓰는 standalone 애플리케이션은 파생물로 보지 않는다는 예외(`Licenses/Exceptions`)도 설명한다. 보드 bring-up 때 U-Boot에 넣은 보드 파일 · DT · 드라이버 수정은 출하 시 공개 대상이다.

### 2.5 펌웨어 블롭 — linux-firmware와 WHENCE (실행 안 함)

WiFi · Bluetooth · GPU · VPU 같은 장치는 커널 드라이버가 `request_firmware()`로 `/lib/firmware/`의 바이너리 블롭을 장치에 올린다. 이 블롭은 커널 일부가 아니라 **별도 저작물**이고 각자 라이선스가 있다. upstream `linux-firmware` 저장소의 `WHENCE` 파일이 블롭마다 "어느 드라이버용인지, 라이선스가 무엇인지(재배포 허용 여부)"를 적어 둔다 — 대부분 "재배포는 허용, 수정 · 역공학은 금지" 형태의 벤더 라이선스이고, 각 `LICENCE.<vendor>` 파일이 따로 있다. (VM에 linux-firmware 패키지가 설치돼 있지 않아 이 절은 직접 확인하지 못했다. 위 설명은 저장소 구조에 대한 일반 지식이다.)

실무 체크: 이미지에 넣는 블롭 목록을 SBOM에 넣고, 각 블롭의 라이선스가 재배포를 허용하는지 확인하고, 해당 `LICENCE.*` 파일을 고지 모음에 포함한다. SoC 벤더 BSP에서 받은 블롭은 NDA 계약 조건이 우선한다.

---

## 3. 커널 소스의 라이선스 표기 — SPDX 태그

### 3.1 직관

커널 소스 파일 7만 개 각각의 맨 위에 GPL 전문을 붙이는 대신, 한 줄 태그를 붙인다.

```c
// SPDX-License-Identifier: GPL-2.0-only
```

**SPDX License Identifier**는 Linux Foundation의 SPDX 프로젝트가 관리하는 라이선스 표준 이름 목록(`MIT`, `GPL-2.0-only` …)이고, 태그는 그 ID로 만든 식(expression)이다. 기계가 읽을 수 있어서 SBOM 도구 · 스캐너가 파일 단위로 라이선스를 판정할 수 있다.

### 3.2 커널 전체의 라이선스 — COPYING과 LICENSES/

```sh
cd /var/lab/src/linux-6.18.55
head -18 COPYING
ls LICENSES/*
```

```text
The Linux Kernel is provided under:

	SPDX-License-Identifier: GPL-2.0 WITH Linux-syscall-note

Being under the terms of the GNU General Public License version 2 only,
according with:

	LICENSES/preferred/GPL-2.0

With an explicit syscall exception, as stated at:

	LICENSES/exceptions/Linux-syscall-note

In addition, other licenses may also apply. Please see:

	Documentation/process/license-rules.rst

for more details.
LICENSES/deprecated:
0BSD
CC0-1.0
GFDL-1.1
GFDL-1.2
GPL-1.0
ISC
Linux-OpenIB
X11
Zlib

LICENSES/dual:
Apache-2.0
CC-BY-4.0
CDDL-1.0
MPL-1.1
copyleft-next-0.3.1

LICENSES/exceptions:
GCC-exception-2.0
Linux-syscall-note

LICENSES/preferred:
BSD-2-Clause
BSD-3-Clause
BSD-3-Clause-Clear
GPL-2.0
LGPL-2.0
LGPL-2.1
MIT
```

출력에서 볼 것: 커널은 **GPL-2.0-only + Linux-syscall-note**다. `LICENSES/`는 커널 안에서 쓸 수 있는 라이선스의 목록이자 원문 저장소다. `preferred/`는 일반적으로 쓰는 것, `dual/`은 다른 라이선스와 `OR`로만 쓸 수 있는 것, `deprecated/`는 기존 코드에만 남은 것이다.

### 3.3 Linux-syscall-note — 유저스페이스가 GPL에 끌려가지 않는 이유

```sh
sed -n 1,10p LICENSES/exceptions/Linux-syscall-note
```

```text
SPDX-Exception-Identifier: Linux-syscall-note
SPDX-URL: https://spdx.org/licenses/Linux-syscall-note.html
SPDX-Licenses: GPL-2.0, GPL-2.0+, GPL-1.0+, LGPL-2.0, LGPL-2.0+, LGPL-2.1, LGPL-2.1+, GPL-2.0-only, GPL-2.0-or-later
Usage-Guide:
  This exception is used together with one of the above SPDX-Licenses
  to mark user space API (uapi) header files so they can be included
  into non GPL compliant user space application code.
  To use this exception add it with the keyword WITH to one of the
  identifiers in the SPDX-Licenses tag:
    SPDX-License-Identifier: <SPDX-License> WITH Linux-syscall-note
```

이 예외가 "syscall로 커널 서비스를 쓰는 유저 프로그램은 커널의 파생물이 아니다"라는 원칙을 명시한다. 그리고 uapi 헤더(유저 프로그램이 `#include <linux/ioctl.h>` 하는 것)에 이 태그가 붙는다. 실제로 얼마나 붙어 있나 세어 본다.

```sh
U=$(find include/uapi arch/*/include/uapi -name "*.h" | wc -l)
N=$(find include/uapi arch/*/include/uapi -name "*.h" -exec grep -l "Linux-syscall-note" {} + | wc -l)
echo "uapi headers=$U with syscall-note=$N"; head -1 include/uapi/linux/ioctl.h
```

```text
uapi headers=1419 with syscall-note=1327
/* SPDX-License-Identifier: GPL-2.0 WITH Linux-syscall-note */
```

출력에서 볼 것: uapi 헤더 1419개 중 1327개가 syscall-note를 달고 있다(나머지는 대개 `OR MIT`/`BSD` 같은 dual이라 따로 필요 없다). Don이 A 모듈에서 쓴 `ioctl()` 기반 유저 앱이 독점이어도 되는 근거가 이것이다. 반대로 **커널 내부 헤더(`include/linux/`)를 쓰는 커널 모듈에는 이 예외가 적용되지 않는다** — §4의 주제다.

### 3.4 6.18 트리의 SPDX 태그 통계 (직접 측정)

무엇을 확인하는 코드인지: 소스 트리를 돌면서 각 파일의 첫 6줄에서 `SPDX-License-Identifier`를 찾아 식별자별로 센다.

```python
#!/usr/bin/env python3
# k7: count SPDX-License-Identifier expressions in a source tree (first 6 lines only)
import os, sys, re, collections
root = sys.argv[1]
tag = re.compile(r'SPDX-License-Identifier:\s*(.+?)\s*(\*/|-->|$)')
cnt = collections.Counter(); total = tagged = 0
exts = ('.c', '.h', '.S', '.rs', '.dts', '.dtsi', '.py', '.sh')
names = ('Makefile', 'Kconfig', 'Kbuild')
for d, dirs, files in os.walk(root):
    dirs[:] = [x for x in dirs if x != '.git']
    for f in files:
        if not (f.endswith(exts) or f.startswith(names)):
            continue
        total += 1
        try:
            with open(os.path.join(d, f), errors='replace') as fp:
                head = [next(fp, '') for _ in range(6)]
        except OSError:
            continue
        for line in head:
            m = tag.search(line)
            if m:
                tagged += 1
                cnt[m.group(1).strip().strip('()')] += 1
                break
print(f'files scanned={total} tagged={tagged} untagged={total-tagged}')
for lic, n in cnt.most_common(int(sys.argv[2]) if len(sys.argv) > 2 else 15):
    print(f'{n:7d}  {lic}')
```

```sh
python3 ~/.elinux_scratch/K7/k7_spdx_stats.py /var/lab/src/linux-6.18.55 15
```

```text
files scanned=76367 tagged=67160 untagged=9207
  26080  GPL-2.0
  17734  GPL-2.0-only
   8779  GPL-2.0-or-later
   3186  GPL-2.0+
   2148  MIT
   1470  GPL-2.0+ OR MIT
   1116  GPL-2.0 WITH Linux-syscall-note
    809  GPL-2.0 OR MIT
    784  GPL-2.0 OR BSD-3-Clause
    621  GPL-2.0-only OR BSD-2-Clause
    506  GPL-2.0 OR Linux-OpenIB
    435  ISC
    371  GPL-2.0-or-later OR MIT
    365  BSD-3-Clause
    362  BSD-3-Clause OR GPL-2.0
```

출력에서 볼 것: 옛 ID `GPL-2.0`(26080)과 새 ID `GPL-2.0-only`(17734)가 같은 뜻으로 섞여 있다. `GPL-2.0+`와 `GPL-2.0-or-later`도 마찬가지. 그래서 같은 스크립트를 계열(family)로 묶는 버전(`k7_spdx_group.py` — 식에 `Linux-syscall-note`가 있으면 syscall, `OR`가 있으면 dual, `GPL-2.0`/`GPL-2.0-only`면 only … 식으로 분류, 확장자 필터는 C·헤더·DT·스크립트만)으로 랩의 네 소스 트리를 비교했다.

```sh
cd /var/lab/src; python3 ~/.elinux_scratch/K7/k7_spdx_group.py linux-6.18.55 u-boot tfa busybox-1.37.0
```

```text
== linux-6.18.55: 71121 source files
  39187  55.1%  GPL-2.0-only
  11887  16.7%  GPL-2.0-or-later
   8849  12.4%  NO TAG
   6388   9.0%  dual (X OR Y)
   2994   4.2%  permissive/other
   1487   2.1%  GPL WITH syscall-note
    329   0.5%  other GPL/LGPL
== u-boot: 23430 source files
  10268  43.8%  GPL-2.0-or-later
   4959  21.2%  dual (X OR Y)
   4851  20.7%  GPL-2.0-only
   2556  10.9%  NO TAG
    737   3.1%  permissive/other
     52   0.2%  other GPL/LGPL
      7   0.0%  GPL WITH syscall-note
== tfa: 4680 source files
   4417  94.4%  permissive/other
    190   4.1%  dual (X OR Y)
     61   1.3%  NO TAG
     12   0.3%  other GPL/LGPL
== busybox-1.37.0: 790 source files
    789  99.9%  NO TAG
      1   0.1%  dual (X OR Y)
```

```svg
<svg viewBox="0 0 680 300" xmlns="http://www.w3.org/2000/svg"><defs><marker id="ah5" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto-start-reverse"><path d="M0,0 L10,5 L0,10 z" fill="currentColor"/></marker></defs><text x="340.0" y="18.0" font-size="13" text-anchor="middle" font-weight="bold">소스 파일 SPDX-License-Identifier 분포 (직접 측정, 파일 수 기준 %)</text><text x="142.0" y="53.0" font-size="12" text-anchor="end">linux-6.18.55</text><text x="142.0" y="67.0" font-size="11" text-anchor="end">71121 files</text><rect x="150.0" y="34" width="275.5" height="30" fill="#d0564a" fill-opacity="0.85" stroke="#d0564a" stroke-width="0.8"/><text x="287.8" y="54.0" font-size="11" text-anchor="middle">55%</text><rect x="425.5" y="34" width="83.5" height="30" fill="#e08a3c" fill-opacity="0.85" stroke="#e08a3c" stroke-width="0.8"/><text x="467.2" y="54.0" font-size="11" text-anchor="middle">17%</text><rect x="509.0" y="34" width="45.0" height="30" fill="#4a7bd0" fill-opacity="0.85" stroke="#4a7bd0" stroke-width="0.8"/><text x="531.5" y="54.0" font-size="11" text-anchor="middle">9%</text><rect x="554.0" y="34" width="21.0" height="30" fill="#3f9a6b" fill-opacity="0.85" stroke="#3f9a6b" stroke-width="0.8"/><rect x="575.0" y="34" width="13.0" height="30" fill="#888" fill-opacity="0.85" stroke="#888" stroke-width="0.8"/><rect x="588.0" y="34" width="62.0" height="30" fill="#888" fill-opacity="0.0" stroke="#888" stroke-width="0.8"/><text x="619.0" y="54.0" font-size="11" text-anchor="middle">12%</text><text x="142.0" y="97.0" font-size="12" text-anchor="end">u-boot</text><text x="142.0" y="111.0" font-size="11" text-anchor="end">23430 files</text><rect x="150.0" y="78" width="103.5" height="30" fill="#d0564a" fill-opacity="0.85" stroke="#d0564a" stroke-width="0.8"/><text x="201.8" y="98.0" font-size="11" text-anchor="middle">21%</text><rect x="253.5" y="78" width="219.0" height="30" fill="#e08a3c" fill-opacity="0.85" stroke="#e08a3c" stroke-width="0.8"/><text x="363.0" y="98.0" font-size="11" text-anchor="middle">44%</text><rect x="472.5" y="78" width="106.0" height="30" fill="#4a7bd0" fill-opacity="0.85" stroke="#4a7bd0" stroke-width="0.8"/><text x="525.5" y="98.0" font-size="11" text-anchor="middle">21%</text><rect x="578.5" y="78" width="15.5" height="30" fill="#3f9a6b" fill-opacity="0.85" stroke="#3f9a6b" stroke-width="0.8"/><rect x="594.0" y="78" width="1.0" height="30" fill="#888" fill-opacity="0.85" stroke="#888" stroke-width="0.8"/><rect x="595.0" y="78" width="54.5" height="30" fill="#888" fill-opacity="0.0" stroke="#888" stroke-width="0.8"/><text x="622.2" y="98.0" font-size="11" text-anchor="middle">11%</text><text x="142.0" y="141.0" font-size="12" text-anchor="end">tfa</text><text x="142.0" y="155.0" font-size="11" text-anchor="end">4680 files</text><rect x="150.0" y="122" width="20.5" height="30" fill="#4a7bd0" fill-opacity="0.85" stroke="#4a7bd0" stroke-width="0.8"/><rect x="170.5" y="122" width="472.0" height="30" fill="#3f9a6b" fill-opacity="0.85" stroke="#3f9a6b" stroke-width="0.8"/><text x="406.5" y="142.0" font-size="11" text-anchor="middle">94%</text><rect x="642.5" y="122" width="1.5" height="30" fill="#888" fill-opacity="0.85" stroke="#888" stroke-width="0.8"/><rect x="644.0" y="122" width="6.5" height="30" fill="#888" fill-opacity="0.0" stroke="#888" stroke-width="0.8"/><text x="142.0" y="185.0" font-size="12" text-anchor="end">busybox-1.37.0</text><text x="142.0" y="199.0" font-size="11" text-anchor="end">790 files</text><rect x="150.0" y="166" width="0.5" height="30" fill="#4a7bd0" fill-opacity="0.85" stroke="#4a7bd0" stroke-width="0.8"/><rect x="150.5" y="166" width="499.5" height="30" fill="#888" fill-opacity="0.0" stroke="#888" stroke-width="0.8"/><text x="400.2" y="186.0" font-size="11" text-anchor="middle">100%</text><line x1="150.0" y1="210" x2="150.0" y2="215" stroke="currentColor" stroke-width="1.4"/><text x="150.0" y="228.0" font-size="11" text-anchor="middle">0%</text><line x1="275.0" y1="210" x2="275.0" y2="215" stroke="currentColor" stroke-width="1.4"/><text x="275.0" y="228.0" font-size="11" text-anchor="middle">25%</text><line x1="400.0" y1="210" x2="400.0" y2="215" stroke="currentColor" stroke-width="1.4"/><text x="400.0" y="228.0" font-size="11" text-anchor="middle">50%</text><line x1="525.0" y1="210" x2="525.0" y2="215" stroke="currentColor" stroke-width="1.4"/><text x="525.0" y="228.0" font-size="11" text-anchor="middle">75%</text><line x1="650.0" y1="210" x2="650.0" y2="215" stroke="currentColor" stroke-width="1.4"/><text x="650.0" y="228.0" font-size="11" text-anchor="middle">100%</text><line x1="150" y1="210" x2="650" y2="210" stroke="currentColor" stroke-width="1.4"/><rect x="20" y="248" width="14" height="14" fill="#d0564a" fill-opacity="0.85" stroke="#d0564a"/><text x="40.0" y="260.0" font-size="12" text-anchor="start">GPL-2.0-only</text><rect x="240" y="248" width="14" height="14" fill="#e08a3c" fill-opacity="0.85" stroke="#e08a3c"/><text x="260.0" y="260.0" font-size="12" text-anchor="start">GPL-2.0-or-later</text><rect x="460" y="248" width="14" height="14" fill="#4a7bd0" fill-opacity="0.85" stroke="#4a7bd0"/><text x="480.0" y="260.0" font-size="12" text-anchor="start">dual (X OR Y)</text><rect x="20" y="272" width="14" height="14" fill="#3f9a6b" fill-opacity="0.85" stroke="#3f9a6b"/><text x="40.0" y="284.0" font-size="12" text-anchor="start">permissive</text><rect x="240" y="272" width="14" height="14" fill="#888" fill-opacity="0.85" stroke="#888"/><text x="260.0" y="284.0" font-size="12" text-anchor="start">기타 GPL/LGPL·syscall-note</text><rect x="460" y="272" width="14" height="14" fill="#888" fill-opacity="0.0" stroke="#888"/><text x="480.0" y="284.0" font-size="12" text-anchor="start">태그 없음</text></svg>
```
그림 4 — 랩 소스 트리 4개의 SPDX 태그 분포(위 출력값). 차트의 "기타 GPL/LGPL·syscall-note"는 `GPL WITH syscall-note` + `other GPL/LGPL`을 합친 것이다. 커널은 GPL-2.0-only가 주류, U-Boot는 or-later가 주류, TF-A는 거의 전부 permissive(BSD-3-Clause), BusyBox는 SPDX 태그 대신 문장으로 표시한다.

"태그 없음"이 곧 "라이선스 없음"은 아니다. 커널의 태그 없는 파일 상당수는 문서 · 스크립트 · 자동 생성 파일이거나 옛 형식의 라이선스 문구를 그대로 갖고 있다. 스캐너(ScanCode 등)는 이런 문구를 텍스트 매칭으로 찾는다 — 이 노트에서는 무거워서 쓰지 않았다.

### 3.5 scripts/spdxcheck.py — 태그 문법 검사

커널에는 SPDX 태그가 `LICENSES/`에 있는 ID만 쓰는지, 식 문법이 맞는지 검사하는 스크립트가 있다. `ply`(파서)와 `GitPython`이 필요하고, git 저장소 안에서 `LICENSES/`를 읽는다. 랩 소스는 git 저장소가 아니므로 `LICENSES/`와 파일 몇 개를 `/var/tmp/K7/k7_spdxtree`에 복사해 `git init`한 뒤 돌렸다. 일부러 틀린 파일 4개도 넣었다.

```sh
# k7_bad1.c: // SPDX-License-Identifier: GPL-2.0-only OR Foo-1.0
# k7_bad2.c: // SPDX-License-Identifier: GPL-2.0-only AND
# k7_bad3.c: // SPDX-License-Identifier: Apache-2.0
# k7_bad4.c: /* SPDX-License-Identifier: GPL-2.0-or-later WITH Linux-syscall-note */
P=/var/tmp/K7/k7_venv/bin/python
$P scripts/spdxcheck.py -v kernel/module/main.c drivers/gpu/drm/drm_drv.c include/uapi/linux/ioctl.h
$P scripts/spdxcheck.py k7_bad1.c k7_bad2.c k7_bad3.c k7_bad4.c; echo "rc=$?"
```

```text

License files:               21
Exception files:              2
License IDs                  34
Exception IDs                 2

Files excluded:               0
Files checked:                3
Lines checked:               17
Files with SPDX:              2  66%
Files without SPDX:           1  33%
Files with errors:            0

Directories accounted:        3
Directories complete:         2  66%
k7_bad1.c: 1:44 Invalid License ID: Foo-1.0
k7_bad2.c: 1:0 Unfinished license expression
rc=0
```

출력에서 볼 것: (1) 21개 라이선스 파일에서 34개 ID를 읽었다 — 한 파일이 여러 ID를 가질 수 있다(`GPL-2.0` 파일에 `Valid-License-Identifier: GPL-2.0`, `GPL-2.0-only`, `GPL-2.0+`, `GPL-2.0-or-later`가 다 있다). (2) "Files without SPDX: 1"은 `drm_drv.c`다 — 첫 줄이 SPDX 태그가 아니라 옛 형식 저작권 주석이다. (3) 없는 ID(`Foo-1.0`)와 끝나지 않은 식은 잡지만, `Apache-2.0` 단독 사용(커널 규칙상 `dual/`이라 `OR`로만 써야 함)은 이 도구가 잡지 않았다. (4) 에러가 있어도 종료 코드가 0이라 CI에서 쓰려면 출력을 검사해야 한다. 커널에 패치를 보낼 때는 `scripts/checkpatch.pl`이 SPDX 줄을 먼저 본다 — §4.4에서 실제 경고를 본다.

---

## 4. 커널 모듈 라이선스 — 독점 모듈을 실어도 되나

### 4.1 직관 — 커널이 그어 둔 선

센서 키트 회사가 독점 카메라 ISP 드라이버를 커널 모듈로 출하하고 싶다고 하자. 법적 쟁점은 "커널 모듈이 커널의 파생 저작물인가"이고, 이건 **확정된 답이 없다**(법원 판례로 정리되지 않았다). 커널 개발자들의 입장은 커널 코드에 직접 박혀 있다.

- `MODULE_LICENSE()`로 모듈이 자기 라이선스를 신고한다.
- 커널 내부 API 중 일부는 `EXPORT_SYMBOL_GPL`로만 내보낸다 — "이 심볼을 쓰는 코드는 커널 내부와 밀접하므로 GPL 호환 모듈만 써라"는 신호다.
- GPL 호환이 아닌 모듈은 GPL-only 심볼을 쓸 수 없고(빌드 · 로드 모두), 로드되면 커널이 "tainted"(오염) 표시를 남긴다. 커널 커뮤니티는 tainted 커널의 버그 리포트를 보통 받지 않는다.

```svg
<svg viewBox="0 0 680 420" xmlns="http://www.w3.org/2000/svg"><defs><marker id="ah4" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto-start-reverse"><path d="M0,0 L10,5 L0,10 z" fill="currentColor"/></marker></defs><text x="170.0" y="20.0" font-size="13" text-anchor="middle" font-weight="bold">빌드 시점 (modpost)</text><text x="510.0" y="20.0" font-size="13" text-anchor="middle" font-weight="bold">로드 시점 (insmod → 커널)</text><rect x="20" y="32" width="300" height="50" rx="6" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/><text x="170.0" y="53.2" font-size="12" text-anchor="middle">k7_prop_bad.c</text><text x="170.0" y="69.2" font-size="12" text-anchor="middle">MODULE_LICENSE("Proprietary")</text><rect x="20" y="102" width="300" height="60" rx="6" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/><text x="170.0" y="120.2" font-size="12" text-anchor="middle">modpost: .modinfo의 license 읽기</text><text x="170.0" y="136.2" font-size="12" text-anchor="middle">+ Module.symvers의 export 종류</text><text x="170.0" y="152.2" font-size="12" text-anchor="middle">(EXPORT_SYMBOL vs _GPL)</text><rect x="20" y="182" width="300" height="60" rx="6" fill="#d0564a" fill-opacity="0.12" stroke="#d0564a"/><text x="170.0" y="200.2" font-size="12" text-anchor="middle">non-GPL 모듈 + GPL-only 심볼</text><text x="170.0" y="216.2" font-size="12" text-anchor="middle">ERROR: GPL-incompatible module</text><text x="170.0" y="232.2" font-size="12" text-anchor="middle">→ .ko가 만들어지지 않음</text><rect x="20" y="262" width="300" height="50" rx="6" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/><text x="170.0" y="283.2" font-size="12" text-anchor="middle">non-GPL 심볼만 쓰면</text><text x="170.0" y="299.2" font-size="12" text-anchor="middle">빌드 통과 → k7_prop_ok.ko</text><line x1="170" y1="84" x2="170" y2="100" stroke="currentColor" stroke-width="1.4" marker-end="url(#ah4)"/><line x1="120" y1="164" x2="120" y2="180" stroke="currentColor" stroke-width="1.4" marker-end="url(#ah4)"/><path d="M290,164 L290,172 L335,172 L335,287 L322,287" fill="none" stroke="currentColor" stroke-width="1.4" marker-end="url(#ah4)"/><rect x="360" y="32" width="300" height="50" rx="6" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/><text x="510.0" y="53.2" font-size="12" text-anchor="middle">load_module()</text><text x="510.0" y="69.2" font-size="12" text-anchor="middle">module_augment_kernel_taints()</text><rect x="360" y="102" width="300" height="50" rx="6" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/><text x="510.0" y="123.2" font-size="12" text-anchor="middle">intree 표시 없음</text><text x="510.0" y="139.2" font-size="12" text-anchor="middle">→ TAINT_OOT_MODULE (O, 값 4096)</text><rect x="360" y="172" width="300" height="60" rx="6" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/><text x="510.0" y="190.2" font-size="12" text-anchor="middle">license_is_gpl_compatible()?</text><text x="510.0" y="206.2" font-size="12" text-anchor="middle">아니면 TAINT_PROPRIETARY_MODULE</text><text x="510.0" y="222.2" font-size="12" text-anchor="middle">(P, 값 1) + 경고 로그</text><rect x="360" y="252" width="300" height="60" rx="6" fill="#d0564a" fill-opacity="0.12" stroke="#d0564a"/><text x="510.0" y="270.2" font-size="12" text-anchor="middle">resolve_symbol(): gplok = false</text><text x="510.0" y="286.2" font-size="12" text-anchor="middle">__ksymtab_gpl 건너뜀</text><text x="510.0" y="302.2" font-size="12" text-anchor="middle">→ Unknown symbol (err -2)</text><line x1="510" y1="84" x2="510" y2="100" stroke="currentColor" stroke-width="1.4" marker-end="url(#ah4)"/><line x1="510" y1="154" x2="510" y2="170" stroke="currentColor" stroke-width="1.4" marker-end="url(#ah4)"/><line x1="510" y1="234" x2="510" y2="250" stroke="currentColor" stroke-width="1.4" marker-end="url(#ah4)"/><line x1="322" y1="270" x2="358" y2="60" stroke="currentColor" stroke-width="1.4" stroke-dasharray="4 3" marker-end="url(#ah4)"/><rect x="20" y="340" width="640" height="60" rx="6" fill="#888" fill-opacity="0.12" stroke="#888"/><text x="340.0" y="366.2" font-size="12" text-anchor="middle">방어선 두 겹: modpost가 막고, 우회해도(예: .modinfo 바이너리 수정) 커널이 심볼 바인딩을 거부한다.</text><text x="340.0" y="382.2" font-size="12" text-anchor="middle">taint는 한 번 켜지면 rmmod해도 꺼지지 않는다 (재부팅 전까지).</text></svg>
```
그림 5 — 모듈 라이선스 검사의 두 방어선. 빌드 시점에는 modpost가, 로드 시점에는 커널 모듈 로더가 같은 규칙을 강제한다.

### 4.2 MODULE_LICENSE가 받는 값

```sh
sed -n 193,219p /var/lab/src/linux-6.18.55/include/linux/module.h
```

```text
 * The following license idents are currently accepted as indicating free
 * software modules
 *
 *	"GPL"				[GNU Public License v2]
 *	"GPL v2"			[GNU Public License v2]
 *	"GPL and additional rights"	[GNU Public License v2 rights and more]
 *	"Dual BSD/GPL"			[GNU Public License v2
 *					 or BSD license choice]
 *	"Dual MIT/GPL"			[GNU Public License v2
 *					 or MIT license choice]
 *	"Dual MPL/GPL"			[GNU Public License v2
 *					 or Mozilla license choice]
 *
 * The following other idents are available
 *
 *	"Proprietary"			[Non free products]
 *
 * Both "GPL v2" and "GPL" (the latter also in dual licensed strings) are
 * merely stating that the module is licensed under the GPL v2, but are not
 * telling whether "GPL v2 only" or "GPL v2 or later". The reason why there
 * are two variants is a historic and failed attempt to convey more
 * information in the MODULE_LICENSE string. For module loading the
 * "only/or later" distinction is completely irrelevant and does neither
 * replace the proper license identifiers in the corresponding source file
 * nor amends them in any way. The sole purpose is to make the
 * 'Proprietary' flagging work and to refuse to bind symbols which are
 * exported with EXPORT_SYMBOL_GPL when a non free module is loaded.
```

출력에서 볼 것: `MODULE_LICENSE` 문자열의 **유일한 목적**은 Proprietary 표시와 GPL-only 심볼 바인딩 거부다. 실제 라이선스는 파일의 SPDX 태그가 정한다. 검사 함수 자체는 6줄짜리 문자열 비교다.

```c
/* include/linux/license.h (6.18.55) */
static inline int license_is_gpl_compatible(const char *license)
{
	return (strcmp(license, "GPL") == 0
		|| strcmp(license, "GPL v2") == 0
		|| strcmp(license, "GPL and additional rights") == 0
		|| strcmp(license, "Dual BSD/GPL") == 0
		|| strcmp(license, "Dual MIT/GPL") == 0
		|| strcmp(license, "Dual MPL/GPL") == 0);
}
```

트리 안 모듈이 실제로 무엇을 쓰는지 센다.

```sh
cd /var/lab/src/linux-6.18.55
grep -rhoE "MODULE_LICENSE\(\"[^\"]*\"\)" --include=*.c . | sort | uniq -c | sort -rn
```

```text
   8297 MODULE_LICENSE("GPL")
   3495 MODULE_LICENSE("GPL v2")
    469 MODULE_LICENSE("Dual BSD/GPL")
     48 MODULE_LICENSE("GPL and additional rights")
     48 MODULE_LICENSE("Dual MIT/GPL")
     10 MODULE_LICENSE("Dual MPL/GPL")
```

출력에서 볼 것: 트리 안에는 `"Proprietary"`가 하나도 없다(당연하다). `"GPL v2"`가 3495개 남아 있지만 checkpatch는 새 코드에 `"GPL"`을 쓰라고 경고한다(§4.4).

### 4.3 EXPORT_SYMBOL vs EXPORT_SYMBOL_GPL — 얼마나 많나

```sh
cd /var/lab/src/linux-6.18.55
grep -rhoE "^\s*EXPORT_SYMBOL(_GPL)?\(" --include=*.c . | sed "s/[ \t]//g" | sort | uniq -c
grep -rhoE "^\s*EXPORT_SYMBOL_NS(_GPL)?\(" --include=*.c . | sed "s/[ \t]//g" | sort | uniq -c
cd /var/lab/out/linux-build; awk '{print $4}' Module.symvers | sort | uniq -c
grep -wE "ktime_get|ktime_get_real_ts64|__platform_driver_register|_printk|gpiod_get|jiffies" Module.symvers
```

```text
  16003 EXPORT_SYMBOL(
  20392 EXPORT_SYMBOL_GPL(
    483 EXPORT_SYMBOL_NS(
   1383 EXPORT_SYMBOL_NS_GPL(
   8329 EXPORT_SYMBOL
  12449 EXPORT_SYMBOL_GPL
0x00000000	_printk	vmlinux	EXPORT_SYMBOL	
0x00000000	ktime_get	vmlinux	EXPORT_SYMBOL_GPL	
0x00000000	ktime_get_real_ts64	vmlinux	EXPORT_SYMBOL	
0x00000000	jiffies	vmlinux	EXPORT_SYMBOL	
0x00000000	gpiod_get	vmlinux	EXPORT_SYMBOL_GPL	
0x00000000	__platform_driver_register	vmlinux	EXPORT_SYMBOL_GPL	
```

출력에서 볼 것: 소스 전체에서도, 랩 커널 빌드(`Module.symvers` — 실제로 빌드된 심볼만)에서도 **GPL-only가 더 많다**(12449 vs 8329). 특히 `__platform_driver_register`(platform 드라이버 등록, G 모듈), `gpiod_get`(GPIO consumer, H 모듈)이 GPL-only다. 즉 현대 드라이버 모델 위에서 독점 드라이버를 쓰는 건 사실상 불가능에 가깝다. CRC 열이 0인 건 랩 커널이 `CONFIG_MODVERSIONS`를 켜지 않았기 때문이다. 다음 실험은 `ktime_get`(GPL-only)과 `ktime_get_real_ts64`(일반)를 비교한다.

### 4.4 실험 1 — 독점 모듈이 GPL-only 심볼을 쓰면 (modpost 에러)

무엇을 확인하는 코드인지: `MODULE_LICENSE("Proprietary")` 모듈에서 GPL-only 심볼 `ktime_get`을 부르면 빌드가 어디서 멈추는지.

```c
// k7_prop_bad.c: proprietary module that calls a GPL-only symbol (ktime_get)
#include <linux/module.h>
#include <linux/timekeeping.h>

static int __init k7_init(void)
{
	ktime_t t = ktime_get();          /* EXPORT_SYMBOL_GPL */
	pr_info("k7_prop_bad: ktime=%lld\n", ktime_to_ns(t));
	return 0;
}
static void __exit k7_exit(void) { }
module_init(k7_init);
module_exit(k7_exit);
MODULE_LICENSE("Proprietary");
MODULE_DESCRIPTION("K7 demo: proprietary + GPL-only symbol");
```

```make
obj-m += k7_prop_bad.o
```

```sh
cd /var/tmp/K7/k7_mod_bad && make -C /var/lab/out/linux-build M=$PWD modules; echo "exit=$?"
```

```text
make: Entering directory '/var/lab/build/linux'
make[1]: Entering directory '/var/tmp/K7/k7_mod_bad'
  CC [M]  k7_prop_bad.o
  MODPOST Module.symvers
ERROR: modpost: GPL-incompatible module k7_prop_bad.ko uses GPL-only symbol 'ktime_get'
make[3]: *** [/var/lab/src/linux-6.18.55/scripts/Makefile.modpost:147: Module.symvers] Error 1
make[2]: *** [/var/lab/src/linux-6.18.55/Makefile:2000: modpost] Error 2
make[1]: *** [/var/lab/src/linux-6.18.55/Makefile:248: __sub-make] Error 2
make[1]: Leaving directory '/var/tmp/K7/k7_mod_bad'
make: *** [/var/lab/src/linux-6.18.55/Makefile:248: __sub-make] Error 2
exit=2
```

출력에서 볼 것: 컴파일(`CC [M]`)은 성공했고, 링크 단계인 **MODPOST에서 에러**가 났다. `.ko`는 만들어지지 않는다. modpost는 `.o`의 `.modinfo` 섹션에서 `license=Proprietary`를 읽고, `Module.symvers`에서 `ktime_get`이 `EXPORT_SYMBOL_GPL`임을 확인했다. 해당 코드는 `scripts/mod/modpost.c`에 있다.

```text
scripts/mod/modpost.c:1597:			if (!license_is_gpl_compatible(license)) {
scripts/mod/modpost.c:1598:				mod->is_gpl_compatible = false;
scripts/mod/modpost.c:1780:		if (!mod->is_gpl_compatible && exp->is_gpl_only)
scripts/mod/modpost.c:1781:			error("GPL-incompatible module %s.ko uses GPL-only symbol '%s'\n",
```

`KBUILD_MODPOST_WARN=1`(일부 modpost 에러를 경고로 낮추는 변수)로 다시 빌드해도 똑같이 실패했다 — 이 검사는 경고로 낮아지지 않는다.

```sh
make -C /var/lab/out/linux-build M=$PWD KBUILD_MODPOST_WARN=1 modules 2>&1 | grep -E "ERROR|Error 1"; ls *.ko
```

```text
ERROR: modpost: GPL-incompatible module k7_prop_bad.ko uses GPL-only symbol 'ktime_get'
make[3]: *** [/var/lab/src/linux-6.18.55/scripts/Makefile.modpost:147: Module.symvers] Error 1
ls: cannot access '*.ko': No such file or directory
```

(실제 출력에서 `MODPOST` 줄과 나머지 make 에러 줄은 위 실험과 같아서 `grep`으로 걸렀다.)

checkpatch는 다른 각도에서 본다 — 독점 모듈 소스에 SPDX 줄이 없다고 경고한다.

```sh
perl /var/lab/src/linux-6.18.55/scripts/checkpatch.pl --no-tree -f ~/.elinux_scratch/K7/mod_ok/k7_prop_ok.c | head -6
```

```text
WARNING: Missing or malformed SPDX-License-Identifier tag in line 1
#1: FILE: /Users/donh/.elinux_scratch/K7/mod_ok/k7_prop_ok.c:1:
+// k7_prop_ok.c: proprietary module that only uses EXPORT_SYMBOL (non-GPL) symbols

total: 0 errors, 1 warnings, 17 lines checked

```

### 4.5 실험 2 — 일반 심볼만 쓰는 독점 모듈은 로드되지만 커널을 taint한다

무엇을 확인하는 코드인지: GPL-only 심볼을 피한 독점 모듈은 빌드 · 로드가 되는지, 그리고 커널 상태에 무엇이 남는지. 비교를 위해 같은 `ktime_get`을 쓰는 GPL 모듈(`k7_gpl.c` — 위 코드에서 이름과 `MODULE_LICENSE("GPL")`만 다름)도 같이 넣는다.

```c
// k7_prop_ok.c: proprietary module that only uses EXPORT_SYMBOL (non-GPL) symbols
#include <linux/module.h>
#include <linux/timekeeping.h>

static int __init k7_init(void)
{
	struct timespec64 ts;

	ktime_get_real_ts64(&ts);         /* EXPORT_SYMBOL */
	pr_info("k7_prop_ok: loaded, sec=%lld\n", (long long)ts.tv_sec);
	return 0;
}
static void __exit k7_exit(void) { pr_info("k7_prop_ok: unloaded\n"); }
module_init(k7_init);
module_exit(k7_exit);
MODULE_LICENSE("Proprietary");
MODULE_DESCRIPTION("K7 demo: proprietary module, non-GPL symbols only");
```

```sh
for m in ok gpl; do (cd /var/tmp/K7/k7_mod_$m && make -s -C /var/lab/out/linux-build M=$PWD modules); done
modinfo /var/tmp/K7/k7_mod_ok/k7_prop_ok.ko; modinfo -F license /var/tmp/K7/k7_mod_gpl/k7_gpl.ko
```

```text
filename:       /var/tmp/K7/k7_mod_ok/k7_prop_ok.ko
description:    K7 demo: proprietary module, non-GPL symbols only
license:        Proprietary
depends:        
name:           k7_prop_ok
vermagic:       6.18.55 SMP preempt mod_unload aarch64
GPL
```

경고 없이 둘 다 빌드됐다. 게스트 안에서 돌릴 스크립트는 이렇다.

```sh
#!/bin/sh
# k7_test.sh: load a GPL module then a proprietary one, watch the taint mask
echo "== before: tainted=$(cat /proc/sys/kernel/tainted)"
insmod /k7_gpl.ko
echo "== after k7_gpl: tainted=$(cat /proc/sys/kernel/tainted)"
insmod /k7_prop_ok.ko
echo "== after k7_prop_ok: tainted=$(cat /proc/sys/kernel/tainted)"
echo "== /sys/module/*/taint:"
for m in k7_gpl k7_prop_ok; do echo "$m: '$(cat /sys/module/$m/taint)'"; done
echo "== lsmod:"; lsmod
echo "== dmesg:"; dmesg | grep -E "k7_|taint"
rmmod k7_prop_ok
echo "== after rmmod: tainted=$(cat /proc/sys/kernel/tainted)"
```

랩 rootfs를 복사해 `.ko` 두 개와 스크립트를 넣고 initramfs를 다시 묶어 부팅한다.

```sh
cd /var/tmp/K7 && mkdir k7_rootfs && cp -a /var/lab/build/rootfs/. k7_rootfs/
cp k7_mod_ok/k7_prop_ok.ko k7_mod_gpl/k7_gpl.ko k7_rootfs/
install -m755 ~/.elinux_scratch/K7/k7_test.sh k7_rootfs/k7_test.sh
(cd k7_rootfs && find . | cpio -o -H newc --quiet | gzip) > k7_initramfs.gz
timeout 90 qemu-system-aarch64 -machine virt -cpu cortex-a57 -m 512M -nographic \
  -kernel /var/lab/out/Image -initrd k7_initramfs.gz \
  -append "console=ttyAMA0 quiet LAB_CMD=/k7_test.sh" 2>&1 | tail -30
```

```text
lab initramfs: 6.18.55 up 1.08s
== before: tainted=0
== after k7_gpl: tainted=4096
== after k7_prop_ok: tainted=4097
== /sys/module/*/taint:
k7_gpl: 'O'
k7_prop_ok: 'PO'
== lsmod:
k7_prop_ok 12288 0 - Live 0xffff9ffd14b78000 (PO)
k7_gpl 12288 0 - Live 0xffff9ffd14b72000 (O)
== dmesg:
[    0.000000] Kernel command line: console=ttyAMA0 quiet LAB_CMD=/k7_test.sh
[    0.000000] Unknown kernel command line parameters "LAB_CMD=/k7_test.sh", will be passed to user space.
[    1.031096]     LAB_CMD=/k7_test.sh
[    1.194020] k7_gpl: loading out-of-tree module taints kernel.
[    1.197599] k7_gpl: ktime=1167700544
[    1.222029] k7_prop_ok: module license 'Proprietary' taints kernel.
[    1.222140] Disabling lock debugging due to kernel taint
[    1.222649] k7_prop_ok: module license taints kernel.
[    1.223311] k7_prop_ok: loaded, sec=1792283881
== after rmmod: tainted=4097
[    1.339756] reboot: Power down
```

출력에서 볼 것 (주소 · 타임스탬프 · ktime 값은 실행마다 다르다):

- **GPL 모듈도 taint한다** — `O`(out-of-tree, 비트 12 = 4096). 트리 밖에서 빌드한 모듈은 `.modinfo`에 `intree=Y`가 없어서다. 이건 라이선스와 무관한 "출처" 표시다.
- 독점 모듈은 `P`(proprietary, 비트 0 = 1)를 더해 4097이 됐다. dmesg에 "module license 'Proprietary' taints kernel."이 찍혔다.
- "Disabling lock debugging due to kernel taint" — P taint는 lockdep을 끈다(`LOCKDEP_NOW_UNRELIABLE`). 독점 코드가 섞인 커널의 락 검증 결과는 믿을 수 없다는 뜻이다.
- **rmmod해도 4097 그대로다.** taint는 재부팅 전까지 지워지지 않는다. 필드에서 받은 oops 로그의 `Tainted: P  O` 줄은 "언젠가 독점 모듈이 로드된 적 있다"는 뜻이다.

taint 값은 커널 소스의 도구로 해석할 수 있다.

```sh
sh /var/lab/src/linux-6.18.55/tools/debugging/kernel-chktaint 4097
```

```text
Kernel is "tainted" for the following reasons:
tools/debugging/kernel-chktaint: 22: [: 4097x: unexpected operator
tools/debugging/kernel-chktaint: 22: [: 4097x: unexpected operator
 * proprietary module was loaded (#0)
 * externally-built ('out-of-tree') module was loaded  (#12)
For a more detailed explanation of the various taint flags see
 Documentation/admin-guide/tainted-kernels.rst in the Linux kernel sources
 or https://kernel.org/doc/html/latest/admin-guide/tainted-kernels.html
Raw taint value as int/string: 4097/'P           O       '
```

(중간의 `unexpected operator` 두 줄은 이 스크립트가 bash 문법을 쓰는데 `sh`(dash)로 돌려서 난 것이다. `bash`로 돌리면 없어진다. 결과 해석에는 영향이 없다.)

### 4.6 실험 3 — modpost를 우회하면? 커널이 다시 막는다

무엇을 확인하는 실험인지: modpost는 빌드 도구일 뿐이다. 누군가 GPL 모듈로 빌드한 뒤 `.ko`의 `license=` 문자열을 바이너리 수정으로 바꾸면 어떻게 될까. GPL로 빌드한 `k7_gpl.ko`(GPL-only `ktime_get` 사용)의 `license=GPL`을 같은 길이의 `license=XPL`로 바꿨다. 랩 커널은 모듈 서명(`CONFIG_MODULE_SIG`)이 꺼져 있어서 수정한 `.ko`도 로더까지 간다.

```sh
python3 -c '
b = open("k7_mod_gpl/k7_gpl.ko", "rb").read()
print("matches:", b.count(b"license=GPL\x00"))
open("k7_rootfs/k7_xpl.ko", "wb").write(b.replace(b"license=GPL\x00", b"license=XPL\x00"))'
modinfo -F license k7_rootfs/k7_xpl.ko
# initramfs 재생성 후 부팅, LAB_CMD: insmod /k7_xpl.ko; echo rc=$?; cat /proc/sys/kernel/tainted; dmesg | grep -E "k7_|taint"
```

```text
matches: 1
XPL
lab initramfs: 6.18.55 up 0.83s
insmod: can't insert '/k7_xpl.ko': unknown symbol in module or invalid parameter
rc=1
4097
[    0.844473] k7_gpl: loading out-of-tree module taints kernel.
[    0.844621] k7_gpl: module license 'XPL' taints kernel.
[    0.844647] Disabling lock debugging due to kernel taint
[    0.844709] k7_gpl: module license taints kernel.
[    0.845226] k7_gpl: Unknown symbol ktime_get (err -2)
```

(같은 initramfs로 두 번 부팅했다. 첫 번째 부팅의 `insmod`/`rc`/taint 줄과 두 번째 부팅의 dmesg 필터 결과를 합쳤고, 커널 명령줄 echo 줄은 뺐다.)

출력에서 볼 것: 커널 로더가 license 문자열을 다시 검사해 P taint를 걸고, GPL-only 심볼 테이블을 건너뛰어 `ktime_get`을 못 찾았다(`-ENOENT` = -2). **로드는 실패했는데 taint는 남았다**(4097). 모듈 이름이 여전히 `k7_gpl`인 것도 보인다 — 이름은 `.gnu.linkonce.this_module`에 따로 저장된다. 이런 "라이선스 세탁" 시도는 커널에 아예 블랙리스트까지 있다.

```sh
grep -n module_license_offenders -A6 /var/lab/src/linux-6.18.55/kernel/module/main.c | head -7
```

```text
2519:static const char *const module_license_offenders[] = {
2520-	/* driverloader was caught wrongly pretending to be under GPL */
2521-	"driverloader",
2522-
2523-	/* lve claims to be GPL but upstream won't provide source */
2524-	"lve",
2525-};
```

### 4.7 그래서 "독점 커널 모듈을 실어도 되나?"

엔지니어가 정리해서 법무에 넘길 사실은 이렇다.

1. **기술적으로**: GPL-only 심볼을 하나라도 쓰면 빌드가 안 된다. platform driver 등록, GPIO descriptor, 상당수 DMA · clock · regmap API가 GPL-only다. 피해서 짤 수 있는 드라이버는 매우 제한적이다.
2. **운영상**: 커널이 P taint된다. upstream · 배포판 커뮤니티 지원을 받기 어렵고, lockdep이 꺼진다.
3. **법적으로**: 모듈이 커널의 파생물인지는 미확정이다. 그래서 대부분 회사의 정책은 "커널 모듈은 GPL로 공개, 비밀이 필요한 로직은 유저스페이스(UIO/VFIO, 사용자 드라이버, 펌웨어 블롭)나 별도 코프로세서로"다. NVIDIA처럼 독점 모듈을 오래 유지한 회사도 최근 오픈 커널 모듈로 옮겨 간 흐름이 있다.
4. **GPL "shim" 꼼수**(GPL 래퍼 모듈이 GPL-only 심볼을 감싸 독점 모듈에 넘겨주기)는 커널이 막으려 하는 대상이다 — `kernel/module/main.c`는 GPL-only 심볼을 쓰는 모듈이 독점 모듈의 심볼을 가져다 쓰는 경우도 거부한다("module using GPL-only symbols uses symbols ... from proprietary module").

---

## 5. SBOM — 이미지의 부품표

### 5.1 직관 — 하드웨어 BOM과 같은 것

Don이 보드 bring-up 때 보는 BOM은 "U12 = TI TPS62xxx, rev B, 공급사 Digi-Key, 수량 1"이다. 부품 하나가 단종되거나 errata가 나오면 BOM으로 "어느 제품 어느 리비전에 이 부품이 들어갔나"를 찾는다. SBOM도 똑같다. **OpenSSL에 치명적 CVE가 나왔을 때 "우리 제품 중 어느 펌웨어 버전에 어느 버전의 OpenSSL이 들어 있나"를 몇 분 안에 답하려고** 만든다.

### 5.2 두 표준 — SPDX와 CycloneDX

| 항목 | SPDX | CycloneDX |
|---|---|---|
| 관리 주체 | Linux Foundation (SPDX 프로젝트) | OWASP |
| 표준화 | SPDX 2.2.1이 ISO/IEC 5962:2021 | ECMA-424 (CycloneDX 1.6 기반) |
| 출발점 | 라이선스 컴플라이언스 | 보안 · 공급망 |
| 현재 버전 | 2.3(2022, 널리 쓰임) · 3.0(2024, 모델 전면 개편) | 1.6이 널리 쓰임, 이 노트 도구는 1.7까지 지원 |
| 형식 | tag-value, JSON, YAML, RDF/XML (3.0은 JSON-LD) | JSON, XML, Protobuf |
| 취약점 | 외부 참조 · 3.0의 Security 프로필 | `vulnerabilities[]` 안에 VEX 내장 |
| 임베디드 빌드 시스템 | Yocto `create-spdx` 클래스 | Buildroot `utils/generate-cyclonedx`, 각종 언어 도구 |

SPDX 3.0은 2.3과 하위 호환이 아니다. 2.3의 Document/Package/File 중심 구조를 "Element + Relationship + Profile(Software, Security, Licensing, Build, AI, Dataset …)" 그래프 모델로 바꿨다. 이 노트의 손 실습은 도구 지원이 가장 넓은 2.3으로 했다. (SPDX 3.0 문서는 만들지 않았다.)

```svg
<svg viewBox="0 0 680 470" xmlns="http://www.w3.org/2000/svg"><defs><marker id="ah6" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto-start-reverse"><path d="M0,0 L10,5 L0,10 z" fill="currentColor"/></marker></defs><text x="170.0" y="20.0" font-size="13" text-anchor="middle" font-weight="bold">SPDX 2.3</text><text x="510.0" y="20.0" font-size="13" text-anchor="middle" font-weight="bold">CycloneDX 1.6</text><rect x="20" y="32" width="300" height="70" rx="6" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/><text x="170.0" y="55.2" font-size="12" text-anchor="middle">Document</text><text x="170.0" y="71.2" font-size="12" text-anchor="middle">creationInfo: creators(6), created(7)</text><text x="170.0" y="87.2" font-size="12" text-anchor="middle">documentNamespace · dataLicense</text><rect x="20" y="120" width="300" height="110" rx="6" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/><text x="170.0" y="139.2" font-size="12" text-anchor="middle">Package (컴포넌트)</text><text x="170.0" y="155.2" font-size="12" text-anchor="middle">name(2) · versionInfo(3)</text><text x="170.0" y="171.2" font-size="12" text-anchor="middle">supplier(1)</text><text x="170.0" y="187.2" font-size="12" text-anchor="middle">externalRefs: purl, cpe23(4)</text><text x="170.0" y="203.2" font-size="12" text-anchor="middle">checksums · licenseConcluded</text><text x="170.0" y="219.2" font-size="12" text-anchor="middle">licenseDeclared · copyrightText</text><rect x="20" y="248" width="300" height="50" rx="6" fill="#888" fill-opacity="0.12" stroke="#888"/><text x="170.0" y="269.2" font-size="12" text-anchor="middle">File / Snippet (선택)</text><text x="170.0" y="285.2" font-size="12" text-anchor="middle">FilesAnalyzed: true일 때</text><rect x="20" y="316" width="300" height="70" rx="6" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/><text x="170.0" y="339.2" font-size="12" text-anchor="middle">Relationship(5)</text><text x="170.0" y="355.2" font-size="12" text-anchor="middle">DESCRIBES · CONTAINS</text><text x="170.0" y="371.2" font-size="12" text-anchor="middle">DEPENDS_ON · DYNAMIC_LINK</text><rect x="20" y="404" width="300" height="50" rx="6" fill="#888" fill-opacity="0.12" stroke="#888"/><text x="170.0" y="425.2" font-size="12" text-anchor="middle">hasExtractedLicensingInfos</text><text x="170.0" y="441.2" font-size="12" text-anchor="middle">LicenseRef-… 사내 라이선스 정의</text><rect x="360" y="32" width="300" height="70" rx="6" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/><text x="510.0" y="55.2" font-size="12" text-anchor="middle">metadata</text><text x="510.0" y="71.2" font-size="12" text-anchor="middle">timestamp(7) · tools · authors(6)</text><text x="510.0" y="87.2" font-size="12" text-anchor="middle">component = 제품 자신</text><rect x="360" y="120" width="300" height="110" rx="6" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/><text x="510.0" y="139.2" font-size="12" text-anchor="middle">components[]</text><text x="510.0" y="155.2" font-size="12" text-anchor="middle">name · version · supplier</text><text x="510.0" y="171.2" font-size="12" text-anchor="middle">purl · cpe · hashes</text><text x="510.0" y="187.2" font-size="12" text-anchor="middle">licenses: id / expression</text><text x="510.0" y="203.2" font-size="12" text-anchor="middle">type: firmware, library,</text><text x="510.0" y="219.2" font-size="12" text-anchor="middle">operating-system …</text><rect x="360" y="248" width="300" height="50" rx="6" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/><text x="510.0" y="269.2" font-size="12" text-anchor="middle">dependencies[]</text><text x="510.0" y="285.2" font-size="12" text-anchor="middle">ref → dependsOn[]</text><rect x="360" y="316" width="300" height="70" rx="6" fill="#d0564a" fill-opacity="0.12" stroke="#d0564a"/><text x="510.0" y="339.2" font-size="12" text-anchor="middle">vulnerabilities[] (VEX)</text><text x="510.0" y="355.2" font-size="12" text-anchor="middle">id · affects · analysis.state</text><text x="510.0" y="371.2" font-size="12" text-anchor="middle">not_affected / exploitable …</text><rect x="360" y="404" width="300" height="50" rx="6" fill="#888" fill-opacity="0.12" stroke="#888"/><text x="510.0" y="425.2" font-size="12" text-anchor="middle">services · compositions …</text><text x="510.0" y="441.2" font-size="12" text-anchor="middle">(SBOM 외 확장 영역)</text><line x1="322" y1="67" x2="358" y2="67" stroke="currentColor" stroke-width="1.4" stroke-dasharray="4 3"/><line x1="322" y1="175" x2="358" y2="175" stroke="currentColor" stroke-width="1.4" stroke-dasharray="4 3"/><line x1="322" y1="351" x2="358" y2="273" stroke="currentColor" stroke-width="1.4" stroke-dasharray="4 3"/></svg>
```
그림 6 — SPDX 2.3과 CycloneDX 1.6의 데이터 모델 대응. 괄호 숫자는 NTIA 최소 요소 번호: (1) 공급자 (2) 컴포넌트 이름 (3) 버전 (4) 기타 고유 식별자 (5) 의존 관계 (6) SBOM 작성자 (7) 타임스탬프.

### 5.3 NTIA 최소 요소 — "SBOM이라고 부르려면 적어도 이것"

미국 행정명령 EO 14028(2021년 5월, "Improving the Nation's Cybersecurity")이 상무부(NTIA)에 SBOM 최소 요소를 정하라고 지시했고, NTIA가 2021년 7월 "The Minimum Elements For a Software Bill of Materials"를 냈다. 데이터 필드는 그림 6의 7개다. 그 밖에 자동화 지원(기계가 읽는 형식: SPDX, CycloneDX, SWID)과 작성 관행(빌드마다 새로 생성, 깊이, 알려진 미지의 표시 등)을 요구한다. 2025년에 CISA가 이를 개정하는 초안(해시, 라이선스, 생성 도구 등 필드 추가)을 의견 수렴용으로 냈다 — 최종 확정 여부는 이 노트에서 확인하지 않았다.

### 5.4 실습 — SPDX 2.3 SBOM을 손으로 쓰기

센서 키트 이미지 하나(이미지 = 커널 + BusyBox + 우리 앱 + glibc)를 SPDX 2.3 tag-value로 쓴다. 체크섬은 랩 산출물과 §1.4 앱 바이너리의 실제 `sha256sum` 값이다. 파일이 길어 두 부분으로 나눴다(실제로는 `cat part1.spdx part2.spdx > k7_image.spdx`).

```text
SPDXVersion: SPDX-2.3
DataLicense: CC0-1.0
SPDXID: SPDXRef-DOCUMENT
DocumentName: k7-sensor-image-1.0
DocumentNamespace: https://example.com/spdx/k7-sensor-image-1.0-6f1c2a9e
Creator: Organization: Example Robotics Inc.
Creator: Tool: hand-written-k7
Created: 2026-10-17T00:00:00Z

PackageName: k7-sensor-image
SPDXID: SPDXRef-Image
PackageVersion: 1.0
PackageSupplier: Organization: Example Robotics Inc.
PackageDownloadLocation: NOASSERTION
FilesAnalyzed: false
PackageChecksum: SHA256: a1fe6b27719bd3b531996e4dbe982bf254b59316b8a84ce17a84d4a237d74c5a
PackageLicenseConcluded: NOASSERTION
PackageLicenseDeclared: NOASSERTION
PackageCopyrightText: NOASSERTION
PrimaryPackagePurpose: FIRMWARE

PackageName: linux
SPDXID: SPDXRef-Linux
PackageVersion: 6.18.55
PackageSupplier: Organization: kernel.org
PackageDownloadLocation: https://cdn.kernel.org/pub/linux/kernel/v6.x/linux-6.18.55.tar.xz
FilesAnalyzed: false
PackageChecksum: SHA256: 6718e4e6cbea7fe66fbd3eaf1c1986f306d2ed5c134a2df43a2a7c05337f8448
PackageLicenseConcluded: GPL-2.0-only WITH Linux-syscall-note
PackageLicenseDeclared: GPL-2.0-only WITH Linux-syscall-note
PackageCopyrightText: NOASSERTION
ExternalRef: SECURITY cpe23Type cpe:2.3:o:linux:linux_kernel:6.18.55:*:*:*:*:*:*:*
ExternalRef: PACKAGE-MANAGER purl pkg:generic/linux@6.18.55
PrimaryPackagePurpose: OPERATING-SYSTEM

PackageName: busybox
SPDXID: SPDXRef-BusyBox
PackageVersion: 1.37.0
PackageSupplier: Organization: busybox.net
PackageDownloadLocation: https://busybox.net/downloads/busybox-1.37.0.tar.bz2
FilesAnalyzed: false
PackageLicenseConcluded: GPL-2.0-only
PackageLicenseDeclared: GPL-2.0-only
PackageCopyrightText: NOASSERTION
ExternalRef: SECURITY cpe23Type cpe:2.3:a:busybox:busybox:1.37.0:*:*:*:*:*:*:*
ExternalRef: PACKAGE-MANAGER purl pkg:generic/busybox@1.37.0
PrimaryPackagePurpose: APPLICATION
```

```text
PackageName: k7-sensor-app
SPDXID: SPDXRef-App
PackageVersion: 1.0.3
PackageSupplier: Organization: Example Robotics Inc.
PackageDownloadLocation: NOASSERTION
FilesAnalyzed: false
PackageChecksum: SHA256: 925a28af642b31e9dca4fc6541ebd782cbdf93a59b48671525b2ed13ad39aaa9
PackageLicenseConcluded: LicenseRef-ExampleRobotics-Proprietary
PackageLicenseDeclared: LicenseRef-ExampleRobotics-Proprietary
PackageCopyrightText: Copyright 2026 Example Robotics Inc.
ExternalRef: PACKAGE-MANAGER purl pkg:generic/example-robotics/k7-sensor-app@1.0.3
PrimaryPackagePurpose: APPLICATION

PackageName: glibc
SPDXID: SPDXRef-glibc
PackageVersion: 2.43
PackageSupplier: Organization: GNU Project
PackageDownloadLocation: https://ftp.gnu.org/gnu/glibc/glibc-2.43.tar.xz
FilesAnalyzed: false
PackageLicenseConcluded: LGPL-2.1-or-later
PackageLicenseDeclared: LGPL-2.1-or-later
PackageCopyrightText: NOASSERTION
ExternalRef: SECURITY cpe23Type cpe:2.3:a:gnu:glibc:2.43:*:*:*:*:*:*:*
ExternalRef: PACKAGE-MANAGER purl pkg:generic/glibc@2.43
PrimaryPackagePurpose: LIBRARY

Relationship: SPDXRef-DOCUMENT DESCRIBES SPDXRef-Image
Relationship: SPDXRef-Image CONTAINS SPDXRef-Linux
Relationship: SPDXRef-Image CONTAINS SPDXRef-BusyBox
Relationship: SPDXRef-Image CONTAINS SPDXRef-App
Relationship: SPDXRef-Image CONTAINS SPDXRef-glibc
Relationship: SPDXRef-App DYNAMIC_LINK SPDXRef-glibc

LicenseID: LicenseRef-ExampleRobotics-Proprietary
ExtractedText: <text>Proprietary. Copyright 2026 Example Robotics Inc. All rights reserved.</text>
LicenseName: Example Robotics Proprietary License
```

읽는 법:

- **Concluded vs Declared**: Declared는 "upstream이 그렇다고 말한 것", Concluded는 "우리가 검토해서 결론 낸 것"이다. 스캐너가 파일 단위로 다른 라이선스를 찾으면 둘이 달라질 수 있다.
- **PURL(Package URL)** 과 **CPE**: 컴포넌트를 전 세계에서 유일하게 가리키는 식별자. PURL은 패키지 생태계 기준(`pkg:pypi/...`, `pkg:deb/...`), CPE는 NVD(미국 취약점 DB)가 CVE를 매칭할 때 쓰는 이름이다. **CVE 매칭 품질은 이 식별자의 정확도로 결정된다.**
- **LicenseRef-**: SPDX 목록에 없는 라이선스(사내 독점 라이선스 등)는 `LicenseRef-` 접두어로 문서 안에서 정의한다.
- **DYNAMIC_LINK** 관계: §1.4의 LGPL 판단에 필요한 정보를 SBOM에 직접 남긴다. 정적 링크였다면 `STATIC_LINK`다.

### 5.5 pyspdxtools로 검증하고 JSON으로 변환

```sh
cd /var/tmp/K7/k7_sbom
V=/var/tmp/K7/k7_venv/bin
$V/pyspdxtools -i k7_image.spdx; echo "rc=$?"
$V/pyspdxtools -i k7_image.spdx -o k7_image.spdx.json; echo "rc=$?"
wc -l k7_image.spdx.json; head -22 k7_image.spdx.json
```

```text
Generating LALR tables
rc=0
rc=0
175 k7_image.spdx.json
{
    "SPDXID": "SPDXRef-DOCUMENT",
    "creationInfo": {
        "created": "2026-10-17T00:00:00Z",
        "creators": [
            "Organization: Example Robotics Inc.",
            "Tool: hand-written-k7"
        ]
    },
    "dataLicense": "CC0-1.0",
    "hasExtractedLicensingInfos": [
        {
            "extractedText": "Proprietary. Copyright 2026 Example Robotics Inc. All rights reserved.",
            "licenseId": "LicenseRef-ExampleRobotics-Proprietary",
            "name": "Example Robotics Proprietary License"
        }
    ],
    "name": "k7-sensor-image-1.0",
    "spdxVersion": "SPDX-2.3",
    "documentNamespace": "https://example.com/spdx/k7-sensor-image-1.0-6f1c2a9e",
    "packages": [
```

출력에서 볼 것: 유효한 문서면 아무 메시지 없이 끝난다("Generating LALR tables"는 tag-value 파서 초기화 메시지). 이제 흔한 실수 두 개를 일부러 넣어 본다 — 비표준 라이선스 이름, 정의 안 된 SPDXID 참조.

```sh
sed -e "s/^PackageLicenseDeclared: GPL-2.0-only$/PackageLicenseDeclared: GPLv2/" \
    -e "s/CONTAINS SPDXRef-glibc/CONTAINS SPDXRef-musl/" k7_image.spdx > k7_broken.spdx
diff k7_image.spdx k7_broken.spdx; $V/pyspdxtools -i k7_broken.spdx 2>&1 | tail -3
```

```text
43c43
< PackageLicenseDeclared: GPL-2.0-only
---
> PackageLicenseDeclared: GPLv2
79c79
< Relationship: SPDXRef-Image CONTAINS SPDXRef-glibc
---
> Relationship: SPDXRef-Image CONTAINS SPDXRef-musl
ERROR:root:The document is invalid. The following issues have been found:
Unrecognized license reference: GPLv2. license_expression must only use IDs from the license list or extracted licensing info, but is: GPLv2
did not find the referenced spdx_id "SPDXRef-musl" in the SPDX document
```

출력에서 볼 것: 사람이 흔히 쓰는 "GPLv2"는 SPDX ID가 아니다. SBOM의 라이선스 필드는 반드시 SPDX 목록의 ID(또는 `LicenseRef-`)여야 기계가 정책을 적용할 수 있다.

### 5.6 NTIA 최소 요소 검사

```sh
$V/ntia-checker k7_image.spdx.json; echo "rc=$?"
```

```text
2021 NTIA SBOM Minimum Elements Conformance Results

Conformant: True

Requirement                                    | Status
-------------------------------------------------------
All component names provided?                  | True
All component versions provided?               | True
All component identifiers provided?            | True
All component suppliers provided?              | True
SBOM author name provided?                     | True
SBOM creation timestamp provided?              | True
Dependency relationships provided?             | True

rc=0
```

BusyBox의 `PackageSupplier`와 glibc의 `PackageVersion`을 지운 버전을 만들어 다시 돌린다.

```sh
awk '/^PackageName: busybox/{b=1} /^PackageName: glibc/{g=1}
     b&&/^PackageSupplier/{b=0;next} g&&/^PackageVersion/{g=0;next} {print}' k7_image.spdx > k7_weak.spdx
$V/pyspdxtools -i k7_weak.spdx -o k7_weak.json && $V/ntia-checker k7_weak.json; echo "rc=$?"
```

```text
2021 NTIA SBOM Minimum Elements Conformance Results

Conformant: False

Requirement                                    | Status
-------------------------------------------------------
All component names provided?                  | True
All component versions provided?               | False
All component identifiers provided?            | True
All component suppliers provided?              | False
SBOM author name provided?                     | True
SBOM creation timestamp provided?              | True
Dependency relationships provided?             | True

rc=1
```

출력에서 볼 것: SPDX 스키마상으로는 유효하지만(공급자 · 버전은 SPDX 2.3에서 선택 필드) NTIA 기준에는 맞지 않는다. 그리고 실패 시 **종료 코드 1** — CI 게이트로 바로 쓸 수 있다.

### 5.7 CycloneDX — Python 도구용 SBOM 자동 생성

센서 키트의 지상국(ground station) 도구가 Python으로 짜여 있다고 하자. `pyserial`(UART)과 `PyYAML`(설정)을 쓰는 venv를 만들고, `cyclonedx-py`로 그 환경의 SBOM을 뽑는다. 루트 컴포넌트 정보는 `pyproject.toml`에서 읽는다.

```text
[project]
name = "k7-ground-tool"
version = "0.4.0"
description = "Ground-station tool that talks to the sensor kit over UART"
license = "Apache-2.0"
dependencies = ["pyserial==3.5", "PyYAML==6.0.2"]
```

```sh
python3 -m venv /var/tmp/K7/k7_toyenv && /var/tmp/K7/k7_toyenv/bin/pip install pyserial==3.5 PyYAML==6.0.2
cd /var/tmp/K7/k7_cdx
/var/tmp/K7/k7_venv/bin/cyclonedx-py environment --pyproject pyproject.toml --sv 1.6 --of JSON \
    --output-reproducible -o k7_bom.cdx.json /var/tmp/K7/k7_toyenv/bin/python; echo rc=$?
python3 -c '
import json; b = json.load(open("k7_bom.cdx.json"))
print("bomFormat:", b["bomFormat"], b["specVersion"])
m = b["metadata"]["component"]; print("main:", m["name"], m["version"], m.get("licenses"))
for c in b["components"]:
    print("comp:", c["name"], c["version"], c.get("purl"),
          [l.get("license", l.get("expression")) for l in c.get("licenses", [])])
print("deps:", b["dependencies"])'
```

```text
rc=0
bomFormat: CycloneDX 1.6
main: k7-ground-tool 0.4.0 [{'license': {'acknowledgement': 'declared', 'id': 'Apache-2.0'}}]
comp: PyYAML 6.0.2 pkg:pypi/pyyaml@6.0.2 [{'acknowledgement': 'declared', 'id': 'MIT'}]
comp: pip 25.1.1 pkg:pypi/pip@25.1.1 [{'acknowledgement': 'declared', 'id': 'MIT'}]
comp: pyserial 3.5 pkg:pypi/pyserial@3.5 [{'acknowledgement': 'declared', 'name': 'License :: OSI Approved :: BSD License'}]
deps: [{'ref': 'PyYAML==6.0.2'}, {'ref': 'pip==25.1.1'}, {'ref': 'pyserial==3.5'}, {'dependsOn': ['PyYAML==6.0.2', 'pyserial==3.5'], 'ref': 'root-component'}]
```

출력에서 볼 것:

- PURL(`pkg:pypi/pyserial@3.5`)이 자동으로 붙는다. 이 값으로 OSV · NVD 같은 취약점 DB를 조회한다.
- **pyserial의 라이선스는 SPDX ID가 아니라 `"License :: OSI Approved :: BSD License"`라는 PyPI 분류자**다. "BSD"는 2-Clause인지 3-Clause인지 모른다. 자동 생성 SBOM도 이런 구멍이 있고, 사람(OSRB)이 확인해 `BSD-3-Clause`로 확정해야 한다 — 그래서 SPDX에 Declared와 Concluded가 따로 있다.
- `pip` 자체도 컴포넌트로 들어갔다. 출하물에 pip가 들어가지 않는다면 걸러야 한다. 도구 출력을 그대로 믿지 말고 범위를 확인한다.

### 5.8 VEX — "이 CVE는 우리 제품에 해당 없음"을 기계가 읽게

SBOM으로 CVE를 매칭하면 오탐이 많다. 커널 CVE는 매년 수천 건인데, 대부분은 우리가 빌드하지 않은 드라이버에 있다. **VEX(Vulnerability Exploitability eXchange)** 는 "이 컴포넌트의 이 CVE는 우리 제품에서 영향 없음(이유: 코드 없음)" 같은 판정을 기계가 읽는 형식으로 남긴다. CycloneDX는 `vulnerabilities[]`로 이를 SBOM 안에 담는다. 아래 CVE 번호는 **가상**이다.

```text
{
  "bomFormat": "CycloneDX",
  "specVersion": "1.6",
  "version": 1,
  "metadata": {
    "timestamp": "2026-10-17T00:00:00Z",
    "component": {"type": "firmware", "bom-ref": "img", "name": "k7-sensor-image", "version": "1.0"}
  },
  "components": [
    {"type": "operating-system", "bom-ref": "linux", "name": "linux", "version": "6.18.55",
     "cpe": "cpe:2.3:o:linux:linux_kernel:6.18.55:*:*:*:*:*:*:*",
     "licenses": [{"expression": "GPL-2.0-only WITH Linux-syscall-note"}]}
  ],
  "dependencies": [{"ref": "img", "dependsOn": ["linux"]}],
  "vulnerabilities": [
    {"id": "CVE-2026-99999", "source": {"name": "NVD"},
     "affects": [{"ref": "linux"}],
     "analysis": {"state": "not_affected", "justification": "code_not_present",
                  "detail": "Bug is in a WiFi driver; CONFIG for that driver is not enabled in our defconfig."}}
  ]
}
```

스키마 검증은 `cyclonedx-python-lib`의 validator로 한다.

```python
#!/usr/bin/env python3
# k7_cdx_validate.py: schema-validate a CycloneDX JSON file with cyclonedx-python-lib
import sys
from cyclonedx.validation.json import JsonStrictValidator
from cyclonedx.schema import SchemaVersion

v = JsonStrictValidator(SchemaVersion.V1_6)
for path in sys.argv[1:]:
    err = v.validate_str(open(path).read())
    print(f'{path}: {"INVALID - " + str(err.data.message) if err else "valid CycloneDX 1.6"}')
```

```sh
sed 's/"not_affected"/"not_vulnerable"/' k7_vex.cdx.json > k7_vex_bad.cdx.json
/var/tmp/K7/k7_venv/bin/python k7_cdx_validate.py k7_bom.cdx.json k7_vex.cdx.json k7_vex_bad.cdx.json
```

```text
k7_bom.cdx.json: valid CycloneDX 1.6
k7_vex.cdx.json: valid CycloneDX 1.6
k7_vex_bad.cdx.json: INVALID - 'not_vulnerable' is not one of ['resolved', 'resolved_with_pedigree', 'exploitable', 'in_triage', 'false_positive', 'not_affected']
```

출력에서 볼 것: 앞서 `cyclonedx-py`가 만든 SBOM도 스키마상 유효하다. VEX의 `analysis.state`는 정해진 값만 허용된다. 커널 CVE의 `code_not_present` 판정 근거는 결국 `.config`다 — §2.1에서 `.config`를 소스 릴리스에 넣은 것이 여기서 다시 쓰인다.

### 5.9 reuse — 우리 저장소부터 깨끗하게

회사 자체 저장소도 파일마다 저작권 · 라이선스 표시가 있어야 SBOM 도구가 판정할 수 있다. FSFE의 REUSE 규격과 `reuse` 도구가 이를 검사한다. 센서 앱 저장소를 흉내 낸 toy repo(`src/imu.c`에만 SPDX 태그, `src/can.c`와 `Makefile`엔 없음)에서 돌린다.

```sh
cd /var/tmp/K7/k7_reuse && git init -q && /var/tmp/K7/k7_venv/bin/reuse lint; echo "rc=$?"
```

```text
# MISSING LICENSES

'MIT' found in:
* src/imu.c

# MISSING COPYRIGHT AND LICENSING INFORMATION

The following files have no copyright and licensing information:
* Makefile
* src/can.c

# SUMMARY

* Bad licenses: 0
* Deprecated licenses: 0
* Licenses without file extension: 0
* Missing licenses: MIT
* Unused licenses: 0
* Used licenses: MIT
* Read errors: 0
* Invalid SPDX License Expressions: 0
* Files with copyright information: 1 / 3
* Files with license information: 1 / 3

Unfortunately, your project is not compliant with version 3.3 of the REUSE Specification :-(
```

(그 뒤의 RECOMMENDATIONS 문단은 생략했다. 종료 코드는 `rc=1`.) 헤더를 넣고 라이선스 원문을 받는다.

```sh
R=/var/tmp/K7/k7_venv/bin/reuse
$R annotate --copyright "Example Robotics Inc." --year 2026 --license MIT src/can.c Makefile
head -4 src/can.c Makefile
$R download --all && ls LICENSES && $R lint | tail -4
$R spdx | grep -E "^(FileName|LicenseInfoInFile|SPDXVersion)"
```

```text
Successfully changed header of src/can.c
Successfully changed header of Makefile
==> src/can.c <==
/*
 * SPDX-FileCopyrightText: 2026 Example Robotics Inc.
 *
 * SPDX-License-Identifier: MIT

==> Makefile <==
# SPDX-FileCopyrightText: 2026 Example Robotics Inc.
#
# SPDX-License-Identifier: MIT

Successfully downloaded LICENSES/MIT.txt.
MIT.txt
* Files with copyright information: 3 / 3
* Files with license information: 3 / 3

Congratulations! Your project is compliant with version 3.3 of the REUSE Specification :-)
SPDXVersion: SPDX-2.1
FileName: ./Makefile
LicenseInfoInFile: MIT
FileName: ./src/can.c
LicenseInfoInFile: MIT
FileName: ./src/imu.c
LicenseInfoInFile: MIT
```

출력에서 볼 것: `reuse annotate`가 파일 형식에 맞는 주석으로 태그를 넣는다. 저장소 구조(`LICENSES/<ID>.txt`)가 커널의 `LICENSES/`와 같은 발상이다. `reuse spdx`는 파일 단위 SPDX 문서를 만들지만 버전이 **SPDX-2.1**이다 — 도구마다 내보내는 SPDX 버전이 다르므로 SBOM을 합칠 때 확인해야 한다.

---

## 6. 빌드 시스템이 만들어 주는 것 (설명 — 이 노트에서 실행하지 않음)

손으로 SBOM을 쓰는 건 이해를 위한 것이고, 실제 제품은 빌드 시스템이 매 빌드마다 만든다. 빌드 시스템은 **각 패키지 레시피가 신고한 라이선스 메타데이터**를 모은다 — 즉 레시피의 라이선스 필드가 틀리면 SBOM도 틀린다.

| 기능 | Buildroot (K3) | Yocto / OpenEmbedded (K4 · K5) |
|---|---|---|
| 패키지 메타데이터 | `<PKG>_LICENSE`, `<PKG>_LICENSE_FILES`, `<PKG>_REDISTRIBUTE`, `<PKG>_CPE_ID_*` | `LICENSE`, `LIC_FILES_CHKSUM`(라이선스 파일의 md5 — 바뀌면 빌드 실패), `CVE_PRODUCT` |
| 라이선스 · 소스 모음 | `make legal-info` → `output/legal-info/`: `manifest.csv`, `host-manifest.csv`, `licenses/`, `sources/`, `buildroot.config`, `legal-info.sha256` | `tmp/deploy/licenses/<image>/license.manifest`, `archiver` 클래스로 소스 · 패치 아카이브 |
| 라이선스 정책 | (외부 스크립트로 manifest 검사) | `INCOMPATIBLE_LICENSE = "GPL-3.0* LGPL-3.0* AGPL-3.0*"` 로 빌드 단계에서 차단 |
| SBOM | `make show-info` → `utils/generate-cyclonedx` (CycloneDX) | `create-spdx` 클래스. 5.1(Styhead)부터 기본 출력이 SPDX 3.0 |
| CVE | `pkg-stats`(CVE 표시), CycloneDX + `support/scripts/cve-check` | `cve-check` 클래스(5.1에서 TEXT 출력 제거, JSON만), `vex` 클래스(외부 도구용 VEX 정보), 외부 `sbom-cve-check` |

몇 가지 메커니즘 포인트:

- Yocto의 `LIC_FILES_CHKSUM`은 upstream이 라이선스를 바꾸면(예: 새 버전에서 GPL-2.0 → GPL-3.0) 체크섬이 깨져 **빌드가 멈춘다**. "버전 올렸더니 라이선스가 바뀐 줄 몰랐다"를 막는 장치다.
- Yocto `INCOMPATIBLE_LICENSE`는 §7의 deny 목록을 빌드 시스템에 직접 넣는 방법이다. 금지 라이선스 레시피가 이미지에 들어가려 하면 빌드 에러가 난다.
- Buildroot `legal-info`의 `sources/`는 GPL 대응 소스 릴리스의 초안이 된다. 단, 우리 out-of-tree 패치 · 외부 트리(`BR2_EXTERNAL`) 처리를 확인해야 한다.
- Debian/Ubuntu 기반(K6, Jetson L4T)은 패키지마다 `/usr/share/doc/<pkg>/copyright`(DEP-5)가 있고 소스 패키지(`apt-get source`)로 대응 소스를 구할 수 있지만, 이미지 전체 SBOM은 별도 도구로 만들어야 한다.

(위 표는 각 프로젝트 매뉴얼 · 릴리스 노트 기반이다. Buildroot `generate-cyclonedx`와 Yocto 5.1의 SPDX 3.0 기본값, `vex` 클래스, cve-check TEXT 제거는 2026-10-17에 공식 문서로 확인했다. 실제 빌드 산출물은 K3 · K4 · K5 노트에서 본다.)

---

## 7. 회사의 컴플라이언스 워크플로

### 7.1 전체 흐름

```svg
<svg viewBox="0 0 680 430" xmlns="http://www.w3.org/2000/svg"><defs><marker id="ah7" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto-start-reverse"><path d="M0,0 L10,5 L0,10 z" fill="currentColor"/></marker></defs><rect x="20" y="30" width="200" height="70" rx="6" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/><text x="120.0" y="61.2" font-size="12" text-anchor="middle" font-weight="bold">① 의존성 추가 요청</text><text x="120.0" y="77.2" font-size="12" text-anchor="middle">개발자: 이 라이브러리 쓸게요</text><rect x="240" y="30" width="200" height="70" rx="6" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/><text x="340.0" y="61.2" font-size="12" text-anchor="middle" font-weight="bold">② OSRB 리뷰</text><text x="340.0" y="77.2" font-size="12" text-anchor="middle">allow / deny / review 목록</text><rect x="460" y="30" width="200" height="70" rx="6" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/><text x="560.0" y="61.2" font-size="12" text-anchor="middle" font-weight="bold">③ 빌드</text><text x="560.0" y="77.2" font-size="12" text-anchor="middle">legal-info / create-spdx</text><rect x="460" y="150" width="200" height="70" rx="6" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/><text x="560.0" y="181.2" font-size="12" text-anchor="middle" font-weight="bold">④ 산출물</text><text x="560.0" y="197.2" font-size="12" text-anchor="middle">SBOM + 라이선스 manifest</text><rect x="240" y="150" width="200" height="70" rx="6" fill="#d0564a" fill-opacity="0.12" stroke="#d0564a"/><text x="340.0" y="181.2" font-size="12" text-anchor="middle" font-weight="bold">⑤ CI 게이트</text><text x="340.0" y="197.2" font-size="12" text-anchor="middle">deny 라이선스 · 소스 누락</text><rect x="20" y="150" width="200" height="70" rx="6" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/><text x="120.0" y="181.2" font-size="12" text-anchor="middle" font-weight="bold">⑥ 출하 패키지</text><text x="120.0" y="197.2" font-size="12" text-anchor="middle">이미지 + 고지 + 소스 릴리스</text><line x1="222" y1="65" x2="238" y2="65" stroke="currentColor" stroke-width="1.4" marker-end="url(#ah7)"/><line x1="442" y1="65" x2="458" y2="65" stroke="currentColor" stroke-width="1.4" marker-end="url(#ah7)"/><line x1="560" y1="102" x2="560" y2="148" stroke="currentColor" stroke-width="1.4" marker-end="url(#ah7)"/><line x1="458" y1="185" x2="442" y2="185" stroke="currentColor" stroke-width="1.4" marker-end="url(#ah7)"/><line x1="238" y1="185" x2="222" y2="185" stroke="currentColor" stroke-width="1.4" marker-end="url(#ah7)"/><rect x="20" y="270" width="200" height="70" rx="6" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/><text x="120.0" y="301.2" font-size="12" text-anchor="middle" font-weight="bold">⑦ 출하 후 감시</text><text x="120.0" y="317.2" font-size="12" text-anchor="middle">SBOM × CVE 피드</text><rect x="240" y="270" width="200" height="70" rx="6" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/><text x="340.0" y="301.2" font-size="12" text-anchor="middle" font-weight="bold">⑧ VEX 판정</text><text x="340.0" y="317.2" font-size="12" text-anchor="middle">not_affected / 패치 필요</text><rect x="460" y="270" width="200" height="70" rx="6" fill="#d0564a" fill-opacity="0.12" stroke="#d0564a"/><text x="560.0" y="301.2" font-size="12" text-anchor="middle" font-weight="bold">⑨ 패치 릴리스</text><text x="560.0" y="317.2" font-size="12" text-anchor="middle">OTA + 새 SBOM + 새 소스</text><line x1="120" y1="222" x2="120" y2="268" stroke="currentColor" stroke-width="1.4" marker-end="url(#ah7)"/><line x1="222" y1="305" x2="238" y2="305" stroke="currentColor" stroke-width="1.4" marker-end="url(#ah7)"/><line x1="442" y1="305" x2="458" y2="305" stroke="currentColor" stroke-width="1.4" marker-end="url(#ah7)"/><path d="M560,342 L560,370 L660,370 L660,10 L560,10 L560,28" fill="none" stroke="currentColor" stroke-width="1.4" marker-end="url(#ah7)"/><text x="590.0" y="362.0" font-size="12" text-anchor="start">다음 릴리스</text><rect x="20" y="380" width="520" height="40" rx="6" fill="#888" fill-opacity="0.12" stroke="#888"/><text x="280.0" y="404.2" font-size="12" text-anchor="middle">⑤에서 FAIL이면 출하 금지 — k7_gate.py 예제가 바로 이 단계</text></svg>
```
그림 7 — 오픈소스 컴플라이언스 파이프라인. 개발자의 의존성 추가 요청부터 출하 후 CVE 대응까지 한 바퀴. ⑤의 CI 게이트가 출하를 막는 지점이다.

- **OSRB(Open Source Review Board)**: 법무 · 보안 · 엔지니어링 대표가 새 오픈소스 사용을 검토하는 조직(이름은 회사마다 다름). 엔지니어가 내는 정보: 컴포넌트, 버전, 라이선스(SPDX ID), 어떻게 결합하나(정적/동적 링크, 커널 모듈, 별도 프로세스), 수정하나, 출하하나.
- **허용/거부/검토 목록**: 예를 들어 allow = MIT · BSD · Apache-2.0 · ISC · Zlib, review = GPL-2.0 · LGPL-2.1 · MPL-2.0(소스 릴리스 절차 필요), deny = GPL-3.0 · LGPL-3.0 · AGPL(장비 펌웨어, secure boot 때문 — §1.3).
- **소스 릴리스 패키지**: 펌웨어 버전마다 GPL/LGPL 컴포넌트의 소스 + 패치 + 설정 + 빌드 방법 + 라이선스 텍스트를 묶은 tarball. 출하 시점에 만들어 최소 3년 이상 보관 · 게시한다.

### 7.2 실습 — SBOM으로 CI 라이선스 게이트 만들기

무엇을 확인하는 코드인지: §5.5에서 만든 SPDX JSON을 읽어, (1) deny 라이선스가 있으면 실패, (2) 소스 공개 대상 라이선스인데 릴리스 디렉터리에 소스 tarball이 없으면 실패. SPDX 식 파서는 spdx-tools가 의존하는 `license-expression` 라이브러리를 쓴다.

```python
#!/usr/bin/env python3
# k7_gate.py: CI license gate - read an SPDX 2.3 JSON SBOM, apply allow/deny policy,
# and check that every copyleft package has its source in the release directory.
import json, sys, glob, os
from license_expression import get_spdx_licensing

DENY = ('GPL-3.0', 'AGPL-', 'LGPL-3.0')            # prefix match (anti-tivoization)
SOURCE_REQUIRED = ('GPL-2.0', 'LGPL-2', 'MPL-2.0')  # must ship corresponding source
lic = get_spdx_licensing()

def keys(expr):
    if expr in ('NOASSERTION', 'NONE'):
        return []
    return lic.license_keys(lic.parse(expr))

sbom, release = json.load(open(sys.argv[1])), sys.argv[2]
bad = 0
for p in sbom['packages']:
    name, ver = p['name'], p.get('versionInfo', '?')
    expr = p.get('licenseConcluded', 'NOASSERTION')
    ks = keys(expr)
    if expr == 'NOASSERTION' and p.get('primaryPackagePurpose') != 'FIRMWARE':
        print(f'REVIEW  {name}-{ver}: license unknown'); bad += 1; continue
    if any(k.startswith(DENY) for k in ks):
        print(f'DENY    {name}-{ver}: {expr}'); bad += 1; continue
    if any(k.startswith(SOURCE_REQUIRED) for k in ks):
        src = glob.glob(os.path.join(release, f'{name}-{ver}*.tar.*'))
        if src:
            print(f'OK-SRC  {name}-{ver}: {expr} -> {os.path.basename(src[0])}')
        else:
            print(f'MISSING {name}-{ver}: {expr} needs source in {release}/'); bad += 1
        continue
    print(f'OK      {name}-{ver}: {expr}')
print(f'gate: {"FAIL" if bad else "PASS"} ({bad} problem(s))')
sys.exit(1 if bad else 0)
```

릴리스 디렉터리에 커널과 BusyBox 소스만 넣고(내용은 자리표시 파일) 돌린다.

```sh
cd /var/tmp/K7/k7_sbom && mkdir -p k7_release
for n in linux-6.18.55 busybox-1.37.0; do echo src > k7_release/$n.tar.xz; done
G="/var/tmp/K7/k7_venv/bin/python ~/.elinux_scratch/K7/k7_gate.py"
$G k7_image.spdx.json k7_release; echo rc=$?
```

```text
OK      k7-sensor-image-1.0: NOASSERTION
OK-SRC  linux-6.18.55: GPL-2.0-only WITH Linux-syscall-note -> linux-6.18.55.tar.xz
OK-SRC  busybox-1.37.0: GPL-2.0-only -> busybox-1.37.0.tar.xz
OK      k7-sensor-app-1.0.3: LicenseRef-ExampleRobotics-Proprietary
MISSING glibc-2.43: LGPL-2.1-or-later needs source in k7_release/
gate: FAIL (1 problem(s))
rc=1
```

glibc 소스를 넣고, 그다음엔 누군가 이미지에 bash 5.3(GPL-3.0-or-later)을 추가한 SBOM(`k7_image_bash.spdx.json` — 패키지 하나를 Python으로 덧붙인 것)으로 돌린다.

```sh
echo src > k7_release/glibc-2.43.tar.xz
echo "--- glibc source added"; $G k7_image.spdx.json k7_release; echo rc=$?
echo "--- someone added bash"; $G k7_image_bash.spdx.json k7_release; echo rc=$?
```

```text
--- glibc source added
OK      k7-sensor-image-1.0: NOASSERTION
OK-SRC  linux-6.18.55: GPL-2.0-only WITH Linux-syscall-note -> linux-6.18.55.tar.xz
OK-SRC  busybox-1.37.0: GPL-2.0-only -> busybox-1.37.0.tar.xz
OK      k7-sensor-app-1.0.3: LicenseRef-ExampleRobotics-Proprietary
OK-SRC  glibc-2.43: LGPL-2.1-or-later -> glibc-2.43.tar.xz
gate: PASS (0 problem(s))
rc=0
--- someone added bash
OK      k7-sensor-image-1.0: NOASSERTION
OK-SRC  linux-6.18.55: GPL-2.0-only WITH Linux-syscall-note -> linux-6.18.55.tar.xz
OK-SRC  busybox-1.37.0: GPL-2.0-only -> busybox-1.37.0.tar.xz
OK      k7-sensor-app-1.0.3: LicenseRef-ExampleRobotics-Proprietary
OK-SRC  glibc-2.43: LGPL-2.1-or-later -> glibc-2.43.tar.xz
DENY    bash-5.3: GPL-3.0-or-later
gate: FAIL (1 problem(s))
rc=1
```

출력에서 볼 것: 게이트가 "소스 누락"과 "금지 라이선스 유입"을 둘 다 잡고 종료 코드로 CI를 실패시킨다. 실제 회사 게이트는 여기에 (1) Concluded가 NOASSERTION인 패키지 → OSRB 리뷰 티켓 자동 생성, (2) 소스 tarball의 해시를 SBOM의 다운로드 정보와 대조, (3) 고지문 모음에 모든 라이선스 텍스트가 있는지 확인 등을 더한다. 이 스크립트는 교육용 최소판이다(예: `GPL-2.0-only OR MIT` 같은 dual은 MIT를 고를 수 있는데 이 스크립트는 소스 요구로 처리한다 — 보수적인 쪽으로 틀린다).

### 7.3 규제가 SBOM을 요구하는 이유 (확인된 것만)

- **미국 EO 14028 (2021-05-12)**: 연방정부에 소프트웨어를 납품하는 회사의 공급망 보안 강화를 지시했고, NTIA 최소 요소(2021-07)가 나왔다. 이후 OMB 지침 · 후속 행정명령으로 세부가 여러 번 바뀌었으므로 납품 계약별로 최신 요구를 확인해야 한다.
- **EU Cyber Resilience Act — Regulation (EU) 2024/2847**: 2024-12-10 발효. 디지털 요소가 있는 제품(임베디드 장비 포함)의 제조사에게 보안 요구를 부과한다. **취약점 · 사고 보고 의무는 2026-09-11부터**, 대부분의 의무는 **2027-12-11부터** 적용된다. 부속서 I Part II(취약점 처리 요구)에 "흔히 쓰이는 기계 판독 형식으로, 적어도 최상위 의존성을 다루는 SBOM을 작성"하는 요구가 있다. SBOM은 공개 의무가 아니라 시장 감시 당국 요청 시 제공하는 기술 문서의 일부다.
- **미국 FDA**: 의료기기(사이버 기기)는 2023년부터 시판 전 제출에 SBOM이 요구된다(FD&C Act 524B).

엔지니어 관점의 결론: 규제는 "SBOM 파일을 만들어라"보다 **"제품 수명 동안 취약점을 추적하고 고쳐라"** 를 요구하고, SBOM은 그 수단이다. 그래서 SBOM은 빌드마다 자동 생성되고, 펌웨어 버전과 1:1로 보관되고, CVE 피드와 계속 대조돼야 의미가 있다.

---

## 8. 커널 안에서는 무슨 일이 일어나나

모듈 라이선스 검사가 커널 코드 어디서 일어나는지 따라간다. 줄 번호는 6.18.55 기준이다.

**1단계 — export 표시 (컴파일)**: `EXPORT_SYMBOL_GPL(ktime_get)`은 `include/linux/export.h`에서 이렇게 펼쳐진다.

```text
89:#define EXPORT_SYMBOL(sym)		_EXPORT_SYMBOL(sym, "")
90:#define EXPORT_SYMBOL_GPL(sym)		_EXPORT_SYMBOL(sym, "GPL")
91:#define EXPORT_SYMBOL_NS(sym, ns)	__EXPORT_SYMBOL(sym, "", ns)
92:#define EXPORT_SYMBOL_NS_GPL(sym, ns)	__EXPORT_SYMBOL(sym, "GPL", ns)
```

6.x 커널에서 `__EXPORT_SYMBOL`은 `.export_symbol` 섹션에 `(license 문자열, namespace, 심볼 주소)`를 기록한다. modpost가 이 섹션을 읽어 심볼마다 `is_gpl_only`를 정하고, `Module.symvers`의 4번째 열(`EXPORT_SYMBOL_GPL`)과 최종 심볼 테이블 섹션을 만든다.

**2단계 — 링크 시 테이블 분리**: 링커 스크립트가 GPL 심볼을 별도 섹션에 모은다.

```text
include/asm-generic/vmlinux.lds.h:502:	__ksymtab_gpl     : AT(ADDR(__ksymtab_gpl) - LOAD_OFFSET) {	\
include/asm-generic/vmlinux.lds.h:503:		__start___ksymtab_gpl = .;				\
include/asm-generic/vmlinux.lds.h:504:		KEEP(*(SORT(___ksymtab_gpl+*)))				\
include/asm-generic/vmlinux.lds.h:505:		__stop___ksymtab_gpl = .;				\
```

vmlinux에는 `__ksymtab`(일반)과 `__ksymtab_gpl`(GPL-only) 두 테이블이 있고, 모듈도 자기 export를 같은 이름의 섹션으로 갖는다(`mod->gpl_syms = section_objs(info, "__ksymtab_gpl", ...)`, main.c:2625).

**3단계 — 로드 시 taint (`module_augment_kernel_taints`)**: §4.5의 dmesg 메시지가 나오는 곳이다.

```c
/* kernel/module/main.c 1750~1760 (6.18.55) */
static void module_license_taint_check(struct module *mod, const char *license)
{
	if (!license)
		license = "unspecified";

	if (!license_is_gpl_compatible(license)) {
		if (!test_taint(TAINT_PROPRIETARY_MODULE))
			pr_warn("%s: module license '%s' taints kernel.\n",
				mod->name, license);
		add_taint_module(mod, TAINT_PROPRIETARY_MODULE,
				 LOCKDEP_NOW_UNRELIABLE);
	}
}
```

`MODULE_LICENSE`가 아예 없으면 `"unspecified"`로 취급돼 역시 P taint다. 같은 함수 묶음에서 `intree`가 없으면 `TAINT_OOT_MODULE`(O), 서명 검증 실패면 `TAINT_UNSIGNED_MODULE`(E)이 붙는다. `add_taint_module()`은 전역 taint 마스크(`/proc/sys/kernel/tainted`)와 모듈별 마스크(`/sys/module/<m>/taint`, `lsmod`의 `(PO)`)를 둘 다 세운다.

**4단계 — 심볼 해석 (`resolve_symbol` → `find_symbol`)**: 독점 모듈이면 `gplok = false`로 검색한다.

```c
/* kernel/module/main.c 362~375 (6.18.55) */
static bool find_exported_symbol_in_section(const struct symsearch *syms,
					    struct module *owner,
					    struct find_symbol_arg *fsa)
{
	struct kernel_symbol *sym;

	if (!fsa->gplok && syms->license == GPL_ONLY)
		return false;

	sym = bsearch(fsa->name, syms->start, syms->stop - syms->start,
			sizeof(struct kernel_symbol), cmp_name);
	if (!sym)
		return false;
```

GPL-only 테이블은 아예 검색하지 않으니 `ktime_get`은 "없는 심볼"이 되고, `simplify_symbols()`(main.c:1513, 메시지는 1566줄)가 "Unknown symbol ktime_get (err -2)"를 찍는다(§4.6). 테이블은 이름순 정렬돼 있어 `bsearch`로 찾는다 — Don이 펌웨어에서 쓰던 정렬된 심볼 테이블 lookup과 같다.

드라이버를 쓸 때 다시 만나는 지점: E4(모듈 기초 — `MODULE_LICENSE` 필수), G 모듈(platform driver 등록이 GPL-only), H 모듈(regmap · GPIO · DMA engine API 대부분 GPL-only), L 모듈(모듈 서명 — `CONFIG_MODULE_SIG_FORCE`면 서명 안 된 모듈은 아예 거부, E taint), M 모듈(oops 로그 첫 줄의 `Tainted:` 플래그 읽기).

---

## 9. Embedded 실무에서 다시 보기

**장면 1 — BSP 벤더 커널을 받았다.** SoC 벤더(i.MX, TI, Jetson)가 준 커널 트리는 upstream + 수천 개 패치다. 출하하면 그 패치 전체가 소스 릴리스 대상이다. 벤더가 바이너리로만 준 `.ko`가 있다면 `modinfo -F license`로 라이선스부터 본다. `Proprietary`면 그 모듈의 재배포 조건(벤더 계약)과 taint를 법무 · 고객 지원팀에 알린다.

```sh
for k in $(find /lib/modules/$(uname -r) -name "*.ko*"); do
  l=$(modinfo -F license "$k"); case "$l" in GPL*|"Dual "*) ;; *) echo "$l  $k";; esac
done
```

VM 호스트 커널(Ubuntu `7.0.0-38-generic`, 모듈 8544개)에서 돌린 실제 결과다.

```text
CDDL
Dual BSD/GPL
Dual MIT/GPL  /lib/modules/7.0.0-38-generic/ubuntu/dkms/zfs/zfs.ko.zst
```

출력에서 볼 것: 걸린 건 하나, **ZFS**다. `modinfo -F license`가 세 줄을 돌려줘서(`MODULE_LICENSE`가 여러 개) 출력이 세 줄로 찍혔다. ZFS는 CDDL(Sun의 copyleft 라이선스)이고 GPL-2.0과의 호환성이 오래된 논쟁거리라 upstream 커널에 들어가지 못하고 DKMS로 따로 빌드된다. 커널 로더의 `get_modinfo()`는 `.modinfo`에서 처음 찾은 `license=`만 보고, `modinfo`가 보여 주는 첫 값이 CDDL이므로 로드하면 P taint가 날 것이다(이 VM에서 zfs는 로드돼 있지 않아 `/proc/sys/kernel/tainted`는 0 — 로드 실험은 하지 않았다). 벤더 BSP 이미지에서 돌리면 독점 GPU · ISP 모듈이 이런 식으로 나온다.

**장면 2 — 필드 크래시 로그의 `Tainted: P O`.** 고객 로봇의 oops 로그에 `Tainted: P           O`가 있다. "P"는 독점 모듈(예: 벤더 GPU 드라이버)이 로드된 적 있다는 뜻이고, upstream에 버그를 보고해도 대개 "독점 모듈 없이 재현해 달라"는 답이 온다. 재현 계획에 "독점 모듈 제외 부팅"을 넣는다.

**장면 3 — 고객사(자동차 · 국방)가 SBOM을 요구한다.** Scout AI 같은 국방 로보틱스 회사는 정부 고객에게 SBOM을 납품 산출물로 내야 할 수 있다. 펌웨어 릴리스 파이프라인에 (1) 빌드 시스템 SBOM 생성, (2) NTIA 체커 · 스키마 검증, (3) 라이선스 게이트, (4) CVE 스캔 결과 + VEX를 넣고, 릴리스 태그마다 SBOM을 아카이브한다.

**장면 4 — "로그 수집용으로 이 GPLv3 도구 넣어도 돼요?"** 개발 편의로 디버그 이미지에 넣는 건 사내 사용이라 대개 문제없다. 그러나 같은 레시피가 양산 이미지에 섞이면 secure boot 정책과 충돌한다. Yocto라면 양산 DISTRO에 `INCOMPATIBLE_LICENSE`를 걸어 **빌드 단계에서** 막는 게 사람의 주의력보다 확실하다.

---

## 10. 흔한 실수와 증상

| 실수 | 증상 | 원인 | 고치는 법 |
|---|---|---|---|
| upstream tarball 링크만 소스로 제공 | 커뮤니티 · 고객의 GPL 위반 지적 | 우리 패치 · `.config` · 빌드 스크립트 누락 | 출하 시 legal-info/archiver로 소스 릴리스 패키지 생성, 버전별 보관 |
| 바이너리 출하물에 BSD/MIT 고지 누락 | 감사에서 지적 | permissive는 의무 없다고 착각 | 라이선스 텍스트 모음을 문서/UI에 포함, 빌드에서 자동 수집 |
| 독점 모듈에서 GPL-only 심볼 사용 | modpost `ERROR: GPL-incompatible module ... uses GPL-only symbol` | `EXPORT_SYMBOL_GPL` API 호출 | 모듈을 GPL로 공개하거나 로직을 유저스페이스로 이동 |
| `MODULE_LICENSE` 누락 | 로드 시 `module license 'unspecified' taints kernel` + GPL-only 심볼 Unknown symbol | 매크로 빠뜨림 | `MODULE_LICENSE("GPL")` 추가(트리 모듈 규칙) |
| 독점 앱을 glibc에 정적 링크 | LGPL relink 의무 발생(앱 오브젝트 제공) | `-static` 습관 | 동적 링크, 또는 musl(MIT) 사용 |
| 양산 이미지에 GPLv3 유틸 유입 | secure boot 정책과 GPLv3 설치 정보 요구 충돌 | 디버그 패키지가 양산 이미지에 섞임 | `INCOMPATIBLE_LICENSE` / CI 라이선스 게이트 |
| SBOM에 "GPLv2" 같은 비표준 이름 | pyspdxtools `Unrecognized license reference` | SPDX ID 미사용 | SPDX 목록 ID 사용, 사내 라이선스는 `LicenseRef-` |
| SBOM에 공급자 · 버전 누락 | NTIA 체커 `Conformant: False` | 자동 생성 메타데이터 부족 | 레시피 메타데이터 보강, CI에서 체커 실행 |
| CPE/PURL 부정확 | CVE 매칭 누락(미탐) 또는 폭주(오탐) | 식별자를 손으로 대충 씀 | 빌드 시스템의 `CVE_PRODUCT`/`_CPE_ID` 정비, VEX로 오탐 판정 기록 |
| SBOM을 한 번 만들고 방치 | 새 CVE에 영향 제품을 못 찾음 | 펌웨어 버전과 SBOM 연결 안 됨 | 빌드마다 생성 · 버전별 보관 · CVE 피드와 주기 대조 |

---

## 11. 면접에서 이렇게 말한다

**Q.** Can we ship a proprietary kernel module in our product?

**A.** 기술적으로는 가능하지만 제약이 크다. `MODULE_LICENSE("Proprietary")` 모듈은 `EXPORT_SYMBOL_GPL` 심볼을 쓸 수 없어서 modpost가 빌드를 막고, 우회해도 로더가 GPL-only 테이블을 검색하지 않아 Unknown symbol로 실패한다. 로드되면 커널이 P taint되고 lockdep이 꺼진다. platform driver 등록이나 GPIO descriptor 같은 핵심 API가 GPL-only라 현대 드라이버는 사실상 GPL이어야 한다. 모듈이 파생 저작물인지는 법적으로 미확정이라 최종 판단은 법무 몫이고, 보통 권장은 커널 모듈은 GPL로 공개하고 비밀 로직은 유저스페이스나 펌웨어로 옮기는 것이다.

> Technically yes, but with heavy constraints. A module with MODULE_LICENSE("Proprietary") can't use EXPORT_SYMBOL_GPL symbols — modpost fails the build with "GPL-incompatible module uses GPL-only symbol", and even if you patch the license string, the loader skips the GPL-only symbol table so you get "Unknown symbol". Loading it taints the kernel with P, disables lockdep, and upstream won't look at your bug reports. Since core APIs like platform driver registration and gpiod are GPL-only, a modern driver realistically has to be GPL. Whether a module is a derivative work is legally unsettled, so legal makes the final call; the usual engineering answer is: GPL the kernel module and keep proprietary logic in user space or firmware.

**꼬리 질문**: "Tainted: P O에서 O는?" → out-of-tree 모듈. GPL 모듈이라도 트리 밖에서 빌드하면 붙는다(실험에서 4096).

**Q.** What do we owe customers if we ship BusyBox?

**A.** BusyBox는 GPL-2.0-only다. 바이너리를 기기에 넣어 팔면 (1) 우리가 실제로 빌드한 BusyBox 버전의 대응 소스 전체 — 우리 패치와 `.config`, 빌드 스크립트까지, (2) GPL 원문과 저작권 고지, (3) 소스를 바이너리와 함께 주거나 3년 이상 유효한 서면 제안을 줘야 한다. 수정하지 않았어도 의무는 같다. BusyBox는 실제 집행 소송이 있었던 프로젝트라 legal-info 같은 빌드 산출물로 소스 릴리스를 출하 시점에 만들어 둔다.

> BusyBox is GPL-2.0-only. If we ship it on a device, we owe the complete corresponding source for the exact version we built — including our patches, our .config and the build scripts — plus the GPL text and copyright notices, delivered either with the device or via a written offer valid for at least three years. That applies even if we didn't modify it. In practice we generate the source release at ship time from the build system — Buildroot legal-info or Yocto's archiver — and keep it per firmware version.

**Q.** Why do many embedded companies avoid GPLv3 in their firmware?

**A.** GPLv3 6조의 Installation Information 때문이다. 소비자 제품 같은 "User Product"에 GPLv3 코드를 넣으면 사용자가 수정한 버전을 그 기기에 설치해 돌릴 수 있게 하는 정보(서명 키나 해제 절차)까지 줘야 한다. secure boot로 서명된 이미지만 부팅하게 잠그는 장비와 충돌한다. 그래서 bash 대신 BusyBox ash를 쓰고, Yocto `INCOMPATIBLE_LICENSE`나 CI 게이트로 GPL-3.0 · LGPL-3.0을 막는다.

> Because of GPLv3's "Installation Information" requirement — the anti-tivoization clause. If GPLv3 code ships in a User Product, you must provide whatever is needed for the user to install and run a modified version on that device, like signing keys or an unlock procedure. That conflicts with locked-down secure boot. So teams use BusyBox ash instead of bash and block GPL-3.0 and LGPL-3.0 at build time with INCOMPATIBLE_LICENSE or a CI license gate.

**Q.** Our app links against glibc. Do we have to open-source the app?

**A.** 아니다. glibc는 LGPL-2.1-or-later이고, 동적 링크면 앱은 공개 의무가 없다. 의무는 glibc 소스(+ 수정분) 제공, 고지, 사용자가 라이브러리를 바꿔 끼울 수 있게 하는 것, 역공학 금지 조항을 넣지 않는 것이다. 정적 링크면 relink할 수 있게 앱 오브젝트 파일을 줘야 하니, 정적 바이너리가 필요하면 MIT인 musl을 쓴다. 커널 쪽은 Linux-syscall-note가 syscall 경계를 명시한다.

> No. glibc is LGPL-2.1-or-later; with dynamic linking the app stays proprietary. We must provide glibc's source and any modifications, include the notices, allow users to replace the library, and not forbid reverse engineering for that purpose. If we link statically we'd have to provide our object files so users can relink, so for static binaries we use musl, which is MIT. On the kernel side, the Linux-syscall-note explicitly says user programs using syscalls aren't derivative works.

**Q.** What is an SBOM and what goes in it?

**A.** 소프트웨어 부품표다. 컴포넌트마다 공급자, 이름, 버전, PURL/CPE 같은 고유 식별자, 의존 관계, 그리고 문서 작성자와 타임스탬프 — 이게 NTIA 최소 요소 7개다. 실무에서는 라이선스(SPDX ID)와 해시도 넣는다. 형식은 SPDX(라이선스 출발, ISO 표준)와 CycloneDX(보안 출발, VEX 내장). 빌드마다 자동 생성해서 펌웨어 버전과 묶어 보관해야 CVE 대응에 쓸 수 있다.

> A software bill of materials: for each component, the supplier, name, version, a unique identifier like a PURL or CPE, and dependency relationships, plus the SBOM author and a timestamp — those are the seven NTIA minimum elements. In practice we also record SPDX license IDs and hashes. The two formats are SPDX, which came from license compliance and is an ISO standard, and CycloneDX, which came from security and carries VEX data. It's only useful if it's generated on every build and archived per firmware version, so we can answer "which shipped devices contain this vulnerable version" quickly.

**Q.** A new kernel CVE drops. How do you figure out if your product is affected?

**A.** SBOM에서 해당 펌웨어 버전들의 커널 버전(CPE)을 찾고, CVE의 영향 범위와 비교한다. 대부분의 커널 CVE는 특정 드라이버 · 서브시스템에 있으니 우리 `.config`에서 그 코드가 빌드되는지 확인한다. 안 되면 VEX에 `not_affected / code_not_present`로 근거와 함께 남기고, 되면 stable 백포트 패치 → OTA → 새 SBOM으로 간다. Yocto면 cve-check/vex 클래스, Buildroot면 pkg-stats나 CycloneDX + cve-check 스크립트로 1차 매칭을 자동화한다.

> First, use the SBOMs to find which firmware versions ship which kernel version, and match against the CVE's affected range. Most kernel CVEs are in specific drivers, so I check whether that code is even built in our .config. If not, I record a VEX statement — not_affected, code_not_present — with the reasoning. If it is, we pick up the stable backport, ship an OTA, and publish a new SBOM. The first-pass matching is automated with Yocto's cve-check and vex classes or Buildroot's pkg-stats and CycloneDX tooling.

**Q.** What does the EU Cyber Resilience Act mean for embedded engineers?

**A.** 2024년 12월 발효된 EU 규정으로, 디지털 요소가 있는 제품 제조사에 보안 요구를 건다. 실제로 악용된 취약점 보고 의무는 2026년 9월부터, 나머지 대부분은 2027년 12월부터다. 취약점 처리 요구에 최소 최상위 의존성을 담은 기계 판독 SBOM이 들어 있다. 공개 의무는 아니고 당국 요청 시 제출한다. 엔지니어에게는 빌드마다 SBOM, CVE 모니터링, 보안 업데이트 배포 능력이 제품 요구사항이 된다는 뜻이다. 세부 해석은 법무와 확인한다.

> The CRA is an EU regulation that entered into force in December 2024 and puts security obligations on manufacturers of products with digital elements, which includes embedded devices. Reporting of actively exploited vulnerabilities applies from September 2026 and most other obligations from December 2027. The vulnerability-handling requirements include a machine-readable SBOM covering at least top-level dependencies — not necessarily public, but available to authorities on request. For us it means SBOM generation per build, CVE monitoring, and the ability to ship security updates become product requirements. Legal interprets the details.

---

## 12. 직접 해보기

1. **개념**: GPL-2.0-only 커널에 GPL-2.0-or-later 파일과 `GPL-2.0 OR MIT` 파일이 섞여 있다. 커널 바이너리 전체는 어떤 조건으로 배포되나? 정답/힌트: 결합물은 모든 조건을 만족해야 하므로 GPL-2.0-only. or-later 파일은 2판을 선택한 것, dual 파일은 GPL 쪽을 선택한 것으로 보면 된다.
2. **개념**: 우리 앱이 LGPL 라이브러리를 `dlopen()`으로 쓰는 경우와 정적 링크하는 경우, 소스 릴리스에 넣어야 하는 것의 차이는? 정답/힌트: 둘 다 라이브러리 소스는 필수. 정적이면 relink 가능한 앱 오브젝트까지.
3. **코드**: §4의 `k7_prop_ok.c`에 `EXPORT_SYMBOL`로 함수 하나를 내보내고, 그 함수를 부르는 GPL 모듈을 만들어 순서대로 insmod해 보라. dmesg에 무엇이 나오나? 정답/힌트: `kernel/module/main.c` 1217줄 — GPL-only 심볼을 쓰는 모듈이 독점 모듈의 심볼을 쓰는 경우의 거부 경로를 읽어 보고, 두 번째 모듈이 GPL-only 심볼을 쓸 때만 거부되는지 확인한다.
4. **관찰**: 랩 게스트에서 `cat /proc/sys/kernel/tainted`를 부팅 직후, GPL out-of-tree 모듈 로드 후에 각각 보고, `echo 1 > /proc/sys/kernel/tainted`로 무엇이 바뀌는지 본다(게스트 안에서만!). 정답/힌트: 사용자가 비트를 켤 수는 있어도 끌 수는 없다.
5. **SBOM**: §5.4의 SPDX 문서에 U-Boot(`GPL-2.0-or-later`)와 TF-A(`BSD-3-Clause`)를 추가하고 pyspdxtools와 ntia-checker를 통과시켜라. 체크섬은 `sha256sum /var/lab/out/u-boot.bin /var/lab/out/flash.bin`. 정답/힌트: `PrimaryPackagePurpose: FIRMWARE`, `Relationship: SPDXRef-Image CONTAINS ...` 추가를 잊지 말 것.
6. **게이트**: `k7_gate.py`가 `GPL-2.0-only OR MIT` 같은 dual 식에서 permissive 쪽을 고를 수 있으면 소스 요구를 면제하도록 고쳐라. 정답/힌트: `lic.parse(expr)`의 결과에서 OR 노드를 찾아, 피연산자 중 하나라도 allow 목록이면 OK로 처리한다.
7. **스캔**: 랩 커널 트리에서 SPDX 태그가 없는 `.c` 파일을 디렉터리별로 세어 상위 5개를 뽑아라. 그 파일들은 어떤 라이선스 문구를 쓰고 있나? 정답/힌트: `k7_spdx_stats.py`를 고쳐 untagged 파일 경로를 모은다. `drivers/gpu/drm/drm_drv.c`처럼 옛 형식 MIT 문구가 많다.

---

## 13. 용어 사전

| 용어 | 뜻 | 한 줄 설명 |
|---|---|---|
| copyleft | 상호주의 라이선스 | 배포 시 같은 조건으로 소스를 주라는 조건 |
| permissive | 허용형 라이선스 | 고지만 지키면 되는 라이선스(MIT · BSD · Apache-2.0) |
| complete corresponding source | 대응 소스 전체 | 바이너리를 다시 만들 수 있는 소스 + 설정 + 빌드 스크립트 |
| written offer | 서면 제안 | 요청 시 소스를 주겠다는 3년 이상 유효한 약속 |
| Installation Information | 설치 정보 | GPLv3가 User Product에 요구하는, 수정본을 설치하는 데 필요한 정보 |
| relink | 재링크 | LGPL 라이브러리를 바꿔 앱과 다시 결합하는 것 |
| Linux-syscall-note | syscall 예외 | syscall로 커널을 쓰는 유저 프로그램은 GPL 대상이 아니라는 커널 예외 |
| SPDX License Identifier | SPDX 라이선스 ID | `GPL-2.0-only` 같은 표준 라이선스 이름 · 식 |
| MODULE_LICENSE | 모듈 라이선스 매크로 | `.modinfo`에 license 문자열을 넣는 매크로 |
| EXPORT_SYMBOL_GPL | GPL 전용 export | GPL 호환 모듈만 쓸 수 있게 심볼을 내보내는 매크로 |
| modpost | 모듈 후처리 도구 | 모듈 링크 단계에서 심볼 · 라이선스를 검사하는 빌드 도구 |
| taint | 커널 오염 표시 | 독점 · 트리 밖 모듈 등 신뢰성을 떨어뜨리는 사건의 비트 마스크 |
| SBOM | 소프트웨어 부품표 | 제품에 든 컴포넌트 · 버전 · 공급자 · 관계 목록 |
| SPDX | SBOM 표준 | Linux Foundation, ISO/IEC 5962, 라이선스 중심 |
| CycloneDX | SBOM 표준 | OWASP, ECMA-424, 보안 중심, VEX 내장 |
| PURL | Package URL | `pkg:pypi/pyserial@3.5` 형태의 패키지 식별자 |
| CPE | Common Platform Enumeration | NVD가 CVE와 제품을 매칭하는 식별자 |
| VEX | 취약점 악용 가능성 교환 | "이 CVE는 우리 제품에 해당 없음" 같은 판정 기록 |
| NTIA minimum elements | NTIA 최소 요소 | 2021년 미국 NTIA가 정한 SBOM 필수 7개 데이터 필드 |
| OSRB | 오픈소스 검토 위원회 | 사내 오픈소스 사용을 승인하는 법무 · 엔지니어링 조직 |
| WHENCE | linux-firmware 목록 파일 | 펌웨어 블롭별 드라이버 · 라이선스 정보 |
| CRA | EU Cyber Resilience Act | 디지털 제품 제조사에 보안 · SBOM 요구를 하는 EU 규정 |

---

## 14. 요약 & 체크리스트

Embedded Linux 이미지는 대부분 오픈소스이고, 각 컴포넌트가 다른 조건으로 사용을 허락한다. 의무는 장비를 **배포**하는 순간 생긴다: permissive는 고지, LGPL은 라이브러리 소스와 교체 가능성, GPL은 우리 패치 · `.config` · 빌드 스크립트까지 포함한 대응 소스 전체와 3년 서면 제안, GPLv3는 secure boot와 충돌하는 설치 정보까지. 커널은 `MODULE_LICENSE`와 `EXPORT_SYMBOL_GPL`로 선을 긋는다 — 독점 모듈이 GPL-only 심볼을 쓰면 modpost가 빌드를 막고, 우회해도 로더가 심볼 바인딩을 거부하며, 로드만 해도 P taint가 남는다. SBOM은 이 모든 정보를 기계가 읽는 형식(SPDX · CycloneDX)으로 남긴 부품표이고, NTIA 최소 요소와 스키마로 검증하고, CI 게이트로 출하를 막고, CVE · VEX로 출하 후까지 쓴다. 엔지니어의 역할은 정확한 데이터를 만드는 것이고, 해석은 법무가 한다.

- [ ] permissive · weak copyleft · strong copyleft · AGPL의 경계와 의무를 표로 설명할 수 있다
- [ ] GPL-2.0-only / -or-later / GPL-3.0의 차이와 GPLv3 Installation Information이 secure boot와 충돌하는 이유를 설명할 수 있다
- [ ] LGPL 동적 링크와 정적 링크의 의무 차이를 `readelf -d`로 보여 줄 수 있다
- [ ] GPL 컴포넌트 출하 시 소스 릴리스에 넣을 것(패치 · `.config` · 빌드 스크립트)과 서면 제안 3년을 말할 수 있다
- [ ] 커널 COPYING의 `GPL-2.0 WITH Linux-syscall-note`와 uapi 헤더의 의미를 설명할 수 있다
- [ ] `MODULE_LICENSE` 값, `EXPORT_SYMBOL_GPL`, modpost 에러, P/O taint를 실제 출력으로 설명할 수 있다
- [ ] modpost를 우회해도 커널 로더가 GPL-only 심볼을 거부하는 코드 경로(`find_exported_symbol_in_section`)를 짚을 수 있다
- [ ] SPDX 2.3 SBOM을 손으로 쓰고 pyspdxtools · ntia-checker로 검증할 수 있다
- [ ] SPDX와 CycloneDX의 차이, PURL · CPE · VEX의 역할을 설명할 수 있다
- [ ] Buildroot legal-info와 Yocto create-spdx · INCOMPATIBLE_LICENSE · cve-check가 무엇을 만드는지 말할 수 있다
- [ ] OSRB → 빌드 → SBOM → CI 게이트 → 소스 릴리스 → CVE 감시 파이프라인을 그릴 수 있다
- [ ] EU CRA의 적용 일정과 SBOM 요구를 과장 없이 말할 수 있다

---

## 참고 자료

- 커널 문서: [Linux kernel licensing rules](https://docs.kernel.org/process/license-rules.html) — SPDX 태그 규칙, `LICENSES/` 구조, `MODULE_LICENSE` 값
- 커널 문서: [Tainted kernels](https://docs.kernel.org/admin-guide/tainted-kernels.html) — taint 비트 표와 `kernel-chktaint`
- 소스(6.18.55): `COPYING`, `LICENSES/`, `include/linux/license.h`, `include/linux/module.h`(193~240줄), `include/linux/export.h`, `kernel/module/main.c`(`module_license_taint_check`, `module_augment_kernel_taints`, `find_exported_symbol_in_section`), `scripts/mod/modpost.c`, `scripts/spdxcheck.py`, `tools/debugging/kernel-chktaint` — [elixir.bootlin.com](https://elixir.bootlin.com/linux/v6.18.55/source)에서 같은 경로로 볼 수 있다
- 라이선스 원문: [GPL-2.0](https://www.gnu.org/licenses/old-licenses/gpl-2.0.html), [GPL-3.0](https://www.gnu.org/licenses/gpl-3.0.html), [LGPL-2.1](https://www.gnu.org/licenses/old-licenses/lgpl-2.1.html), [Apache-2.0](https://www.apache.org/licenses/LICENSE-2.0), [MPL-2.0](https://www.mozilla.org/en-US/MPL/2.0/)
- [SPDX License List](https://spdx.org/licenses/), [SPDX 2.3 specification](https://spdx.github.io/spdx-spec/v2.3/), [SPDX 3.0 specification](https://spdx.github.io/spdx-spec/v3.0.1/)
- [CycloneDX specification](https://cyclonedx.org/specification/overview/)
- [NTIA — The Minimum Elements For a Software Bill of Materials (2021)](https://www.ntia.gov/report/2021/minimum-elements-software-bill-materials-sbom)
- [Regulation (EU) 2024/2847 (Cyber Resilience Act)](https://eur-lex.europa.eu/eli/reg/2024/2847/oj/eng)
- [REUSE specification and tool](https://reuse.software/)
- Buildroot 매뉴얼 — legal-info, show-info/generate-cyclonedx, pkg-stats: [buildroot.org/downloads/manual/manual.html](https://buildroot.org/downloads/manual/manual.html)
- Yocto Project — [Release notes 5.1 (Styhead)](https://docs.yoctoproject.org/migration-guides/release-notes-5.1.html)(SPDX 3.0 기본값, vex 클래스, cve-check 변경), Yocto Project Development Tasks Manual의 "Working With Licenses", "Creating a Software Bill of Materials" 절
- 같은 모듈: K3(Buildroot), K4 · K5(Yocto), K6(Debian/L4T). 관련: E4(모듈 기초), L(secure boot · 모듈 서명), M(oops의 Tainted 플래그)
