/* ENV: linux */
/* PCIE-B06 lspci·setpci 실전 해석: -tv, -vvv, -xxxx
 *
 * sysfs(/sys/bus/pci/devices/<DDDD:BB:DD.F>/) 의 config · resource · vendor · device · class · driver 를
 * 직접 읽어 BDF, VID:DID, class, BAR(주소·크기·종류), capability 리스트, "Kernel driver in use" 를 만들고,
 * 같은 장치의 lspci -vvv / lspci -xxx / lspci -n / setpci 출력과 값이 같은지 대조한다.
 *
 * Part A: lspci -xxx 텍스트 → 바이트 (parse_lspci_x), sysfs resource 파싱 (parse_resource),
 *         lspci 식 크기 문자열 (fmt_size), legacy capability 순회 (walk_caps), sysfs 디렉터리 읽기 (read_dev)
 * Part B: 잘린/깨진 텍스트, 루프·나쁜 포인터, 64바이트(비root) config, 가짜 sysfs 트리(드라이버 없음·config 없음·stale)
 * Part C: lspci -vvv 텍스트 파서 (parse_lspci_vvv) + 이 VM 의 모든 PCI 함수에 대해 lspci/setpci 와 대조,
 *         비root(nobody) 로 읽으면 config 가 64바이트만 보인다는 것까지 실측.
 *
 * 상수 이름·값은 Linux include/uapi/linux/pci_regs.h, include/linux/ioport.h 와 같다 (헤더 없이 빌드되도록 직접 #define).
 * config 는 리틀엔디언 바이트 배열, 읽기는 cfg_rd8/16/32 로만 (비정렬 포인터 캐스트 금지). malloc 없음 — 정적/호출자 버퍼.
 * 실행: VM 안에서 root 로 (check.sh 가 sudo 로 돌린다). Part C 는 root 가 아니면 FAIL.
 * 빌드: gcc -std=c11 -Wall -Wextra -Werror -O2 solution.c -o solution && sudo ./solution
 */
#define _GNU_SOURCE
#include <dirent.h>
#include <grp.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

/* ---- pci_regs.h 와 같은 이름·값 ---- */
#define PCI_CFG_SPACE_SIZE       256
#define PCI_CFG_SPACE_EXP_SIZE   4096
#define PCI_STD_HEADER_SIZEOF    64
#define PCI_STD_NUM_BARS         6
#define PCI_VENDOR_ID            0x00
#define PCI_DEVICE_ID            0x02
#define PCI_COMMAND              0x04
#define  PCI_COMMAND_IO          0x1
#define  PCI_COMMAND_MEMORY      0x2
#define  PCI_COMMAND_MASTER      0x4
#define  PCI_COMMAND_INTX_DISABLE 0x400
#define PCI_STATUS               0x06
#define  PCI_STATUS_CAP_LIST     0x10
#define PCI_CLASS_REVISION       0x08
#define PCI_HEADER_TYPE          0x0e
#define  PCI_HEADER_TYPE_MASK    0x7f
#define PCI_BASE_ADDRESS_0       0x10
#define  PCI_BASE_ADDRESS_SPACE_IO    0x01
#define  PCI_BASE_ADDRESS_MEM_TYPE_64 0x04
#define PCI_CAPABILITY_LIST      0x34
#define PCI_CAP_LIST_ID          0
#define PCI_CAP_LIST_NEXT        1
#define  PCI_CAP_ID_PM           0x01
#define  PCI_CAP_ID_MSI          0x05
#define  PCI_CAP_ID_VNDR         0x09
#define  PCI_CAP_ID_SSVID        0x0D
#define  PCI_CAP_ID_EXP          0x10
#define  PCI_CAP_ID_MSIX         0x11
/* include/linux/ioport.h — sysfs resource 파일 3번째 열(flags) */
#define IORESOURCE_IO            0x00000100
#define IORESOURCE_MEM           0x00000200
#define IORESOURCE_PREFETCH      0x00002000
#define IORESOURCE_MEM_64        0x00100000

enum { E_OK = 0, E_FORMAT = -1, E_TRUNC = -2, E_ORDER = -3, E_CAP = -4, E_LOOP = -5, E_BADPTR = -6, E_IO = -7 };

#define MAX_CAPS  48           /* (0x100 - 0x40) / 4: legacy 영역에 4바이트 정렬로 들어갈 수 있는 최대 개수 */
#define MAX_RES   24
#define MAX_DEVS  64
#define TXT_MAX   65536
#define SYSFS_PCI "/sys/bus/pci/devices"

typedef struct { int n, err; uint8_t off[MAX_CAPS], id[MAX_CAPS]; } cap_list;
typedef struct { uint64_t start, end, flags; } pci_res;
typedef struct {
    char     bdf[16];                       /* "0000:00:06.0" */
    uint8_t  cfg[PCI_CFG_SPACE_EXP_SIZE];   /* sysfs config 그대로 (root 256/4096B, 비root 64B) */
    size_t   cfg_len;
    uint32_t s_vendor, s_device, s_class;   /* sysfs 텍스트 파일 vendor/device/class 값 */
    pci_res  res[MAX_RES];
    int      nres;
    char     driver[64];                    /* driver 심볼릭 링크의 basename, 없으면 "" */
    cap_list caps;
} pci_dev;

typedef struct { int idx; uint64_t addr; char size[16]; int is_io, is64, pref, disabled; } lspci_region;
typedef struct {
    char driver[64];                        /* "Kernel driver in use: X" 의 X, 없으면 "" */
    int  caps_denied;                       /* "Capabilities: <access denied>" (비root) */
    int  ncap; uint16_t cap_off[MAX_CAPS];  /* "Capabilities: [40] ..." 의 40, 나온 순서대로 (ext "[100 v2]" 포함) */
    int  nreg; lspci_region reg[PCI_STD_NUM_BARS];
} lspci_view;

/* ---------------- 주어진 도우미 ---------------- */

static uint8_t  cfg_rd8(const uint8_t *c, uint16_t off)  { return c[off]; }
static uint16_t cfg_rd16(const uint8_t *c, uint16_t off) { return (uint16_t)(c[off] | c[off + 1] << 8); }
static uint32_t cfg_rd32(const uint8_t *c, uint16_t off) {
    return (uint32_t)c[off] | (uint32_t)c[off + 1] << 8 | (uint32_t)c[off + 2] << 16 | (uint32_t)c[off + 3] << 24;
}

static int hexval(int c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}
/* s[*i..n) 에서 16진수 토큰 하나 (앞의 "0x" 허용). 숫자가 없거나 16자리를 넘으면 -1 */
static int hex_tok(const char *s, size_t n, size_t *i, uint64_t *v) {
    size_t k = *i, nd = 0;
    uint64_t x = 0;
    if (k + 1 < n && s[k] == '0' && (s[k + 1] == 'x' || s[k + 1] == 'X')) k += 2;
    while (k < n && hexval(s[k]) >= 0) {
        if (++nd > 16) return -1;
        x = x << 4 | (uint64_t)hexval(s[k++]);
    }
    if (nd == 0) return -1;
    *i = k; *v = x;
    return 0;
}
/* text[0..len) 의 다음 줄 → (*ln, *n). 끝의 '\r'·공백 제거. 줄이 없으면 0 */
static int next_line(const char *t, size_t len, size_t *pos, const char **ln, size_t *n) {
    if (*pos >= len) return 0;
    size_t s = *pos, e = s;
    while (e < len && t[e] != '\n') e++;
    *pos = e < len ? e + 1 : e;
    *ln = t + s; *n = e - s;
    while (*n > 0 && (t[s + *n - 1] == '\r' || t[s + *n - 1] == ' ')) (*n)--;
    return 1;
}
static int starts(const char *s, size_t n, const char *p) { size_t k = strlen(p); return n >= k && memcmp(s, p, k) == 0; }
static long find_in(const char *s, size_t n, const char *p) {
    size_t k = strlen(p);
    for (size_t i = 0; i + k <= n; i++) if (memcmp(s + i, p, k) == 0) return (long)i;
    return -1;
}

/* 파일 전체(최대 cap 바이트)를 읽는다. sysfs 는 짧은 read 를 줄 수 있어 EOF 까지 반복 */
static int read_file(const char *path, uint8_t *buf, size_t cap, size_t *n) {
    FILE *f = fopen(path, "rb");
    if (!f) return E_IO;
    size_t got = 0, k;
    while (got < cap && (k = fread(buf + got, 1, cap - got, f)) > 0) got += k;
    int bad = ferror(f);
    fclose(f);
    if (bad) return E_IO;
    *n = got;
    return 0;
}
static int read_hex_file(const char *sysroot, const char *bdf, const char *name, uint32_t *v) {
    char path[256], txt[64];
    size_t n, i = 0;
    uint64_t x;
    snprintf(path, sizeof path, "%s/%s/%s", sysroot, bdf, name);
    if (read_file(path, (uint8_t *)txt, sizeof txt, &n) < 0 || hex_tok(txt, n, &i, &x) < 0 || x > 0xFFFFFFFFu) return E_IO;
    *v = (uint32_t)x;
    return 0;
}
static uint64_t res_size(const pci_res *r) { return (r->start || r->end) ? r->end - r->start + 1 : 0; }

static const char *cap_name(uint8_t id) {
    switch (id) {
    case PCI_CAP_ID_PM:    return "Power Management";
    case PCI_CAP_ID_MSI:   return "MSI";
    case PCI_CAP_ID_VNDR:  return "Vendor Specific";
    case PCI_CAP_ID_SSVID: return "Subsystem ID (bridge)";
    case PCI_CAP_ID_EXP:   return "PCI Express";
    case PCI_CAP_ID_MSIX:  return "MSI-X";
    default:               return "?";
    }
}

/* ---------------- Part A ---------------- */

/* A1: lspci -xxx / -xxxx 텍스트 → cfg[]. 반환: 바이트 수(64/256/4096) 또는 E_*.
 *   데이터 줄 = "<16진 2~3자리>: " + 16 × " hh". 오프셋은 0 부터 16씩 (아니면 E_ORDER).
 *   그 밖의 비어 있지 않은 줄은 데이터 앞의 장치 머리줄("00:06.0 Mass storage ...")로만 허용 — 데이터 뒤면 E_FORMAT.
 *   데이터 뒤 빈 줄 = 장치 끝. 바이트가 16개가 아니거나 16진이 아니면 E_FORMAT, cap 을 넘으면 E_CAP,
 *   합계가 64/256/4096 이 아니면 E_TRUNC. */
static int parse_lspci_x(const char *t, size_t len, uint8_t *cfg, size_t cap) {
    size_t pos = 0, got = 0, n;
    const char *ln;
    int in_data = 0;
    while (next_line(t, len, &pos, &ln, &n)) {
        if (n == 0) { if (in_data) break; continue; }
        size_t j = 0, i = 0;
        uint64_t off;
        while (j < n && hexval(ln[j]) >= 0) j++;
        int is_data = j >= 2 && j <= 3 && j + 1 < n && ln[j] == ':' && ln[j + 1] == ' ';
        if (!is_data) { if (in_data) return E_FORMAT; continue; }
        hex_tok(ln, j, &i, &off);
        if (off != got) return E_ORDER;
        if (got + 16 > cap) return E_CAP;
        i = j + 1;
        for (int k = 0; k < 16; k++, i += 3) {
            if (i + 3 > n || ln[i] != ' ' || hexval(ln[i + 1]) < 0 || hexval(ln[i + 2]) < 0) return E_FORMAT;
            cfg[got + (size_t)k] = (uint8_t)(hexval(ln[i + 1]) << 4 | hexval(ln[i + 2]));
        }
        if (i != n) return E_FORMAT;
        got += 16;
        in_data = 1;
    }
    if (got != PCI_STD_HEADER_SIZEOF && got != PCI_CFG_SPACE_SIZE && got != PCI_CFG_SPACE_EXP_SIZE) return E_TRUNC;
    return (int)got;
}

/* A2: sysfs resource 텍스트("0x%016llx 0x%016llx 0x%016llx" 줄들) → res[]. 반환: 줄 수 또는 E_*.
 *   줄마다 16진 토큰 정확히 3개 (아니면 E_FORMAT), 비어 있지 않은 줄인데 end < start 면 E_FORMAT,
 *   max 줄 초과 E_CAP, BAR 6줄보다 적으면 E_TRUNC. */
static int parse_resource(const char *t, size_t len, pci_res *res, int max) {
    size_t pos = 0, n;
    const char *ln;
    int cnt = 0;
    while (next_line(t, len, &pos, &ln, &n)) {
        if (n == 0) continue;
        if (cnt >= max) return E_CAP;
        uint64_t v[3];
        size_t i = 0;
        for (int k = 0; k < 3; k++) {
            if (k > 0 && (i >= n || ln[i] != ' ')) return E_FORMAT;
            while (i < n && ln[i] == ' ') i++;
            if (hex_tok(ln, n, &i, &v[k]) < 0) return E_FORMAT;
        }
        if (i != n) return E_FORMAT;
        if ((v[0] || v[1]) && v[1] < v[0]) return E_FORMAT;
        res[cnt].start = v[0]; res[cnt].end = v[1]; res[cnt].flags = v[2];
        cnt++;
    }
    if (cnt < PCI_STD_NUM_BARS) return E_TRUNC;
    return cnt;
}

/* A3: lspci 식 크기 문자열: 1024 로 나누어떨어지는 동안 K→M→G→T. 65536 → "64K", 64 → "64", 0 → "" */
static void fmt_size(uint64_t sz, char *out, size_t cap) {
    static const char *suf[] = { "", "K", "M", "G", "T" };
    unsigned i = 0;
    if (cap == 0) return;
    if (sz == 0) { out[0] = '\0'; return; }
    while (i < 4 && sz % 1024 == 0) { sz /= 1024; i++; }
    snprintf(out, cap, "%llu%s", (unsigned long long)sz, suf[i]);
}

/* A4: legacy capability 리스트 순회. 반환: 개수 또는 E_*, cl->err 에도 기록.
 *   Status.CapList=0 이면 0개. 포인터 = cfg[0x34], 다음 = cfg[p+1], 하위 2비트는 마스크(&~3).
 *   p < 0x40 → E_BADPTR, p+2 > len → E_TRUNC (비root 64B 읽기!), 이미 본 p → E_LOOP. 멈출 때까지 찾은 것은 남긴다. */
static int walk_caps(const uint8_t *cfg, size_t len, cap_list *cl) {
    uint8_t seen[256] = { 0 };
    cl->n = 0;
    cl->err = E_OK;
    if (len < PCI_STD_HEADER_SIZEOF) return cl->err = E_TRUNC;
    if (!(cfg_rd16(cfg, PCI_STATUS) & PCI_STATUS_CAP_LIST)) return 0;
    unsigned p = cfg_rd8(cfg, PCI_CAPABILITY_LIST) & ~3u;
    while (p != 0) {
        if (p < PCI_STD_HEADER_SIZEOF) { cl->err = E_BADPTR; break; }
        if (p + 2 > len) { cl->err = E_TRUNC; break; }
        if (seen[p] || cl->n >= MAX_CAPS) { cl->err = E_LOOP; break; }
        seen[p] = 1;
        cl->off[cl->n] = (uint8_t)p;
        cl->id[cl->n] = cfg_rd8(cfg, (uint16_t)(p + PCI_CAP_LIST_ID));
        cl->n++;
        p = cfg_rd8(cfg, (uint16_t)(p + PCI_CAP_LIST_NEXT)) & ~3u;
    }
    return cl->err ? cl->err : cl->n;
}

/* A5: sysroot/bdf/ 에서 config(최대 4KB), vendor/device/class, resource, driver 링크를 읽어 d 를 채운다.
 *   config/vendor/device/class/resource 중 하나라도 못 읽으면 E_IO (resource 파싱 오류는 그 코드).
 *   driver 링크가 없으면 driver = "" (드라이버 미바인딩은 오류가 아니다). caps 는 walk_caps 결과(에러 포함)를 담는다. */
static int read_dev(const char *sysroot, const char *bdf, pci_dev *d) {
    char path[256], txt[4096], link[256];
    size_t n;
    memset(d, 0, sizeof *d);
    snprintf(d->bdf, sizeof d->bdf, "%s", bdf);
    snprintf(path, sizeof path, "%s/%s/config", sysroot, bdf);
    if (read_file(path, d->cfg, sizeof d->cfg, &n) < 0) return E_IO;
    d->cfg_len = n;
    if (read_hex_file(sysroot, bdf, "vendor", &d->s_vendor) < 0 ||
        read_hex_file(sysroot, bdf, "device", &d->s_device) < 0 ||
        read_hex_file(sysroot, bdf, "class", &d->s_class) < 0) return E_IO;
    snprintf(path, sizeof path, "%s/%s/resource", sysroot, bdf);
    if (read_file(path, (uint8_t *)txt, sizeof txt, &n) < 0) return E_IO;
    int r = parse_resource(txt, n, d->res, MAX_RES);
    if (r < 0) return r;
    d->nres = r;
    snprintf(path, sizeof path, "%s/%s/driver", sysroot, bdf);
    ssize_t k = readlink(path, link, sizeof link - 1);
    if (k > 0) {
        link[k] = '\0';
        const char *base = strrchr(link, '/');
        snprintf(d->driver, sizeof d->driver, "%.63s", base ? base + 1 : link);
    }
    walk_caps(d->cfg, d->cfg_len, &d->caps);
    return 0;
}

/* ---------------- Part C ---------------- */

/* C1: lspci -vvv 텍스트(한 장치) → v. 반환 0 또는 E_FORMAT / E_CAP.
 *   "\tKernel driver in use: X"  → driver
 *   "\tCapabilities: <access denied>" → caps_denied = 1
 *   "\tCapabilities: [40] ..." / "[100 v2]" → cap_off[] 에 순서대로
 *   "\tRegion N: Memory at ADDR (64-bit, non-prefetchable) [disabled] [size=64K]"
 *   "\tRegion N: I/O ports at ADDR [size=32]" → reg[] (idx, addr, size 문자열, is_io, is64, pref, disabled)
 *   Region 줄인데 "Memory at"/"I/O ports at" + 16진 주소가 아니면 E_FORMAT. */
static int parse_lspci_vvv(const char *t, size_t len, lspci_view *v) {
    size_t pos = 0, n;
    const char *ln;
    memset(v, 0, sizeof *v);
    while (next_line(t, len, &pos, &ln, &n)) {
        if (starts(ln, n, "\tKernel driver in use: ")) {
            size_t k = strlen("\tKernel driver in use: "), m = n - k;
            if (m >= sizeof v->driver) m = sizeof v->driver - 1;
            memcpy(v->driver, ln + k, m);
            v->driver[m] = '\0';
        } else if (starts(ln, n, "\tCapabilities: <access denied>")) {
            v->caps_denied = 1;
        } else if (starts(ln, n, "\tCapabilities: [")) {
            size_t i = strlen("\tCapabilities: [");
            uint64_t off;
            if (hex_tok(ln, n, &i, &off) < 0 || off >= PCI_CFG_SPACE_EXP_SIZE) return E_FORMAT;
            if (v->ncap >= MAX_CAPS) return E_CAP;
            v->cap_off[v->ncap++] = (uint16_t)off;
        } else if (starts(ln, n, "\tRegion ")) {
            size_t i = strlen("\tRegion ");
            int idx = 0, nd = 0;
            while (i < n && ln[i] >= '0' && ln[i] <= '9') { idx = idx * 10 + (ln[i++] - '0'); nd++; }
            if (nd == 0 || idx >= PCI_STD_NUM_BARS || !starts(ln + i, n - i, ": ")) return E_FORMAT;
            i += 2;
            if (v->nreg >= PCI_STD_NUM_BARS) return E_CAP;
            lspci_region *r = &v->reg[v->nreg];
            memset(r, 0, sizeof *r);
            r->idx = idx;
            if (starts(ln + i, n - i, "Memory at ")) i += strlen("Memory at ");
            else if (starts(ln + i, n - i, "I/O ports at ")) { i += strlen("I/O ports at "); r->is_io = 1; }
            else return E_FORMAT;
            if (hex_tok(ln, n, &i, &r->addr) < 0) return E_FORMAT;
            r->is64 = find_in(ln, n, "(64-bit") >= 0;
            r->pref = find_in(ln, n, ", prefetchable)") >= 0;
            r->disabled = find_in(ln, n, "[disabled]") >= 0;
            long s = find_in(ln, n, "[size=");
            if (s >= 0) {
                size_t a = (size_t)s + strlen("[size="), b = a;
                while (b < n && ln[b] != ']' && b - a < sizeof r->size - 1) b++;
                memcpy(r->size, ln + a, b - a);
                r->size[b - a] = '\0';
            }
            v->nreg++;
        }
    }
    return 0;
}

/* (주어짐) 우리 파서 결과 d 와 lspci -vvv 결과 v 를 대조. 반환: 불일치 개수, why 에 첫 불일치 설명 */
static int compare_dev(const pci_dev *d, const lspci_view *v, char *why, size_t cap) {
    int bad = 0, nb = 0, k = 0;
    char s[16];
    why[0] = '\0';
#define MISMATCH(...) do { if (!bad) snprintf(why, cap, __VA_ARGS__); bad++; } while (0)
    if (strcmp(d->driver, v->driver) != 0) MISMATCH("driver ours='%s' lspci='%s'", d->driver, v->driver);
    if (v->caps_denied) {
        if (d->caps.err != E_TRUNC) MISMATCH("lspci <access denied> but our walk err=%d", d->caps.err);
    } else {
        for (int i = 0; i < v->ncap; i++) {
            if (v->cap_off[i] >= PCI_CFG_SPACE_SIZE) continue;   /* extended cap 은 PCIE-B04 */
            if (k >= d->caps.n || d->caps.off[k] != v->cap_off[i]) MISMATCH("cap #%d lspci [%x]", k, v->cap_off[i]);
            k++;
        }
        if (k != d->caps.n) MISMATCH("cap count ours=%d lspci=%d", d->caps.n, k);
    }
    for (int b = 0; b < PCI_STD_NUM_BARS && b < d->nres; b++) {
        const pci_res *r = &d->res[b];
        const lspci_region *g = NULL;
        if (res_size(r) == 0) continue;
        nb++;
        for (int i = 0; i < v->nreg; i++) if (v->reg[i].idx == b) g = &v->reg[i];
        if (!g) { MISMATCH("BAR%d missing in lspci", b); continue; }
        fmt_size(res_size(r), s, sizeof s);
        if (g->addr != r->start) MISMATCH("BAR%d addr ours=%llx lspci=%llx", b, (unsigned long long)r->start, (unsigned long long)g->addr);
        if (strcmp(g->size, s) != 0) MISMATCH("BAR%d size ours=%s lspci=%s", b, s, g->size);
        if (g->is_io != !!(r->flags & IORESOURCE_IO)) MISMATCH("BAR%d io", b);
        if (!g->is_io && g->is64 != !!(r->flags & IORESOURCE_MEM_64)) MISMATCH("BAR%d 64-bit", b);
        if (g->pref != !!(r->flags & IORESOURCE_PREFETCH)) MISMATCH("BAR%d prefetch", b);
    }
    if (nb != v->nreg) MISMATCH("region count ours=%d lspci=%d", nb, v->nreg);
#undef MISMATCH
    return bad;
}

/* (주어짐) sysfs 텍스트 파일과 config 바이트가 같은 말을 하나 */
static int sysfs_matches_cfg(const pci_dev *d) {
    return d->cfg_len >= PCI_STD_HEADER_SIZEOF &&
           d->s_vendor == cfg_rd16(d->cfg, PCI_VENDOR_ID) && d->s_device == cfg_rd16(d->cfg, PCI_DEVICE_ID) &&
           d->s_class == cfg_rd32(d->cfg, PCI_CLASS_REVISION) >> 8;
}

/* (주어짐) "00:06.0 0180: 1af4:1042 (rev 01)" — lspci -n 한 줄 (도메인 0 이면 생략) */
static void lspci_n_line(const pci_dev *d, char *out, size_t cap) {
    const char *b = strncmp(d->bdf, "0000:", 5) == 0 ? d->bdf + 5 : d->bdf;
    uint32_t cr = cfg_rd32(d->cfg, PCI_CLASS_REVISION);
    int k = snprintf(out, cap, "%s %04x: %04x:%04x", b, (unsigned)(cr >> 16),
                     cfg_rd16(d->cfg, PCI_VENDOR_ID), cfg_rd16(d->cfg, PCI_DEVICE_ID));
    if ((cr & 0xFF) && k > 0 && (size_t)k < cap) snprintf(out + k, cap - (size_t)k, " (rev %02x)", (unsigned)(cr & 0xFF));
}

/* (주어짐) 우리 파서의 요약 출력 */
static void print_dev(const pci_dev *d) {
    uint16_t cmd = cfg_rd16(d->cfg, PCI_COMMAND), st = cfg_rd16(d->cfg, PCI_STATUS);
    uint8_t ht = cfg_rd8(d->cfg, PCI_HEADER_TYPE);
    char s[16];
    printf("%s %04x:%04x class %06x hdr %02x%s | Control %04x Mem%c BusMaster%c DisINTx%c | Status %04x Cap%c\n",
           d->bdf, cfg_rd16(d->cfg, PCI_VENDOR_ID), cfg_rd16(d->cfg, PCI_DEVICE_ID),
           (unsigned)(cfg_rd32(d->cfg, PCI_CLASS_REVISION) >> 8), ht,
           (ht & PCI_HEADER_TYPE_MASK) == 1 ? " (Type 1, PCIE-B07)" : "", cmd,
           cmd & PCI_COMMAND_MEMORY ? '+' : '-', cmd & PCI_COMMAND_MASTER ? '+' : '-',
           cmd & PCI_COMMAND_INTX_DISABLE ? '+' : '-', st, st & PCI_STATUS_CAP_LIST ? '+' : '-');
    for (int b = 0; b < PCI_STD_NUM_BARS && b < d->nres; b++) {
        const pci_res *r = &d->res[b];
        if (!res_size(r)) continue;
        fmt_size(res_size(r), s, sizeof s);
        printf("    Region %d: %s %llx (%s%s) [size=%s]  flags=%06llx\n", b,
               r->flags & IORESOURCE_IO ? "I/O ports at" : "Memory at", (unsigned long long)r->start,
               r->flags & IORESOURCE_MEM_64 ? "64-bit" : "32-bit",
               r->flags & IORESOURCE_PREFETCH ? ", prefetchable" : ", non-prefetchable", s,
               (unsigned long long)r->flags);
    }
    printf("    caps:");
    for (int i = 0; i < d->caps.n; i++) printf(" [%02x] %02x %s", d->caps.off[i], d->caps.id[i], cap_name(d->caps.id[i]));
    if (d->caps.err) printf(" (walk err %d)", d->caps.err);
    printf("\n    Kernel driver in use: %s\n", d->driver[0] ? d->driver : "(none)");
}

/* (주어짐) 셸 명령 실행 → 출력(NUL 종료)과 종료 코드 */
static int run_cmd(const char *cmd, char *out, size_t cap, size_t *n) {
    FILE *f = popen(cmd, "r");
    if (!f) return -1;
    size_t k = fread(out, 1, cap - 1, f);
    out[k] = '\0';
    *n = k;
    int st = pclose(f);
    return (st != -1 && WIFEXITED(st)) ? WEXITSTATUS(st) : -1;
}
static int bdf_ok(const char *s) {   /* "DDDD:BB:DD.F" 만 명령줄에 넣는다 */
    if (strlen(s) != 12 || s[4] != ':' || s[7] != ':' || s[10] != '.') return 0;
    for (int i = 0; i < 12; i++) if (i != 4 && i != 7 && i != 10 && hexval(s[i]) < 0) return 0;
    return 1;
}
static int list_devs(const char *sysroot, char names[][16], int max) {
    DIR *dir = opendir(sysroot);
    struct dirent *e;
    int n = 0;
    if (!dir) return E_IO;
    while ((e = readdir(dir)) != NULL && n < max)
        if (bdf_ok(e->d_name)) snprintf(names[n++], 16, "%.15s", e->d_name);
    closedir(dir);
    for (int i = 1; i < n; i++)          /* 삽입 정렬 (BDF 순) */
        for (int j = i; j > 0 && strcmp(names[j - 1], names[j]) > 0; j--) {
            char t[16];
            memcpy(t, names[j], 16); memcpy(names[j], names[j - 1], 16); memcpy(names[j - 1], t, 16);
        }
    return n;
}
/* (주어짐) nobody(65534) 권한으로 path 를 읽어 buf 에 담는다. 반환: 바이트 수 또는 -1 */
static long read_as_nobody(const char *path, uint8_t *buf, size_t cap) {
    int fd[2];
    if (pipe(fd) != 0) return -1;
    pid_t pid = fork();
    if (pid < 0) { close(fd[0]); close(fd[1]); return -1; }
    if (pid == 0) {
        size_t k = 0;
        long n = -1;
        close(fd[0]);
        if (setgroups(0, NULL) == 0 && setgid(65534) == 0 && setuid(65534) == 0 && read_file(path, buf, cap, &k) == 0) n = (long)k;
        if (write(fd[1], &n, sizeof n) != (ssize_t)sizeof n) _exit(2);
        for (size_t w = 0; n > 0 && w < (size_t)n;) {
            ssize_t r = write(fd[1], buf + w, (size_t)n - w);
            if (r <= 0) _exit(3);
            w += (size_t)r;
        }
        _exit(0);
    }
    close(fd[1]);
    long n = -1;
    if (read(fd[0], &n, sizeof n) != (ssize_t)sizeof n) n = -1;
    for (size_t got = 0; n > 0 && got < (size_t)n;) {
        ssize_t r = read(fd[0], buf + got, (size_t)n - got);
        if (r <= 0) { n = -1; break; }
        got += (size_t)r;
    }
    close(fd[0]);
    waitpid(pid, NULL, 0);
    return n;
}

/* ---------------- 가짜 sysfs 트리 (Part B) ---------------- */
static int write_txt(const char *dir, const char *name, const void *data, size_t n) {
    char path[256];
    snprintf(path, sizeof path, "%s/%s", dir, name);
    FILE *f = fopen(path, "wb");
    if (!f) return -1;
    size_t w = fwrite(data, 1, n, f);
    return (fclose(f) == 0 && w == n) ? 0 : -1;
}
static int make_fake_dev(const char *root, const char *bdf, const uint8_t *cfg, size_t cfglen, const char *vendor,
                         const char *device, const char *klass, const char *resource, const char *drvlink) {
    char dir[256], path[300];
    snprintf(dir, sizeof dir, "%s/%s", root, bdf);
    if (mkdir(dir, 0755) != 0) return -1;
    if (cfg && write_txt(dir, "config", cfg, cfglen) != 0) return -1;
    if (write_txt(dir, "vendor", vendor, strlen(vendor)) || write_txt(dir, "device", device, strlen(device)) ||
        write_txt(dir, "class", klass, strlen(klass)) || write_txt(dir, "resource", resource, strlen(resource))) return -1;
    snprintf(path, sizeof path, "%s/driver", dir);
    if (drvlink && symlink(drvlink, path) != 0) return -1;
    return 0;
}
static void rm_fake(const char *root, const char *bdf) {
    static const char *files[] = { "config", "vendor", "device", "class", "resource", "driver" };
    char path[320];
    for (size_t i = 0; i < sizeof files / sizeof files[0]; i++) {
        snprintf(path, sizeof path, "%s/%s/%s", root, bdf, files[i]);
        unlink(path);
    }
    snprintf(path, sizeof path, "%s/%s", root, bdf);
    rmdir(path);
}

/* ---------------- 실측 픽스처 (capture_vm.txt / capture_qemu.txt 에서 그대로) ---------------- */
/* Lima VM elinux, sudo lspci -xxxx -s 00:06.0 (virtio-blk, conventional PCI 형 endpoint, 256B) */
static const char VM_0006_XXXX[] =
    "00:06.0 Mass storage controller: Red Hat, Inc. Virtio 1.0 block device (rev 01)\n"
    "00: f4 1a 42 10 16 04 10 00 01 00 80 01 00 40 00 00\n"
    "10: 04 00 02 80 03 00 00 00 40 15 00 50 00 00 00 00\n"
    "20: 00 00 00 00 00 00 00 00 00 00 00 00 f4 1a 42 00\n"
    "30: 00 00 00 00 40 00 00 00 00 00 00 00 ff 01 00 00\n"
    "40: 09 50 10 01 00 00 00 00 00 00 00 00 38 00 00 00\n"
    "50: 09 60 10 03 00 00 00 00 00 10 00 00 01 00 00 00\n"
    "60: 09 74 14 02 00 00 00 00 00 40 00 00 04 00 00 00\n"
    "70: 04 00 00 00 09 84 10 04 00 00 00 00 00 80 00 00\n"
    "80: 3c 00 00 00 11 00 01 80 02 00 00 00 22 00 00 00\n"
    "90: 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00\n"
    "a0: 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00\n"
    "b0: 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00\n"
    "c0: 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00\n"
    "d0: 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00\n"
    "e0: 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00\n"
    "f0: 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00\n\n";
/* 같은 장치, 비root lspci -xxx (64B 만) */
static const char VM_0006_XXX_USER[] =
    "00:06.0 Mass storage controller: Red Hat, Inc. Virtio 1.0 block device (rev 01)\n"
    "00: f4 1a 42 10 16 04 10 00 01 00 80 01 00 40 00 00\n"
    "10: 04 00 02 80 03 00 00 00 40 15 00 50 00 00 00 00\n"
    "20: 00 00 00 00 00 00 00 00 00 00 00 00 f4 1a 42 00\n"
    "30: 00 00 00 00 40 00 00 00 00 00 00 00 ff 01 00 00\n\n";
/* cat /sys/bus/pci/devices/0000:00:06.0/resource (13줄: BAR0..5, ROM, 그 뒤는 커널 설정에 따라) */
static const char VM_0006_RES[] =
    "0x0000000380020000 0x000000038002ffff 0x0000000000140204\n"
    "0x0000000000000000 0x0000000000000000 0x0000000000000000\n"
    "0x0000000050001540 0x000000005000157f 0x0000000000040200\n"
    "0x0000000000000000 0x0000000000000000 0x0000000000000000\n"
    "0x0000000000000000 0x0000000000000000 0x0000000000000000\n"
    "0x0000000000000000 0x0000000000000000 0x0000000000000000\n"
    "0x0000000000000000 0x0000000000000000 0x0000000000000000\n"
    "0x0000000000000000 0x0000000000000000 0x0000000000000000\n"
    "0x0000000000000000 0x0000000000000000 0x0000000000000000\n"
    "0x0000000000000000 0x0000000000000000 0x0000000000000000\n"
    "0x0000000000000000 0x0000000000000000 0x0000000000000000\n"
    "0x0000000000000000 0x0000000000000000 0x0000000000000000\n"
    "0x0000000000000000 0x0000000000000000 0x0000000000000000\n";
/* 비root lspci -vvv -s 00:06.0 */
static const char VM_0006_VVV_USER[] =
    "00:06.0 Mass storage controller: Red Hat, Inc. Virtio 1.0 block device (rev 01)\n"
    "\tSubsystem: Red Hat, Inc. Device 0042\n"
    "\tControl: I/O- Mem+ BusMaster+ SpecCycle- MemWINV+ VGASnoop- ParErr- Stepping- SERR- FastB2B- DisINTx+\n"
    "\tStatus: Cap+ 66MHz- UDF- FastB2B- ParErr- DEVSEL=fast >TAbort- <TAbort- <MAbort- >SERR- <PERR- INTx-\n"
    "\tLatency: 64\n"
    "\tInterrupt: pin A routed to IRQ 16\n"
    "\tRegion 0: Memory at 380020000 (64-bit, non-prefetchable) [size=64K]\n"
    "\tRegion 2: Memory at 50001540 (32-bit, non-prefetchable) [size=64]\n"
    "\tCapabilities: <access denied>\n"
    "\tKernel driver in use: virtio-pci\n"
    "\tKernel modules: virtio_pci\n\n";
/* root lspci -vvv -s 00:06.0 (발췌 없이 전체) */
static const char VM_0006_VVV_ROOT[] =
    "00:06.0 Mass storage controller: Red Hat, Inc. Virtio 1.0 block device (rev 01)\n"
    "\tSubsystem: Red Hat, Inc. Device 0042\n"
    "\tControl: I/O- Mem+ BusMaster+ SpecCycle- MemWINV+ VGASnoop- ParErr- Stepping- SERR- FastB2B- DisINTx+\n"
    "\tStatus: Cap+ 66MHz- UDF- FastB2B- ParErr- DEVSEL=fast >TAbort- <TAbort- <MAbort- >SERR- <PERR- INTx-\n"
    "\tLatency: 64\n"
    "\tInterrupt: pin A routed to IRQ 16\n"
    "\tRegion 0: Memory at 380020000 (64-bit, non-prefetchable) [size=64K]\n"
    "\tRegion 2: Memory at 50001540 (32-bit, non-prefetchable) [size=64]\n"
    "\tCapabilities: [40] Vendor Specific Information: VirtIO: CommonCfg\n"
    "\t\tBAR=0 offset=00000000 size=00000038\n"
    "\tCapabilities: [50] Vendor Specific Information: VirtIO: ISR\n"
    "\t\tBAR=0 offset=00001000 size=00000001\n"
    "\tCapabilities: [60] Vendor Specific Information: VirtIO: Notify\n"
    "\t\tBAR=0 offset=00004000 size=00000004 multiplier=00000004\n"
    "\tCapabilities: [74] Vendor Specific Information: VirtIO: DeviceCfg\n"
    "\t\tBAR=0 offset=00008000 size=0000003c\n"
    "\tCapabilities: [84] MSI-X: Enable+ Count=2 Masked-\n"
    "\t\tVector table: BAR=2 offset=00000000\n"
    "\t\tPBA: BAR=2 offset=00000020\n"
    "\tKernel driver in use: virtio-pci\n"
    "\tKernel modules: virtio_pci\n\n";
/* QEMU 게스트 01:00.0 (QEMU NVMe, nvme.ko 없음 → Mem- BusMaster-), lspci -F 로 본 -vvv 머리 부분 */
static const char QEMU_NVME_VVV_HEAD[] =
    "01:00.0 Non-Volatile memory controller: Red Hat, Inc. QEMU NVM Express Controller (rev 02) (prog-if 02 [NVM Express])\n"
    "\tSubsystem: Red Hat, Inc. Device 1100\n"
    "\tControl: I/O- Mem- BusMaster- SpecCycle- MemWINV- VGASnoop- ParErr- Stepping- SERR- FastB2B- DisINTx-\n"
    "\tRegion 0: Memory at 10000000 (64-bit, non-prefetchable) [disabled]\n"
    "\tCapabilities: [40] MSI-X: Enable- Count=65 Masked-\n"
    "\tCapabilities: [80] Express (v2) Endpoint, IntMsgNum 0\n"
    "\tCapabilities: [60] Power Management version 3\n";
/* QEMU 게스트 01:00.0 config 256B (capture_qemu.txt) */
static const char QEMU_NVME_XXX[] =
    "01:00.0 Non-Volatile memory controller: Red Hat, Inc. QEMU NVM Express Controller (rev 02)\n"
    "00: 36 1b 10 00 00 00 10 00 02 02 08 01 00 00 00 00\n"
    "10: 04 00 00 10 00 00 00 00 00 00 00 00 00 00 00 00\n"
    "20: 00 00 00 00 00 00 00 00 00 00 00 00 f4 1a 00 11\n"
    "30: 00 00 00 00 40 00 00 00 00 00 00 00 00 01 00 00\n"
    "40: 11 80 40 00 00 20 00 00 00 30 00 00 00 00 00 00\n"
    "50: 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00\n"
    "60: 01 00 03 00 08 00 00 00 00 00 00 00 00 00 00 00\n"
    "70: 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00\n"
    "80: 10 60 02 00 20 80 00 10 00 00 00 00 11 04 00 00\n"
    "90: 00 00 11 00 00 00 00 00 00 00 00 00 00 00 00 00\n"
    "a0: 00 00 00 00 00 00 30 00 00 00 00 00 00 00 00 00\n"
    "b0: 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00\n"
    "c0: 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00\n"
    "d0: 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00\n"
    "e0: 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00\n"
    "f0: 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00\n";
/* QEMU 게스트 00:02.0 (pcie-root-port, Type 1) config 256B */
static const char QEMU_RP_XXX[] =
    "00:02.0 PCI bridge: Red Hat, Inc. QEMU PCIe Root port\n"
    "00: 36 1b 0c 00 07 04 10 00 00 00 04 06 00 00 01 00\n"
    "10: 00 10 28 10 00 00 00 00 00 01 01 00 10 10 00 00\n"
    "20: 00 10 10 10 01 00 11 00 80 00 00 00 80 00 00 00\n"
    "30: 00 00 00 00 54 00 00 00 00 00 00 00 0f 01 02 00\n"
    "40: 0d 00 00 00 36 1b 00 00 11 40 00 80 00 00 00 00\n"
    "50: 00 08 00 00 10 48 42 01 20 80 00 00 0f 00 00 00\n"
    "60: 04 06 30 00 00 00 11 20 7b 00 0a 00 c0 03 50 00\n"
    "70: 00 00 00 00 00 00 00 00 20 00 30 00 00 00 00 00\n"
    "80: 1e 00 00 00 04 00 00 00 00 00 00 00 00 00 00 00\n"
    "90: 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00\n"
    "a0: 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00\n"
    "b0: 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00\n"
    "c0: 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00\n"
    "d0: 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00\n"
    "e0: 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00\n"
    "f0: 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00\n";

/* cfg[0..len) → lspci -xxx 형식 텍스트 (Part B 변형 입력 만들기용) */
static size_t emit_lspci_x(const uint8_t *cfg, size_t len, char *out, size_t cap) {
    size_t k = (size_t)snprintf(out, cap, "00:06.0 test device\n");
    for (size_t off = 0; off < len && k + 64 < cap; off += 16) {
        k += (size_t)snprintf(out + k, cap - k, "%02zx:", off);
        for (int i = 0; i < 16; i++) k += (size_t)snprintf(out + k, cap - k, " %02x", cfg[off + (size_t)i]);
        k += (size_t)snprintf(out + k, cap - k, "\n");
    }
    return k;
}

/* ---------------- 테스트 하네스 ---------------- */
static int g_pass, g_total;
#define CHECK(name, expr) do { g_total++; if (expr) { g_pass++; printf("PASS %s\n", name); } \
    else printf("FAIL %s: %s (line %d)\n", name, #expr, __LINE__); } while (0)
#define SZ(s) (sizeof(s) - 1)

int main(void) {
    static uint8_t cfg[PCI_CFG_SPACE_EXP_SIZE], c2[PCI_CFG_SPACE_EXP_SIZE];
    static char txt[TXT_MAX], why[160];
    static pci_res res[MAX_RES];
    static pci_dev d;
    static lspci_view v;
    static char names[MAX_DEVS][16];
    cap_list cl = { 0 };
    char s[16];
    size_t n;
    int r;

    /* ===== Part A — 실측 덤프 디코드 ===== */
    r = parse_lspci_x(VM_0006_XXXX, SZ(VM_0006_XXXX), cfg, sizeof cfg);
    CHECK("A1 lspci -xxxx 256B parsed", r == 256);
    CHECK("A1 VID:DID 1af4:1042, class 018000, rev 01, hdr 00",
          cfg_rd16(cfg, PCI_VENDOR_ID) == 0x1af4 && cfg_rd16(cfg, PCI_DEVICE_ID) == 0x1042 &&
          cfg_rd32(cfg, PCI_CLASS_REVISION) == 0x01800001u && cfg_rd8(cfg, PCI_HEADER_TYPE) == 0x00);
    CHECK("A1 Command 0416 (Mem+ BusMaster+ MemWINV+ DisINTx+), Status 0010 (Cap+)",
          cfg_rd16(cfg, PCI_COMMAND) == 0x0416 && cfg_rd16(cfg, PCI_STATUS) == 0x0010);
    CHECK("A1 BAR0/1 = 80020004/00000003 (64-bit pair), BAR2 = 50001540, cap ptr 40, IntPin 1 Line ff",
          cfg_rd32(cfg, PCI_BASE_ADDRESS_0) == 0x80020004u && cfg_rd32(cfg, 0x14) == 3u &&
          cfg_rd32(cfg, 0x18) == 0x50001540u && cfg_rd8(cfg, PCI_CAPABILITY_LIST) == 0x40 &&
          cfg_rd8(cfg, 0x3d) == 1 && cfg_rd8(cfg, 0x3c) == 0xff);
    CHECK("A1 non-root lspci -xxx = 64B, same header bytes",
          parse_lspci_x(VM_0006_XXX_USER, SZ(VM_0006_XXX_USER), c2, sizeof c2) == 64 && memcmp(c2, cfg, 64) == 0);
    memset(c2, 0, sizeof c2);
    memcpy(c2, cfg, 256);
    n = emit_lspci_x(c2, 4096, txt, sizeof txt);
    CHECK("A1 4KB -xxxx text (offsets 100: .. ff0:) parsed", parse_lspci_x(txt, n, c2, sizeof c2) == 4096 && memcmp(c2, cfg, 256) == 0);

    r = parse_resource(VM_0006_RES, SZ(VM_0006_RES), res, MAX_RES);
    CHECK("A2 resource: 13 lines", r == 13);
    CHECK("A2 BAR0 380020000-38002ffff flags 140204 = MEM|MEM_64 (+SIZEALIGN), size 64K",
          res[0].start == 0x380020000ull && res[0].end == 0x38002ffffull && res[0].flags == 0x140204 &&
          (res[0].flags & IORESOURCE_MEM) && (res[0].flags & IORESOURCE_MEM_64) && res_size(&res[0]) == 65536);
    CHECK("A2 BAR1 line empty (upper half of 64-bit BAR0), BAR2 50001540 size 64 32-bit",
          res_size(&res[1]) == 0 && res[2].start == 0x50001540ull && res_size(&res[2]) == 64 &&
          !(res[2].flags & IORESOURCE_MEM_64));
    CHECK("A2 resource start == config BAR address, flags low nibble == BAR type bits",
          res[0].start != 0 && ((uint64_t)cfg_rd32(cfg, 0x14) << 32 | (cfg_rd32(cfg, 0x10) & ~0xFu)) == res[0].start &&
          (res[0].flags & 0xF) == (cfg_rd32(cfg, 0x10) & 0xF) && (cfg_rd32(cfg, 0x18) & ~0xFu) == res[2].start);
    {
        static const struct { uint64_t sz; const char *s; } t[] = {
            { 65536, "64K" }, { 64, "64" }, { 128, "128" }, { 32768, "32K" }, { 4096, "4K" }, { 2097152, "2M" },
            { 1ull << 30, "1G" }, { 256ull << 20, "256M" }, { 0x1800, "6K" }, { 4097, "4097" }, { 1ull << 40, "1T" }, { 0, "" } };
        int ok = 1;
        for (size_t i = 0; i < sizeof t / sizeof t[0]; i++) {
            fmt_size(t[i].sz, s, sizeof s);
            if (strcmp(s, t[i].s) != 0) { ok = 0; printf("   fmt_size(%llu) = '%s' want '%s'\n", (unsigned long long)t[i].sz, s, t[i].s); }
        }
        CHECK("A3 fmt_size: 64K 64 128 32K 4K 2M 1G 256M 6K 4097 1T ''", ok);
    }
    r = walk_caps(cfg, 256, &cl);
    CHECK("A4 virtio-blk caps: [40]09 [50]09 [60]09 [74]09 [84]11",
          r == 5 && cl.off[0] == 0x40 && cl.off[1] == 0x50 && cl.off[2] == 0x60 && cl.off[3] == 0x74 && cl.off[4] == 0x84 &&
          cl.id[0] == PCI_CAP_ID_VNDR && cl.id[3] == PCI_CAP_ID_VNDR && cl.id[4] == PCI_CAP_ID_MSIX);
    CHECK("A4 MSI-X Message Control at cap+2 = 8001 (Enable+, table size 1 → Count=2)", r == 5 && cfg_rd16(cfg, (uint16_t)(cl.off[4] + 2)) == 0x8001);
    r = parse_lspci_x(QEMU_NVME_XXX, SZ(QEMU_NVME_XXX), c2, sizeof c2);
    CHECK("A4 QEMU NVMe: class 010802, Command 0000, caps [40]11 → [80]10 → [60]01 (not sorted)",
          r == 256 && cfg_rd32(c2, PCI_CLASS_REVISION) >> 8 == 0x010802 && cfg_rd16(c2, PCI_COMMAND) == 0 &&
          walk_caps(c2, 256, &cl) == 3 && cl.off[0] == 0x40 && cl.id[0] == PCI_CAP_ID_MSIX && cl.off[1] == 0x80 &&
          cl.id[1] == PCI_CAP_ID_EXP && cl.off[2] == 0x60 && cl.id[2] == PCI_CAP_ID_PM);
    r = parse_lspci_x(QEMU_RP_XXX, SZ(QEMU_RP_XXX), c2, sizeof c2);
    CHECK("A4 QEMU root port: Type 1, class 060400, caps [54]10 → [48]11 → [40]0d",
          r == 256 && (cfg_rd8(c2, PCI_HEADER_TYPE) & PCI_HEADER_TYPE_MASK) == 1 && cfg_rd32(c2, 8) >> 8 == 0x060400 &&
          walk_caps(c2, 256, &cl) == 3 && cl.off[0] == 0x54 && cl.id[0] == PCI_CAP_ID_EXP && cl.off[1] == 0x48 &&
          cl.id[1] == PCI_CAP_ID_MSIX && cl.off[2] == 0x40 && cl.id[2] == PCI_CAP_ID_SSVID);

    /* ===== Part B — 깨진 입력 ===== */
    {
        memcpy(c2, cfg, 256);
        n = emit_lspci_x(c2, 48, txt, sizeof txt);
        CHECK("B1 48B (3 lines) → E_TRUNC", parse_lspci_x(txt, n, c2, sizeof c2) == E_TRUNC);
        n = emit_lspci_x(cfg, 64, txt, sizeof txt);
        txt[n - 4] = '\n'; txt[n - 3] = '\0';                      /* 마지막 줄에서 바이트 하나 잘라 냄 */
        CHECK("B1 line with 15 bytes → E_FORMAT", parse_lspci_x(txt, n - 3, c2, sizeof c2) == E_FORMAT);
        n = emit_lspci_x(cfg, 64, txt, sizeof txt);
        memcpy(strstr(txt, "10: ") + 4, "zz", 2);
        CHECK("B1 non-hex byte 'zz' → E_FORMAT", parse_lspci_x(txt, n, c2, sizeof c2) == E_FORMAT);
        n = emit_lspci_x(cfg, 64, txt, sizeof txt);
        memcpy(strstr(txt, "20: "), "30", 2);
        CHECK("B1 offset 30 where 20 expected → E_ORDER", parse_lspci_x(txt, n, c2, sizeof c2) == E_ORDER);
        CHECK("B1 256B text into 64B buffer → E_CAP", parse_lspci_x(VM_0006_XXXX, SZ(VM_0006_XXXX), c2, 64) == E_CAP);
        n = emit_lspci_x(cfg, 64, txt, sizeof txt);
        n += (size_t)snprintf(txt + n, sizeof txt - n, "00:07.0 next device\n40: 00\n");
        CHECK("B1 second header right after data (no blank line) → E_FORMAT", parse_lspci_x(txt, n, c2, sizeof c2) == E_FORMAT);
        CHECK("B1 empty text → E_TRUNC", parse_lspci_x("", 0, c2, sizeof c2) == E_TRUNC);
        CHECK("B1 -vvv text fed to hex parser → E_TRUNC (no data lines)",
              parse_lspci_x(VM_0006_VVV_ROOT, SZ(VM_0006_VVV_ROOT), c2, sizeof c2) == E_TRUNC);
        n = emit_lspci_x(cfg, 64, txt, sizeof txt);
        CHECK("B1 length-bounded: same text cut at n-20 (no NUL at end) → E_FORMAT", parse_lspci_x(txt, n - 20, c2, sizeof c2) == E_FORMAT);
    }
    {
        memcpy(c2, cfg, 256);
        c2[PCI_STATUS] &= (uint8_t)~PCI_STATUS_CAP_LIST;
        CHECK("B2 Status.CapList=0 → 0 caps even though 0x34=40", walk_caps(c2, 256, &cl) == 0 && cl.n == 0);
        memcpy(c2, cfg, 256);
        c2[0x51] = 0x40;                                            /* [50].next → 40: 루프 */
        r = walk_caps(c2, 256, &cl);
        CHECK("B2 loop 40→50→40 → E_LOOP after 2 caps", r == E_LOOP && cl.err == E_LOOP && cl.n == 2);
        memcpy(c2, cfg, 256);
        c2[0x61] = 0x20;
        r = walk_caps(c2, 256, &cl);
        CHECK("B2 next ptr 0x20 (< 0x40, inside header) → E_BADPTR after 3", r == E_BADPTR && cl.n == 3);
        r = walk_caps(cfg, 64, &cl);
        CHECK("B2 64B buffer, ptr 0x40 → E_TRUNC (why lspci says <access denied>)", r == E_TRUNC && cl.n == 0);
        memcpy(c2, cfg, 256);
        c2[PCI_CAPABILITY_LIST] = 0x43;
        c2[0x41] = 0x53;
        CHECK("B2 low 2 bits of pointers masked: 43→40, 53→50", walk_caps(c2, 256, &cl) == 5 && cl.off[0] == 0x40 && cl.off[1] == 0x50);
    }
    {
        CHECK("B3 resource: 2 tokens → E_FORMAT", parse_resource("0x1 0x2\n", SZ("0x1 0x2\n"), res, MAX_RES) == E_FORMAT);
        CHECK("B3 resource: garbage → E_FORMAT", parse_resource("hello world !\n", SZ("hello world !\n"), res, MAX_RES) == E_FORMAT);
        CHECK("B3 resource: end < start → E_FORMAT",
              parse_resource("0x2000 0x1fff 0x200\n", SZ("0x2000 0x1fff 0x200\n"), res, MAX_RES) == E_FORMAT);
        CHECK("B3 resource: 5 lines → E_TRUNC", parse_resource(VM_0006_RES, 5 * 57, res, MAX_RES) == E_TRUNC);
        CHECK("B3 resource: 13 lines into max 8 → E_CAP", parse_resource(VM_0006_RES, SZ(VM_0006_RES), res, 8) == E_CAP);
        CHECK("B3 resource: 4th token → E_FORMAT", parse_resource("0x1 0x2 0x3 0x4\n", SZ("0x1 0x2 0x3 0x4\n"), res, MAX_RES) == E_FORMAT);
    }

    /* ===== A5 + B4: 가짜 sysfs 트리 (결정적, root 불필요) ===== */
    char root[] = "/tmp/PCIE-B06_fake.XXXXXX";
    int have_fake = mkdtemp(root) != NULL;
    CHECK("A5 fake sysfs dir created", have_fake);
    if (have_fake) {
        make_fake_dev(root, "0000:00:06.0", cfg, 256, "0x1af4\n", "0x1042\n", "0x018000\n", VM_0006_RES,
                      "../../../bus/pci/drivers/virtio-pci");
        make_fake_dev(root, "0000:00:1e.0", cfg, 64, "0x1af4\n", "0x1042\n", "0x018000\n", VM_0006_RES, NULL);
        make_fake_dev(root, "0000:00:1f.0", NULL, 0, "0x1af4\n", "0x1042\n", "0x018000\n", VM_0006_RES, NULL);
        make_fake_dev(root, "0000:00:1d.0", cfg, 256, "0x8086\n", "0x1042\n", "0x018000\n", VM_0006_RES, NULL);
        memset(c2, 0xff, 256);
        make_fake_dev(root, "0000:00:1c.0", c2, 256, "0xffff\n", "0xffff\n", "0xffffff\n", VM_0006_RES, NULL);
        make_fake_dev(root, "0000:00:1b.0", cfg, 256, "0x1af4\n", "0x1042\n", "0x018000\n", "0x1 0x2\n", NULL);

        r = read_dev(root, "0000:00:06.0", &d);
        CHECK("A5 read_dev: 256B config, sysfs text == config, driver 'virtio-pci', 5 caps, 13 res",
              r == 0 && d.cfg_len == 256 && sysfs_matches_cfg(&d) && strcmp(d.driver, "virtio-pci") == 0 &&
              d.caps.n == 5 && d.caps.err == E_OK && d.nres == 13);

        r = read_dev(root, "0000:00:1e.0", &d);
        CHECK("B4 64B config (non-root view) + no driver link: driver '', caps E_TRUNC",
              r == 0 && d.cfg_len == 64 && d.driver[0] == '\0' && d.caps.err == E_TRUNC && d.caps.n == 0);
        CHECK("B4 missing config file → E_IO", read_dev(root, "0000:00:1f.0", &d) == E_IO);
        r = read_dev(root, "0000:00:1d.0", &d);
        CHECK("B4 stale vendor file (8086) vs config (1af4) detected", r == 0 && !sysfs_matches_cfg(&d));
        r = read_dev(root, "0000:00:1c.0", &d);
        CHECK("B4 all-FF config: VID ffff = no device, walk stops (Status ffff, ptr fc→ loop/trunc guarded)",
              r == 0 && cfg_rd16(d.cfg, PCI_VENDOR_ID) == 0xffff && d.caps.err != E_OK && d.caps.n <= 1);
        CHECK("B4 malformed resource file → E_FORMAT", read_dev(root, "0000:00:1b.0", &d) == E_FORMAT);
        static const char *fakes[] = { "0000:00:06.0", "0000:00:1e.0", "0000:00:1f.0", "0000:00:1d.0", "0000:00:1c.0", "0000:00:1b.0" };
        for (size_t i = 0; i < sizeof fakes / sizeof fakes[0]; i++) rm_fake(root, fakes[i]);
        rmdir(root);
    }

    /* ===== Part C — lspci -vvv 파서 + VM 실측 대조 ===== */
    r = parse_lspci_vvv(VM_0006_VVV_ROOT, SZ(VM_0006_VVV_ROOT), &v);
    CHECK("C1 -vvv root: driver virtio-pci, caps 40 50 60 74 84, regions 0(64-bit 64K) 2(32-bit 64)",
          r == 0 && strcmp(v.driver, "virtio-pci") == 0 && !v.caps_denied && v.ncap == 5 && v.cap_off[0] == 0x40 &&
          v.cap_off[4] == 0x84 && v.nreg == 2 && v.reg[0].idx == 0 && v.reg[0].addr == 0x380020000ull && v.reg[0].is64 &&
          strcmp(v.reg[0].size, "64K") == 0 && v.reg[1].idx == 2 && !v.reg[1].is64 && strcmp(v.reg[1].size, "64") == 0);
    r = parse_lspci_vvv(VM_0006_VVV_USER, SZ(VM_0006_VVV_USER), &v);
    CHECK("C1 -vvv non-root: caps <access denied>, regions still shown", r == 0 && v.caps_denied && v.ncap == 0 && v.nreg == 2);
    memset(&d, 0, sizeof d);                                       /* 64B 만 읽은 우리 쪽 결과와 대조 */
    memcpy(d.cfg, cfg, 64); d.cfg_len = 64;
    d.nres = parse_resource(VM_0006_RES, SZ(VM_0006_RES), d.res, MAX_RES);
    snprintf(d.driver, sizeof d.driver, "virtio-pci");
    walk_caps(d.cfg, d.cfg_len, &d.caps);
    CHECK("C1 non-root view: ours (64B, caps E_TRUNC) == lspci (<access denied>)", compare_dev(&d, &v, why, sizeof why) == 0);
    r = parse_lspci_vvv(QEMU_NVME_VVV_HEAD, SZ(QEMU_NVME_VVV_HEAD), &v);
    CHECK("C1 QEMU NVMe: no driver line → '', Region 0 [disabled] no size, caps 40 80 60",
          r == 0 && v.driver[0] == '\0' && v.nreg == 1 && v.reg[0].disabled && v.reg[0].size[0] == '\0' &&
          v.ncap == 3 && v.cap_off[1] == 0x80);
    CHECK("C1 malformed Region line → E_FORMAT",
          parse_lspci_vvv("\tRegion 0: Banana at 1\n", SZ("\tRegion 0: Banana at 1\n"), &v) == E_FORMAT &&
          parse_lspci_vvv("\tRegion 9: Memory at 1\n", SZ("\tRegion 9: Memory at 1\n"), &v) == E_FORMAT);

    int ndev = list_devs(SYSFS_PCI, names, MAX_DEVS);
    int is_root = geteuid() == 0;
    printf("live: %d PCI functions under %s, euid %d\n", ndev, SYSFS_PCI, (int)geteuid());
    CHECK("C2 running as root with >= 1 PCI function (check.sh uses sudo)", is_root && ndev > 0);
    if (is_root && ndev > 0) {
        int bad_read = 0, bad_x = 0, bad_v = 0, bad_n = 0;
        for (int i = 0; i < ndev; i++) {
            char cmd[128];
            if (read_dev(SYSFS_PCI, names[i], &d) != 0) {
                bad_read++; bad_x++; bad_v++; bad_n++; printf("   read_dev %s failed\n", names[i]); continue;
            }
            print_dev(&d);
            if (!sysfs_matches_cfg(&d) || (d.cfg_len != 256 && d.cfg_len != 4096) || d.caps.err != E_OK) {
                bad_read++; printf("   %s: cfg_len %zu, caps err %d, sysfs match %d\n", names[i], d.cfg_len, d.caps.err, sysfs_matches_cfg(&d));
            }
            snprintf(cmd, sizeof cmd, "lspci -xxx -s %s 2>&1", names[i]);
            if (run_cmd(cmd, txt, sizeof txt, &n) != 0 || parse_lspci_x(txt, n, c2, sizeof c2) != 256 || memcmp(c2, d.cfg, 256) != 0) {
                bad_x++; printf("   %s: lspci -xxx bytes differ from sysfs config\n", names[i]);
            }
            snprintf(cmd, sizeof cmd, "lspci -vvv -s %s 2>&1", names[i]);
            if (run_cmd(cmd, txt, sizeof txt, &n) != 0 || parse_lspci_vvv(txt, n, &v) != 0 || compare_dev(&d, &v, why, sizeof why) != 0) {
                bad_v++; printf("   %s: lspci -vvv mismatch: %s\n", names[i], why);
            }
            char mine[96];
            lspci_n_line(&d, mine, sizeof mine);
            snprintf(cmd, sizeof cmd, "lspci -n -s %s 2>&1", names[i]);
            if (run_cmd(cmd, txt, sizeof txt, &n) != 0 || n == 0 || strncmp(txt, mine, strlen(mine)) != 0 || txt[strlen(mine)] != '\n') {
                bad_n++; printf("   %s: lspci -n '%.*s' vs ours '%s'\n", names[i], (int)strcspn(txt, "\n"), txt, mine);
            }
        }
        CHECK("C2 every function: read_dev ok, 256/4096B, sysfs vendor/device/class == config, caps walk clean", bad_read == 0);
        CHECK("C2 every function: lspci -xxx bytes == our sysfs config bytes", bad_x == 0);
        CHECK("C3 every function: lspci -vvv (driver, regions addr/size/64-bit/prefetch, cap offsets) == ours", bad_v == 0);
        CHECK("C3 every function: lspci -n line == ours", bad_n == 0);

        /* setpci: 대표 장치 하나 (00:06.0 이 있으면 그것, 없으면 첫 장치) */
        char tb[16];
        memcpy(tb, names[0], sizeof tb);
        for (int i = 0; i < ndev; i++) if (strcmp(names[i], "0000:00:06.0") == 0) memcpy(tb, names[i], sizeof tb);
        read_dev(SYSFS_PCI, tb, &d);
        struct { const char *reg; uint32_t want; } sp[] = {
            { "COMMAND", cfg_rd16(d.cfg, PCI_COMMAND) }, { "VENDOR_ID.w", cfg_rd16(d.cfg, PCI_VENDOR_ID) },
            { "BASE_ADDRESS_0", cfg_rd32(d.cfg, PCI_BASE_ADDRESS_0) }, { "0x34.b", cfg_rd8(d.cfg, PCI_CAPABILITY_LIST) } };
        int bad_sp = 0;
        for (size_t i = 0; i < sizeof sp / sizeof sp[0]; i++) {
            char cmd[128];
            uint64_t x;
            size_t k = 0;
            snprintf(cmd, sizeof cmd, "setpci -s %s %s 2>&1", tb, sp[i].reg);
            if (run_cmd(cmd, txt, sizeof txt, &n) != 0 || hex_tok(txt, n, &k, &x) < 0 || x != sp[i].want) {
                bad_sp++; printf("   setpci %s %s = '%.*s' want %x\n", tb, sp[i].reg, (int)strcspn(txt, "\n"), txt, sp[i].want);
            }
        }
        CHECK("C4 setpci COMMAND / VENDOR_ID.w / BASE_ADDRESS_0 / 0x34.b == our cfg_rd", bad_sp == 0);
        int msix = -1, exp = -1;
        for (int i = 0; i < d.caps.n; i++) {
            if (d.caps.id[i] == PCI_CAP_ID_MSIX && msix < 0) msix = d.caps.off[i];
            if (d.caps.id[i] == PCI_CAP_ID_EXP && exp < 0) exp = d.caps.off[i];
        }
        char cmd[128];
        uint64_t x = 0;
        size_t k = 0;
        snprintf(cmd, sizeof cmd, "setpci -s %s CAP_MSIX+2.w 2>&1", tb);
        int rc = run_cmd(cmd, txt, sizeof txt, &n);
        CHECK("C4 setpci CAP_MSIX+2.w == cfg_rd16(our MSI-X offset + 2) (or both say 'no MSI-X')",
              msix >= 0 ? (rc == 0 && hex_tok(txt, n, &k, &x) == 0 && x == cfg_rd16(d.cfg, (uint16_t)(msix + 2))) : rc != 0);
        snprintf(cmd, sizeof cmd, "setpci -s %s CAP_EXP+0x12.w 2>&1", tb);
        rc = run_cmd(cmd, txt, sizeof txt, &n);
        k = 0;
        CHECK("C4 setpci CAP_EXP+0x12.w (LnkSta) fails iff our walk found no PCIe cap (0x10)",
              exp >= 0 ? (rc == 0 && hex_tok(txt, n, &k, &x) == 0 && x == cfg_rd16(d.cfg, (uint16_t)(exp + 0x12))) : rc != 0);
        printf("   setpci -s %s CAP_EXP+0x12.w → exit %d: %.*s\n", tb, rc, (int)strcspn(txt, "\n"), txt);

        /* 비root 로 읽으면? */
        char path[128];
        snprintf(path, sizeof path, "%s/%s/config", SYSFS_PCI, tb);
        long un = read_as_nobody(path, c2, sizeof c2);
        r = un > 0 ? walk_caps(c2, (size_t)un, &cl) : E_IO;
        printf("   nobody read %s: %ld bytes, walk_caps → %d\n", path, un, r);
        CHECK("C5 as nobody: config read = 64B (header only), same bytes, cap walk → E_TRUNC",
              un == 64 && memcmp(c2, d.cfg, 64) == 0 && (d.caps.n == 0 || r == E_TRUNC));
        snprintf(cmd, sizeof cmd, "setpriv --reuid=65534 --regid=65534 --clear-groups lspci -vvv -s %s 2>&1", tb);
        rc = run_cmd(cmd, txt, sizeof txt, &n);
        CHECK("C5 lspci -vvv as nobody → 'Capabilities: <access denied>' (if device has caps)",
              rc == 0 && parse_lspci_vvv(txt, n, &v) == 0 && (d.caps.n == 0 || v.caps_denied));
    }

    printf("%d/%d passed\n", g_pass, g_total);
    return g_pass == g_total ? 0 : 1;
}
