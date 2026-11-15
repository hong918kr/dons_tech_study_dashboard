/* ENV: qemu */
/* SPDX-License-Identifier: GPL-2.0 WITH Linux-syscall-note */
/*
 * kecho_ioctl.h — ELX-B08: /dev/kecho ioctl ABI (유저와 커널이 같이 include).
 * 실제 드라이버라면 include/uapi/linux/ 에 둔다.
 * 규칙: 고정 폭 타입(__u32/__u64)만, long/포인터/enum/size_t 금지, 패딩은 명시.
 */
#ifndef KECHO_IOCTL_H
#define KECHO_IOCTL_H

#include <linux/types.h>
#include <linux/ioctl.h>

/* 'x' (0x78): ioctl-number.rst(6.18)에 없는 문자. 'k'는 spidev가 이미 쓴다. */
#define KECHO_IOC_MAGIC		'x'

#define KECHO_MODE_RAW		0	/* write 한 바이트 그대로 저장 */
#define KECHO_MODE_UPPER	1	/* write 할 때 a-z 를 A-Z 로 바꿔 저장 */
#define KECHO_MODE_MAX		KECHO_MODE_UPPER

/* 커널 -> 유저 (_IOR). 모든 ABI(arm64/arm32/x86_64/i386)에서 40 바이트, 구멍 없음. */
struct kecho_stats {
	__u64 bytes_written;	/*  0 */
	__u64 bytes_read;	/*  8 */
	__u32 nwrites;		/* 16 */
	__u32 nreads;		/* 20 */
	__u32 data_len;		/* 24  현재 버퍼에 든 바이트 수 */
	__u32 mode;		/* 28  KECHO_MODE_* */
	__u32 bad_cmds;		/* 32  -ENOTTY 로 거절한 ioctl 수 */
	__u32 __pad;		/* 36  명시적 패딩: 항상 0. 없으면 i386 에서 36 B 가 된다 */
};				/* 40 */

#define KECHO_NR_GET_STATS	1
#define KECHO_NR_SET_MODE	2
#define KECHO_NR_RESET		3

#define KECHO_IOC_GET_STATS	_IOR(KECHO_IOC_MAGIC, KECHO_NR_GET_STATS, struct kecho_stats)
#define KECHO_IOC_SET_MODE	_IOW(KECHO_IOC_MAGIC, KECHO_NR_SET_MODE, __u32)
#define KECHO_IOC_RESET		_IO(KECHO_IOC_MAGIC, KECHO_NR_RESET)

/* ---- 추가 연습(practice.c)에서만 구현하는 명령 ---- */
struct kecho_peek {		/* 유저 포인터는 __u64 에 싣는다 (32비트 앱은 0-확장) */
	__u64 buf_ptr;		/*  0  유저 버퍼 주소 */
	__u32 offset;		/*  8  데이터 안의 시작 위치 */
	__u32 len;		/* 12  in: 원하는 길이, out: 실제로 복사한 길이 */
};				/* 16 */

#define KECHO_NR_GET_MODE	4
#define KECHO_NR_XCHG_MODE	5
#define KECHO_NR_PEEK		6

#define KECHO_IOC_GET_MODE	_IOR(KECHO_IOC_MAGIC, KECHO_NR_GET_MODE, __u32)
#define KECHO_IOC_XCHG_MODE	_IOWR(KECHO_IOC_MAGIC, KECHO_NR_XCHG_MODE, __u32)
#define KECHO_IOC_PEEK		_IOWR(KECHO_IOC_MAGIC, KECHO_NR_PEEK, struct kecho_peek)

#endif /* KECHO_IOCTL_H */
