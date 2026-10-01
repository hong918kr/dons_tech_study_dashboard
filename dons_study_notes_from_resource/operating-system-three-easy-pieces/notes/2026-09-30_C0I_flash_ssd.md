# 부록 I Flash 기반 SSD — NAND 물리, FTL, GC, 매핑 테이블, wear leveling

> 📖 원문: [I. Flash-based SSDs](../book-md/C0I_flash_based_ssds.md) · [PDF p.656](../Operating%20Systems%20-%20Three%20Easy%20Pieces.pdf#page=656) · ⏱️ 읽기 약 60분 · 🔗 선행: [Ch.36 I/O 장치](2026-09-30_C36_io_devices.md), [Ch.37 HDD](2026-09-30_C37_hard_disk_drives.md), [Ch.38 RAID](2026-09-30_C38_raid.md) · 관련: [Ch.43 LFS](2026-09-30_C43_lfs.md)

## 0. 한눈에 보기

> **CRUX: HOW TO BUILD A FLASH-BASED SSD** — "How can we build a flash-based SSD? How can we handle the expensive nature of erasing? How can we build a device that lasts a long time, given that repeated overwrite will wear the device out? Will the march of progress in technology ever cease? Or cease to amaze?"
> (플래시 SSD 를 어떻게 만들까? 비싼 erase 를 어떻게 다룰까? 덮어쓰기가 장치를 닳게 하는데 어떻게 오래 쓰게 할까?)

- NAND 의 세 가지 이상한 규칙: **읽기·쓰기 단위는 page(수~16KB), 지우기 단위는 block(수백 KB~수십 MB)**, **지운(erased) page 에만 쓸 수 있다(program, 1→0 만)**, **block 은 P/E 를 수천 번 하면 닳는다**.
- 그래서 SSD 안의 펌웨어 **FTL** 이 블록 인터페이스(LBA 읽기/쓰기)를 흉내 낸다. 가장 단순한 direct-mapped FTL 은 4KB 쓰기마다 블록 전체를 read-erase-program 해서 **HDD 보다 느리다**. 현대 FTL 은 **log-structured**: 쓰기는 항상 다음 빈 page 에 append, 위치는 **L2P 매핑 테이블** 에 기록.
- 대가 두 가지: (1) 덮어쓰기로 생긴 **쓰레기(garbage)** 를 치우는 **GC** → **write amplification(WAF)**, over-provisioning 으로 완화. (2) **매핑 테이블 크기**(1TB 당 ~1GB DRAM) → block 매핑, hybrid 매핑, 캐시된 매핑(DFTL).
- 그리고 **wear leveling**: 오래 안 바뀌는 cold 데이터가 앉아 있는 블록도 주기적으로 옮겨서 모든 블록이 비슷하게 닳게 한다.

## 1. 5분 복습표

| 용어 | 한 줄 뜻 | 예시 / 비유 |
|---|---|---|
| NAND flash | 전하를 가둬 비트를 저장하는 비휘발 메모리 | 3D TLC NAND |
| SLC / MLC / TLC / QLC | 셀당 1 / 2 / 3 / 4 비트 | TLC 가 주류 |
| page | read/program 단위 | 4KB(책) ~ 16KB(현행) |
| block (erase block) | erase 단위, page 의 묶음 | 128KB(책) ~ 수십 MB(현행 3D) |
| plane / die(LUN) | 독립 동작 가능한 배열 / 칩 하나 | 다이당 2~6 plane |
| program | ERASED page 의 1 을 0 으로 | 수백 µs |
| erase | block 전체를 1 로 | 수 ms |
| P/E cycle | program-erase 한 번 | MLC 10K, SLC 100K(책 기준) |
| disturb | 이웃 page 비트가 뒤집힘 | read disturb, program disturb |
| FTL | 논리 블록 ↔ 물리 page 번역 + GC + WL | SSD 펌웨어 핵심 |
| L2P (mapping table) | 논리 page → 물리 page 표 | 4B × (용량/4KB) |
| OOB (spare area) | page 옆의 메타데이터 영역 | LPN, seq, ECC 패리티 |
| garbage collection | 쓰레기 많은 block 의 live page 를 옮기고 erase | greedy victim |
| write amplification (WAF) | NAND 에 쓴 양 / 호스트가 쓴 양 | WAF 3 = 3배 닳음 |
| over-provisioning (OP) | 사용자에게 안 보이는 여유 공간 | 7%, 28% |
| wear leveling | 모든 block 의 erase 수를 고르게 | dynamic / static |
| hybrid mapping | 데이터 block 은 block 매핑, log block 은 page 매핑 | switch/partial/full merge |
| TRIM / deallocate | "이 LBA 는 이제 안 씀" 알림 | NVMe DSM |

## 2. 비트 하나 저장하기 (I.1)

플래시 셀은 **게이트 아래에 전하를 가두는 트랜지스터** 다(floating gate, 요즘 3D NAND 는 charge-trap). 가둔 전하량이 셀의 **문턱전압 Vt** 를 바꾸고, 읽을 때는 게이트에 기준 전압을 걸어 "켜지나/안 켜지나"로 Vt 가 어느 구간인지 판단한다.

- **SLC**: Vt 구간 2개 → 1비트. 기준 전압 1개로 판별.
- **MLC**: 4개 → 2비트. 기준 전압 3개.
- **TLC**: 8개 → 3비트. 기준 전압 7개. (QLC 는 16개, 15개.)

```svg
<svg viewBox="0 0 700 330" xmlns="http://www.w3.org/2000/svg" font-family="-apple-system, sans-serif" font-size="13">
<text x="10" y="75" fill="currentColor" font-weight="bold">SLC (1 bit)</text>
<line x1="110" y1="95" x2="680" y2="95" stroke="currentColor"/>
<polyline points="121.0,94.3 125.3,94.1 129.7,93.8 134.1,93.4 138.5,93.0 142.9,92.4 147.3,91.6 151.7,90.7 156.0,89.7 160.4,88.4 164.8,86.9 169.2,85.1 173.6,83.1 178.0,80.9 182.3,78.3 186.7,75.5 191.1,72.5 195.5,69.2 199.9,65.8 204.3,62.2 208.7,58.6 213.0,55.0 217.4,51.4 221.8,48.0 226.2,44.9 230.6,42.1 235.0,39.6 239.3,37.6 243.7,36.2 248.1,35.3 252.5,35.0 256.9,35.3 261.3,36.2 265.7,37.6 270.0,39.6 274.4,42.1 278.8,44.9 283.2,48.0 287.6,51.4 292.0,55.0 296.3,58.6 300.7,62.2 305.1,65.8 309.5,69.2 313.9,72.5 318.3,75.5 322.7,78.3 327.0,80.9 331.4,83.1 335.8,85.1 340.2,86.9 344.6,88.4 349.0,89.7 353.3,90.7 357.7,91.6 362.1,92.4 366.5,93.0 370.9,93.4 375.3,93.8 379.7,94.1 384.0,94.3" style="fill:var(--accent-soft);stroke:var(--accent)" stroke-width="1.5"/>
<text x="252" y="111" text-anchor="middle" fill="currentColor" font-size="11" font-family="Menlo, monospace">1</text>
<polyline points="406.0,94.3 410.3,94.1 414.7,93.8 419.1,93.4 423.5,93.0 427.9,92.4 432.3,91.6 436.7,90.7 441.0,89.7 445.4,88.4 449.8,86.9 454.2,85.1 458.6,83.1 463.0,80.9 467.3,78.3 471.7,75.5 476.1,72.5 480.5,69.2 484.9,65.8 489.3,62.2 493.7,58.6 498.0,55.0 502.4,51.4 506.8,48.0 511.2,44.9 515.6,42.1 520.0,39.6 524.3,37.6 528.7,36.2 533.1,35.3 537.5,35.0 541.9,35.3 546.3,36.2 550.7,37.6 555.0,39.6 559.4,42.1 563.8,44.9 568.2,48.0 572.6,51.4 577.0,55.0 581.3,58.6 585.7,62.2 590.1,65.8 594.5,69.2 598.9,72.5 603.3,75.5 607.7,78.3 612.0,80.9 616.4,83.1 620.8,85.1 625.2,86.9 629.6,88.4 634.0,89.7 638.3,90.7 642.7,91.6 647.1,92.4 651.5,93.0 655.9,93.4 660.3,93.8 664.7,94.1 669.0,94.3" fill="none" stroke="currentColor" stroke-width="1.5"/>
<text x="538" y="111" text-anchor="middle" fill="currentColor" font-size="11" font-family="Menlo, monospace">0</text>
<line x1="395.0" y1="29" x2="395.0" y2="95" stroke="#d9534f" stroke-dasharray="3 3"/>
<text x="680" y="25" text-anchor="end" fill="currentColor" font-size="11">read 기준전압 1개</text>
<text x="10" y="180" fill="currentColor" font-weight="bold">MLC (2 bits)</text>
<line x1="110" y1="200" x2="680" y2="200" stroke="currentColor"/>
<polyline points="115.5,199.3 117.7,199.1 119.9,198.8 122.1,198.4 124.2,198.0 126.4,197.4 128.6,196.6 130.8,195.7 133.0,194.7 135.2,193.4 137.4,191.9 139.6,190.1 141.8,188.1 144.0,185.9 146.2,183.3 148.4,180.5 150.6,177.5 152.8,174.2 154.9,170.8 157.1,167.2 159.3,163.6 161.5,160.0 163.7,156.4 165.9,153.0 168.1,149.9 170.3,147.1 172.5,144.6 174.7,142.6 176.9,141.2 179.1,140.3 181.2,140.0 183.4,140.3 185.6,141.2 187.8,142.6 190.0,144.6 192.2,147.1 194.4,149.9 196.6,153.0 198.8,156.4 201.0,160.0 203.2,163.6 205.4,167.2 207.6,170.8 209.8,174.2 211.9,177.5 214.1,180.5 216.3,183.3 218.5,185.9 220.7,188.1 222.9,190.1 225.1,191.9 227.3,193.4 229.5,194.7 231.7,195.7 233.9,196.6 236.1,197.4 238.2,198.0 240.4,198.4 242.6,198.8 244.8,199.1 247.0,199.3" style="fill:var(--accent-soft);stroke:var(--accent)" stroke-width="1.5"/>
<text x="181" y="216" text-anchor="middle" fill="currentColor" font-size="11" font-family="Menlo, monospace">11</text>
<polyline points="258.0,199.3 260.2,199.1 262.4,198.8 264.6,198.4 266.8,198.0 268.9,197.4 271.1,196.6 273.3,195.7 275.5,194.7 277.7,193.4 279.9,191.9 282.1,190.1 284.3,188.1 286.5,185.9 288.7,183.3 290.9,180.5 293.1,177.5 295.2,174.2 297.4,170.8 299.6,167.2 301.8,163.6 304.0,160.0 306.2,156.4 308.4,153.0 310.6,149.9 312.8,147.1 315.0,144.6 317.2,142.6 319.4,141.2 321.6,140.3 323.8,140.0 325.9,140.3 328.1,141.2 330.3,142.6 332.5,144.6 334.7,147.1 336.9,149.9 339.1,153.0 341.3,156.4 343.5,160.0 345.7,163.6 347.9,167.2 350.1,170.8 352.2,174.2 354.4,177.5 356.6,180.5 358.8,183.3 361.0,185.9 363.2,188.1 365.4,190.1 367.6,191.9 369.8,193.4 372.0,194.7 374.2,195.7 376.4,196.6 378.6,197.4 380.8,198.0 382.9,198.4 385.1,198.8 387.3,199.1 389.5,199.3" fill="none" stroke="currentColor" stroke-width="1.5"/>
<text x="324" y="216" text-anchor="middle" fill="currentColor" font-size="11" font-family="Menlo, monospace">01</text>
<polyline points="400.5,199.3 402.7,199.1 404.9,198.8 407.1,198.4 409.2,198.0 411.4,197.4 413.6,196.6 415.8,195.7 418.0,194.7 420.2,193.4 422.4,191.9 424.6,190.1 426.8,188.1 429.0,185.9 431.2,183.3 433.4,180.5 435.6,177.5 437.8,174.2 439.9,170.8 442.1,167.2 444.3,163.6 446.5,160.0 448.7,156.4 450.9,153.0 453.1,149.9 455.3,147.1 457.5,144.6 459.7,142.6 461.9,141.2 464.1,140.3 466.2,140.0 468.4,140.3 470.6,141.2 472.8,142.6 475.0,144.6 477.2,147.1 479.4,149.9 481.6,153.0 483.8,156.4 486.0,160.0 488.2,163.6 490.4,167.2 492.6,170.8 494.8,174.2 496.9,177.5 499.1,180.5 501.3,183.3 503.5,185.9 505.7,188.1 507.9,190.1 510.1,191.9 512.3,193.4 514.5,194.7 516.7,195.7 518.9,196.6 521.1,197.4 523.2,198.0 525.4,198.4 527.6,198.8 529.8,199.1 532.0,199.3" fill="none" stroke="currentColor" stroke-width="1.5"/>
<text x="466" y="216" text-anchor="middle" fill="currentColor" font-size="11" font-family="Menlo, monospace">00</text>
<polyline points="543.0,199.3 545.2,199.1 547.4,198.8 549.6,198.4 551.8,198.0 553.9,197.4 556.1,196.6 558.3,195.7 560.5,194.7 562.7,193.4 564.9,191.9 567.1,190.1 569.3,188.1 571.5,185.9 573.7,183.3 575.9,180.5 578.1,177.5 580.2,174.2 582.4,170.8 584.6,167.2 586.8,163.6 589.0,160.0 591.2,156.4 593.4,153.0 595.6,149.9 597.8,147.1 600.0,144.6 602.2,142.6 604.4,141.2 606.6,140.3 608.8,140.0 610.9,140.3 613.1,141.2 615.3,142.6 617.5,144.6 619.7,147.1 621.9,149.9 624.1,153.0 626.3,156.4 628.5,160.0 630.7,163.6 632.9,167.2 635.1,170.8 637.2,174.2 639.4,177.5 641.6,180.5 643.8,183.3 646.0,185.9 648.2,188.1 650.4,190.1 652.6,191.9 654.8,193.4 657.0,194.7 659.2,195.7 661.4,196.6 663.6,197.4 665.8,198.0 667.9,198.4 670.1,198.8 672.3,199.1 674.5,199.3" fill="none" stroke="currentColor" stroke-width="1.5"/>
<text x="609" y="216" text-anchor="middle" fill="currentColor" font-size="11" font-family="Menlo, monospace">10</text>
<line x1="252.5" y1="134" x2="252.5" y2="200" stroke="#d9534f" stroke-dasharray="3 3"/>
<line x1="395.0" y1="134" x2="395.0" y2="200" stroke="#d9534f" stroke-dasharray="3 3"/>
<line x1="537.5" y1="134" x2="537.5" y2="200" stroke="#d9534f" stroke-dasharray="3 3"/>
<text x="680" y="130" text-anchor="end" fill="currentColor" font-size="11">read 기준전압 3개</text>
<text x="10" y="285" fill="currentColor" font-weight="bold">TLC (3 bits)</text>
<line x1="110" y1="305" x2="680" y2="305" stroke="currentColor"/>
<polyline points="112.7,304.3 113.8,304.1 114.9,303.8 116.0,303.4 117.1,303.0 118.2,302.4 119.3,301.6 120.4,300.7 121.5,299.7 122.6,298.4 123.7,296.9 124.8,295.1 125.9,293.1 127.0,290.9 128.1,288.3 129.2,285.5 130.3,282.5 131.4,279.2 132.5,275.8 133.6,272.2 134.7,268.6 135.8,265.0 136.9,261.4 138.0,258.0 139.0,254.9 140.1,252.1 141.2,249.6 142.3,247.6 143.4,246.2 144.5,245.3 145.6,245.0 146.7,245.3 147.8,246.2 148.9,247.6 150.0,249.6 151.1,252.1 152.2,254.9 153.3,258.0 154.4,261.4 155.5,265.0 156.6,268.6 157.7,272.2 158.8,275.8 159.9,279.2 161.0,282.5 162.1,285.5 163.2,288.3 164.3,290.9 165.4,293.1 166.5,295.1 167.5,296.9 168.6,298.4 169.7,299.7 170.8,300.7 171.9,301.6 173.0,302.4 174.1,303.0 175.2,303.4 176.3,303.8 177.4,304.1 178.5,304.3" style="fill:var(--accent-soft);stroke:var(--accent)" stroke-width="1.5"/>
<text x="146" y="321" text-anchor="middle" fill="currentColor" font-size="11" font-family="Menlo, monospace">111</text>
<polyline points="184.0,304.3 185.1,304.1 186.2,303.8 187.3,303.4 188.4,303.0 189.5,302.4 190.6,301.6 191.7,300.7 192.8,299.7 193.9,298.4 195.0,296.9 196.0,295.1 197.1,293.1 198.2,290.9 199.3,288.3 200.4,285.5 201.5,282.5 202.6,279.2 203.7,275.8 204.8,272.2 205.9,268.6 207.0,265.0 208.1,261.4 209.2,258.0 210.3,254.9 211.4,252.1 212.5,249.6 213.6,247.6 214.7,246.2 215.8,245.3 216.9,245.0 218.0,245.3 219.1,246.2 220.2,247.6 221.3,249.6 222.4,252.1 223.5,254.9 224.5,258.0 225.6,261.4 226.7,265.0 227.8,268.6 228.9,272.2 230.0,275.8 231.1,279.2 232.2,282.5 233.3,285.5 234.4,288.3 235.5,290.9 236.6,293.1 237.7,295.1 238.8,296.9 239.9,298.4 241.0,299.7 242.1,300.7 243.2,301.6 244.3,302.4 245.4,303.0 246.5,303.4 247.6,303.8 248.7,304.1 249.8,304.3" fill="none" stroke="currentColor" stroke-width="1.5"/>
<text x="217" y="321" text-anchor="middle" fill="currentColor" font-size="11" font-family="Menlo, monospace">011</text>
<polyline points="255.2,304.3 256.3,304.1 257.4,303.8 258.5,303.4 259.6,303.0 260.7,302.4 261.8,301.6 262.9,300.7 264.0,299.7 265.1,298.4 266.2,296.9 267.3,295.1 268.4,293.1 269.5,290.9 270.6,288.3 271.7,285.5 272.8,282.5 273.9,279.2 275.0,275.8 276.1,272.2 277.2,268.6 278.3,265.0 279.4,261.4 280.5,258.0 281.5,254.9 282.6,252.1 283.7,249.6 284.8,247.6 285.9,246.2 287.0,245.3 288.1,245.0 289.2,245.3 290.3,246.2 291.4,247.6 292.5,249.6 293.6,252.1 294.7,254.9 295.8,258.0 296.9,261.4 298.0,265.0 299.1,268.6 300.2,272.2 301.3,275.8 302.4,279.2 303.5,282.5 304.6,285.5 305.7,288.3 306.8,290.9 307.9,293.1 309.0,295.1 310.0,296.9 311.1,298.4 312.2,299.7 313.3,300.7 314.4,301.6 315.5,302.4 316.6,303.0 317.7,303.4 318.8,303.8 319.9,304.1 321.0,304.3" fill="none" stroke="currentColor" stroke-width="1.5"/>
<text x="288" y="321" text-anchor="middle" fill="currentColor" font-size="11" font-family="Menlo, monospace">001</text>
<polyline points="326.5,304.3 327.6,304.1 328.7,303.8 329.8,303.4 330.9,303.0 332.0,302.4 333.1,301.6 334.2,300.7 335.3,299.7 336.4,298.4 337.5,296.9 338.5,295.1 339.6,293.1 340.7,290.9 341.8,288.3 342.9,285.5 344.0,282.5 345.1,279.2 346.2,275.8 347.3,272.2 348.4,268.6 349.5,265.0 350.6,261.4 351.7,258.0 352.8,254.9 353.9,252.1 355.0,249.6 356.1,247.6 357.2,246.2 358.3,245.3 359.4,245.0 360.5,245.3 361.6,246.2 362.7,247.6 363.8,249.6 364.9,252.1 366.0,254.9 367.0,258.0 368.1,261.4 369.2,265.0 370.3,268.6 371.4,272.2 372.5,275.8 373.6,279.2 374.7,282.5 375.8,285.5 376.9,288.3 378.0,290.9 379.1,293.1 380.2,295.1 381.3,296.9 382.4,298.4 383.5,299.7 384.6,300.7 385.7,301.6 386.8,302.4 387.9,303.0 389.0,303.4 390.1,303.8 391.2,304.1 392.3,304.3" fill="none" stroke="currentColor" stroke-width="1.5"/>
<text x="359" y="321" text-anchor="middle" fill="currentColor" font-size="11" font-family="Menlo, monospace">000</text>
<polyline points="397.7,304.3 398.8,304.1 399.9,303.8 401.0,303.4 402.1,303.0 403.2,302.4 404.3,301.6 405.4,300.7 406.5,299.7 407.6,298.4 408.7,296.9 409.8,295.1 410.9,293.1 412.0,290.9 413.1,288.3 414.2,285.5 415.3,282.5 416.4,279.2 417.5,275.8 418.6,272.2 419.7,268.6 420.8,265.0 421.9,261.4 423.0,258.0 424.0,254.9 425.1,252.1 426.2,249.6 427.3,247.6 428.4,246.2 429.5,245.3 430.6,245.0 431.7,245.3 432.8,246.2 433.9,247.6 435.0,249.6 436.1,252.1 437.2,254.9 438.3,258.0 439.4,261.4 440.5,265.0 441.6,268.6 442.7,272.2 443.8,275.8 444.9,279.2 446.0,282.5 447.1,285.5 448.2,288.3 449.3,290.9 450.4,293.1 451.5,295.1 452.5,296.9 453.6,298.4 454.7,299.7 455.8,300.7 456.9,301.6 458.0,302.4 459.1,303.0 460.2,303.4 461.3,303.8 462.4,304.1 463.5,304.3" fill="none" stroke="currentColor" stroke-width="1.5"/>
<text x="431" y="321" text-anchor="middle" fill="currentColor" font-size="11" font-family="Menlo, monospace">010</text>
<polyline points="469.0,304.3 470.1,304.1 471.2,303.8 472.3,303.4 473.4,303.0 474.5,302.4 475.6,301.6 476.7,300.7 477.8,299.7 478.9,298.4 480.0,296.9 481.0,295.1 482.1,293.1 483.2,290.9 484.3,288.3 485.4,285.5 486.5,282.5 487.6,279.2 488.7,275.8 489.8,272.2 490.9,268.6 492.0,265.0 493.1,261.4 494.2,258.0 495.3,254.9 496.4,252.1 497.5,249.6 498.6,247.6 499.7,246.2 500.8,245.3 501.9,245.0 503.0,245.3 504.1,246.2 505.2,247.6 506.3,249.6 507.4,252.1 508.5,254.9 509.5,258.0 510.6,261.4 511.7,265.0 512.8,268.6 513.9,272.2 515.0,275.8 516.1,279.2 517.2,282.5 518.3,285.5 519.4,288.3 520.5,290.9 521.6,293.1 522.7,295.1 523.8,296.9 524.9,298.4 526.0,299.7 527.1,300.7 528.2,301.6 529.3,302.4 530.4,303.0 531.5,303.4 532.6,303.8 533.7,304.1 534.8,304.3" fill="none" stroke="currentColor" stroke-width="1.5"/>
<text x="502" y="321" text-anchor="middle" fill="currentColor" font-size="11" font-family="Menlo, monospace">110</text>
<polyline points="540.2,304.3 541.3,304.1 542.4,303.8 543.5,303.4 544.6,303.0 545.7,302.4 546.8,301.6 547.9,300.7 549.0,299.7 550.1,298.4 551.2,296.9 552.3,295.1 553.4,293.1 554.5,290.9 555.6,288.3 556.7,285.5 557.8,282.5 558.9,279.2 560.0,275.8 561.1,272.2 562.2,268.6 563.3,265.0 564.4,261.4 565.5,258.0 566.5,254.9 567.6,252.1 568.7,249.6 569.8,247.6 570.9,246.2 572.0,245.3 573.1,245.0 574.2,245.3 575.3,246.2 576.4,247.6 577.5,249.6 578.6,252.1 579.7,254.9 580.8,258.0 581.9,261.4 583.0,265.0 584.1,268.6 585.2,272.2 586.3,275.8 587.4,279.2 588.5,282.5 589.6,285.5 590.7,288.3 591.8,290.9 592.9,293.1 594.0,295.1 595.0,296.9 596.1,298.4 597.2,299.7 598.3,300.7 599.4,301.6 600.5,302.4 601.6,303.0 602.7,303.4 603.8,303.8 604.9,304.1 606.0,304.3" fill="none" stroke="currentColor" stroke-width="1.5"/>
<text x="573" y="321" text-anchor="middle" fill="currentColor" font-size="11" font-family="Menlo, monospace">100</text>
<polyline points="611.5,304.3 612.6,304.1 613.7,303.8 614.8,303.4 615.9,303.0 617.0,302.4 618.1,301.6 619.2,300.7 620.3,299.7 621.4,298.4 622.5,296.9 623.5,295.1 624.6,293.1 625.7,290.9 626.8,288.3 627.9,285.5 629.0,282.5 630.1,279.2 631.2,275.8 632.3,272.2 633.4,268.6 634.5,265.0 635.6,261.4 636.7,258.0 637.8,254.9 638.9,252.1 640.0,249.6 641.1,247.6 642.2,246.2 643.3,245.3 644.4,245.0 645.5,245.3 646.6,246.2 647.7,247.6 648.8,249.6 649.9,252.1 651.0,254.9 652.0,258.0 653.1,261.4 654.2,265.0 655.3,268.6 656.4,272.2 657.5,275.8 658.6,279.2 659.7,282.5 660.8,285.5 661.9,288.3 663.0,290.9 664.1,293.1 665.2,295.1 666.3,296.9 667.4,298.4 668.5,299.7 669.6,300.7 670.7,301.6 671.8,302.4 672.9,303.0 674.0,303.4 675.1,303.8 676.2,304.1 677.3,304.3" fill="none" stroke="currentColor" stroke-width="1.5"/>
<text x="644" y="321" text-anchor="middle" fill="currentColor" font-size="11" font-family="Menlo, monospace">101</text>
<line x1="181.2" y1="239" x2="181.2" y2="305" stroke="#d9534f" stroke-dasharray="3 3"/>
<line x1="252.5" y1="239" x2="252.5" y2="305" stroke="#d9534f" stroke-dasharray="3 3"/>
<line x1="323.8" y1="239" x2="323.8" y2="305" stroke="#d9534f" stroke-dasharray="3 3"/>
<line x1="395.0" y1="239" x2="395.0" y2="305" stroke="#d9534f" stroke-dasharray="3 3"/>
<line x1="466.2" y1="239" x2="466.2" y2="305" stroke="#d9534f" stroke-dasharray="3 3"/>
<line x1="537.5" y1="239" x2="537.5" y2="305" stroke="#d9534f" stroke-dasharray="3 3"/>
<line x1="608.8" y1="239" x2="608.8" y2="305" stroke="#d9534f" stroke-dasharray="3 3"/>
<text x="680" y="235" text-anchor="end" fill="currentColor" font-size="11">read 기준전압 7개</text>
<text x="110" y="322" fill="currentColor" font-size="12">가로축 = 셀 문턱전압 Vt (왼쪽 = erase 상태, 전하 적음) · 칠한 분포 = erased(모두 1) · 빨간 점선 = read reference</text>
</svg>
```

같은 전압 창에 구간을 더 많이 넣을수록 구간 사이의 여유(margin)가 줄어든다. 그래서 비트가 많을수록 **읽기가 느리고**(기준 전압을 여러 번 걸어야), **쓰기가 느리고**(Vt 를 좁은 구간에 정확히 맞추려고 조금씩 올리고 확인하는 ISPP — Incremental Step Pulse Programming — 를 더 잘게), **덜 견딘다**(약간의 전하 누설·트랩만으로 이웃 구간으로 넘어감). SLC 가 빠르고 비싼 이유가 이 그림 하나에 다 있다. 인접 구간을 Gray code 로 매기는 것도 포인트다: 이웃 구간으로 잘못 읽혀도 **비트 하나만** 틀리게 해서 ECC 부담을 줄인다.

왜 "erase 먼저"인가? program 은 선택한 **워드라인(= page)** 의 게이트에 높은 전압을 걸어 전자를 **넣는** 동작이라 page 단위로 고를 수 있다. 전자를 **빼는** erase 는 반대로 기판(p-well)에 높은 전압(~20V)을 걸고 게이트를 0V 로 묶어야 하는데, well 은 여러 block 이 공유하고, 어느 셀을 지울지는 **block 디코더가 block 단위로만** 고른다(선택 block 의 워드라인만 0V, 나머지는 floating 으로 둬서 지워지지 않게). page 단위로 골라 지우는 회로는 면적·전압 문제로 두지 않는다. 그래서 빼는 건 block 단위로만 된다. 1→0(전자 넣기)은 page 단위, 0→1(전자 빼기)은 block 단위 — 이 비대칭이 이 장의 모든 복잡함의 근원이다.

> **TIP — BE CAREFUL WITH TERMINOLOGY**: flash 의 "block" 과 "page" 는 디스크의 block, VM 의 page 와 다른 뜻이다. 분야마다 용어를 정확히 쓰자. (디스크 block ≈ flash page 크기, flash block = erase 단위)

## 3. 비트에서 bank/plane 으로 (I.2)

셀들을 묶어 **plane**(책: bank)을 만들고, plane 은 두 크기로 접근된다.

- **block(erase block)**: 책 기준 128KB~256KB. 오늘날 3D TLC 는 page 16KB × 수백~수천 page 로 **수 MB~수십 MB**.
- **page**: 책 기준 4KB, 오늘날 16KB(멀티 plane 으로 묶으면 64KB 단위 program).

실제 계층은 **패키지 → die(LUN) → plane → block → page**. die 는 독립적으로 명령을 수행하고, 같은 die 의 plane 들은 같은 종류의 명령을 동시에(multi-plane) 수행할 수 있다. SSD 성능은 대부분 "몇 개의 die 를 동시에 바쁘게 하느냐"로 결정된다.

## 4. 기본 연산 (I.3)

| 연산 | 단위 | 시간(책) | 하는 일 |
|---|---|---|---|
| read | page | 10s µs | 위치와 무관하게 일정 → **random access 장치** |
| erase | block | 수 ms | block 의 모든 비트를 1 로, 상태 → ERASED |
| program | page | 100s µs | ERASED page 의 일부 1 을 0 으로, 상태 → VALID |

page 상태 기계(4-page block):

```text
            iiii    처음: INVALID
Erase()   → EEEE    모두 ERASED (programmable)
Program(0)→ VEEE    page 0 VALID
Program(0)→ error   이미 program 된 page 는 다시 못 씀
Program(1)→ VVEE
Erase()   → EEEE    내용 전부 사라짐
```

책의 8비트 예제: page 0..3 이 `00011000 11001110 00000001 00111111`(모두 VALID)일 때 page 0 만 `00000011` 로 바꾸고 싶다. erase 하면 block 전체가 `11111111` 이 되고, page 0 을 program 하면 page 0 은 원하는 값이 되지만 **page 1..3 의 원래 내용은 사라졌다**. 덮어쓰기 전에 살릴 데이터는 미리 다른 곳(메모리나 다른 block)으로 옮겨야 한다.

새 예제 하나: program 은 1→0 만 할 수 있으므로, 이미 `11110000` 인 page 에 `11000000` 을 "덧쓰기" 하는 것은 비트 단위로는 가능하다(1→0 만 필요). 하지만 `11110001` 로는 못 바꾼다(마지막 비트 0→1). 실제 NAND 는 MLC 이상에서 page 재-program 자체를 금지하고(NOP=1), SLC 도 부분 program 횟수(NOP)를 몇 번으로 제한한다 — 이웃 셀 disturb 와 ECC 패리티 때문에, 비트 규칙상 가능해도 하지 않는다.

## 5. 성능과 신뢰성 (I.4)

| 종류 | Read (µs) | Program (µs) | Erase (µs) |
|---|---|---|---|
| SLC | 25 | 200–300 | 1500–2000 |
| MLC | 50 | 600–900 | ~3000 |
| TLC | ~75 | ~900–1350 | ~4500 |

- read 는 수십 µs 로 HDD(수 ms)보다 100배 이상 빠르다.
- program 은 수백 µs, 비트가 많을수록 느리다 → **여러 칩을 병렬로** 써야 쓰기 성능이 나온다.
- erase 는 수 ms. 이 비용을 숨기는 게 FTL 설계의 핵심.

신뢰성:

- **wear out**: erase/program 할 때마다 터널 산화막에 전하가 조금씩 갇힌다(trap). 쌓이면 0 과 1 의 Vt 구간을 구분하기 어려워지고, 결국 그 block 은 못 쓴다. 제조사 정격: MLC **10,000 P/E**, SLC **100,000 P/E**(책 기준; 현행 3D TLC 는 보통 수천, QLC 는 1,000 안팎). 연구에 따르면 실제 수명은 정격보다 훨씬 길 수 있다.
- **disturb**: 한 page 를 읽거나 program 할 때 이웃 page 의 비트가 뒤집힐 수 있다 — **read disturb**, **program disturb**. FTL 이 block 안에서 page 를 **낮은 번호부터 순서대로** program 하는 이유 중 하나.
- 책 밖의 현실: **retention**(시간이 지나면 전하가 새서 Vt 분포가 왼쪽으로 이동, 고온·고 P/E 에서 악화), 그래서 모든 page 는 강한 **ECC**(BCH → 현재는 LDPC)를 OOB 에 함께 저장하고, 읽기 실패 시 기준 전압을 바꿔 다시 읽는 **read retry**, LDPC soft decoding 까지 동원한다.

## 6. Raw flash 에서 SSD 로 (I.5)

SSD 가 할 일: 낡은 블록 인터페이스(512B/4KB 섹터를 LBA 로 읽고 쓰기)를 raw flash 위에 제공하기.

```svg
<svg viewBox="0 0 700 300" xmlns="http://www.w3.org/2000/svg" font-family="-apple-system, sans-serif" font-size="13">
  <defs><marker id="C0I-arrow" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto-start-reverse"><path d="M0,0 L10,5 L0,10 z" fill="currentColor"/></marker></defs>
  <rect x="10" y="110" width="80" height="60" rx="6" fill="none" stroke="currentColor"/>
  <text x="50" y="136" text-anchor="middle" fill="currentColor">Host</text>
  <text x="50" y="155" text-anchor="middle" fill="currentColor" font-size="11">NVMe/PCIe</text>
  <line x1="90" y1="140" x2="128" y2="140" stroke="currentColor" marker-start="url(#C0I-arrow)" marker-end="url(#C0I-arrow)"/>
  <rect x="130" y="30" width="270" height="240" rx="8" style="fill:var(--accent-soft)" stroke="currentColor"/>
  <text x="265" y="50" text-anchor="middle" fill="currentColor" font-weight="bold">Flash controller (SoC)</text>
  <rect x="145" y="62" width="110" height="38" rx="4" fill="none" stroke="currentColor"/>
  <text x="200" y="85" text-anchor="middle" fill="currentColor" font-size="12">host I/F (NVMe)</text>
  <rect x="270" y="62" width="115" height="38" rx="4" fill="none" stroke="currentColor"/>
  <text x="327" y="80" text-anchor="middle" fill="currentColor" font-size="12">CPU cores</text>
  <text x="327" y="94" text-anchor="middle" fill="currentColor" font-size="11">FTL · GC · WL</text>
  <rect x="145" y="110" width="110" height="38" rx="4" fill="none" stroke="currentColor"/>
  <text x="200" y="133" text-anchor="middle" fill="currentColor" font-size="12">SRAM buffers</text>
  <rect x="270" y="110" width="115" height="38" rx="4" fill="none" stroke="currentColor"/>
  <text x="327" y="133" text-anchor="middle" fill="currentColor" font-size="12">DRAM ctrl</text>
  <rect x="145" y="158" width="110" height="38" rx="4" fill="none" stroke="currentColor"/>
  <text x="200" y="181" text-anchor="middle" fill="currentColor" font-size="12">LDPC ECC</text>
  <rect x="270" y="158" width="115" height="38" rx="4" fill="none" stroke="currentColor"/>
  <text x="327" y="181" text-anchor="middle" fill="currentColor" font-size="12">XOR / DMA</text>
  <rect x="145" y="206" width="240" height="50" rx="4" fill="none" stroke="currentColor"/>
  <text x="265" y="228" text-anchor="middle" fill="currentColor" font-size="12">flash interface: channel 0..N-1</text>
  <text x="265" y="245" text-anchor="middle" fill="currentColor" font-size="11">(ONFI/Toggle, 채널당 die 여러 개)</text>
  <rect x="300" y="2" width="100" height="24" rx="4" fill="none" stroke="currentColor"/>
  <text x="350" y="19" text-anchor="middle" fill="currentColor" font-size="12">DRAM (L2P)</text>
  <line x1="350" y1="26" x2="350" y2="108" stroke="currentColor" stroke-dasharray="4 3"/>
  <g stroke="currentColor">
    <line x1="385" y1="215" x2="430" y2="70"/><line x1="385" y1="225" x2="430" y2="130"/><line x1="385" y1="238" x2="430" y2="190"/><line x1="385" y1="250" x2="430" y2="250"/>
  </g>
  <g font-size="11">
    <text x="430" y="62" fill="currentColor">ch0</text><text x="430" y="122" fill="currentColor">ch1</text><text x="430" y="182" fill="currentColor">ch2</text><text x="430" y="242" fill="currentColor">ch3</text>
  </g>
  <g stroke="currentColor" fill="none">
    <rect x="460" y="55" width="50" height="30" rx="3"/><rect x="520" y="55" width="50" height="30" rx="3"/><rect x="580" y="55" width="50" height="30" rx="3"/><rect x="640" y="55" width="50" height="30" rx="3"/>
    <rect x="460" y="115" width="50" height="30" rx="3"/><rect x="520" y="115" width="50" height="30" rx="3"/><rect x="580" y="115" width="50" height="30" rx="3"/><rect x="640" y="115" width="50" height="30" rx="3"/>
    <rect x="460" y="175" width="50" height="30" rx="3"/><rect x="520" y="175" width="50" height="30" rx="3"/><rect x="580" y="175" width="50" height="30" rx="3"/><rect x="640" y="175" width="50" height="30" rx="3"/>
    <rect x="460" y="235" width="50" height="30" rx="3"/><rect x="520" y="235" width="50" height="30" rx="3"/><rect x="580" y="235" width="50" height="30" rx="3"/><rect x="640" y="235" width="50" height="30" rx="3"/>
  </g>
  <g font-size="11" text-anchor="middle">
    <text x="485" y="74" fill="currentColor">die</text><text x="545" y="74" fill="currentColor">die</text><text x="605" y="74" fill="currentColor">die</text><text x="665" y="74" fill="currentColor">die</text>
  </g>
  <text x="460" y="290" fill="currentColor" font-size="12">die 16개 x plane 4 = 64 개가 동시에 tPROG 를 숨긴다</text>
</svg>
```

- 플래시 칩 여러 개(영속 저장), 휘발성 메모리(SRAM/DRAM — 버퍼, 그리고 **매핑 테이블**), 그리고 제어 로직. 이 제어 로직의 핵심 기능이 **FTL(Flash Translation Layer)**: 논리 block 에 대한 read/write 를 물리 block/page 에 대한 read/erase/program 으로 바꾼다.
- 성능 목표 두 가지: (1) **여러 칩 병렬 사용**(책은 깊이 안 다룸 — 실제로는 FW 성능 설계의 절반), (2) **write amplification 줄이기**:

```text
WAF = (FTL 이 flash 칩에 보낸 총 쓰기 바이트) / (클라이언트가 SSD 에 보낸 총 쓰기 바이트)
```

- 신뢰성 목표: **wear leveling**(모든 block 이 비슷한 시점에 닳도록), **program disturb 최소화**(block 안에서 낮은 page 부터 순서대로).

> **TIP — THE IMPORTANCE OF BACKWARDS COMPATIBILITY**: 블록 인터페이스를 유지했기에 SSD 는 기존 FS 아래 그대로 꽂혔다. 대신 그 인터페이스가 flash 에 안 맞는 부분(덮어쓰기, 삭제 통보 없음)을 FTL 이 떠안았다. ZFS 가 FS+RAID 를 다시 설계했듯, 인터페이스를 다시 생각하는 시도가 TRIM, multi-stream, ZNS, FDP 다.

## 7. 나쁜 FTL: direct mapped (I.6)

가장 단순한 FTL: 논리 page N → 물리 page N. 읽기는 쉽다. 쓰기는:

1. page N 이 속한 block 전체를 읽고,
2. block 을 erase 하고,
3. 옛 page 들과 새 page 를 다시 program.

숫자로 보자(새 예제, 책 Figure I.2 의 MLC 값 + 현실적인 block): page 4KB, block 당 256 page, read 50 µs, program 600 µs, erase 3 ms.

```text
4KB 덮어쓰기 하나:
  read  256 page x 50 us  =  12.8 ms
  erase 1 block            =   3.0 ms
  prog  256 page x 600 us = 153.6 ms
  합계                     ≈ 169 ms  →  약 6 IOPS
  WAF = 256 page 를 써서 1 page 를 갱신 = 256
```

HDD 의 랜덤 쓰기(~13 ms, 76 IOPS)보다 **10배 이상 느리다**. 책의 말대로 "typical hard drives 보다도 느리다". 그리고 신뢰성도 최악: 파일시스템 메타데이터처럼 같은 논리 page 를 계속 덮어쓰면 **같은 물리 block 만** 계속 erase 돼서 금방 닳는다. 워크로드가 wear 를 통제하게 놔두는 설계다.

## 8. Log-structured FTL (I.7)

오늘날 대부분의 FTL 은 **log structured** 다(파일시스템 쪽 버전은 [Ch.43 LFS](2026-09-30_C43_lfs.md)). 논리 block N 쓰기 = 현재 쓰고 있는 block 의 **다음 빈 page 에 append**, 그리고 "N 은 지금 물리 page X 에 있다"를 **매핑 테이블** 에 기록.

책의 예제(16KB block = 4KB page × 4, 처음엔 모두 INVALID):

```text
Write(100)=a1 → block 0 erase, page 0 program        Table: 100→0
Write(101)=a2 → page 1                               Table: 101→1
Write(2000)=b1 → page 2                              Table: 2000→2
Write(2001)=b2 → page 3                              Table: 2001→3

Block:    0           1           2
Page:  00 01 02 03 04 05 06 07 08 09 10 11
Data:  a1 a2 b1 b2
State: V  V  V  V  i  i  i  i  i  i  i  i
```

장점: erase 는 가끔만(block 하나를 다 채울 때마다 1번), read-modify-write 가 사라졌고, 쓰기가 모든 page 에 퍼지니 자연스럽게 wear 도 분산된다.

> **ASIDE — FTL MAPPING INFORMATION PERSISTENCE**: 전원이 나가면 RAM 의 매핑 테이블은? 가장 단순한 방법: 각 page 의 **OOB(out-of-band) 영역** 에 "이 page 는 논리 N" 을 같이 쓰고, 부팅 때 전체 OOB 를 스캔해 테이블을 재구성. 큰 SSD 에서는 너무 느리므로 고급 장치는 **로깅 + 체크포인트** 를 쓴다.

책은 여기서 멈추지만 FW 관점에서 한 단계 더: OOB 에 LPN 만 쓰면 같은 LPN 의 사본이 여러 개일 때 **어느 게 최신인지** 모른다. 그래서 **단조 증가 sequence 번호**(또는 block 별 write 순서 + block 의 open 순서)를 같이 쓴다. 그리고 OOB 에는 TRIM 의 흔적이 없다 — 아래 toy FTL 실험 [4] 에서 이 문제가 실제로 튀어나온다.

## 9. Garbage collection (I.8)

### 9.1 쓰레기가 생기는 이유

이어서 100, 101 을 c1, c2 로 덮어쓰면 다음 빈 page(4, 5)에 append 되고 테이블이 갱신된다(block 1 은 먼저 erase).

```text
Table: 100→4  101→5  2000→2  2001→3
Block:    0           1           2
Page:  00 01 02 03 04 05 06 07 08 09 10 11
Data:  a1 a2 b1 b2 c1 c2
State: V  V  V  V  V  V  E  E  i  i  i  i
```

page 0, 1 은 VALID 상태지만 아무도 안 가리키는 **옛 버전 = garbage(dead)**. log 구조에서는 덮어쓰기가 곧 쓰레기 생산이다.

### 9.2 GC 절차

victim block 을 고르고 → **live page 만 읽어 log 끝에 다시 쓰고** → victim block 을 erase.

page 가 live 인지 어떻게 아나? block 안(OOB 또는 block 끝 summary)에 "각 page 에 어떤 논리 block 이 있는지" 를 적어 두고, **매핑 테이블이 아직 그 물리 page 를 가리키는지** 확인한다. 위 예에서 block 0 의 2000, 2001 은 테이블이 여전히 2, 3 을 가리키니 live, 100, 101 은 4, 5 를 가리키니 dead.

```text
GC(block 0): read page 2,3 → write to page 6,7 → erase block 0
Table: 100→4  101→5  2000→6  2001→7
Block:    0           1           2
Data:              c1 c2 b1 b2
State: E  E  E  E  V  V  V  V  i  i  i  i
```

GC 는 비싸다(live 데이터를 읽고 다시 써야 함). 이상적인 victim 은 **전부 dead 인 block** — 복사 없이 바로 erase.

### 9.3 WAF 를 숫자로

victim block 의 live 비율을 u 라 하자. block 하나(P page)를 회수하면 (1−u)·P 개의 빈 page 가 생기고, 그걸 얻으려고 u·P 개를 복사했다. 호스트 쓰기 (1−u)·P 개를 받는 동안 NAND 에는 P 개를 쓴 셈이므로

```text
WAF = P / ((1-u)·P) = 1 / (1 - u)
u = 0.5 → WAF 2,  u = 0.8 → WAF 5,  u = 0.9 → WAF 10
```

u 를 낮추는 방법: **over-provisioning**(사용자에게 안 보이는 여유 공간 → 전체 중 live 비율이 낮아져 victim 의 u 도 낮아짐), **좋은 victim 선택**(greedy: u 최소, cost-benefit: 나이까지 고려), **hot/cold 분리**(같이 죽을 데이터를 같은 block 에 → block 이 통째로 dead 가 되기 쉬움), **TRIM**(삭제된 데이터를 live 로 착각하지 않게). 아래 C 실험에서 OP 7% → 100% 일 때 WAF 8.0 → 1.2, 그리고 측정한 u 로 계산한 1/(1−u) 가 실측 WAF 와 거의 같다는 것을 확인한다.

OP 는 GC 를 **백그라운드로 미루는 완충** 이기도 하다. 책: "여유 용량이 있으면 청소를 덜 바쁠 때로 미룰 수 있고, 내부 대역폭도 늘어나 청소가 사용자 대역폭을 덜 깎는다."

## 10. 매핑 테이블 크기 (I.9)

### 10.1 page-level 매핑의 메모리

1TB SSD, 4KB page 마다 4B 엔트리:

```text
엔트리 수 = 2^40 / 2^12 = 2^28 = 268,435,456
메모리    = 2^28 x 4 B = 2^30 B = 1 GiB
```

용량 1TB 당 DRAM 1GB — 업계의 "1:1000 규칙". 책은 "그래서 page-level FTL 은 비현실적"이라 했지만(2014년 관점), 지금의 고성능 SSD 는 실제로 이만큼 DRAM 을 단다. 대신 비용·전력 때문에 아래 대안들이 공존한다.

### 10.2 block-level 매핑

포인터를 block 당 하나만: 크기가 `Size_block / Size_page` 배 줄어든다(256KB/4KB = 64배 → 1TB 에 16MB). VM 에서 큰 page 를 쓰는 것과 같다(VPN 비트↓, offset 비트↑).

논리 주소 = (chunk 번호, offset). 4 page/block 이면 offset 2비트. 책 예: 2000..2003(a,b,c,d)은 chunk 500, offset 0..3, physical block 1(page 4..7)에 있다 → 테이블 `500 → 4` 한 줄. 2002 읽기: chunk 500 → 4, +offset 2 → page 6 → c.

문제는 **small write**: 2002 만 c′ 로 바꾸려면 2000, 2001, 2003 을 읽어 c′ 와 함께 새 block 에 4 page 를 다 써야 한다(`500 → 8`). block 이 256KB 이상이면 4KB 쓰기마다 거의 direct-mapped 수준의 WAF.

### 10.3 hybrid 매핑 (log block)

FTL 이 erase 된 block 몇 개를 **log block** 으로 두고 모든 쓰기를 거기로 보낸다. log block 만 **page 매핑**(log table), 나머지 데이터 block 은 **block 매핑**(data table). 읽기는 log table 먼저, 없으면 data table.

핵심은 log block 수를 작게 유지하는 것. 그래서 주기적으로 log block 을 "block 포인터 하나로 가리킬 수 있는" 데이터 block 으로 바꾼다. 세 가지 merge:

```text
초기: data table 250→8 (block 2 = 1000,1001,1002,1003 = a b c d)

(1) switch merge — 1000..1003 을 같은 순서로 전부 덮어씀 (a' b' c' d' → log block 0)
    log block 0 이 그대로 새 데이터 block: data table 250→0, block 2 는 erase 해서 새 log block.
    추가 I/O 0. 최선.

(2) partial merge — 1000, 1001 만 덮어씀 (a' b' → log block 0 page 0,1)
    c, d 를 block 2 에서 읽어 log block 0 의 page 2,3 에 마저 써서 완성 → 250→0.
    추가 I/O = 2 read + 2 program.

(3) full merge — log block A 에 논리 0, 4, 8, 12 가 섞여 들어옴
    0..3, 4..7, 8..11, 12..15 네 개의 데이터 block 을 각각 새로 조립해야 log block A 를 비울 수 있음.
    추가 I/O = 최대 4 x (3 read + 4 program). 최악.
```

> 원문 주의: partial merge 설명에 "logical blocks 0, 1, 2, and 4", "read from block 4", "physical pages 18 and 19 … 22 and 23" 이라고 되어 있는데, 바로 앞 예제(논리 1000–1003, physical block 2 = page 8–11)와 맞지 않는 **오타** 다. 예제에 맞추면 "1002, 1003(c, d)을 physical page 10, 11 에서 읽어 log block 0 의 page 2, 3 에 쓴다"가 맞다.

hybrid FTL 은 순차 쓰기(switch merge)에 강하고 랜덤 쓰기(full merge)에 약하다. 2000년대 USB/SD/eMMC 컨트롤러(BAST, FAST 등)가 이 계열이었다.

### 10.4 오늘날의 답

- **DFTL(demand-based)**: 전체 page 매핑은 flash 에 두고, 자주 쓰는 부분만 SRAM/DRAM 에 캐시(책 참고문헌 GY+09). VM 의 TLB/페이지 테이블 구조와 같은 발상([Ch.19 TLB](2026-09-30_C19_tlb.md)).
- **DRAM-less SSD + HMB**: NVMe Host Memory Buffer 로 호스트 DRAM 일부를 빌려 매핑 캐시로 쓴다(저가 클라이언트 SSD).
- **매핑 단위 키우기**: 대용량 QLC SSD 는 indirection unit 을 16KB, 64KB 로 키워 DRAM 을 1/4~1/16 로 줄인다. 대신 4KB 랜덤 쓰기는 FTL 안에서 RMW 가 되어 WAF 가 커진다 — 10.2 의 trade-off 가 단위만 바뀌어 돌아온 것.

## 11. Wear leveling (I.10)

목표: erase/program 을 모든 block 에 고르게 → 모든 block 이 대략 같은 때 닳도록.

- log 구조와 GC 만으로도 쓰기는 꽤 퍼진다(**dynamic wear leveling**: 새로 쓸 block 을 고를 때 erase 수가 적은 free block 을 고름).
- 문제는 **오래 사는 cold 데이터**. 한 번 쓰고 안 바뀌는 데이터가 앉은 block 은 GC victim 이 되지 않으니 erase 수가 안 늘고, 나머지 block 이 그만큼 더 닳는다.
- 해결(**static wear leveling**): 주기적으로 그런 block 의 live 데이터를 **강제로 옮겨** 그 block 을 회전에 다시 넣는다. 추가 쓰기라서 WAF 가 늘고 성능이 약간 준다.

아래 실험에서: 80/20 hot/cold 워크로드에서 dynamic WL 만 쓰면 erase count 가 107 ~ 155(편차 48), static WL 을 켜면 134 ~ 154(편차 20)로 좁아지고, 대가로 WAF 가 4.94 → 5.14 로 오른다.

## 12. SSD 성능과 비용 (I.11)

| 장치 | 랜덤 읽기 (MB/s) | 랜덤 쓰기 | 순차 읽기 | 순차 쓰기 |
|---|---|---|---|---|
| Samsung 840 Pro SSD | 103 | 287 | 421 | 384 |
| Seagate 600 SSD | 84 | 252 | 424 | 374 |
| Intel SSD 335 SSD | 39 | 222 | 344 | 354 |
| Seagate Savvio 15K.3 HDD | 2 | 2 | 223 | 223 |

- 가장 극적인 차이는 **랜덤 I/O**: SSD 는 수십~수백 MB/s, 최고급 HDD 는 2 MB/s.
- 순차는 차이가 작다 — 순차만 필요하면 HDD 도 괜찮다.
- **SSD 의 랜덤 쓰기가 랜덤 읽기보다 빠르다!** log 구조가 랜덤 쓰기를 순차 append 로 바꾸고, 쓰기는 버퍼에서 바로 완료 + 여러 die 에 striping 되기 때문. 반면 QD1 랜덤 읽기는 tR(수십 µs)을 그대로 기다려야 한다.
- 순차/랜덤 차이가 줄었을 뿐 사라지지 않았으므로, HDD 시절의 FS 기법(순차화)은 SSD 에서도 여전히 유효하다.

비용(2015년 기준): SSD 250GB $150 → **60¢/GB**, HDD 1TB $50 → **5¢/GB**, 10배 이상. 그래서 대규모 저장은 HDD, 성능은 SSD, 그리고 **hot 데이터는 SSD, cold 데이터는 HDD** 로 섞는 계층형 구성이 나온다.

## 13. 직접 해보기

### 13.1 C: toy page-mapped FTL (GC + wear leveling + 전원 손실 복구)

NAND 규칙(ERASED page 에만 program, block 안에서는 순서대로)을 코드로 **강제** 하고 어기면 즉시 종료한다. FTL 은 page-level L2P, OOB 에 (LPN, seq), host 쓰기와 GC 복사를 **서로 다른 open block(stream)** 으로 분리, free block 2개 이하에서 greedy GC, erase 수 최소 free block 할당(dynamic WL), 옵션으로 static WL.

```bash
cc -Wall -Wextra -O0 code/C0I_toy_ftl.c -o .work/bin/C0I_toy_ftl
.work/bin/C0I_toy_ftl
```

```c
// C0I_toy_ftl.c — OSTEP 부록 I: page-mapped, log-structured toy FTL + greedy GC + wear leveling
//
//  NAND 규칙을 코드로 강제한다:
//    * erase 단위 = block, program 단위 = page
//    * ERASED 페이지에만 program 가능, block 안에서는 낮은 page → 높은 page 순서로만 (program disturb 회피)
//  FTL:
//    * L2P(logical→physical) 테이블은 RAM, 각 page 의 OOB 에 (LPN, seq) 를 같이 기록
//    * host write 는 "host open block" 에 append, GC 복사는 별도 "GC open block" 에 append
//    * free block 이 low-water 아래로 내려가면 greedy GC (valid page 가 가장 적은 block 을 victim)
//    * free block 할당 시 erase count 가 가장 작은 것 선택 (dynamic wear leveling)
//    * 옵션: static wear leveling (오래 안 바뀌는 cold block 을 강제로 옮겨 erase 풀에 다시 넣기)
//  실험:
//    [1] 책 I.7~I.8 예제 재현 (4 page/block, LBA 100,101,2000,2001 → overwrite → GC)
//    [2] uniform random 4KB write: over-provisioning(OP) 별 WAF
//    [3] 80/20 hot/cold: static wear leveling 유무에 따른 erase count 편차
//    [4] power-loss: L2P 를 버리고 OOB scan 으로 재구성 (+ TRIM 이 로그되지 않으면 생기는 문제)
//    [5] mapping table 크기 계산
//
// build: cc -Wall -Wextra -O0 code/C0I_toy_ftl.c -o .work/bin/C0I_toy_ftl
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum { PG_ERASED = 0, PG_PROGRAMMED = 1 };
enum { BLK_FREE = 0, BLK_OPEN = 1, BLK_CLOSED = 2 };
#define NONE (-1)

typedef struct {               // physical page = data + OOB(spare area)
    uint8_t  state;
    int32_t  oob_lpn;          // 이 page 에 들어 있는 logical page 번호
    uint32_t oob_seq;          // 전역 증가 시퀀스 (복구 시 최신본 판별)
    uint32_t data;             // 4KB 대신 32bit 토큰 (검증용)
} page_t;

typedef struct {
    int ppb, nblk, nlpn;
    page_t  *pg;               // nblk*ppb
    int32_t *l2p;              // nlpn
    int     *valid, *erase_cnt, *bstate, *next_pg;
    int     host_open, gc_open, nfree;
    uint32_t seq;
    uint32_t *ver;             // lpn 별 최신 버전 (검증용, 호스트 쪽 진실)
    // stats
    long host_w, nand_w, nand_r, erases, gc_runs, gc_copied, wl_moves;
    double victim_valid_sum;
    int static_wl, wl_thresh;
    int verbose, gc_same_stream;
} ftl_t;

static uint64_t rs = 0x2545F4914F6CDD1Dull;
static uint32_t rnd(void) { rs ^= rs << 13; rs ^= rs >> 7; rs ^= rs << 17; return (uint32_t)(rs >> 11); }

// ------------------------------------------------------------ raw NAND ops
static int nand_erase(ftl_t *f, int b) {
    for (int i = 0; i < f->ppb; i++) { page_t *p = &f->pg[b * f->ppb + i]; p->state = PG_ERASED; p->oob_lpn = NONE; }
    f->next_pg[b] = 0; f->erase_cnt[b]++; f->erases++; f->valid[b] = 0;
    return 0;
}
static int nand_program(ftl_t *f, int ppn, int32_t lpn, uint32_t data) {
    int b = ppn / f->ppb, off = ppn % f->ppb;
    if (f->pg[ppn].state != PG_ERASED) { fprintf(stderr, "PROGRAM ERROR: page %d not erased\n", ppn); exit(2); }
    if (off != f->next_pg[b])           { fprintf(stderr, "PROGRAM ERROR: out-of-order page %d\n", ppn); exit(2); }
    f->pg[ppn] = (page_t){ PG_PROGRAMMED, lpn, ++f->seq, data };
    f->next_pg[b]++; f->nand_w++;
    return 0;
}

// ------------------------------------------------------------ FTL
static ftl_t *ftl_new(int nblk, int ppb, int nlpn) {
    ftl_t *f = calloc(1, sizeof *f);
    f->nblk = nblk; f->ppb = ppb; f->nlpn = nlpn;
    f->pg = calloc((size_t)nblk * ppb, sizeof(page_t));
    f->l2p = malloc(sizeof(int32_t) * nlpn); f->ver = calloc(nlpn, sizeof(uint32_t));
    f->valid = calloc(nblk, sizeof(int)); f->erase_cnt = calloc(nblk, sizeof(int));
    f->bstate = calloc(nblk, sizeof(int)); f->next_pg = calloc(nblk, sizeof(int));
    for (int i = 0; i < nlpn; i++) f->l2p[i] = NONE;
    for (int i = 0; i < nblk * ppb; i++) { f->pg[i].state = PG_PROGRAMMED; f->pg[i].oob_lpn = NONE; } // 공장 출하: INVALID
    f->nfree = nblk; f->host_open = f->gc_open = NONE;
    return f;
}
static void ftl_free(ftl_t *f) {
    free(f->pg); free(f->l2p); free(f->ver); free(f->valid); free(f->erase_cnt);
    free(f->bstate); free(f->next_pg); free(f);
}

static int alloc_block(ftl_t *f) {       // erase count 최소인 free block (dynamic WL)
    int best = NONE;
    for (int b = 0; b < f->nblk; b++)
        if (f->bstate[b] == BLK_FREE && (best == NONE || f->erase_cnt[b] < f->erase_cnt[best])) best = b;
    if (best == NONE) { fprintf(stderr, "FATAL: no free block\n"); exit(3); }
    nand_erase(f, best);                 // 쓰기 직전에 erase (OSTEP 예제와 같은 순서)
    f->bstate[best] = BLK_OPEN; f->nfree--;
    if (f->verbose) printf("    erase(block %d)\n", best);
    return best;
}

static void invalidate(ftl_t *f, int32_t lpn) {
    if (f->l2p[lpn] != NONE) { f->valid[f->l2p[lpn] / f->ppb]--; f->l2p[lpn] = NONE; }
}

// stream: 0 = host, 1 = GC
static int append(ftl_t *f, int stream, int32_t lpn, uint32_t data) {
    int *open = stream ? &f->gc_open : &f->host_open;
    if (*open == NONE || f->next_pg[*open] == f->ppb) {
        if (*open != NONE) f->bstate[*open] = BLK_CLOSED;
        *open = alloc_block(f);
    }
    int ppn = *open * f->ppb + f->next_pg[*open];
    nand_program(f, ppn, lpn, data);
    invalidate(f, lpn);
    f->l2p[lpn] = ppn; f->valid[*open]++;
    return ppn;
}

static void collect_block(ftl_t *f, int victim) {
    f->gc_runs++; f->victim_valid_sum += (double)f->valid[victim] / f->ppb;
    if (f->verbose) printf("    GC victim block %d (valid %d/%d)\n", victim, f->valid[victim], f->ppb);
    for (int i = 0; i < f->ppb; i++) {
        int ppn = victim * f->ppb + i; page_t *p = &f->pg[ppn];
        if (p->state == PG_PROGRAMMED && p->oob_lpn != NONE && f->l2p[p->oob_lpn] == ppn) { // live?
            f->nand_r++; f->gc_copied++;
            int to = append(f, f->gc_same_stream ? 0 : 1, p->oob_lpn, p->data);
            if (f->verbose) printf("    copy live LPN %d: page %d -> page %d\n", p->oob_lpn, ppn, to);
        }
    }
    f->bstate[victim] = BLK_FREE; f->nfree++;   // erase 는 다음 할당 때 (lazy erase)
}

static void maybe_gc(ftl_t *f) {
    while (f->nfree < 2) {                        // low-water mark: free block 2개 유지
        int victim = NONE;
        for (int b = 0; b < f->nblk; b++)
            if (f->bstate[b] == BLK_CLOSED && (victim == NONE || f->valid[b] < f->valid[victim])) victim = b;
        if (victim == NONE) { fprintf(stderr, "FATAL: nothing to collect\n"); exit(4); }
        collect_block(f, victim);
    }
    if (f->static_wl) {                           // static WL: 가장 덜 닳은 closed block 이 너무 뒤처지면 강제 이주
        int maxe = 0, cold = NONE;
        for (int b = 0; b < f->nblk; b++) if (f->erase_cnt[b] > maxe) maxe = f->erase_cnt[b];
        for (int b = 0; b < f->nblk; b++)
            if (f->bstate[b] == BLK_CLOSED && (cold == NONE || f->erase_cnt[b] < f->erase_cnt[cold])) cold = b;
        if (cold != NONE && maxe - f->erase_cnt[cold] > f->wl_thresh) { f->wl_moves++; collect_block(f, cold); }
    }
}

static void ftl_write(ftl_t *f, int32_t lpn) {
    maybe_gc(f);
    f->host_w++; f->ver[lpn]++;
    int ppn = append(f, 0, lpn, (uint32_t)lpn * 2654435761u ^ f->ver[lpn]);
    if (f->verbose) printf("  write(LBA %d) -> page %d\n", lpn, ppn);
}
static void ftl_trim(ftl_t *f, int32_t lpn) { invalidate(f, lpn); f->ver[lpn] = 0; }

static int ftl_verify(ftl_t *f) {             // 모든 LPN 을 읽어서 최신 버전인지 확인
    int bad = 0;
    for (int l = 0; l < f->nlpn; l++) {
        if (f->ver[l] == 0) { if (f->l2p[l] != NONE) bad++; continue; }
        if (f->l2p[l] == NONE || f->pg[f->l2p[l]].data != ((uint32_t)l * 2654435761u ^ f->ver[l])) bad++;
    }
    return bad;
}

static void erase_stats(ftl_t *f, int *mn, int *mx, double *avg) {
    *mn = 1 << 30; *mx = 0; long s = 0;
    for (int b = 0; b < f->nblk; b++) { int e = f->erase_cnt[b]; s += e; if (e < *mn) *mn = e; if (e > *mx) *mx = e; }
    *avg = (double)s / f->nblk;
}

// ------------------------------------------------------------ experiments
static void exp_book_example(void) {
    printf("[1] OSTEP I.7-I.8 example: 4 pages/block, write 100,101,2000,2001 then overwrite 100,101\n");
    ftl_t *f = ftl_new(4, 4, 4096); f->verbose = 1; f->gc_same_stream = 1;  // 책처럼 로그 하나
    int seq[] = { 100, 101, 2000, 2001, 100, 101 };
    for (int i = 0; i < 6; i++) { f->ver[seq[i]]++; f->host_w++; int p = append(f, 0, seq[i], (uint32_t)seq[i] * 2654435761u ^ f->ver[seq[i]]); printf("  write(LBA %d) -> page %d\n", seq[i], p); }
    printf("  map: 100->%d 101->%d 2000->%d 2001->%d ; block0 valid=%d (pages 0,1 are garbage)\n",
           f->l2p[100], f->l2p[101], f->l2p[2000], f->l2p[2001], f->valid[0]);
    collect_block(f, 0);
    printf("  after GC: 2000->%d 2001->%d ; block0 free again, verify errors = %d\n\n",
           f->l2p[2000], f->l2p[2001], ftl_verify(f));
    ftl_free(f);
}

static void run_random(int nblk, int ppb, double op, int skew, int static_wl, long writes_mult,
                       const char *label, int print_wear) {
    int phys = nblk * ppb;
    int nlpn = (int)(phys / (1.0 + op));       // OP = (phys - user) / user
    ftl_t *f = ftl_new(nblk, ppb, nlpn);
    f->static_wl = static_wl; f->wl_thresh = 20;
    for (int l = 0; l < nlpn; l++) ftl_write(f, l);       // 1회 순차 채우기 (preconditioning)
    long w0 = f->host_w, n0 = f->nand_w;
    long total = writes_mult * nlpn;
    for (long i = 0; i < total; i++) {
        int32_t lpn;
        if (skew) lpn = (rnd() % 100 < 80) ? (int32_t)(rnd() % (nlpn / 5)) : (int32_t)(nlpn / 5 + rnd() % (nlpn - nlpn / 5));
        else      lpn = (int32_t)(rnd() % nlpn);
        ftl_write(f, lpn);
    }
    double waf = (double)(f->nand_w - n0) / (f->host_w - w0);
    double u = f->gc_runs ? f->victim_valid_sum / f->gc_runs : 0;
    int mn, mx; double avg; erase_stats(f, &mn, &mx, &avg);
    // MLC 숫자(Figure I.2): read 50us, program 600us, erase 3000us. 한 die 직렬 가정
    double us_per_host_w = (f->nand_w * 600.0 + f->nand_r * 50.0 + f->erases * 3000.0) / f->host_w;
    printf("  %-26s OP=%5.1f%%  WAF=%5.2f  victim valid u=%.2f  1/(1-u)=%5.2f  ~%5.0f us/host-write",
           label, op * 100, waf, u, u < 1 ? 1 / (1 - u) : 0.0, us_per_host_w);
    if (print_wear) printf("\n      erase count min/avg/max = %d / %.1f / %d   static-WL moves = %ld",
                           mn, avg, mx, f->wl_moves);
    printf("   verify=%s\n", ftl_verify(f) ? "FAIL" : "ok");
    ftl_free(f);
}

static void exp_recovery(void) {
    printf("[4] power loss: throw away L2P, rebuild from OOB scan (latest seq wins)\n");
    int nblk = 64, ppb = 32; int nlpn = (int)(nblk * ppb / 1.25);
    ftl_t *f = ftl_new(nblk, ppb, nlpn);
    for (long i = 0; i < 5L * nlpn; i++) ftl_write(f, (int32_t)(rnd() % nlpn));
    int trimmed = 0;
    for (int l = 0; l < nlpn; l += 37) if (f->ver[l]) { ftl_trim(f, l); trimmed++; }
    int32_t *rebuilt = malloc(sizeof(int32_t) * nlpn); uint32_t *best = calloc(nlpn, sizeof(uint32_t));
    for (int l = 0; l < nlpn; l++) rebuilt[l] = NONE;
    long scanned = 0;
    for (int p = 0; p < nblk * ppb; p++) {
        page_t *pg = &f->pg[p];
        if (pg->state != PG_PROGRAMMED || pg->oob_lpn == NONE) continue;
        scanned++;
        if (pg->oob_seq > best[pg->oob_lpn]) { best[pg->oob_lpn] = pg->oob_seq; rebuilt[pg->oob_lpn] = p; }
    }
    int diff = 0, resurrected = 0;
    for (int l = 0; l < nlpn; l++) {
        if (rebuilt[l] != f->l2p[l]) { diff++; if (f->l2p[l] == NONE) resurrected++; }
    }
    printf("  scanned %ld programmed pages' OOB, L2P entries differing = %d, of which TRIMmed-but-resurrected = %d (trimmed %d)\n",
           scanned, diff, resurrected, trimmed);
    printf("  -> 덮어쓴 데이터는 seq 로 정확히 복구되지만, TRIM 은 NAND 에 흔적이 없어서 되살아난다.\n");
    printf("     실제 FW 는 TRIM/unmap 도 journal(또는 L2P checkpoint+delta log)에 남긴다.\n\n");
    free(rebuilt); free(best); ftl_free(f);
}

int main(void) {
    exp_book_example();

    printf("[2] uniform random 4KB overwrite, 256 blocks x 64 pages, 8x capacity after fill\n");
    double ops[] = { 0.07, 0.125, 0.28, 0.50, 1.00 };
    for (int i = 0; i < 5; i++) run_random(256, 64, ops[i], 0, 0, 8, "uniform", 0);

    printf("\n[3] 80/20 hot/cold skew, OP=12.5%%, 256 x 64, 30x capacity\n");
    run_random(256, 64, 0.125, 1, 0, 30, "80/20, dynamic WL only", 1);
    run_random(256, 64, 0.125, 1, 1, 30, "80/20, + static WL(th=20)", 1);
    run_random(256, 64, 0.125, 0, 0, 30, "uniform (reference)", 1);
    printf("\n");

    exp_recovery();

    printf("[5] mapping table size (4-byte entries)\n");
    double cap = 1024.0 * 1024 * 1024 * 1024;   // 1 TiB
    printf("  page-level  (4KB) : %6.0f MiB\n", cap / 4096 * 4 / (1 << 20));
    printf("  block-level (256KB): %6.0f MiB\n", cap / (256 * 1024) * 4 / (1 << 20));
    printf("  4TB SSD page-level: %6.0f MiB  -> \"DRAM 1GB per 1TB\" 경험칙\n", 4 * cap / 4096 * 4 / (1 << 20));
    return 0;
}
```

실제 출력:

```text
[1] OSTEP I.7-I.8 example: 4 pages/block, write 100,101,2000,2001 then overwrite 100,101
    erase(block 0)
  write(LBA 100) -> page 0
  write(LBA 101) -> page 1
  write(LBA 2000) -> page 2
  write(LBA 2001) -> page 3
    erase(block 1)
  write(LBA 100) -> page 4
  write(LBA 101) -> page 5
  map: 100->4 101->5 2000->2 2001->3 ; block0 valid=2 (pages 0,1 are garbage)
    GC victim block 0 (valid 2/4)
    copy live LPN 2000: page 2 -> page 6
    copy live LPN 2001: page 3 -> page 7
  after GC: 2000->6 2001->7 ; block0 free again, verify errors = 0

[2] uniform random 4KB overwrite, 256 blocks x 64 pages, 8x capacity after fill
  uniform                    OP=  7.0%  WAF= 8.01  victim valid u=0.88  1/(1-u)= 8.06  ~ 4987 us/host-write   verify=ok
  uniform                    OP= 12.5%  WAF= 4.68  victim valid u=0.79  1/(1-u)= 4.74  ~ 2928 us/host-write   verify=ok
  uniform                    OP= 28.0%  WAF= 2.44  victim valid u=0.60  1/(1-u)= 2.50  ~ 1542 us/host-write   verify=ok
  uniform                    OP= 50.0%  WAF= 1.68  victim valid u=0.42  1/(1-u)= 1.72  ~ 1067 us/host-write   verify=ok
  uniform                    OP=100.0%  WAF= 1.22  victim valid u=0.20  1/(1-u)= 1.26  ~  786 us/host-write   verify=ok

[3] 80/20 hot/cold skew, OP=12.5%, 256 x 64, 30x capacity
  80/20, dynamic WL only     OP= 12.5%  WAF= 4.94  victim valid u=0.80  1/(1-u)= 4.96  ~ 3306 us/host-write
      erase count min/avg/max = 107 / 132.7 / 155   static-WL moves = 0   verify=ok
  80/20, + static WL(th=20)  OP= 12.5%  WAF= 5.14  victim valid u=0.81  1/(1-u)= 5.15  ~ 3438 us/host-write
      erase count min/avg/max = 134 / 137.9 / 154   static-WL moves = 7543   verify=ok
  uniform (reference)        OP= 12.5%  WAF= 4.73  victim valid u=0.79  1/(1-u)= 4.74  ~ 3160 us/host-write
      erase count min/avg/max = 110 / 126.9 / 141   static-WL moves = 0   verify=ok

[4] power loss: throw away L2P, rebuild from OOB scan (latest seq wins)
  scanned 2044 programmed pages' OOB, L2P entries differing = 45, of which TRIMmed-but-resurrected = 45 (trimmed 45)
  -> 덮어쓴 데이터는 seq 로 정확히 복구되지만, TRIM 은 NAND 에 흔적이 없어서 되살아난다.
     실제 FW 는 TRIM/unmap 도 journal(또는 L2P checkpoint+delta log)에 남긴다.

[5] mapping table size (4-byte entries)
  page-level  (4KB) :   1024 MiB
  block-level (256KB):     16 MiB
  4TB SSD page-level:   4096 MiB  -> "DRAM 1GB per 1TB" 경험칙
```

해석:

- **[1]** 책 I.7–I.8 의 상태 변화를 그대로 재현한다(이 실험만 GC 복사를 같은 log 에 쓰도록 `gc_same_stream=1`). 100,101 덮어쓰기 후 block 0 에 live 2개, GC 가 2000→6, 2001→7 로 옮기고 block 0 을 회수.
- **[2] OP 와 WAF**: OP 7% → WAF **8.0**, 12.5% → 4.7, 28% → 2.4, 50% → 1.7, 100% → 1.2. 그리고 매 GC 의 victim live 비율 평균 u 로 계산한 1/(1−u) 가 실측 WAF 와 0.1 이내로 맞는다(9.3 의 식). 마지막 열은 Figure I.2 MLC 값으로 환산한 "host 쓰기 1개당 NAND 시간"(한 die 직렬 가정) — OP 7% 에서는 4KB 쓰기 하나에 NAND 가 5 ms 를 쓴다. 소비자 SSD 가 꽉 차면 느려지는 이유다.
- **[3] hot/cold 와 wear leveling**: 80/20 skew 인데 greedy GC 의 WAF 가 uniform(4.73)보다 **오히려 높다**(4.94). hot 과 cold 가 같은 block 에 섞여 써지면, cold page 가 victim 마다 끌려다니며 복사되기 때문이다. greedy 는 skew 에 약하다는 알려진 결과이고, 실제 FW 가 hot/cold(또는 stream, 나이)로 **쓰기 목적지를 분리** 하는 이유다. static WL 은 erase 편차를 48 → 20 으로 줄였고 WAF 는 0.2 늘었다. (임계값 20 이 낮아 static WL 이주가 7,543번 일어났다 — 실제 FW 는 이주 빈도에 rate limit 을 둔다.)
- **[4] 전원 손실**: RAM 의 L2P 를 버리고 모든 page 의 OOB 를 스캔해 "LPN 별 최대 seq" 로 재구성. 덮어쓰기로 생긴 옛 사본들은 seq 덕분에 정확히 걸러졌지만, **TRIM 한 45개 LPN 이 전부 되살아났다**. TRIM 은 NAND 에 아무것도 쓰지 않으니 OOB 스캔으로는 알 수 없다. 실제 FW 는 TRIM/unmap 을 L2P 저널(delta log)에 기록하고, 체크포인트 + 저널 replay 로 복구한다.
- **[5]** 10.1 의 계산.

### 13.2 OSTEP 시뮬레이터: ssd.py

이 부록에는 원문 Homework 가 없지만 `file-ssd/ssd.py` 가 있다(README 기준으로 사용). 시뮬레이터 FTL 은 page 매핑 하나뿐이고 `-T ideal|direct|log` 로 바꾼다. 기본 시간: read 10, program 40, erase 1000 (µs).

**direct vs log — 같은 논리 page 1 을 세 번 덮어쓰기**

```bash
cd .tools/ostep-homework/file-ssd
python3 ssd.py -T direct -l 30 -B 3 -p 10 -L w0:a,w1:b,w2:c,w1:B,w1:X -C -S -c
python3 ssd.py -T log    -l 30 -B 3 -p 10 -L w0:a,w1:b,w2:c,w1:B,w1:X -C -S -c
```

```text
(direct)
FTL     0:  0   1:  1   2:  2
Block 0          1          2
Page  0000000000 1111111111 2222222222
      0123456789 0123456789 0123456789
State vvvEEEEEEE iiiiiiiiii iiiiiiiiii
Data  aXc
Live  +++

Physical Operations Per Block
Erases   5          0          0          Sum: 5
Writes  12          0          0          Sum: 12
Reads    9          0          0          Sum: 9
...
Times
  Erase time 5000.00
  Write time 480.00
  Read time  90.00
  Total time 5570.00

(log)
FTL     0:  0   1:  4   2:  2
Block 0          1          2
Page  0000000000 1111111111 2222222222
      0123456789 0123456789 0123456789
State vvvvvEEEEE iiiiiiiiii iiiiiiiiii
Data  abcBX
Live  + + +

Physical Operations Per Block
Erases   1          0          0          Sum: 1
Writes   5          0          0          Sum: 5
Reads    0          0          0          Sum: 0
...
Times
  Erase time 1000.00
  Write time 200.00
  Read time  0.00
  Total time 1200.00
```

(ARG 줄과 cmd 줄, Logical Operation Sums 는 생략.) direct 는 논리 쓰기 5번에 **erase 5번, program 12번, read 9번**: 쓰기마다 block 0 의 live page 를 읽고(0, 1, 2, 3, 3개…), erase 하고, 다 다시 program. log 는 **erase 1번, program 5번**, page 1, 3 은 쓰레기(Live 줄에 + 없음)로 남아 나중에 GC 대상. 시간 5570 vs 1200 (4.6배).

**GC 와 over-provisioning**: 논리 30 page, 쓰기만 200번(`-P 0/100/0`), 물리 block 수를 4(OP 33%) vs 6(OP 100%)으로.

```bash
python3 ssd.py -T log -l 30 -B 4 -p 10 -s 10 -n 200 -P 0/100/0 -G 4 -g 3 -S -c
python3 ssd.py -T log -l 30 -B 6 -p 10 -s 10 -n 200 -P 0/100/0 -G 6 -g 5 -S -c
```

```text
(B=4)
Physical Operations Per Block
Erases  37         34         25         39          Sum: 135
Writes 370        340        250        380          Sum: 1340
Reads  314        288        202        336          Sum: 1140

Logical Operation Sums
  Write count 1340 (0 failed)
...
  Total time 200000.00

(B=6)
Physical Operations Per Block
Erases   5          5          5          5          4          4          Sum: 28
Writes  50         50         48         40         40         40          Sum: 268
Reads   17         11          9         16          4         11          Sum: 68

Logical Operation Sums
  Write count 268 (0 failed)
...
  Total time 39400.00
```

주의: 이 시뮬레이터의 "Logical Operation Sums - Write count" 는 GC 가 내부적으로 부르는 `write()` 까지 센다(ssd.py 의 `garbage_collect` 가 `self.write()` 를 호출). 그래서 호스트 쓰기는 200 이고, GC 복사는 Reads 합계(1140 / 68)와 같다.

- B=4: WAF = 1340 / 200 = **6.7**, erase 135.
- B=6: WAF = 268 / 200 = **1.34**, erase 28, 총 시간 1/5.

OP 를 33% → 100% 로 늘리자 WAF 가 5배 줄었다. (B=3, 즉 OP 0% 로 돌리면 GC 가 공간을 못 만들어 "Write count … (56 failed)" — 장치가 꽉 차서 쓰기가 실패한다. OP 없는 log-structured FTL 은 동작 자체가 불가능하다는 것.) 시뮬레이터 GC 는 victim 을 "다음 순서의 block" 으로 고르는 단순한 방식이라, 13.1 의 greedy 보다 WAF 가 높게 나온다.

`-J` 를 붙이면 GC 의 내부 동작이 보인다(앞부분만):

```bash
python3 ssd.py -T log -l 30 -B 3 -p 10 -s 10 -n 60 -G 3 -g 2 -J -c
```

```text
gc 0:: read(physical_page=3)
gc 0:: write()
gc 0:: read(physical_page=6)
gc 0:: write()
gc 0:: read(physical_page=7)
gc 0:: write()
gc 0:: read(physical_page=9)
gc 0:: write()
gc 0:: erase(block=0)
gc 1:: read(physical_page=11)
...
```

block 0 에서 live page 4개(3, 6, 7, 9)를 읽어 log 끝에 다시 쓰고 block 0 을 erase — 9.2 의 절차 그대로다.

## 14. 펌웨어 엔지니어의 눈으로

이 부록은 Don 의 7년이 교과서 20쪽으로 압축된 것이다. 책이 생략한 부분을 실제 FW 의 설계 결정으로 채우면:

- **전원 손실 복구 = 이 부록의 진짜 본론.** 책은 "OOB 스캔, 또는 로깅·체크포인트" 한 문단이지만, 실제 FW 는 (1) L2P 를 주기적으로 NAND 에 **체크포인트**(전체 또는 dirty segment 단위), (2) 그 사이의 변경을 **L2P 저널/delta log** 로 기록(TRIM, 블록 상태 변경 포함 — 위 [4] 실험이 이게 왜 필요한지 보여 줌), (3) 부팅 시 체크포인트 로드 → 저널 replay → **마지막 open block 들만 OOB 스캔**(체크포인트 이후 쓰인 page). 여기에 **PLP 캐패시터** 가 있으면 전원 손실 감지(PLI) 후 수 ms 안에 쓰기 버퍼와 dirty 매핑을 덤프한다. 복구 시간(TTR) 요구사항이 체크포인트 주기를 정한다.
- **open block 과 미완성 page.** 전원 손실 시 program 중이던 page(그리고 MLC/TLC 에서는 같은 워드라인을 공유하는 **이미 써 둔 lower page** 까지 — paired page 문제)가 깨질 수 있다. 그래서 FW 는 open block 의 마지막 쓰기 영역을 신뢰하지 않고 복구 후 다른 block 으로 옮긴다. 책의 "page 를 순서대로 program" 은 disturb 뿐 아니라 이 복구를 위해서도 필요하다.
- **GC 정책은 WAF 와 tail latency 의 trade-off.** 실험 [2] 의 OP-WAF 곡선이 바로 데이터센터 SSD 가 OP 7%(1DWPD) vs 28%(3DWPD) 제품으로 나뉘는 이유다. 실무에서는 victim 선택(greedy vs cost-benefit with age), **GC 와 호스트 쓰기의 비율 조절**(free block 수에 따라 host write 당 GC page 수를 동적으로 — 급하게 하면 p99 가 튐), **erase/program suspend** 로 읽기 지연 보호, 그리고 hot/cold 분리(실험 [3] 에서 섞으면 skew 에서도 WAF 가 나빠지는 것)를 위한 **multi-stream / FDP(Flexible Data Placement)** 지원이 핵심 설계 포인트다.
- **SLC 캐시.** 책 결론부의 "Samsung 이 TLC 와 SLC 를 한 SSD 에 섞는다" 가 지금은 표준이다. TLC block 일부를 SLC 모드로 써서(빠른 program, 높은 내구성) 쓰기 버스트를 받고, idle 때 TLC 로 **folding** 한다. 버스트가 캐시 크기를 넘으면 쓰기 속도가 "절벽"처럼 떨어지는 것이 벤치마크에서 보이는 SLC cache cliff 다.
- **신뢰성 관리 루프.** read disturb 카운터(block 별 read 횟수가 임계를 넘으면 refresh/relocation), retention 대비 **background patrol read**(ECC 에러 비트 수가 커지면 미리 옮김), **read retry 테이블 + LDPC soft decode**, **bad block 관리**(공장 bad block + grown bad block, 예비 block 풀), 다이 고장 대비 **die-level XOR(RAID-5)** — [Ch.38](2026-09-30_C38_raid.md)의 parity 가 SSD 안에 들어온 것. 이 모든 것이 추가 쓰기를 만들어 WAF 에 더해진다.
- **TRIM 과 호스트 협력.** 실험 [4] 처럼 TRIM 은 "흔적 없는 메타데이터 변경"이라 저널링 대상이다. 호스트 쪽에서는 [Ch.39](2026-09-30_C39_files_and_directories.md)의 unlink 가 discard 로 내려와야 FTL 이 쓰레기를 쓰레기로 안다. ZNS 는 한 걸음 더 나가 L2P 와 GC 자체를 호스트(F2FS, RocksDB ZenFS)로 넘긴다 — 매핑 DRAM 과 OP 가 거의 필요 없어진다. 책 TIP 의 "인터페이스를 다시 생각하기" 의 현재형이다.
- **온디바이스 LLM 과의 연결.** 휴대기기에서 DRAM 보다 큰 모델을 돌리려면 가중치를 flash 에서 필요할 때 읽어야 한다(Apple 의 "LLM in a flash" 연구가 이 문제를 다룬다). 이때 성능은 이 부록의 숫자로 결정된다: **랜덤 작은 read 는 tR 에 묶이고 큰 순차 read 는 채널·die 병렬성으로 대역폭이 난다** → 가중치를 행/열 묶음으로 크게, 연속으로 배치하고, 재사용되는 부분은 DRAM 에 캐시. 읽기 위주라 WAF 는 문제가 아니지만 **read disturb 관리** 가 중요해진다. 데이터센터 쪽에서도 GPUDirect Storage(NVMe → GPU 메모리 DMA, [Ch.36](2026-09-30_C36_io_devices.md))와 KV-cache 를 SSD 로 내리는 시도가 같은 숫자 위에 있다.

## 15. 면접 질문

### Q1. SSD 에서 write amplification 이 생기는 원인과 줄이는 방법을 FW 관점에서 설명하라.
<details>
<summary>답 보기</summary>

원인: NAND 는 **덮어쓰기 불가 + erase 는 block 단위** → log-structured 로 쓰면 덮어쓰기가 쓰레기를 만들고, **GC 가 victim 의 live page 를 복사** 한다. victim live 비율이 u 면 **WAF = 1/(1−u)**. 그 밖에 static wear leveling 이주, read disturb/retention refresh, 매핑 메타데이터·저널 쓰기, 매핑 단위보다 작은 쓰기의 RMW, 미완성 page 패딩도 더한다. 줄이는 법: **OP 늘리기**(u↓), victim 선택(greedy/cost-benefit), **hot/cold·stream 분리**(같이 죽을 데이터를 같은 block 에), **TRIM** 반영, 쓰기 버퍼로 작은 쓰기 병합, 정렬된 쓰기, ZNS/FDP 로 호스트가 배치 힌트 제공. 측정은 SMART/로그의 NAND writes ÷ host writes.

</details>

### Q2. page-level, block-level, hybrid 매핑을 비교하라. 1TB SSD 의 page-level L2P 는 얼마나 큰가?
<details>
<summary>답 보기</summary>

page-level: 4KB 마다 엔트리, 1TB → 2^28 × 4B = **1 GiB DRAM**. 어떤 쓰기든 아무 데나 append 가능해 WAF 최소. block-level: block 당 엔트리(256KB 면 64배 작은 16 MiB)지만, block 보다 작은 쓰기마다 block 전체 복사 → WAF 폭증. hybrid: 소수의 **log block 만 page 매핑**, 나머지는 block 매핑; 순차 덮어쓰기는 **switch merge**(추가 I/O 0), 부분은 **partial merge**, 랜덤은 **full merge**(최악). 현대 해법: DRAM 1:1000 page 매핑, DRAM-less 는 **DFTL 식 캐시 + HMB**, 대용량 QLC 는 **indirection unit 확대**(16–64KB)로 DRAM↓ 대신 작은 쓰기 RMW.

</details>

### Q3. 전원이 갑자기 꺼졌다. FTL 은 부팅 후 L2P 를 어떻게 복구하나? TRIM 은?
<details>
<summary>답 보기</summary>

기본: 각 page 의 OOB 에 **(LPN, 단조 증가 seq)** 를 기록 → 전체 스캔해 LPN 별 최대 seq 의 page 를 채택. 너무 느리므로 실제로는 **주기적 L2P 체크포인트 + 이후 변경의 저널(delta log)** 을 NAND 에 남기고, 부팅 시 체크포인트 로드 → 저널 replay → 체크포인트 이후 쓰인 **open block 만 OOB 스캔**. **TRIM 은 NAND 에 데이터를 안 쓰므로 OOB 로는 복구 불가** → 저널에 unmap 레코드를 남겨야 하며, 안 그러면 삭제한 데이터가 되살아난다(toy FTL 실험에서 45/45 재현). 추가로 open block 의 마지막 page·paired page 는 깨졌을 수 있어 검증 후 재배치, PLP 가 있으면 버퍼와 dirty 매핑을 캐패시터 전력으로 덤프.

</details>

### Q4. SSD 의 랜덤 쓰기가 랜덤 읽기보다 빠른 경우가 많은 이유는?
<details>
<summary>답 보기</summary>

쓰기는 (1) **쓰기 버퍼(DRAM/SRAM)에 들어가는 즉시 완료** 를 보고할 수 있고, (2) log-structured FTL 이 랜덤 LBA 를 **순차 append** 로 바꿔 여러 die/plane 에 **striping** 하므로 program 지연(수백 µs)이 병렬로 숨겨진다. 읽기는 데이터가 있는 **특정 die 의 tR(수십 µs) + ECC 디코드** 를 피할 수 없고, QD 가 낮으면 병렬성도 없다. 그래서 QD1 랜덤 읽기가 SSD 의 근본 지연을 드러내는 지표다. 단, 장치가 꽉 차서 GC 가 돌기 시작하면(steady state) 랜덤 쓰기는 WAF 만큼 느려진다.

</details>

### Q5. wear leveling 의 dynamic 과 static 차이는? static 을 너무 자주 돌리면?
<details>
<summary>답 보기</summary>

dynamic: 새로 쓸 block 을 고를 때 **erase 수가 적은 free block** 을 고른다 — 비용 없음, 하지만 cold 데이터가 앉은 block 은 victim 이 안 되어 erase 가 안 늘고 나머지가 대신 닳는다. static: erase 수 편차(max − min)가 임계를 넘으면 **cold block 의 live 데이터를 강제로 옮겨** 그 block 을 회전에 넣는다. 너무 자주 하면 **추가 쓰기 → WAF↑, 성능↓, 오히려 전체 수명↓**. 실험에서 편차 48 → 20 을 얻는 데 WAF 4.94 → 5.14. 실무는 임계값 + rate limit, idle 시간 활용, cold 데이터를 이미 많이 닳은 block 으로 보내는 식으로 비용을 줄인다.

</details>

### Q6. direct-mapped FTL 이 HDD 보다 느릴 수 있다는 걸 숫자로 보여라.
<details>
<summary>답 보기</summary>

page 4KB, block 256 page, read 50 µs, program 600 µs, erase 3 ms 라 하면, 4KB 덮어쓰기 하나에 block 전체 read 12.8 ms + erase 3 ms + program 256 × 0.6 = 153.6 ms ≈ **169 ms → 약 6 IOPS**, **WAF 256**. 7,200 RPM HDD 랜덤 쓰기 ≈ 13 ms(76 IOPS)보다 10배 이상 느리다. 게다가 같은 LBA 를 반복해 쓰면 같은 물리 block 만 닳는다. log-structured 로 바꾸면 덮어쓰기는 program 600 µs 하나 + 가끔의 GC 로 줄어든다.

</details>

## 16. 자가 점검 & 숙제

### 퀴즈

**퀴즈 1.** NAND 에서 0 → 1 로 바꾸는 유일한 방법과 그 단위는?

<details>
<summary>정답</summary>

**erase**, 단위는 **block**(그 안의 모든 page 가 1 로).

</details>

**퀴즈 2.** 2TB SSD, 4KB 매핑 단위, 엔트리 4B. L2P 크기는? 매핑 단위를 16KB 로 키우면?

<details>
<summary>정답</summary>

2^41 / 2^12 × 4 = 2^31 B = **2 GiB**. 16KB 면 1/4 인 **512 MiB**.

</details>

**퀴즈 3.** GC victim 의 live 비율이 평균 75% 라면 WAF 는?

<details>
<summary>정답</summary>

1 / (1 − 0.75) = **4**.

</details>

**퀴즈 4.** 책 I.8 예제에서 block 0 을 GC 할 때 몇 page 를 복사하나? 왜 100, 101 은 버리나?

<details>
<summary>정답</summary>

2 page(2000, 2001). 매핑 테이블이 100→4, 101→5 를 가리키므로 block 0 의 page 0, 1 은 아무도 가리키지 않는 옛 버전(garbage).

</details>

**퀴즈 5.** hybrid FTL 의 세 merge 를 비용 순으로 나열하라.

<details>
<summary>정답</summary>

switch merge(추가 I/O 0) < partial merge(남은 page 만 복사) < full merge(여러 데이터 block 을 각각 재조립).

</details>

### 숙제

원문 부록에는 Homework 가 없다. `file-ssd/README.md` 와 이 노트의 코드로 꼭 해 볼 것:

- `ssd.py -T log … -n 60 -G 3 -g 2 -C -F -J` 를 돌리며 **GC 가 언제 시작되고 어느 block 을 고르는지** 를 FTL 상태와 함께 따라가기 → 9.2 의 절차를 상태 그림으로 확인하는 문제.
- `ssd.py` 에 `-K 80/20` 을 넣어 skew 가 WAF 에 주는 영향을 보고, toy FTL 실험 [3] 의 결론(섞어 쓰면 skew 가 오히려 해롭다)과 비교 → hot/cold 분리의 동기.
- `code/C0I_toy_ftl.c` 에 **cost-benefit victim 선택**(LFS 의 benefit/cost = (1−u)·age / (1+u))을 추가해 80/20 에서 greedy 와 WAF 를 비교하고, TRIM 저널을 추가해 실험 [4] 의 resurrected 를 0 으로 만들기 → 실제 FW 설계 두 가지를 직접 구현해 보는 문제.

## 17. 다음으로

FTL 의 log 구조는 파일시스템 쪽에서 먼저 나온 아이디어다 → [Ch.43 Log-structured File System](2026-09-30_C43_lfs.md) 에서 같은 GC·segment cleaning·매핑(imap) 문제를 FS 관점으로 다시 본다. 전원 손실과 일관성은 [Ch.42 저널링](2026-09-30_C42_crash_consistency_journaling.md), 조용한 비트 오류와 체크섬은 [Ch.44 데이터 무결성](2026-09-30_C44_data_integrity.md).
