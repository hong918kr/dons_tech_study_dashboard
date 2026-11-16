"""PCIE-B06: QEMU 덤프(lspci -F 형식)에서 PCIe cap 의 LnkCap/LnkSta, AER UESta 바이트만 고친 '가상 덤프'를 만든다.
값은 실HW 형식 예시용 가정값이다 (실제 SSD 캡처 아님). 사용: python3 -I make_variants.py <qemu.dump> <outdir>"""
import sys

RP, EP = '00:02.0', '01:00.0'
RPX, EPX = 0x54, 0x80          # 루트 포트 / NVMe 의 PCIe capability 위치 (캡처에서 확인)
LNKCAP, LNKSTA, AER_UESTA = 0x0c, 0x12, 0x104


def load(path):
    devs, order, cur = {}, [], None
    for line in open(path):
        line = line.rstrip('\n')
        if not line.strip():
            continue
        head = line.split(':', 1)[0]
        if line[len(head):len(head) + 2] == ': ' and all(c in '0123456789abcdef' for c in head):
            off = int(head, 16)
            for i, b in enumerate(line.split(': ', 1)[1].split()):
                devs[cur][off + i] = int(b, 16)
        else:
            cur = line.split()[0]
            order.append(cur)
            devs[cur] = bytearray(4096)
    return devs, order


def save(devs, order, path):
    with open(path, 'w') as f:
        for d in order:
            f.write(d + ' dev\n')
            for off in range(0, 4096, 16):
                f.write('%02x: %s\n' % (off, ' '.join('%02x' % b for b in devs[d][off:off + 16])))
            f.write('\n')


def put(b, off, v, n):
    for i in range(n):
        b[off + i] = (v >> (8 * i)) & 0xff


def x2(d):      # Gen4 x4 SSD 가 Gen4 x2 로 학습 (루트 포트도 x4 슬롯으로 가정)
    put(d[RP], RPX + LNKCAP, 0x00300444, 4)   # 16GT/s x4, ASPM L0s, DLLLARC, LBNC
    put(d[RP], RPX + LNKSTA, 0x2024, 2)       # 16GT/s x2, DLActive+
    put(d[EP], EPX + LNKCAP, 0x00000844, 4)   # 16GT/s x4, ASPM L1
    put(d[EP], EPX + LNKSTA, 0x0024, 2)       # 16GT/s x2


def gen1(d):    # 같은 SSD 가 2.5GT/s x4 로 학습
    put(d[RP], RPX + LNKCAP, 0x00300444, 4)
    put(d[RP], RPX + LNKSTA, 0x2041, 2)
    put(d[EP], EPX + LNKCAP, 0x00000844, 4)
    put(d[EP], EPX + LNKSTA, 0x0041, 2)


def dldown(d):  # 카드는 꽂혀 있는데(PresDet+) 링크가 안 올라옴
    put(d[RP], RPX + LNKSTA, 0x0001, 2)


def over(d):    # LnkSta 가 LnkCap 보다 빠름 (lspci 의 "(overdriven)" 표시 확인용, 비정상 값)
    put(d[EP], EPX + LNKCAP, 0x00000842, 4)   # 5GT/s x4
    put(d[EP], EPX + LNKSTA, 0x0044, 2)       # 16GT/s x4


def cmplto(d):  # 루트 포트 AER 에 Completion Timeout 기록
    put(d[RP], AER_UESTA, 0x00004000, 4)


if __name__ == '__main__':
    base, order = load(sys.argv[1])
    for name, fn in (('x2', x2), ('gen1', gen1), ('dldown', dldown), ('over', over), ('cmplto', cmplto)):
        d = {k: bytearray(v) for k, v in base.items()}
        fn(d)
        save(d, order, '%s/var_%s.dump' % (sys.argv[2], name))
