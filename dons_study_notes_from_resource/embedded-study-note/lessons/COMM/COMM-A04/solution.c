/* COMM-A04 Ethernet/IPv4/UDP 프레임 파서와 체크섬 — pcapparse
 *
 * 입력: classic pcap (24 B 전역 헤더 + 레코드마다 16 B 헤더 + caplen 바이트). linktype 1 = Ethernet.
 *       magic 0xA1B2C3D4(µs) / 0xA1B23C4D(ns), 파일 바이트 순서는 LE·BE 둘 다 → 호스트 엔디언과 무관하게 바이트로 읽는다.
 * 계층: Ethernet II (+ 802.1Q 0x8100 / 802.1ad 0x88A8 태그 최대 2개) → IPv4 (IHL·옵션·total length·헤더 체크섬·조각)
 *       → UDP (길이, 의사 헤더(pseudo-header) 체크섬; 0 = 계산 안 함; 계산 결과 0 은 0xFFFF 로 보냄).
 * 원칙: 모든 길이를 caplen(캡처된 바이트)·wire len(원래 길이)과 대조한다. caplen 밖은 절대 읽지 않는다(ASan 으로 확인).
 *       "캡처가 잘림"(PE_TRUNC, CS_TRUNC) 과 "패킷이 틀림"(PE_IP_TOTLEN 등) 을 구분한다.
 *
 * Part A: csum_add / csum_fold (RFC 1071), pcap_open / pcap_next, parse_l2 (Ethernet + VLAN)
 * Part B: parse_ipv4 (검증 순서, 체크섬, 조각), parse_udp (길이, 의사 헤더 체크섬, offload 부분합 판별)
 * Part C: csum_update16 (RFC 1624 증분 갱신), ip_ttl_dec (라우터의 TTL 감소), nat_rewrite (src IP/port 재작성)
 *
 * 빌드:  cc -std=c11 -Wall -Wextra -Werror -O2 solution.c -o solution && ./solution        (테스트)
 * 도구:  ./solution dump file.pcap      한 줄씩 해독 + 요약 (verify_pcap.py dump 와 같은 형식)
 *        ./solution vectors out.pcap    시험 벡터 17개를 pcap 으로 쓰기 (verify_pcap.py build 와 바이트 단위 비교)
 */
#include <stdarg.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ---------------- 바이트 순서 도우미 (주어짐) ---------------- */
static uint16_t rd16be(const uint8_t *p) { return (uint16_t)((p[0] << 8) | p[1]); }
static uint32_t rd32be(const uint8_t *p) { return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) | ((uint32_t)p[2] << 8) | p[3]; }
static uint32_t rd32le(const uint8_t *p) { return ((uint32_t)p[3] << 24) | ((uint32_t)p[2] << 16) | ((uint32_t)p[1] << 8) | p[0]; }
static uint16_t rd16le(const uint8_t *p) { return (uint16_t)((p[1] << 8) | p[0]); }
static void wr16be(uint8_t *p, uint16_t v) { p[0] = (uint8_t)(v >> 8); p[1] = (uint8_t)v; }
static void wr32be(uint8_t *p, uint32_t v) { p[0] = (uint8_t)(v >> 24); p[1] = (uint8_t)(v >> 16); p[2] = (uint8_t)(v >> 8); p[3] = (uint8_t)v; }
static void wr16le(uint8_t *p, uint16_t v) { p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8); }
static void wr32le(uint8_t *p, uint32_t v) { p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8); p[2] = (uint8_t)(v >> 16); p[3] = (uint8_t)(v >> 24); }

/* ---------------- Part A: 인터넷 체크섬 (RFC 1071) ---------------- */

/* 16비트 big-endian 워드의 1의 보수 합(one's complement sum)을 sum 에 더해, 접힌(folded) 16비트 값으로 돌려준다.
 * 홀수 길이면 마지막 바이트를 상위 바이트로 보고 하위에 0 을 덧댄 것처럼 더한다 (p[n] 은 읽지 않는다!).
 * 이어 붙여 부를 때 홀수 길이 조각은 마지막에만 와야 한다. 전제: sum <= 0xFFFF, n <= 128 KiB. */
static uint32_t csum_add(uint32_t sum, const uint8_t *p, size_t n) {
    /* TODO A1: 2바이트씩 big-endian 으로 더하고, 홀수 꼬리는 (p[n-1] << 8), 끝에서 캐리를 접는다 */
    size_t i;
    for (i = 0; i + 1 < n; i += 2) sum += rd16be(p + i);
    if (n & 1u) sum += (uint32_t)p[n - 1] << 8;
    while (sum >> 16) sum = (sum & 0xFFFFu) + (sum >> 16);
    return sum;
}

/* 접고(end-around carry) 1의 보수를 취한 값 = 헤더에 넣을 체크섬. 검증할 때는 체크섬 필드까지 더한 합 → 0 이면 정상. */
static uint16_t csum_fold(uint32_t sum) {
    /* TODO A2 */
    while (sum >> 16) sum = (sum & 0xFFFFu) + (sum >> 16);
    return (uint16_t)~sum;
}

/* ---------------- Part A: pcap 리더 ---------------- */

#define PCAP_MAX_CAPLEN 262144u        /* 이보다 큰 caplen 은 깨진 파일로 본다 (tcpdump 기본 snaplen 과 같음) */
#define LINKTYPE_ETHERNET 1u

enum { PCAP_OK = 0, PCAP_EOF = 1, PCAP_E_SHORT = -1, PCAP_E_MAGIC = -2, PCAP_E_VERSION = -3, PCAP_E_LINKTYPE = -4,
       PCAP_E_TRUNC_HDR = -5, PCAP_E_TRUNC_DATA = -6, PCAP_E_CAPLEN = -7 };

typedef struct {
    const uint8_t *buf;
    size_t   n, off;             /* 파일 크기, 다음 레코드 위치 */
    bool     swap;               /* true = 파일이 big-endian 으로 쓰임 */
    bool     nano;               /* true = 타임스탬프 소수부가 ns */
    uint16_t vmaj, vmin;
    uint32_t snaplen;
    uint32_t linktype;           /* LinkType 필드의 하위 16비트 */
    uint32_t link_extra;         /* 상위 16비트 (FCS 정보 등 — 이 도구는 보고만 한다) */
} pcap_reader;

typedef struct {
    uint32_t ts_sec, ts_frac;    /* frac = µs 또는 ns (reader.nano) */
    uint32_t caplen, len;        /* 캡처된 바이트 / 원래 프레임 길이 */
    const uint8_t *data;         /* buf 안을 가리킴 (caplen 바이트) */
} pcap_rec;

static uint32_t pcap_u32(const pcap_reader *r, const uint8_t *p) { return r->swap ? rd32be(p) : rd32le(p); }
static uint16_t pcap_u16(const pcap_reader *r, const uint8_t *p) { return r->swap ? rd16be(p) : rd16le(p); }

/* 24 B 전역 헤더 해석. 반환 PCAP_OK 또는 PCAP_E_*. */
static int pcap_open(pcap_reader *r, const uint8_t *buf, size_t n) {
    memset(r, 0, sizeof *r);
    r->buf = buf; r->n = n;
    /* TODO A3: 길이 >= 24, magic 4종(LE/BE × µs/ns), version 2.x, snaplen, linktype(하위 16비트 == 1) */
    if (n < 24) return PCAP_E_SHORT;
    uint32_t m = rd32le(buf);
    if (m == 0xA1B2C3D4u)      { r->swap = false; r->nano = false; }
    else if (m == 0xA1B23C4Du) { r->swap = false; r->nano = true; }
    else if (m == 0xD4C3B2A1u) { r->swap = true;  r->nano = false; }
    else if (m == 0x4D3CB2A1u) { r->swap = true;  r->nano = true; }
    else return PCAP_E_MAGIC;
    r->vmaj = pcap_u16(r, buf + 4);
    r->vmin = pcap_u16(r, buf + 6);
    if (r->vmaj != 2) return PCAP_E_VERSION;
    r->snaplen = pcap_u32(r, buf + 16);
    uint32_t lt = pcap_u32(r, buf + 20);
    r->linktype = lt & 0xFFFFu;
    r->link_extra = lt >> 16;
    if (r->linktype != LINKTYPE_ETHERNET) return PCAP_E_LINKTYPE;
    r->off = 24;
    return PCAP_OK;
}

/* 다음 레코드. 반환 PCAP_OK(rec 채움) / PCAP_EOF(정확히 파일 끝) / PCAP_E_TRUNC_HDR / PCAP_E_TRUNC_DATA / PCAP_E_CAPLEN. */
static int pcap_next(pcap_reader *r, pcap_rec *rec) {
    /* TODO A4: 남은 바이트로 16 B 헤더 확인 → caplen <= len, caplen <= PCAP_MAX_CAPLEN, caplen <= 남은 바이트 */
    if (r->off == r->n) return PCAP_EOF;
    size_t left = r->n - r->off;
    if (left < 16) return PCAP_E_TRUNC_HDR;
    const uint8_t *h = r->buf + r->off;
    uint32_t caplen = pcap_u32(r, h + 8), len = pcap_u32(r, h + 12);
    if (caplen > PCAP_MAX_CAPLEN || caplen > len) return PCAP_E_CAPLEN;
    if (caplen > left - 16) return PCAP_E_TRUNC_DATA;
    rec->ts_sec = pcap_u32(r, h);
    rec->ts_frac = pcap_u32(r, h + 4);
    rec->caplen = caplen;
    rec->len = len;
    rec->data = h + 16;
    r->off += 16u + caplen;
    return PCAP_OK;
}

/* ---------------- 패킷 해석 결과 ---------------- */

enum { CS_NA = 0, CS_OK, CS_BAD, CS_NONE, CS_PARTIAL, CS_FRAG, CS_TRUNC };
static const char *const cs_name[] = { "na", "ok", "BAD", "none", "partial", "frag", "trunc" };

enum { PE_NONE = 0,
       PE_TRUNC,          /* 캡처(caplen)가 필요한 헤더 앞에서 끝남 — 패킷 잘못이 아님 */
       PE_ETH_SHORT,      /* 원래 프레임이 Ethernet 헤더(+태그)보다 짧음 */
       PE_VLAN_DEPTH,     /* 태그 3개 이상 */
       PE_IP_SHORT,       /* L2 뒤 원래 길이 < 20 */
       PE_IP_VER,         /* version != 4 */
       PE_IP_IHL,         /* IHL < 5 */
       PE_IP_TOTLEN,      /* total length < IHL*4, 또는 > L2 뒤 원래 길이 */
       PE_UDP_SHORT,      /* IP payload < 8 인 UDP */
       PE_UDP_LEN };      /* UDP length < 8, 또는 (조각 아님) > IP payload */
static const char *const pe_name[] = { "none", "trunc", "eth_short", "vlan_depth", "ip_short", "ip_ver", "ip_ihl",
                                       "ip_totlen", "udp_short", "udp_len" };

typedef struct {
    int      err;                        /* PE_* — 처음 만난 해석 오류 */
    /* L2 */
    uint8_t  dst[6], src[6];
    int      ntags;
    uint16_t tpid[2], tci[2];            /* tci = PCP(3) DEI(1) VID(12) */
    uint16_t ethertype;
    size_t   l3_off;
    /* IPv4 */
    bool     has_ip;
    uint8_t  ihl, tos, ttl, proto;
    uint16_t tot_len, id, fragword;      /* fragword = flags(3) | offset(13), offset 단위 8 B */
    uint16_t hcs, hcs_want;              /* 헤더 체크섬: 저장값 / 필드를 0 으로 놓고 다시 계산한 값 */
    int      ip_cs;                      /* CS_OK / CS_BAD */
    uint32_t ip_src, ip_dst;
    size_t   l4_off, pad;                /* pad = IP total length 뒤에 붙은 Ethernet 패딩(트레일러) 바이트 */
    /* UDP */
    bool     has_udp;
    uint16_t sport, dport, ulen, ucs, ucs_want;
    int      udp_cs;                     /* CS_* */
    size_t   pay_off, pay_len;
} pkt_info;

#define IP_MF 0x2000u
#define IP_DF 0x4000u
#define IP_OFFMASK 0x1FFFu

/* ---------------- Part A: Ethernet II + VLAN ---------------- */

/* p[0..cap) = 캡처된 바이트, wire = 원래 프레임 길이(>= cap). 태그 TPID 0x8100(C-tag)·0x88A8(S-tag) 를 최대 2개 벗긴다. */
static int parse_l2(const uint8_t *p, size_t cap, size_t wire, pkt_info *pi) {
    /* TODO A5: 14 B 헤더 (wire 먼저 → PE_ETH_SHORT, 그다음 cap → PE_TRUNC), 태그 루프 (3개째 → PE_VLAN_DEPTH) */
    if (wire < 14) return PE_ETH_SHORT;
    if (cap < 14) return PE_TRUNC;
    memcpy(pi->dst, p, 6);
    memcpy(pi->src, p + 6, 6);
    uint16_t type = rd16be(p + 12);
    size_t off = 14;
    while (type == 0x8100u || type == 0x88A8u) {
        if (pi->ntags == 2) return PE_VLAN_DEPTH;
        if (wire < off + 4) return PE_ETH_SHORT;
        if (cap < off + 4) return PE_TRUNC;
        pi->tpid[pi->ntags] = type;
        pi->tci[pi->ntags] = rd16be(p + off);
        pi->ntags++;
        type = rd16be(p + off + 2);
        off += 4;
    }
    pi->ethertype = type;
    pi->l3_off = off;
    return PE_NONE;
}

/* ---------------- Part B: IPv4 ---------------- */

/* 검사 순서 (이 순서가 오류 코드를 정한다):
 *   원래 길이 < 20 → IP_SHORT, 캡처 < 20 → TRUNC, version → IP_VER, IHL < 5 → IP_IHL,
 *   tot < IHL*4 또는 tot > (wire − l3_off) → IP_TOTLEN, 캡처 < IHL*4 → TRUNC.
 * 통과하면 필드를 채우고 헤더 체크섬을 검증한다(틀려도 계속 해석 — tcpdump/Wireshark 처럼 "표시" 하는 도구이므로). */
static int parse_ipv4(const uint8_t *p, size_t cap, size_t wire, pkt_info *pi) {
    /* TODO B1 */
    size_t o = pi->l3_off;
    size_t avail_w = wire - o, avail_c = cap - o;
    if (avail_w < 20) return PE_IP_SHORT;
    if (avail_c < 20) return PE_TRUNC;
    const uint8_t *ip = p + o;
    if ((ip[0] >> 4) != 4) return PE_IP_VER;
    uint8_t ihl = ip[0] & 0x0F;
    if (ihl < 5) return PE_IP_IHL;
    size_t hl = (size_t)ihl * 4u;
    uint16_t tot = rd16be(ip + 2);
    if (tot < hl || tot > avail_w) return PE_IP_TOTLEN;
    if (avail_c < hl) return PE_TRUNC;
    pi->has_ip = true;
    pi->ihl = ihl; pi->tos = ip[1]; pi->tot_len = tot;
    pi->id = rd16be(ip + 4); pi->fragword = rd16be(ip + 6);
    pi->ttl = ip[8]; pi->proto = ip[9];
    pi->hcs = rd16be(ip + 10);
    pi->ip_src = rd32be(ip + 12); pi->ip_dst = rd32be(ip + 16);
    pi->l4_off = o + hl;
    pi->pad = avail_w - tot;
    uint32_t s = csum_add(0, ip, 10);                 /* 체크섬 필드(10..11)를 빼고 다시 계산 */
    s = csum_add(s, ip + 12, hl - 12);
    pi->hcs_want = csum_fold(s);
    pi->ip_cs = (csum_fold(csum_add(0, ip, hl)) == 0) ? CS_OK : CS_BAD;
    return PE_NONE;
}

/* ---------------- Part B: UDP ---------------- */

/* 의사 헤더(pseudo-header) 합: src(4) dst(4) 0 proto(1) UDP length(2) — IP total length 가 아니라 UDP length 필드! */
static uint32_t udp_pseudo_sum(uint32_t src, uint32_t dst, uint16_t ulen) {
    uint8_t ph[12];
    wr32be(ph, src); wr32be(ph + 4, dst);
    ph[8] = 0; ph[9] = 17; wr16be(ph + 10, ulen);
    return csum_add(0, ph, sizeof ph);
}

/* UDP 해석. 체크섬 판정:
 *   저장값 0 → CS_NONE (IPv4 에서는 "계산 안 함").  MF=1 첫 조각 → CS_FRAG (데이터그램 전체가 없어 검증 불가).
 *   캡처가 ulen 보다 짧음 → CS_TRUNC.  합이 맞음 → CS_OK.
 *   저장값 == 접기만 한(보수 안 취한) 의사 헤더 합 → CS_PARTIAL (송신측 checksum offload 의 부분합이 캡처됨).
 *   그 외 → CS_BAD.  ucs_want = 올바른 값 (계산 결과 0 이면 0xFFFF). */
static int parse_udp(const uint8_t *p, size_t cap, pkt_info *pi) {
    if (!pi->has_ip || pi->proto != 17) return PE_NONE;
    if ((pi->fragword & IP_OFFMASK) != 0) return PE_NONE;       /* 첫 조각이 아니면 UDP 헤더가 없다 */
    /* TODO B2: IP payload >= 8, 캡처 >= 8, 필드, 길이 규칙(조각이면 ulen > IP payload 허용), 체크섬 판정 */
    size_t ipl = (size_t)pi->tot_len - (size_t)pi->ihl * 4u;
    size_t o = pi->l4_off;
    if (ipl < 8) return PE_UDP_SHORT;
    if (cap - o < 8) return PE_TRUNC;
    const uint8_t *u = p + o;
    bool mf = (pi->fragword & IP_MF) != 0;
    pi->has_udp = true;
    pi->sport = rd16be(u); pi->dport = rd16be(u + 2);
    pi->ulen = rd16be(u + 4); pi->ucs = rd16be(u + 6);
    if (pi->ulen < 8 || (!mf && pi->ulen > ipl)) return PE_UDP_LEN;
    pi->pay_off = o + 8;
    pi->pay_len = (mf ? ipl : pi->ulen) - 8u;
    if (mf) { pi->udp_cs = pi->ucs ? CS_FRAG : CS_NONE; return PE_NONE; }
    if (cap - o < pi->ulen) { pi->udp_cs = pi->ucs ? CS_TRUNC : CS_NONE; return PE_NONE; }
    uint32_t ps = udp_pseudo_sum(pi->ip_src, pi->ip_dst, pi->ulen);
    uint32_t s = csum_add(ps, u, 6);                  /* 체크섬 필드(6..7) 빼고 */
    s = csum_add(s, u + 8, pi->ulen - 8u);
    uint16_t want = csum_fold(s);
    pi->ucs_want = want ? want : 0xFFFFu;
    if (pi->ucs == 0) pi->udp_cs = CS_NONE;
    else if (csum_fold(csum_add(ps, u, pi->ulen)) == 0) pi->udp_cs = CS_OK;
    else if (pi->ucs == (uint16_t)ps) pi->udp_cs = CS_PARTIAL;  /* ps 는 이미 접힌 값 */
    else pi->udp_cs = CS_BAD;
    return PE_NONE;
}

/* 전체 (주어짐): L2 → (EtherType 0x0800 이면) IPv4 → (proto 17 이면) UDP. */
static int pkt_parse(const uint8_t *p, size_t cap, size_t wire, pkt_info *pi) {
    memset(pi, 0, sizeof *pi);
    if (cap > wire) cap = wire;
    pi->err = parse_l2(p, cap, wire, pi);
    if (pi->err == PE_NONE && pi->ethertype == 0x0800u) pi->err = parse_ipv4(p, cap, wire, pi);
    if (pi->err == PE_NONE && pi->has_ip) pi->err = parse_udp(p, cap, pi);
    return pi->err;
}

/* ---------------- Part C: 증분 체크섬 갱신 (RFC 1624) ---------------- */

/* 16비트 필드 m 이 m2 로 바뀔 때 체크섬 hc 를 갱신: HC' = ~(~HC + ~m + m')  (RFC 1624 식 3). */
static uint16_t csum_update16(uint16_t hc, uint16_t m, uint16_t m2) {
    /* TODO C1 */
    uint32_t s = (uint32_t)(uint16_t)~hc + (uint16_t)~m + m2;
    while (s >> 16) s = (s & 0xFFFFu) + (s >> 16);
    return (uint16_t)~s;
}

/* 비교용 (주어짐): RFC 1141 식 2  HC' = HC + m + ~m'  — 결과 0x0000 이어야 할 때 0xFFFF(−0)를 낸다. */
static uint16_t csum_update16_rfc1141(uint16_t hc, uint16_t m, uint16_t m2) {
    uint32_t s = (uint32_t)hc + m + (uint16_t)~m2;
    while (s >> 16) s = (s & 0xFFFFu) + (s >> 16);
    return (uint16_t)s;
}

enum { RW_OK = 0, RW_E_NOTIP = -1, RW_E_TTL = -2, RW_E_FRAG = -3, RW_E_CAP = -4 };

/* 라우터가 하는 일: TTL 감소 + 헤더 체크섬 증분 갱신. TTL <= 1 이면 버림(RW_E_TTL; 실제 라우터는 ICMP Time Exceeded).
 * frame 은 pkt_parse 로 pi 를 채운 그 버퍼 (cap 바이트). pi 도 갱신한다. */
static int ip_ttl_dec(uint8_t *frame, size_t cap, pkt_info *pi) {
    if (!pi->has_ip || pi->l3_off + 20u > cap) return RW_E_NOTIP;
    /* TODO C2: TTL 은 (TTL << 8 | proto) 워드의 상위 바이트 */
    if (pi->ttl <= 1) return RW_E_TTL;
    uint8_t *ip = frame + pi->l3_off;
    uint16_t m = rd16be(ip + 8);
    ip[8]--;
    uint16_t hc = csum_update16(rd16be(ip + 10), m, rd16be(ip + 8));
    wr16be(ip + 10, hc);
    pi->ttl = ip[8]; pi->hcs = hc;
    return RW_OK;
}

/* SNAT: src IP 와 UDP src port 를 바꾸고 IP 헤더 체크섬·UDP 체크섬을 증분 갱신한다.
 * UDP 체크섬은 의사 헤더로 src IP 도 덮는다. UDP 체크섬 0(없음)은 그대로 0, 갱신 결과 0 은 0xFFFF.
 * 정책: 조각난 패킷(MF 또는 offset != 0)은 RW_E_FRAG (포트가 첫 조각에만 있어 조각 추적이 필요 — 이 레슨 범위 밖). */
static int nat_rewrite(uint8_t *frame, size_t cap, pkt_info *pi, uint32_t new_src, uint16_t new_sport) {
    if (!pi->has_ip || !pi->has_udp) return RW_E_NOTIP;
    if (pi->fragword & (IP_MF | IP_OFFMASK)) return RW_E_FRAG;
    if (pi->l4_off + 8u > cap) return RW_E_CAP;
    /* TODO C3: src IP 두 워드 → IP 체크섬과 UDP 체크섬, src port 워드 → UDP 체크섬 */
    uint8_t *ip = frame + pi->l3_off, *u = frame + pi->l4_off;
    uint16_t hc = rd16be(ip + 10), uc = rd16be(u + 6);
    uint16_t old_hi = (uint16_t)(pi->ip_src >> 16), old_lo = (uint16_t)pi->ip_src;
    uint16_t new_hi = (uint16_t)(new_src >> 16), new_lo = (uint16_t)new_src;
    hc = csum_update16(csum_update16(hc, old_hi, new_hi), old_lo, new_lo);
    if (uc != 0) {
        uc = csum_update16(csum_update16(uc, old_hi, new_hi), old_lo, new_lo);
        uc = csum_update16(uc, pi->sport, new_sport);
        if (uc == 0) uc = 0xFFFFu;
    }
    wr32be(ip + 12, new_src); wr16be(ip + 10, hc);
    wr16be(u, new_sport); wr16be(u + 6, uc);
    pi->ip_src = new_src; pi->hcs = hc; pi->sport = new_sport; pi->ucs = uc;
    return RW_OK;
}

/* ======================================================================================================
 * 아래는 도구·하네스 (주어짐): 한 줄 출력 형식, 요약 카운터, 프레임 빌더(pcap writer), 시험 벡터, 테스트
 * ====================================================================================================== */

/* 표준 출력 형식 (verify_pcap.py 의 fmt 와 글자 단위로 같아야 한다) */
static size_t g_fo;
static void ap(char *o, size_t cap, const char *fmt, ...) __attribute__((format(printf, 3, 4)));
static void ap(char *o, size_t cap, const char *fmt, ...) {
    if (g_fo >= cap) return;
    va_list a; va_start(a, fmt);
    int k = vsnprintf(o + g_fo, cap - g_fo, fmt, a);
    va_end(a);
    if (k > 0) g_fo += (size_t)k;
}
static void fmt_ip(char *o, size_t cap, uint32_t a) {
    ap(o, cap, "%u.%u.%u.%u", (unsigned)(a >> 24), (unsigned)((a >> 16) & 255), (unsigned)((a >> 8) & 255), (unsigned)(a & 255));
}
static void fmt_pkt(char *o, size_t cap, unsigned idx, const pcap_rec *r, bool nano, const pkt_info *pi) {
    g_fo = 0; o[0] = 0;
    ap(o, cap, nano ? "#%u t=%u.%09u cap=%u len=%u" : "#%u t=%u.%06u cap=%u len=%u",
       idx, (unsigned)r->ts_sec, (unsigned)r->ts_frac, (unsigned)r->caplen, (unsigned)r->len);
    if (r->caplen >= 14 && r->len >= 14) {
        const uint8_t *s = pi->src, *d = pi->dst;
        ap(o, cap, " eth %02x:%02x:%02x:%02x:%02x:%02x>%02x:%02x:%02x:%02x:%02x:%02x",
           s[0], s[1], s[2], s[3], s[4], s[5], d[0], d[1], d[2], d[3], d[4], d[5]);
        for (int i = 0; i < pi->ntags; i++)
            ap(o, cap, " vlan 0x%04x:p%u:d%u:v%u", pi->tpid[i], (unsigned)(pi->tci[i] >> 13),
               (unsigned)((pi->tci[i] >> 12) & 1u), (unsigned)(pi->tci[i] & 0xFFFu));
        if (pi->l3_off) ap(o, cap, " type 0x%04x", pi->ethertype);
    }
    if (pi->has_ip) {
        ap(o, cap, " ip "); fmt_ip(o, cap, pi->ip_src); ap(o, cap, ">"); fmt_ip(o, cap, pi->ip_dst);
        ap(o, cap, " ihl %u tot %u id 0x%04x ttl %u p %u frag %s%s%u", pi->ihl, pi->tot_len, pi->id, pi->ttl, pi->proto,
           (pi->fragword & IP_DF) ? "DF" : "", (pi->fragword & IP_MF) ? "MF+" : "+", (unsigned)(pi->fragword & IP_OFFMASK));
        ap(o, cap, " hcs 0x%04x %s", pi->hcs, cs_name[pi->ip_cs]);
        if (pi->ip_cs == CS_BAD) ap(o, cap, " want 0x%04x", pi->hcs_want);
        if (pi->pad) ap(o, cap, " pad %u", (unsigned)pi->pad);
    }
    if (pi->has_udp) {
        ap(o, cap, " udp %u>%u ulen %u cs 0x%04x %s", pi->sport, pi->dport, pi->ulen, pi->ucs, cs_name[pi->udp_cs]);
        if (pi->udp_cs == CS_BAD || pi->udp_cs == CS_PARTIAL) ap(o, cap, " want 0x%04x", pi->ucs_want);
        if (pi->err == PE_NONE) ap(o, cap, " pay %u", (unsigned)pi->pay_len);
    }
    if (pi->err) ap(o, cap, " err %s", pe_name[pi->err]);
}

/* 요약 카운터 — bring-up 때 읽는 숫자 */
typedef struct {
    unsigned frames, vlan, ipv4, other_l3, ip_bad, frag, udp, udp_ok, udp_bad, udp_partial, udp_none, udp_unverif, err[10];
} pcap_stats;
static void stats_add(pcap_stats *st, const pkt_info *pi) {
    st->frames++;
    if (pi->ntags) st->vlan++;
    if (pi->has_ip) { st->ipv4++; if (pi->ip_cs == CS_BAD) st->ip_bad++; if (pi->fragword & (IP_MF | IP_OFFMASK)) st->frag++; }
    else if (pi->err == PE_NONE) st->other_l3++;
    if (pi->has_udp && pi->err == PE_NONE) {
        st->udp++;
        if (pi->udp_cs == CS_OK) st->udp_ok++;
        else if (pi->udp_cs == CS_BAD) st->udp_bad++;
        else if (pi->udp_cs == CS_PARTIAL) st->udp_partial++;
        else if (pi->udp_cs == CS_NONE) st->udp_none++;
        else st->udp_unverif++;
    }
    if (pi->err > 0 && pi->err < 10) st->err[pi->err]++;
}
static void stats_line(char *o, size_t cap, const pcap_stats *st) {
    g_fo = 0; o[0] = 0;
    ap(o, cap, "summary frames=%u vlan=%u ipv4=%u other=%u ip_bad=%u frag=%u udp=%u ok=%u bad=%u partial=%u none=%u unverif=%u",
       st->frames, st->vlan, st->ipv4, st->other_l3, st->ip_bad, st->frag, st->udp, st->udp_ok, st->udp_bad,
       st->udp_partial, st->udp_none, st->udp_unverif);
    for (int i = 1; i < 10; i++) if (st->err[i]) ap(o, cap, " %s=%u", pe_name[i], st->err[i]);
}

/* ---------- 프레임 빌더 (시험 벡터용 — verify_pcap.py 가 같은 바이트를 독립적으로 만든다) ---------- */
enum { UCS_AUTO = 0, UCS_ZERO, UCS_SET };
typedef struct {
    const uint8_t *dst, *src;
    int      ntags; uint16_t tpid[3], tci[3];
    uint16_t ethertype;            /* 0 → 0x0800 */
    const uint8_t *l3raw; size_t l3rawlen;   /* ethertype != 0x0800 일 때 L2 뒤에 그대로 */
    uint8_t  ihl_set;              /* 0 → 5 + optlen/4 */
    const uint8_t *opts; size_t optlen;
    uint8_t  tos, ttl, proto;
    uint16_t id, fragword, tot_set; /* tot_set 0 → 실제 길이 */
    uint32_t src_ip, dst_ip;
    bool     l4raw;                /* true: pl 이 IP payload 전체 (UDP 헤더 직접 만들지 않음) */
    uint16_t sport, dport, ulen_set; /* ulen_set 0 → 8 + pllen */
    const uint8_t *pl; size_t pllen;
    int      ucs_mode; uint16_t ucs_val;
    uint16_t hcs_xor, ucs_xor;
    size_t   pad_to;               /* 짧으면 0 으로 채움 (Ethernet 최소 60 B, FCS 없음) */
} frame_spec;

static const uint8_t MAC_A[6] = { 0x02, 0, 0, 0, 0, 0x01 }, MAC_B[6] = { 0x02, 0, 0, 0, 0, 0x02 };

static int build_frame(const frame_spec *s, uint8_t *out, size_t cap) {
    size_t need = 14u + 4u * (size_t)s->ntags + (s->ethertype && s->ethertype != 0x0800u ? s->l3rawlen
                  : 20u + s->optlen + (s->l4raw ? 0u : 8u) + s->pllen);
    size_t total = need < s->pad_to ? s->pad_to : need;
    if (total > cap) return -1;
    memset(out, 0, total);
    memcpy(out, s->dst ? s->dst : MAC_B, 6);
    memcpy(out + 6, s->src ? s->src : MAC_A, 6);
    size_t o = 12;
    for (int i = 0; i < s->ntags; i++) { wr16be(out + o, s->tpid[i]); wr16be(out + o + 2, s->tci[i]); o += 4; }
    uint16_t et = s->ethertype ? s->ethertype : 0x0800u;
    wr16be(out + o, et); o += 2;
    if (et != 0x0800u) { memcpy(out + o, s->l3raw, s->l3rawlen); return (int)total; }
    uint8_t *ip = out + o;
    size_t hl = 20u + s->optlen, l4len = (s->l4raw ? 0u : 8u) + s->pllen;
    ip[0] = (uint8_t)(0x40u | (s->ihl_set ? s->ihl_set : (uint8_t)(hl / 4u)));
    ip[1] = s->tos;
    wr16be(ip + 2, s->tot_set ? s->tot_set : (uint16_t)(hl + l4len));
    wr16be(ip + 4, s->id); wr16be(ip + 6, s->fragword);
    ip[8] = s->ttl; ip[9] = s->proto;
    wr32be(ip + 12, s->src_ip); wr32be(ip + 16, s->dst_ip);
    if (s->optlen) memcpy(ip + 20, s->opts, s->optlen);
    wr16be(ip + 10, (uint16_t)(csum_fold(csum_add(0, ip, hl)) ^ s->hcs_xor));
    uint8_t *u = ip + hl;
    if (s->l4raw) { if (s->pllen) memcpy(u, s->pl, s->pllen); return (int)total; }
    uint16_t ulen = s->ulen_set ? s->ulen_set : (uint16_t)(8u + s->pllen);
    wr16be(u, s->sport); wr16be(u + 2, s->dport); wr16be(u + 4, ulen);
    if (s->pllen) memcpy(u + 8, s->pl, s->pllen);
    uint16_t c = 0;
    if (s->ucs_mode == UCS_AUTO) {
        c = csum_fold(csum_add(udp_pseudo_sum(s->src_ip, s->dst_ip, (uint16_t)(8u + s->pllen)), u, 8u + s->pllen));
        if (c == 0) c = 0xFFFFu;
    } else if (s->ucs_mode == UCS_SET) c = s->ucs_val;
    wr16be(u + 6, (uint16_t)(c ^ s->ucs_xor));
    return (int)total;
}

/* pcap writer */
typedef struct { uint8_t *buf; size_t cap, n; bool swap, nano; bool err; } pcap_writer;
static void pw_u32(pcap_writer *w, uint32_t v) {
    if (w->n + 4 > w->cap) { w->err = true; return; }
    if (w->swap) wr32be(w->buf + w->n, v); else wr32le(w->buf + w->n, v);
    w->n += 4;
}
static void pw_begin(pcap_writer *w, uint8_t *buf, size_t cap, bool swap, bool nano, uint32_t snaplen, uint32_t linktype) {
    w->buf = buf; w->cap = cap; w->n = 0; w->swap = swap; w->nano = nano; w->err = false;
    pw_u32(w, nano ? 0xA1B23C4Du : 0xA1B2C3D4u);
    if (w->n + 4 > cap) { w->err = true; return; }
    if (swap) { wr16be(buf + w->n, 2); wr16be(buf + w->n + 2, 4); } else { wr16le(buf + w->n, 2); wr16le(buf + w->n + 2, 4); }
    w->n += 4;
    pw_u32(w, 0); pw_u32(w, 0); pw_u32(w, snaplen); pw_u32(w, linktype);
}
static void pw_rec(pcap_writer *w, uint32_t sec, uint32_t usec, const uint8_t *frame, uint32_t caplen, uint32_t len) {
    pw_u32(w, sec); pw_u32(w, w->nano ? usec * 1000u : usec); pw_u32(w, caplen); pw_u32(w, len);
    if (w->n + caplen > w->cap) { w->err = true; return; }
    memcpy(w->buf + w->n, frame, caplen); w->n += caplen;
}

/* ---------- 시험 벡터 17개 (verify_pcap.py 의 VECTORS 와 같은 정의) ---------- */
#define NVEC 17
#define VEC_MAX 1600
static uint8_t  g_vec[NVEC][VEC_MAX];
static uint32_t g_vlen[NVEC], g_vcap[NVEC];
static const char *const g_vname[NVEC] = {
    "basic_odd5", "vlan100", "qinq", "ip_options", "frag1_mf", "frag2_last", "ucs_zero", "ucs_ffff",
    "bad_ip_csum", "bad_udp_csum", "arp", "icmp", "ihl4", "tot_gt_frame", "snap_trunc", "ulen_gt_ip", "empty_payload" };
static uint8_t g_dgram[1608];          /* 조각낼 UDP 데이터그램 (헤더 8 + payload 1600) */

#define IP_A 0xC0A8010Au   /* 192.168.1.10 */
#define IP_B 0xC0A80114u   /* 192.168.1.20 */

static void base_spec(frame_spec *s, unsigned i) {
    memset(s, 0, sizeof *s);
    s->ttl = 64; s->proto = 17; s->id = (uint16_t)(0x1C40u + i); s->fragword = IP_DF;
    s->src_ip = IP_A; s->dst_ip = IP_B; s->sport = 5000; s->dport = 6000; s->pad_to = 60;
}
static void make_vectors(void) {
    static const uint8_t hello[] = "hello", vl[] = "vlan-tagged!", qq[] = "qinq", op[] = "opt", nc[] = "nocsum";
    static const uint8_t ra[4] = { 0x94, 0x04, 0x00, 0x00 };           /* Router Alert 옵션 (RFC 2113) */
    static uint8_t arp[28], icmp[8], big[64], zz[4];
    frame_spec s;
    for (unsigned i = 0; i < NVEC; i++) {
        base_spec(&s, i);
        switch (i) {
        case 0: s.pl = hello; s.pllen = 5; break;
        case 1: s.ntags = 1; s.tpid[0] = 0x8100; s.tci[0] = (5u << 13) | 100u; s.pl = vl; s.pllen = 12; break;
        case 2: s.ntags = 2; s.tpid[0] = 0x88A8; s.tci[0] = 200u; s.tpid[1] = 0x8100; s.tci[1] = (3u << 13) | (1u << 12) | 100u;
                s.pl = qq; s.pllen = 4; break;
        case 3: s.opts = ra; s.optlen = 4; s.pl = op; s.pllen = 3; break;
        case 4: s.id = 0x2222; s.fragword = IP_MF; s.l4raw = true; s.pl = g_dgram; s.pllen = 1480; break;
        case 5: s.id = 0x2222; s.fragword = 185; s.l4raw = true; s.pl = g_dgram + 1480; s.pllen = 128; break;
        case 6: s.pl = nc; s.pllen = 6; s.ucs_mode = UCS_ZERO; break;
        case 7: s.pl = zz; s.pllen = 4; break;
        case 8: s.pl = hello; s.pllen = 5; s.hcs_xor = 0x0100; break;
        case 9: s.pl = hello; s.pllen = 5; s.ucs_xor = 0x0001; break;
        case 10: s.ethertype = 0x0806; s.l3raw = arp; s.l3rawlen = 28; break;
        case 11: s.proto = 1; s.l4raw = true; s.pl = icmp; s.pllen = 8; break;
        case 12: s.pl = hello; s.pllen = 5; s.ihl_set = 4; break;
        case 13: s.pl = hello; s.pllen = 5; s.tot_set = 200; break;
        case 14: s.pl = big; s.pllen = 64; break;
        case 15: s.pl = hello; s.pllen = 5; s.ulen_set = 100; break;
        case 16: s.pl = NULL; s.pllen = 0; break;
        }
        if (i == 0) {   /* 1회 준비: 조각용 데이터그램, ARP, ICMP, 큰 payload, 체크섬이 0 이 되는 payload */
            frame_spec d; uint8_t tmp[1700];
            static uint8_t dpl[1600];
            for (unsigned k = 0; k < 1600; k++) dpl[k] = (uint8_t)(k * 7u + 3u);
            base_spec(&d, 4); d.pl = dpl; d.pllen = 1600; d.pad_to = 0; d.fragword = 0; d.id = 0x2222;
            int n = build_frame(&d, tmp, sizeof tmp);
            if (n == 14 + 20 + 1608) memcpy(g_dgram, tmp + 34, 1608);
            static const uint8_t arp_hdr[8] = { 0, 1, 8, 0, 6, 4, 0, 1 };
            memcpy(arp, arp_hdr, 8); memcpy(arp + 8, MAC_A, 6); wr32be(arp + 14, IP_A); wr32be(arp + 24, IP_B);
            static const uint8_t icmp_hdr[8] = { 8, 0, 0, 0, 0x12, 0x34, 0, 1 };
            memcpy(icmp, icmp_hdr, 8); wr16be(icmp + 2, csum_fold(csum_add(0, icmp, 8)));
            for (unsigned k = 0; k < 64; k++) big[k] = (uint8_t)(0xA0u + k);
            /* zz = 'z' 'z' + 조정 워드: 조정 0 일 때의 체크섬 c 를 넣으면 합이 0xFFFF → 계산 결과 0 → 0xFFFF 로 전송 */
            zz[0] = 'z'; zz[1] = 'z'; zz[2] = 0; zz[3] = 0;
            uint16_t c = csum_fold(csum_add(udp_pseudo_sum(IP_A, IP_B, 12), (const uint8_t[]){ 0x13, 0x88, 0x17, 0x70, 0, 12, 0, 0, 'z', 'z', 0, 0 }, 12));
            wr16be(zz + 2, c);
        }
        int n = build_frame(&s, g_vec[i], VEC_MAX);
        g_vlen[i] = n > 0 ? (uint32_t)n : 0;
        g_vcap[i] = g_vlen[i];
    }
    g_vcap[14] = 54;      /* snaplen 54 로 잘린 캡처: 원래 106 B */
}
static size_t write_vectors(uint8_t *buf, size_t cap, bool swap, bool nano) {
    pcap_writer w;
    pw_begin(&w, buf, cap, swap, nano, 65535, LINKTYPE_ETHERNET);
    for (unsigned i = 0; i < NVEC; i++) pw_rec(&w, 1700000000u + i, 1000u * i + 7u, g_vec[i], g_vcap[i], g_vlen[i]);
    return w.err ? 0 : w.n;
}

/* ---------- 실제 캡처 (Lima VM, Linux 7.0, tcpdump 4.99.6 -i lo, 2026-11-11) — 레슨 폴더의 .pcap 과 같은 바이트 ---------- */
static const uint8_t g_lo_pcap[444] = {   /* lo.pcap (Lima VM tcpdump -i lo, 444 B) */
    0xd4,0xc3,0xb2,0xa1,0x02,0x00,0x04,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x04,0x00,
    0x01,0x00,0x00,0x00,0x15,0x38,0xf5,0x6a,0xbf,0xfb,0x08,0x00,0x2f,0x00,0x00,0x00,0x2f,0x00,0x00,0x00,
    0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x08,0x00,0x45,0x00,0x00,0x21,0x93,0x75,
    0x40,0x00,0x40,0x11,0xa9,0x54,0x7f,0x00,0x00,0x01,0x7f,0x00,0x00,0x01,0x9c,0x41,0xc4,0xe4,0x00,0x0d,
    0xfe,0x20,0x68,0x65,0x6c,0x6c,0x6f,0x15,0x38,0xf5,0x6a,0xc2,0xc3,0x09,0x00,0x2e,0x00,0x00,0x00,0x2e,
    0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x08,0x00,0x45,0x00,0x00,
    0x20,0x93,0x77,0x40,0x00,0x40,0x11,0xa9,0x53,0x7f,0x00,0x00,0x01,0x7f,0x00,0x00,0x01,0x9c,0x42,0xc4,
    0xe4,0x00,0x0c,0xfe,0x1f,0x70,0x69,0x6e,0x67,0x15,0x38,0xf5,0x6a,0x56,0x89,0x0a,0x00,0x2a,0x00,0x00,
    0x00,0x2a,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x08,0x00,0x45,
    0x00,0x00,0x1c,0x93,0x7c,0x40,0x00,0x40,0x11,0xa9,0x52,0x7f,0x00,0x00,0x01,0x7f,0x00,0x00,0x01,0x9c,
    0x43,0xc4,0xe4,0x00,0x08,0xfe,0x1b,0x15,0x38,0xf5,0x6a,0x01,0x61,0x0b,0x00,0x30,0x00,0x00,0x00,0x30,
    0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x08,0x00,0x45,0x00,0x00,
    0x22,0x93,0x9c,0x40,0x00,0x40,0x11,0xa9,0x2c,0x7f,0x00,0x00,0x01,0x7f,0x00,0x00,0x01,0x9c,0x44,0xc4,
    0xe4,0x00,0x0e,0x00,0x00,0x6e,0x6f,0x63,0x73,0x75,0x6d,0x15,0x38,0xf5,0x6a,0xc0,0x25,0x0c,0x00,0x33,
    0x00,0x00,0x00,0x33,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x08,
    0x00,0x46,0x00,0x00,0x25,0x93,0xcd,0x40,0x00,0x40,0x11,0xa5,0xf7,0x7f,0x00,0x00,0x01,0x7f,0x00,0x00,
    0x01,0x01,0x01,0x01,0x00,0x9c,0x45,0xc4,0xe4,0x00,0x0d,0xfe,0x20,0x6f,0x70,0x74,0x73,0x21,0x15,0x38,
    0xf5,0x6a,0xb2,0xea,0x0c,0x00,0x5a,0x00,0x00,0x00,0x5a,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
    0x00,0x00,0x00,0x00,0x00,0x00,0x08,0x00,0x45,0x00,0x00,0x4c,0x93,0xe1,0x40,0x00,0x40,0x11,0xa8,0xbd,
    0x7f,0x00,0x00,0x01,0x7f,0x00,0x00,0x01,0x9c,0x46,0xc4,0xe4,0x00,0x38,0xfe,0x4b,0x00,0x01,0x02,0x03,
    0x04,0x05,0x06,0x07,0x08,0x09,0x0a,0x0b,0x0c,0x0d,0x0e,0x0f,0x10,0x11,0x12,0x13,0x14,0x15,0x16,0x17,
    0x18,0x19,0x1a,0x1b,0x1c,0x1d,0x1e,0x1f,0x20,0x21,0x22,0x23,0x24,0x25,0x26,0x27,0x28,0x29,0x2a,0x2b,
    0x2c,0x2d,0x2e,0x2f,
};
static const uint8_t g_lo_ns_snap60_pcap[414] = {   /* lo_ns_snap60.pcap (Lima VM tcpdump -i lo, 414 B) */
    0x4d,0x3c,0xb2,0xa1,0x02,0x00,0x04,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x3c,0x00,0x00,0x00,
    0x01,0x00,0x00,0x00,0x15,0x38,0xf5,0x6a,0xf2,0x62,0x17,0x23,0x2f,0x00,0x00,0x00,0x2f,0x00,0x00,0x00,
    0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x08,0x00,0x45,0x00,0x00,0x21,0x93,0x75,
    0x40,0x00,0x40,0x11,0xa9,0x54,0x7f,0x00,0x00,0x01,0x7f,0x00,0x00,0x01,0x9c,0x41,0xc4,0xe4,0x00,0x0d,
    0xfe,0x20,0x68,0x65,0x6c,0x6c,0x6f,0x15,0x38,0xf5,0x6a,0xad,0xb1,0x24,0x26,0x2e,0x00,0x00,0x00,0x2e,
    0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x08,0x00,0x45,0x00,0x00,
    0x20,0x93,0x77,0x40,0x00,0x40,0x11,0xa9,0x53,0x7f,0x00,0x00,0x01,0x7f,0x00,0x00,0x01,0x9c,0x42,0xc4,
    0xe4,0x00,0x0c,0xfe,0x1f,0x70,0x69,0x6e,0x67,0x15,0x38,0xf5,0x6a,0x6b,0x79,0x28,0x29,0x2a,0x00,0x00,
    0x00,0x2a,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x08,0x00,0x45,
    0x00,0x00,0x1c,0x93,0x7c,0x40,0x00,0x40,0x11,0xa9,0x52,0x7f,0x00,0x00,0x01,0x7f,0x00,0x00,0x01,0x9c,
    0x43,0xc4,0xe4,0x00,0x08,0xfe,0x1b,0x15,0x38,0xf5,0x6a,0x16,0xed,0x72,0x2c,0x30,0x00,0x00,0x00,0x30,
    0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x08,0x00,0x45,0x00,0x00,
    0x22,0x93,0x9c,0x40,0x00,0x40,0x11,0xa9,0x2c,0x7f,0x00,0x00,0x01,0x7f,0x00,0x00,0x01,0x9c,0x44,0xc4,
    0xe4,0x00,0x0e,0x00,0x00,0x6e,0x6f,0x63,0x73,0x75,0x6d,0x15,0x38,0xf5,0x6a,0xdd,0x78,0x73,0x2f,0x33,
    0x00,0x00,0x00,0x33,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x08,
    0x00,0x46,0x00,0x00,0x25,0x93,0xcd,0x40,0x00,0x40,0x11,0xa5,0xf7,0x7f,0x00,0x00,0x01,0x7f,0x00,0x00,
    0x01,0x01,0x01,0x01,0x00,0x9c,0x45,0xc4,0xe4,0x00,0x0d,0xfe,0x20,0x6f,0x70,0x74,0x73,0x21,0x15,0x38,
    0xf5,0x6a,0x19,0xc9,0x74,0x32,0x3c,0x00,0x00,0x00,0x5a,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
    0x00,0x00,0x00,0x00,0x00,0x00,0x08,0x00,0x45,0x00,0x00,0x4c,0x93,0xe1,0x40,0x00,0x40,0x11,0xa8,0xbd,
    0x7f,0x00,0x00,0x01,0x7f,0x00,0x00,0x01,0x9c,0x46,0xc4,0xe4,0x00,0x38,0xfe,0x4b,0x00,0x01,0x02,0x03,
    0x04,0x05,0x06,0x07,0x08,0x09,0x0a,0x0b,0x0c,0x0d,0x0e,0x0f,0x10,0x11,
};

/* ---------- 도구 모드 ---------- */
static int dump_buf(const uint8_t *buf, size_t n, FILE *out, pcap_stats *st_out) {
    pcap_reader r; pcap_rec rec; pkt_info pi; pcap_stats st; char line[512];
    memset(&st, 0, sizeof st);
    int rc = pcap_open(&r, buf, n);
    if (st_out) *st_out = st;
    if (rc != PCAP_OK) { if (out) fprintf(out, "pcap_open error %d\n", rc); return rc; }
    unsigned idx = 0;
    while ((rc = pcap_next(&r, &rec)) == PCAP_OK) {
        pkt_parse(rec.data, rec.caplen, rec.len, &pi);
        stats_add(&st, &pi);
        if (out) { fmt_pkt(line, sizeof line, ++idx, &rec, r.nano, &pi); fprintf(out, "%s\n", line); }
    }
    if (out) {
        stats_line(line, sizeof line, &st);
        fprintf(out, "%s%s\n", line, rc == PCAP_EOF ? "" : (rc == PCAP_E_TRUNC_DATA || rc == PCAP_E_TRUNC_HDR) ? " file=truncated" : " file=bad_record");
    }
    if (st_out) *st_out = st;
    return rc == PCAP_EOF ? 0 : rc;
}
static uint8_t g_filebuf[1u << 22];
static int tool_main(int argc, char **argv) {
    if (argc == 3 && strcmp(argv[1], "dump") == 0) {
        FILE *f = fopen(argv[2], "rb");
        if (!f) { perror(argv[2]); return 2; }
        size_t n = fread(g_filebuf, 1, sizeof g_filebuf, f);
        fclose(f);
        return dump_buf(g_filebuf, n, stdout, NULL) == 0 ? 0 : 1;
    }
    if (argc == 3 && strcmp(argv[1], "vectors") == 0) {
        make_vectors();
        size_t n = write_vectors(g_filebuf, sizeof g_filebuf, false, false);
        FILE *f = fopen(argv[2], "wb");
        if (!f || n == 0) return 2;
        fwrite(g_filebuf, 1, n, f);
        fclose(f);
        for (unsigned i = 0; i < NVEC; i++) printf("  #%-2u %-14s cap=%u len=%u\n", i + 1, g_vname[i], (unsigned)g_vcap[i], (unsigned)g_vlen[i]);
        printf("wrote %zu bytes, %d records\n", n, NVEC);
        return 0;
    }
    fprintf(stderr, "usage: %s [dump file.pcap | vectors out.pcap]\n", argv[0]);
    return 2;
}

/* ---------------- 테스트 하네스 ---------------- */
static int g_pass, g_total;
#define CHECK(expr) do { g_total++; if (expr) { g_pass++; } \
    else { printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #expr); } } while (0)

static uint32_t g_rng = 0xC0A80114u;
static uint32_t xs32(void) { uint32_t x = g_rng; x ^= x << 13; x ^= x >> 17; x ^= x << 5; return g_rng = x; }

/* exact-size 힙 복사본에서 해석 → caplen 밖을 1바이트라도 읽으면 ASan 이 잡는다 */
static int parse_exact(const uint8_t *p, size_t cap, size_t wire, pkt_info *pi) {
    uint8_t *h = malloc(cap ? cap : 1);
    if (!h) return -99;
    if (cap) memcpy(h, p, cap);
    int e = pkt_parse(h, cap, wire, pi);
    free(h);
    return e;
}
static int vparse(unsigned i, pkt_info *pi) { return parse_exact(g_vec[i], g_vcap[i], g_vlen[i], pi); }

/* 독립 검증용: 체크섬 필드를 0 으로 놓고 처음부터 다시 계산 (증분 갱신의 정답) */
static uint16_t recompute_ip(const uint8_t *ip, size_t hl) {
    uint8_t t[60]; memcpy(t, ip, hl); t[10] = t[11] = 0;
    return csum_fold(csum_add(0, t, hl));
}
static uint16_t recompute_udp(uint32_t src, uint32_t dst, const uint8_t *u, uint16_t ulen) {
    uint32_t s = csum_add(udp_pseudo_sum(src, dst, ulen), u, 6);
    s = csum_add(s, u + 8, ulen - 8u);
    uint16_t c = csum_fold(s);
    return c ? c : 0xFFFFu;
}

/* 랜덤 UDP 프레임 (태그 0~2, 옵션 0~40 B, payload 0~64 B, 10% 는 UDP 체크섬 0) */
static int random_frame(uint8_t *out, size_t cap, frame_spec *s, uint8_t *plbuf, uint8_t *optbuf) {
    base_spec(s, 0);
    s->ntags = (int)(xs32() % 3u);
    for (int t = 0; t < s->ntags; t++) { s->tpid[t] = (t == 0 && s->ntags == 2) ? 0x88A8 : 0x8100; s->tci[t] = (uint16_t)xs32(); }
    s->optlen = 4u * (xs32() % 11u);
    for (size_t k = 0; k < s->optlen; k++) optbuf[k] = 1;    /* NOP 로 채움 */
    s->opts = optbuf;
    s->src_ip = xs32(); s->dst_ip = xs32(); s->sport = (uint16_t)xs32(); s->dport = (uint16_t)xs32();
    s->ttl = (uint8_t)(2u + xs32() % 254u); s->id = (uint16_t)xs32(); s->tos = (uint8_t)xs32();
    s->pllen = xs32() % 65u;
    for (size_t k = 0; k < s->pllen; k++) plbuf[k] = (uint8_t)xs32();
    s->pl = plbuf;
    s->ucs_mode = (xs32() % 10u == 0) ? UCS_ZERO : UCS_AUTO;
    s->pad_to = (xs32() & 1u) ? 60 : 0;
    return build_frame(s, out, cap);
}

int main(int argc, char **argv) {
    if (argc > 1) return tool_main(argc, argv);
    char line[512];

    /* ===== Part A ===== */
    {   /* A1/A2: RFC 1071 §3 예제 (00 01 f2 03 f4 f5 f6 f7 → 합 0xddf2), 위키백과식 IPv4 헤더 (체크섬 0xb861) */
        static const uint8_t r1071[8] = { 0x00, 0x01, 0xf2, 0x03, 0xf4, 0xf5, 0xf6, 0xf7 };
        CHECK(csum_add(0, r1071, 8) == 0xddf2u && csum_fold(csum_add(0, r1071, 8)) == 0x220du);
        static const uint8_t hdr[20] = { 0x45, 0x00, 0x00, 0x73, 0x00, 0x00, 0x40, 0x00, 0x40, 0x11, 0x00, 0x00,
                                         0xc0, 0xa8, 0x00, 0x01, 0xc0, 0xa8, 0x00, 0xc7 };
        CHECK(csum_fold(csum_add(0, hdr, 20)) == 0xb861u);
        uint8_t h2[20]; memcpy(h2, hdr, 20); h2[10] = 0xb8; h2[11] = 0x61;
        CHECK(csum_fold(csum_add(0, h2, 20)) == 0);                          /* 검증: 체크섬 포함 합 → 0 */
        static const uint8_t odd[3] = { 0x12, 0x34, 0xAB };
        CHECK(csum_add(0, odd, 3) == 0xBD34u);                                /* 0x1234 + 0xAB00 */
        CHECK(csum_add(csum_add(0, odd, 2), odd + 2, 1) == csum_add(0, odd, 3));
        uint8_t ff[4] = { 0xFF, 0xFF, 0xFF, 0xFF };
        CHECK(csum_add(0, ff, 4) == 0xFFFFu && csum_fold(csum_add(0, ff, 4)) == 0);
    }
    make_vectors();
    static uint8_t file[8192];
    size_t fn = write_vectors(file, sizeof file, false, false);
    {   /* A3/A4: pcap 헤더·레코드, magic 4종 */
        pcap_reader r; pcap_rec rec;
        CHECK(fn > 24 && file[0] == 0xD4 && file[1] == 0xC3 && file[2] == 0xB2 && file[3] == 0xA1);
        CHECK(pcap_open(&r, file, fn) == PCAP_OK && !r.swap && !r.nano && r.vmaj == 2 && r.vmin == 4 && r.snaplen == 65535);
        unsigned n = 0; int rc;
        bool lens_ok = true;
        while ((rc = pcap_next(&r, &rec)) == PCAP_OK) { if (rec.caplen != g_vcap[n] || rec.len != g_vlen[n]) lens_ok = false; n++; }
        CHECK(rc == PCAP_EOF && n == NVEC && lens_ok);
        static uint8_t f2[8192];
        bool same = true; unsigned variants_ok = 0;
        for (int v = 0; v < 4; v++) {
            bool sw = v & 1, na = v >> 1;
            size_t n2 = write_vectors(f2, sizeof f2, sw, na);
            static const uint8_t mg[4][4] = { { 0xD4, 0xC3, 0xB2, 0xA1 }, { 0xA1, 0xB2, 0xC3, 0xD4 }, { 0x4D, 0x3C, 0xB2, 0xA1 }, { 0xA1, 0xB2, 0x3C, 0x4D } };
            pcap_reader r2, r1; pcap_rec q1, q2; pkt_info p1, p2; char l1[512], l2[512];
            if (n2 != fn || memcmp(f2, mg[v], 4) != 0) continue;
            if (pcap_open(&r2, f2, n2) != PCAP_OK || r2.swap != sw || r2.nano != na) continue;
            pcap_open(&r1, file, fn);
            unsigned k = 0;
            while (pcap_next(&r1, &q1) == PCAP_OK && pcap_next(&r2, &q2) == PCAP_OK) {
                pkt_parse(q1.data, q1.caplen, q1.len, &p1); pkt_parse(q2.data, q2.caplen, q2.len, &p2);
                fmt_pkt(l1, sizeof l1, k, &q1, false, &p1); fmt_pkt(l2, sizeof l2, k, &q2, na, &p2);
                if (strcmp(strstr(l1, " cap="), strstr(l2, " cap=")) != 0) same = false;   /* 시각 표기만 다름 */
                if (q2.ts_frac != (na ? q1.ts_frac * 1000u : q1.ts_frac)) same = false;
                k++;
            }
            if (k == NVEC) variants_ok++;
        }
        CHECK(variants_ok == 4 && same);
        uint8_t bad[24]; memcpy(bad, file, 24);
        CHECK(pcap_open(&r, file, 23) == PCAP_E_SHORT);
        bad[0] = 0x0A; CHECK(pcap_open(&r, bad, 24) == PCAP_E_MAGIC);
        memcpy(bad, file, 24); bad[4] = 1; CHECK(pcap_open(&r, bad, 24) == PCAP_E_VERSION);
        memcpy(bad, file, 24); bad[20] = 113; CHECK(pcap_open(&r, bad, 24) == PCAP_E_LINKTYPE);      /* Linux cooked SLL */
        memcpy(bad, file, 24); bad[23] = 0x10; CHECK(pcap_open(&r, bad, 24) == PCAP_OK && r.link_extra == 0x1000u);
        CHECK(pcap_open(&r, file, 24) == PCAP_OK && pcap_next(&r, &rec) == PCAP_EOF);               /* 레코드 0개 */
    }
    {   /* A5 + 해석: 벡터별 기대값 */
        pkt_info pi;
        CHECK(vparse(0, &pi) == PE_NONE && pi.ntags == 0 && pi.ethertype == 0x0800 && pi.ip_cs == CS_OK && pi.udp_cs == CS_OK
              && pi.sport == 5000 && pi.dport == 6000 && pi.ulen == 13 && pi.pay_len == 5 && pi.pad == 13 && pi.l4_off == 34);
        CHECK(memcmp(g_vec[0] + pi.pay_off, "hello", 5) == 0 && pi.ip_src == IP_A && pi.ip_dst == IP_B && pi.ttl == 64);
        CHECK(vparse(1, &pi) == PE_NONE && pi.ntags == 1 && pi.tpid[0] == 0x8100 && (pi.tci[0] >> 13) == 5 && (pi.tci[0] & 0xFFF) == 100
              && pi.l3_off == 18 && pi.udp_cs == CS_OK && pi.pay_len == 12);
        CHECK(vparse(2, &pi) == PE_NONE && pi.ntags == 2 && pi.tpid[0] == 0x88A8 && (pi.tci[0] & 0xFFF) == 200
              && pi.tpid[1] == 0x8100 && ((pi.tci[1] >> 12) & 1) == 1 && pi.l3_off == 22 && pi.udp_cs == CS_OK);
        fmt_pkt(line, sizeof line, 3, &(pcap_rec){ 1700000002u, 2007u, g_vcap[2], g_vlen[2], g_vec[2] }, false, &pi);
        printf("A  %s\n", line);
        CHECK(strstr(line, "vlan 0x88a8:p0:d0:v200 vlan 0x8100:p3:d1:v100 type 0x0800") != NULL);
        CHECK(vparse(3, &pi) == PE_NONE && pi.ihl == 6 && pi.l4_off == 38 && pi.ip_cs == CS_OK && pi.udp_cs == CS_OK && pi.pay_len == 3);
        CHECK(vparse(10, &pi) == PE_NONE && !pi.has_ip && pi.ethertype == 0x0806);
        CHECK(vparse(11, &pi) == PE_NONE && pi.has_ip && pi.proto == 1 && !pi.has_udp && pi.ip_cs == CS_OK);
        CHECK(vparse(16, &pi) == PE_NONE && pi.ulen == 8 && pi.pay_len == 0 && pi.udp_cs == CS_OK);
    }
    {   /* 실제 캡처 1: lo.pcap (µs, LE) — 기대값은 tcpdump -r lo.pcap -nn -vv -e 출력에서 */
        static const uint16_t want[6] = { 0x5cd9, 0xc1db, 0xa0b3, 0, 0x9bc3, 0x760e };
        static const uint16_t ids[6] = { 37749, 37751, 37756, 37788, 37837, 37857 };
        static const uint16_t ulens[6] = { 13, 12, 8, 14, 13, 56 };
        pcap_reader r; pcap_rec rec; pkt_info pi; pcap_stats st;
        CHECK(pcap_open(&r, g_lo_pcap, sizeof g_lo_pcap) == PCAP_OK && !r.nano && !r.swap && r.snaplen == 262144);
        unsigned k = 0, good = 0;
        while (pcap_next(&r, &rec) == PCAP_OK && k < 6) {
            parse_exact(rec.data, rec.caplen, rec.len, &pi);
            fmt_pkt(line, sizeof line, k + 1, &rec, r.nano, &pi);
            printf("lo %s\n", line);
            bool ok = pi.err == PE_NONE && pi.ip_cs == CS_OK && pi.id == ids[k] && pi.sport == 40001 + k && pi.dport == 50404
                      && pi.ulen == ulens[k] && pi.ttl == 64 && (pi.fragword & IP_DF) && pi.pad == 0 && pi.ip_src == 0x7F000001u;
            if (k == 3) ok = ok && pi.ucs == 0 && pi.udp_cs == CS_NONE;
            else ok = ok && pi.udp_cs == CS_PARTIAL && pi.ucs_want == want[k];
            if (k == 4) ok = ok && pi.ihl == 6;
            good += ok; k++;
        }
        CHECK(k == 6 && good == 6);
        dump_buf(g_lo_pcap, sizeof g_lo_pcap, NULL, &st);
        stats_line(line, sizeof line, &st);
        printf("lo %s\n", line);
        CHECK(st.frames == 6 && st.udp == 6 && st.udp_partial == 5 && st.udp_none == 1 && st.ip_bad == 0);
    }
    {   /* 실제 캡처 2: lo_ns_snap60.pcap (ns magic, snaplen 60) — #6 은 caplen 60 / len 90 → UDP 체크섬 검증 불가 */
        pcap_reader r; pcap_rec rec; pkt_info pi;
        CHECK(pcap_open(&r, g_lo_ns_snap60_pcap, sizeof g_lo_ns_snap60_pcap) == PCAP_OK && r.nano && r.snaplen == 60);
        unsigned k = 0; bool ok = true;
        while (pcap_next(&r, &rec) == PCAP_OK) {
            parse_exact(rec.data, rec.caplen, rec.len, &pi);
            k++;
            if (k == 6) {
                fmt_pkt(line, sizeof line, k, &rec, r.nano, &pi);
                printf("ns %s\n", line);
                ok = ok && rec.caplen == 60 && rec.len == 90 && pi.err == PE_NONE && pi.udp_cs == CS_TRUNC && pi.ulen == 56;
            } else ok = ok && rec.caplen == rec.len && pi.err == PE_NONE;
            if (k == 1) ok = ok && rec.ts_frac == 588735218u;
        }
        CHECK(k == 6 && ok);
    }

    /* ===== Part B ===== */
    {   /* B: 벡터별 판정 */
        pkt_info pi;
        CHECK(vparse(4, &pi) == PE_NONE && pi.has_udp && pi.udp_cs == CS_FRAG && pi.ulen == 1608 && pi.pay_len == 1472
              && (pi.fragword & IP_MF) && pi.ip_cs == CS_OK);
        CHECK(vparse(5, &pi) == PE_NONE && pi.has_ip && !pi.has_udp && (pi.fragword & IP_OFFMASK) == 185 && pi.ip_cs == CS_OK);
        CHECK(vparse(6, &pi) == PE_NONE && pi.ucs == 0 && pi.udp_cs == CS_NONE);
        CHECK(vparse(7, &pi) == PE_NONE && pi.ucs == 0xFFFF && pi.udp_cs == CS_OK && pi.ucs_want == 0xFFFF);
        CHECK(vparse(8, &pi) == PE_NONE && pi.ip_cs == CS_BAD && pi.hcs == (pi.hcs_want ^ 0x0100) && pi.udp_cs == CS_OK);
        CHECK(vparse(9, &pi) == PE_NONE && pi.ip_cs == CS_OK && pi.udp_cs == CS_BAD && pi.ucs == (pi.ucs_want ^ 1));
        CHECK(vparse(12, &pi) == PE_IP_IHL && !pi.has_ip);
        CHECK(vparse(13, &pi) == PE_IP_TOTLEN);
        CHECK(vparse(14, &pi) == PE_NONE && pi.udp_cs == CS_TRUNC && pi.ulen == 72 && g_vlen[14] == 106);
        CHECK(vparse(15, &pi) == PE_UDP_LEN && pi.has_udp && pi.ulen == 100);
        uint8_t f[64];
        memcpy(f, g_vec[0], 60); f[14] = 0x65;                            /* version 6 */
        CHECK(parse_exact(f, 60, 60, &pi) == PE_IP_VER);
        memcpy(f, g_vec[0], 60); wr16be(f + 16, 19);                      /* tot < 20 */
        CHECK(parse_exact(f, 60, 60, &pi) == PE_IP_TOTLEN);
        memcpy(f, g_vec[0], 60); wr16be(f + 16, 27);                      /* UDP 인데 IP payload 7 B */
        CHECK(parse_exact(f, 60, 60, &pi) == PE_UDP_SHORT);
        memcpy(f, g_vec[0], 60); wr16be(f + 38, 7);                       /* UDP length 7 */
        CHECK(parse_exact(f, 60, 60, &pi) == PE_UDP_LEN);
        memcpy(f, g_vec[0], 60); wr16be(f + 16, 36); f[47] = f[48] = f[49] = 0xAB;   /* IP payload 16 B, UDP length 13: 뒤 3 B 는 UDP 밖 */
        wr16be(f + 24, 0); wr16be(f + 24, recompute_ip(f + 14, 20));
        CHECK(parse_exact(f, 60, 60, &pi) == PE_NONE && pi.udp_cs == CS_OK && pi.pay_len == 5 && pi.pad == 10);
        CHECK(parse_exact(g_vec[0], 10, 10, &pi) == PE_ETH_SHORT);
        CHECK(parse_exact(g_vec[0], 10, 60, &pi) == PE_TRUNC);            /* 원래는 60 B, 캡처만 10 B */
        CHECK(parse_exact(g_vec[0], 30, 30, &pi) == PE_IP_SHORT);
        uint8_t t3[64] = { 0 };
        memcpy(t3, g_vec[0], 12);
        for (int t = 0; t < 3; t++) { wr16be(t3 + 12 + 4 * t, 0x8100); wr16be(t3 + 14 + 4 * t, 7); }
        wr16be(t3 + 24, 0x8100);
        CHECK(parse_exact(t3, 64, 64, &pi) == PE_VLAN_DEPTH);
        memcpy(f, g_vec[3], 64); wr16be(f + 16, 23);                      /* IHL 6 (24 B) 인데 tot 23 */
        CHECK(parse_exact(f, 64, 64, &pi) == PE_IP_TOTLEN);
        CHECK(parse_exact(g_vec[3], 36, 64, &pi) == PE_TRUNC);            /* 옵션이 캡처 밖 */
    }
    {   /* B: 단일 비트 오류 전수 — IP 헤더 160비트, UDP 헤더+payload 104비트. 패딩 비트는 체크섬 밖 */
        unsigned ip_ok = 0, udp_ok = 0, pad_ok = 0;
        uint8_t f[60]; pkt_info pi;
        for (unsigned b = 0; b < 160; b++) {
            memcpy(f, g_vec[0], 60); f[14 + b / 8] ^= (uint8_t)(1u << (b % 8));
            int e = parse_exact(f, 60, 60, &pi);
            if (e == PE_NONE && pi.ip_cs == CS_OK) ip_ok++;
        }
        for (unsigned b = 0; b < 104; b++) {
            memcpy(f, g_vec[0], 60); f[34 + b / 8] ^= (uint8_t)(1u << (b % 8));
            int e = parse_exact(f, 60, 60, &pi);
            if (e == PE_NONE && pi.udp_cs == CS_OK) udp_ok++;
        }
        for (unsigned b = 0; b < 13 * 8; b++) {
            memcpy(f, g_vec[0], 60); f[47 + b / 8] ^= (uint8_t)(1u << (b % 8));
            if (parse_exact(f, 60, 60, &pi) == PE_NONE && pi.ip_cs == CS_OK && pi.udp_cs == CS_OK) pad_ok++;
        }
        printf("B  single-bit flips accepted: ip %u/160, udp %u/104, pad (not covered) %u/104\n", ip_ok, udp_ok, pad_ok);
        CHECK(ip_ok == 0 && udp_ok == 0 && pad_ok == 104);
    }
    {   /* B: 1의 보수 합의 구멍 — 워드 순서 교환은 못 잡는다 (src ↔ dst IP 교환 → IP·UDP 둘 다 ok) */
        uint8_t f[60]; pkt_info pi;
        memcpy(f, g_vec[0], 60);
        uint8_t t[4]; memcpy(t, f + 26, 4); memcpy(f + 26, f + 30, 4); memcpy(f + 30, t, 4);
        CHECK(parse_exact(f, 60, 60, &pi) == PE_NONE && pi.ip_cs == CS_OK && pi.udp_cs == CS_OK && pi.ip_src == IP_B);
    }
    {   /* B: 캡처 잘림 전수 — 정상 벡터 × 모든 cut. exact-size 힙 버퍼, 결과는 none/trunc 뿐, 검증 불가한 걸 ok 라 하지 않음 */
        static const unsigned good_v[] = { 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 14, 16 };
        unsigned cuts = 0, bad = 0;
        for (size_t gi = 0; gi < sizeof good_v / sizeof good_v[0]; gi++) {
            unsigned v = good_v[gi]; pkt_info full, pi;
            vparse(v, &full);
            for (uint32_t cut = 0; cut <= g_vcap[v]; cut++) {
                int e = parse_exact(g_vec[v], cut, g_vlen[v], &pi);
                cuts++;
                bool ok = (e == PE_NONE || e == PE_TRUNC);
                if (pi.udp_cs == CS_OK || pi.udp_cs == CS_BAD) ok = ok && pi.l4_off + pi.ulen <= cut && pi.udp_cs == full.udp_cs;
                if (pi.has_ip) ok = ok && pi.l4_off <= cut && pi.ip_cs == full.ip_cs && pi.tot_len == full.tot_len;
                if (pi.has_udp) ok = ok && pi.sport == full.sport && pi.ulen == full.ulen;
                if (e == PE_NONE) ok = ok && pi.has_ip == full.has_ip && (pi.has_udp == full.has_udp);
                if (!ok) bad++;
            }
        }
        printf("B  truncation sweep: %u cuts, violations %u\n", cuts, bad);
        CHECK(cuts == 2404 && bad == 0);
    }
    {   /* B: pcap 파일 자체가 잘림 — 모든 바이트 위치에서 */
        unsigned bad = 0;
        size_t bounds[NVEC + 1]; bounds[0] = 24;
        for (unsigned i = 0; i < NVEC; i++) bounds[i + 1] = bounds[i] + 16 + g_vcap[i];
        for (size_t cut = 0; cut <= fn; cut++) {
            uint8_t *h = malloc(cut ? cut : 1);
            if (cut) memcpy(h, file, cut);
            pcap_reader r; pcap_rec rec; int rc = pcap_open(&r, h, cut); unsigned n = 0;
            if (rc == PCAP_OK) while ((rc = pcap_next(&r, &rec)) == PCAP_OK) n++;
            unsigned whole = 0; bool on_b = false;
            for (unsigned i = 0; i <= NVEC; i++) { if (bounds[i] <= cut) whole = i; if (bounds[i] == cut) on_b = true; }
            int expect = cut < 24 ? PCAP_E_SHORT : on_b ? PCAP_EOF : (cut - bounds[whole] < 16) ? PCAP_E_TRUNC_HDR : PCAP_E_TRUNC_DATA;
            if (rc != expect || (cut >= 24 && n != whole)) bad++;
            free(h);
        }
        printf("B  pcap file truncated at every offset 0..%zu: mismatches %u\n", fn, bad);
        CHECK(bad == 0);
        uint8_t f[24 + 16 + 60]; pcap_reader r; pcap_rec rec;
        memcpy(f, file, 24 + 16 + 60);
        wr32le(f + 24 + 8, 61); wr32le(f + 24 + 12, 60);                  /* caplen > len */
        CHECK(pcap_open(&r, f, sizeof f) == PCAP_OK && pcap_next(&r, &rec) == PCAP_E_CAPLEN);
        wr32le(f + 24 + 8, 0x7FFFFFFFu); wr32le(f + 24 + 12, 0x7FFFFFFFu); /* 거대한 caplen: 덧셈 넘침 없이 거절 */
        CHECK(pcap_open(&r, f, sizeof f) == PCAP_OK && pcap_next(&r, &rec) == PCAP_E_CAPLEN);
        wr32le(f + 24 + 8, 200); wr32le(f + 24 + 12, 200);                 /* 파일보다 큰 caplen */
        CHECK(pcap_open(&r, f, sizeof f) == PCAP_OK && pcap_next(&r, &rec) == PCAP_E_TRUNC_DATA);
    }
    {   /* B: 고정 시드 변이 퍼징 20000 회 — 크래시/OOB 없음(ASan) + 불변식 */
        unsigned bad = 0, n_ok = 0, n_err = 0;
        uint8_t f[VEC_MAX];
        for (unsigned it = 0; it < 20000; it++) {
            unsigned v = xs32() % NVEC;
            uint32_t len = g_vlen[v], cap = g_vcap[v];
            memcpy(f, g_vec[v], cap);
            unsigned nm = 1 + xs32() % 8;
            for (unsigned k = 0; k < nm; k++) {
                uint32_t pos = xs32() % (cap < 64 ? cap : 64);            /* 헤더 쪽을 주로 망가뜨림 */
                if (xs32() & 1) f[pos] ^= (uint8_t)(1u << (xs32() % 8)); else f[pos] = (uint8_t)xs32();
            }
            uint32_t c2 = (xs32() % 4 == 0) ? xs32() % (cap + 1) : cap;
            uint32_t l2 = (xs32() % 8 == 0) ? c2 + xs32() % 64 : len;     /* 가끔 wire len 도 바꿈 */
            if (l2 < c2) l2 = c2;
            pkt_info pi;
            int e = parse_exact(f, c2, l2, &pi);
            bool ok = e >= 0 && e < 10;
            if (pi.has_ip) ok = ok && pi.l3_off + (size_t)pi.ihl * 4u <= c2 && pi.l3_off + pi.tot_len <= l2;
            if (pi.udp_cs == CS_OK) ok = ok && pi.l4_off + pi.ulen <= c2 && pi.ucs == recompute_udp(pi.ip_src, pi.ip_dst, f + pi.l4_off, pi.ulen);
            if (pi.ip_cs == CS_OK) ok = ok && pi.hcs == recompute_ip(f + pi.l3_off, (size_t)pi.ihl * 4u);
            if (!ok) bad++;
            if (e == PE_NONE) n_ok++; else n_err++;
        }
        printf("B  fuzz 20000: parsed %u, rejected %u, invariant violations %u\n", n_ok, n_err, bad);
        CHECK(bad == 0 && n_ok > 1000 && n_err > 1000);
    }
    {   /* A+B: 랜덤 빌드 → 해석 왕복 3000 개 (태그·옵션·payload·체크섬 0 섞음) */
        unsigned bad = 0; uint8_t f[256], plb[64], ob[40]; frame_spec s; pkt_info pi;
        for (unsigned it = 0; it < 3000; it++) {
            int n = random_frame(f, sizeof f, &s, plb, ob);
            int e = parse_exact(f, (size_t)n, (size_t)n, &pi);
            bool ok = n > 0 && e == PE_NONE && pi.ntags == s.ntags && pi.ip_cs == CS_OK && pi.ihl == 5 + s.optlen / 4
                      && pi.ip_src == s.src_ip && pi.sport == s.sport && pi.dport == s.dport && pi.pay_len == s.pllen
                      && pi.udp_cs == (s.ucs_mode == UCS_ZERO ? CS_NONE : CS_OK) && memcmp(f + pi.pay_off, plb, s.pllen) == 0;
            if (!ok) bad++;
        }
        CHECK(bad == 0);
    }

    /* ===== Part C ===== */
    {   /* C1: RFC 1624 §3 예제 — 다른 필드 합 0xCD7A, m 0x5555 → m' 0x3285 */
        uint32_t s0 = 0xCD7Au + 0x5555u; while (s0 >> 16) s0 = (s0 & 0xFFFF) + (s0 >> 16);   /* 0x22D0 */
        uint16_t hc = (uint16_t)~s0;                                       /* 0xDD2F */
        CHECK(hc == 0xDD2Fu);
        CHECK(csum_update16(hc, 0x5555, 0x3285) == 0x0000u);               /* 다시 계산한 값과 같음 */
        CHECK(csum_update16_rfc1141(hc, 0x5555, 0x3285) == 0xFFFFu);       /* −0 */
        uint32_t s = 0xCD7Au + 0x3285u; while (s >> 16) s = (s & 0xFFFF) + (s >> 16);
        CHECK((uint16_t)~s == 0x0000u);
    }
    {   /* C1: 전수 — 벡터 0 의 IP 헤더에서 id 워드를 0..65535 로 바꿀 때 증분 == 재계산 */
        uint8_t ip[20]; memcpy(ip, g_vec[0] + 14, 20);
        uint16_t m = rd16be(ip + 4), hc = rd16be(ip + 10);
        unsigned mism3 = 0, mism2 = 0, verify2_fail = 0;
        for (uint32_t m2 = 0; m2 <= 0xFFFF; m2++) {
            wr16be(ip + 4, (uint16_t)m2);
            uint16_t full = recompute_ip(ip, 20);
            uint16_t e3 = csum_update16(hc, m, (uint16_t)m2), e2 = csum_update16_rfc1141(hc, m, (uint16_t)m2);
            if (e3 != full) mism3++;
            if (e2 != full) {
                mism2++;
                wr16be(ip + 10, e2);
                if (csum_fold(csum_add(0, ip, 20)) != 0) verify2_fail++;
                wr16be(ip + 10, hc);
            }
        }
        printf("C  id word 0..65535: RFC1624 eqn3 mismatches %u, RFC1141 eqn2 mismatches %u (still verifies: %s)\n",
               mism3, mism2, verify2_fail ? "no" : "yes");
        CHECK(mism3 == 0 && mism2 >= 1 && verify2_fail == 0);
    }
    {   /* C2: TTL 감소 — 랜덤 프레임 2000 개 × TTL 끝까지, 매 단계 재계산과 비교 */
        unsigned bad = 0, steps = 0; uint8_t f[256], plb[64], ob[40]; frame_spec s; pkt_info pi;
        for (unsigned it = 0; it < 2000; it++) {
            int n = random_frame(f, sizeof f, &s, plb, ob);
            pkt_parse(f, (size_t)n, (size_t)n, &pi);
            while (pi.ttl > 1) {
                if (ip_ttl_dec(f, (size_t)n, &pi) != RW_OK) { bad++; break; }
                steps++;
                if (pi.hcs != recompute_ip(f + pi.l3_off, (size_t)pi.ihl * 4u) || rd16be(f + pi.l3_off + 10) != pi.hcs) { bad++; break; }
            }
            uint8_t before[256]; memcpy(before, f, (size_t)n);
            if (ip_ttl_dec(f, (size_t)n, &pi) != RW_E_TTL || memcmp(before, f, (size_t)n) != 0) bad++;   /* TTL 1: 건드리지 않음 */
        }
        printf("C  ttl_dec: %u steps, mismatches %u\n", steps, bad);
        CHECK(bad == 0 && steps > 200000);
    }
    {   /* C3: NAT — 랜덤 2000 개, 증분 결과 == 새 값으로 처음부터 만든 프레임 */
        unsigned bad = 0, zero_kept = 0, ffff = 0; uint8_t f[256], g[256], plb[64], ob[40]; frame_spec s; pkt_info pi, pj;
        for (unsigned it = 0; it < 2000; it++) {
            int n = random_frame(f, sizeof f, &s, plb, ob);
            pkt_parse(f, (size_t)n, (size_t)n, &pi);
            uint32_t nip = xs32(); uint16_t np = (uint16_t)xs32();
            int mode = s.ucs_mode;
            if (it < 40) {   /* 일부는 결과 체크섬이 정확히 0 이 되도록 포트를 골라 0→0xFFFF 규칙을 시험 */
                s.src_ip = nip; s.sport = 0; s.ucs_mode = UCS_AUTO; build_frame(&s, g, sizeof g);
                pkt_info t; pkt_parse(g, (size_t)n, (size_t)n, &t);
                uint32_t sum = csum_add(udp_pseudo_sum(t.ip_src, t.ip_dst, t.ulen), g + t.l4_off, 6);
                sum = csum_add(sum, g + t.l4_off + 8, t.ulen - 8u);
                while (sum >> 16) sum = (sum & 0xFFFF) + (sum >> 16);
                np = (uint16_t)~sum;                                        /* 포트 워드 = ~합 → 전체 합 0xFFFF → 체크섬 0 */
            }
            bool had_zero = pi.ucs == 0;
            if (nat_rewrite(f, (size_t)n, &pi, nip, np) != RW_OK) { bad++; continue; }
            s.src_ip = nip; s.sport = np; s.ucs_mode = mode;
            build_frame(&s, g, sizeof g);
            if (memcmp(f, g, (size_t)n) != 0) bad++;
            if (pkt_parse(f, (size_t)n, (size_t)n, &pj) != PE_NONE || pj.ip_cs != CS_OK || pj.udp_cs != (had_zero ? CS_NONE : CS_OK)) bad++;
            if (had_zero && pj.ucs == 0) zero_kept++;
            if (!had_zero && pj.ucs == 0xFFFF) ffff++;
        }
        printf("C  nat_rewrite: 2000 frames, mismatches %u, ucs=0 kept %u, result 0xFFFF %u\n", bad, zero_kept, ffff);
        CHECK(bad == 0 && zero_kept > 100 && ffff >= 30);
        pkt_info p4;
        uint8_t fr[VEC_MAX]; memcpy(fr, g_vec[4], g_vlen[4]);
        vparse(4, &p4);
        CHECK(nat_rewrite(fr, g_vlen[4], &p4, IP_B, 1) == RW_E_FRAG && memcmp(fr, g_vec[4], g_vlen[4]) == 0);
    }
    {   /* C3: 증분 갱신은 "망가진 체크섬을 망가진 채로" 둔다 — 재계산은 손상을 세탁한다 */
        uint8_t f[60]; pkt_info pi, pj;
        memcpy(f, g_vec[9], 60);                                            /* UDP 체크섬이 틀린 벡터 */
        f[42] ^= 0x20;                                                      /* + 전송 중 payload 손상 가정 */
        pkt_parse(f, 60, 60, &pi);
        CHECK(pi.udp_cs == CS_BAD && nat_rewrite(f, 60, &pi, 0x0A000001u, 40000) == RW_OK);
        CHECK(pkt_parse(f, 60, 60, &pj) == PE_NONE && pj.ip_cs == CS_OK && pj.udp_cs == CS_BAD);
    }
    {   /* C: bring-up 요약 — 시험 벡터 파일 전체 */
        pcap_stats st;
        dump_buf(file, fn, NULL, &st);
        stats_line(line, sizeof line, &st);
        printf("C  vectors %s\n", line);
        CHECK(st.frames == 17 && st.vlan == 2 && st.ip_bad == 1 && st.udp_bad == 1 && st.frag == 2 && st.err[PE_IP_IHL] == 1
              && st.err[PE_IP_TOTLEN] == 1 && st.err[PE_UDP_LEN] == 1 && st.udp_none == 1 && st.udp_unverif == 2 && st.other_l3 == 1);
    }

    printf("%d/%d passed\n", g_pass, g_total);
    return g_pass == g_total ? 0 : 1;
}
