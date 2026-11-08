/* CPP-B08 demo — 레이아웃을 컴파일러에게 직접 그리게 한다 (헤더 없음 → 어느 타깃이든 -fsyntax-only 가능)
 *   arm64 (호스트):  c++ -std=c++20 -fsyntax-only -Xclang -fdump-record-layouts demo_layout.cpp
 *   Cortex-M4 (ILP32): c++ -std=c++20 --target=thumbv7em-none-eabi -fsyntax-only -Xclang -fdump-record-layouts demo_layout.cpp
 */
typedef unsigned char u8; typedef unsigned short u16; typedef unsigned int u32;

struct Empty {};
struct WithVirt { virtual void on_irq() {} u32 x; };
struct EboClash : Empty { Empty e; u32 x; };
struct NuaMember { [[no_unique_address]] Empty e; u32 x; };
struct NonPodBase { NonPodBase() {} u32 a; u8 b; };
struct AfterNonPod : NonPodBase { u8 c; };

/* starter 의 버그 버전 vs 고친 버전 */
struct UartRegsVirt { volatile u32 CR, SR, BRR, DR; volatile u16 FIFO_LVL, FIFO_THR; virtual void dump() const {} };
struct UartRegs     { volatile u32 CR, SR; u32 reserved0_[2]; volatile u32 BRR, DR; volatile u16 FIFO_LVL, FIFO_THR; };

static_assert(sizeof(WithVirt) == 2 * sizeof(void*));      /* 두 타깃 공통으로 성립 */
static_assert(sizeof(UartRegs) == 0x1C);                    /* 포인터 없음 → 타깃 무관 */

/* sizeof 를 써야 clang 이 레이아웃을 계산·덤프한다 */
int force_layout[] = { sizeof(WithVirt), sizeof(EboClash), sizeof(NuaMember), sizeof(AfterNonPod),
                       sizeof(UartRegsVirt), sizeof(UartRegs) };
