/* device_excerpt.h — BM-B01 교육용 "CMSIS 스타일 디바이스 헤더" 발췌 (STM32F4 계열 모양)
 *
 * ST 의 stm32f4xx.h / stm32f407xx.h 를 복사한 것이 아니라, 같은 관례(convention)로 직접 쓴 짧은 발췌다.
 *   ① 접근 한정자        __I / __O / __IO (+ 구조체 멤버용 __IM / __OM / __IOM)     ← core_cm4.h 의 정의와 같음
 *   ② 레지스터 구조체     GPIO_TypeDef / USART_TypeDef / RCC_TypeDef (필요한 필드 + RESERVEDn 패딩)
 *   ③ 베이스 주소 체인    PERIPH_BASE → APB1/AHB1PERIPH_BASE → GPIOA_BASE … → GPIOA 캐스트 매크로
 *   ④ 비트 매크로         <PERIPH>_<REG>_<FIELD>_Pos / _Msk (+ 값 별칭 _0, _1)
 *   ⑤ 레지스터 접근 매크로 SET_BIT / CLEAR_BIT / READ_BIT / WRITE_REG / READ_REG  ← stm32f4xx.h 의 것과 같은 모양
 *      (_VAL2FLD / _FLD2VAL / MODIFY_REG 는 starter.c 의 TODO — 직접 써 본다)
 *
 * 주소·오프셋·비트 위치·리셋값은 RM0090 (STM32F405/407 …) 기준 "대표값"이다. 실제 칩 작업 전 RM 확인 권장.
 * 매크로 이름은 ST 헤더 버전·계열마다 조금씩 다르다 (MODER5/MODE5, OSPEEDR5/OSPEED5, AFRL2/AFSEL2 …).
 */
#ifndef DEVICE_EXCERPT_H
#define DEVICE_EXCERPT_H

#include <stdint.h>

/* ---------- ① 접근 한정자 (CMSIS core_cm4.h 와 같은 정의) ---------- */
#ifdef __cplusplus
#define __I   volatile             /* 읽기 전용 (C++ 에선 const 를 빼서 구조체를 기본 생성 가능하게) */
#else
#define __I   volatile const       /* 읽기 전용 */
#endif
#define __O   volatile             /* 쓰기 전용 — 컴파일러가 막지 않는다, 문서용 */
#define __IO  volatile             /* 읽기/쓰기 */
#define __IM  volatile const       /* 구조체 멤버용: 읽기 전용 */
#define __OM  volatile             /* 구조체 멤버용: 쓰기 전용 */
#define __IOM volatile             /* 구조체 멤버용: 읽기/쓰기 */

/* ---------- ② 레지스터 구조체 (RM0090 오프셋 — 확인 권장) ---------- */
typedef struct {
    __IO uint32_t MODER;    /* 0x00 모드: 핀당 2비트 00 입력 01 출력 10 AF 11 아날로그 */
    __IO uint32_t OTYPER;   /* 0x04 출력 타입: 핀당 1비트 0 push-pull 1 open-drain */
    __IO uint32_t OSPEEDR;  /* 0x08 출력 속도: 핀당 2비트 */
    __IO uint32_t PUPDR;    /* 0x0C 풀업/풀다운: 핀당 2비트 00 없음 01 업 10 다운 */
    __IO uint32_t IDR;      /* 0x10 입력 데이터 (ST 헤더도 __IO 로 선언 — 하드웨어상 읽기 전용) */
    __IO uint32_t ODR;      /* 0x14 출력 데이터 */
    __IO uint32_t BSRR;     /* 0x18 [15:0] set, [31:16] reset (쓰기 전용, 읽으면 0) */
    __IO uint32_t LCKR;     /* 0x1C 설정 잠금 */
    __IO uint32_t AFR[2];   /* 0x20 AFRL(핀 0–7), 0x24 AFRH(핀 8–15): 핀당 4비트 */
} GPIO_TypeDef;

typedef struct {
    __IO uint32_t SR;       /* 0x00 상태 */
    __IO uint32_t DR;       /* 0x04 데이터 */
    __IO uint32_t BRR;      /* 0x08 baud: [15:4] mantissa, [3:0] fraction */
    __IO uint32_t CR1;      /* 0x0C */
    __IO uint32_t CR2;      /* 0x10 */
    __IO uint32_t CR3;      /* 0x14  (실제 헤더는 0x18 GTPR 까지 — 이 발췌에선 생략) */
} USART_TypeDef;

typedef struct {
    __IO uint32_t CR;          /* 0x00 */
    __IO uint32_t PLLCFGR;     /* 0x04 */
    __IO uint32_t CFGR;        /* 0x08 */
    __IO uint32_t CIR;         /* 0x0C */
    __IO uint32_t AHB1RSTR;    /* 0x10 */
    __IO uint32_t AHB2RSTR;    /* 0x14 */
    __IO uint32_t AHB3RSTR;    /* 0x18 */
    uint32_t      RESERVED0;   /* 0x1C */
    __IO uint32_t APB1RSTR;    /* 0x20 */
    __IO uint32_t APB2RSTR;    /* 0x24 */
#ifdef DEMO_BAD_RESERVED
    uint32_t      RESERVED1;   /* 버그 재현: RM 의 빈칸 0x28–0x2C 는 2 워드인데 1 워드만 */
#else
    uint32_t      RESERVED1[2];/* 0x28-0x2C */
#endif
    __IO uint32_t AHB1ENR;     /* 0x30 */
    __IO uint32_t AHB2ENR;     /* 0x34 */
    __IO uint32_t AHB3ENR;     /* 0x38 */
    uint32_t      RESERVED2;   /* 0x3C */
    __IO uint32_t APB1ENR;     /* 0x40 */
    __IO uint32_t APB2ENR;     /* 0x44  (실제 헤더는 계속 이어진다: LPENR, BDCR, CSR …) */
} RCC_TypeDef;

/* ---------- ③ 베이스 주소 체인 (RM0090 메모리 맵 — 확인 권장) ---------- */
#define PERIPH_BASE        0x40000000UL
#define APB1PERIPH_BASE    PERIPH_BASE
#define APB2PERIPH_BASE    (PERIPH_BASE + 0x00010000UL)
#define AHB1PERIPH_BASE    (PERIPH_BASE + 0x00020000UL)

#define USART2_BASE        (APB1PERIPH_BASE + 0x4400UL)
#define GPIOA_BASE         (AHB1PERIPH_BASE + 0x0000UL)
#define GPIOB_BASE         (AHB1PERIPH_BASE + 0x0400UL)
#define GPIOC_BASE         (AHB1PERIPH_BASE + 0x0800UL)
#define RCC_BASE           (AHB1PERIPH_BASE + 0x3800UL)

#define USART2             ((USART_TypeDef *) USART2_BASE)
#define GPIOA              ((GPIO_TypeDef *) GPIOA_BASE)
#define GPIOB              ((GPIO_TypeDef *) GPIOB_BASE)
#define GPIOC              ((GPIO_TypeDef *) GPIOC_BASE)
#define RCC                ((RCC_TypeDef *) RCC_BASE)

/* ---------- ④ 비트 매크로: _Pos = 위치(시프트 양), _Msk = 필드 전체가 1 인 마스크 ---------- */
/* RCC */
#define RCC_AHB1ENR_GPIOAEN_Pos   (0U)
#define RCC_AHB1ENR_GPIOAEN_Msk   (0x1UL << RCC_AHB1ENR_GPIOAEN_Pos)        /* 0x00000001 */
#define RCC_AHB1ENR_GPIOAEN       RCC_AHB1ENR_GPIOAEN_Msk
#define RCC_AHB1ENR_GPIOCEN_Pos   (2U)
#define RCC_AHB1ENR_GPIOCEN_Msk   (0x1UL << RCC_AHB1ENR_GPIOCEN_Pos)        /* 0x00000004 */
#define RCC_AHB1ENR_GPIOCEN       RCC_AHB1ENR_GPIOCEN_Msk
#define RCC_APB1ENR_USART2EN_Pos  (17U)
#define RCC_APB1ENR_USART2EN_Msk  (0x1UL << RCC_APB1ENR_USART2EN_Pos)       /* 0x00020000 */
#define RCC_APB1ENR_USART2EN      RCC_APB1ENR_USART2EN_Msk

/* GPIO MODER: 핀 n → 비트 [2n+1:2n] */
#define GPIO_MODER_MODER0_Pos     (0U)
#define GPIO_MODER_MODER0_Msk     (0x3UL << GPIO_MODER_MODER0_Pos)          /* 0x00000003 */
#define GPIO_MODER_MODER2_Pos     (4U)
#define GPIO_MODER_MODER2_Msk     (0x3UL << GPIO_MODER_MODER2_Pos)          /* 0x00000030 */
#define GPIO_MODER_MODER3_Pos     (6U)
#define GPIO_MODER_MODER3_Msk     (0x3UL << GPIO_MODER_MODER3_Pos)          /* 0x000000C0 */
#define GPIO_MODER_MODER5_Pos     (10U)
#define GPIO_MODER_MODER5_Msk     (0x3UL << GPIO_MODER_MODER5_Pos)          /* 0x00000C00 */
#define GPIO_MODER_MODER5         GPIO_MODER_MODER5_Msk
#define GPIO_MODER_MODER5_0       (0x1UL << GPIO_MODER_MODER5_Pos)          /* 0x00000400 */
#define GPIO_MODER_MODER5_1       (0x2UL << GPIO_MODER_MODER5_Pos)          /* 0x00000800 */

/* GPIO OTYPER: 핀 n → 비트 n */
#define GPIO_OTYPER_OT2_Pos       (2U)
#define GPIO_OTYPER_OT2_Msk       (0x1UL << GPIO_OTYPER_OT2_Pos)            /* 0x00000004 */
#define GPIO_OTYPER_OT5_Pos       (5U)
#define GPIO_OTYPER_OT5_Msk       (0x1UL << GPIO_OTYPER_OT5_Pos)            /* 0x00000020 */

/* GPIO OSPEEDR: 핀 n → 비트 [2n+1:2n]  (ST F4 옛 헤더 이름은 GPIO_OSPEEDER_OSPEEDRn — 오타 그대로) */
#define GPIO_OSPEEDR_OSPEED2_Pos  (4U)
#define GPIO_OSPEEDR_OSPEED2_Msk  (0x3UL << GPIO_OSPEEDR_OSPEED2_Pos)       /* 0x00000030 */
#define GPIO_OSPEEDR_OSPEED5_Pos  (10U)
#define GPIO_OSPEEDR_OSPEED5_Msk  (0x3UL << GPIO_OSPEEDR_OSPEED5_Pos)       /* 0x00000C00 */

/* GPIO PUPDR: 핀 n → 비트 [2n+1:2n] */
#define GPIO_PUPDR_PUPD2_Pos      (4U)
#define GPIO_PUPDR_PUPD2_Msk      (0x3UL << GPIO_PUPDR_PUPD2_Pos)           /* 0x00000030 */
#define GPIO_PUPDR_PUPD3_Pos      (6U)
#define GPIO_PUPDR_PUPD3_Msk      (0x3UL << GPIO_PUPDR_PUPD3_Pos)           /* 0x000000C0 */
#define GPIO_PUPDR_PUPD5_Pos      (10U)
#define GPIO_PUPDR_PUPD5_Msk      (0x3UL << GPIO_PUPDR_PUPD5_Pos)           /* 0x00000C00 */

/* GPIO AFRL (= AFR[0]): 핀 n(0–7) → 비트 [4n+3:4n] */
#define GPIO_AFRL_AFSEL0_Pos      (0U)
#define GPIO_AFRL_AFSEL0_Msk      (0xFUL << GPIO_AFRL_AFSEL0_Pos)           /* 0x0000000F */
#define GPIO_AFRL_AFSEL2_Pos      (8U)
#define GPIO_AFRL_AFSEL2_Msk      (0xFUL << GPIO_AFRL_AFSEL2_Pos)           /* 0x00000F00 */
#define GPIO_AFRL_AFSEL3_Pos      (12U)
#define GPIO_AFRL_AFSEL3_Msk      (0xFUL << GPIO_AFRL_AFSEL3_Pos)           /* 0x0000F000 */

/* GPIO BSRR */
#define GPIO_BSRR_BS5_Pos         (5U)
#define GPIO_BSRR_BS5_Msk         (0x1UL << GPIO_BSRR_BS5_Pos)              /* 0x00000020 */
#define GPIO_BSRR_BR5_Pos         (21U)
#define GPIO_BSRR_BR5_Msk         (0x1UL << GPIO_BSRR_BR5_Pos)              /* 0x00200000 */

/* USART SR */
#define USART_SR_RXNE_Pos         (5U)
#define USART_SR_RXNE_Msk         (0x1UL << USART_SR_RXNE_Pos)              /* 0x00000020 */
#define USART_SR_TC_Pos           (6U)
#define USART_SR_TC_Msk           (0x1UL << USART_SR_TC_Pos)                /* 0x00000040 */
#define USART_SR_TXE_Pos          (7U)
#define USART_SR_TXE_Msk          (0x1UL << USART_SR_TXE_Pos)               /* 0x00000080 */

/* USART BRR */
#define USART_BRR_DIV_Fraction_Pos (0U)
#define USART_BRR_DIV_Fraction_Msk (0xFUL << USART_BRR_DIV_Fraction_Pos)    /* 0x0000000F */
#define USART_BRR_DIV_Mantissa_Pos (4U)
#define USART_BRR_DIV_Mantissa_Msk (0xFFFUL << USART_BRR_DIV_Mantissa_Pos)  /* 0x0000FFF0 */

/* USART CR1 */
#define USART_CR1_SBK_Pos         (0U)
#define USART_CR1_SBK_Msk         (0x1UL << USART_CR1_SBK_Pos)              /* 0x00000001 break 송신 */
#define USART_CR1_RE_Pos          (2U)
#define USART_CR1_RE_Msk          (0x1UL << USART_CR1_RE_Pos)               /* 0x00000004 */
#define USART_CR1_TE_Pos          (3U)
#define USART_CR1_TE_Msk          (0x1UL << USART_CR1_TE_Pos)               /* 0x00000008 */
#define USART_CR1_RXNEIE_Pos      (5U)
#define USART_CR1_RXNEIE_Msk      (0x1UL << USART_CR1_RXNEIE_Pos)           /* 0x00000020 */
#define USART_CR1_PS_Pos          (9U)
#define USART_CR1_PS_Msk          (0x1UL << USART_CR1_PS_Pos)               /* 0x00000200 */
#define USART_CR1_PCE_Pos         (10U)
#define USART_CR1_PCE_Msk         (0x1UL << USART_CR1_PCE_Pos)              /* 0x00000400 */
#define USART_CR1_M_Pos           (12U)
#define USART_CR1_M_Msk           (0x1UL << USART_CR1_M_Pos)                /* 0x00001000 */
#define USART_CR1_UE_Pos          (13U)
#define USART_CR1_UE_Msk          (0x1UL << USART_CR1_UE_Pos)               /* 0x00002000 */
#define USART_CR1_OVER8_Pos       (15U)
#define USART_CR1_OVER8_Msk       (0x1UL << USART_CR1_OVER8_Pos)            /* 0x00008000 */

/* USART CR2 */
#define USART_CR2_STOP_Pos        (12U)
#define USART_CR2_STOP_Msk        (0x3UL << USART_CR2_STOP_Pos)             /* 0x00003000 00=1 stop */

/* ---------- ⑤ 레지스터 접근 매크로 (stm32f4xx.h 와 같은 모양) ---------- */
#define SET_BIT(REG, BIT)     ((REG) |= (BIT))
#define CLEAR_BIT(REG, BIT)   ((REG) &= ~(BIT))
#define READ_BIT(REG, BIT)    ((REG) & (BIT))
#define WRITE_REG(REG, VAL)   ((REG) = (VAL))
#define READ_REG(REG)         ((REG))

#endif /* DEVICE_EXCERPT_H */
