/* ENV: linux */
/* PCIE-B06 추가 연습 — 한 번에 검증 (VM 안에서 root 로: check.sh practice)
 *
 * 연습 1: lspci -tvn 트리를 직접 그리기 — 이 VM 의 평평한 버스(sysfs) + QEMU 의 root port 아래 NVMe(실측 덤프)
 * 연습 2: PCIe capability 의 LnkCap/LnkSta 를 lspci 문자열로 + "(downgraded)" 규칙 + 링크 판정(triage)
 * 연습 3: lspci -d ::0180 / -d 1af4: 필터를 sysfs 로 직접 구현해 대조
 * 연습 4: sysfs resource 의 BAR 가 /proc/iomem 에 "0000:bb:dd.f" 로 잡혀 있는지 + 비root 에게는 주소가 0 으로 보이는지
 *
 * solution.c 의 도우미(parse_lspci_x, walk_caps, read_file …)를 축약해 다시 담았다 — 파일 하나로 빌드된다.
 * 빌드: gcc -std=c11 -Wall -Wextra -Werror -O2 practice.c -o practice && sudo ./practice
 */
#define _GNU_SOURCE
#include <dirent.h>
#include <grp.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#define PCI_STD_HEADER_SIZEOF   64
#define PCI_STD_NUM_BARS        6
#define PCI_VENDOR_ID           0x00
#define PCI_DEVICE_ID           0x02
#define PCI_STATUS              0x06
#define  PCI_STATUS_CAP_LIST    0x10
#define PCI_CLASS_REVISION      0x08
#define PCI_HEADER_TYPE         0x0e
#define  PCI_HEADER_TYPE_MASK   0x7f
#define PCI_SECONDARY_BUS       0x19
#define PCI_SUBORDINATE_BUS     0x1a
#define PCI_CAPABILITY_LIST     0x34
#define  PCI_CAP_ID_EXP         0x10
#define PCI_EXP_FLAGS           0x02
#define  PCI_EXP_FLAGS_TYPE     0x00f0
#define   PCI_EXP_TYPE_ENDPOINT    0x0
#define   PCI_EXP_TYPE_ROOT_PORT   0x4
#define   PCI_EXP_TYPE_UPSTREAM    0x5
#define   PCI_EXP_TYPE_DOWNSTREAM  0x6
#define   PCI_EXP_TYPE_PCIE_BRIDGE 0x8
#define PCI_EXP_LNKCAP          0x0c
#define  PCI_EXP_LNKCAP_SLS     0x0000000f
#define  PCI_EXP_LNKCAP_MLW     0x000003f0
#define  PCI_EXP_LNKCAP_ASPMS   0x00000c00
#define  PCI_EXP_LNKCAP_DLLLARC 0x00100000
#define  PCI_EXP_LNKCAP_PN      0xff000000
#define PCI_EXP_LNKSTA          0x12
#define  PCI_EXP_LNKSTA_CLS     0x000f
#define  PCI_EXP_LNKSTA_NLW     0x03f0
#define  PCI_EXP_LNKSTA_LT      0x0800
#define  PCI_EXP_LNKSTA_SLC     0x1000
#define  PCI_EXP_LNKSTA_DLLLA   0x2000
#define  PCI_EXP_LNKSTA_LBMS    0x4000
#define  PCI_EXP_LNKSTA_LABS    0x8000
#define LNKSTA_TRERR_LSPCI      0x0400   /* lspci 의 "TrErr" (bit 10, 1.0a 시절 Training Error) — pci_regs.h 에 이름 없음 */
#define IORESOURCE_IO           0x00000100
#define IORESOURCE_MEM          0x00000200

enum { E_OK = 0, E_FORMAT = -1, E_TRUNC = -2, E_ORDER = -3, E_CAP = -4, E_LOOP = -5, E_BADPTR = -6, E_IO = -7 };
#define MAX_CAPS 48
#define MAX_DEVS 64
#define SYSFS_PCI "/sys/bus/pci/devices"

typedef struct { int n, err; uint8_t off[MAX_CAPS], id[MAX_CAPS]; } cap_list;

/* ---------------- solution.c 축약판 도우미 ---------------- */
static uint8_t  cfg_rd8(const uint8_t *c, uint16_t o)  { return c[o]; }
static uint16_t cfg_rd16(const uint8_t *c, uint16_t o) { return (uint16_t)(c[o] | c[o + 1] << 8); }
static uint32_t cfg_rd32(const uint8_t *c, uint16_t o) {
    return (uint32_t)c[o] | (uint32_t)c[o + 1] << 8 | (uint32_t)c[o + 2] << 16 | (uint32_t)c[o + 3] << 24;
}
static int hexval(int c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}
static int hex_tok(const char *s, size_t n, size_t *i, uint64_t *v) {
    size_t k = *i, nd = 0;
    uint64_t x = 0;
    if (k + 1 < n && s[k] == '0' && (s[k + 1] == 'x' || s[k + 1] == 'X')) k += 2;
    while (k < n && hexval(s[k]) >= 0) { if (++nd > 16) return -1; x = x << 4 | (uint64_t)hexval(s[k++]); }
    if (nd == 0) return -1;
    *i = k; *v = x;
    return 0;
}
static int next_line(const char *t, size_t len, size_t *pos, const char **ln, size_t *n) {
    if (*pos >= len) return 0;
    size_t s = *pos, e = s;
    while (e < len && t[e] != '\n') e++;
    *pos = e < len ? e + 1 : e;
    *ln = t + s; *n = e - s;
    while (*n > 0 && (t[s + *n - 1] == '\r' || t[s + *n - 1] == ' ')) (*n)--;
    return 1;
}
static int parse_lspci_x(const char *t, size_t len, uint8_t *cfg, size_t cap) {
    size_t pos = 0, got = 0, n;
    const char *ln;
    int in_data = 0;
    while (next_line(t, len, &pos, &ln, &n)) {
        if (n == 0) { if (in_data) break; continue; }
        size_t j = 0, i = 0;
        uint64_t off;
        while (j < n && hexval(ln[j]) >= 0) j++;
        if (!(j >= 2 && j <= 3 && j + 1 < n && ln[j] == ':' && ln[j + 1] == ' ')) { if (in_data) return E_FORMAT; continue; }
        hex_tok(ln, j, &i, &off);
        if (off != got) return E_ORDER;
        if (got + 16 > cap) return E_CAP;
        for (int k = 0, q = (int)j + 1; k < 16; k++, q += 3) {
            if ((size_t)q + 3 > n || ln[q] != ' ' || hexval(ln[q + 1]) < 0 || hexval(ln[q + 2]) < 0) return E_FORMAT;
            cfg[got + (size_t)k] = (uint8_t)(hexval(ln[q + 1]) << 4 | hexval(ln[q + 2]));
        }
        if (j + 1 + 48 != n) return E_FORMAT;
        got += 16;
        in_data = 1;
    }
    return (got == 64 || got == 256 || got == 4096) ? (int)got : E_TRUNC;
}
static int walk_caps(const uint8_t *cfg, size_t len, cap_list *cl) {
    uint8_t seen[256] = { 0 };
    cl->n = 0; cl->err = E_OK;
    if (len < PCI_STD_HEADER_SIZEOF) return cl->err = E_TRUNC;
    if (!(cfg_rd16(cfg, PCI_STATUS) & PCI_STATUS_CAP_LIST)) return 0;
    for (unsigned p = cfg_rd8(cfg, PCI_CAPABILITY_LIST) & ~3u; p; p = cfg_rd8(cfg, (uint16_t)(p + 1)) & ~3u) {
        if (p < PCI_STD_HEADER_SIZEOF) { cl->err = E_BADPTR; break; }
        if (p + 2 > len) { cl->err = E_TRUNC; break; }
        if (seen[p] || cl->n >= MAX_CAPS) { cl->err = E_LOOP; break; }
        seen[p] = 1;
        cl->off[cl->n] = (uint8_t)p; cl->id[cl->n] = cfg[p]; cl->n++;
    }
    return cl->err ? cl->err : cl->n;
}
static int find_cap(const uint8_t *cfg, size_t len, uint8_t id) {
    cap_list cl;
    walk_caps(cfg, len, &cl);
    for (int i = 0; i < cl.n; i++) if (cl.id[i] == id) return cl.off[i];
    return -1;
}
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
static int run_cmd(const char *cmd, char *out, size_t cap, size_t *n) {
    FILE *f = popen(cmd, "r");
    if (!f) return -1;
    size_t k = fread(out, 1, cap - 1, f);
    out[k] = '\0'; *n = k;
    int st = pclose(f);
    return (st != -1 && WIFEXITED(st)) ? WEXITSTATUS(st) : -1;
}
static int bdf_ok(const char *s) {
    if (strlen(s) != 12 || s[4] != ':' || s[7] != ':' || s[10] != '.') return 0;
    for (int i = 0; i < 12; i++) if (i != 4 && i != 7 && i != 10 && hexval(s[i]) < 0) return 0;
    return 1;
}
static int list_devs(const char *sysroot, char names[][16], int max) {
    DIR *dir = opendir(sysroot);
    struct dirent *e;
    int n = 0;
    if (!dir) return E_IO;
    while ((e = readdir(dir)) != NULL && n < max) if (bdf_ok(e->d_name)) snprintf(names[n++], 16, "%.15s", e->d_name);
    closedir(dir);
    for (int i = 1; i < n; i++)
        for (int j = i; j > 0 && strcmp(names[j - 1], names[j]) > 0; j--) {
            char t[16];
            memcpy(t, names[j], 16); memcpy(names[j], names[j - 1], 16); memcpy(names[j - 1], t, 16);
        }
    return n;
}
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
        for (size_t w = 0; n > 0 && w < (size_t)n;) { ssize_t r = write(fd[1], buf + w, (size_t)n - w); if (r <= 0) _exit(3); w += (size_t)r; }
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
/* sysfs 장치 하나의 config (root 면 256B 이상) */
static int read_cfg(const char *bdf, uint8_t *cfg, size_t cap, size_t *n) {
    char path[128];
    snprintf(path, sizeof path, "%s/%.15s/config", SYSFS_PCI, bdf);
    return read_file(path, cfg, cap, n);
}

/* ================= 연습 1: lspci -tvn 트리 ================= */
typedef struct { int dom, bus, dev, fn; uint16_t vid, did; int is_bridge, sec, sub; } tdev;
typedef struct { char *out; size_t cap, o; } sbuf;

/* pciutils ls-tree.c 의 print_it 과 같은 방식: 줄을 내보낸 뒤 '+','|' → '|', 나머지 → ' ' 로 바꿔 다음 줄의 왼쪽 여백이 된다 */
static void print_it(sbuf *sb, char *line, size_t p) {
    for (size_t i = 0; i < p && sb->o + 1 < sb->cap; i++) sb->out[sb->o++] = line[i];
    if (sb->o + 1 < sb->cap) sb->out[sb->o++] = '\n';
    sb->out[sb->o] = '\0';
    for (size_t i = 0; i < p; i++) line[i] = (line[i] == '+' || line[i] == '|') ? '|' : ' ';
}
static void tree_bus(const tdev *v, int nv, int bus, sbuf *sb, char *line, size_t p, size_t lcap);
static void tree_dev(const tdev *v, int nv, const tdev *d, sbuf *sb, char *line, size_t p, size_t lcap) {
    p += (size_t)snprintf(line + p, lcap - p, "%02x.%x", d->dev, d->fn);
    if (d->is_bridge) {
        if (d->sec == d->sub) p += (size_t)snprintf(line + p, lcap - p, "-[%02x]-", d->sec);
        else p += (size_t)snprintf(line + p, lcap - p, "-[%02x-%02x]-", d->sec, d->sub);
        line[p++] = '-';
        tree_bus(v, nv, d->sec, sb, line, p, lcap);
        return;
    }
    p += (size_t)snprintf(line + p, lcap - p, "  %04x:%04x", d->vid, d->did);
    print_it(sb, line, p);
}
static void tree_bus(const tdev *v, int nv, int bus, sbuf *sb, char *line, size_t p, size_t lcap) {
    int idx[MAX_DEVS], k = 0;
    for (int i = 0; i < nv && k < MAX_DEVS; i++) if (v[i].bus == bus) idx[k++] = i;
    if (p + 48 >= lcap) return;                                    /* 깊이 제한: 줄 버퍼 보호 */
    if (k == 0) { print_it(sb, line, p); return; }
    if (k == 1) { line[p] = '-'; line[p + 1] = '-'; tree_dev(v, nv, &v[idx[0]], sb, line, p + 2, lcap); return; }
    for (int i = 0; i < k; i++) {
        line[p] = i == k - 1 ? '\\' : '+';
        line[p + 1] = '-';
        tree_dev(v, nv, &v[idx[i]], sb, line, p + 2, lcap);
    }
}
static size_t lspci_tree(const tdev *v, int nv, char *out, size_t cap) {
    char line[256];
    sbuf sb = { out, cap, 0 };
    out[0] = '\0';
    if (nv == 0) return 0;
    size_t p = (size_t)snprintf(line, sizeof line, "-[%04x:%02x]-", v[0].dom, 0);
    tree_bus(v, nv, 0, &sb, line, p, sizeof line);
    return sb.o;
}
static int tdev_from_cfg(const char *bdf, const uint8_t *cfg, tdev *t) {
    unsigned dom, bus, dev, fn;
    if (sscanf(bdf, "%4x:%2x:%2x.%1x", &dom, &bus, &dev, &fn) != 4) return E_FORMAT;
    t->dom = (int)dom; t->bus = (int)bus; t->dev = (int)dev; t->fn = (int)fn;
    t->vid = cfg_rd16(cfg, PCI_VENDOR_ID); t->did = cfg_rd16(cfg, PCI_DEVICE_ID);
    t->is_bridge = (cfg_rd8(cfg, PCI_HEADER_TYPE) & PCI_HEADER_TYPE_MASK) == 1;
    t->sec = cfg_rd8(cfg, PCI_SECONDARY_BUS); t->sub = cfg_rd8(cfg, PCI_SUBORDINATE_BUS);
    return 0;
}

/* QEMU 게스트 실측 (capture_qemu.txt): 64B 머리만 */
static const struct { const char *bdf, *txt; } QEMU_HDRS[] = {
    { "0000:00:00.0", "00: 36 1b 08 00 00 00 00 00 00 00 00 06 00 00 00 00\n10: 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00\n"
                      "20: 00 00 00 00 00 00 00 00 00 00 00 00 f4 1a 00 11\n30: 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00\n" },
    { "0000:00:01.0", "00: f4 1a 00 10 07 04 10 00 00 00 00 02 00 00 00 00\n10: 01 20 00 00 00 00 28 10 00 00 00 00 00 00 00 00\n"
                      "20: 0c 00 20 00 80 00 00 00 00 00 00 00 f4 1a 01 00\n30: 00 00 00 00 98 00 00 00 00 00 00 00 11 01 00 00\n" },
    { "0000:00:02.0", "00: 36 1b 0c 00 07 04 10 00 00 00 04 06 00 00 01 00\n10: 00 10 28 10 00 00 00 00 00 01 01 00 10 10 00 00\n"
                      "20: 00 10 10 10 01 00 11 00 80 00 00 00 80 00 00 00\n30: 00 00 00 00 54 00 00 00 00 00 00 00 0f 01 02 00\n" },
    { "0000:01:00.0", "00: 36 1b 10 00 00 00 10 00 02 02 08 01 00 00 00 00\n10: 04 00 00 10 00 00 00 00 00 00 00 00 00 00 00 00\n"
                      "20: 00 00 00 00 00 00 00 00 00 00 00 00 f4 1a 00 11\n30: 00 00 00 00 40 00 00 00 00 00 00 00 00 01 00 00\n" },
};
/* VM 의 lspci -F <dump> -tvn 실제 출력 */
static const char QEMU_TREE[] =
    "-[0000:00]-+-00.0  1b36:0008\n"
    "           +-01.0  1af4:1000\n"
    "           \\-02.0-[01]----00.0  1b36:0010\n";

/* ================= 연습 2: LnkCap / LnkSta ================= */
static const char *link_speed(unsigned code) {
    static const char *s[] = { "unknown", "2.5GT/s", "5GT/s", "8GT/s", "16GT/s", "32GT/s", "64GT/s" };
    return code < 7 ? s[code] : "unknown";
}
/* pciutils ls-caps.c 의 규칙: sta > cap → overdriven, 같으면 "", 아래쪽을 향한 포트(root port 4, downstream port 6, PCI/PCI-X→PCIe bridge 8)는 표시 안 함 */
static const char *link_compare(int type, unsigned sta, unsigned cap) {
    if (sta > cap) return " (overdriven)";
    if (sta == cap) return "";
    if (type == PCI_EXP_TYPE_ROOT_PORT || type == PCI_EXP_TYPE_DOWNSTREAM || type == PCI_EXP_TYPE_PCIE_BRIDGE) return "";
    return " (downgraded)";
}
static void fmt_lnkcap(uint32_t cap, char *out, size_t n) {
    static const char *aspm[] = { "not supported", "L0s", "L1", "L0s L1" };
    snprintf(out, n, "LnkCap:\tPort #%u, Speed %s, Width x%u, ASPM %s", (unsigned)(cap >> 24),
             link_speed(cap & PCI_EXP_LNKCAP_SLS), (unsigned)((cap & PCI_EXP_LNKCAP_MLW) >> 4),
             aspm[(cap & PCI_EXP_LNKCAP_ASPMS) >> 10]);
}
static void fmt_lnksta(int type, uint32_t cap, uint16_t sta, char *out, size_t n) {
    unsigned cs = sta & PCI_EXP_LNKSTA_CLS, cw = (sta & PCI_EXP_LNKSTA_NLW) >> 4;
#define PM(b) ((sta & (b)) ? '+' : '-')
    snprintf(out, n, "LnkSta:\tSpeed %s%s, Width x%u%s\n\t\t\tTrErr%c Train%c SlotClk%c DLActive%c BWMgmt%c ABWMgmt%c",
             link_speed(cs), link_compare(type, cs, cap & PCI_EXP_LNKCAP_SLS), cw,
             link_compare(type, cw, (cap & PCI_EXP_LNKCAP_MLW) >> 4), PM(LNKSTA_TRERR_LSPCI), PM(PCI_EXP_LNKSTA_LT),
             PM(PCI_EXP_LNKSTA_SLC), PM(PCI_EXP_LNKSTA_DLLLA), PM(PCI_EXP_LNKSTA_LBMS), PM(PCI_EXP_LNKSTA_LABS));
#undef PM
}
/* 판정: 기대 속도·폭 = min(포트 cap, 장치 cap). 포트 쪽 LnkSta(+DLActive)를 본다. */
enum { LK_OK = 0, LK_DOWN = 1, LK_SPEED = 2, LK_WIDTH = 4 };
static int link_triage(uint32_t port_cap, uint16_t port_sta, uint32_t ep_cap) {
    if ((port_cap & PCI_EXP_LNKCAP_DLLLARC) && !(port_sta & PCI_EXP_LNKSTA_DLLLA)) return LK_DOWN;
    unsigned ws = (port_sta & PCI_EXP_LNKSTA_NLW) >> 4, ss = port_sta & PCI_EXP_LNKSTA_CLS;
    if (ws == 0) return LK_DOWN;
    unsigned es = (port_cap & 0xF) < (ep_cap & 0xF) ? (port_cap & 0xF) : (ep_cap & 0xF);
    unsigned ew = ((port_cap & PCI_EXP_LNKCAP_MLW) >> 4) < ((ep_cap & PCI_EXP_LNKCAP_MLW) >> 4)
                      ? (port_cap & PCI_EXP_LNKCAP_MLW) >> 4 : (ep_cap & PCI_EXP_LNKCAP_MLW) >> 4;
    return (ss < es ? LK_SPEED : 0) | (ws < ew ? LK_WIDTH : 0);
}
/* QEMU 실측 PCIe cap 근처 (capture_qemu.txt): 루트 포트 00:02.0 은 0x54, NVMe 01:00.0 은 0x80 */
static const char QEMU_RP_TXT[] =
    "00: 36 1b 0c 00 07 04 10 00 00 00 04 06 00 00 01 00\n10: 00 10 28 10 00 00 00 00 00 01 01 00 10 10 00 00\n"
    "20: 00 10 10 10 01 00 11 00 80 00 00 00 80 00 00 00\n30: 00 00 00 00 54 00 00 00 00 00 00 00 0f 01 02 00\n"
    "40: 0d 00 00 00 36 1b 00 00 11 40 00 80 00 00 00 00\n50: 00 08 00 00 10 48 42 01 20 80 00 00 0f 00 00 00\n"
    "60: 04 06 30 00 00 00 11 20 7b 00 0a 00 c0 03 50 00\n70: 00 00 00 00 00 00 00 00 20 00 30 00 00 00 00 00\n"
    "80: 1e 00 00 00 04 00 00 00 00 00 00 00 00 00 00 00\n90: 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00\n"
    "a0: 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00\nb0: 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00\n"
    "c0: 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00\nd0: 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00\n"
    "e0: 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00\nf0: 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00\n";
static const char QEMU_EP_TXT[] =
    "00: 36 1b 10 00 00 00 10 00 02 02 08 01 00 00 00 00\n10: 04 00 00 10 00 00 00 00 00 00 00 00 00 00 00 00\n"
    "20: 00 00 00 00 00 00 00 00 00 00 00 00 f4 1a 00 11\n30: 00 00 00 00 40 00 00 00 00 00 00 00 00 01 00 00\n"
    "40: 11 80 40 00 00 20 00 00 00 30 00 00 00 00 00 00\n50: 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00\n"
    "60: 01 00 03 00 08 00 00 00 00 00 00 00 00 00 00 00\n70: 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00\n"
    "80: 10 60 02 00 20 80 00 10 00 00 00 00 11 04 00 00\n90: 00 00 11 00 00 00 00 00 00 00 00 00 00 00 00 00\n"
    "a0: 00 00 00 00 00 00 30 00 00 00 00 00 00 00 00 00\nb0: 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00\n"
    "c0: 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00\nd0: 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00\n"
    "e0: 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00\nf0: 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00\n";

/* ================= 연습 4: /proc/iomem ================= */
/* "  380020000-38002ffff : 0000:00:06.0" 같은 줄에서 start-end 와 이름이 모두 맞는 줄이 있나 */
static int iomem_has(const char *t, size_t len, uint64_t s, uint64_t e, const char *name) {
    size_t pos = 0, n;
    const char *ln;
    while (next_line(t, len, &pos, &ln, &n)) {
        size_t i = 0;
        uint64_t a, b;
        while (i < n && ln[i] == ' ') i++;
        if (hex_tok(ln, n, &i, &a) < 0 || i >= n || ln[i++] != '-' || hex_tok(ln, n, &i, &b) < 0) continue;
        if (i + 3 > n || memcmp(ln + i, " : ", 3) != 0) continue;
        i += 3;
        if (a == s && b == e && n - i == strlen(name) && memcmp(ln + i, name, n - i) == 0) return 1;
    }
    return 0;
}
static int iomem_all_zero(const char *t, size_t len, int *lines) {
    size_t pos = 0, n;
    const char *ln;
    int ok = 1;
    *lines = 0;
    while (next_line(t, len, &pos, &ln, &n)) {
        size_t i = 0;
        uint64_t a, b;
        while (i < n && ln[i] == ' ') i++;
        if (hex_tok(ln, n, &i, &a) < 0 || i >= n || ln[i++] != '-' || hex_tok(ln, n, &i, &b) < 0) continue;
        (*lines)++;
        if (a || b) ok = 0;
    }
    return ok && *lines > 0;
}

/* ---------------- 테스트 하네스 ---------------- */
static int g_pass, g_total;
#define CHECK(name, expr) do { g_total++; if (expr) { g_pass++; printf("PASS %s\n", name); } \
    else printf("FAIL %s: %s (line %d)\n", name, #expr, __LINE__); } while (0)
#define SZ(s) (sizeof(s) - 1)

int main(void) {
    static uint8_t cfg[4096], rp[4096], ep[4096];
    static char out[16384], txt[65536], names[MAX_DEVS][16];
    static tdev tv[MAX_DEVS];
    char buf[256];
    size_t n;
    int nv = 0;

    /* ---- 연습 1: QEMU 토폴로지 (실측 덤프) ---- */
    for (size_t i = 0; i < sizeof QEMU_HDRS / sizeof QEMU_HDRS[0]; i++) {
        if (parse_lspci_x(QEMU_HDRS[i].txt, strlen(QEMU_HDRS[i].txt), cfg, sizeof cfg) == 64 &&
            tdev_from_cfg(QEMU_HDRS[i].bdf, cfg, &tv[nv]) == 0) nv++;
    }
    lspci_tree(tv, nv, out, sizeof out);
    printf("%s", out);
    CHECK("P1 QEMU: root port 00:02.0 (Type 1, sec=sub=01) → tree == lspci -F -tvn", nv == 4 && strcmp(out, QEMU_TREE) == 0);
    tv[2].sub = 2;                                                  /* 하위 버스 범위가 넓으면 "[01-02]" */
    lspci_tree(tv, nv, out, sizeof out);
    CHECK("P1 bridge with sec 01 sub 02 prints -[01-02]-", strstr(out, "\\-02.0-[01-02]----00.0  1b36:0010\n") != NULL);
    tv[3].bus = 5;                                                  /* 01 버스가 비면 "02.0-[01-02]--" 에서 끝 */
    lspci_tree(tv, nv, out, sizeof out);
    CHECK("P1 empty secondary bus ends the branch", strstr(out, "\\-02.0-[01-02]--\n") != NULL);

    /* ---- 연습 1: 이 VM (sysfs) vs lspci -tvn ---- */
    int is_root = geteuid() == 0;
    int ndev = list_devs(SYSFS_PCI, names, MAX_DEVS);
    nv = 0;
    for (int i = 0; i < ndev; i++)
        if (read_cfg(names[i], cfg, sizeof cfg, &n) == 0 && n >= 64 && tdev_from_cfg(names[i], cfg, &tv[nv]) == 0) nv++;
    lspci_tree(tv, nv, out, sizeof out);
    int rc = run_cmd("lspci -tvn 2>&1", txt, sizeof txt, &n);
    printf("%s", out);
    CHECK("P1 VM: our tree from sysfs == lspci -tvn (exact text)", ndev > 0 && nv == ndev && rc == 0 && strcmp(out, txt) == 0);

    /* ---- 연습 2: LnkCap/LnkSta (QEMU 실측) ---- */
    int okp = parse_lspci_x(QEMU_RP_TXT, SZ(QEMU_RP_TXT), rp, sizeof rp) == 256;
    int oke = parse_lspci_x(QEMU_EP_TXT, SZ(QEMU_EP_TXT), ep, sizeof ep) == 256;
    int xr = find_cap(rp, 256, PCI_CAP_ID_EXP), xe = find_cap(ep, 256, PCI_CAP_ID_EXP);
    CHECK("P2 PCIe cap found: root port at 0x54, NVMe at 0x80", okp && oke && xr == 0x54 && xe == 0x80);
    if (xr > 0 && xe > 0) {
        int trp = (cfg_rd16(rp, (uint16_t)(xr + PCI_EXP_FLAGS)) & PCI_EXP_FLAGS_TYPE) >> 4;
        int tep = (cfg_rd16(ep, (uint16_t)(xe + PCI_EXP_FLAGS)) & PCI_EXP_FLAGS_TYPE) >> 4;
        uint32_t crp = cfg_rd32(rp, (uint16_t)(xr + PCI_EXP_LNKCAP)), cep = cfg_rd32(ep, (uint16_t)(xe + PCI_EXP_LNKCAP));
        uint16_t srp = cfg_rd16(rp, (uint16_t)(xr + PCI_EXP_LNKSTA)), sep = cfg_rd16(ep, (uint16_t)(xe + PCI_EXP_LNKSTA));
        printf("   QEMU root port: type %d LnkCap %08x LnkSta %04x | NVMe: type %d LnkCap %08x LnkSta %04x\n", trp, crp, srp, tep, cep, sep);
        CHECK("P2 types: root port = 4, NVMe = 0 (Endpoint)", trp == PCI_EXP_TYPE_ROOT_PORT && tep == PCI_EXP_TYPE_ENDPOINT);
        fmt_lnkcap(crp, buf, sizeof buf);
        CHECK("P2 RP LnkCap == lspci 'Port #0, Speed 16GT/s, Width x32, ASPM L0s'",
              strcmp(buf, "LnkCap:\tPort #0, Speed 16GT/s, Width x32, ASPM L0s") == 0);
        fmt_lnksta(trp, crp, srp, buf, sizeof buf);
        CHECK("P2 RP LnkSta == lspci 'Speed 2.5GT/s, Width x1' (no '(downgraded)' on a root port) + DLActive+",
              strcmp(buf, "LnkSta:\tSpeed 2.5GT/s, Width x1\n\t\t\tTrErr- Train- SlotClk- DLActive+ BWMgmt- ABWMgmt-") == 0);
        fmt_lnkcap(cep, buf, sizeof buf);
        CHECK("P2 NVMe LnkCap == lspci 'Port #0, Speed 2.5GT/s, Width x1, ASPM L0s'",
              strcmp(buf, "LnkCap:\tPort #0, Speed 2.5GT/s, Width x1, ASPM L0s") == 0);
        fmt_lnksta(tep, cep, sep, buf, sizeof buf);
        CHECK("P2 NVMe LnkSta == lspci 'Speed 2.5GT/s, Width x1' + DLActive- (endpoint, not capable)",
              strcmp(buf, "LnkSta:\tSpeed 2.5GT/s, Width x1\n\t\t\tTrErr- Train- SlotClk- DLActive- BWMgmt- ABWMgmt-") == 0);
        CHECK("P2 triage QEMU: RP x32 Gen4 cap but EP x1 Gen1 → expected min = 2.5GT/s x1 → OK",
              link_triage(crp, srp, cep) == LK_OK);
    }
    /* 가상 덤프 (capture_variants.txt, make_variants.py) — lspci -F 가 실제로 찍은 문자열과 비교 */
    {
        char b2[256];
        fmt_lnksta(PCI_EXP_TYPE_ENDPOINT, 0x00000844, 0x0024, buf, sizeof buf);
        fmt_lnksta(PCI_EXP_TYPE_ROOT_PORT, 0x00300444, 0x2024, b2, sizeof b2);
        CHECK("P2 x2 variant: EP 'Speed 16GT/s, Width x2 (downgraded)', RP 'Speed 16GT/s, Width x2'",
              strncmp(buf, "LnkSta:\tSpeed 16GT/s, Width x2 (downgraded)\n", 44) == 0 &&
              strncmp(b2, "LnkSta:\tSpeed 16GT/s, Width x2\n", 31) == 0);
        CHECK("P2 x2 triage → WIDTH", link_triage(0x00300444, 0x2024, 0x00000844) == LK_WIDTH);
        fmt_lnksta(PCI_EXP_TYPE_ENDPOINT, 0x00000844, 0x0041, buf, sizeof buf);
        CHECK("P2 gen1 variant: EP 'Speed 2.5GT/s (downgraded), Width x4'",
              strncmp(buf, "LnkSta:\tSpeed 2.5GT/s (downgraded), Width x4\n", 45) == 0);
        CHECK("P2 gen1 triage → SPEED", link_triage(0x00300444, 0x2041, 0x00000844) == LK_SPEED);
        fmt_lnksta(PCI_EXP_TYPE_ROOT_PORT, 0x00300604, 0x0001, buf, sizeof buf);
        CHECK("P2 dldown variant: RP 'Speed 2.5GT/s, Width x0' + DLActive-",
              strcmp(buf, "LnkSta:\tSpeed 2.5GT/s, Width x0\n\t\t\tTrErr- Train- SlotClk- DLActive- BWMgmt- ABWMgmt-") == 0);
        CHECK("P2 dldown triage → DOWN", link_triage(0x00300604, 0x0001, 0x00000411) == LK_DOWN);
        CHECK("P2 Gen4 x4 both sides at Gen3 x2 → SPEED|WIDTH", link_triage(0x00300444, 0x2023, 0x00000844) == (LK_SPEED | LK_WIDTH));
        fmt_lnksta(PCI_EXP_TYPE_ENDPOINT, 0x00000842, 0x0044, buf, sizeof buf);
        CHECK("P2 over variant: sta 16GT/s > cap 5GT/s → '(overdriven)'", strncmp(buf, "LnkSta:\tSpeed 16GT/s (overdriven), Width x4\n", 44) == 0);
    }

    /* ---- 연습 3: 필터 ---- */
    {
        char mine[2048] = "", lsp[2048] = "";
        size_t m = 0, l = 0, pos = 0, ln_n;
        const char *ln;
        int cnt = 0;
        for (int i = 0; i < ndev; i++)
            if (read_cfg(names[i], cfg, sizeof cfg, &n) == 0 && n >= 64 && (cfg_rd32(cfg, PCI_CLASS_REVISION) >> 16) == 0x0180) {
                m += (size_t)snprintf(mine + m, sizeof mine - m, "%s\n", names[i] + 5);
                cnt++;
            }
        rc = run_cmd("lspci -n -d ::0180 2>&1", txt, sizeof txt, &n);
        while (next_line(txt, n, &pos, &ln, &ln_n)) if (ln_n >= 7) l += (size_t)snprintf(lsp + l, sizeof lsp - l, "%.7s\n", ln);
        printf("   class 0180 (ours): %d → %.*s\n", cnt, (int)strcspn(mine, "\n"), mine);
        CHECK("P3 class ::0180 (mass storage 'other'): our sysfs scan == lspci -n -d ::0180", rc == 0 && cnt > 0 && strcmp(mine, lsp) == 0);
        m = l = pos = 0; mine[0] = lsp[0] = '\0'; cnt = 0;
        for (int i = 0; i < ndev; i++)
            if (read_cfg(names[i], cfg, sizeof cfg, &n) == 0 && n >= 64 && cfg_rd16(cfg, PCI_VENDOR_ID) == 0x1af4) {
                m += (size_t)snprintf(mine + m, sizeof mine - m, "%s\n", names[i] + 5);
                cnt++;
            }
        rc = run_cmd("lspci -n -d 1af4: 2>&1", txt, sizeof txt, &n);
        while (next_line(txt, n, &pos, &ln, &ln_n)) if (ln_n >= 7) l += (size_t)snprintf(lsp + l, sizeof lsp - l, "%.7s\n", ln);
        CHECK("P3 vendor 1af4: (virtio): our sysfs scan == lspci -n -d 1af4:", rc == 0 && cnt > 0 && strcmp(mine, lsp) == 0);
    }

    /* ---- 연습 4: /proc/iomem ---- */
    CHECK("P4 running as root (iomem addresses visible)", is_root);
    if (is_root) {
        size_t in = 0;
        int bars = 0, found = 0;
        if (read_file("/proc/iomem", (uint8_t *)txt, sizeof txt - 1, &in) != 0) in = 0;
        for (int i = 0; i < ndev; i++) {
            char path[128], rtxt[4096];
            size_t rn, pos = 0, ln_n;
            const char *ln;
            snprintf(path, sizeof path, "%s/%.15s/resource", SYSFS_PCI, names[i]);
            if (read_file(path, (uint8_t *)rtxt, sizeof rtxt, &rn) != 0) continue;
            for (int b = 0; b < PCI_STD_NUM_BARS && next_line(rtxt, rn, &pos, &ln, &ln_n); b++) {
                size_t k = 0;
                uint64_t s, e, f;
                if (hex_tok(ln, ln_n, &k, &s) || ++k > ln_n || hex_tok(ln, ln_n, &k, &e) || ++k > ln_n || hex_tok(ln, ln_n, &k, &f)) break;
                if (!(f & IORESOURCE_MEM) || (s == 0 && e == 0)) continue;
                bars++;
                if (iomem_has(txt, in, s, e, names[i])) found++;
                else printf("   %s BAR%d %llx-%llx not in /proc/iomem\n", names[i], b, (unsigned long long)s, (unsigned long long)e);
            }
        }
        printf("   /proc/iomem: %d/%d memory BARs claimed as '<start>-<end> : <BDF>'\n", found, bars);
        CHECK("P4 every memory BAR in sysfs resource appears in /proc/iomem under its BDF", bars > 0 && found == bars);
        long un = read_as_nobody("/proc/iomem", (uint8_t *)txt, sizeof txt - 1);
        int lines = 0;
        CHECK("P4 /proc/iomem as nobody: every address reads as 0 (kernel hides them)",
              un > 0 && iomem_all_zero(txt, (size_t)un, &lines));
        printf("   nobody saw %d iomem lines, all 00000000-00000000\n", lines);
    }

    printf("%d/%d passed\n", g_pass, g_total);
    return g_pass == g_total ? 0 : 1;
}
