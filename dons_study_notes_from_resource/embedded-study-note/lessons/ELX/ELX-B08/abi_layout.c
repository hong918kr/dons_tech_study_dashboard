/* ENV: linux */
/*
 * ELX-B08 ABI demo: 같은 구조체를 arm64 / arm32(armhf) / x86_64 / i386 으로 컴파일해
 * pahole 로 레이아웃을 보고, 그 크기가 들어간 _IOR 번호를 .rodata 에서 읽는다.
 * 실행하지 않는다 (gcc -g -c 만). abi_demo.sh 가 돌린다.
 */
#include <linux/types.h>
#include <linux/ioctl.h>
#include "kecho_ioctl.h"			/* good: struct kecho_stats (40 B, 명시적 __pad) */

struct kecho_stats_nopad {		/* __pad 를 뺀 것: 꼬리 패딩이 ABI 마다 다르다 */
	__u64 bytes_written;
	__u64 bytes_read;
	__u32 nwrites;
	__u32 nreads;
	__u32 data_len;
	__u32 mode;
	__u32 bad_cmds;
};

struct kecho_stats_bad {		/* long 과 포인터: 크기 자체가 ABI 마다 다르다 */
	__u32 mode;
	unsigned long bytes_written;
	void *user_buf;
	__u64 total;
};

__attribute__((used)) const unsigned int kecho_nums[3] = {
	_IOR(KECHO_IOC_MAGIC, 1, struct kecho_stats),
	_IOR(KECHO_IOC_MAGIC, 1, struct kecho_stats_nopad),
	_IOR(KECHO_IOC_MAGIC, 1, struct kecho_stats_bad),
};

/* pahole 이 타입을 보도록 변수 하나씩 */
struct kecho_stats g_good;
struct kecho_stats_nopad g_nopad;
struct kecho_stats_bad g_bad;
