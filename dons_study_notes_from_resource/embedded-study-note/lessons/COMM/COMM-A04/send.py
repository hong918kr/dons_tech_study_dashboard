"""COMM-A04 실캡처용 송신기 — Lima VM(elinux) 안에서 실행 (loopback 만 사용).

  sudo tcpdump -i lo -U -w /tmp/COMM-A04_work/lo.pcap udp port 50404 &
  sudo tcpdump -i lo -U -s 60 --time-stamp-precision=nano -w /tmp/COMM-A04_work/lo_ns_snap60.pcap udp port 50404 &
  python3 send.py ; sudo pkill -INT -x tcpdump
"""
import socket, time
DST = ("127.0.0.1", 50404)
def send(payload, sport, opts=None, nocheck=False):
    s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    s.bind(("127.0.0.1", sport))
    if opts is not None:
        s.setsockopt(socket.IPPROTO_IP, socket.IP_OPTIONS, opts)
    if nocheck:
        s.setsockopt(socket.SOL_SOCKET, 11, 1)   # SO_NO_CHECK
    s.sendto(payload, DST)
    s.close()
    time.sleep(0.05)
send(b"hello", 40001)                       # 1: odd payload (5 B)
send(b"ping", 40002)                        # 2: even payload
send(b"", 40003)                            # 3: empty payload
send(b"nocsum", 40004, nocheck=True)        # 4: SO_NO_CHECK -> checksum 0
send(b"opts!", 40005, opts=b"\x01\x01\x01\x00")  # 5: IP options NOP NOP NOP EOL -> IHL 6
send(bytes(range(48)), 40006)               # 6: 48 B payload (longer than snaplen-60 capture)
