/* ELX-B05 Device Tree 읽기·쓰기: libfdt 없이 DTB(FDT) 파서 만들기
 *
 * 빌드: cc -std=c11 -Wall -Wextra -Werror -O2 starter.c -o /tmp/ELX-B05_x   (solution.c 도 같은 방법)
 * 실행: /tmp/ELX-B05_x             → 자체 테스트 (N/M passed)
 *       /tmp/ELX-B05_x board.dtb   → dtc -I dtb -O dts 와 같은 형식으로 출력
 * DTB 는 board_dtb.h 에 C 배열로 들어 있다 (make_blob.sh 가 board.dts 에서 생성).
 */
#include <stdarg.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "board_dtb.h"

#define FDT_MAGIC      0xd00dfeedu
#define FDT_BEGIN_NODE 0x1u
#define FDT_END_NODE   0x2u
#define FDT_PROP       0x3u
#define FDT_NOP        0x4u
#define FDT_END        0x9u
#define FDT_HDR_SIZE   40u
#define FDT_MAX_DEPTH  32

enum {
	FDT_OK = 0,
	FDT_ERR_SHORT = -1,     /* 버퍼가 헤더(40 B)보다 작다 */
	FDT_ERR_MAGIC = -2,     /* magic != 0xd00dfeed */
	FDT_ERR_VERSION = -3,   /* version < 17 또는 last_comp_version > 17 */
	FDT_ERR_TRUNC = -4,     /* 블록·토큰·값이 blob 밖으로 나간다 */
	FDT_ERR_ALIGN = -5,     /* 오프셋 정렬 위반 */
	FDT_ERR_BADTOKEN = -6,  /* 모르는 토큰 */
	FDT_ERR_STROFF = -7,    /* nameoff 가 strings 블록 밖 / NUL 없음 */
	FDT_ERR_NEST = -8,      /* BEGIN/END 짝이 안 맞음, 너무 깊음 */
	FDT_ERR_NOSPACE = -9,   /* 출력 버퍼 부족 */
	FDT_ERR_NOTFOUND = -10, /* 노드/프로퍼티/phandle 없음 */
	FDT_ERR_BADCELLS = -11, /* #address-cells/#size-cells 와 reg 길이가 안 맞음 */
};

typedef struct {
	const uint8_t *b;          /* blob 시작 */
	size_t len;                /* 버퍼 길이 (파일 크기) */
	uint32_t totalsize, off_struct, off_strings, off_rsvmap;
	uint32_t version, last_comp, boot_cpuid, size_strings, size_struct;
} fdt_t;

typedef struct {
	uint32_t tag;              /* FDT_BEGIN_NODE / FDT_PROP / ... */
	uint32_t off;              /* 이 토큰의 위치 (structure 블록 기준 오프셋) */
	const char *name;          /* BEGIN_NODE: 노드 이름, PROP: 프로퍼티 이름 */
	const uint8_t *val;        /* PROP 값 */
	uint32_t len;              /* PROP 값 길이 (패딩 전) */
} fdt_tok_t;

/* ---------------- 주어진 도우미 ---------------- */
static uint32_t be32(const uint8_t *p) {
	return (uint32_t)p[0] << 24 | (uint32_t)p[1] << 16 | (uint32_t)p[2] << 8 | p[3];
}
static uint64_t be64(const uint8_t *p) { return (uint64_t)be32(p) << 32 | be32(p + 4); }
static uint32_t align4(uint32_t x) { return (x + 3u) & ~3u; }

typedef struct { char *buf; size_t cap, len; bool overflow; } sb_t;   /* 출력 버퍼 */
static void sb_printf(sb_t *s, const char *fmt, ...) {
	va_list ap;
	va_start(ap, fmt);
	size_t room = s->len < s->cap ? s->cap - s->len : 0;
	int n = vsnprintf(room ? s->buf + s->len : NULL, room, fmt, ap);
	va_end(ap);
	if (n < 0 || (size_t)n >= room) { s->overflow = true; return; }
	s->len += (size_t)n;
}
static void sb_indent(sb_t *s, int depth) { for (int i = 0; i < depth; i++) sb_printf(s, "\t"); }

enum { VAL_STRING, VAL_CELLS, VAL_BYTES };
static const char esc_in[] = "\a\b\t\n\v\f\r\\\"", esc_out[] = "abtnvfr\\\"";
static void print_value(sb_t *s, int type, const uint8_t *v, uint32_t len) {
	if (type == VAL_STRING) {                 /* "a", "b" — NUL 마다 문자열 하나 */
		uint32_t i = 0;
		while (i < len) {
			const char *str = (const char *)v + i;
			size_t n = strlen(str);           /* guess_type 이 마지막 바이트 NUL 을 보장 */
			sb_printf(s, "%s\"", i ? ", " : "");
			for (size_t k = 0; k < n; k++) {      /* dtc 와 같은 이스케이프: \n → "\\n" */
				const char *e = strchr(esc_in, str[k]);
				if (e) sb_printf(s, "\\%c", esc_out[e - esc_in]);
				else sb_printf(s, "%c", str[k]);
			}
			sb_printf(s, "\"");
			i += (uint32_t)n + 1u;
		}
	} else if (type == VAL_CELLS) {           /* <0x01 0x9000000> — dtc 는 %02x */
		sb_printf(s, "<");
		for (uint32_t i = 0; i < len; i += 4)
			sb_printf(s, "%s0x%02x", i ? " " : "", be32(v + i));
		sb_printf(s, ">");
	} else {                                  /* [02 00 5e] */
		sb_printf(s, "[");
		for (uint32_t i = 0; i < len; i++)
			sb_printf(s, "%s%02x", i ? " " : "", v[i]);
		sb_printf(s, "]");
	}
}

/* ---------------- Part A: 헤더 검증 ---------------- */
/* 성공하면 f 를 채우고 FDT_OK. 모든 필드는 big-endian u32. 검사 순서:
 * SHORT → MAGIC → VERSION → TRUNC(totalsize) → ALIGN(rsvmap 8, struct 4) → TRUNC(각 블록) */
static int fdt_open(fdt_t *f, const uint8_t *blob, size_t len) {
	if (len < FDT_HDR_SIZE) return FDT_ERR_SHORT;
	if (be32(blob) != FDT_MAGIC) return FDT_ERR_MAGIC;
	f->b = blob;
	f->len = len;
	f->totalsize = be32(blob + 0x04);
	f->off_struct = be32(blob + 0x08);
	f->off_strings = be32(blob + 0x0c);
	f->off_rsvmap = be32(blob + 0x10);
	f->version = be32(blob + 0x14);
	f->last_comp = be32(blob + 0x18);
	f->boot_cpuid = be32(blob + 0x1c);
	f->size_strings = be32(blob + 0x20);
	f->size_struct = be32(blob + 0x24);
	if (f->version < 17 || f->last_comp > 17) return FDT_ERR_VERSION;
	if (f->totalsize > len || f->totalsize < FDT_HDR_SIZE) return FDT_ERR_TRUNC;
	if (f->off_rsvmap % 8 || f->off_struct % 4) return FDT_ERR_ALIGN;
	if (f->off_rsvmap < FDT_HDR_SIZE || f->off_rsvmap >= f->totalsize) return FDT_ERR_TRUNC;
	if ((uint64_t)f->off_struct + f->size_struct > f->totalsize) return FDT_ERR_TRUNC;
	if ((uint64_t)f->off_strings + f->size_strings > f->totalsize) return FDT_ERR_TRUNC;
	return FDT_OK;
}

/* ---------------- Part B: 토큰 순회와 DTS 출력 ---------------- */
/* structure 블록의 *pos 에서 토큰 하나를 읽어 t 에 채우고, *pos 를 다음 토큰(4바이트 정렬)으로 옮긴다.
 * 반환: 토큰 태그(양수) 또는 FDT_ERR_*. 절대 structure/strings 블록 밖을 읽지 않는다. */
static int fdt_next(const fdt_t *f, uint32_t *pos, fdt_tok_t *t) {
	const uint8_t *st = f->b + f->off_struct;
	uint32_t p = *pos, end = f->size_struct;
	if ((uint64_t)p + 4 > end) return FDT_ERR_TRUNC;
	memset(t, 0, sizeof *t);
	t->tag = be32(st + p);
	t->off = p;
	switch (t->tag) {
	case FDT_BEGIN_NODE: {
		const uint8_t *nm = st + p + 4;
		const uint8_t *nul = memchr(nm, '\0', end - (p + 4));
		if (!nul) return FDT_ERR_TRUNC;
		t->name = (const char *)nm;
		*pos = align4(p + 4 + (uint32_t)(nul - nm) + 1);
		break;
	}
	case FDT_PROP: {
		if ((uint64_t)p + 12 > end) return FDT_ERR_TRUNC;
		t->len = be32(st + p + 4);
		uint32_t nameoff = be32(st + p + 8);
		if ((uint64_t)p + 12 + t->len > end) return FDT_ERR_TRUNC;
		if (nameoff >= f->size_strings) return FDT_ERR_STROFF;
		const uint8_t *strs = f->b + f->off_strings;
		if (!memchr(strs + nameoff, '\0', f->size_strings - nameoff)) return FDT_ERR_STROFF;
		t->name = (const char *)strs + nameoff;
		t->val = st + p + 12;
		*pos = align4(p + 12 + t->len);
		break;
	}
	case FDT_END_NODE:
	case FDT_NOP:
	case FDT_END:
		*pos = p + 4;
		break;
	default:
		return FDT_ERR_BADTOKEN;
	}
	return (int)t->tag;
}

/* dtc 역컴파일러(treesource.c guess_value_type)와 같은 규칙으로 값의 모양을 추측한다:
 * 마지막 바이트가 NUL + 모든 바이트가 "문자열 문자"(0x20~0x7e, NUL, \a\b\t\n\v\f\r)
 * + NUL 개수 <= 나머지 개수 → 문자열(목록). 아니고 길이가 4의 배수 → 셀, 나머지 → 바이트 */
static int guess_type(const uint8_t *v, uint32_t len) {
	uint32_t nnul = 0;
	bool printable = true;
	for (uint32_t i = 0; i < len; i++) {
		if (v[i] == '\0') nnul++;
		else if ((v[i] < 0x20 || v[i] > 0x7e) && !(v[i] >= '\a' && v[i] <= '\r')) printable = false;
	}
	if (len > 0 && v[len - 1] == '\0' && printable && nnul <= len - nnul) return VAL_STRING;
	if (len % 4 == 0) return VAL_CELLS;
	return VAL_BYTES;
}

/* `dtc -I dtb -O dts` 와 바이트 단위로 같은 텍스트를 out 에 쓴다.
 * /dts-v1/; → /memreserve/ 줄들 → 노드 트리. 자식 노드 앞엔 빈 줄 하나, 들여쓰기는 탭. */
static int fdt_to_dts(const fdt_t *f, char *out, size_t cap) {
	sb_t s = { out, cap, 0, false };
	sb_printf(&s, "/dts-v1/;\n\n");
	for (uint32_t r = f->off_rsvmap;; r += 16) {
		if ((uint64_t)r + 16 > f->totalsize) return FDT_ERR_TRUNC;
		uint64_t addr = be64(f->b + r), size = be64(f->b + r + 8);
		if (addr == 0 && size == 0) break;
		sb_printf(&s, "/memreserve/\t0x%016llx 0x%016llx;\n",
			  (unsigned long long)addr, (unsigned long long)size);
	}
	uint32_t pos = 0;
	int depth = 0;
	bool seen_root = false;
	fdt_tok_t t;
	for (;;) {
		int tag = fdt_next(f, &pos, &t);
		if (tag < 0) return tag;
		if (tag == FDT_NOP) continue;
		if (tag == FDT_BEGIN_NODE) {
			if (depth == 0 && seen_root) return FDT_ERR_NEST;   /* 루트가 두 개 */
			if (depth >= FDT_MAX_DEPTH) return FDT_ERR_NEST;
			if (depth == 0) sb_printf(&s, "/ {\n");
			else { sb_printf(&s, "\n"); sb_indent(&s, depth); sb_printf(&s, "%s {\n", t.name); }
			seen_root = true;
			depth++;
		} else if (tag == FDT_PROP) {
			if (depth == 0) return FDT_ERR_NEST;               /* 노드 밖 프로퍼티 */
			sb_indent(&s, depth);
			sb_printf(&s, "%s", t.name);
			if (t.len) { sb_printf(&s, " = "); print_value(&s, guess_type(t.val, t.len), t.val, t.len); }
			sb_printf(&s, ";\n");
		} else if (tag == FDT_END_NODE) {
			if (depth == 0) return FDT_ERR_NEST;
			depth--;
			sb_indent(&s, depth);
			sb_printf(&s, "};\n");
		} else { /* FDT_END */
			if (depth != 0 || !seen_root) return FDT_ERR_NEST;
			break;
		}
	}
	return s.overflow ? FDT_ERR_NOSPACE : FDT_OK;
}

/* ---------------- 주어진 도우미 (Part C 용) ---------------- */
/* 노드(BEGIN_NODE 오프셋)의 프로퍼티 하나를 찾는다. 프로퍼티는 자식 노드보다 먼저 나온다. */
static int fdt_getprop(const fdt_t *f, uint32_t node, const char *name, const uint8_t **val, uint32_t *len) {
	uint32_t pos = node;
	fdt_tok_t t;
	int tag = fdt_next(f, &pos, &t);
	if (tag != (int)FDT_BEGIN_NODE) return tag < 0 ? tag : FDT_ERR_NOTFOUND;
	for (;;) {
		tag = fdt_next(f, &pos, &t);
		if (tag < 0) return tag;
		if (tag == FDT_NOP) continue;
		if (tag != FDT_PROP) return FDT_ERR_NOTFOUND;
		if (strcmp(t.name, name) == 0) { *val = t.val; *len = t.len; return FDT_OK; }
	}
}

/* 경로("/soc@0/i2c@9040000")로 노드를 찾는다. stack[0]=루트 … stack[*depth-1]=그 노드. */
static int fdt_find(const fdt_t *f, const char *path, uint32_t stack[FDT_MAX_DEPTH], int *depth) {
	char cur[512];
	size_t plen[FDT_MAX_DEPTH + 1];
	int d = 0;
	uint32_t pos = 0;
	fdt_tok_t t;
	for (;;) {
		int tag = fdt_next(f, &pos, &t);
		if (tag < 0) return tag;
		if (tag == FDT_END) return FDT_ERR_NOTFOUND;
		if (tag == FDT_END_NODE) { if (d == 0) return FDT_ERR_NEST; d--; continue; }
		if (tag != FDT_BEGIN_NODE) continue;
		if (d >= FDT_MAX_DEPTH) return FDT_ERR_NEST;
		size_t base = d ? plen[d - 1] : 0;
		int n = d == 0 ? snprintf(cur, sizeof cur, "/")
			       : snprintf(cur + base, sizeof cur - base, "%s%s", d > 1 ? "/" : "", t.name);
		if (n < 0 || base + (size_t)n >= sizeof cur) return FDT_ERR_NOSPACE;
		plen[d] = base + (size_t)n;
		stack[d++] = t.off;
		if (strcmp(cur, path) == 0) { *depth = d; return FDT_OK; }
	}
}

/* 노드 오프셋 → 전체 경로 문자열 */
static int fdt_get_path(const fdt_t *f, uint32_t node, char *out, size_t cap) {
	uint32_t pos = 0, stack[FDT_MAX_DEPTH];
	int d = 0;
	fdt_tok_t t;
	for (;;) {
		int tag = fdt_next(f, &pos, &t);
		if (tag < 0) return tag;
		if (tag == FDT_END) return FDT_ERR_NOTFOUND;
		if (tag == FDT_END_NODE) { if (d == 0) return FDT_ERR_NEST; d--; continue; }
		if (tag != FDT_BEGIN_NODE) continue;
		if (d >= FDT_MAX_DEPTH) return FDT_ERR_NEST;
		stack[d++] = t.off;
		if (t.off != node) continue;
		sb_t s = { out, cap, 0, false };
		if (d == 1) sb_printf(&s, "/");
		for (int i = 1; i < d; i++) {            /* 이름은 BEGIN_NODE 토큰 바로 뒤에 있다 */
			sb_printf(&s, "/%s", (const char *)f->b + f->off_struct + stack[i] + 4);
		}
		return s.overflow ? FDT_ERR_NOSPACE : FDT_OK;
	}
}

/* ---------------- Part C: 셀 해석, phandle, interrupt-parent ---------------- */
static uint32_t cells_or(const fdt_t *f, uint32_t node, const char *name, uint32_t dflt) {
	const uint8_t *v;
	uint32_t len;
	if (fdt_getprop(f, node, name, &v, &len) == FDT_OK && len == 4) return be32(v);
	return dflt;
}

/* path 노드의 reg 에서 idx 번째 (주소, 크기). 셀 개수는 **부모**의 #address-cells(기본 2)/#size-cells(기본 1).
 * 주소 셀 1~2, 크기 셀 0~2만 지원. reg 길이가 (ac+sc)*4 의 배수가 아니면 FDT_ERR_BADCELLS. */
static int fdt_reg(const fdt_t *f, const char *path, int idx, uint64_t *addr, uint64_t *size) {
	uint32_t stack[FDT_MAX_DEPTH];
	int d;
	int rc = fdt_find(f, path, stack, &d);
	if (rc) return rc;
	if (d < 2) return FDT_ERR_NOTFOUND;                     /* 루트의 reg 는 해석할 부모가 없다 */
	uint32_t ac = cells_or(f, stack[d - 2], "#address-cells", 2);
	uint32_t sc = cells_or(f, stack[d - 2], "#size-cells", 1);
	if (ac < 1 || ac > 2 || sc > 2) return FDT_ERR_BADCELLS;
	const uint8_t *v;
	uint32_t len;
	rc = fdt_getprop(f, stack[d - 1], "reg", &v, &len);
	if (rc) return rc;
	uint32_t entry = (ac + sc) * 4;
	if (len % entry) return FDT_ERR_BADCELLS;
	if (idx < 0 || (uint32_t)idx >= len / entry) return FDT_ERR_NOTFOUND;
	const uint8_t *e = v + (uint32_t)idx * entry;
	*addr = ac == 2 ? be64(e) : be32(e);
	*size = sc == 2 ? be64(e + ac * 4) : sc == 1 ? be32(e + ac * 4) : 0;
	return FDT_OK;
}

/* phandle 값 → 그 노드의 BEGIN_NODE 오프셋. 0 과 0xffffffff 는 유효한 phandle 이 아니다. */
static int fdt_node_by_phandle(const fdt_t *f, uint32_t ph, uint32_t *node) {
	if (ph == 0 || ph == 0xffffffffu) return FDT_ERR_NOTFOUND;
	uint32_t pos = 0;
	fdt_tok_t t;
	for (;;) {
		int tag = fdt_next(f, &pos, &t);
		if (tag < 0) return tag;
		if (tag == FDT_END) return FDT_ERR_NOTFOUND;
		if (tag != FDT_BEGIN_NODE) continue;
		const uint8_t *v;
		uint32_t len;
		if ((fdt_getprop(f, t.off, "phandle", &v, &len) == FDT_OK ||
		     fdt_getprop(f, t.off, "linux,phandle", &v, &len) == FDT_OK) && len == 4 && be32(v) == ph) {
			*node = t.off;
			return FDT_OK;
		}
	}
}

/* path 노드의 인터럽트 부모 경로. 자기 자신부터 루트까지 올라가며 처음 만나는 interrupt-parent 를 따른다. */
static int fdt_irq_parent(const fdt_t *f, const char *path, char *out, size_t cap) {
	uint32_t stack[FDT_MAX_DEPTH];
	int d;
	int rc = fdt_find(f, path, stack, &d);
	if (rc) return rc;
	for (int i = d - 1; i >= 0; i--) {
		const uint8_t *v;
		uint32_t len;
		if (fdt_getprop(f, stack[i], "interrupt-parent", &v, &len) != FDT_OK) continue;
		if (len != 4) return FDT_ERR_BADCELLS;
		uint32_t node;
		rc = fdt_node_by_phandle(f, be32(v), &node);
		if (rc) return rc;
		return fdt_get_path(f, node, out, cap);
	}
	return FDT_ERR_NOTFOUND;
}

/* ---------------- 테스트 하네스 ---------------- */
static int g_pass, g_total;
#define CHECK(expr) do { g_total++; if (expr) { g_pass++; } \
	else printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #expr); } while (0)

static uint8_t g_buf[4096];          /* 망가뜨린 blob 을 만드는 작업 공간 */
static char g_out[16384];

static void put32(uint8_t *p, uint32_t v) { p[0] = (uint8_t)(v >> 24); p[1] = (uint8_t)(v >> 16); p[2] = (uint8_t)(v >> 8); p[3] = (uint8_t)v; }
static size_t fresh(void) { memcpy(g_buf, board_dtb, sizeof board_dtb); return sizeof board_dtb; }
static int open_buf(size_t len) { fdt_t f; return fdt_open(&f, g_buf, len); }
static int dts_buf(size_t len) {
	fdt_t f;
	int rc = fdt_open(&f, g_buf, len);
	return rc ? rc : fdt_to_dts(&f, g_out, sizeof g_out);
}
/* board_dtb 에서 node_name 노드(NULL 이면 아무 노드) 안의 첫 PROP name 토큰의 blob 오프셋.
 * 테스트가 직접 걷는다 (fdt_next 가 TODO 여도 동작하도록) — 믿을 수 있는 board_dtb 에만 쓴다. */
static size_t find_prop_tok(const char *name, const char *node_name) {
	const uint8_t *b = board_dtb;
	uint32_t st = be32(b + 8), str = be32(b + 0x0c);
	bool in_node = node_name == NULL;
	for (uint32_t p = 0;;) {
		uint32_t tag = be32(b + st + p);
		if (tag == FDT_BEGIN_NODE) {
			const char *nm = (const char *)b + st + p + 4;
			if (node_name) in_node = !strcmp(nm, node_name);
			p = align4(p + 4 + (uint32_t)strlen(nm) + 1);
		} else if (tag == FDT_PROP) {
			if (in_node && !strcmp((const char *)b + str + be32(b + st + p + 8), name)) return st + p;
			p = align4(p + 12 + be32(b + st + p + 4));
		} else if (tag == FDT_END) {
			return 0;
		} else {
			p += 4;
		}
	}
}

static int dump_file(const char *path) {
	FILE *fp = fopen(path, "rb");
	if (!fp) { perror(path); return 2; }
	size_t cap = 1 << 16, n = 0;
	uint8_t *b = malloc(cap);
	size_t r;
	while (b && (r = fread(b + n, 1, cap - n, fp)) > 0) {
		n += r;
		if (n == cap) { uint8_t *nb = realloc(b, cap * 2); if (!nb) { free(b); b = NULL; break; } b = nb; cap *= 2; }
	}
	fclose(fp);
	if (!b) return 2;
	fdt_t f;
	int rc = fdt_open(&f, b, n);
	for (size_t ocap = 16 * n + 65536; rc == FDT_OK; ocap *= 2) {
		char *o = malloc(ocap);
		if (!o) { rc = FDT_ERR_NOSPACE; break; }
		rc = fdt_to_dts(&f, o, ocap);
		if (rc == FDT_OK) fputs(o, stdout);
		free(o);
		if (rc != FDT_ERR_NOSPACE) break;
		rc = FDT_OK;
	}
	free(b);
	if (rc) fprintf(stderr, "%s: parse error %d\n", path, rc);
	return rc ? 1 : 0;
}

int main(int argc, char **argv) {
	if (argc > 1) return dump_file(argv[1]);
	fdt_t f;
	memset(&f, 0, sizeof f);
	uint64_t a = 0, sz = 0;
	char path[256];
	size_t len;

	/* Part A — 헤더 (값은 fdtdump board.dtb 로 확인) */
	CHECK(fdt_open(&f, board_dtb, sizeof board_dtb) == FDT_OK);
	CHECK(f.totalsize == 1889 && f.version == 17 && f.last_comp == 16);
	CHECK(f.off_rsvmap == 0x28 && f.off_struct == 0x48 && f.off_strings == 0x664);
	CHECK(f.size_struct == 0x61c && f.size_strings == 0xfd && f.boot_cpuid == 0);
	CHECK(open_buf(39) == FDT_ERR_SHORT);
	len = fresh(); g_buf[3] = 0xee;                                    CHECK(open_buf(len) == FDT_ERR_MAGIC);
	len = fresh(); put32(g_buf + 0x14, 16);                            CHECK(open_buf(len) == FDT_ERR_VERSION);
	len = fresh(); put32(g_buf + 0x18, 18);                            CHECK(open_buf(len) == FDT_ERR_VERSION);
	len = fresh();                                                     CHECK(open_buf(len - 1) == FDT_ERR_TRUNC);
	len = fresh(); put32(g_buf + 0x08, 0x4a);                          CHECK(open_buf(len) == FDT_ERR_ALIGN);
	len = fresh(); put32(g_buf + 0x10, 0x2c);                          CHECK(open_buf(len) == FDT_ERR_ALIGN);
	len = fresh(); put32(g_buf + 0x20, 0xfffffff0u);                   CHECK(open_buf(len) == FDT_ERR_TRUNC);
	len = fresh(); put32(g_buf + 0x24, 0x761);                         CHECK(open_buf(len) == FDT_ERR_TRUNC);

	/* Part B — 토큰과 DTS 출력 */
	uint32_t pos = 0;
	fdt_tok_t t;
	CHECK(fdt_next(&f, &pos, &t) == (int)FDT_BEGIN_NODE && t.name && t.name[0] == '\0' && pos == 8);
	CHECK(fdt_next(&f, &pos, &t) == (int)FDT_PROP && t.name && !strcmp(t.name, "model") && t.len == 22 && pos == 44);
	CHECK(guess_type((const uint8_t *)"hi", 3) == VAL_STRING);
	CHECK(guess_type((const uint8_t *)"arm,pl011\0arm,primecell", 24) == VAL_STRING);
	CHECK(guess_type((const uint8_t *)"\0\0\0\x01", 4) == VAL_CELLS);
	CHECK(guess_type((const uint8_t *)"\x02\0\x5e\x10\0\x01", 6) == VAL_BYTES);
	CHECK(guess_type((const uint8_t *)"ab\n", 4) == VAL_STRING);          /* \n 도 문자열 문자 */
	CHECK(guess_type((const uint8_t *)"\x01" "ab", 4) == VAL_CELLS);       /* 0x01 은 아님 */
	CHECK(guess_type((const uint8_t *)"\0\0\0", 4) == VAL_CELLS);         /* NUL 이 너무 많다 */
	CHECK(guess_type((const uint8_t *)"", 0) == VAL_CELLS);                /* 빈 값: 출력은 "prop;" */
	CHECK(fdt_to_dts(&f, g_out, sizeof g_out) == FDT_OK && strcmp(g_out, board_dts_golden) == 0);
	CHECK(fdt_to_dts(&f, g_out, 100) == FDT_ERR_NOSPACE);
	size_t pt = find_prop_tok("model", NULL);
	len = fresh(); put32(g_buf + pt + 8, 0xfd);                        CHECK(dts_buf(len) == FDT_ERR_STROFF);
	len = fresh(); put32(g_buf + pt + 4, 0xfffffff0u);                 CHECK(dts_buf(len) == FDT_ERR_TRUNC);
	len = fresh(); put32(g_buf + pt, 7);                               CHECK(dts_buf(len) == FDT_ERR_BADTOKEN);
	len = fresh(); put32(g_buf + 0x48 + 0x61c - 4, FDT_END_NODE);      CHECK(dts_buf(len) == FDT_ERR_NEST);
	len = fresh(); put32(g_buf + 0x24, 0x61c - 4);                     CHECK(dts_buf(len) == FDT_ERR_TRUNC);
	len = fresh(); put32(g_buf + 0x48 + 0x61c - 8, FDT_END);           CHECK(dts_buf(len) == FDT_ERR_NEST);
	/* fdt_nop_property() 흉내: ethernet 의 status 프로퍼티(12 + "disabled\0"→12 = 24 B)를 NOP 6개로 덮는다 */
	size_t ps = find_prop_tok("status", "ethernet@9050000");
	len = fresh();
	for (int i = 0; i < 6; i++) put32(g_buf + ps + 4 * (size_t)i, FDT_NOP);
	{
		static char want[sizeof board_dts_golden];
		const char *line = "\t\t\tstatus = \"disabled\";\n";
		const char *hit = strstr(board_dts_golden, line);
		size_t head = hit ? (size_t)(hit - board_dts_golden) : 0;
		memcpy(want, board_dts_golden, head);
		strcpy(want + head, hit ? hit + strlen(line) : "");
		CHECK(hit && dts_buf(len) == FDT_OK && strcmp(g_out, want) == 0);
	}
	/* 퍼징: 모든 길이로 자르기 + 모든 바이트 뒤집기 → 에러 코드든 OK 든, 죽지만 않으면 된다 (ASan 으로 확인) */
	int trunc_err = 0, flip_ok = 0, flip_err = 0;
	for (size_t L = 0; L < sizeof board_dtb; L++) { fresh(); trunc_err += dts_buf(L) < 0; }
	for (size_t i = 0; i < sizeof board_dtb; i++) {
		len = fresh(); g_buf[i] ^= 0xff;
		int rc = dts_buf(len);
		if (rc == FDT_OK) flip_ok++; else if (rc >= FDT_ERR_BADCELLS) flip_err++;
	}
	CHECK(trunc_err == (int)sizeof board_dtb);
	CHECK(flip_ok + flip_err == (int)sizeof board_dtb);

	/* Part C — reg 셀, phandle, interrupt-parent */
	CHECK(fdt_reg(&f, "/memory@40000000", 0, &a, &sz) == FDT_OK && a == 0x40000000u && sz == 0x20000000u);
	CHECK(fdt_reg(&f, "/interrupt-controller@8000000", 1, &a, &sz) == FDT_OK && a == 0x08010000u && sz == 0x2000u);
	CHECK(fdt_reg(&f, "/soc@0/serial@9000000", 0, &a, &sz) == FDT_OK && a == 0x09000000u && sz == 0x1000u);
	CHECK(fdt_reg(&f, "/soc@0/i2c@9040000/temp-sensor@48", 0, &a, &sz) == FDT_OK && a == 0x48 && sz == 0);
	CHECK(fdt_reg(&f, "/cpus/cpu@1", 0, &a, &sz) == FDT_OK && a == 1 && sz == 0);
	CHECK(fdt_reg(&f, "/soc@0/serial@9000000", 1, &a, &sz) == FDT_ERR_NOTFOUND);
	CHECK(fdt_reg(&f, "/chosen", 0, &a, &sz) == FDT_ERR_NOTFOUND);
	CHECK(fdt_reg(&f, "/soc@0/nope@0", 0, &a, &sz) == FDT_ERR_NOTFOUND);
	uint32_t node = 0;
	CHECK(fdt_node_by_phandle(&f, 1, &node) == FDT_OK && fdt_get_path(&f, node, path, sizeof path) == FDT_OK &&
	      !strcmp(path, "/interrupt-controller@8000000"));
	CHECK(fdt_node_by_phandle(&f, 2, &node) == FDT_OK && fdt_get_path(&f, node, path, sizeof path) == FDT_OK &&
	      !strcmp(path, "/soc@0/gpio@9030000"));
	CHECK(fdt_node_by_phandle(&f, 99, &node) == FDT_ERR_NOTFOUND);
	CHECK(fdt_node_by_phandle(&f, 0, &node) == FDT_ERR_NOTFOUND);
	CHECK(fdt_irq_parent(&f, "/soc@0/i2c@9040000/temp-sensor@48", path, sizeof path) == FDT_OK &&
	      !strcmp(path, "/soc@0/gpio@9030000"));
	CHECK(fdt_irq_parent(&f, "/soc@0/serial@9000000", path, sizeof path) == FDT_OK &&
	      !strcmp(path, "/interrupt-controller@8000000"));
	CHECK(fdt_irq_parent(&f, "/soc@0/i2c@9040000/eeprom@50", path, sizeof path) == FDT_OK &&
	      !strcmp(path, "/interrupt-controller@8000000"));
	/* 잘못된 #size-cells: i2c 의 #size-cells 를 1 로 바꾸면 센서 reg(4 B)가 (1+1)*4 의 배수가 아니다 */
	{
		size_t psc = find_prop_tok("#size-cells", "i2c@9040000");
		len = fresh(); put32(g_buf + psc + 12, 1);
		fdt_t g;
		CHECK(fdt_open(&g, g_buf, len) == FDT_OK &&
		      fdt_reg(&g, "/soc@0/i2c@9040000/temp-sensor@48", 0, &a, &sz) == FDT_ERR_BADCELLS);
	}

	printf("fuzz: %d/%zu truncations rejected, byte flips: %d parsed OK, %d rejected\n",
	       trunc_err, sizeof board_dtb, flip_ok, flip_err);
	printf("%d/%d passed\n", g_pass, g_total);
	return g_pass == g_total ? 0 : 1;
}
