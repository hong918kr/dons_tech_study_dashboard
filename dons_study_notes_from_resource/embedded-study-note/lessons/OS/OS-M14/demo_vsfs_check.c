/* OS-M14 데모: OSTEP vsfs.py 출력(-c)을 표준 입력으로 받아, 연산(mkdir/creat/link/unlink/write)을 우리 VSFS 에
 * 그대로 다시 실행하고, 매 상태마다 inode bitmap / inodes / data bitmap 줄을 글자 그대로, data 줄은 디렉터리 칸을
 * 정렬해서 비교한다 (vsfs.py 는 지운 칸을 당겨 붙이고 우리는 빈 칸을 재사용하므로 순서만 다를 수 있음).
 * 우리 배치는 inode 16 의 배수 → vsfs.py 에 -i 16 -d 16 을 주고, 디스크 = 3 + 1 + 16 = 20 블록.
 * build: cc -std=c11 -Wall -Wextra -Werror -O2 demo_vsfs_check.c -o demo_vsfs_check
 * run:   python3 -I <ostep-homework>/file-implementation/vsfs.py -s 1 -n 40 -i 16 -d 16 -c | ./demo_vsfs_check
 */
#define VSFS_NO_MAIN
#include "solution.c"

static size_t cat(char *o, size_t n, size_t cap, const char *s) {
    size_t k = strlen(s);
    if (n + k >= cap) k = cap - n - 1;
    memcpy(o + n, s, k); o[n + k] = 0;
    return n + k;
}
static int cmp_str(const void *a, const void *b) { return strcmp(*(char *const *)a, *(char *const *)b); }
/* "[(.,0) (..,0) (u,1)]" 안의 칸을 정렬, 파일 내용 "[x]" 는 "[*]" 로 */
static size_t norm_group(char *o, size_t n, size_t cap, const char *g, size_t len) {
    char tmp[1024], *ent[160];
    int ne = 0;
    if (len >= sizeof tmp) len = sizeof tmp - 1;
    memcpy(tmp, g, len); tmp[len] = 0;
    if (len == 0) return cat(o, n, cap, "[]");
    if (tmp[0] != '(') return cat(o, n, cap, "[*]");
    for (char *t = strtok(tmp, " "); t && ne < 160; t = strtok(NULL, " ")) ent[ne++] = t;
    qsort(ent, (size_t)ne, sizeof ent[0], cmp_str);
    n = cat(o, n, cap, "[");
    for (int i = 0; i < ne; i++) { if (i) n = cat(o, n, cap, " "); n = cat(o, n, cap, ent[i]); }
    return cat(o, n, cap, "]");
}
static void norm_data(const char *line, char *o, size_t cap) {
    size_t n = 0; o[0] = 0;
    for (const char *p = strchr(line, '['); p; p = strchr(p + 1, '[')) {
        const char *q = strchr(p, ']');
        if (!q) break;
        n = norm_group(o, n, cap, p + 1, (size_t)(q - p - 1));
        p = q;
    }
}
/* 우리 상태를 vsfs.py 형식으로 */
static void dump_line(const char *which, char *o, size_t cap) {
    size_t n = 0; char t[160];
    op_begin(); o[0] = 0;
    if (!strcmp(which, "ibm")) for (uint32_t i = 0; i < SB.ninodes; i++) n = cat(o, n, cap, bm_get(SB.ibitmap, i) ? "1" : "0");
    if (!strcmp(which, "dbm")) for (uint32_t i = 0; i < SB.ndata; i++) n = cat(o, n, cap, bm_get(SB.dbitmap, i) ? "1" : "0");
    if (!strcmp(which, "inodes"))
        for (uint32_t i = 0; i < SB.ninodes; i++) {
            dinode_t d; iget(i, &d);
            if (d.type == T_FREE) snprintf(t, sizeof t, "[]");
            else snprintf(t, sizeof t, "[%c a:%d r:%u]", d.type == T_DIR ? 'd' : 'f',
                          d.direct[0] ? (int)(d.direct[0] - SB.data_start) : -1, d.nlink);
            n = cat(o, n, cap, t);
        }
    if (!strcmp(which, "data")) {
        char raw[4096]; size_t rn = 0; raw[0] = 0;
        for (uint32_t b = 0; b < SB.ndata; b++) {
            int owner_dir = -1, owner_file = 0;
            if (bm_get(SB.dbitmap, b))
                for (uint32_t i = 0; i < SB.ninodes; i++) {
                    dinode_t d; iget(i, &d);
                    if (d.type != T_FREE && d.direct[0] == SB.data_start + b) { if (d.type == T_DIR) owner_dir = (int)i; else owner_file = 1; }
                }
            rn = cat(raw, rn, sizeof raw, "[");
            if (owner_dir >= 0) {
                dinode_t d; iget((uint32_t)owner_dir, &d);
                const uint8_t *p = bget(d.direct[0]);
                int first = 1;
                for (uint32_t s = 0; s < d.size / DIRENT_SIZE; s++) {
                    dirent_t e; memcpy(&e, p + s * DIRENT_SIZE, sizeof e);
                    if (!e.name[0]) continue;
                    snprintf(t, sizeof t, "%s(%s,%u)", first ? "" : " ", e.name, e.inum); first = 0;
                    rn = cat(raw, rn, sizeof raw, t);
                    p = bget(d.direct[0]);
                }
            } else if (owner_file) rn = cat(raw, rn, sizeof raw, "x");
            rn = cat(raw, rn, sizeof raw, "]");
        }
        norm_data(raw, o, cap);
    }
}
static void arg(const char *line, int k, char *out, size_t cap) {      /* k 번째 "..." 문자열 */
    const char *p = line;
    for (int i = 0; i <= k; i++) { p = strchr(p, '"'); if (!p) { out[0] = 0; return; } if (i < k) p = strchr(p + 1, '"') + 1; }
    const char *q = strchr(p + 1, '"');
    size_t n = (size_t)(q - p - 1) < cap ? (size_t)(q - p - 1) : cap - 1;
    memcpy(out, p + 1, n); out[n] = 0;
}
int main(void) {
    static char line[8192], ours[4096], theirs[4096];
    static uint8_t blk[BSIZE];
    if (disk_open(20) != 0 || fs_mkfs(16) != 0) return 2;
    int ops = 0, states = 0, mismatch = 0, failed_ops = 0;
    while (fgets(line, sizeof line, stdin)) {
        line[strcspn(line, "\n")] = 0;
        char a[64], b[64];
        int rc = 0, isop = 1;
        if (!strncmp(line, "mkdir(", 6)) { arg(line, 0, a, sizeof a); rc = fs_mkdir(a); }
        else if (!strncmp(line, "creat(", 6)) { arg(line, 0, a, sizeof a); rc = fs_create(a); }
        else if (!strncmp(line, "unlink(", 7)) { arg(line, 0, a, sizeof a); rc = fs_unlink(a); }
        else if (!strncmp(line, "link(", 5)) { arg(line, 0, a, sizeof a); arg(line, 1, b, sizeof b); rc = fs_link(a, b); }
        else if (!strncmp(line, "fd=open(", 8)) {
            dinode_t d; arg(line, 0, a, sizeof a);
            int i = fs_namei(a, &d);
            rc = i < 0 ? i : (int)fs_write((uint32_t)i, d.size, blk, BSIZE);
        } else isop = 0;
        if (isop) { ops++; if (rc < 0) { failed_ops++; printf("op failed (%d): %s\n", rc, line); } continue; }
        const char *which = !strncmp(line, "inode bitmap", 12) ? "ibm" : !strncmp(line, "inodes", 6) ? "inodes"
                          : !strncmp(line, "data bitmap", 11) ? "dbm" : !strncmp(line, "data ", 5) ? "data" : NULL;
        if (!which) continue;
        const char *v = line + strcspn(line, "01[");
        if (!strcmp(which, "data")) norm_data(v, theirs, sizeof theirs);
        else snprintf(theirs, sizeof theirs, "%s", v);
        dump_line(which, ours, sizeof ours);
        if (!strcmp(which, "ibm")) states++;
        if (strcmp(ours, theirs) != 0) { mismatch++; printf("MISMATCH after op %d (%s)\n  vsfs.py: %s\n  ours   : %s\n", ops, which, theirs, ours); }
    }
    fsck_t r;
    int problems = fsck(0, &r);
    printf("replayed %d ops, compared %d states x 4 lines: %d mismatches, %d failed ops; fsck problems=%d\n",
           ops, states, mismatch, failed_ops, problems);
    disk_close();
    return mismatch || failed_ops ? 1 : 0;
}
