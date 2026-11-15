/* ENV: linux */
/* ELX-B02 elfinfo — 크로스 바이너리를 "실행하지 않고" 읽는 작은 ELF 리더 (32/64-bit, little-endian)
 *
 *   elfinfo FILE...                               class/data/type/machine/interp/needed/runpath/rpath/soname
 *   elfinfo --sysroot DIR [--libpath P] FILE      + 타깃 sysroot 안에서 interp/needed 를 찾는다 → RUNNABLE/MISSING
 *   elfinfo --selftest                            메모리에서 만든 ELF 조각으로 CHECK 테스트 (N/M passed)
 *   elfinfo --fuzz FILE                           잘리고 뒤집힌 입력 수천 개에도 죽지 않는지
 *
 * build: gcc -std=c11 -Wall -Wextra -Werror -O2 <starter|solution|practice>.c -o elfinfo
 * TODO: A1 elf_parse_header  A2 find_interp  B1 vaddr_to_off  B2 parse_dynamic  C sysroot_find
 */
#define _DEFAULT_SOURCE
#include <elf.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#define MAX_NEEDED 16
#define STR_MAX 256
#define NONE UINT64_MAX

enum { ELFI_OK = 0, ELFI_NOTELF = -1, ELFI_UNSUP = -2, ELFI_TRUNC = -3, ELFI_BAD = -4 };
static const char *const errname[] = { "ok", "not an ELF file", "unsupported (class/endianness)",
				       "truncated", "malformed" };

/* ---- 32/64 레이아웃: 필드 위치(바이트)를 표로. 구조체 캐스트 대신 이 표 + 바이트 읽기 ---- */
struct elf_layout {
	int cls;                                /* 32 | 64 */
	unsigned ehsize;                        /* sizeof(ElfN_Ehdr) */
	unsigned word;                          /* 주소/오프셋/d_tag/d_val 크기: 4 | 8 */
	unsigned phoff_at, flags_at, phentsize_at, phnum_at;
	unsigned phsize;                        /* sizeof(ElfN_Phdr) */
	unsigned p_offset_at, p_vaddr_at, p_filesz_at;  /* p_type 은 둘 다 0 */
	unsigned dynsize;                       /* sizeof(ElfN_Dyn) */
};
static const struct elf_layout L32 = { 32, 52, 4, 28, 36, 42, 44, 32, 4, 8, 16, 8 };
static const struct elf_layout L64 = { 64, 64, 8, 32, 48, 54, 56, 56, 8, 16, 32, 16 };

struct elf_info {
	const struct elf_layout *L;
	int cls;                                /* 32 | 64 */
	unsigned type, machine;                 /* e_type, e_machine */
	uint32_t flags;                         /* e_flags (ARM: EABI 버전, float ABI) */
	uint64_t phoff;
	unsigned phnum;
	char interp[STR_MAX];                   /* "" = PT_INTERP 없음 */
	int has_dynamic;
	int n_needed;
	char needed[MAX_NEEDED][STR_MAX];
	char runpath[STR_MAX], rpath[STR_MAX], soname[STR_MAX];
};

struct phdr { uint32_t type; uint64_t offset, vaddr, filesz; };

/* little-endian n바이트 (n ≤ 8). 정렬·호스트 엔디언과 무관하다 */
static uint64_t rd(const uint8_t *p, unsigned n)
{
	uint64_t v = 0;

	while (n--)
		v = v << 8 | p[n];
	return v;
}

/* [off, off+n) 이 [0, len) 안인가 — 덧셈 오버플로 없이 */
static int in_file(size_t len, uint64_t off, uint64_t n)
{
	return off <= len && n <= len - off;
}

static struct phdr get_phdr(const uint8_t *b, const struct elf_info *e, unsigned i)
{
	const struct elf_layout *L = e->L;
	const uint8_t *p = b + e->phoff + (uint64_t)i * L->phsize;
	struct phdr ph = { (uint32_t)rd(p, 4), rd(p + L->p_offset_at, L->word),
			   rd(p + L->p_vaddr_at, L->word), rd(p + L->p_filesz_at, L->word) };
	return ph;
}

/* dynstr[idx] 에서 C 문자열 하나를 dst 로. NUL 이 STRSZ 안에 있어야 한다 */
static int dyn_str(const uint8_t *b, size_t len, uint64_t stroff, uint64_t strsz,
		   uint64_t idx, char *dst, size_t cap)
{
	if (idx >= strsz || !in_file(len, stroff, strsz))
		return ELFI_BAD;
	const uint8_t *s = b + stroff + idx;
	const uint8_t *nul = memchr(s, 0, strsz - idx);
	if (!nul || (size_t)(nul - s) >= cap)
		return ELFI_BAD;
	memcpy(dst, s, (size_t)(nul - s) + 1);
	return ELFI_OK;
}

/* ===== Part A1: ELF 헤더 ===== */
static int elf_parse_header(const uint8_t *b, size_t len, struct elf_info *e)
{
	memset(e, 0, sizeof *e);
	if (len < EI_NIDENT || memcmp(b, ELFMAG, SELFMAG) != 0)
		return ELFI_NOTELF;
	if (b[EI_DATA] != ELFDATA2LSB)
		return ELFI_UNSUP;
	if (b[EI_CLASS] == ELFCLASS32)
		e->L = &L32;
	else if (b[EI_CLASS] == ELFCLASS64)
		e->L = &L64;
	else
		return ELFI_UNSUP;
	if (len < e->L->ehsize)
		return ELFI_TRUNC;
	e->cls = e->L->cls;
	e->type = (unsigned)rd(b + 16, 2);
	e->machine = (unsigned)rd(b + 18, 2);
	e->flags = (uint32_t)rd(b + e->L->flags_at, 4);
	e->phoff = rd(b + e->L->phoff_at, e->L->word);
	e->phnum = (unsigned)rd(b + e->L->phnum_at, 2);
	if (e->phnum && rd(b + e->L->phentsize_at, 2) != e->L->phsize)
		return ELFI_BAD;
	return ELFI_OK;
}

/* ===== Part A2: PT_INTERP ===== */
static int find_interp(const uint8_t *b, size_t len, struct elf_info *e)
{
	for (unsigned i = 0; i < e->phnum; i++) {
		struct phdr ph = get_phdr(b, e, i);
		if (ph.type != PT_INTERP)
			continue;
		if (ph.filesz < 2 || ph.filesz > sizeof e->interp)
			return ELFI_BAD;
		if (!in_file(len, ph.offset, ph.filesz))
			return ELFI_TRUNC;
		if (b[ph.offset + ph.filesz - 1] != '\0')  /* fs/binfmt_elf.c 도 같은 검사 */
			return ELFI_BAD;
		memcpy(e->interp, b + ph.offset, ph.filesz);
		return ELFI_OK;
	}
	return ELFI_OK;
}

/* ===== Part B1: 가상 주소 → 파일 오프셋 (PT_LOAD 로) ===== */
static int vaddr_to_off(const uint8_t *b, const struct elf_info *e, uint64_t vaddr, uint64_t *off)
{
	for (unsigned i = 0; i < e->phnum; i++) {
		struct phdr ph = get_phdr(b, e, i);
		if (ph.type == PT_LOAD && vaddr >= ph.vaddr && vaddr - ph.vaddr < ph.filesz) {
			*off = ph.offset + (vaddr - ph.vaddr);
			return 1;
		}
	}
	return 0;
}

/* ===== Part B2: PT_DYNAMIC → NEEDED / RUNPATH / RPATH / SONAME ===== */
static int parse_dynamic(const uint8_t *b, size_t len, struct elf_info *e)
{
	const struct elf_layout *L = e->L;
	struct phdr dyn = { 0, 0, 0, 0 };
	unsigned i;

	for (i = 0; i < e->phnum; i++) {
		dyn = get_phdr(b, e, i);
		if (dyn.type == PT_DYNAMIC)
			break;
	}
	if (i == e->phnum)
		return ELFI_OK;
	if (!in_file(len, dyn.offset, dyn.filesz))
		return ELFI_TRUNC;
	e->has_dynamic = 1;

	uint64_t strtab = NONE, strsz = 0, need[MAX_NEEDED];
	uint64_t runpath = NONE, rpath = NONE, soname = NONE;
	int nn = 0;
	for (uint64_t o = 0; o + L->dynsize <= dyn.filesz; o += L->dynsize) {
		const uint8_t *d = b + dyn.offset + o;
		uint64_t tag = rd(d, L->word), val = rd(d + L->word, L->word);
		if (tag == DT_NULL)
			break;
		if (tag == DT_STRTAB)
			strtab = val;
		else if (tag == DT_STRSZ)
			strsz = val;
		else if (tag == DT_RUNPATH)
			runpath = val;
		else if (tag == DT_RPATH)
			rpath = val;
		else if (tag == DT_SONAME)
			soname = val;
		else if (tag == DT_NEEDED) {
			if (nn == MAX_NEEDED)
				return ELFI_BAD;
			need[nn++] = val;
		}
	}
	if (!nn && runpath == NONE && rpath == NONE && soname == NONE)
		return ELFI_OK;
	uint64_t stroff;
	if (strtab == NONE || !vaddr_to_off(b, e, strtab, &stroff))
		return ELFI_BAD;
	for (int k = 0; k < nn; k++)
		if (dyn_str(b, len, stroff, strsz, need[k], e->needed[k], STR_MAX))
			return ELFI_BAD;
	e->n_needed = nn;
	if (runpath != NONE && dyn_str(b, len, stroff, strsz, runpath, e->runpath, STR_MAX))
		return ELFI_BAD;
	if (rpath != NONE && dyn_str(b, len, stroff, strsz, rpath, e->rpath, STR_MAX))
		return ELFI_BAD;
	if (soname != NONE && dyn_str(b, len, stroff, strsz, soname, e->soname, STR_MAX))
		return ELFI_BAD;
	return ELFI_OK;
}

static int elf_parse(const uint8_t *b, size_t len, struct elf_info *e)
{
	int rc = elf_parse_header(b, len, e);

	if (rc)
		return rc;
	if (!in_file(len, e->phoff, (uint64_t)e->phnum * e->L->phsize))
		return ELFI_TRUNC;
	rc = find_interp(b, len, e);
	return rc ? rc : parse_dynamic(b, len, e);
}

/* ===== Part C: sysroot 안에서 찾기 (ld.so 의 탐색 순서를 흉내) ===== */
static const char *multiarch(const struct elf_info *e)
{
	switch (e->machine) {
	case EM_AARCH64: return "aarch64-linux-gnu";
	case EM_ARM:     return (e->flags & EF_ARM_ABI_FLOAT_HARD) ? "arm-linux-gnueabihf"
								   : "arm-linux-gnueabi";
	case EM_X86_64:  return "x86_64-linux-gnu";
	case EM_RISCV:   return "riscv64-linux-gnu";
	default:         return "unknown";
	}
}

/* 후보 파일이 같은 class·machine 의 ELF 인가. ld.so 도 아니면 "incompatible" 로 건너뛴다 */
static int same_target(const char *path, const struct elf_info *want)
{
	uint8_t hdr[64];
	struct elf_info c;
	FILE *f = fopen(path, "rb");
	if (!f)
		return 0;
	size_t n = fread(hdr, 1, sizeof hdr, f);
	fclose(f);
	return elf_parse_header(hdr, n, &c) == ELFI_OK && c.cls == want->cls &&
	       c.machine == want->machine;
}

/* "dirA:$ORIGIN/lib" 목록에서 name 찾기. $ORIGIN = 바이너리 디렉터리, 나머지 절대경로는 sysroot 아래 */
static int search_list(const char *list, const char *sysroot, const char *origin, const char *name,
		       const struct elf_info *want, char *out, size_t cap)
{
	char dir[STR_MAX * 2];

	while (list && *list) {
		size_t n = strcspn(list, ":");
		if (n >= 7 && strncmp(list, "$ORIGIN", 7) == 0)
			snprintf(dir, sizeof dir, "%s%.*s", origin, (int)(n - 7), list + 7);
		else
			snprintf(dir, sizeof dir, "%s%.*s", sysroot, (int)n, list);
		snprintf(out, cap, "%s/%s", dir, name);
		if (same_target(out, want))
			return 1;
		list += n + (list[n] == ':');
	}
	return 0;
}

/* name 을 ld.so 순서대로 찾는다: RPATH(RUNPATH 없을 때) → LD_LIBRARY_PATH → RUNPATH → 기본 디렉터리.
 * 찾으면 경로를 out 에, 어디서 찾았는지를 *via 에 넣고 1 */
static int sysroot_find(const struct elf_info *e, const char *sysroot, const char *libpath,
			const char *origin, const char *name, char *out, size_t cap, const char **via)
{
	char defaults[STR_MAX * 2];
	const char *ma = multiarch(e);

	if (strchr(name, '/')) {
		snprintf(out, cap, "%s%s", name[0] == '/' ? sysroot : "", name);
		*via = "path";
		return same_target(out, e);
	}
	if (!e->runpath[0] && e->rpath[0] && search_list(e->rpath, sysroot, origin, name, e, out, cap)) {
		*via = "rpath";
		return 1;
	}
	if (libpath && search_list(libpath, sysroot, origin, name, e, out, cap)) {
		*via = "libpath";
		return 1;
	}
	if (e->runpath[0] && search_list(e->runpath, sysroot, origin, name, e, out, cap)) {
		*via = "runpath";
		return 1;
	}
	snprintf(defaults, sizeof defaults, "/lib/%s:/usr/lib/%s:/lib:/usr/lib", ma, ma);
	*via = "default";
	return search_list(defaults, sysroot, origin, name, e, out, cap);
}

/* ---------------- 출력 ---------------- */
static const char *machine_name(unsigned m)
{
	switch (m) {
	case EM_AARCH64: return "AArch64";
	case EM_ARM:     return "ARM";
	case EM_X86_64:  return "Advanced Micro Devices X86-64";
	case EM_RISCV:   return "RISC-V";
	case EM_386:     return "Intel 80386";
	default:         return "unknown";
	}
}

static const char *type_name(unsigned t)
{
	switch (t) {
	case ET_REL:  return "REL";
	case ET_EXEC: return "EXEC";
	case ET_DYN:  return "DYN";
	case ET_CORE: return "CORE";
	default:      return "NONE";
	}
}

static void print_info(const struct elf_info *e)
{
	printf("class: ELF%d\ndata: LE\ntype: %s\nmachine: %s (%u)\n", e->cls, type_name(e->type),
	       machine_name(e->machine), e->machine);
	if (e->interp[0])
		printf("interp: %s\n", e->interp);
	for (int k = 0; k < e->n_needed; k++)
		printf("needed: %s\n", e->needed[k]);
	if (e->runpath[0])
		printf("runpath: %s\n", e->runpath);
	if (e->rpath[0])
		printf("rpath: %s\n", e->rpath);
	if (e->soname[0])
		printf("soname: %s\n", e->soname);
}

/* --sysroot 보고서. 반환: 못 찾은 개수 */
static int print_sysroot(const struct elf_info *e, const char *file, const char *sysroot,
			 const char *libpath)
{
	char origin[STR_MAX * 2], path[STR_MAX * 4], sr[STR_MAX * 2];
	const char *via = "";
	int missing = 0;

	snprintf(sr, sizeof sr, "%s", sysroot);
	if (strlen(sr) > 0 && sr[strlen(sr) - 1] == '/')
		sr[strlen(sr) - 1] = '\0';                      /* "/" → "" */
	char *real = realpath(file, NULL);
	snprintf(origin, sizeof origin, "%s", real ? real : file);
	free(real);
	char *slash = strrchr(origin, '/');
	if (slash)
		*slash = '\0';
	else
		snprintf(origin, sizeof origin, ".");

	if (e->interp[0]) {
		snprintf(path, sizeof path, "%s%s", sr, e->interp);
		int ok = same_target(path, e);
		printf("interp %s -> %s\n", e->interp, ok ? path : "MISSING");
		missing += !ok;
	}
	for (int k = 0; k < e->n_needed; k++) {
		int ok = sysroot_find(e, sr, libpath, origin, e->needed[k], path, sizeof path, &via);
		printf("needed %s -> %s\n", e->needed[k], ok ? path : "MISSING");
		missing += !ok;
	}
	if (missing)
		printf("verdict: MISSING %d\n", missing);
	else
		printf("verdict: RUNNABLE\n");
	return missing;
}

static uint8_t *read_file(const char *path, size_t *len)
{
	FILE *f = fopen(path, "rb");
	uint8_t *buf = NULL;
	size_t cap = 0, n = 0, r;

	if (!f)
		return NULL;
	do {
		if (n == cap) {
			uint8_t *nb = realloc(buf, cap ? cap * 2 : 65536);
			if (!nb) {
				free(buf);
				fclose(f);
				return NULL;
			}
			buf = nb;
			cap = cap ? cap * 2 : 65536;
		}
		r = fread(buf + n, 1, cap - n, f);
		n += r;
	} while (r > 0);
	fclose(f);
	*len = n;
	return buf;
}

/* ---------------- 테스트 하네스 ---------------- */
static int g_pass, g_total;
#define CHECK(expr) do { g_total++; if (expr) { g_pass++; printf("  PASS %s\n", #expr); } \
			 else printf("  FAIL %s  (line %d)\n", #expr, __LINE__); } while (0)

static void wr(uint8_t *p, unsigned n, uint64_t v)
{
	for (unsigned i = 0; i < n; i++, v >>= 8)
		p[i] = (uint8_t)v;
}

/* 메모리 안의 최소 ELF: [ehdr][phdr: LOAD, INTERP, DYNAMIC] @0x100 interp @0x140 dynstr @0x200 dynamic */
#define IMG 0x300
static const char k_str[] = "\0libfoo.so.2\0libc.so.6\0$ORIGIN/../lib\0libself.so.9";
enum { S_FOO = 1, S_LIBC = 13, S_RUNPATH = 23, S_SONAME = 38 };

static size_t mk_elf(uint8_t *b, int cls, uint64_t base, unsigned machine)
{
	const struct elf_layout *L = cls == 64 ? &L64 : &L32;
	unsigned w = L->word, ph = L->ehsize;
	uint64_t dyn[][2] = { { DT_NEEDED, S_FOO }, { DT_NEEDED, S_LIBC }, { DT_RUNPATH, S_RUNPATH },
			      { DT_SONAME, S_SONAME }, { DT_STRTAB, base + 0x140 },
			      { DT_STRSZ, sizeof k_str }, { DT_NULL, 0 } };
	uint64_t seg[3][4] = { { PT_LOAD, 0, base, IMG }, { PT_INTERP, 0x100, base + 0x100, 18 },
			       { PT_DYNAMIC, 0x200, base + 0x200, 7 * (uint64_t)L->dynsize } };

	memset(b, 0, IMG);
	memcpy(b, ELFMAG, SELFMAG);
	b[EI_CLASS] = cls == 64 ? ELFCLASS64 : ELFCLASS32;
	b[EI_DATA] = ELFDATA2LSB;
	b[EI_VERSION] = EV_CURRENT;
	wr(b + 16, 2, ET_DYN);
	wr(b + 18, 2, machine);
	wr(b + L->phoff_at, w, ph);
	wr(b + L->phentsize_at, 2, L->phsize);
	wr(b + L->phnum_at, 2, 3);
	for (int i = 0; i < 3; i++) {
		uint8_t *p = b + ph + i * L->phsize;
		wr(p, 4, seg[i][0]);
		wr(p + L->p_offset_at, w, seg[i][1]);
		wr(p + L->p_vaddr_at, w, seg[i][2]);
		wr(p + L->p_filesz_at, w, seg[i][3]);
	}
	memcpy(b + 0x100, "/lib/ld-test.so.1", 18);
	memcpy(b + 0x140, k_str, sizeof k_str);
	for (int i = 0; i < 7; i++) {
		wr(b + 0x200 + i * L->dynsize, w, dyn[i][0]);
		wr(b + 0x200 + i * L->dynsize + w, w, dyn[i][1]);
	}
	return IMG;
}

static int selftest(void)
{
	uint8_t b[IMG], m[IMG];
	struct elf_info e;
	size_t n;

	puts("[layout: 표가 <elf.h> 구조체와 같은가]");
	CHECK(L64.phoff_at == offsetof(Elf64_Ehdr, e_phoff) && L32.phoff_at == offsetof(Elf32_Ehdr, e_phoff));
	CHECK(L64.flags_at == offsetof(Elf64_Ehdr, e_flags) && L32.phnum_at == offsetof(Elf32_Ehdr, e_phnum));
	CHECK(L64.p_offset_at == offsetof(Elf64_Phdr, p_offset) && L32.p_offset_at == offsetof(Elf32_Phdr, p_offset));
	CHECK(L64.phsize == sizeof(Elf64_Phdr) && L32.phsize == sizeof(Elf32_Phdr));
	CHECK(L64.dynsize == sizeof(Elf64_Dyn) && L32.dynsize == sizeof(Elf32_Dyn));

	puts("[Part A: 헤더 + PT_INTERP]");
	n = mk_elf(b, 64, 0x400000, EM_AARCH64);
	CHECK(elf_parse(b, n, &e) == ELFI_OK);
	CHECK(e.cls == 64 && e.type == ET_DYN && e.machine == EM_AARCH64);
	CHECK(strcmp(e.interp, "/lib/ld-test.so.1") == 0);
	n = mk_elf(b, 32, 0x10000, EM_ARM);
	CHECK(elf_parse(b, n, &e) == ELFI_OK && e.cls == 32 && e.machine == EM_ARM);
	CHECK(strcmp(e.interp, "/lib/ld-test.so.1") == 0);

	puts("[Part B: 동적 섹션 — STRTAB 은 주소다]");
	n = mk_elf(b, 64, 0x400000, EM_AARCH64);
	CHECK(elf_parse(b, n, &e) == ELFI_OK && e.n_needed == 2);
	CHECK(strcmp(e.needed[0], "libfoo.so.2") == 0 && strcmp(e.needed[1], "libc.so.6") == 0);
	CHECK(strcmp(e.runpath, "$ORIGIN/../lib") == 0 && strcmp(e.soname, "libself.so.9") == 0);
	n = mk_elf(b, 32, 0x10000, EM_ARM);
	CHECK(elf_parse(b, n, &e) == ELFI_OK && e.n_needed == 2 && strcmp(e.needed[1], "libc.so.6") == 0);
	n = mk_elf(b, 64, 0, EM_X86_64);                    /* base 0: 주소 == 오프셋인 경우도 */
	CHECK(elf_parse(b, n, &e) == ELFI_OK && strcmp(e.soname, "libself.so.9") == 0);

	puts("[Part B: 망가진 입력 → 에러 코드, 절대 크래시 없음]");
	n = mk_elf(b, 64, 0x400000, EM_AARCH64);
	memcpy(m, b, n); m[1] = 'X';
	CHECK(elf_parse(m, n, &e) == ELFI_NOTELF);
	memcpy(m, b, n); m[EI_DATA] = ELFDATA2MSB;
	CHECK(elf_parse(m, n, &e) == ELFI_UNSUP);
	CHECK(elf_parse(b, 40, &e) == ELFI_TRUNC);          /* ELF64 헤더(64B)보다 짧다 */
	CHECK(elf_parse(b, 3, &e) == ELFI_NOTELF);
	memcpy(m, b, n); wr(m + L64.phoff_at, 8, 0xfffffffffffffff0ull);
	CHECK(elf_parse(m, n, &e) == ELFI_TRUNC);           /* phoff + 크기 오버플로 */
	memcpy(m, b, n); wr(m + L64.phentsize_at, 2, 32);
	CHECK(elf_parse(m, n, &e) == ELFI_BAD);
	memcpy(m, b, n); m[0x100 + 17] = 'X';               /* interp 끝 NUL 없음 */
	CHECK(elf_parse(m, n, &e) == ELFI_BAD);
	memcpy(m, b, n); wr(m + 0x200 + 8, 8, 4096);        /* NEEDED 인덱스 ≥ STRSZ */
	CHECK(elf_parse(m, n, &e) == ELFI_BAD);
	memcpy(m, b, n); wr(m + 0x200 + 4 * 16 + 8, 8, 0x140);  /* STRTAB=0x140: 어느 LOAD 에도 없음 */
	CHECK(elf_parse(m, n, &e) == ELFI_BAD);
	memcpy(m, b, n); m[0x140 + sizeof k_str - 1] = 'X'; /* 마지막 문자열이 STRSZ 안에서 안 끝남 */
	CHECK(elf_parse(m, n, &e) == ELFI_BAD);
	memcpy(m, b, n); wr(m + 0x200 + 6 * 16, 8, DT_DEBUG);   /* DT_NULL 없음 → 세그먼트 끝에서 멈춤 */
	CHECK(elf_parse(m, n, &e) == ELFI_OK && e.n_needed == 2);
	memcpy(m, b, n); wr(m + L64.phnum_at, 2, 1);        /* LOAD 하나뿐 = 정적 */
	CHECK(elf_parse(m, n, &e) == ELFI_OK && e.interp[0] == '\0' && !e.has_dynamic);

	printf("%d/%d passed\n", g_pass, g_total);
	return g_pass == g_total ? 0 : 1;
}

/* 잘린 입력 + 바이트 뒤집기. ASan 빌드로 돌리면 범위 밖 읽기가 곧바로 죽는다 */
static int fuzz(const char *file)
{
	size_t len, cases = 0;
	uint8_t *b = read_file(file, &len), *m;
	struct elf_info e;
	int hist[5] = { 0 };
	uint32_t x = 12345;

	if (!b || !(m = malloc(len ? len : 1))) {
		free(b);
		return 1;
	}
	for (size_t cut = 0; cut <= len; cut += 1 + cut / 8, cases++)
		hist[-elf_parse(b, cut, &e)]++;
	for (int i = 0; i < 3000; i++, cases++) {
		memcpy(m, b, len);
		for (int k = 0; k < 4; k++) {
			x = x * 1103515245u + 12345u;
			size_t at = (x >> 8) % (len < 4096 ? len : 4096);
			m[at] ^= (uint8_t)(1u << (x & 7));
		}
		hist[-elf_parse(m, len, &e)]++;
	}
	printf("fuzz %s: %zu cases, ok=%d notelf=%d unsup=%d trunc=%d bad=%d\n", file, cases,
	       hist[0], hist[1], hist[2], hist[3], hist[4]);
	free(b);
	free(m);
	return 0;
}

int main(int argc, char **argv)
{
	const char *sysroot = NULL, *libpath = NULL;
	int i = 1, rc = 0;

	if (argc == 2 && strcmp(argv[1], "--selftest") == 0)
		return selftest();
	if (argc == 3 && strcmp(argv[1], "--fuzz") == 0)
		return fuzz(argv[2]);
	for (; i < argc; i++) {
		if (strcmp(argv[i], "--sysroot") == 0 && i + 1 < argc)
			sysroot = argv[++i];
		else if (strcmp(argv[i], "--libpath") == 0 && i + 1 < argc)
			libpath = argv[++i];
		else
			break;
	}
	if (i >= argc) {
		fprintf(stderr, "usage: elfinfo [--sysroot DIR [--libpath P]] FILE... | --selftest | --fuzz FILE\n");
		return 2;
	}
	for (int f = i; f < argc; f++) {
		size_t len;
		struct elf_info e;
		uint8_t *b = read_file(argv[f], &len);
		int r = b ? elf_parse(b, len, &e) : ELFI_NOTELF;

		if (argc - i > 1)
			printf("== %s\n", argv[f]);
		if (r) {
			fprintf(stderr, "elfinfo: %s: %s\n", argv[f], b ? errname[-r] : "cannot read");
			rc = 1;
		} else {
			print_info(&e);
			if (sysroot && print_sysroot(&e, argv[f], sysroot, libpath))
				rc = 1;
		}
		free(b);
	}
	return rc;
}
