/* SPI0 register map -- AUTO-GENERATED from regmap.json by SIV-B01 codegen. DO NOT EDIT.
 * block SPI0 v1.0, base 0x40013000, size 0x40, 14 registers */
#ifndef SPI0_REGS_H
#define SPI0_REGS_H

#include <stdint.h>

#define SPI0_BASE                          0x40013000u
#define SPI0_SIZE                          0x040u
#define SPI0_REG_COUNT                     14u

/* ---- ID @ 0x000 : IP 식별자 ---- */
#define SPI0_ID_OFFSET                     0x000u
#define SPI0_ID_ADDR                       (SPI0_BASE + SPI0_ID_OFFSET)
#define SPI0_ID_RESET                      0x53500102u
#define SPI0_ID_DEFINED_MASK               0xFFFFFFFFu
#define SPI0_ID_RW_MASK                    0x00000000u
#define SPI0_ID_RO_MASK                    0xFFFFFFFFu
#define SPI0_ID_WO_MASK                    0x00000000u
#define SPI0_ID_W1C_MASK                   0x00000000u
#define SPI0_ID_W1S_MASK                   0x00000000u
#define SPI0_ID_RC_MASK                    0x00000000u
#define SPI0_ID_VOLATILE_MASK              0x00000000u
#define SPI0_ID_MINOR_SHIFT                0u
#define SPI0_ID_MINOR_WIDTH                8u
#define SPI0_ID_MINOR_MASK                 0x000000FFu
#define SPI0_ID_MINOR_RESET                0x2u
#define SPI0_ID_MINOR_GET(r)               ((((uint32_t)(r)) & SPI0_ID_MINOR_MASK) >> SPI0_ID_MINOR_SHIFT)
#define SPI0_ID_MINOR_PREP(v)              ((((uint32_t)(v)) << SPI0_ID_MINOR_SHIFT) & SPI0_ID_MINOR_MASK)
#define SPI0_ID_MAJOR_SHIFT                8u
#define SPI0_ID_MAJOR_WIDTH                8u
#define SPI0_ID_MAJOR_MASK                 0x0000FF00u
#define SPI0_ID_MAJOR_RESET                0x1u
#define SPI0_ID_MAJOR_GET(r)               ((((uint32_t)(r)) & SPI0_ID_MAJOR_MASK) >> SPI0_ID_MAJOR_SHIFT)
#define SPI0_ID_MAJOR_PREP(v)              ((((uint32_t)(v)) << SPI0_ID_MAJOR_SHIFT) & SPI0_ID_MAJOR_MASK)
#define SPI0_ID_PART_SHIFT                 16u
#define SPI0_ID_PART_WIDTH                 16u
#define SPI0_ID_PART_MASK                  0xFFFF0000u
#define SPI0_ID_PART_RESET                 0x5350u
#define SPI0_ID_PART_GET(r)                ((((uint32_t)(r)) & SPI0_ID_PART_MASK) >> SPI0_ID_PART_SHIFT)
#define SPI0_ID_PART_PREP(v)               ((((uint32_t)(v)) << SPI0_ID_PART_SHIFT) & SPI0_ID_PART_MASK)

/* ---- CTRL @ 0x004 : 제어 ---- */
#define SPI0_CTRL_OFFSET                   0x004u
#define SPI0_CTRL_ADDR                     (SPI0_BASE + SPI0_CTRL_OFFSET)
#define SPI0_CTRL_RESET                    0x00070302u
#define SPI0_CTRL_DEFINED_MASK             0x001F0F3Fu
#define SPI0_CTRL_RW_MASK                  0x001F0F3Fu
#define SPI0_CTRL_RO_MASK                  0x00000000u
#define SPI0_CTRL_WO_MASK                  0x00000000u
#define SPI0_CTRL_W1C_MASK                 0x00000000u
#define SPI0_CTRL_W1S_MASK                 0x00000000u
#define SPI0_CTRL_RC_MASK                  0x00000000u
#define SPI0_CTRL_VOLATILE_MASK            0x00000000u
#define SPI0_CTRL_EN_SHIFT                 0u
#define SPI0_CTRL_EN_WIDTH                 1u
#define SPI0_CTRL_EN_MASK                  0x00000001u
#define SPI0_CTRL_EN_RESET                 0x0u
#define SPI0_CTRL_EN_GET(r)                ((((uint32_t)(r)) & SPI0_CTRL_EN_MASK) >> SPI0_CTRL_EN_SHIFT)
#define SPI0_CTRL_EN_PREP(v)               ((((uint32_t)(v)) << SPI0_CTRL_EN_SHIFT) & SPI0_CTRL_EN_MASK)
#define SPI0_CTRL_MSTR_SHIFT               1u
#define SPI0_CTRL_MSTR_WIDTH               1u
#define SPI0_CTRL_MSTR_MASK                0x00000002u
#define SPI0_CTRL_MSTR_RESET               0x1u
#define SPI0_CTRL_MSTR_GET(r)              ((((uint32_t)(r)) & SPI0_CTRL_MSTR_MASK) >> SPI0_CTRL_MSTR_SHIFT)
#define SPI0_CTRL_MSTR_PREP(v)             ((((uint32_t)(v)) << SPI0_CTRL_MSTR_SHIFT) & SPI0_CTRL_MSTR_MASK)
#define SPI0_CTRL_CPOL_SHIFT               2u
#define SPI0_CTRL_CPOL_WIDTH               1u
#define SPI0_CTRL_CPOL_MASK                0x00000004u
#define SPI0_CTRL_CPOL_RESET               0x0u
#define SPI0_CTRL_CPOL_GET(r)              ((((uint32_t)(r)) & SPI0_CTRL_CPOL_MASK) >> SPI0_CTRL_CPOL_SHIFT)
#define SPI0_CTRL_CPOL_PREP(v)             ((((uint32_t)(v)) << SPI0_CTRL_CPOL_SHIFT) & SPI0_CTRL_CPOL_MASK)
#define SPI0_CTRL_CPHA_SHIFT               3u
#define SPI0_CTRL_CPHA_WIDTH               1u
#define SPI0_CTRL_CPHA_MASK                0x00000008u
#define SPI0_CTRL_CPHA_RESET               0x0u
#define SPI0_CTRL_CPHA_GET(r)              ((((uint32_t)(r)) & SPI0_CTRL_CPHA_MASK) >> SPI0_CTRL_CPHA_SHIFT)
#define SPI0_CTRL_CPHA_PREP(v)             ((((uint32_t)(v)) << SPI0_CTRL_CPHA_SHIFT) & SPI0_CTRL_CPHA_MASK)
#define SPI0_CTRL_LSBF_SHIFT               4u
#define SPI0_CTRL_LSBF_WIDTH               1u
#define SPI0_CTRL_LSBF_MASK                0x00000010u
#define SPI0_CTRL_LSBF_RESET               0x0u
#define SPI0_CTRL_LSBF_GET(r)              ((((uint32_t)(r)) & SPI0_CTRL_LSBF_MASK) >> SPI0_CTRL_LSBF_SHIFT)
#define SPI0_CTRL_LSBF_PREP(v)             ((((uint32_t)(v)) << SPI0_CTRL_LSBF_SHIFT) & SPI0_CTRL_LSBF_MASK)
#define SPI0_CTRL_LOOPBACK_SHIFT           5u
#define SPI0_CTRL_LOOPBACK_WIDTH           1u
#define SPI0_CTRL_LOOPBACK_MASK            0x00000020u
#define SPI0_CTRL_LOOPBACK_RESET           0x0u
#define SPI0_CTRL_LOOPBACK_GET(r)          ((((uint32_t)(r)) & SPI0_CTRL_LOOPBACK_MASK) >> SPI0_CTRL_LOOPBACK_SHIFT)
#define SPI0_CTRL_LOOPBACK_PREP(v)         ((((uint32_t)(v)) << SPI0_CTRL_LOOPBACK_SHIFT) & SPI0_CTRL_LOOPBACK_MASK)
#define SPI0_CTRL_CLKDIV_SHIFT             8u
#define SPI0_CTRL_CLKDIV_WIDTH             4u
#define SPI0_CTRL_CLKDIV_MASK              0x00000F00u
#define SPI0_CTRL_CLKDIV_RESET             0x3u
#define SPI0_CTRL_CLKDIV_GET(r)            ((((uint32_t)(r)) & SPI0_CTRL_CLKDIV_MASK) >> SPI0_CTRL_CLKDIV_SHIFT)
#define SPI0_CTRL_CLKDIV_PREP(v)           ((((uint32_t)(v)) << SPI0_CTRL_CLKDIV_SHIFT) & SPI0_CTRL_CLKDIV_MASK)
#define SPI0_CTRL_FSIZE_SHIFT              16u
#define SPI0_CTRL_FSIZE_WIDTH              5u
#define SPI0_CTRL_FSIZE_MASK               0x001F0000u
#define SPI0_CTRL_FSIZE_RESET              0x7u
#define SPI0_CTRL_FSIZE_GET(r)             ((((uint32_t)(r)) & SPI0_CTRL_FSIZE_MASK) >> SPI0_CTRL_FSIZE_SHIFT)
#define SPI0_CTRL_FSIZE_PREP(v)            ((((uint32_t)(v)) << SPI0_CTRL_FSIZE_SHIFT) & SPI0_CTRL_FSIZE_MASK)

/* ---- STATUS @ 0x008 : 상태 (HW가 갱신) ---- */
#define SPI0_STATUS_OFFSET                 0x008u
#define SPI0_STATUS_ADDR                   (SPI0_BASE + SPI0_STATUS_OFFSET)
#define SPI0_STATUS_RESET                  0x00000002u
#define SPI0_STATUS_DEFINED_MASK           0x0000000Fu
#define SPI0_STATUS_RW_MASK                0x00000000u
#define SPI0_STATUS_RO_MASK                0x0000000Fu
#define SPI0_STATUS_WO_MASK                0x00000000u
#define SPI0_STATUS_W1C_MASK               0x00000000u
#define SPI0_STATUS_W1S_MASK               0x00000000u
#define SPI0_STATUS_RC_MASK                0x00000000u
#define SPI0_STATUS_VOLATILE_MASK          0x0000000Fu
#define SPI0_STATUS_BUSY_SHIFT             0u
#define SPI0_STATUS_BUSY_WIDTH             1u
#define SPI0_STATUS_BUSY_MASK              0x00000001u
#define SPI0_STATUS_BUSY_RESET             0x0u
#define SPI0_STATUS_BUSY_GET(r)            ((((uint32_t)(r)) & SPI0_STATUS_BUSY_MASK) >> SPI0_STATUS_BUSY_SHIFT)
#define SPI0_STATUS_BUSY_PREP(v)           ((((uint32_t)(v)) << SPI0_STATUS_BUSY_SHIFT) & SPI0_STATUS_BUSY_MASK)
#define SPI0_STATUS_TXE_SHIFT              1u
#define SPI0_STATUS_TXE_WIDTH              1u
#define SPI0_STATUS_TXE_MASK               0x00000002u
#define SPI0_STATUS_TXE_RESET              0x1u
#define SPI0_STATUS_TXE_GET(r)             ((((uint32_t)(r)) & SPI0_STATUS_TXE_MASK) >> SPI0_STATUS_TXE_SHIFT)
#define SPI0_STATUS_TXE_PREP(v)            ((((uint32_t)(v)) << SPI0_STATUS_TXE_SHIFT) & SPI0_STATUS_TXE_MASK)
#define SPI0_STATUS_RXNE_SHIFT             2u
#define SPI0_STATUS_RXNE_WIDTH             1u
#define SPI0_STATUS_RXNE_MASK              0x00000004u
#define SPI0_STATUS_RXNE_RESET             0x0u
#define SPI0_STATUS_RXNE_GET(r)            ((((uint32_t)(r)) & SPI0_STATUS_RXNE_MASK) >> SPI0_STATUS_RXNE_SHIFT)
#define SPI0_STATUS_RXNE_PREP(v)           ((((uint32_t)(v)) << SPI0_STATUS_RXNE_SHIFT) & SPI0_STATUS_RXNE_MASK)
#define SPI0_STATUS_TXF_SHIFT              3u
#define SPI0_STATUS_TXF_WIDTH              1u
#define SPI0_STATUS_TXF_MASK               0x00000008u
#define SPI0_STATUS_TXF_RESET              0x0u
#define SPI0_STATUS_TXF_GET(r)             ((((uint32_t)(r)) & SPI0_STATUS_TXF_MASK) >> SPI0_STATUS_TXF_SHIFT)
#define SPI0_STATUS_TXF_PREP(v)            ((((uint32_t)(v)) << SPI0_STATUS_TXF_SHIFT) & SPI0_STATUS_TXF_MASK)

/* ---- INT_RAW @ 0x00C : 인터럽트 raw status (W1C) ---- */
#define SPI0_INT_RAW_OFFSET                0x00Cu
#define SPI0_INT_RAW_ADDR                  (SPI0_BASE + SPI0_INT_RAW_OFFSET)
#define SPI0_INT_RAW_RESET                 0x00000000u
#define SPI0_INT_RAW_DEFINED_MASK          0x00000007u
#define SPI0_INT_RAW_RW_MASK               0x00000000u
#define SPI0_INT_RAW_RO_MASK               0x00000000u
#define SPI0_INT_RAW_WO_MASK               0x00000000u
#define SPI0_INT_RAW_W1C_MASK              0x00000007u
#define SPI0_INT_RAW_W1S_MASK              0x00000000u
#define SPI0_INT_RAW_RC_MASK               0x00000000u
#define SPI0_INT_RAW_VOLATILE_MASK         0x00000007u
#define SPI0_INT_RAW_TXDONE_SHIFT          0u
#define SPI0_INT_RAW_TXDONE_WIDTH          1u
#define SPI0_INT_RAW_TXDONE_MASK           0x00000001u
#define SPI0_INT_RAW_TXDONE_RESET          0x0u
#define SPI0_INT_RAW_TXDONE_GET(r)         ((((uint32_t)(r)) & SPI0_INT_RAW_TXDONE_MASK) >> SPI0_INT_RAW_TXDONE_SHIFT)
#define SPI0_INT_RAW_TXDONE_PREP(v)        ((((uint32_t)(v)) << SPI0_INT_RAW_TXDONE_SHIFT) & SPI0_INT_RAW_TXDONE_MASK)
#define SPI0_INT_RAW_RXOVF_SHIFT           1u
#define SPI0_INT_RAW_RXOVF_WIDTH           1u
#define SPI0_INT_RAW_RXOVF_MASK            0x00000002u
#define SPI0_INT_RAW_RXOVF_RESET           0x0u
#define SPI0_INT_RAW_RXOVF_GET(r)          ((((uint32_t)(r)) & SPI0_INT_RAW_RXOVF_MASK) >> SPI0_INT_RAW_RXOVF_SHIFT)
#define SPI0_INT_RAW_RXOVF_PREP(v)         ((((uint32_t)(v)) << SPI0_INT_RAW_RXOVF_SHIFT) & SPI0_INT_RAW_RXOVF_MASK)
#define SPI0_INT_RAW_MODF_SHIFT            2u
#define SPI0_INT_RAW_MODF_WIDTH            1u
#define SPI0_INT_RAW_MODF_MASK             0x00000004u
#define SPI0_INT_RAW_MODF_RESET            0x0u
#define SPI0_INT_RAW_MODF_GET(r)           ((((uint32_t)(r)) & SPI0_INT_RAW_MODF_MASK) >> SPI0_INT_RAW_MODF_SHIFT)
#define SPI0_INT_RAW_MODF_PREP(v)          ((((uint32_t)(v)) << SPI0_INT_RAW_MODF_SHIFT) & SPI0_INT_RAW_MODF_MASK)

/* ---- INT_EN @ 0x010 : 인터럽트 enable ---- */
#define SPI0_INT_EN_OFFSET                 0x010u
#define SPI0_INT_EN_ADDR                   (SPI0_BASE + SPI0_INT_EN_OFFSET)
#define SPI0_INT_EN_RESET                  0x00000000u
#define SPI0_INT_EN_DEFINED_MASK           0x00000007u
#define SPI0_INT_EN_RW_MASK                0x00000007u
#define SPI0_INT_EN_RO_MASK                0x00000000u
#define SPI0_INT_EN_WO_MASK                0x00000000u
#define SPI0_INT_EN_W1C_MASK               0x00000000u
#define SPI0_INT_EN_W1S_MASK               0x00000000u
#define SPI0_INT_EN_RC_MASK                0x00000000u
#define SPI0_INT_EN_VOLATILE_MASK          0x00000000u
#define SPI0_INT_EN_TXDONE_SHIFT           0u
#define SPI0_INT_EN_TXDONE_WIDTH           1u
#define SPI0_INT_EN_TXDONE_MASK            0x00000001u
#define SPI0_INT_EN_TXDONE_RESET           0x0u
#define SPI0_INT_EN_TXDONE_GET(r)          ((((uint32_t)(r)) & SPI0_INT_EN_TXDONE_MASK) >> SPI0_INT_EN_TXDONE_SHIFT)
#define SPI0_INT_EN_TXDONE_PREP(v)         ((((uint32_t)(v)) << SPI0_INT_EN_TXDONE_SHIFT) & SPI0_INT_EN_TXDONE_MASK)
#define SPI0_INT_EN_RXOVF_SHIFT            1u
#define SPI0_INT_EN_RXOVF_WIDTH            1u
#define SPI0_INT_EN_RXOVF_MASK             0x00000002u
#define SPI0_INT_EN_RXOVF_RESET            0x0u
#define SPI0_INT_EN_RXOVF_GET(r)           ((((uint32_t)(r)) & SPI0_INT_EN_RXOVF_MASK) >> SPI0_INT_EN_RXOVF_SHIFT)
#define SPI0_INT_EN_RXOVF_PREP(v)          ((((uint32_t)(v)) << SPI0_INT_EN_RXOVF_SHIFT) & SPI0_INT_EN_RXOVF_MASK)
#define SPI0_INT_EN_MODF_SHIFT             2u
#define SPI0_INT_EN_MODF_WIDTH             1u
#define SPI0_INT_EN_MODF_MASK              0x00000004u
#define SPI0_INT_EN_MODF_RESET             0x0u
#define SPI0_INT_EN_MODF_GET(r)            ((((uint32_t)(r)) & SPI0_INT_EN_MODF_MASK) >> SPI0_INT_EN_MODF_SHIFT)
#define SPI0_INT_EN_MODF_PREP(v)           ((((uint32_t)(v)) << SPI0_INT_EN_MODF_SHIFT) & SPI0_INT_EN_MODF_MASK)

/* ---- INT_STAT @ 0x014 : masked status = RAW & EN ---- */
#define SPI0_INT_STAT_OFFSET               0x014u
#define SPI0_INT_STAT_ADDR                 (SPI0_BASE + SPI0_INT_STAT_OFFSET)
#define SPI0_INT_STAT_RESET                0x00000000u
#define SPI0_INT_STAT_DEFINED_MASK         0x00000007u
#define SPI0_INT_STAT_RW_MASK              0x00000000u
#define SPI0_INT_STAT_RO_MASK              0x00000007u
#define SPI0_INT_STAT_WO_MASK              0x00000000u
#define SPI0_INT_STAT_W1C_MASK             0x00000000u
#define SPI0_INT_STAT_W1S_MASK             0x00000000u
#define SPI0_INT_STAT_RC_MASK              0x00000000u
#define SPI0_INT_STAT_VOLATILE_MASK        0x00000007u
#define SPI0_INT_STAT_TXDONE_SHIFT         0u
#define SPI0_INT_STAT_TXDONE_WIDTH         1u
#define SPI0_INT_STAT_TXDONE_MASK          0x00000001u
#define SPI0_INT_STAT_TXDONE_RESET         0x0u
#define SPI0_INT_STAT_TXDONE_GET(r)        ((((uint32_t)(r)) & SPI0_INT_STAT_TXDONE_MASK) >> SPI0_INT_STAT_TXDONE_SHIFT)
#define SPI0_INT_STAT_TXDONE_PREP(v)       ((((uint32_t)(v)) << SPI0_INT_STAT_TXDONE_SHIFT) & SPI0_INT_STAT_TXDONE_MASK)
#define SPI0_INT_STAT_RXOVF_SHIFT          1u
#define SPI0_INT_STAT_RXOVF_WIDTH          1u
#define SPI0_INT_STAT_RXOVF_MASK           0x00000002u
#define SPI0_INT_STAT_RXOVF_RESET          0x0u
#define SPI0_INT_STAT_RXOVF_GET(r)         ((((uint32_t)(r)) & SPI0_INT_STAT_RXOVF_MASK) >> SPI0_INT_STAT_RXOVF_SHIFT)
#define SPI0_INT_STAT_RXOVF_PREP(v)        ((((uint32_t)(v)) << SPI0_INT_STAT_RXOVF_SHIFT) & SPI0_INT_STAT_RXOVF_MASK)
#define SPI0_INT_STAT_MODF_SHIFT           2u
#define SPI0_INT_STAT_MODF_WIDTH           1u
#define SPI0_INT_STAT_MODF_MASK            0x00000004u
#define SPI0_INT_STAT_MODF_RESET           0x0u
#define SPI0_INT_STAT_MODF_GET(r)          ((((uint32_t)(r)) & SPI0_INT_STAT_MODF_MASK) >> SPI0_INT_STAT_MODF_SHIFT)
#define SPI0_INT_STAT_MODF_PREP(v)         ((((uint32_t)(v)) << SPI0_INT_STAT_MODF_SHIFT) & SPI0_INT_STAT_MODF_MASK)

/* ---- INT_FORCE @ 0x018 : 인터럽트 강제 트리거 (테스트용, 읽으면 0) ---- */
#define SPI0_INT_FORCE_OFFSET              0x018u
#define SPI0_INT_FORCE_ADDR                (SPI0_BASE + SPI0_INT_FORCE_OFFSET)
#define SPI0_INT_FORCE_RESET               0x00000000u
#define SPI0_INT_FORCE_DEFINED_MASK        0x00000007u
#define SPI0_INT_FORCE_RW_MASK             0x00000000u
#define SPI0_INT_FORCE_RO_MASK             0x00000000u
#define SPI0_INT_FORCE_WO_MASK             0x00000007u
#define SPI0_INT_FORCE_W1C_MASK            0x00000000u
#define SPI0_INT_FORCE_W1S_MASK            0x00000000u
#define SPI0_INT_FORCE_RC_MASK             0x00000000u
#define SPI0_INT_FORCE_VOLATILE_MASK       0x00000000u
#define SPI0_INT_FORCE_TXDONE_SHIFT        0u
#define SPI0_INT_FORCE_TXDONE_WIDTH        1u
#define SPI0_INT_FORCE_TXDONE_MASK         0x00000001u
#define SPI0_INT_FORCE_TXDONE_RESET        0x0u
#define SPI0_INT_FORCE_TXDONE_GET(r)       ((((uint32_t)(r)) & SPI0_INT_FORCE_TXDONE_MASK) >> SPI0_INT_FORCE_TXDONE_SHIFT)
#define SPI0_INT_FORCE_TXDONE_PREP(v)      ((((uint32_t)(v)) << SPI0_INT_FORCE_TXDONE_SHIFT) & SPI0_INT_FORCE_TXDONE_MASK)
#define SPI0_INT_FORCE_RXOVF_SHIFT         1u
#define SPI0_INT_FORCE_RXOVF_WIDTH         1u
#define SPI0_INT_FORCE_RXOVF_MASK          0x00000002u
#define SPI0_INT_FORCE_RXOVF_RESET         0x0u
#define SPI0_INT_FORCE_RXOVF_GET(r)        ((((uint32_t)(r)) & SPI0_INT_FORCE_RXOVF_MASK) >> SPI0_INT_FORCE_RXOVF_SHIFT)
#define SPI0_INT_FORCE_RXOVF_PREP(v)       ((((uint32_t)(v)) << SPI0_INT_FORCE_RXOVF_SHIFT) & SPI0_INT_FORCE_RXOVF_MASK)
#define SPI0_INT_FORCE_MODF_SHIFT          2u
#define SPI0_INT_FORCE_MODF_WIDTH          1u
#define SPI0_INT_FORCE_MODF_MASK           0x00000004u
#define SPI0_INT_FORCE_MODF_RESET          0x0u
#define SPI0_INT_FORCE_MODF_GET(r)         ((((uint32_t)(r)) & SPI0_INT_FORCE_MODF_MASK) >> SPI0_INT_FORCE_MODF_SHIFT)
#define SPI0_INT_FORCE_MODF_PREP(v)        ((((uint32_t)(v)) << SPI0_INT_FORCE_MODF_SHIFT) & SPI0_INT_FORCE_MODF_MASK)

/* ---- TXDATA @ 0x01C : TX FIFO push ---- */
#define SPI0_TXDATA_OFFSET                 0x01Cu
#define SPI0_TXDATA_ADDR                   (SPI0_BASE + SPI0_TXDATA_OFFSET)
#define SPI0_TXDATA_RESET                  0x00000000u
#define SPI0_TXDATA_DEFINED_MASK           0xFFFFFFFFu
#define SPI0_TXDATA_RW_MASK                0x00000000u
#define SPI0_TXDATA_RO_MASK                0x00000000u
#define SPI0_TXDATA_WO_MASK                0xFFFFFFFFu
#define SPI0_TXDATA_W1C_MASK               0x00000000u
#define SPI0_TXDATA_W1S_MASK               0x00000000u
#define SPI0_TXDATA_RC_MASK                0x00000000u
#define SPI0_TXDATA_VOLATILE_MASK          0x00000000u
#define SPI0_TXDATA_DATA_SHIFT             0u
#define SPI0_TXDATA_DATA_WIDTH             32u
#define SPI0_TXDATA_DATA_MASK              0xFFFFFFFFu
#define SPI0_TXDATA_DATA_RESET             0x0u
#define SPI0_TXDATA_DATA_GET(r)            ((((uint32_t)(r)) & SPI0_TXDATA_DATA_MASK) >> SPI0_TXDATA_DATA_SHIFT)
#define SPI0_TXDATA_DATA_PREP(v)           ((((uint32_t)(v)) << SPI0_TXDATA_DATA_SHIFT) & SPI0_TXDATA_DATA_MASK)

/* ---- RXDATA @ 0x020 : RX FIFO pop ---- */
#define SPI0_RXDATA_OFFSET                 0x020u
#define SPI0_RXDATA_ADDR                   (SPI0_BASE + SPI0_RXDATA_OFFSET)
#define SPI0_RXDATA_RESET                  0x00000000u
#define SPI0_RXDATA_DEFINED_MASK           0xFFFFFFFFu
#define SPI0_RXDATA_RW_MASK                0x00000000u
#define SPI0_RXDATA_RO_MASK                0xFFFFFFFFu
#define SPI0_RXDATA_WO_MASK                0x00000000u
#define SPI0_RXDATA_W1C_MASK               0x00000000u
#define SPI0_RXDATA_W1S_MASK               0x00000000u
#define SPI0_RXDATA_RC_MASK                0x00000000u
#define SPI0_RXDATA_VOLATILE_MASK          0xFFFFFFFFu
#define SPI0_RXDATA_DATA_SHIFT             0u
#define SPI0_RXDATA_DATA_WIDTH             32u
#define SPI0_RXDATA_DATA_MASK              0xFFFFFFFFu
#define SPI0_RXDATA_DATA_RESET             0x0u
#define SPI0_RXDATA_DATA_GET(r)            ((((uint32_t)(r)) & SPI0_RXDATA_DATA_MASK) >> SPI0_RXDATA_DATA_SHIFT)
#define SPI0_RXDATA_DATA_PREP(v)           ((((uint32_t)(v)) << SPI0_RXDATA_DATA_SHIFT) & SPI0_RXDATA_DATA_MASK)

/* ---- FIFO_LVL @ 0x024 : FIFO 채움 수준 ---- */
#define SPI0_FIFO_LVL_OFFSET               0x024u
#define SPI0_FIFO_LVL_ADDR                 (SPI0_BASE + SPI0_FIFO_LVL_OFFSET)
#define SPI0_FIFO_LVL_RESET                0x00000000u
#define SPI0_FIFO_LVL_DEFINED_MASK         0x00000F0Fu
#define SPI0_FIFO_LVL_RW_MASK              0x00000000u
#define SPI0_FIFO_LVL_RO_MASK              0x00000F0Fu
#define SPI0_FIFO_LVL_WO_MASK              0x00000000u
#define SPI0_FIFO_LVL_W1C_MASK             0x00000000u
#define SPI0_FIFO_LVL_W1S_MASK             0x00000000u
#define SPI0_FIFO_LVL_RC_MASK              0x00000000u
#define SPI0_FIFO_LVL_VOLATILE_MASK        0x00000F0Fu
#define SPI0_FIFO_LVL_TXLVL_SHIFT          0u
#define SPI0_FIFO_LVL_TXLVL_WIDTH          4u
#define SPI0_FIFO_LVL_TXLVL_MASK           0x0000000Fu
#define SPI0_FIFO_LVL_TXLVL_RESET          0x0u
#define SPI0_FIFO_LVL_TXLVL_GET(r)         ((((uint32_t)(r)) & SPI0_FIFO_LVL_TXLVL_MASK) >> SPI0_FIFO_LVL_TXLVL_SHIFT)
#define SPI0_FIFO_LVL_TXLVL_PREP(v)        ((((uint32_t)(v)) << SPI0_FIFO_LVL_TXLVL_SHIFT) & SPI0_FIFO_LVL_TXLVL_MASK)
#define SPI0_FIFO_LVL_RXLVL_SHIFT          8u
#define SPI0_FIFO_LVL_RXLVL_WIDTH          4u
#define SPI0_FIFO_LVL_RXLVL_MASK           0x00000F00u
#define SPI0_FIFO_LVL_RXLVL_RESET          0x0u
#define SPI0_FIFO_LVL_RXLVL_GET(r)         ((((uint32_t)(r)) & SPI0_FIFO_LVL_RXLVL_MASK) >> SPI0_FIFO_LVL_RXLVL_SHIFT)
#define SPI0_FIFO_LVL_RXLVL_PREP(v)        ((((uint32_t)(v)) << SPI0_FIFO_LVL_RXLVL_SHIFT) & SPI0_FIFO_LVL_RXLVL_MASK)

/* ---- CS_CTRL @ 0x028 : chip-select 제어 ---- */
#define SPI0_CS_CTRL_OFFSET                0x028u
#define SPI0_CS_CTRL_ADDR                  (SPI0_BASE + SPI0_CS_CTRL_OFFSET)
#define SPI0_CS_CTRL_RESET                 0x00000400u
#define SPI0_CS_CTRL_DEFINED_MASK          0x0000FFF3u
#define SPI0_CS_CTRL_RW_MASK               0x0000FFF3u
#define SPI0_CS_CTRL_RO_MASK               0x00000000u
#define SPI0_CS_CTRL_WO_MASK               0x00000000u
#define SPI0_CS_CTRL_W1C_MASK              0x00000000u
#define SPI0_CS_CTRL_W1S_MASK              0x00000000u
#define SPI0_CS_CTRL_RC_MASK               0x00000000u
#define SPI0_CS_CTRL_VOLATILE_MASK         0x00000000u
#define SPI0_CS_CTRL_CS0_EN_SHIFT          0u
#define SPI0_CS_CTRL_CS0_EN_WIDTH          1u
#define SPI0_CS_CTRL_CS0_EN_MASK           0x00000001u
#define SPI0_CS_CTRL_CS0_EN_RESET          0x0u
#define SPI0_CS_CTRL_CS0_EN_GET(r)         ((((uint32_t)(r)) & SPI0_CS_CTRL_CS0_EN_MASK) >> SPI0_CS_CTRL_CS0_EN_SHIFT)
#define SPI0_CS_CTRL_CS0_EN_PREP(v)        ((((uint32_t)(v)) << SPI0_CS_CTRL_CS0_EN_SHIFT) & SPI0_CS_CTRL_CS0_EN_MASK)
#define SPI0_CS_CTRL_CS1_EN_SHIFT          1u
#define SPI0_CS_CTRL_CS1_EN_WIDTH          1u
#define SPI0_CS_CTRL_CS1_EN_MASK           0x00000002u
#define SPI0_CS_CTRL_CS1_EN_RESET          0x0u
#define SPI0_CS_CTRL_CS1_EN_GET(r)         ((((uint32_t)(r)) & SPI0_CS_CTRL_CS1_EN_MASK) >> SPI0_CS_CTRL_CS1_EN_SHIFT)
#define SPI0_CS_CTRL_CS1_EN_PREP(v)        ((((uint32_t)(v)) << SPI0_CS_CTRL_CS1_EN_SHIFT) & SPI0_CS_CTRL_CS1_EN_MASK)
#define SPI0_CS_CTRL_CS_POL_SHIFT          4u
#define SPI0_CS_CTRL_CS_POL_WIDTH          4u
#define SPI0_CS_CTRL_CS_POL_MASK           0x000000F0u
#define SPI0_CS_CTRL_CS_POL_RESET          0x0u
#define SPI0_CS_CTRL_CS_POL_GET(r)         ((((uint32_t)(r)) & SPI0_CS_CTRL_CS_POL_MASK) >> SPI0_CS_CTRL_CS_POL_SHIFT)
#define SPI0_CS_CTRL_CS_POL_PREP(v)        ((((uint32_t)(v)) << SPI0_CS_CTRL_CS_POL_SHIFT) & SPI0_CS_CTRL_CS_POL_MASK)
#define SPI0_CS_CTRL_CS_HOLD_SHIFT         8u
#define SPI0_CS_CTRL_CS_HOLD_WIDTH         8u
#define SPI0_CS_CTRL_CS_HOLD_MASK          0x0000FF00u
#define SPI0_CS_CTRL_CS_HOLD_RESET         0x4u
#define SPI0_CS_CTRL_CS_HOLD_GET(r)        ((((uint32_t)(r)) & SPI0_CS_CTRL_CS_HOLD_MASK) >> SPI0_CS_CTRL_CS_HOLD_SHIFT)
#define SPI0_CS_CTRL_CS_HOLD_PREP(v)       ((((uint32_t)(v)) << SPI0_CS_CTRL_CS_HOLD_SHIFT) & SPI0_CS_CTRL_CS_HOLD_MASK)

/* ---- ERR_CNT @ 0x02C : 에러 카운터 (읽으면 0) ---- */
#define SPI0_ERR_CNT_OFFSET                0x02Cu
#define SPI0_ERR_CNT_ADDR                  (SPI0_BASE + SPI0_ERR_CNT_OFFSET)
#define SPI0_ERR_CNT_RESET                 0x00000000u
#define SPI0_ERR_CNT_DEFINED_MASK          0x0000FFFFu
#define SPI0_ERR_CNT_RW_MASK               0x00000000u
#define SPI0_ERR_CNT_RO_MASK               0x00000000u
#define SPI0_ERR_CNT_WO_MASK               0x00000000u
#define SPI0_ERR_CNT_W1C_MASK              0x00000000u
#define SPI0_ERR_CNT_W1S_MASK              0x00000000u
#define SPI0_ERR_CNT_RC_MASK               0x0000FFFFu
#define SPI0_ERR_CNT_VOLATILE_MASK         0x0000FFFFu
#define SPI0_ERR_CNT_OVR_SHIFT             0u
#define SPI0_ERR_CNT_OVR_WIDTH             8u
#define SPI0_ERR_CNT_OVR_MASK              0x000000FFu
#define SPI0_ERR_CNT_OVR_RESET             0x0u
#define SPI0_ERR_CNT_OVR_GET(r)            ((((uint32_t)(r)) & SPI0_ERR_CNT_OVR_MASK) >> SPI0_ERR_CNT_OVR_SHIFT)
#define SPI0_ERR_CNT_OVR_PREP(v)           ((((uint32_t)(v)) << SPI0_ERR_CNT_OVR_SHIFT) & SPI0_ERR_CNT_OVR_MASK)
#define SPI0_ERR_CNT_MODF_SHIFT            8u
#define SPI0_ERR_CNT_MODF_WIDTH            8u
#define SPI0_ERR_CNT_MODF_MASK             0x0000FF00u
#define SPI0_ERR_CNT_MODF_RESET            0x0u
#define SPI0_ERR_CNT_MODF_GET(r)           ((((uint32_t)(r)) & SPI0_ERR_CNT_MODF_MASK) >> SPI0_ERR_CNT_MODF_SHIFT)
#define SPI0_ERR_CNT_MODF_PREP(v)          ((((uint32_t)(v)) << SPI0_ERR_CNT_MODF_SHIFT) & SPI0_ERR_CNT_MODF_MASK)

/* ---- DMA_CTRL @ 0x030 : DMA 요청 제어 ---- */
#define SPI0_DMA_CTRL_OFFSET               0x030u
#define SPI0_DMA_CTRL_ADDR                 (SPI0_BASE + SPI0_DMA_CTRL_OFFSET)
#define SPI0_DMA_CTRL_RESET                0x00010400u
#define SPI0_DMA_CTRL_DEFINED_MASK         0x000F0F03u
#define SPI0_DMA_CTRL_RW_MASK              0x000F0F03u
#define SPI0_DMA_CTRL_RO_MASK              0x00000000u
#define SPI0_DMA_CTRL_WO_MASK              0x00000000u
#define SPI0_DMA_CTRL_W1C_MASK             0x00000000u
#define SPI0_DMA_CTRL_W1S_MASK             0x00000000u
#define SPI0_DMA_CTRL_RC_MASK              0x00000000u
#define SPI0_DMA_CTRL_VOLATILE_MASK        0x00000000u
#define SPI0_DMA_CTRL_TXDMAEN_SHIFT        0u
#define SPI0_DMA_CTRL_TXDMAEN_WIDTH        1u
#define SPI0_DMA_CTRL_TXDMAEN_MASK         0x00000001u
#define SPI0_DMA_CTRL_TXDMAEN_RESET        0x0u
#define SPI0_DMA_CTRL_TXDMAEN_GET(r)       ((((uint32_t)(r)) & SPI0_DMA_CTRL_TXDMAEN_MASK) >> SPI0_DMA_CTRL_TXDMAEN_SHIFT)
#define SPI0_DMA_CTRL_TXDMAEN_PREP(v)      ((((uint32_t)(v)) << SPI0_DMA_CTRL_TXDMAEN_SHIFT) & SPI0_DMA_CTRL_TXDMAEN_MASK)
#define SPI0_DMA_CTRL_RXDMAEN_SHIFT        1u
#define SPI0_DMA_CTRL_RXDMAEN_WIDTH        1u
#define SPI0_DMA_CTRL_RXDMAEN_MASK         0x00000002u
#define SPI0_DMA_CTRL_RXDMAEN_RESET        0x0u
#define SPI0_DMA_CTRL_RXDMAEN_GET(r)       ((((uint32_t)(r)) & SPI0_DMA_CTRL_RXDMAEN_MASK) >> SPI0_DMA_CTRL_RXDMAEN_SHIFT)
#define SPI0_DMA_CTRL_RXDMAEN_PREP(v)      ((((uint32_t)(v)) << SPI0_DMA_CTRL_RXDMAEN_SHIFT) & SPI0_DMA_CTRL_RXDMAEN_MASK)
#define SPI0_DMA_CTRL_TXTHR_SHIFT          8u
#define SPI0_DMA_CTRL_TXTHR_WIDTH          4u
#define SPI0_DMA_CTRL_TXTHR_MASK           0x00000F00u
#define SPI0_DMA_CTRL_TXTHR_RESET          0x4u
#define SPI0_DMA_CTRL_TXTHR_GET(r)         ((((uint32_t)(r)) & SPI0_DMA_CTRL_TXTHR_MASK) >> SPI0_DMA_CTRL_TXTHR_SHIFT)
#define SPI0_DMA_CTRL_TXTHR_PREP(v)        ((((uint32_t)(v)) << SPI0_DMA_CTRL_TXTHR_SHIFT) & SPI0_DMA_CTRL_TXTHR_MASK)
#define SPI0_DMA_CTRL_RXTHR_SHIFT          16u
#define SPI0_DMA_CTRL_RXTHR_WIDTH          4u
#define SPI0_DMA_CTRL_RXTHR_MASK           0x000F0000u
#define SPI0_DMA_CTRL_RXTHR_RESET          0x1u
#define SPI0_DMA_CTRL_RXTHR_GET(r)         ((((uint32_t)(r)) & SPI0_DMA_CTRL_RXTHR_MASK) >> SPI0_DMA_CTRL_RXTHR_SHIFT)
#define SPI0_DMA_CTRL_RXTHR_PREP(v)        ((((uint32_t)(v)) << SPI0_DMA_CTRL_RXTHR_SHIFT) & SPI0_DMA_CTRL_RXTHR_MASK)

/* ---- SCRATCH @ 0x03C : SW 스크래치 (버스 sanity 용) ---- */
#define SPI0_SCRATCH_OFFSET                0x03Cu
#define SPI0_SCRATCH_ADDR                  (SPI0_BASE + SPI0_SCRATCH_OFFSET)
#define SPI0_SCRATCH_RESET                 0x00000000u
#define SPI0_SCRATCH_DEFINED_MASK          0xFFFFFFFFu
#define SPI0_SCRATCH_RW_MASK               0xFFFFFFFFu
#define SPI0_SCRATCH_RO_MASK               0x00000000u
#define SPI0_SCRATCH_WO_MASK               0x00000000u
#define SPI0_SCRATCH_W1C_MASK              0x00000000u
#define SPI0_SCRATCH_W1S_MASK              0x00000000u
#define SPI0_SCRATCH_RC_MASK               0x00000000u
#define SPI0_SCRATCH_VOLATILE_MASK         0x00000000u
#define SPI0_SCRATCH_VAL_SHIFT             0u
#define SPI0_SCRATCH_VAL_WIDTH             32u
#define SPI0_SCRATCH_VAL_MASK              0xFFFFFFFFu
#define SPI0_SCRATCH_VAL_RESET             0x0u
#define SPI0_SCRATCH_VAL_GET(r)            ((((uint32_t)(r)) & SPI0_SCRATCH_VAL_MASK) >> SPI0_SCRATCH_VAL_SHIFT)
#define SPI0_SCRATCH_VAL_PREP(v)           ((((uint32_t)(v)) << SPI0_SCRATCH_VAL_SHIFT) & SPI0_SCRATCH_VAL_MASK)

typedef struct {
    const char *name;
    uint32_t offset, reset, defined, rw, ro, wo, w1c, w1s, rc, vol;
} spi0_reg_desc_t;

#ifdef SPI0_REGS_WANT_TABLE   /* 테이블이 필요한 번역 단위 하나에서만 정의하고 include */
static const spi0_reg_desc_t SPI0_REGS[SPI0_REG_COUNT] = {
    /* name, offset, reset, defined, rw, ro, wo, w1c, w1s, rc, vol */
    { "ID",        0x000u, 0x53500102u, 0xFFFFFFFFu, 0x00000000u, 0xFFFFFFFFu, 0x00000000u, 0x00000000u, 0x00000000u, 0x00000000u, 0x00000000u },
    { "CTRL",      0x004u, 0x00070302u, 0x001F0F3Fu, 0x001F0F3Fu, 0x00000000u, 0x00000000u, 0x00000000u, 0x00000000u, 0x00000000u, 0x00000000u },
    { "STATUS",    0x008u, 0x00000002u, 0x0000000Fu, 0x00000000u, 0x0000000Fu, 0x00000000u, 0x00000000u, 0x00000000u, 0x00000000u, 0x0000000Fu },
    { "INT_RAW",   0x00Cu, 0x00000000u, 0x00000007u, 0x00000000u, 0x00000000u, 0x00000000u, 0x00000007u, 0x00000000u, 0x00000000u, 0x00000007u },
    { "INT_EN",    0x010u, 0x00000000u, 0x00000007u, 0x00000007u, 0x00000000u, 0x00000000u, 0x00000000u, 0x00000000u, 0x00000000u, 0x00000000u },
    { "INT_STAT",  0x014u, 0x00000000u, 0x00000007u, 0x00000000u, 0x00000007u, 0x00000000u, 0x00000000u, 0x00000000u, 0x00000000u, 0x00000007u },
    { "INT_FORCE", 0x018u, 0x00000000u, 0x00000007u, 0x00000000u, 0x00000000u, 0x00000007u, 0x00000000u, 0x00000000u, 0x00000000u, 0x00000000u },
    { "TXDATA",    0x01Cu, 0x00000000u, 0xFFFFFFFFu, 0x00000000u, 0x00000000u, 0xFFFFFFFFu, 0x00000000u, 0x00000000u, 0x00000000u, 0x00000000u },
    { "RXDATA",    0x020u, 0x00000000u, 0xFFFFFFFFu, 0x00000000u, 0xFFFFFFFFu, 0x00000000u, 0x00000000u, 0x00000000u, 0x00000000u, 0xFFFFFFFFu },
    { "FIFO_LVL",  0x024u, 0x00000000u, 0x00000F0Fu, 0x00000000u, 0x00000F0Fu, 0x00000000u, 0x00000000u, 0x00000000u, 0x00000000u, 0x00000F0Fu },
    { "CS_CTRL",   0x028u, 0x00000400u, 0x0000FFF3u, 0x0000FFF3u, 0x00000000u, 0x00000000u, 0x00000000u, 0x00000000u, 0x00000000u, 0x00000000u },
    { "ERR_CNT",   0x02Cu, 0x00000000u, 0x0000FFFFu, 0x00000000u, 0x00000000u, 0x00000000u, 0x00000000u, 0x00000000u, 0x0000FFFFu, 0x0000FFFFu },
    { "DMA_CTRL",  0x030u, 0x00010400u, 0x000F0F03u, 0x000F0F03u, 0x00000000u, 0x00000000u, 0x00000000u, 0x00000000u, 0x00000000u, 0x00000000u },
    { "SCRATCH",   0x03Cu, 0x00000000u, 0xFFFFFFFFu, 0xFFFFFFFFu, 0x00000000u, 0x00000000u, 0x00000000u, 0x00000000u, 0x00000000u, 0x00000000u },
};
#endif

#endif /* SPI0_REGS_H */
