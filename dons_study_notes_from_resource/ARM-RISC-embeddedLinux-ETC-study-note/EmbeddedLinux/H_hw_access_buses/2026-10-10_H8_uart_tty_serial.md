# H8. UART · TTY · serial — serial core, TTY 계층, line discipline, termios, serdev

> **이 노트를 다 읽으면**: UART 드라이버 → serial core → TTY core → line discipline → `/dev/ttyXXX`로 이어지는 5단 구조와 각 층의 구조체(`uart_port`·`uart_ops`·`tty_struct`·`tty_port`·`tty_ldisc_ops`)를 그릴 수 있다 · `termios`로 raw 모드·VMIN/VTIME·흐름 제어를 정확히 설정해 GNSS NMEA나 바이너리 프로토콜을 깨지지 않게 읽을 수 있다 · DT 자식 노드 + `serdev_device_driver`로 UART 위 장치(GNSS·BT·MCU)를 커널 드라이버로 붙일 수 있다 · 콘솔(`console=`/`earlycon`)과 RS-485 설정을 설명할 수 있다
> **왜 필요한가**: 센서 키트의 GNSS·BT·보조 MCU는 거의 다 UART에 붙는다. "NMEA가 가끔 깨진다", "바이너리 프로토콜에서 0x0D가 0x0A로 바뀐다", "BT 모듈을 hciattach 대신 DT로 붙여라" 같은 일이 입사 첫 달에 나오고, 면접에서는 "TTY 계층과 line discipline을 설명하라", "raw 모드가 왜 필요한가"가 단골이다
> **Don 기준 난이도**: UART 하드웨어·FIFO·baud 분주·IRQ·링버퍼는 이미 안다 / 새로 배울 부분은 그 위에 얹힌 **TTY 계층(옛 텔레타이프 시절의 "터미널" 추상화)과 line discipline**, `termios` 플래그의 의미, 그리고 serdev로 "UART 장치"를 커널 드라이버로 만드는 방법
> **선행 노트**: A3(파일 I/O와 디바이스 접근), D1–D3(Device Tree), E1(커널 아키텍처 — `/dev/ttyAMA0`의 `read()`를 `n_tty_read`까지 따라감), F7(워크큐), G2–G4(드라이버 모델·문자 디바이스), H6(인터럽트 컨트롤러)

---

## 0. 큰 그림 — 이게 왜 필요한가

Don이 SSD 펌웨어에서 UART를 쓸 때는 구조가 단순했다. RX ISR이 데이터 레지스터에서 바이트를 꺼내 링버퍼에 넣고, 디버그 셸 태스크가 그 링버퍼에서 한 줄씩 꺼내 명령을 파싱한다. 층은 둘뿐이다: **드라이버(ISR + 링버퍼)**와 **소비자(셸)**.

Linux에서 UART는 다섯 층이다. 바이트 하나가 앱에 닿기까지 이 층을 모두 지난다.

```svg
<svg viewBox="0 0 680 470" xmlns="http://www.w3.org/2000/svg">
  <defs><marker id="ah1" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto-start-reverse"><path d="M0,0 L10,5 L0,10 z" fill="currentColor"/></marker></defs>
  <rect x="20" y="10" width="400" height="44" rx="5" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
  <text x="32" y="30" font-size="13" font-weight="bold">앱 (유저 공간)</text>
  <text x="32" y="46" font-size="12">open("/dev/ttyAMA1") · read() · tcsetattr()</text>
  <rect x="460" y="10" width="200" height="44" rx="5" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
  <text x="472" y="30" font-size="13" font-weight="bold">앱 또는 커널 소비자</text>
  <text x="472" y="46" font-size="12">/dev/gnss0 · hci0 (BT)</text>
  <rect x="20" y="74" width="400" height="44" rx="5" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
  <text x="32" y="94" font-size="13" font-weight="bold">④ line discipline (N_TTY)</text>
  <text x="32" y="110" font-size="12">줄 편집 · echo · ^C 신호 · CR→LF · n_tty.c</text>
  <rect x="460" y="74" width="200" height="108" rx="5" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
  <text x="472" y="94" font-size="13" font-weight="bold">serdev 장치 드라이버</text>
  <text x="472" y="112" font-size="12">gnss-mtk · h8_gnss</text>
  <text x="472" y="130" font-size="12">receive_buf() 콜백</text>
  <text x="472" y="148" font-size="12">ldisc와 /dev/tty 없음</text>
  <text x="472" y="166" font-size="12">drivers/tty/serdev</text>
  <rect x="20" y="138" width="400" height="44" rx="5" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
  <text x="32" y="158" font-size="13" font-weight="bold">③ TTY core</text>
  <text x="32" y="174" font-size="12">tty_struct · tty_port · flip buffer · tty_io.c</text>
  <rect x="20" y="202" width="640" height="44" rx="5" fill="#888" fill-opacity="0.12" stroke="#888"/>
  <text x="32" y="222" font-size="13" font-weight="bold">② serial core</text>
  <text x="32" y="238" font-size="12">uart_driver · xmit kfifo · termios → set_termios · serial_core.c</text>
  <rect x="20" y="266" width="640" height="44" rx="5" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
  <text x="32" y="286" font-size="13" font-weight="bold">① UART 하드웨어 드라이버</text>
  <text x="32" y="302" font-size="12">uart_port + uart_ops (startup · set_termios · start_tx) · amba-pl011.c</text>
  <rect x="20" y="330" width="640" height="44" rx="5" fill="#d0564a" fill-opacity="0.12" stroke="#d0564a"/>
  <text x="32" y="350" font-size="13" font-weight="bold">하드웨어</text>
  <text x="32" y="366" font-size="12">PL011 레지스터 (DR · FR · IBRD/FBRD · LCR_H · IMSC) · FIFO · IRQ → GIC</text>
  <rect x="20" y="394" width="640" height="56" rx="5" fill="#888" fill-opacity="0.12" stroke="#888"/>
  <text x="32" y="414" font-size="13" font-weight="bold">선로 위의 장치</text>
  <text x="32" y="432" font-size="12">GNSS 수신기 (NMEA/UBX) · BT 모듈 (HCI H4) · 보조 MCU · RS-485 모터 드라이버 · 디버그 콘솔</text>
  <line x1="220" y1="74" x2="220" y2="55" stroke="currentColor" marker-end="url(#ah1)"/>
  <line x1="220" y1="138" x2="220" y2="119" stroke="currentColor" marker-end="url(#ah1)"/>
  <line x1="220" y1="202" x2="220" y2="183" stroke="currentColor" marker-end="url(#ah1)"/>
  <line x1="560" y1="202" x2="560" y2="183" stroke="currentColor" marker-end="url(#ah1)"/>
  <line x1="560" y1="74" x2="560" y2="55" stroke="currentColor" marker-end="url(#ah1)"/>
  <line x1="220" y1="266" x2="220" y2="247" stroke="currentColor" marker-end="url(#ah1)"/>
  <line x1="220" y1="330" x2="220" y2="311" stroke="currentColor" marker-end="url(#ah1)"/>
  <line x1="220" y1="394" x2="220" y2="375" stroke="currentColor" marker-end="url(#ah1)"/>
</svg>
```
그림 1 — UART 수신 데이터가 올라가는 층. 왼쪽 기둥이 보통의 tty 경로(`/dev/ttyAMA1`), 오른쪽이 serdev 경로다. serdev가 붙은 포트는 TTY core까지는 같이 쓰지만 line discipline과 `/dev/tty*` 노드를 건너뛰고 커널 드라이버가 바이트를 직접 받는다(§8).

각 층을 펌웨어 감각으로 번역하면 이렇다.

| 층 | 펌웨어 비유 | Linux에서의 실체 | 드라이버 작성자가 쓰나? |
|---|---|---|---|
| ① UART 드라이버 | UART HAL + ISR | `struct uart_port` 하나 = 포트 하나, `struct uart_ops` = 하드웨어 조작 함수 표 | **예** (새 SoC UART) |
| ② serial core | 모든 UART HAL이 공유하는 공용 코드 | TX 링버퍼(`kfifo`), termios → `set_termios` 호출, 모뎀 선, 콘솔 | 아니오 (호출만) |
| ③ TTY core | 바이트 스트림 장치의 공통 틀 | `tty_struct`(열린 터미널), `tty_port`(물리 포트), flip buffer(RX 버퍼) | 아니오 |
| ④ line discipline | 디버그 셸의 줄 편집기 | `N_TTY`(기본), `N_GSM0710`, `N_HCI` … 교체 가능한 "프로토콜 해석기" | 드물게 |
| serdev | "UART에 붙은 칩"의 드라이버 | `serdev_device_driver` — GNSS·BT·MCU | **예** (UART 장치) |

왜 이렇게 많은 층이 있나. TTY는 원래 1970년대 **텔레타이프(teletype) 터미널**이 시리얼 선으로 유닉스에 붙던 구조다. 사람이 타이핑하는 글자를 줄 단위로 모으고(Backspace 처리), 화면에 다시 찍고(echo), Ctrl-C면 프로그램을 죽이는 일을 커널이 해 줬다. 그 "사람용 처리" 층이 line discipline이고, 오늘날 GNSS나 MCU처럼 **기계가 말하는 바이너리 스트림**에는 그 처리가 오히려 독이 된다. 이 노트의 절반은 그 독을 빼는 법(§5, §6)이다.

이 노트의 순서: §1 실습 장치 만들기 → §2 `/proc`·`/sys`로 층 들여다보기 → §3 UART 드라이버 층과 baud → §4 TTY core와 RX 경로 → §5 line discipline → §6 termios → §7 콘솔 → §8 serdev와 GNSS 서브시스템 → §9 RS-485 → §10 처리량·지연 측정.

> 실행 환경: Lima VM `elinux`(aarch64, 호스트 커널 `uname -r` = `7.0.0-38-generic`, QEMU 10.2.1) 안에서 공용 랩 커널 **Linux 6.18.55**(BusyBox initramfs)를 QEMU `virt`(cortex-a57, 512 MB, TCG)로 부팅해 실험했다. 게스트 바이너리는 VM에서 `gcc -static -Wall -Wextra -O2`로 빌드(경고 0개), 커널 모듈은 `make -C /var/lab/out/linux-build M=$PWD`로 빌드했다. 랩은 읽기 전용으로만 썼고 산출물은 VM의 `/var/tmp/H8/`에 만들었다. 소스 줄 번호는 `/var/lab/src/linux-6.18.55` 기준이다. 시각·주소·타이밍 값은 실행마다 다르다.

---

## 1. 실습 장치 만들기 — 두 번째 UART와 가짜 GNSS 모듈

### 1.1 직관

E1에서는 콘솔 UART(`ttyAMA0`)를 읽었다. 콘솔은 커널 로그와 셸이 쓰고 있어서 실험용으로 쓰기 곤란하다. 실제 보드라면 GNSS 모듈은 **별도 UART**(예: i.MX8의 `ttymxc1`)에 붙는다. 그래서 QEMU에 UART를 하나 더 달고, 그 반대쪽 끝(선로 건너편)을 VM 호스트의 Python 스크립트로 만든다. 이 스크립트가 "GNSS 모듈" 역할을 해서 NMEA 문장을 보낸다.

```svg
<svg viewBox="0 0 680 250" xmlns="http://www.w3.org/2000/svg">
  <defs><marker id="ah2" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto-start-reverse"><path d="M0,0 L10,5 L0,10 z" fill="currentColor"/></marker></defs>
  <rect x="10" y="10" width="300" height="230" rx="6" fill="#888" fill-opacity="0.08" stroke="#888"/>
  <text x="22" y="30" font-size="13" font-weight="bold">VM 호스트 (Ubuntu, 커널 7.0)</text>
  <rect x="25" y="45" width="270" height="70" rx="4" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
  <text x="37" y="66" font-size="13">h8_gen.py — 가짜 GNSS 모듈</text>
  <text x="37" y="85" font-size="12">NMEA를 9600 baud 속도로 송신</text>
  <text x="37" y="103" font-size="12">게스트가 보낸 바이트(echo) 기록</text>
  <rect x="25" y="140" width="270" height="44" rx="4" fill="#888" fill-opacity="0.12" stroke="#888"/>
  <text x="37" y="160" font-size="12">unix socket /var/tmp/H8/h8_TAG.sock</text>
  <text x="37" y="176" font-size="12">= 선로 (TX/RX 두 가닥)</text>
  <rect x="340" y="10" width="330" height="230" rx="6" fill="#4a7bd0" fill-opacity="0.08" stroke="#4a7bd0"/>
  <text x="352" y="30" font-size="13" font-weight="bold">QEMU virt 게스트 (Linux 6.18.55)</text>
  <rect x="355" y="45" width="300" height="44" rx="4" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
  <text x="367" y="65" font-size="12">pl011@9000000 = ttyAMA0 (콘솔, stdio)</text>
  <text x="367" y="81" font-size="12">-serial mon:stdio</text>
  <rect x="355" y="100" width="300" height="44" rx="4" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
  <text x="367" y="120" font-size="12">pl011@9040000 = ttyAMA1 (실험용)</text>
  <text x="367" y="136" font-size="12">-serial unix:SOCK, IRQ = GIC SPI 8</text>
  <rect x="355" y="160" width="300" height="64" rx="4" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
  <text x="367" y="180" font-size="12">h8_nmea · h8_bin · h8_gga · h8_tput (C)</text>
  <text x="367" y="198" font-size="12">또는 DT 자식 노드 + serdev 드라이버</text>
  <text x="367" y="216" font-size="12">(gnss-mtk, h8_gnss.ko)</text>
  <line x1="296" y1="162" x2="354" y2="122" stroke="currentColor" marker-end="url(#ah2)" marker-start="url(#ah2)"/>
  <line x1="160" y1="116" x2="160" y2="139" stroke="currentColor" marker-end="url(#ah2)" marker-start="url(#ah2)"/>
</svg>
```
그림 2 — 실습 배선. 두 번째 PL011의 "선로"가 VM 호스트의 unix socket이고, 그 끝에서 Python이 GNSS 모듈 흉내를 낸다. 실제 보드라면 이 자리에 u-blox나 MediaTek GNSS 칩이 TX/RX 두 가닥으로 붙는다.

### 1.2 예제 1 — QEMU `virt`에 PL011을 하나 더 달 수 있나

무엇을 확인하나: `-serial`을 두 번 주면 `virt` 머신이 두 번째 PL011을 DT에 넣는지 DTB를 덤프해서 본다(QEMU 버전에 따라 다르므로 직접 확인해야 한다).

```sh
limactl shell elinux -- bash -lc 'mkdir -p /var/tmp/H8 && cd /var/tmp/H8 && timeout 20 qemu-system-aarch64 -machine virt,dumpdtb=h8_two.dtb -cpu cortex-a57 -m 512M -nographic -serial mon:stdio -serial null; dtc -I dtb -O dts h8_two.dtb 2>/dev/null | grep -A6 "pl011@"; dtc -I dtb -O dts h8_two.dtb 2>/dev/null | grep -A3 "aliases {"'
```

```text
	pl011@9000000 {
		clock-names = "uartclk", "apb_pclk";
		clocks = <0x8000 0x8000>;
		interrupts = <0x00 0x01 0x04>;
		reg = <0x00 0x9000000 0x00 0x1000>;
		compatible = "arm,pl011", "arm,primecell";
	};
--
	pl011@9040000 {
		clock-names = "uartclk", "apb_pclk";
		clocks = <0x8000 0x8000>;
		interrupts = <0x00 0x08 0x04>;
		reg = <0x00 0x9040000 0x00 0x1000>;
		compatible = "arm,pl011", "arm,primecell";
	};
	aliases {
		serial0 = "/pl011@9000000";
		serial1 = "/pl011@9040000";
	};
```

출력에서 볼 것:

- QEMU 10.2의 `virt`는 `-serial`이 두 개면 **두 번째 PL011을 `0x9040000`, SPI 8**에 만든다(옛 QEMU는 이 주소의 UART를 `secure=on`일 때 Secure 월드용으로만 썼다). `aliases`의 `serial1`이 리눅스에서 `ttyAMA1`이라는 번호가 된다(`pl011_probe_dt_alias`, `amba-pl011.c:2776`).
- 이 노드는 진짜 DT 노드이므로 **자식 노드를 달아 serdev 장치를 기술할 수 있다**(§8). PCI 16550(`-device pci-serial`)이나 virtio-console은 DT 노드가 없어 serdev 실험에 못 쓴다.

참고로 다른 두 방법도 이 랩 커널에서 동작은 한다(예제 1b). 무엇을 확인하나: `-device pci-serial`(PCI에 붙은 16550, 드라이버 `8250_pci`, `CONFIG_SERIAL_8250_PCI=y`)과 virtio 시리얼 포트가 어떤 장치 노드가 되는지.

```sh
limactl shell elinux -- bash -lc 'cd /var/tmp/H8 && timeout 60 qemu-system-aarch64 -machine virt -cpu cortex-a57 -m 512M -nographic -kernel /var/lab/out/Image -initrd /var/lab/out/initramfs.cpio.gz -chardev null,id=h8p -device pci-serial,chardev=h8p -device virtio-serial-device -chardev null,id=h8v -device virtserialport,chardev=h8v,name=h8.gnss -append "console=ttyAMA0 quiet LAB_CMD=dmesg~|~grep~-E~\"ttyS|0000:00:0\"~|~tail~-4;~ls~/dev/ttyS*~/dev/vport*;~cat~/sys/class/virtio-ports/vport*/name;~grep~-v~unknown~/proc/tty/driver/serial" </dev/null 2>&1 | tr -d "\r" | grep -av "^lab init"'
```

```text
[    0.511073] virtio-pci 0000:00:01.0: enabling device (0000 -> 0003)
[    0.528699] serial 0000:00:02.0: enabling device (0000 -> 0001)
[    0.556206] 0000:00:02.0: ttyS0 at I/O 0x1020 (irq = 16, base_baud = 115200) is a 16550A
/dev/ttyS0     /dev/ttyS1     /dev/ttyS2     /dev/ttyS3     /dev/vport0p1
h8.gnss
serinfo:1.0 driver revision:
0: uart:16550A port:00001020 irq:16 tx:0 rx:0 CTS|DSR|CD
```

(LAB_CMD 에코 줄과 `ls`의 색 제어 문자는 지웠다.) PCI 16550은 `ttyS0`(16550A, `base_baud = 115200` = 1.8432 MHz / 16)이 되고, `ttyS1~3`은 `CONFIG_SERIAL_8250_RUNTIME_UARTS=4` 때문에 미리 만들어진 빈 자리다. virtio 포트는 TTY가 아니라 `/dev/vport0p1`이라는 **단순 문자 장치**다(termios가 없다). 이 노트는 실제 SoC와 가장 닮은 두 번째 PL011을 쓴다.

### 1.3 예제 2 — 가짜 GNSS 모듈 (VM 호스트 쪽 Python)

무엇을 확인하나: 선로 건너편 장치를 흉내 낸다. NMEA 문장은 `$`로 시작해 `*` 뒤에 체크섬(`$`와 `*` 사이 모든 바이트의 XOR, 16진 두 자리), 끝에 `\r\n`이 붙는다. 송신 속도는 "8N1에서 1바이트 = 10비트"로 계산해 9600 baud인 척 문장마다 쉬어 준다.

```python
#!/usr/bin/env python3
"""h8_gen.py (1/2): fake GNSS module on the VM host side of a QEMU serial port.
listens on a unix socket; QEMU connects with -serial unix:PATH.
modes: nmea (paced NMEA), bin (bytes 0..255), flood (unpaced), echo."""
import socket, sys, time, os, threading

path, mode = sys.argv[1], sys.argv[2]
baud = int(sys.argv[3]) if len(sys.argv) > 3 else 9600
count = int(sys.argv[4]) if len(sys.argv) > 4 else 10
wait_cmd = os.environ.get("H8_WAIT", "1") == "1"

def nmea(body):
    cs = 0
    for ch in body.encode():
        cs ^= ch
    return ("$%s*%02X\r\n" % (body, cs)).encode()

def gga(i):
    t = time.gmtime(1791590400 + i)          # fixed fake date
    hhmmss = time.strftime("%H%M%S", t)
    lat = "3746.%04d" % (4940 + i)           # around 37.77N (SF)
    return nmea("GPGGA,%s.00,%s,N,12225.1640,W,1,08,0.9,%d.0,M,-25.0,M,,"
                % (hhmmss, lat, 15 + i % 5))

def rmc(i):
    t = time.gmtime(1791590400 + i)
    return nmea("GPRMC,%s.00,A,3746.%04d,N,12225.1640,W,0.02,0.0,%s,,,A"
                % (time.strftime("%H%M%S", t), 4940 + i,
                   time.strftime("%d%m%y", t)))

if os.path.exists(path):
    os.unlink(path)
srv = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
srv.bind(path)
srv.listen(1)
conn, _ = srv.accept()
log = open(path + ".log", "w")
lines = []                 # complete lines from guest while idle
echo = bytearray()         # bytes from guest while streaming
streaming = threading.Event()
cv = threading.Condition()

def reader():
    buf = bytearray()
    while True:
        try:
            d = conn.recv(4096)
        except OSError:
            break
        if not d:
            break
        if mode == "echo":
            conn.sendall(d)
            continue
        if streaming.is_set():
            echo.extend(d)
            continue
        buf.extend(d)
        while b"\n" in buf:
            line, _, rest = bytes(buf).partition(b"\n")
            buf[:] = rest
            with cv:
                lines.append(line)
                cv.notify()
```

```python
# h8_gen.py (2/2): one "session" per command line from the guest
threading.Thread(target=reader, daemon=True).start()
if mode == "echo":
    threading.Event().wait(600)
session = 0
while True:
    if wait_cmd:
        with cv:
            if not cv.wait_for(lambda: lines, 120):
                break
            cmd = lines.pop(0)
    else:
        cmd = b"(no wait)"
    m, b, c = mode, baud, count
    if cmd.startswith(b"$H8,"):                 # $H8,mode,baud,count
        f = cmd.decode().strip().split(",")
        m, b, c = f[1], int(f[2]), int(f[3])
    session += 1
    echo.clear()
    streaming.set()
    time.sleep(0.2)
    sent = 0
    t0 = time.time()
    if m == "nmea":
        for i in range(c):
            for s in (gga(i), rmc(i)):
                conn.sendall(s)
                sent += len(s)
                time.sleep(len(s) * 10.0 / b)   # 10 bits per byte (8N1)
    elif m == "bin":
        data = bytes(range(256)) + b"\n"
        conn.sendall(data)
        sent = len(data)
    elif m == "flood":
        blk = (b"0123456789ABCDEF" * 64)
        while sent < c:
            conn.sendall(blk)
            sent += len(blk)
    dt = time.time() - t0
    time.sleep(1.0)
    streaming.clear()
    log.write("session %d cmd=%r mode=%s sent=%d bytes in %.3f s; "
              "bytes sent back by guest=%d\n"
              % (session, cmd.strip(), m, sent, dt, len(echo)))
    if echo:
        log.write("  first echoed bytes: %r\n" % bytes(echo[:48]))
    log.flush()
    if not wait_cmd:
        break
log.close()
threading.Event().wait(600)
```

동작 규칙: 게스트가 한 줄(예: MediaTek GNSS의 설정 명령 `$PMTK220,1000*1F` = "1초마다 출력")을 보내면 그때부터 한 "세션" 동안 스트림을 보낸다. 실제 GNSS 모듈은 전원을 켜면 그냥 쏟아내지만, 실험마다 시작점을 맞추려고 이렇게 했다. 세션 동안 게스트가 보낸 바이트는 "echo"로 센다 — §6에서 쓴다.

> 에뮬레이터 주의: 이 스크립트는 **문장 단위로 몰아서** 보내고 문장 사이를 쉰다. 진짜 9600 baud 선로는 바이트마다 1.04 ms 간격으로 들어온다. 그리고 QEMU의 PL011은 baud 설정과 상관없이 소켓에 들어온 만큼 바로 RX FIFO에 넣는다(§10에서 측정). 그래서 타이밍 숫자는 "실제 보드에서도 이렇다"가 아니라 "커널 경로가 이렇게 동작한다"를 보는 용도다.

### 1.4 실행 도구 — `h8_run.sh`

무엇을 하나: initramfs에 게스트 프로그램을 넣고, Python 생성기를 띄운 뒤, 두 번째 시리얼을 그 소켓에 연결해 부팅한다. 이후 모든 예제가 이 스크립트로 돈다.

```sh
#!/bin/bash
# h8_run.sh TAG GUEST_SCRIPT GENMODE [BAUD COUNT] -- [extra qemu args]
# boots the lab kernel with a 2nd PL011 (ttyAMA1) wired to h8_gen.py
set -e
W=/var/tmp/H8; S=/Users/donh/.elinux_scratch/H8
TAG=$1; GS=$2; MODE=$3; BAUD=${4:-9600}; CNT=${5:-10}; shift 5 || true
[ "$1" = "--" ] && shift
cd $W
rm -rf r && mkdir r && cp -a /var/lab/build/rootfs/. r/
cp -r $W/bin/* r/
cp $GS r/h8_guest.sh
(cd r && find . | cpio -o -H newc --quiet | gzip) > h8_i.gz
SOCK=$W/h8_$TAG.sock
rm -f $SOCK $SOCK.log
python3 $S/h8_gen.py $SOCK $MODE $BAUD $CNT &
GP=$!
for i in $(seq 50); do [ -S $SOCK ] && break; sleep 0.1; done
timeout ${H8_TO:-90} qemu-system-aarch64 -machine virt -cpu cortex-a57 -m 512M \
  -nographic -serial mon:stdio -serial unix:$SOCK \
  -kernel /var/lab/out/Image -initrd h8_i.gz "$@" \
  -append "console=ttyAMA0 quiet ${H8_APPEND} LAB_CMD=sh~/h8_guest.sh" \
  < /dev/null 2>&1 | tr -d '\r' | grep -a -v "^lab initramfs" || true
sleep 0.5
kill $GP 2>/dev/null || true
wait $GP 2>/dev/null || true
echo "---- generator log (VM host) ----"
cat $SOCK.log 2>/dev/null || echo "(no log)"
rm -f $SOCK
```

게스트 C 프로그램은 VM에서 `gcc -std=gnu11 -Wall -Wextra -O2 -static X.c -o /var/tmp/H8/bin/X`로 빌드해 `bin/`에 두면 initramfs 루트에 들어간다. 호출 예: `limactl shell elinux -- bash -lc 'bash /Users/donh/.elinux_scratch/H8/h8_run.sh modes /Users/donh/.elinux_scratch/H8/g_modes.sh nmea 9600 6'`.

---

## 2. 층 들여다보기 — `/proc/tty`, `/sys/class/tty`, `stty`

### 2.1 예제 3 — 게스트에서 TTY 계층 훑어보기

무엇을 확인하나: 커널에 등록된 tty 드라이버와 line discipline 목록, `ttyAMA1`의 sysfs 속성, 기본 termios, 포트별 카운터.

```sh
# g_inspect.sh (guest)
echo "== /proc/tty/drivers"; cat /proc/tty/drivers
echo "== /proc/tty/ldiscs"; cat /proc/tty/ldiscs
echo "== ls /dev/ttyAMA*"; ls -l /dev/ttyAMA* | cat
echo "== /sys/class/tty/ttyAMA1"; ls /sys/class/tty/ttyAMA1/ | tr "\n" " "; echo
for f in type line port irq uartclk xmit_fifo_size io_type iomem_base close_delay; do echo "$f=$(cat /sys/class/tty/ttyAMA1/$f)"; done
echo "device -> $(readlink /sys/class/tty/ttyAMA1/device)"
echo "driver -> $(readlink /sys/class/tty/ttyAMA1/device/driver)"
echo "== /proc/consoles"; cat /proc/consoles
echo "== stty -a < /dev/ttyAMA1"; stty -a < /dev/ttyAMA1
echo "== /proc/interrupts (uart)"; grep -i uart /proc/interrupts
echo "== /proc/tty/driver/ttyAMA"; cat /proc/tty/driver/ttyAMA
```

```text
== /proc/tty/drivers
/dev/tty             /dev/tty        5       0 system:/dev/tty
/dev/console         /dev/console    5       1 system:console
/dev/ptmx            /dev/ptmx       5       2 system
/dev/vc/0            /dev/vc/0       4       0 system:vtmaster
tegra_hsuart         /dev/ttyTHS   236 0-7 serial
stm32-usart          /dev/ttySTM   237 0-8 serial
qcom_geni_uart       /dev/ttyHS    238 0-7 serial
qcom_geni_console    /dev/ttyMSM   239       0 serial
owl-uart             /dev/ttyOWL   240 0-6 serial
msm_serial           /dev/ttyMSM   241 0-2 serial
IMX-uart             /dev/ttymxc   207 16-23 serial
fsl-lpuart           /dev/ttyLP    242 0-11 serial
fsl-linflexuart      /dev/ttyLF    243 0-3 serial
serial               /dev/ttyS       4 64-71 serial
pty_slave            /dev/pts      136 0-1048575 pty:slave
pty_master           /dev/ptm      128 0-1048575 pty:master
pty_slave            /dev/ttyp       3 0-15 pty:slave
pty_master           /dev/pty        2 0-15 pty:master
unknown              /dev/tty        4 1-63 console
ttyAMA               /dev/ttyAMA   204 64-77 serial
mvebu_serial         /dev/ttyMV    253 0-1 serial
== /proc/tty/ldiscs
n_tty       0
n_null     27
== ls /dev/ttyAMA*
crw-------    1 0        0         204,  64 Jan  1  1970 /dev/ttyAMA0
crw-------    1 0        0         204,  65 Jan  1  1970 /dev/ttyAMA1
== /sys/class/tty/ttyAMA1
close_delay closing_wait console custom_divisor dev device flags io_type iomem_base iomem_reg_shift irq line port power subsystem type uartclk uevent xmit_fifo_size 
type=32
line=1
port=0x0
irq=14
uartclk=0
xmit_fifo_size=16
io_type=2
iomem_base=0x9040000
close_delay=50
device -> ../../../9040000.pl011:0.0
driver -> ../../../../../bus/serial-base/drivers/port
== /proc/consoles
ttyAMA0              -W- (EC Np a)  204:64
== stty -a < /dev/ttyAMA1
speed 9600 baud;stty: standard input
 line = 0;
intr = ^C; quit = ^\; erase = ^?; kill = ^U; eof = ^D; eol = <undef>;
eol2 = <undef>; swtch = <undef>; start = ^Q; stop = ^S; susp = ^Z; rprnt = ^R;
werase = ^W; lnext = ^V; flush = ^O; min = 1; time = 0;
-parenb -parodd -cmspar cs8 hupcl -cstopb cread clocal -crtscts
-ignbrk -brkint -ignpar -parmrk -inpck -istrip -inlcr -igncr icrnl ixon -ixoff
-iuclc -ixany -imaxbel -iutf8
opost -olcuc -ocrnl onlcr -onocr -onlret -ofill -ofdel nl0 cr0 tab0 bs0 vt0 ff0
isig icanon iexten echo echoe echok -echonl -noflsh -xcase -tostop -echoprt
echoctl echoke -flusho -extproc
== /proc/interrupts (uart)
 13:          0 GIC-0  33 Level     uart-pl011
== /proc/tty/driver/ttyAMA
serinfo:1.0 driver revision:
0: uart:PL011 rev1 mmio:0x09000000 irq:13 tx:2661 rx:0 RTS|DTR
1: uart:PL011 rev1 mmio:0x09040000 irq:14 tx:0 rx:0
```

출력에서 볼 것 — 층마다 하나씩:

- **`/proc/tty/drivers` = 등록된 `tty_driver` 목록**. 한 줄이 "이름, 노드 접두어, major, minor 범위, 종류"다. `ttyAMA 204 64-77`은 PL011 드라이버가 `uart_register_driver()`로 등록한 것이고, 그 안에서 serial core가 `tty_driver`를 만들었다. 하드웨어가 없는 `tegra_hsuart`, `IMX-uart`(`/dev/ttymxc`, i.MX 보드의 이름이 여기서 온다)까지 보이는 건 defconfig가 여러 SoC 드라이버를 빌트인으로 넣었고, `uart_register_driver()`는 장치가 없어도 모듈 init에서 불리기 때문이다. pty(가상 터미널 쌍, `ssh`·`tmux`가 쓰는 것)도 같은 TTY 계층의 드라이버다.
- **`/proc/tty/ldiscs` = 등록된 line discipline**. 기본 `n_tty`(번호 0)와 오류 처리용 `n_null`(27)뿐이다. `N_GSM0710`은 이 커널에서 `# CONFIG_N_GSM is not set`, BT용 `N_HCI`(15)는 `hci_uart` 모듈이 로드돼야 등록된다.
- **sysfs `ttyAMA1`**: `type=32`는 `PORT_AMBA`, `io_type=2`는 MMIO(`UPIO_MEM`), `xmit_fifo_size=16`은 드라이버가 알려 준 하드웨어 FIFO 깊이다. 부모 장치가 `9040000.pl011:0.0`이고 드라이버가 `serial-base` 버스의 `port`인 것은 E1에서 본 6.x serial core의 "포트 장치" 계층이다.
- **`uartclk=0`, 그리고 `/proc/interrupts`에 IRQ 14가 없다**. `ttyAMA1`은 아직 아무도 연 적이 없다. PL011 드라이버는 포트를 **처음 열 때** `uart_ops.startup`(`pl011_startup`, 1905행)에서 클럭을 켜고 `uartclk`를 읽으며(`pl011_hwinit` 1795행 `clk_get_rate`) IRQ를 요청한다(`pl011_allocate_irq`, 1916행). 펌웨어의 "UART init은 부팅 때 한 번"과 다르다. **안 쓰는 UART는 클럭도 IRQ도 안 잡는다** — 전력 관리 관점에서 당연한 설계다. `stty`가 잠깐 열었다 닫은 뒤라 지금은 다시 닫힌 상태다.
- **`stty -a`의 기본 termios**: `icanon`(줄 단위), `echo`(받은 글자를 되돌려 보냄), `isig`(^C를 신호로), `icrnl`(CR을 LF로), `ixon`(^S/^Q 흐름 제어), `opost onlcr`(보낼 때 LF를 CRLF로). **전부 사람 터미널용 기본값**이다. GNSS 모듈 앞에서 이게 무슨 일을 하는지가 §5–§6의 주제다. (`stty: standard input` 한 줄은 BusyBox `stty`가 창 크기 ioctl에 실패하며 찍는 메시지로, 결과와 무관하다.)
- **`/proc/tty/driver/ttyAMA`**: serial core가 포트마다 보여 주는 카운터(`tx`, `rx`, 그리고 에러가 있으면 `fe`(framing)·`pe`(parity)·`oe`(overrun)·`brk`)와 모뎀 선 상태(`RTS|DTR`). 현장에서 "데이터가 깨진다"고 하면 **가장 먼저 보는 파일**이다. `oe`가 늘면 FIFO 넘침(ISR 지연), `fe`가 늘면 baud 불일치나 노이즈다. root만 읽을 수 있다.

### 2.2 디바이스 번호와 이름의 출처

`/dev/ttyAMA1`의 `204, 65`는 PL011 드라이버의 `uart_driver`에서 온다: `.major = SERIAL_AMBA_MAJOR (204)`, `.minor = 64`(첫 minor), 그리고 포트 번호 `line = 1`을 더해 65. 이름 `ttyAMA`는 `.dev_name`이다(E1 예제 4에서 `"ttyAMA"`를 grep으로 찾았다). 보드마다 이름이 다른 이유가 이것이다.

| SoC | 드라이버 | 노드 이름 |
|---|---|---|
| ARM PL011 (QEMU, Raspberry Pi 일부, 많은 Arm 레퍼런스) | `amba-pl011.c` | `ttyAMA0` |
| 16550 호환 (x86 PC, TI AM62, Rockchip, PCI 카드) | `8250_*.c` | `ttyS0` |
| NXP i.MX6/7/8 | `imx.c` | `ttymxc0` |
| NXP i.MX8ULP·i.MX93 LPUART | `fsl_lpuart.c` | `ttyLP0` |
| NVIDIA Tegra (Jetson) 고속 UART | `serial-tegra.c` | `ttyTHS0` |
| USB-UART 변환 칩 (FTDI, CP210x) | `usb-serial` | `ttyUSB0` |
| USB CDC-ACM (MCU의 USB 가상 COM) | `cdc-acm` | `ttyACM0` |

---

## 3. ① UART 드라이버 층 — `uart_driver`, `uart_port`, `uart_ops`

### 3.1 세 구조체의 역할

- **`struct uart_driver`** — "이 종류의 UART 전체"를 대표한다. 이름(`ttyAMA`), major/minor, 포트 개수(`nr`), 콘솔 구조체. 드라이버 init에서 `uart_register_driver()`로 한 번 등록한다. 이때 serial core가 `tty_driver`를 만든다(§2의 `/proc/tty/drivers` 한 줄).
- **`struct uart_port`** — **포트 하나**. 레지스터 베이스(`membase`, `mapbase`), IRQ, `uartclk`, `fifosize`, 카운터(`icount`), 그리고 아래 `ops`를 가리킨다. probe에서 채워 `uart_add_one_port()`로 등록하면 `/dev/ttyAMA1`(또는 serdev 컨트롤러)이 생긴다.
- **`struct uart_ops`** — 하드웨어를 만지는 함수 표. serial core가 "지금 TX를 시작해", "baud를 바꿔"를 이 표로 부른다.

PL011의 ops 표가 그대로 교과서다.

```c
/* drivers/tty/serial/amba-pl011.c:2324 (linux-6.18.55) */
static const struct uart_ops amba_pl011_pops = {
	.tx_empty	= pl011_tx_empty,
	.set_mctrl	= pl011_set_mctrl,
	.get_mctrl	= pl011_get_mctrl,
	.stop_tx	= pl011_stop_tx,
	.start_tx	= pl011_start_tx,
	.stop_rx	= pl011_stop_rx,
	.throttle	= pl011_throttle_rx,
	.unthrottle	= pl011_unthrottle_rx,
	.enable_ms	= pl011_enable_ms,
	.break_ctl	= pl011_break_ctl,
	.startup	= pl011_startup,
	.shutdown	= pl011_shutdown,
	.flush_buffer	= pl011_dma_flush_buffer,
	.set_termios	= pl011_set_termios,
	.type		= pl011_type,
	.config_port	= pl011_config_port,
	.verify_port	= pl011_verify_port,
	...
};
```

| op | 언제 불리나 | 펌웨어로 치면 |
|---|---|---|
| `startup` / `shutdown` | 포트를 처음 열 때 / 마지막으로 닫을 때 | 클럭 켜기, FIFO 리셋, RX 인터럽트 enable, IRQ 등록 |
| `set_termios` | `tcsetattr()`, serdev `set_baudrate` | baud 분주기(IBRD/FBRD), 데이터 비트·패리티·정지 비트(LCR_H), RTS/CTS 자동 흐름 제어 enable |
| `start_tx` | TX 링버퍼에 데이터가 들어왔을 때 | TX 인터럽트 enable 또는 FIFO에 바로 써 넣기 |
| `stop_tx` | 버퍼가 비었거나 CTS가 내려갔을 때 | TX 인터럽트 disable |
| `stop_rx` | 포트 닫을 때 | RX 인터럽트 disable |
| `tx_empty` | `tcdrain()`, 닫기 전 대기 | "시프트 레지스터까지 비었나" (FR.BUSY) |
| `set_mctrl` / `get_mctrl` | `TIOCMSET`/`TIOCMGET`, 흐름 제어 | RTS/DTR 출력 핀, CTS/DSR/DCD/RI 입력 핀 |
| `throttle` / `unthrottle` | 위층 버퍼가 거의 찼을 때 | RX 인터럽트 끄기 + RTS 내리기 |
| `break_ctl` | `tcsendbreak()` | TX 선을 일정 시간 0으로 유지 (LIN·DMX 동기용) |

### 3.2 TX 경로와 `kfifo`

`write(fd, buf, n)`의 흐름: VFS → `tty_write` → ldisc의 `n_tty_write`(`OPOST`면 LF→CRLF 변환) → `tty->ops->write` = serial core의 `uart_write`(`serial_core.c:610`) → **포트의 TX 링버퍼에 복사** → `uart_start` → `uart_ops.start_tx`. 드라이버는 TX 인터럽트(또는 DMA 완료)마다 링버퍼에서 꺼내 FIFO를 채운다.

이 TX 링버퍼가 **6.10부터 `kfifo`로 바뀌었다**(그 전에는 `struct circ_buf xmit`이 `uart_state`에 있었다). 6.18.55에서 확인하면:

```sh
limactl shell elinux -- bash -lc 'S=/var/lab/src/linux-6.18.55; grep -n "xmit_fifo" $S/include/linux/tty_port.h; grep -n "kfifo" $S/drivers/tty/serial/serial_core.c | head -4; grep -n "kfifo" $S/drivers/tty/serial/amba-pl011.c | tail -5'
```

```text
74: * @xmit_fifo: optional xmit buffer used by some drivers
118:	DECLARE_KFIFO_PTR(xmit_fifo, u8);
262:		kfifo_init(&state->port.xmit_fifo, state->port.xmit_buf,
293:	INIT_KFIFO(port->xmit_fifo);
600:		ret = kfifo_put(&state->port.xmit_fifo, c);
631:		ret = kfifo_in(&state->port.xmit_fifo, buf, count);
1526:	if (kfifo_is_empty(&tport->xmit_fifo) || uart_tx_stopped(&uap->port)) {
1541:		if (!kfifo_peek(&tport->xmit_fifo, &c))
1547:		kfifo_skip(&tport->xmit_fifo);
1550:	if (kfifo_len(&tport->xmit_fifo) < WAKEUP_CHARS)
1553:	if (kfifo_is_empty(&tport->xmit_fifo)) {
```

TX 링버퍼는 이제 `tty_port.xmit_fifo`이고, serial core의 `uart_write`가 `kfifo_in`으로 넣고(631행), PL011의 TX 루프가 `kfifo_peek` → FIFO에 쓰기 → `kfifo_skip`(1541–1547행)으로 꺼낸다. `kfifo_len < WAKEUP_CHARS`(256)면 `uart_write_wakeup`으로 "더 써도 된다"고 위층을 깨운다. Don이 짜던 TX 링버퍼 + "반 이상 비면 producer 깨우기" 그대로다. 오래된 드라이버 코드나 블로그에서 `xmit->buf[xmit->tail]`, `uart_circ_empty()`가 보이면 6.10 이전 코드다.

### 3.3 baud와 클럭 — 예제 4

PL011의 baud는 `UARTCLK / (16 × baud)`를 16.6 고정소수점(IBRD 정수부 16비트 + FBRD 소수부 6비트)으로 넣는다. 커널은 이렇게 계산한다.

```c
/* amba-pl011.c:2139-2141, 2226-2227 (발췌) */
		quot = DIV_ROUND_CLOSEST(port->uartclk * 8, baud);   /* ST variant, x8 oversampling */
		quot = DIV_ROUND_CLOSEST(port->uartclk * 4, baud);   /* ARM PL011 */
	...
	pl011_write(quot & 0x3f, uap, REG_FBRD);
	pl011_write(quot >> 6, uap, REG_IBRD);
```

무엇을 확인하나: 같은 계산을 Python으로 해서 QEMU `virt`의 24 MHz 클럭(`/sys/class/tty/ttyAMA1/uartclk`, 예제 6)에서 baud 오차를 본다.

```python
# h8_baud.py: PL011 divisor (as amba-pl011.c computes it) and baud error
clk = 24_000_000                       # uartclk from /sys/class/tty/ttyAMA1/uartclk
print(f"uartclk={clk}  max baud (clk/16) = {clk // 16}")
print(f"{'baud':>8} {'IBRD':>5} {'FBRD':>5} {'actual':>10} {'error %':>8}")
for baud in (9600, 115200, 230400, 460800, 921600, 1_000_000, 1_500_000):
    quot = (clk * 4 + baud // 2) // baud      # DIV_ROUND_CLOSEST(uartclk*4, baud)
    ibrd, fbrd = quot >> 6, quot & 0x3f       # 16.6 fixed-point divisor
    actual = clk * 4 / quot
    print(f"{baud:>8} {ibrd:>5} {fbrd:>5} {actual:>10.1f} {100*(actual-baud)/baud:>+8.3f}")
```

```sh
limactl shell elinux -- python3 /Users/donh/.elinux_scratch/H8/h8_baud.py
```

```text
uartclk=24000000  max baud (clk/16) = 1500000
    baud  IBRD  FBRD     actual  error %
    9600   156    16     9600.0   +0.000
  115200    13     1   115246.1   +0.040
  230400     6    33   230215.8   -0.080
  460800     3    16   461538.5   +0.160
  921600     1    40   923076.9   +0.160
 1000000     1    32  1000000.0   +0.000
 1500000     1     0  1500000.0   +0.000
```

출력에서 볼 것: 24 MHz에서 921600은 +0.16% 오차, 1 Mbaud·1.5 Mbaud는 정확하다. UART는 보통 양쪽 합쳐 2~3% 오차까지 견디므로 문제없다. **최대 baud = uartclk/16 = 1.5 Mbaud** — 예제 7에서 `B3000000`을 요청하면 커널이 거부하는 이유다. 실무에서 "3 Mbaud BT 모듈이 안 붙는다"면 DT의 UART 클럭(`assigned-clock-rates`)부터 본다(H3). Don이 RF 칩 bring-up에서 보던 "레퍼런스 클럭이 틀리면 모든 게 조금씩 어긋난다"와 같은 이야기다.

한 가지 함정: `/sys/.../uartclk`는 포트가 한 번 열리기 전엔 0이었다(예제 3). 클럭 값을 확인하려면 포트를 연 뒤에 본다.

---

## 4. ③ TTY core — `tty_struct`, `tty_port`, flip buffer

### 4.1 두 구조체

- **`struct tty_port`** — **물리 포트**의 수명과 같다. 장치가 있는 동안 계속 존재한다. RX flip buffer(`buf`), TX `xmit_fifo`, 열린 횟수, 모뎀 선 대기(carrier) 처리, 그리고 `client_ops`(받은 데이터를 누구에게 줄지 — ldisc냐 serdev냐, §8)를 가진다.
- **`struct tty_struct`** — **열린 터미널 세션**. 누군가 `open()`하면 생기고 마지막 `close()`에 사라진다. termios, 걸린 line discipline(`ldisc`), 세션·프로세스 그룹(Ctrl-C를 누구에게 보낼지), `read_wait`/`write_wait` 대기 큐를 가진다.

펌웨어 비유: `tty_port`는 "UART 채널 제어 블록(정적)", `tty_struct`는 "그 채널을 지금 쓰고 있는 셸 세션 컨텍스트(동적)"다.

### 4.2 flip buffer — ISR과 소비자 사이의 버퍼

드라이버의 RX ISR은 바이트를 `tty_insert_flip_char()`/`tty_insert_flip_string()`으로 `tty_port.buf`에 넣고 마지막에 `tty_flip_buffer_push()`를 부른다. 이 버퍼가 **flip buffer**다. 이름은 옛날 구현(두 버퍼를 번갈아 뒤집던 더블 버퍼)에서 왔고, 지금은 `struct tty_buffer` 조각들의 연결 리스트다. 조각 하나는 최대 `TTY_BUFFER_PAGE`(4 KB 페이지에서 약 2 KB), 포트당 메모리 한도는 `TTYB_DEFAULT_MEM_LIMIT` = 640 KB다(`tty_buffer.c:24-41`). 바이트마다 "플래그"(정상·패리티 에러·프레이밍 에러·break·overrun)도 같이 저장돼서, ldisc가 에러 바이트를 다르게 처리할 수 있다(`PARMRK`, `INPCK`).

`tty_flip_buffer_push()`는 인터럽트 컨텍스트에서 **워크큐에 일을 넘기고 끝난다**(`queue_work(system_unbound_wq, ...)`, E1 §6.4). 실제로 ldisc에 데이터를 넘기는 `flush_to_ldisc()`는 kworker 스레드(프로세스 컨텍스트)에서 돈다. 그래서 ldisc와 serdev의 `receive_buf`는 **잘 수 있다**(mutex를 써도 된다).

### 4.3 예제 5 — kprobe로 RX 경로를 실제로 찍기

E1은 이 경로를 소스로만 따라갔다(랩 기본 커널에 ftrace가 없어서). 지금은 랩에 `Image-dbg`(kprobe·ftrace 포함)가 있으니 실제로 찍어 본다. 무엇을 확인하나: `tty_flip_buffer_push`(ISR 쪽)와 `n_tty_receive_buf2`(ldisc 쪽)에 kprobe를 걸고 각각의 커널 스택을 `stacktrace` 트리거로 남긴다.

```sh
# g_kp.sh (guest, on out/Image-dbg)
T=/sys/kernel/tracing
mount -t tracefs none $T
echo 'p:h8push tty_flip_buffer_push' >> $T/kprobe_events
echo 'p:h8ldisc n_tty_receive_buf2' >> $T/kprobe_events
for e in h8push h8ldisc; do echo 'stacktrace if common_pid >= 0' > $T/events/kprobes/$e/trigger 2>/dev/null || echo stacktrace > $T/events/kprobes/$e/trigger; echo 1 > $T/events/kprobes/$e/enable; done
/h8_nmea /dev/ttyAMA1 canon 1 0 0 2 > /dev/null
echo 0 > $T/events/kprobes/enable
echo "== events: $(grep -c 'h8push:' $T/trace) flip pushes, $(grep -c 'h8ldisc:' $T/trace) ldisc calls"
grep -m1 -A14 'h8push:' $T/trace
grep -m1 -A12 'h8ldisc:' $T/trace
```

부팅은 `h8_run.sh`와 같되 `-kernel /var/lab/out/Image-dbg`로 했다(`h8_nmea`는 §6의 예제). 생성기는 `nmea 9600 2`(문장 4개).

```text
== events: 16 flip pushes, 5 ldisc calls
          <idle>-0       [000] d.h1.     6.141295: h8push: (tty_flip_buffer_push+0x0/0x58)
          <idle>-0       [000] d.h1.     6.141991: <stack trace>
 => kprobe_trace_func
 => kprobe_dispatcher
 => kprobe_brk_handler
 => do_el1_brk64
 => el1_brk64
 => el1h_64_sync_handler
 => el1h_64_sync
 => tty_flip_buffer_push
 => __handle_irq_event_percpu
 => handle_irq_event
 => handle_fasteoi_irq
 => handle_irq_desc
 => generic_handle_domain_irq
    kworker/u4:2-34      [000] d....     6.143858: h8ldisc: (n_tty_receive_buf2+0x0/0x30)
    kworker/u4:2-34      [000] d....     6.143870: <stack trace>
 => kprobe_trace_func
 => kprobe_dispatcher
 => kprobe_brk_handler
 => do_el1_brk64
 => el1_brk64
 => el1h_64_sync_handler
 => el1h_64_sync
 => n_tty_receive_buf2
 => tty_port_default_receive_buf
 => flush_to_ldisc
 => process_one_work
```

출력에서 볼 것:

- 위 7줄(`kprobe_trace_func` … `el1h_64_sync`)은 kprobe 자체의 브레이크포인트 예외 처리라 무시한다.
- **`tty_flip_buffer_push`는 `<idle>-0` 컨텍스트, 플래그 `d.h1.`**(`h` = hardirq)에서 불렸다. CPU가 놀다가 UART IRQ를 받았고, 스택이 `generic_handle_domain_irq`(GIC, H6) → `handle_fasteoi_irq` → `__handle_irq_event_percpu`(등록된 handler 호출)다. 그 사이에 있어야 할 `pl011_int`가 안 보이는 것은 **함수 진입 지점의 kprobe라서 호출자(`pl011_int`)의 복귀 주소가 아직 링크 레지스터에만 있고 스택 프레임에 안 쌓였기 때문**이다(arm64 프레임 포인터 unwinder의 알려진 모양). E1 §6.4 표의 4~7단계가 이 줄들이다.
- **`n_tty_receive_buf2`는 `kworker/u4:2` 스레드, 플래그 `d....`**(인터럽트 아님)에서 `process_one_work` → `flush_to_ldisc` → `tty_port_default_receive_buf` → ldisc로 불렸다. IRQ에서 워크큐로 넘어가는 "top half / bottom half" 분리(F6, F7)가 눈에 보인다.
- **push 16번 vs ldisc 호출 5번**: IRQ마다 push가 일어나지만, 워크가 이미 큐에 있으면 다시 넣지 않으므로 여러 번의 push가 한 번의 `flush_to_ldisc`로 **합쳐진다**. 문장 4개가 ldisc 호출 5번으로 처리됐다 — 대략 문장당 한 번이다(정확히 어느 push들이 어느 호출로 묶였는지는 타임스탬프를 전부 대조하지 않아 확인하지 않았다). 펌웨어에서 "ISR은 버퍼에 넣고 이벤트만 세팅, 태스크가 몰아서 처리"와 같은 구조다.

### 4.4 throttle — 위층이 못 따라올 때

ldisc의 읽기 버퍼(`N_TTY_BUF_SIZE` = 4096바이트, `n_tty.c:59`)가 거의 차면 n_tty가 `tty_throttle_safe()`를 부르고, serial core는 `uart_ops.throttle`(PL011이면 `pl011_throttle_rx`)로 RX 인터럽트를 끄고 하드웨어 흐름 제어가 켜져 있으면 **RTS를 내려 상대방을 멈춘다**. 앱이 다시 읽어서 버퍼가 비면 `unthrottle`. 흐름 제어가 꺼져 있으면? 하드웨어 FIFO가 넘치고 `/proc/tty/driver/*`의 `oe`(overrun) 카운터가 오른다. 흐름 제어 없는 고속 UART에서 데이터가 사라지는 전형적인 경로다.

---

## 5. ④ line discipline — N_TTY와 친구들

### 5.1 무엇을 하나

line discipline(ldisc)은 **TTY core와 앱 사이에 끼우는 교체 가능한 프로토콜 처리기**다. 같은 UART라도 어떤 ldisc를 거느냐에 따라 "사람용 터미널", "PPP 네트워크 링크", "BT HCI 링크", "GSM 다중화 채널"이 된다. 함수 표는 `struct tty_ldisc_ops`(`.open`, `.close`, `.read`, `.write`, `.receive_buf2`, `.write_wakeup`, `.ioctl` …)이고, `ioctl(fd, TIOCSETD, &num)`으로 바꾼다.

| ldisc | 번호 | 하는 일 | 실무 장면 |
|---|---|---|---|
| `N_TTY` | 0 | 줄 편집, echo, 신호, CR/LF 변환, ^S/^Q | 모든 tty의 기본값. 셸·getty |
| `N_SLIP` / `N_PPP` | 1 / 3 | IP 패킷을 시리얼 프레임으로 → 네트워크 인터페이스 | 셀룰러 모뎀(옛 방식) |
| `N_HCI` | 15 | BT HCI 패킷(H4/H5/BCSP)을 프레이밍 → `hci0` | `hciattach`/`btattach`가 붙임 (serdev 이전 방식) |
| `N_GSM0710` | 21 | 3GPP 27.010 다중화: UART 하나를 가상 tty 여러 개로 | 셀룰러 모뎀의 AT·데이터·GNSS 채널 분리 |
| `N_NULL` | 27 | 아무것도 안 함 (오류 시 대체) | 커널 내부 |
| `N_DEVELOPMENT` | 29 | 트리 밖 실험용 번호 | 자기 ldisc 모듈 테스트 |

ldisc는 모듈로 등록된다(`tty_register_ldisc()`). `CONFIG_LDISC_AUTOLOAD=y`면 `TIOCSETD`가 `tty-ldisc-<번호>` 모듈을 자동 로드한다(보안상 끄는 배포판도 있다). 번호는 `include/uapi/linux/tty.h`에 정의돼 있다.

### 5.2 N_TTY가 받은 바이트에 하는 일

`n_tty_receive_buf_common` → 바이트마다 termios 플래그를 보고 처리한다. 사람이 타이핑할 때 편리한 기능들이다.

- `ICRNL`: CR(0x0D)을 LF(0x0A)로 바꾼다. `IGNCR`면 CR을 버린다. `INLCR`은 반대.
- `ICANON`(canonical, 줄 단위): LF·EOF·EOL을 받을 때까지 `read()`를 깨우지 않는다. 그 사이 `VERASE`(0x7F, Backspace)는 앞 글자를 지우고 `VKILL`(^U, 0x15)은 줄 전체를, `VWERASE`(^W)는 단어를 지운다. `VEOF`(^D, 0x04)는 지금까지를 바로 돌려준다(빈 줄이면 0 = EOF). `VLNEXT`(^V)는 다음 글자를 문자 그대로 받는다.
- `ISIG`: `VINTR`(^C 0x03) → SIGINT, `VQUIT`(^\ 0x1C) → SIGQUIT, `VSUSP`(^Z 0x1A) → SIGTSTP를 tty의 foreground 프로세스 그룹에 보내고, **입력 큐를 비운다**(`NOFLSH`가 아니면).
- `IXON`: ^S(0x13, XOFF)를 받으면 **우리 쪽 송신을 멈추고**, ^Q(0x11, XON)에 재개. 두 바이트는 앱에 전달되지 않는다.
- `ECHO`: 받은 바이트를 **송신 쪽으로 다시 내보낸다**. `ECHOCTL`이면 제어 문자를 `^M`처럼 두 글자로.
- `ISTRIP`: 8번째 비트를 지운다(7비트 시절 유물).

GNSS가 보내는 NMEA, u-blox UBX 바이너리, MCU 프로토콜에서는 **이 모든 기능이 데이터 손상 장치**가 된다.

### 5.3 예제 6 — 바이너리 0x00~0xFF가 "기본 tty"를 지나면

무엇을 확인하나: 장치가 0x00~0xFF 256바이트와 `\n`을 보낼 때, 새로 열린 tty의 기본값(ECHO만 끔)과 raw 모드에서 앱이 받는 바이트를 비교한다.

```c
/* h8_bin.c: ask the fake module for bytes 0x00..0xFF + '\n' and check what
 * arrives in "cooked" (default-like) vs raw termios.  usage: h8_bin DEV cooked|raw */
#include <fcntl.h>
#include <poll.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <termios.h>
#include <unistd.h>

int main(int argc, char **argv)
{
    if (argc < 3) { fprintf(stderr, "usage\n"); return 1; }
    int fd = open(argv[1], O_RDWR | O_NOCTTY);
    if (fd < 0) { perror("open"); exit(1); }
    struct termios t;
    if (tcgetattr(fd, &t) < 0) { perror("tcgetattr"); exit(1); }
    if (strcmp(argv[2], "raw") == 0) {
        cfmakeraw(&t);
    } else {                              /* what a fresh tty gives you */
        t.c_iflag = ICRNL | IXON;
        t.c_oflag = OPOST | ONLCR;
        t.c_lflag = ISIG | ICANON | IEXTEN | ECHOE | ECHOK | ECHOCTL | ECHOKE;
    }                                     /* ECHO left off on purpose */
    t.c_cc[VMIN] = 1; t.c_cc[VTIME] = 0;
    if (tcsetattr(fd, TCSANOW, &t) < 0) { perror("tcsetattr"); exit(1); }
    tcflush(fd, TCIOFLUSH);
    if (write(fd, "$H8,bin,0,0\n", 12) != 12) { perror("write"); exit(1); }

    unsigned char got[1024];
    int n = 0, reads = 0;
    struct pollfd p = { .fd = fd, .events = POLLIN };
    while (poll(&p, 1, 1500) > 0 && n < (int)sizeof got) {
        ssize_t r = read(fd, got + n, sizeof got - n);
        if (r < 0) { perror("read"); exit(1); }
        n += r; reads++;
    }
    printf("%s: %d reads, %d bytes (sent 257)\n", argv[2], reads, n);
    int seen[256] = {0};
    for (int i = 0; i < n; i++) seen[got[i]]++;
    printf("  values missing:");
    for (int v = 0; v < 256; v++) if (!seen[v]) printf(" %02x", v);
    printf("\n  values seen more than once:");
    for (int v = 0; v < 256; v++) if (seen[v] > 1) printf(" %02x(x%d)", v, seen[v]);
    printf("\n  first 16 bytes:");
    for (int i = 0; i < n && i < 16; i++) printf(" %02x", got[i]);
    printf("\n");
    close(fd);
    return 0;
}
```

게스트에서 `/h8_bin /dev/ttyAMA1 cooked`, 이어서 `/h8_bin /dev/ttyAMA1 raw`를 실행했다(`h8_run.sh bin g_bin.sh nmea 9600 5`).

```text
== h8_bin cooked
cooked: 1 reads, 226 bytes (sent 257)
  values missing: 00 01 02 03 04 05 06 07 08 09 0b 0c 0d 0e 0f 10 11 12 13 14 15 16 17 18 19 1a 1b 1c 7e 7f
  values seen more than once:
  first 16 bytes: 1d 1e 1f 20 21 22 23 24 25 26 27 28 29 2a 2b 2c
== h8_bin raw
raw: 2 reads, 257 bytes (sent 257)
  values missing:
  values seen more than once: 0a(x2)
  first 16 bytes: 00 01 02 03 04 05 06 07 08 09 0a 0b 0c 0d 0e 0f
```

출력에서 볼 것:

- **raw**: 257바이트가 그대로 왔다(0x0A가 두 번인 것은 데이터 속 0x0A와 끝의 `\n`이다). 이것이 바이너리 프로토콜에 필요한 상태다.
- **cooked**: 31바이트가 사라졌다. 범인을 차례로 짚으면, 0x03(^C)·0x1A(^Z)·0x1C(^\)가 `ISIG` 문자라 **받는 순간 입력 큐 전체를 비웠다**. 마지막 비우기가 0x1C에서 일어났으므로 그 앞의 0x00~0x1C가 통째로 날아갔다(중간의 0x0A 줄바꿈으로 완성됐던 줄까지). 그래서 결과가 0x1D부터 시작한다. 끝 쪽에서는 0x7F(`VERASE`)가 바로 앞의 0x7E를 지우고 자기도 사라졌다. 남은 0x0A 하나는 마지막 `\n`이다. 0x11/0x13(XON/XOFF)은 이미 날아간 구간에 있었지만, 살아남았더라도 `IXON`이 먹었을 것이다.
- 그리고 `read()`가 **1번**에 226바이트를 줬다. canonical 모드는 "줄"을 돌려주기 때문이다.

결론: **바이너리를 다루는 앱은 반드시 raw 모드를 설정한다.** "대부분 잘 되는데 가끔 패킷이 깨진다"는 버그는 데이터에 0x03·0x0D·0x11·0x13·0x7F가 섞였을 때만 터지므로 찾기 어렵다. Don이 SSD에서 봤을 법한 "특정 데이터 패턴에서만 나는 버그"의 리눅스판이다.

---

## 6. termios — 앱이 정하는 회선 설정

### 6.1 구조

`struct termios`는 플래그 묶음 넷과 제어 문자 배열 하나다. `tcgetattr()`로 읽고 고쳐서 `tcsetattr()`로 쓴다. 커널 안에서는 ldisc의 `set_termios`(n_tty가 플래그를 캐시)와 serial core의 `uart_set_termios` → 드라이버 `uart_ops.set_termios`(레지스터)로 나뉘어 들어간다.

| 필드 | 담당 층 | 주요 플래그 | 의미 |
|---|---|---|---|
| `c_cflag` (control) | **하드웨어** (`uart_ops.set_termios`) | `CSIZE`=`CS8`, `PARENB`/`PARODD`, `CSTOPB`, `CRTSCTS`, `CLOCAL`, `CREAD`, baud(`cfsetspeed`) | 8N1인지, 패리티, 정지 비트 2개, RTS/CTS 자동 흐름 제어, 모뎀 선 무시, 수신 enable |
| `c_iflag` (input) | ldisc | `ICRNL`, `IGNCR`, `INLCR`, `IXON`/`IXOFF`, `ISTRIP`, `INPCK`, `PARMRK`, `IGNBRK`/`BRKINT` | 받은 바이트 변환, 소프트웨어 흐름 제어, 에러 바이트 처리 |
| `c_oflag` (output) | ldisc | `OPOST`, `ONLCR` | 보낼 때 LF → CRLF 등 |
| `c_lflag` (local) | ldisc | `ICANON`, `ECHO`, `ECHOE`, `ECHOCTL`, `ISIG`, `IEXTEN` | 줄 단위, echo, 신호 |
| `c_cc[]` | ldisc | `VMIN`, `VTIME`, `VINTR`, `VERASE`, `VEOF` … | 특수 문자, non-canonical 읽기 조건 |

`cfmakeraw()`는 glibc가 제공하는 "raw로 만들기" 함수로, 하는 일은 정확히 이것이다(man 3 termios).

```c
termios_p->c_iflag &= ~(IGNBRK | BRKINT | PARMRK | ISTRIP
                | INLCR | IGNCR | ICRNL | IXON);
termios_p->c_oflag &= ~OPOST;
termios_p->c_lflag &= ~(ECHO | ECHONL | ICANON | ISIG | IEXTEN);
termios_p->c_cflag &= ~(CSIZE | PARENB);
termios_p->c_cflag |= CS8;
```

(glibc 구현은 여기에 `VMIN = 1`, `VTIME = 0`도 넣는다.) baud, `CLOCAL`/`CREAD`, `CRTSCTS`, `CSTOPB`는 건드리지 않으므로 **따로 설정해야 한다**.

### 6.2 read()는 언제 돌아오나 — canonical vs non-canonical

```svg
<svg viewBox="0 0 680 370" xmlns="http://www.w3.org/2000/svg">
  <defs><marker id="ah3" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto-start-reverse"><path d="M0,0 L10,5 L0,10 z" fill="currentColor"/></marker></defs>
  <rect x="240" y="10" width="200" height="36" rx="5" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
  <text x="340" y="33" text-anchor="middle" font-size="13">read(fd, buf, n)</text>
  <rect x="220" y="70" width="240" height="36" rx="5" fill="#888" fill-opacity="0.12" stroke="#888"/>
  <text x="340" y="93" text-anchor="middle" font-size="13">c_lflag에 ICANON이 있나?</text>
  <line x1="340" y1="46" x2="340" y2="69" stroke="currentColor" marker-end="url(#ah3)"/>
  <rect x="20" y="140" width="220" height="130" rx="5" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
  <text x="32" y="162" font-size="13" font-weight="bold">canonical (줄 모드)</text>
  <text x="32" y="184" font-size="12">줄 끝(LF · EOF · EOL)까지 대기</text>
  <text x="32" y="204" font-size="12">ERASE · KILL 편집 적용</text>
  <text x="32" y="224" font-size="12">한 번에 최대 한 줄 반환</text>
  <text x="32" y="244" font-size="12">VMIN · VTIME 무시</text>
  <text x="32" y="262" font-size="12">줄 버퍼 4 KB 한계 (n_tty)</text>
  <line x1="220" y1="98" x2="140" y2="139" stroke="currentColor" marker-end="url(#ah3)"/>
  <text x="140" y="120" font-size="12">있다</text>
  <line x1="460" y1="98" x2="520" y2="139" stroke="currentColor" marker-end="url(#ah3)"/>
  <text x="500" y="120" font-size="12">없다 (raw)</text>
  <rect x="260" y="140" width="200" height="90" rx="5" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
  <text x="272" y="162" font-size="13" font-weight="bold">VMIN=0 · VTIME=0</text>
  <text x="272" y="184" font-size="12">있는 만큼 즉시 반환</text>
  <text x="272" y="204" font-size="12">없으면 0 — 폴링</text>
  <rect x="470" y="140" width="200" height="90" rx="5" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
  <text x="482" y="162" font-size="13" font-weight="bold">VMIN&gt;0 · VTIME=0</text>
  <text x="482" y="184" font-size="12">MIN 바이트 모일 때까지</text>
  <text x="482" y="204" font-size="12">무한 대기 (cfmakeraw 기본)</text>
  <rect x="260" y="245" width="200" height="90" rx="5" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
  <text x="272" y="267" font-size="13" font-weight="bold">VMIN=0 · VTIME&gt;0</text>
  <text x="272" y="289" font-size="12">1바이트라도 오면 반환</text>
  <text x="272" y="309" font-size="12">TIME x 0.1 s 지나면 0</text>
  <rect x="470" y="245" width="200" height="90" rx="5" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
  <text x="482" y="267" font-size="13" font-weight="bold">VMIN&gt;0 · VTIME&gt;0</text>
  <text x="482" y="289" font-size="12">첫 바이트 뒤 간격 타이머</text>
  <text x="482" y="309" font-size="12">MIN개 또는 간격 초과 시</text>
  <text x="20" y="360" font-size="12">모든 경우: 이미 n바이트 이상 쌓여 있으면 n바이트만 반환. O_NONBLOCK이면 데이터 없을 때 EAGAIN.</text>
</svg>
```
그림 3 — `read()`의 반환 조건. canonical이면 "줄"이 단위다. non-canonical(raw)이면 `VMIN`(최소 바이트 수)과 `VTIME`(0.1초 단위 타이머)의 조합 네 가지로 갈린다. 오른쪽 아래 경우의 타이머는 "전체 타임아웃"이 아니라 **바이트 사이 간격** 타이머다.

### 6.3 예제 7 — 같은 NMEA 스트림을 다섯 가지 설정으로 읽기

무엇을 확인하나: 같은 GNSS 스트림(9600 baud 속도, 문장 12개 = 840바이트)을 canonical / raw의 VMIN·VTIME 조합 / ECHO 켬으로 읽으며 `read()`가 매번 무엇을 돌려주는지 시각과 함께 찍는다. 시간 제한은 `alarm()` + `SA_RESTART` 없는 `SIGALRM`으로 블록된 `read()`를 `EINTR`로 깨우는 방식이다.

```c
/* h8_nmea.c: read a serial port in canonical or raw mode and show what
 * each read() returns.  usage: h8_nmea DEV canon|raw VMIN VTIME ECHO SECS */
#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <termios.h>
#include <time.h>
#include <unistd.h>

static void on_alarm(int sig) { (void)sig; }   /* just interrupt read() */
static double now_ms(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec * 1e3 + ts.tv_nsec / 1e6;
}

int main(int argc, char **argv)
{
    if (argc < 7) { fprintf(stderr, "usage\n"); return 1; }
    int fd = open(argv[1], O_RDWR | O_NOCTTY);
    if (fd < 0) { perror("open"); exit(1); }
    struct termios t;
    if (tcgetattr(fd, &t) < 0) { perror("tcgetattr"); exit(1); }
    if (strcmp(argv[2], "raw") == 0)
        cfmakeraw(&t);                     /* ~ICANON ~ECHO ~ISIG ~ICRNL ... */
    else
        t.c_lflag |= ICANON;              /* line mode, ICRNL stays on */
    if (atoi(argv[5])) t.c_lflag |= ECHO; else t.c_lflag &= ~ECHO;
    t.c_cc[VMIN] = atoi(argv[3]);
    t.c_cc[VTIME] = atoi(argv[4]);         /* units of 0.1 s */
    cfsetspeed(&t, B9600);
    t.c_cflag |= CLOCAL | CREAD;
    if (tcsetattr(fd, TCSANOW, &t) < 0) { perror("tcsetattr"); exit(1); }
    tcflush(fd, TCIOFLUSH);
    if (write(fd, "$PMTK220,1000*1F\r\n", 18) != 18) { perror("write"); exit(1); }
    struct sigaction sa = { .sa_handler = on_alarm };   /* no SA_RESTART */
    if (sigaction(SIGALRM, &sa, NULL) < 0) { perror("sigaction"); exit(1); }
    alarm(atoi(argv[6]));
    double t0 = now_ms();
    int reads = 0, bytes = 0;
    for (;;) {
        char buf[512];
        ssize_t n = read(fd, buf, sizeof buf);
        if (n < 0 && errno == EINTR) break;              /* time is up */
        if (n < 0) { perror("read"); exit(1); }
        reads++; bytes += n;
        if (reads <= 8) {
            printf("[%7.1f ms] n=%3zd ", now_ms() - t0, n);
            for (ssize_t i = 0; i < n && i < 40; i++)
                buf[i] == '\r' ? fputs("\\r", stdout) : buf[i] == '\n'
                    ? fputs("\\n", stdout) : putchar(buf[i]);
            printf("%s\n", n > 40 ? "..." : "");
        }
    }
    printf("total: %d reads, %d bytes, avg %.1f bytes/read\n",
           reads, bytes, reads ? (double)bytes / reads : 0.0);
    close(fd);
    return 0;
}
```

```sh
# g_modes.sh (guest) — run with: h8_run.sh modes g_modes.sh nmea 9600 6
D=/dev/ttyAMA1
echo "== A canonical, echo off";          /h8_nmea $D canon 1 0 0 3
echo "== B raw VMIN=1 VTIME=0";           /h8_nmea $D raw 1 0 0 3
echo "== C raw VMIN=0 VTIME=5 (0.5 s)";   /h8_nmea $D raw 0 5 0 3
echo "== D raw VMIN=64 VTIME=2";          /h8_nmea $D raw 64 2 0 3
echo "== E canonical, ECHO ON (default)"; /h8_nmea $D canon 1 0 1 3
grep -E "^1:" /proc/tty/driver/ttyAMA
```

```text
== A canonical, echo off
[  211.0 ms] n= 71 $GPGGA,000000.00,3746.4940,N,12225.1640,...
[  216.2 ms] n=  1 \n
[  286.7 ms] n= 67 $GPRMC,000000.00,A,3746.4940,N,12225.164...
[  287.6 ms] n=  1 \n
[  355.0 ms] n= 71 $GPGGA,000001.00,3746.4941,N,12225.1640,...
[  356.7 ms] n=  1 \n
[  433.3 ms] n= 67 $GPRMC,000001.00,A,3746.4941,N,12225.164...
[  433.7 ms] n=  1 \n
total: 24 reads, 840 bytes, avg 35.0 bytes/read
== B raw VMIN=1 VTIME=0
[  201.0 ms] n= 72 $GPGGA,000000.00,3746.4940,N,12225.1640,...
[  276.3 ms] n= 68 $GPRMC,000000.00,A,3746.4940,N,12225.164...
[  353.9 ms] n= 72 $GPGGA,000001.00,3746.4941,N,12225.1640,...
[  429.3 ms] n= 68 $GPRMC,000001.00,A,3746.4941,N,12225.164...
[  503.1 ms] n= 72 $GPGGA,000002.00,3746.4942,N,12225.1640,...
[  578.6 ms] n= 68 $GPRMC,000002.00,A,3746.4942,N,12225.164...
[  650.0 ms] n= 70 $GPGGA,000003.00,3746.4943,N,12225.1640,...
[  650.5 ms] n=  2 \r\n
total: 13 reads, 840 bytes, avg 64.6 bytes/read
== C raw VMIN=0 VTIME=5 (0.5 s)
[  207.0 ms] n= 72 $GPGGA,000000.00,3746.4940,N,12225.1640,...
[  287.5 ms] n= 68 $GPRMC,000000.00,A,3746.4940,N,12225.164...
[  358.5 ms] n= 72 $GPGGA,000001.00,3746.4941,N,12225.1640,...
[  434.1 ms] n= 68 $GPRMC,000001.00,A,3746.4941,N,12225.164...
[  505.1 ms] n= 72 $GPGGA,000002.00,3746.4942,N,12225.1640,...
[  581.3 ms] n= 68 $GPRMC,000002.00,A,3746.4942,N,12225.164...
[  654.2 ms] n= 72 $GPGGA,000003.00,3746.4943,N,12225.1640,...
[  729.1 ms] n= 68 $GPRMC,000003.00,A,3746.4943,N,12225.164...
total: 15 reads, 840 bytes, avg 56.0 bytes/read
== D raw VMIN=64 VTIME=2
[  203.1 ms] n= 72 $GPGGA,000000.00,3746.4940,N,12225.1640,...
[  279.1 ms] n= 68 $GPRMC,000000.00,A,3746.4940,N,12225.164...
[  350.4 ms] n= 72 $GPGGA,000001.00,3746.4941,N,12225.1640,...
[  425.6 ms] n= 68 $GPRMC,000001.00,A,3746.4941,N,12225.164...
[  497.2 ms] n= 72 $GPGGA,000002.00,3746.4942,N,12225.1640,...
[  572.8 ms] n= 68 $GPRMC,000002.00,A,3746.4942,N,12225.164...
[  644.0 ms] n= 72 $GPGGA,000003.00,3746.4943,N,12225.1640,...
[  720.3 ms] n= 68 $GPRMC,000003.00,A,3746.4943,N,12225.164...
total: 12 reads, 840 bytes, avg 70.0 bytes/read
== E canonical, ECHO ON (default)
[  201.8 ms] n= 72 $GPGGA,000000.00,3746.4940,N,12225.1640,...
[  276.8 ms] n= 68 $GPRMC,000000.00,A,3746.4940,N,12225.164...
[  348.4 ms] n= 72 $GPGGA,000001.00,3746.4941,N,12225.1640,...
[  423.6 ms] n= 68 $GPRMC,000001.00,A,3746.4941,N,12225.164...
[  494.8 ms] n= 72 $GPGGA,000002.00,3746.4942,N,12225.1640,...
[  570.0 ms] n= 68 $GPRMC,000002.00,A,3746.4942,N,12225.164...
[  641.4 ms] n= 72 $GPGGA,000003.00,3746.4943,N,12225.1640,...
[  716.3 ms] n= 68 $GPRMC,000003.00,A,3746.4943,N,12225.164...
total: 12 reads, 840 bytes, avg 70.0 bytes/read
1: uart:PL011 rev1 mmio:0x09040000 irq:14 tx:943 rx:4200
---- generator log (VM host) ----
session 1 cmd=b'$PMTK220,1000*1F' mode=nmea sent=840 bytes in 0.890 s; bytes sent back by guest=0
session 2 cmd=b'$PMTK220,1000*1F' mode=nmea sent=840 bytes in 0.889 s; bytes sent back by guest=0
session 3 cmd=b'$PMTK220,1000*1F' mode=nmea sent=840 bytes in 0.897 s; bytes sent back by guest=0
session 4 cmd=b'$PMTK220,1000*1F' mode=nmea sent=840 bytes in 0.881 s; bytes sent back by guest=0
session 5 cmd=b'$PMTK220,1000*1F' mode=nmea sent=840 bytes in 0.881 s; bytes sent back by guest=852
  first echoed bytes: b'$GPGGA,000000.00,3746.4940,N,12225.1640,W,1,08,0'
```

출력에서 볼 것 — 다섯 가지 교훈:

- **A (canonical, 기본 `ICRNL`)**: 문장 하나가 `read()` 두 번이 됐다. NMEA 끝의 `\r\n`에서 `ICRNL`이 `\r`을 `\n`으로 바꿔 줄 끝이 둘이 됐기 때문이다(71바이트 줄 + 빈 줄 1바이트, 24 reads). 문장 사이 간격 약 75 ms = 71바이트 × 10비트 / 9600 baud(74 ms). 생성기의 페이싱이 보인다.
- **B (raw, VMIN=1)**: 데이터가 있으면 바로 돌아오므로 **`read()` 경계가 문장 경계와 무관하다**. 이번 실행에서는 대부분 문장 단위로 왔지만 마지막 문장이 70 + 2바이트로 잘렸다(같은 프로그램의 이전 실행에서는 `n= 16`, `n= 56`처럼 더 잘게 잘렸다 — 19 reads). 생성기가 문장 단위로 몰아 보내서 그나마 이 정도이고, 실제 9600 baud 선로에서는 바이트가 1 ms마다 하나씩 오므로 훨씬 잘게 쪼개진다. **raw 모드 앱은 반드시 자기 버퍼에 이어 붙여 줄(프레임)을 다시 조립해야 한다**(예제 8).
- **C (VMIN=0, VTIME=5)**: 데이터가 오면 즉시, 안 오면 0.5초 뒤 0을 반환한다. 840바이트를 12번에 받고, 스트림이 끝난 뒤 3초 창의 남은 시간 동안 0을 3번 돌려받았다(15 reads). "타임아웃이 있는 블로킹 read"가 필요할 때 쓴다(GNSS가 죽었는지 감지).
- **D (VMIN=64, VTIME=2)**: 64바이트가 모이거나 바이트 사이가 0.2초 이상 비면 반환. 시스템콜 횟수가 가장 적다(12 reads, 평균 70바이트). 고속 스트림에서 CPU를 아끼는 설정이다.
- **E (ECHO 켬)**: 앱은 똑같이 읽었지만 **생성기 로그에 "guest가 852바이트를 되돌려 보냈다"**가 찍혔다. 받은 NMEA를 그대로 GNSS 모듈 쪽으로 송신한 것이다. 852 = 840 + 12인 것은 `ECHOCTL`이 문장마다 `\r`을 `^M` 두 글자로 echo했기 때문이다. 실제 GNSS 모듈은 이 쓰레기를 명령으로 해석하려다 무시하거나, 최악이면 설정이 바뀐다. 포트 카운터 `tx:943`도 그 증거다(명령 18바이트 × 5회 = 90, echo 852, 합 942. 남는 1바이트의 출처는 확인하지 않았다).

E에는 함정이 하나 더 숨어 있다. E는 A와 같은 "canonical" 설정인데 줄 길이가 71+1이 아니라 72다. **termios는 포트에 남는다.** B~D가 `cfmakeraw`로 `ICRNL`을 꺼 놓았고, E는 `ICANON`만 다시 켰으므로 `\r`이 그대로 통과했다. 앱은 열 때마다 **필요한 플래그를 전부 명시적으로 설정**해야 한다. "이전 프로세스가 무엇을 해 놓았는지"에 기대면 재부팅 직후와 그 후의 동작이 달라진다.

그리고 이 노트를 준비하며 실제로 겪은 일: 생성기를 "명령 없이 바로 송신" 모드로 띄워 놓고 `stty -a < /dev/ttyAMA1`을 실행했더니, **stty가 포트를 여는 순간의 기본 termios(ECHO 켜짐) 때문에** 생성기 로그에 `bytes sent back by guest after cmd=430`이 찍혔다. 읽기 전용으로 상태만 본다고 생각한 명령도 포트를 열면 echo가 동작한다. 실제 장비에서 GNSS·MCU 포트를 열 때는 열자마자 raw로 바꾸거나, 부팅 때 `stty -F /dev/ttyX raw -echo`를 먼저 걸어 둔다.

### 6.4 예제 8 — 실전형 NMEA 리더: raw + 프레임 재조립 + 체크섬

무엇을 확인하나: 실제 제품 코드처럼 raw 모드로 읽고, `$`에서 재동기화하며 줄을 조립하고, 체크섬을 검증한 뒤 `$GPGGA`(위치 고정 정보)의 필드를 뽑는다. VTIME=10으로 "1초 동안 아무것도 안 오면" 감지한다.

```c
/* h8_gga.c: production-style NMEA reader: raw mode, own line assembly,
 * checksum check, parse $GPGGA.  usage: h8_gga DEV SECS */
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <termios.h>
#include <unistd.h>

static void handle(char *s)                  /* s = "$....*HH" without CR/LF */
{
    char *star = strrchr(s, '*');
    if (s[0] != '$' || !star) return;
    unsigned cs = 0, want = strtoul(star + 1, NULL, 16);
    for (char *p = s + 1; p < star; p++) cs ^= (unsigned char)*p;
    if (cs != want) { printf("bad checksum %02X != %02X\n", cs, want); return; }
    if (strncmp(s, "$GPGGA,", 7) != 0) return;
    char *f[16] = {0}; int nf = 0;
    for (char *p = s; p && nf < 16; ) {      /* split on ',' keeping empties */
        f[nf++] = p;
        p = strchr(p, ',');
        if (p) *p++ = '\0';
    }
    if (nf < 10) return;
    printf("GGA utc=%s lat=%s%s lon=%s%s fix=%s sats=%s alt=%sm\n",
           f[1], f[2], f[3], f[4], f[5], f[6], f[7], f[9]);
}

int main(int argc, char **argv)
{
    if (argc < 3) { fprintf(stderr, "usage\n"); return 1; }
    int fd = open(argv[1], O_RDWR | O_NOCTTY);
    if (fd < 0) { perror("open"); exit(1); }
    struct termios t;
    if (tcgetattr(fd, &t) < 0) { perror("tcgetattr"); exit(1); }
    cfmakeraw(&t);                           /* bytes in = bytes out */
    cfsetspeed(&t, B9600);
    t.c_cflag |= CLOCAL | CREAD;
    t.c_cc[VMIN] = 0; t.c_cc[VTIME] = 10;    /* return after 1 s silence */
    if (tcsetattr(fd, TCSANOW, &t) < 0) { perror("tcsetattr"); exit(1); }
    tcflush(fd, TCIFLUSH);
    if (write(fd, "$PMTK220,1000*1F\r\n", 18) != 18) { perror("write"); exit(1); }

    char line[128]; size_t len = 0; int idle = 0;
    while (idle < atoi(argv[2])) {
        char buf[256];
        ssize_t n = read(fd, buf, sizeof buf);
        if (n < 0) { perror("read"); exit(1); }
        if (n == 0) { idle++; continue; }    /* VTIME expired */
        for (ssize_t i = 0; i < n; i++) {
            char c = buf[i];
            if (c == '$') len = 0;           /* resync on start char */
            if (c == '\r') continue;
            if (c == '\n') { line[len] = '\0'; handle(line); len = 0; continue; }
            if (len < sizeof line - 1) line[len++] = c;
        }
    }
    close(fd);
    return 0;
}
```

게스트에서 `/h8_gga /dev/ttyAMA1 2`(1초 무응답 2번이면 종료), 생성기는 `nmea 9600 5`.

```text
== h8_gga
GGA utc=000000.00 lat=3746.4940N lon=12225.1640W fix=1 sats=08 alt=15.0m
GGA utc=000001.00 lat=3746.4941N lon=12225.1640W fix=1 sats=08 alt=16.0m
GGA utc=000002.00 lat=3746.4942N lon=12225.1640W fix=1 sats=08 alt=17.0m
GGA utc=000003.00 lat=3746.4943N lon=12225.1640W fix=1 sats=08 alt=18.0m
GGA utc=000004.00 lat=3746.4944N lon=12225.1640W fix=1 sats=08 alt=19.0m
```

출력에서 볼 것: GGA 다섯 개가 체크섬 에러 없이 파싱됐다(RMC는 체크섬만 보고 버렸다). `read()`가 몇 조각으로 오든 결과가 같다는 것이 핵심이다. 실제 GNSS는 전원 직후 절반짜리 문장이나 노이즈를 보내므로 `$` 재동기화와 체크섬은 필수다. u-blox UBX 같은 바이너리 프로토콜이면 `$` 대신 동기 바이트 `0xB5 0x62` + 길이 필드 + Fletcher 체크섬으로 같은 일을 한다.

### 6.5 흐름 제어 — RTS/CTS와 XON/XOFF

- **하드웨어 흐름 제어(RTS/CTS, `CRTSCTS`)**: 받는 쪽이 RTS 선을 내리면 보내는 쪽이 멈춘다. PL011·i.MX·16550 대부분은 하드웨어가 CTS를 보고 **바이트 단위로 자동** 멈춘다(드라이버가 `set_termios`에서 auto-RTS/CTS 비트를 켠다). BT 모듈(3 Mbaud급)은 거의 필수다. 보드에 RTS/CTS 선이 실제로 연결돼 있어야 하고, DT에 `uart-has-rtscts;`(또는 SoC별 속성)가 있어야 드라이버가 핀을 쓴다.
- **소프트웨어 흐름 제어(XON/XOFF, `IXON`/`IXOFF`)**: 데이터 스트림 안에 0x13(정지)·0x11(재개)을 섞는다. 선이 2가닥뿐일 때 쓰지만 **바이너리 데이터와 공존할 수 없다**(예제 6에서 본 대로 0x11/0x13이 사라진다).
- 둘 다 없으면 받는 쪽의 FIFO + ISR 지연 + flip buffer로 버텨야 하고, 넘치면 `oe` 카운터가 오른다. 9600 baud GNSS는 흐름 제어 없이도 충분하다.

### 6.6 예제 9 — 드라이버가 ioctl로 알려 주는 것들

무엇을 확인하나: 현재 ldisc 번호, 모뎀 선 상태, `serial_struct`(드라이버 정보), 에러 카운터, RS-485 설정, 그리고 지원 안 되는 baud를 요청하면 커널이 어떻게 하는지.

```c
/* h8_ioctl.c: what the serial driver reports through tty ioctls:
 * line discipline, modem lines, serial_struct, counters, RS-485, baud. */
#include <fcntl.h>
#include <linux/serial.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/ioctl.h>
#include <termios.h>
#include <unistd.h>

int main(int argc, char **argv)
{
    const char *dev = argc > 1 ? argv[1] : "/dev/ttyAMA1";
    int fd = open(dev, O_RDWR | O_NOCTTY | O_NONBLOCK);
    if (fd < 0) { perror("open"); exit(1); }
    int ld, m;
    if (ioctl(fd, TIOCGETD, &ld) < 0) { perror("TIOCGETD"); exit(1); }
    printf("ldisc=%d (0 = N_TTY)\n", ld);
    if (ioctl(fd, TIOCMGET, &m) < 0) { perror("TIOCMGET"); exit(1); }
    printf("modem: RTS=%d DTR=%d CTS=%d DSR=%d DCD=%d RI=%d\n",
           !!(m & TIOCM_RTS), !!(m & TIOCM_DTR), !!(m & TIOCM_CTS),
           !!(m & TIOCM_DSR), !!(m & TIOCM_CD), !!(m & TIOCM_RI));
    struct serial_struct ss;
    if (ioctl(fd, TIOCGSERIAL, &ss) < 0) { perror("TIOCGSERIAL"); exit(1); }
    printf("serial: type=%d line=%d irq=%d baud_base=%d xmit_fifo=%d\n",
           ss.type, ss.line, ss.irq, ss.baud_base, ss.xmit_fifo_size);
    struct serial_icounter_struct ic;
    if (ioctl(fd, TIOCGICOUNT, &ic) < 0) { perror("TIOCGICOUNT"); exit(1); }
    printf("icount: rx=%d tx=%d frame=%d overrun=%d parity=%d brk=%d buf_overrun=%d\n",
           ic.rx, ic.tx, ic.frame, ic.overrun, ic.parity, ic.brk, ic.buf_overrun);
    struct serial_rs485 rs;
    if (ioctl(fd, TIOCGRS485, &rs) < 0) perror("TIOCGRS485");
    else printf("rs485: flags=0x%x (enabled=%d)\n", rs.flags,
                !!(rs.flags & SER_RS485_ENABLED));
    rs.flags = SER_RS485_ENABLED | SER_RS485_RTS_ON_SEND;
    rs.delay_rts_before_send = 0; rs.delay_rts_after_send = 0;
    if (ioctl(fd, TIOCSRS485, &rs) < 0) perror("TIOCSRS485");
    else printf("TIOCSRS485 accepted, flags now 0x%x\n", rs.flags);

    struct termios t;
    speed_t sp[] = { B9600, B115200, B921600, B3000000 };
    const char *nm[] = { "9600", "115200", "921600", "3000000" };
    for (int i = 0; i < 4; i++) {
        if (tcgetattr(fd, &t) < 0) { perror("tcgetattr"); exit(1); }
        cfsetspeed(&t, sp[i]);
        if (tcsetattr(fd, TCSANOW, &t) < 0) { perror("tcsetattr"); exit(1); }
        tcgetattr(fd, &t);
        printf("asked %-7s -> kernel kept %s\n", nm[i],
               cfgetospeed(&t) == sp[i] ? "same" : "DIFFERENT");
    }
    close(fd);
    return 0;
}
```

예제 6·8과 같은 부팅에서 이어서 실행했다(그래서 카운터에 앞 예제들의 바이트가 쌓여 있다).

```text
== h8_ioctl
ldisc=0 (0 = N_TTY)
modem: RTS=1 DTR=1 CTS=0 DSR=0 DCD=0 RI=0
serial: type=32 line=1 irq=14 baud_base=1500000 xmit_fifo=16
icount: rx=1214 tx=43 frame=0 overrun=0 parity=0 brk=0 buf_overrun=0
rs485: flags=0x2 (enabled=0)
TIOCSRS485 accepted, flags now 0x3
asked 9600    -> kernel kept same
asked 115200  -> kernel kept same
asked 921600  -> kernel kept same
asked 3000000 -> kernel kept DIFFERENT
uartclk=24000000
```

출력에서 볼 것:

- `rx=1214` = 257 + 257 + 700(GGA·RMC 10개). 앞 예제들이 받은 바이트와 정확히 맞는다. `frame`/`overrun`/`parity`가 0이면 선로와 ISR이 건강하다는 뜻이다. 현장에서 이 숫자를 주기적으로 로그에 남기면 "가끔 깨진다"를 숫자로 잡을 수 있다.
- `baud_base=1500000` = `uartclk / 16`. 마지막 줄의 `uartclk=24000000`은 포트가 열린 뒤라 이제 채워졌다(예제 3에서는 0).
- `B3000000` 요청은 **에러 없이 성공했지만 실제로는 다른 값이 남았다.** `uart_get_baud_rate()`는 범위 밖 baud를 받으면 이전 값이나 9600으로 되돌리고 그 값을 termios에 다시 써 넣는다. **`tcsetattr()`의 반환값 0은 "요청 중 일부라도 적용됐다"는 뜻이지 "전부 적용됐다"가 아니다**(POSIX 규정). 그래서 설정 후 `tcgetattr()`로 다시 읽어 확인하는 것이 정석이다.
- RS-485는 §9에서 본다.

---

## 7. 콘솔 UART — `console=`, `earlycon`, printk

### 7.1 콘솔은 tty와 다른 경로다

커널 로그(`printk`)는 tty 계층을 거치지 않는다. 드라이버가 `struct console`을 등록하면 printk가 그 `write` 함수로 **직접** 레지스터에 쓴다(인터럽트 핸들러 안에서도, 패닉 중에도 찍혀야 하니까). PL011의 콘솔은 `amba_console`(`amba-pl011.c` 2630행 부근)이고, 6.18에서는 새 nbcon(non-blocking console) 인터페이스인 `.write_atomic`/`.write_thread`를 구현한다. 같은 UART가 `/dev/ttyAMA0`(tty 경로)과 콘솔(printk 경로) 두 얼굴을 갖는 셈이다.

- `console=ttyAMA0,115200` — 커널 인자로 어느 tty 드라이버의 콘솔을 쓸지 정한다. 여러 개 줄 수 있고 **마지막 것이 `/dev/console`**이 된다. 없으면 DT `/chosen/stdout-path`를 쓴다.
- `earlycon=pl011,mmio32,0x9000000` — 드라이버 모델이 뜨기 **전**(DT 파싱 직후)부터 쓰는 아주 단순한 콘솔. 레지스터 주소만 알면 된다. 부팅 초반 크래시를 볼 때 필수다. 인자 없이 `earlycon`만 쓰면 `stdout-path`에서 찾는다. 펌웨어로 치면 "C 런타임 초기화 전에 UART 레지스터에 직접 쓰는 `putc`"다.

### 7.2 예제 10 — earlycon에서 진짜 콘솔로 넘어가는 순간

무엇을 확인하나: `earlycon`을 켜고 부팅해서 bootconsole → 진짜 콘솔 전환을 dmesg에서 본다.

```sh
limactl shell elinux -- bash -lc 'cd /var/tmp/H8 && timeout 60 qemu-system-aarch64 -machine virt -cpu cortex-a57 -m 512M -nographic -kernel /var/lab/out/Image -initrd /var/lab/out/initramfs.cpio.gz -append "earlycon=pl011,mmio32,0x9000000 console=ttyAMA0,115200 loglevel=3 LAB_CMD=dmesg~>/tmp/d;~grep~-iE~\"earlycon|console~\\[|bootconsole|printk:\"~/tmp/d;~cat~/proc/consoles;~cat~/proc/cmdline" </dev/null 2>&1 | tr -d "\r" | grep -av "^lab init" | tail -12'
```

```text
[    0.000000] printk: legacy bootconsole [pl11] enabled
[    0.000000] earlycon: pl11 at MMIO32 0x0000000009000000 (options '')
[    0.000000] printk: legacy bootconsole [pl11] enabled
[    0.000000] Kernel command line: earlycon=pl011,mmio32,0x9000000 console=ttyAMA0,115200 loglevel=3 LAB_CMD=dmesg~>/tmp/d;~grep~-iE~"earlycon|console~\[|bootconsole|printk:"~/tmp/d;~cat~/proc/consoles;~cat~/proc/cmdline
[    0.000000] Unknown kernel command line parameters "LAB_CMD=dmesg~>/tmp/d;~grep~-iE~"earlycon|console~\[|bootconsole|printk:"~/tmp/d;~cat~/proc/consoles;~cat~/proc/cmdline", will be passed to user space.
[    0.000000] printk: log buffer data + meta data: 131072 + 458752 = 589824 bytes
[    0.446451] printk: console [ttyAMA0] enabled
[    0.447666] printk: legacy bootconsole [pl11] disabled
...
ttyAMA0              -W- (EC N  a)  204:64
```

(첫 줄은 earlycon이 켜지자마자 콘솔에 직접 찍은 것이고, 둘째 줄부터가 `grep` 결과다. `...`에서 LAB_CMD 에코와 `/proc/cmdline` 줄을 생략했다.)

출력에서 볼 것:

- 시각 0.000000에 `bootconsole [pl11]`이 켜졌다 — 메모리·인터럽트·드라이버 모델이 준비되기 전이다. 0.446초에 PL011 드라이버가 probe되며 진짜 콘솔 `[ttyAMA0]`이 켜지고, 곧바로 bootconsole이 꺼진다. 그 사이 0.44초 동안의 로그는 earlycon이 없으면 **아무 데도 안 나온다**(버퍼에만 쌓였다가 진짜 콘솔이 켜질 때 한꺼번에 나오는데, 그 전에 죽으면 영영 못 본다).
- `/proc/consoles`의 플래그(`fs/proc/consoles.c`): `E` 켜짐, `C` `/dev/console`의 주인, `N` nbcon 콘솔, `a` CPU 핫플러그 중에도 쓸 수 있음. `-W-`는 쓰기 전용.

실무 포인트: 양산 장비에서는 콘솔 UART가 보안 구멍(로그 노출, getty 로그인)이 되므로 `console=`을 빼거나 `loglevel`을 낮추고 getty를 끈다. 반대로 bring-up 보드에서는 `earlycon`을 항상 켠다. 콘솔 UART에 `printk`가 많으면 115200 baud에서 한 줄(약 80바이트)에 약 7 ms가 걸리고, 레거시 콘솔은 그동안 printk 호출자를 붙잡을 수 있다 — nbcon이 생긴 이유 중 하나다.

---

## 8. serdev — UART 위 장치를 커널 드라이버로

### 8.1 직관 — 왜 필요한가

GNSS·BT·보조 MCU는 UART에 붙지만 "UART"가 아니라 **장치**다. 이 장치에는 전원 레귤레이터, 리셋·wake GPIO, 클럭, 그리고 "부팅 직후 baud를 바꾸는 절차" 같은 것이 딸려 있다. 옛날 방식은 이랬다.

- 커널은 `/dev/ttyS2`만 만든다.
- 부팅 스크립트가 GPIO로 BT 칩 전원을 켜고(sysfs GPIO), `hciattach /dev/ttyS2 bcm43xx 3000000`을 실행한다. hciattach가 baud를 바꾸고 펌웨어를 올리고 `TIOCSETD`로 `N_HCI` ldisc를 건다.
- GNSS는 `gpsd`가 `/dev/ttyS2`를 열어 파싱한다.

문제: 하드웨어 기술이 DT가 아니라 **스크립트에 흩어진다**. 부팅 순서 경합, 서스펜드/리줌 때 전원 처리 불가(유저 데몬은 커널 PM을 모른다), 보드마다 다른 스크립트. 그래서 4.11(2017)에 **serdev**(serial device bus)가 들어왔다. UART 노드 아래 DT 자식 노드로 장치를 기술하면, 커널이 그 UART를 `/dev/tty`로 내보내는 대신 **serdev 컨트롤러**로 만들고, 자식 노드와 매칭되는 `serdev_device_driver`를 probe한다. I2C/SPI 장치와 똑같은 모델이 된다(H5, H7).

```svg
<svg viewBox="0 0 680 350" xmlns="http://www.w3.org/2000/svg">
  <defs><marker id="ah5" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto-start-reverse"><path d="M0,0 L10,5 L0,10 z" fill="currentColor"/></marker></defs>
  <text x="170" y="22" text-anchor="middle" font-size="13" font-weight="bold">옛 방식 — 유저 데몬</text>
  <text x="510" y="22" text-anchor="middle" font-size="13" font-weight="bold">serdev — 커널 드라이버</text>
  <rect x="20" y="36" width="300" height="50" rx="5" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
  <text x="32" y="56" font-size="12">gpsd · hciattach (유저 데몬)</text>
  <text x="32" y="74" font-size="12">baud · 전원 GPIO · 펌웨어를 스크립트가 처리</text>
  <rect x="20" y="106" width="300" height="40" rx="5" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
  <text x="32" y="131" font-size="12">/dev/ttyS2 + ldisc (N_TTY 또는 N_HCI)</text>
  <rect x="20" y="166" width="300" height="40" rx="5" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
  <text x="32" y="191" font-size="12">TTY core (flip buffer)</text>
  <rect x="20" y="226" width="300" height="40" rx="5" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
  <text x="32" y="251" font-size="12">UART 드라이버 (uart_port)</text>
  <rect x="20" y="286" width="300" height="50" rx="5" fill="#888" fill-opacity="0.12" stroke="#888"/>
  <text x="32" y="306" font-size="12">DT: UART 노드뿐 — 장치 정보 없음</text>
  <text x="32" y="324" font-size="12">서스펜드 때 전원 처리 불가</text>
  <rect x="360" y="36" width="300" height="50" rx="5" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
  <text x="372" y="56" font-size="12">앱: /dev/gnss0 · hci0 · IIO</text>
  <text x="372" y="74" font-size="12">표준 서브시스템 인터페이스</text>
  <rect x="360" y="106" width="300" height="40" rx="5" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
  <text x="372" y="131" font-size="12">serdev 드라이버 (gnss-mtk · hci_bcm)</text>
  <rect x="360" y="166" width="300" height="40" rx="5" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
  <text x="372" y="191" font-size="12">TTY core — ldisc 없음, /dev/tty 없음</text>
  <rect x="360" y="226" width="300" height="40" rx="5" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
  <text x="372" y="251" font-size="12">UART 드라이버 (그대로 재사용)</text>
  <rect x="360" y="286" width="300" height="50" rx="5" fill="#888" fill-opacity="0.12" stroke="#888"/>
  <text x="372" y="306" font-size="12">DT: UART 노드 + 자식 장치 노드</text>
  <text x="372" y="324" font-size="12">전원 · GPIO · baud를 드라이버가 PM과 함께</text>
  <line x1="170" y1="105" x2="170" y2="87" stroke="currentColor" marker-end="url(#ah5)"/>
  <line x1="170" y1="165" x2="170" y2="147" stroke="currentColor" marker-end="url(#ah5)"/>
  <line x1="170" y1="225" x2="170" y2="207" stroke="currentColor" marker-end="url(#ah5)"/>
  <line x1="510" y1="105" x2="510" y2="87" stroke="currentColor" marker-end="url(#ah5)"/>
  <line x1="510" y1="165" x2="510" y2="147" stroke="currentColor" marker-end="url(#ah5)"/>
  <line x1="510" y1="225" x2="510" y2="207" stroke="currentColor" marker-end="url(#ah5)"/>
</svg>
```
그림 4 — 같은 UART 위 장치를 다루는 두 방식. serdev는 UART 드라이버와 TTY core를 그대로 재사용하면서, line discipline과 `/dev/tty` 노드 자리에 커널 드라이버를 꽂는다.

### 8.2 어떻게 갈라지나 — 소스 두 군데

serial core가 포트를 등록할 때(`uart_add_one_port` → `serial_core.c:3157`) 이 함수를 부른다.

```c
/* drivers/tty/tty_port.c:178-195 (linux-6.18.55, 발췌) */
struct device *tty_port_register_device_attr_serdev(struct tty_port *port,
		struct tty_driver *driver, unsigned index,
		struct device *host, struct device *parent, void *drvdata,
		const struct attribute_group **attr_grp)
{
	struct device *dev;

	tty_port_link_device(port, driver, index);

	dev = serdev_tty_port_register(port, host, parent, driver, index);
	if (PTR_ERR(dev) != -ENODEV) {
		/* Skip creating cdev if we registered a serdev device */
		return dev;
	}

	return tty_register_device_attr(driver, index, parent, drvdata,
			attr_grp);
}
```

UART의 DT 노드에 자식이 있으면 serdev 컨트롤러가 등록되고 **문자 장치(`/dev/ttyAMA1`)는 만들지 않는다**. 자식이 없으면 `-ENODEV`가 돌아와 평소처럼 tty 노드를 만든다. 그리고 serdev 컨트롤러(`serdev-ttyport.c`)는 `tty_port`의 `client_ops`를 자기 것으로 바꿔 끼운다. 그래서 `flush_to_ldisc`가 ldisc 대신 `ttyport_receive_buf` → 드라이버의 `receive_buf`를 부른다.

드라이버가 `serdev_device_open()`을 부르면 `ttyport_open`(95행)이 tty를 커널 안에서 열고 **raw 8비트 + 하드웨어 흐름 제어**로 초기화한다.

```c
/* drivers/tty/serdev/serdev-ttyport.c:118-130 */
	/* Bring the UART into a known 8 bits no parity hw fc state */
	ktermios = tty->termios;
	ktermios.c_iflag &= ~(IGNBRK | BRKINT | PARMRK | ISTRIP |
			      INLCR | IGNCR | ICRNL | IXON);
	ktermios.c_oflag &= ~OPOST;
	ktermios.c_lflag &= ~(ECHO | ECHONL | ICANON | ISIG | IEXTEN);
	ktermios.c_cflag &= ~(CSIZE | PARENB);
	ktermios.c_cflag |= CS8;
	ktermios.c_cflag |= CRTSCTS;
	/* Hangups are not supported so make sure to ignore carrier detect. */
	ktermios.c_cflag |= CLOCAL;
	tty_set_termios(tty, &ktermios);
```

§6.1의 `cfmakeraw`와 똑같다 — 커널도 기계 대 기계 통신에는 raw를 쓴다. 단 **`CRTSCTS`가 기본으로 켜진다**. RTS/CTS 선이 없는 GNSS라면 드라이버가 `serdev_device_set_flow_control(serdev, false)`를 불러야 한다(안 그러면 CTS가 안 올라와 송신이 멈출 수 있다).

serdev 드라이버 API 요약(`include/linux/serdev.h`):

| 함수 / 구조체 | 역할 |
|---|---|
| `struct serdev_device_driver` + `module_serdev_device_driver()` | 드라이버 등록 (`.probe`, `.remove`, `.driver.of_match_table`) |
| `serdev_device_set_client_ops(serdev, &ops)` | `.receive_buf`(받은 데이터, 소비한 바이트 수 반환, 잘 수 있음), `.write_wakeup`(더 써도 됨, 자면 안 됨) |
| `serdev_device_open()` / `devm_serdev_device_open()` | 포트 열기 (tty open + raw 설정) |
| `serdev_device_set_baudrate()` | 실제 적용된 baud 반환 |
| `serdev_device_set_flow_control()` / `serdev_device_set_parity()` | 흐름 제어 / 패리티 |
| `serdev_device_write_buf()` | 비블로킹 쓰기 (TX 링버퍼에 들어간 만큼 반환) |
| `serdev_device_write()` | 블로킹 쓰기 (`.write_wakeup = serdev_device_write_wakeup` 필요) |
| `serdev_device_wait_until_sent()` | TX가 선로로 다 나갈 때까지 대기 |
| `serdev_device_get_tiocm()` / `set_tiocm()` | 모뎀 선 (RTS/DTR/CTS) |

### 8.3 예제 11 — 커널에 이미 있는 GNSS 드라이버를 DT로 붙이기

무엇을 확인하나: 두 번째 PL011 노드 아래에 MediaTek GNSS(GlobalTop PA6H) 자식 노드를 넣으면 `/dev/ttyAMA1`이 사라지고 `/dev/gnss0`이 생기는지, 그리고 거기서 NMEA가 읽히는지. 이 커널에는 `CONFIG_GNSS=m`, `CONFIG_GNSS_SERIAL=m`, `CONFIG_GNSS_MTK_SERIAL=m`이 있다.

DT 수정 — 예제 1에서 덤프한 `h8_two.dts`의 두 번째 PL011에 자식 하나를 넣었다(VM에서 Python으로 문자열 치환 후 `dtc`로 컴파일).

```dts
	pl011@9040000 {
		clock-names = "uartclk", "apb_pclk";
		clocks = <0x8000 0x8000>;
		interrupts = <0x00 0x08 0x04>;
		reg = <0x00 0x9040000 0x00 0x1000>;
		compatible = "arm,pl011", "arm,primecell";
		h8_gnss_mtk {
			compatible = "globaltop,pa6h";
			current-speed = <9600>;
		};
	};
```

```sh
limactl shell elinux -- bash -lc 'cd /var/tmp/H8 && dtc -q -I dts -O dtb -o h8_mtk.dtb h8_mtk.dts && mkdir -p bin/h8ko && cp /var/lab/build/linux/drivers/gnss/{gnss,gnss-serial,gnss-mtk}.ko bin/h8ko/ && H8_TO=120 bash /Users/donh/.elinux_scratch/H8/h8_run.sh mtk /Users/donh/.elinux_scratch/H8/g_mtk.sh nmea 9600 4 -- -dtb /var/tmp/H8/h8_mtk.dtb'
```

```sh
# g_mtk.sh (guest)
echo "== serdev devices before driver"; ls /sys/bus/serial/devices/ | cat
echo "== ls /dev/ttyAMA*"; ls /dev/ttyAMA* | cat
echo "== of_node of serial1 child"; tr "\0" " " < /sys/bus/serial/devices/serial0-0/of_node/compatible; echo
G=/h8ko
insmod $G/gnss.ko && insmod $G/gnss-serial.ko && insmod $G/gnss-mtk.ko
dmesg | grep -iE "gnss|vcc|serial0" | tail -5
echo "== /dev/gnss0"; ls -l /dev/gnss0 | cat; echo "type=$(cat /sys/class/gnss/gnss0/type)"
echo "driver -> $(readlink /sys/bus/serial/devices/serial0-0/driver)"
exec 3<>/dev/gnss0
printf '$PMTK220,1000*1F\r\n' >&3
timeout 2 cat <&3 > /tmp/g.txt
exec 3<&-
echo "== read from /dev/gnss0: $(wc -c < /tmp/g.txt) bytes, first 3 lines"; head -3 /tmp/g.txt | tr -d '\r'
```

```text
== serdev devices before driver
serial0
serial0-0
== ls /dev/ttyAMA*
/dev/ttyAMA0
== of_node of serial1 child
globaltop,pa6h 
[    0.950183] serial serial0: tty port ttyAMA1 registered
[    2.910557] gnss: GNSS driver registered with major 510
[    2.933388] gnss-mtk serial0-0: supply vcc not found, using dummy regulator
== /dev/gnss0
crw-------    1 0        0         510,   0 Oct 11 03:52 /dev/gnss0
type=MTK
driver -> ../../../../../../../bus/serial/drivers/gnss-mtk
Terminated
== read from /dev/gnss0: 560 bytes, first 3 lines
$GPGGA,000000.00,3746.4940,N,12225.1640,W,1,08,0.9,15.0,M,-25.0,M,,*6B
$GPRMC,000000.00,A,3746.4940,N,12225.1640,W,0.02,0.0,101026,,,A*70
$GPGGA,000001.00,3746.4941,N,12225.1640,W,1,08,0.9,16.0,M,-25.0,M,,*68
```

출력에서 볼 것:

- **`/dev/ttyAMA1`이 없다.** 대신 serdev 버스에 컨트롤러 `serial0`과 장치 `serial0-0`이 생겼고, 부팅 로그에 `serial serial0: tty port ttyAMA1 registered`가 찍혔다. 장치 이름 `serial0-0`은 "컨트롤러 0번의 0번 자식"이다(DT alias `serial1`과 번호가 다르니 헷갈리지 말 것).
- 모듈을 넣자 `gnss-mtk`가 DT의 `compatible = "globaltop,pa6h"`로 매칭돼 probe됐다. `vcc-supply`를 DT에 안 적었으므로 regulator 프레임워크가 dummy regulator를 줬다(H3). 실제 보드라면 GNSS 전원 레귤레이터를 `vcc-supply = <&reg_gnss>;`로 연결하고, 드라이버가 `/dev/gnss0`을 열 때 켜고 닫을 때 끈다(`gnss-mtk`의 `mtk_set_active`).
- `/dev/gnss0`은 **NMEA 바이트를 그대로 전달하는 단순 문자 장치**다(termios 없음, baud는 DT의 `current-speed`로 드라이버가 설정). 우리가 쓴 `$PMTK220` 명령도 그대로 모듈에 전달돼 생성기가 스트림을 시작했다. `type=MTK`로 프로토콜 종류를 알려 주므로 gpsd 같은 유저 프로그램이 장치를 자동 인식할 수 있다. 이것이 **GNSS 서브시스템**(`drivers/gnss/`, 4.19부터)의 전부다: 파싱은 유저 공간에 맡기고, 커널은 전원·baud·장치 수명만 책임진다. 지원 칩: `gnss-mtk`(MediaTek), `gnss-ubx`(u-blox neo-6m/neo-8/neo-m8), `gnss-sirf`, USB GNSS.
- `Terminated`는 `timeout 2 cat`이 2초 후 cat을 죽이며 셸이 찍은 메시지다.

### 8.4 예제 12 — 직접 짠 serdev 드라이버 `h8_gnss`

무엇을 확인하나: 최소한의 serdev 드라이버를 직접 써서 DT 자식 노드(`compatible = "h8,gnss"`)에 붙인다. probe에서 포트를 열고 baud를 맞추고 명령을 보낸다. `receive_buf`에서 NMEA를 조립·검증해 GGA를 sysfs 속성 `fix`로 내보낸다. 첫 `receive_buf` 호출 때 `dump_stack()`으로 누가 부르는지도 찍는다.

```c
// SPDX-License-Identifier: GPL-2.0
/* h8_gnss.c (1/2): minimal serdev driver for an NMEA GNSS module on a UART.
 * DT: a child node of the UART with compatible = "h8,gnss". */
#include <linux/module.h>
#include <linux/of.h>
#include <linux/serdev.h>
#include <linux/string.h>

struct h8_gnss {
	struct serdev_device *serdev;
	char line[128];
	size_t len;
	unsigned int ok, bad;
	bool stack_shown;
	char last_gga[128];
};

static bool nmea_ok(const char *s)
{
	const char *star = strrchr(s, '*');
	u8 cs = 0;
	unsigned int want;

	if (s[0] != '$' || !star || kstrtouint(star + 1, 16, &want))
		return false;
	for (s++; s < star; s++)
		cs ^= *s;
	return cs == want;
}

/* called from the tty flip-buffer work (process context, may sleep) */
static size_t h8_receive_buf(struct serdev_device *serdev, const u8 *buf,
			     size_t count)
{
	struct h8_gnss *g = serdev_device_get_drvdata(serdev);
	size_t i;

	if (!g->stack_shown) {		/* who calls us? print once */
		g->stack_shown = true;
		dump_stack();
	}
	for (i = 0; i < count; i++) {
		char c = buf[i];

		if (c == '$')
			g->len = 0;
		if (c == '\r')
			continue;
		if (c != '\n') {
			if (g->len < sizeof(g->line) - 1)
				g->line[g->len++] = c;
			continue;
		}
		g->line[g->len] = '\0';
		g->len = 0;
		if (!nmea_ok(g->line)) {
			g->bad++;
			continue;
		}
		g->ok++;
		if (!strncmp(g->line, "$GPGGA,", 7)) {
			strscpy(g->last_gga, g->line, sizeof(g->last_gga));
			if (g->ok <= 3)
				dev_info(&serdev->dev, "rx %s\n", g->line);
		}
	}
	return count;		/* we consumed everything */
}
```

```c
/* h8_gnss.c (2/2): ops, sysfs attribute, probe, DT match */
static const struct serdev_device_ops h8_ops = {
	.receive_buf  = h8_receive_buf,
	.write_wakeup = serdev_device_write_wakeup,
};

static ssize_t fix_show(struct device *dev, struct device_attribute *a, char *buf)
{
	struct h8_gnss *g = dev_get_drvdata(dev);

	return sysfs_emit(buf, "ok=%u bad=%u last=%s\n", g->ok, g->bad, g->last_gga);
}
static DEVICE_ATTR_RO(fix);
static struct attribute *h8_attrs[] = { &dev_attr_fix.attr, NULL };
ATTRIBUTE_GROUPS(h8);

static int h8_probe(struct serdev_device *serdev)
{
	static const u8 cmd[] = "$PMTK220,1000*1F\r\n";   /* "1 Hz please" */
	struct device *dev = &serdev->dev;
	struct h8_gnss *g;
	u32 speed = 9600;
	int ret;

	g = devm_kzalloc(dev, sizeof(*g), GFP_KERNEL);
	if (!g)
		return -ENOMEM;
	g->serdev = serdev;
	serdev_device_set_drvdata(serdev, g);
	serdev_device_set_client_ops(serdev, &h8_ops);

	ret = devm_serdev_device_open(dev, serdev);   /* = tty open, raw 8N1 */
	if (ret)
		return dev_err_probe(dev, ret, "open failed\n");
	of_property_read_u32(dev->of_node, "current-speed", &speed);
	speed = serdev_device_set_baudrate(serdev, speed);
	serdev_device_set_flow_control(serdev, false);   /* no RTS/CTS wired */

	ret = serdev_device_write_buf(serdev, cmd, sizeof(cmd) - 1);
	dev_info(dev, "probed: baud=%u, wrote %d byte command\n", speed, ret);
	return 0;
}

static const struct of_device_id h8_of_match[] = {
	{ .compatible = "h8,gnss" },
	{ }
};
MODULE_DEVICE_TABLE(of, h8_of_match);

static struct serdev_device_driver h8_driver = {
	.probe = h8_probe,
	.driver = {
		.name = "h8_gnss",
		.of_match_table = h8_of_match,
		.dev_groups = h8_groups,
	},
};
module_serdev_device_driver(h8_driver);
MODULE_DESCRIPTION("H8 study: NMEA GNSS serdev client");
MODULE_LICENSE("GPL");
```

```make
# Kbuild
obj-m += h8_gnss.o
```

DT는 예제 11과 같고 자식 노드만 `h8_gnss { compatible = "h8,gnss"; current-speed = <9600>; };`로 바꿨다(`h8_h8.dtb`). 빌드(경고 0개)와 실행:

```sh
limactl shell elinux -- bash -lc 'rm -rf /var/tmp/H8/kmod && cp -r /Users/donh/.elinux_scratch/H8/h8_gnss /var/tmp/H8/kmod && cd /var/tmp/H8/kmod && make -C /var/lab/out/linux-build M=$PWD -j2 modules && cp h8_gnss.ko /var/tmp/H8/bin/ && H8_TO=120 bash /Users/donh/.elinux_scratch/H8/h8_run.sh h8 /Users/donh/.elinux_scratch/H8/g_h8.sh nmea 9600 4 -- -dtb /var/tmp/H8/h8_h8.dtb'
```

```sh
# g_h8.sh (guest)
echo "== before insmod"; ls /dev/ttyAMA* | cat; ls /sys/bus/serial/devices/ | cat
insmod /h8_gnss.ko
sleep 3
echo "== dmesg"; dmesg | grep -E "h8_gnss|serial0" | sed 's/^\[ *[0-9.]*\] //'
echo "== stack of first receive_buf call"; dmesg | grep -A40 "Call trace" | grep -E "^\[.*\]  " | sed 's/^\[ *[0-9.]*\] //' | head -16
D=/sys/bus/serial/devices/serial0-0
echo "== sysfs"; echo "driver -> $(readlink $D/driver)"; cat $D/fix
echo "== /proc/tty/driver/ttyAMA"; grep "^1:" /proc/tty/driver/ttyAMA
rmmod h8_gnss && echo "rmmod ok"; ls /dev/ttyAMA* | cat
```

```text
== before insmod
/dev/ttyAMA0
serial0
serial0-0
== dmesg
serial serial0: tty port ttyAMA1 registered
h8_gnss: loading out-of-tree module taints kernel.
h8_gnss serial0-0: probed: baud=9600, wrote 18 byte command
 h8_receive_buf+0x1e4/0x1ec [h8_gnss]
h8_gnss serial0-0: rx $GPGGA,000000.00,3746.4940,N,12225.1640,W,1,08,0.9,15.0,M,-25.0,M,,*6B
h8_gnss serial0-0: rx $GPGGA,000001.00,3746.4941,N,12225.1640,W,1,08,0.9,16.0,M,-25.0,M,,*68
== stack of first receive_buf call
 show_stack+0x28/0x7c (C)
 dump_stack_lvl+0x60/0x80
 dump_stack+0x18/0x24
 h8_receive_buf+0x1e4/0x1ec [h8_gnss]
 ttyport_receive_buf+0x4c/0xd8
 flush_to_ldisc+0xe4/0x1b4
 process_one_work+0x150/0x298
 worker_thread+0x180/0x2f4
 kthread+0x12c/0x210
 ret_from_fork+0x10/0x20
== sysfs
driver -> ../../../../../../../bus/serial/drivers/h8_gnss
ok=8 bad=0 last=$GPGGA,000003.00,3746.4943,N,12225.1640,W,1,08,0.9,18.0,M,-25.0,M,,*66
== /proc/tty/driver/ttyAMA
1: uart:PL011 rev1 mmio:0x09040000 irq:14 tx:18 rx:560 RTS|DTR
rmmod ok
/dev/ttyAMA0
```

출력에서 볼 것:

- **probe가 보낸 18바이트 명령**(`tx:18`)을 생성기가 받고 스트림을 시작했다. 앱 없이 커널 드라이버만으로 장치를 깨우고 설정했다. 실제 드라이버라면 여기서 전원 GPIO·리셋·"기본 9600으로 붙어서 115200으로 올리는 명령 → `set_baudrate(115200)`" 같은 절차를 넣는다(BT 드라이버 `hci_bcm`이 그렇게 한다).
- **호출 스택**: `flush_to_ldisc` → `ttyport_receive_buf` → `h8_receive_buf`. 예제 5의 n_tty 스택(`flush_to_ldisc` → `tty_port_default_receive_buf` → `n_tty_receive_buf2`)과 비교하면 **ldisc 자리만 serdev로 바뀌었다**(그림 1의 오른쪽 기둥). kworker(프로세스 컨텍스트)이므로 `receive_buf` 안에서 mutex를 잡거나 `dev_info`를 불러도 된다.
- `ok=8 bad=0`: GGA 4개 + RMC 4개를 체크섬까지 통과시켰다. dmesg의 `rx` 줄이 2개뿐인 것은 `ok <= 3` 조건이 RMC까지 센 숫자라서다(1번째·3번째가 GGA).
- **`rmmod` 뒤에도 `/dev/ttyAMA1`은 돌아오지 않는다.** tty 노드를 만들지 말지는 포트 등록 때(DT 자식 유무로) 정해지기 때문이다. 디버깅 중 "UART를 잠깐 `/dev/tty`로 보고 싶다"면 DT overlay로 자식 노드를 빼고 포트를 다시 등록해야 한다.

실제 BT 모듈의 DT는 이렇게 생겼다(커널 바인딩 문서 `Documentation/devicetree/bindings/net/bluetooth/brcm,bluetooth.yaml`의 예제 그대로).

```dts
    uart {
        uart-has-rtscts;

        bluetooth {
            compatible = "brcm,bcm4330-bt";
            max-speed = <921600>;
            brcm,bt-pcm-int-params = [01 02 00 01 01];
            shutdown-gpios = <&gpio 30 GPIO_ACTIVE_HIGH>;
            device-wakeup-gpios = <&gpio 7 GPIO_ACTIVE_HIGH>;
            reset-gpios = <&gpio 9 GPIO_ACTIVE_LOW>;
            interrupt-parent = <&gpio>;
            interrupts = <8 IRQ_TYPE_EDGE_FALLING>;
        };
    };
```

전원 GPIO, wake GPIO, host-wake IRQ, 최대 baud, 그리고 RTS/CTS 선이 있다는 사실(`uart-has-rtscts`)까지 전부 DT에 있다. hciattach 시절에는 이것들이 부팅 스크립트 여기저기에 흩어져 있었다.

### 8.5 serdev를 못 쓸 때 — 자기 line discipline

serdev는 UART가 **DT(또는 ACPI)로 기술된 경우**에만 쓸 수 있다. USB-UART(`ttyUSB0`)나 PCI 16550처럼 DT 노드가 없는 포트에 붙은 장치를 커널에서 처리하려면 옛 방식인 **ldisc 모듈**을 쓴다: `struct tty_ldisc_ops`에 `.receive_buf2`(받은 바이트 처리), `.open`/`.close`를 채워 `tty_register_ldisc()`로 등록하고, 유저 프로그램이 포트를 열어 `ioctl(fd, TIOCSETD, &num)`으로 건다(`N_HCI`가 이 방식이다. 트리 밖 실험은 번호 `N_DEVELOPMENT` = 29). 이 노트에서는 serdev가 동작했으므로 ldisc 모듈은 만들지 않았다. 단점은 분명하다: 누군가 유저 공간에서 포트를 열고 `TIOCSETD`를 걸어 줘야 하고, 그 프로세스가 죽으면 ldisc도 풀린다.

---

## 9. RS-485 — 산업용 반이중 버스

RS-485는 차동 신호 두 가닥(A/B)에 여러 장치가 매달리는 **반이중** 버스다(모터 드라이버, PLC, Modbus RTU 센서). UART 신호 자체는 같고, 바깥의 트랜시버 칩이 "지금 송신 중"일 때만 드라이버를 켠다(DE 핀). 보통 그 DE 핀을 UART의 **RTS** 선으로 제어한다. 문제는 타이밍이다: 마지막 바이트의 **정지 비트가 선로를 다 빠져나간 직후** DE를 내려야 한다. 너무 빨리 내리면 마지막 바이트가 잘리고, 너무 늦으면 상대의 응답 첫 바이트와 충돌한다. 유저 공간에서 `tcdrain()` 후 GPIO를 내리는 방식은 스케줄링 지연 때문에 수 ms씩 늦는다.

그래서 serial core가 이것을 커널(드라이버의 TX 완료 인터럽트)에서 처리하는 RS-485 모드를 제공한다.

- ioctl: `TIOCSRS485` / `TIOCGRS485`, 구조체 `struct serial_rs485`(`include/uapi/linux/serial.h`). 플래그 `SER_RS485_ENABLED`, `SER_RS485_RTS_ON_SEND`(송신 중 RTS=1), `SER_RS485_RTS_AFTER_SEND`, `SER_RS485_RX_DURING_TX`, 그리고 `delay_rts_before_send`/`delay_rts_after_send`(ms).
- DT: `linux,rs485-enabled-at-boot-time;`, `rs485-rts-delay = <before after>;`, `rs485-rts-active-low;`, `rs485-term-gpios`(종단 저항 스위치) — `Documentation/devicetree/bindings/serial/rs485.yaml`.
- 드라이버 쪽: `uart_port.rs485_config` 콜백과 `rs485_supported`. PL011은 6.x에서 지원이 들어왔고(`pl011_rs485_tx_stop_now` 등, `amba-pl011.c:1272`), 예제 9에서 `TIOCSRS485`가 받아들여졌다(`flags now 0x3` = `ENABLED | RTS_ON_SEND`). 지원하지 않는 드라이버면 `ENOTTY`가 돌아온다.

이 노트에서는 RTS 핀의 실제 파형은 볼 수 없었다(QEMU PL011은 RTS 타이밍을 모델링하지 않는다). 실제 보드에서는 로직 분석기로 TX와 RTS를 같이 찍어 정지 비트 끝과 RTS 하강의 간격을 확인한다 — Don이 늘 하던 방식이다.

---

## 10. 처리량과 지연 — 에뮬레이터에서 재 본 tty 경로

### 10.1 예제 13 — baud를 바꾸면 빨라지나?

무엇을 확인하나: termios baud를 9600 / 115200 / 921600으로 바꿔 가며 (a) 생성기가 쉬지 않고 256 KB를 쏟아낼 때의 수신 처리량, (b) 1바이트를 보내고 생성기가 되돌려 준 1바이트를 받기까지의 왕복 지연(RTT)을 잰다.

```c
/* h8_tput.c: throughput (tput) or 1-byte round-trip latency (rtt) of the
 * tty path at a given termios baud.  usage: h8_tput DEV BAUD tput|rtt N */
#include <fcntl.h>
#include <poll.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <termios.h>
#include <time.h>
#include <unistd.h>
static double now_us(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec * 1e6 + ts.tv_nsec / 1e3;
}
static int cmp(const void *a, const void *b)
{
    double x = *(const double *)a, y = *(const double *)b;
    return (x > y) - (x < y);
}
int main(int argc, char **argv)
{
    if (argc < 5) { fprintf(stderr, "usage\n"); return 1; }
    int fd = open(argv[1], O_RDWR | O_NOCTTY);
    if (fd < 0) { perror("open"); exit(1); }
    struct termios t;
    if (tcgetattr(fd, &t) < 0) { perror("tcgetattr"); exit(1); }
    cfmakeraw(&t);
    speed_t sp = atoi(argv[2]) == 9600 ? B9600 : atoi(argv[2]) == 115200 ? B115200 : B921600;
    cfsetspeed(&t, sp);
    t.c_cc[VMIN] = 1; t.c_cc[VTIME] = 0;
    if (tcsetattr(fd, TCSANOW, &t) < 0) { perror("tcsetattr"); exit(1); }
    tcflush(fd, TCIOFLUSH);
    int n = atoi(argv[4]);
    struct pollfd p = { .fd = fd, .events = POLLIN };
    static char buf[4096];
    if (strcmp(argv[3], "tput") == 0) {
        dprintf(fd, "$H8,flood,0,%d\n", n);
        long got = 0; double first = 0, last = 0;
        while (poll(&p, 1, 2000) > 0) {
            ssize_t r = read(fd, buf, sizeof buf);
            if (r <= 0) break;
            if (!got) first = now_us();
            got += r; last = now_us();
        }
        printf("baud %-6s: %ld bytes in %.3f s = %.0f bytes/s\n", argv[2],
               got, (last - first) / 1e6, got / ((last - first) / 1e6));
    } else {
        static double rtt[1000];
        n = n > 1000 ? 1000 : n;
        for (int i = 0; i < n; i++) {
            double t0 = now_us();
            if (write(fd, "U", 1) != 1) { perror("write"); exit(1); }
            if (read(fd, buf, 1) != 1) { perror("read"); exit(1); }
            rtt[i] = now_us() - t0;
        }
        qsort(rtt, n, sizeof rtt[0], cmp);
        printf("baud %-6s: rtt n=%d min=%.0f median=%.0f p99=%.0f us\n",
               argv[2], n, rtt[0], rtt[n / 2], rtt[n * 99 / 100]);
    }
    close(fd);
    return 0;
}
```

처리량은 baud별로 2회(첫 부팅) + 4회(둘째 부팅) = 6회, RTT는 생성기를 `echo` 모드로 띄운 부팅에서 baud별 500회 쟀다. 게스트 스크립트는 `for b in 9600 115200 921600; do /h8_tput /dev/ttyAMA1 $b tput 262144; sleep 1.5; done`(처리량)와 `for b in 9600 115200 921600; do /h8_tput /dev/ttyAMA1 $b rtt 500; done`(RTT).

```text
== boot 1 (tput x2, then /proc/tty/driver line)
baud 9600  : 262144 bytes in 4.769 s = 54965 bytes/s
baud 115200: 262144 bytes in 6.209 s = 42218 bytes/s
baud 921600: 262144 bytes in 3.212 s = 81625 bytes/s
baud 9600  : 262144 bytes in 3.463 s = 75693 bytes/s
baud 115200: 262144 bytes in 5.856 s = 44765 bytes/s
baud 921600: 262144 bytes in 5.108 s = 51321 bytes/s
1: uart:PL011 rev1 mmio:0x09040000 irq:14 tx:114 rx:1572864
== boot 2 (tput x4)
baud 9600  : 262144 bytes in 5.001 s = 52422 bytes/s
baud 115200: 262144 bytes in 4.846 s = 54094 bytes/s
baud 921600: 262144 bytes in 4.445 s = 58978 bytes/s
baud 9600  : 262144 bytes in 4.388 s = 59737 bytes/s
baud 115200: 262144 bytes in 3.529 s = 74276 bytes/s
baud 921600: 262144 bytes in 2.486 s = 105449 bytes/s
baud 9600  : 262144 bytes in 1.761 s = 148894 bytes/s
baud 115200: 262144 bytes in 4.520 s = 58001 bytes/s
baud 921600: 262144 bytes in 2.721 s = 96324 bytes/s
baud 9600  : 262144 bytes in 4.691 s = 55887 bytes/s
baud 115200: 262144 bytes in 3.552 s = 73801 bytes/s
baud 921600: 262144 bytes in 2.630 s = 99689 bytes/s
== boot 3 (rtt, generator in echo mode)
baud 9600  : rtt n=500 min=143 median=227 p99=3063 us
baud 115200: rtt n=500 min=160 median=236 p99=3072 us
baud 921600: rtt n=500 min=178 median=292 p99=3202 us
```

(`== boot` 머리줄은 세 번의 실행을 구분하려고 붙였다. 나머지는 출력 그대로다.)

```svg
<svg viewBox="0 0 640 310" xmlns="http://www.w3.org/2000/svg">
  <line x1="90" y1="240.0" x2="620" y2="240.0" stroke="currentColor" stroke-opacity="0.15"/>
  <text x="84" y="244.0" text-anchor="end" font-size="12">100</text>
  <line x1="90" y1="190.0" x2="620" y2="190.0" stroke="currentColor" stroke-opacity="0.15"/>
  <text x="84" y="194.0" text-anchor="end" font-size="12">1k</text>
  <line x1="90" y1="140.0" x2="620" y2="140.0" stroke="currentColor" stroke-opacity="0.15"/>
  <text x="84" y="144.0" text-anchor="end" font-size="12">10k</text>
  <line x1="90" y1="90.0" x2="620" y2="90.0" stroke="currentColor" stroke-opacity="0.15"/>
  <text x="84" y="94.0" text-anchor="end" font-size="12">100k</text>
  <line x1="90" y1="40.0" x2="620" y2="40.0" stroke="currentColor" stroke-opacity="0.15"/>
  <text x="84" y="44.0" text-anchor="end" font-size="12">1M</text>
  <line x1="90" y1="40" x2="90" y2="240" stroke="currentColor"/>
  <line x1="90" y1="240" x2="620" y2="240" stroke="currentColor"/>
  <text x="20" y="30" font-size="12">bytes/s (log)</text>
  <rect x="130" y="190.9" width="45" height="49.1" fill="#888" fill-opacity="0.35" stroke="#888"/>
  <text x="152.5" y="184.9" text-anchor="middle" font-size="12">1.0k</text>
  <rect x="185" y="101.9" width="45" height="138.1" fill="#e08a3c" fill-opacity="0.35" stroke="#e08a3c"/>
  <text x="207.5" y="95.9" text-anchor="middle" font-size="12">58k</text>
  <line x1="207.5" y1="104.0" x2="207.5" y2="81.4" stroke="currentColor"/>
  <line x1="201.5" y1="81.4" x2="213.5" y2="81.4" stroke="currentColor"/>
  <line x1="201.5" y1="104.0" x2="213.5" y2="104.0" stroke="currentColor"/>
  <text x="180" y="260" text-anchor="middle" font-size="13">termios 9600</text>
  <rect x="305" y="136.9" width="45" height="103.1" fill="#888" fill-opacity="0.35" stroke="#888"/>
  <text x="327.5" y="130.9" text-anchor="middle" font-size="12">12k</text>
  <rect x="360" y="102.6" width="45" height="137.4" fill="#e08a3c" fill-opacity="0.35" stroke="#e08a3c"/>
  <text x="382.5" y="96.6" text-anchor="middle" font-size="12">56k</text>
  <line x1="382.5" y1="108.7" x2="382.5" y2="96.5" stroke="currentColor"/>
  <line x1="376.5" y1="96.5" x2="388.5" y2="96.5" stroke="currentColor"/>
  <line x1="376.5" y1="108.7" x2="388.5" y2="108.7" stroke="currentColor"/>
  <text x="355" y="260" text-anchor="middle" font-size="13">termios 115200</text>
  <rect x="480" y="91.8" width="45" height="148.2" fill="#888" fill-opacity="0.35" stroke="#888"/>
  <text x="502.5" y="85.8" text-anchor="middle" font-size="12">92k</text>
  <rect x="535" y="92.5" width="45" height="147.5" fill="#e08a3c" fill-opacity="0.35" stroke="#e08a3c"/>
  <text x="557.5" y="86.5" text-anchor="middle" font-size="12">89k</text>
  <line x1="557.5" y1="104.5" x2="557.5" y2="88.8" stroke="currentColor"/>
  <line x1="551.5" y1="88.8" x2="563.5" y2="88.8" stroke="currentColor"/>
  <line x1="551.5" y1="104.5" x2="563.5" y2="104.5" stroke="currentColor"/>
  <text x="530" y="260" text-anchor="middle" font-size="13">termios 921600</text>
  <rect x="140" y="280" width="14" height="12" fill="#888" fill-opacity="0.35" stroke="#888"/>
  <text x="160" y="291" font-size="12">실제 UART 선로 한계 (baud/10, 8N1)</text>
  <rect x="400" y="280" width="14" height="12" fill="#e08a3c" fill-opacity="0.35" stroke="#e08a3c"/>
  <text x="420" y="291" font-size="12">QEMU 측정 중앙값 (범위)</text>
</svg>
```
그림 5 — 수신 처리량(로그 축). 회색은 실제 UART 선로가 낼 수 있는 상한(baud / 10비트), 주황은 QEMU에서 잰 6회의 중앙값(세로선 = 최소~최대). 9600 baud 설정에서도 선로 한계의 약 60배가 나왔다 — QEMU PL011이 baud를 흉내 내지 않기 때문이다.

```svg
<svg viewBox="0 0 640 300" xmlns="http://www.w3.org/2000/svg">
  <line x1="90" y1="230.0" x2="620" y2="230.0" stroke="currentColor" stroke-opacity="0.15"/>
  <text x="84" y="234.0" text-anchor="end" font-size="12">0</text>
  <line x1="90" y1="182.5" x2="620" y2="182.5" stroke="currentColor" stroke-opacity="0.15"/>
  <text x="84" y="186.5" text-anchor="end" font-size="12">600</text>
  <line x1="90" y1="135.0" x2="620" y2="135.0" stroke="currentColor" stroke-opacity="0.15"/>
  <text x="84" y="139.0" text-anchor="end" font-size="12">1200</text>
  <line x1="90" y1="87.5" x2="620" y2="87.5" stroke="currentColor" stroke-opacity="0.15"/>
  <text x="84" y="91.5" text-anchor="end" font-size="12">1800</text>
  <line x1="90" y1="40.0" x2="620" y2="40.0" stroke="currentColor" stroke-opacity="0.15"/>
  <text x="84" y="44.0" text-anchor="end" font-size="12">2400</text>
  <line x1="90" y1="40" x2="90" y2="230" stroke="currentColor"/>
  <line x1="90" y1="230" x2="620" y2="230" stroke="currentColor"/>
  <text x="20" y="28" font-size="12">마이크로초 (us)</text>
  <rect x="130" y="65.1" width="45" height="164.9" fill="#888" fill-opacity="0.35" stroke="#888"/>
  <text x="152.5" y="59.1" text-anchor="middle" font-size="12">2083</text>
  <rect x="185" y="212.0" width="45" height="18.0" fill="#4a7bd0" fill-opacity="0.35" stroke="#4a7bd0"/>
  <text x="207.5" y="206.0" text-anchor="middle" font-size="12">227</text>
  <text x="180" y="250" text-anchor="middle" font-size="13">termios 9600</text>
  <rect x="305" y="216.2" width="45" height="13.8" fill="#888" fill-opacity="0.35" stroke="#888"/>
  <text x="327.5" y="210.2" text-anchor="middle" font-size="12">174</text>
  <rect x="360" y="211.3" width="45" height="18.7" fill="#4a7bd0" fill-opacity="0.35" stroke="#4a7bd0"/>
  <text x="382.5" y="205.3" text-anchor="middle" font-size="12">236</text>
  <text x="355" y="250" text-anchor="middle" font-size="13">termios 115200</text>
  <rect x="480" y="228.3" width="45" height="1.7" fill="#888" fill-opacity="0.35" stroke="#888"/>
  <text x="502.5" y="222.3" text-anchor="middle" font-size="12">21.7</text>
  <rect x="535" y="206.9" width="45" height="23.1" fill="#4a7bd0" fill-opacity="0.35" stroke="#4a7bd0"/>
  <text x="557.5" y="200.9" text-anchor="middle" font-size="12">292</text>
  <text x="530" y="250" text-anchor="middle" font-size="13">termios 921600</text>
  <rect x="120" y="270" width="14" height="12" fill="#888" fill-opacity="0.35" stroke="#888"/>
  <text x="140" y="281" font-size="12">선로 왕복 2바이트 시간 (20 bit / baud)</text>
  <rect x="390" y="270" width="14" height="12" fill="#4a7bd0" fill-opacity="0.35" stroke="#4a7bd0"/>
  <text x="410" y="281" font-size="12">QEMU 측정 RTT 중앙값 (500회)</text>
</svg>
```
그림 6 — 1바이트 왕복 지연. 회색은 실제 선로에서 2바이트(가고 오고)가 걸리는 시간, 파랑은 QEMU에서 잰 중앙값. 실제 9600 baud에서는 선로 시간(2.1 ms)이 지배하지만, 115200 이상에서는 **소프트웨어 경로**(IRQ → 워크큐 → ldisc → 깨우기 → 유저 복귀)가 지배하는 영역으로 넘어간다.

출력에서 볼 것:

- **baud 설정과 처리량 사이에 관계가 없다.** 6회 중앙값이 9600에서 약 58 KB/s, 115200에서 약 56 KB/s, 921600에서 약 89 KB/s이고 회차마다 2배 넘게 흔들린다(9600 한 번은 149 KB/s). 이 VM에서 다른 작업 11개가 동시에 돌고 있었으므로 이 흔들림은 호스트 부하다. 결론은 하나: **QEMU의 PL011은 IBRD/FBRD를 무시하고 소켓에 들어온 속도대로 받는다.** 그래서 에뮬레이터에서 "9600인데 잘 된다"는 실제 보드에서 아무것도 보장하지 않는다.
- 그래도 이 숫자는 쓸모가 있다. 약 50~100 KB/s는 **tty 경로 자체(TCG 에뮬레이션 위의 IRQ·워크큐·ldisc 복사)의 상한**에 가깝다. 실제 SoC는 TCG보다 수십 배 빠르므로 tty 경로는 수 Mbaud UART도 따라간다. 실제 병목은 대개 FIFO 깊이 대비 IRQ 지연(→ overrun)이고, 그래서 고속 UART는 DMA(`pl011_dma_rx_irq`, H9)를 쓴다.
- **RTT 중앙값 약 230 µs, p99 약 3 ms.** 실제 921600 baud 선로라면 2바이트 왕복이 22 µs인데, 소프트웨어 경로가 그 10배 이상이다. 실제 보드에서도 tty 경로의 지연은 수십~수백 µs이고, 워크큐 스레드가 늦게 스케줄되면 ms 단위가 된다(p99). **UART로 1 ms 이하의 응답이 필요한 제어 루프**(예: MCU와 명령·응답)라면 이 꼬리 지연을 측정해 봐야 하고, 필요하면 PREEMPT_RT, 워크큐 우선순위, 또는 프로토콜 쪽에서 응답을 기다리지 않는 설계(파이프라이닝)로 간다. 이 측정은 TCG 위라 절대값은 실제 하드웨어와 다르다.
- 첫 부팅의 `rx:1572864` = 262144 × 6. 바이트 손실 없이 전부 받았다(에뮬레이터는 소켓 역압으로 흐름이 자동 제어되어 overrun이 생기지 않는다 — 실제 하드웨어와 다른 점).

---

## 11. 커널 안에서는 무슨 일이 일어나나

유저가 부르는 API 하나하나가 어느 층의 어떤 함수로 가는지를 한 장으로 정리한다. 층과 층은 함수 포인터 표(ops)로 연결된다(E1 §7).

```svg
<svg viewBox="0 0 680 400" xmlns="http://www.w3.org/2000/svg">
  <defs><marker id="ah8" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto-start-reverse"><path d="M0,0 L10,5 L0,10 z" fill="currentColor"/></marker></defs>
  <rect x="20" y="10" width="640" height="40" rx="5" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
  <text x="32" y="35" font-size="12">유저: open · read · write · poll · ioctl(TCSETS · TIOCSETD · TIOCSRS485)</text>
  <rect x="20" y="70" width="640" height="52" rx="5" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
  <text x="32" y="90" font-size="13" font-weight="bold">file_operations tty_fops (tty_io.c:459)</text>
  <text x="32" y="110" font-size="12">.open = tty_open · .read_iter = tty_read · .write_iter = tty_write · .poll</text>
  <rect x="20" y="142" width="640" height="52" rx="5" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
  <text x="32" y="162" font-size="13" font-weight="bold">tty_ldisc_ops n_tty_ops (n_tty.c:2512)</text>
  <text x="32" y="182" font-size="12">.read = n_tty_read · .write = n_tty_write · .set_termios · .receive_buf2 (RX에서 위로)</text>
  <rect x="20" y="214" width="640" height="52" rx="5" fill="#888" fill-opacity="0.12" stroke="#888"/>
  <text x="32" y="234" font-size="13" font-weight="bold">tty_operations uart_ops (serial_core.c:2700)</text>
  <text x="32" y="254" font-size="12">.open = uart_open · .write = uart_write (kfifo) · .set_termios · .ioctl = uart_ioctl</text>
  <rect x="20" y="286" width="640" height="52" rx="5" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
  <text x="32" y="306" font-size="13" font-weight="bold">uart_ops amba_pl011_pops (amba-pl011.c:2324)</text>
  <text x="32" y="326" font-size="12">.startup · .start_tx · .set_termios (IBRD/FBRD) · .set_mctrl · rs485_config</text>
  <rect x="20" y="358" width="640" height="34" rx="5" fill="#d0564a" fill-opacity="0.12" stroke="#d0564a"/>
  <text x="32" y="380" font-size="12">레지스터 (readl · writel, H1) · IRQ → pl011_int → tty_flip_buffer_push → 워크큐</text>
  <line x1="340" y1="51" x2="340" y2="69" stroke="currentColor" marker-end="url(#ah8)"/>
  <line x1="340" y1="123" x2="340" y2="141" stroke="currentColor" marker-end="url(#ah8)"/>
  <line x1="340" y1="195" x2="340" y2="213" stroke="currentColor" marker-end="url(#ah8)"/>
  <line x1="340" y1="267" x2="340" y2="285" stroke="currentColor" marker-end="url(#ah8)"/>
  <line x1="340" y1="339" x2="340" y2="357" stroke="currentColor" marker-end="url(#ah8)"/>
</svg>
```
그림 7 — ops 표 사슬. 아래로 내려가는 호출(쓰기·설정)은 위 표에서 아래 표로, RX 데이터는 IRQ에서 flip buffer와 워크큐를 거쳐 `.receive_buf2`로 다시 올라온다(예제 5). serdev 포트에서는 둘째 칸(ldisc)이 `ttyport_receive_buf` → serdev 드라이버로 바뀐다(예제 12).

| 유저 동작 | 거치는 함수 (6.18.55) | 드라이버 작성자가 구현하는 곳 |
|---|---|---|
| `open("/dev/ttyAMA1")` | `chrdev_open` → `tty_open` → `tty_init_dev`(tty_struct 생성, N_TTY 붙임) → `uart_open` → `tty_port_open` → `uart_port_startup` | `uart_ops.startup` (클럭·IRQ·FIFO) |
| `read()` | `tty_read` → `n_tty_read` → 데이터 없으면 `wait_woken` (E1 §6) | 없음 (RX는 ISR이 밀어 올림) |
| RX 바이트 도착 | IRQ → `pl011_int` → `pl011_fifo_to_tty` → `tty_insert_flip_*` → `tty_flip_buffer_push` → kworker `flush_to_ldisc` → `n_tty_receive_buf2` → `wake_up` | ISR에서 `uart_insert_char`/`tty_insert_flip_string` + `tty_flip_buffer_push` |
| `write()` | `tty_write` → `n_tty_write`(OPOST 변환) → `uart_write` → `kfifo_in` → `uart_ops.start_tx` | `start_tx`, TX ISR에서 `kfifo` 소비 + `uart_write_wakeup` |
| `tcsetattr()` | `tty_ioctl(TCSETS)` → `tty_set_termios` → `n_tty_set_termios` + `uart_set_termios` → `uart_ops.set_termios` | `set_termios` (baud 분주, LCR, auto-CTS) |
| `ioctl(TIOCSETD)` | `tty_set_ldisc` → 이전 ldisc `.close`, 새 ldisc `.open` | `tty_ldisc_ops` (ldisc 모듈) |
| `ioctl(TIOCSRS485)` | `uart_ioctl` → `uart_set_rs485_config` → `port->rs485_config` | `rs485_config` 콜백 |
| 마지막 `close()` | `tty_release` → `uart_close` → `uart_port_shutdown` | `uart_ops.shutdown` |

드라이버 작성 관점의 요약: **새 SoC UART 드라이버**를 쓴다면 `uart_driver` 하나, 포트마다 `uart_port`, 그리고 `uart_ops`(최소 `tx_empty`, `set_mctrl`, `get_mctrl`, `stop_tx`, `start_tx`, `stop_rx`, `startup`, `shutdown`, `set_termios`, `type`, `config_port`)를 채우고 platform/amba 드라이버의 probe에서 `uart_add_one_port()`를 부른다(G3·G5 패턴). ISR은 FIFO → `uart_insert_char()` → `tty_flip_buffer_push()`, TX는 `kfifo` → FIFO. 이 이상은 serial core가 다 한다. **UART에 붙은 칩의 드라이버**를 쓴다면 serdev(§8)다 — 실무에서는 이쪽이 훨씬 자주 나온다.

---

## 12. Embedded 실무에서 다시 보기

### 12.1 센서 키트의 GNSS — i.MX8 + u-blox

예를 들어 센서 키트가 i.MX8M Plus에 u-blox NEO-M8을 UART2(`ttymxc1`)로 붙였다고 하자. 선택지는 둘이다.

- **유저 공간(gpsd 또는 자체 데몬)**: DT에는 UART만 켜 두고(`status = "okay"`), 앱이 `/dev/ttymxc1`을 열어 **raw + 9600(또는 설정 후 115200) + `CLOCAL`**으로 설정한다. NMEA만 쓰면 예제 8처럼, UBX 바이너리를 쓰면 반드시 raw. 앱 시작 시 `tcflush(fd, TCIFLUSH)`로 부팅 중 쌓인 반쪽 문장을 버린다.
- **커널 GNSS 서브시스템**: UART 노드에 `gnss { compatible = "u-blox,neo-m8"; vcc-supply = <&reg_gnss>; current-speed = <9600>; };`를 넣으면 `/dev/gnss0`이 생기고, 전원은 드라이버가 open/close에 맞춰 켜고 끈다. gpsd는 `/dev/gnss0`을 연다. 저전력 장비에서 유리하다.

어느 쪽이든 PPS(1초 펄스) 핀은 UART가 아니라 GPIO → `pps-gpio`(`/dev/pps0`)로 따로 받는다. 시간 동기(chrony)에 GNSS를 쓸 때 NMEA 문장의 도착 시각은 tty 지연(예제 13의 꼬리 지연) 때문에 ms 단위로 흔들리므로 정밀 시각은 PPS가 준다.

### 12.2 BT/Wi-Fi 콤보 칩 — serdev + `hci_uart`

Jetson이나 i.MX 보드의 BT는 거의 다 UART(H4, 3~4 Mbaud, RTS/CTS 필수)다. DT에 §8.4의 `bluetooth { compatible = "brcm,..."; }` 노드를 넣으면 `hci_uart`의 serdev 드라이버(`hci_bcm`, `hci_qca` 등. 이 랩 커널은 `CONFIG_BT_HCIUART_SERDEV=y`, `BCM`·`QCA`·`LL`·`MRVL`가 켜져 있다)가 칩 전원을 켜고, 기본 baud로 펌웨어를 올리고, 고속으로 바꾼 뒤 `hci0`을 등록한다. `hciattach`나 `btattach`를 부팅 스크립트에서 부르고 있다면 레거시다. "BT가 가끔 안 올라온다"의 단골 원인: RTS/CTS 미배선인데 `uart-has-rtscts`를 넣었거나 그 반대, UART 클럭이 3 Mbaud를 못 만듦(§3.3), 전원 시퀀스 타이밍.

### 12.3 보조 MCU (모터·전원 관리)와의 프로토콜

로봇에서는 Cortex-M MCU가 모터·배터리를 맡고 Linux SoC와 UART로 통신하는 구조가 흔하다. 체크리스트:

- raw 모드, 그리고 **프레이밍**: 길이 필드 + CRC, 또는 COBS/SLIP처럼 구분 바이트를 쓰는 인코딩. "줄바꿈으로 구분"은 바이너리에서 깨진다.
- 수신은 `poll()`/`epoll`(A5)로 기다리고 `VMIN=0`·`VTIME=0` + 논블로킹 read로 있는 만큼 가져와 자기 버퍼에서 프레임을 찾는다.
- 응답 시간 요구가 있으면 꼬리 지연을 잰다(예제 13 방식). 필요하면 커널 serdev 드라이버로 내려서 응답 처리를 워크큐 안에서 끝낸다.
- MCU 펌웨어 업데이트 경로(부트로더가 다른 baud·패리티를 쓰는 경우, 예: STM32 시스템 부트로더는 8E1)를 termios로 바꿀 수 있게 설계한다.

### 12.4 RS-485 모터 드라이버 / Modbus RTU

산업용 액추에이터는 RS-485 Modbus RTU를 쓴다. DT에 `linux,rs485-enabled-at-boot-time;`과 `rs485-rts-delay`를 넣거나 앱에서 `TIOCSRS485`를 설정한다(§9). Modbus RTU는 프레임 사이 3.5문자 무음 구간으로 프레임을 구분하므로 `VTIME`은 너무 거칠다(0.1초 단위). 앱은 `poll()` 타임아웃이나 타이머로 직접 구간을 잰다. libmodbus가 이 처리를 해 준다.

---

## 13. 흔한 실수와 증상

| 실수 | 증상 | 원인 | 고치는 법 |
|---|---|---|---|
| 바이너리 프로토콜인데 raw 모드를 안 씀 | 가끔 패킷 깨짐, 특정 값에서만 | `ICRNL`, `ISIG`(입력 큐 비움), `IXON`, `VERASE`가 바이트를 바꾸거나 먹음 (예제 6) | `cfmakeraw()` + `CLOCAL`/`CREAD` 명시 |
| ECHO를 안 끔 | 장치가 이상한 설정으로 바뀜, 버스 충돌 | 받은 데이터가 그대로 송신됨 (예제 7 E: 852바이트 반송) | raw 모드, 열자마자 설정 |
| termios를 일부만 바꿈 | 재부팅 직후와 그 후 동작이 다름 | termios가 포트에 남음 (예제 7 E의 72바이트 줄) | 필요한 플래그를 전부 명시 |
| raw `read()` 한 번 = 메시지 하나라고 가정 | 가끔 파싱 실패 | read 경계는 타이밍에 따라 임의 (예제 7 B) | 자기 버퍼에 이어 붙이고 프레임 재조립 (예제 8) |
| `tcsetattr()` 반환값만 확인 | baud가 실제로 안 바뀜 | 일부만 적용돼도 0 반환 (예제 9 `B3000000`) | `tcgetattr()`로 다시 읽어 비교 |
| RTS/CTS 미배선인데 `CRTSCTS` 켬 (또는 serdev 기본값 그대로) | 송신이 멈춤, `write()`가 블록 | CTS가 안 올라와 TX 정지 | `CRTSCTS` 끄기 / `serdev_device_set_flow_control(false)` |
| 고속 UART에 흐름 제어·DMA 없음 | `/proc/tty/driver/*`의 `oe` 증가, 데이터 유실 | FIFO가 IRQ 지연보다 먼저 참 | RTS/CTS, DMA, IRQ 스레드 우선순위 |
| UART 클럭이 baud를 못 만듦 | 특정 baud에서 `fe`(framing) 증가, 쓰레기 문자 | 분주 오차 또는 최대 baud 초과 (예제 4) | DT `assigned-clock-rates`, 오차 계산 |
| DT 자식 노드를 넣고 `/dev/ttyX`를 찾음 | "포트가 사라졌다" | serdev 컨트롤러가 되면 tty 노드를 안 만듦 (예제 11, 12) | 의도대로면 정상. tty로 쓰려면 자식 노드 제거 |
| 양산 장비에 콘솔·getty를 그대로 둠 | 보안 감사 지적, 부팅 지연 | `console=`과 getty가 UART를 점유 | `console=` 제거·`loglevel` 낮춤, getty 비활성 |

---

## 14. 면접에서 이렇게 말한다

**Q.** Walk me through the Linux serial/TTY stack. What happens between the UART hardware and an application's `read()`?

**A.** 다섯 층이다. 맨 아래 UART 드라이버가 `uart_port`와 `uart_ops`로 하드웨어를 다루고, serial core가 모든 UART에 공통인 TX `kfifo`·termios·모뎀 선을 처리한다. 그 위 TTY core가 `tty_port`의 flip buffer로 RX를 받고, line discipline(기본 N_TTY)이 줄 편집·echo·신호를 처리한 뒤 앱의 `read()`에 넘긴다. RX는 IRQ에서 flip buffer에 넣고 워크큐에서 ldisc로 올리는 top/bottom half 구조다.

> There are roughly five layers. At the bottom, the UART driver fills in a `uart_port` and `uart_ops` — startup, set_termios, start_tx and so on. The serial core sits on top of every UART driver and handles the TX ring buffer, which is a kfifo since 6.10, termios translation, modem lines and the console. Above that is the TTY core: `tty_port` owns the RX flip buffer, `tty_struct` represents the open session. Then the line discipline, normally N_TTY, does canonical line editing, echo and signal characters before data reaches `read()`. On receive, the ISR pushes bytes into the flip buffer and calls `tty_flip_buffer_push`, and a kworker runs `flush_to_ldisc` to hand the data to the line discipline in process context. I've actually traced that with kprobes: the push runs in hardirq context, the ldisc call runs in a kworker.

**꼬리 질문**: "Why is the receive path split into IRQ and workqueue?" → ISR을 짧게 유지하고, ldisc 처리(echo 송신, 신호, 잠든 reader 깨우기, mutex)는 잘 수 있는 컨텍스트에서 하기 위해서다.

**Q.** Why do embedded applications need to put a serial port into raw mode?

**A.** 기본 termios는 사람 터미널용이라 CR을 LF로 바꾸고(`ICRNL`), ^C·^Z·^\를 신호로 처리하며 입력 큐를 비우고(`ISIG`), 0x11/0x13을 흐름 제어로 먹고(`IXON`), 받은 걸 echo한다. 바이너리 프로토콜에서는 이게 데이터 손상이다. 실험에서 0x00~0xFF를 기본 설정으로 받았더니 257바이트 중 226바이트만 왔고 raw로는 전부 왔다.

> The default termios settings are designed for a human at a terminal. ICRNL turns carriage returns into newlines, ISIG turns 0x03, 0x1A and 0x1C into signals and flushes the input queue, IXON swallows 0x11 and 0x13 as flow control, ECHO sends everything back to the device, and canonical mode holds data until a newline. For a GNSS module or an MCU protocol that's silent data corruption that only shows up with certain byte values. When I sent all 256 byte values through a default-configured port, 31 bytes disappeared; in raw mode all 257 arrived. So I always call `cfmakeraw`, set CLOCAL and CREAD explicitly, set the baud rate, and then read the settings back to verify.

**Q.** What do VMIN and VTIME do?

**A.** non-canonical 모드에서 `read()`가 언제 돌아올지 정한다. 0/0은 폴링, MIN>0/TIME=0은 MIN 바이트까지 블록, MIN=0/TIME>0은 전체 타임아웃(0.1초 단위), 둘 다 양수면 첫 바이트 이후의 바이트 간 타이머다.

> They only matter in non-canonical mode. VMIN is the minimum byte count and VTIME is a timer in tenths of a second. Zero and zero means poll — return whatever is there, possibly zero. VMIN greater than zero with VTIME zero blocks until that many bytes arrive. VMIN zero with VTIME positive is an overall timeout: return as soon as one byte arrives, or zero after the timeout — handy for detecting a dead GNSS. Both positive means an inter-byte timer that starts after the first byte. Either way, read boundaries don't match message boundaries, so the application must reassemble frames itself.

**Q.** What is serdev, and why is it better than running hciattach or a user-space daemon?

**A.** UART에 붙은 장치(BT·GNSS·MCU)를 DT의 UART 자식 노드로 기술하고 `serdev_device_driver`로 커널 드라이버를 붙이는 버스다. 전원·GPIO·baud 절차를 드라이버가 PM과 함께 관리하고, 보드 정보가 스크립트가 아니라 DT에 있게 된다. 자식 노드가 있으면 serial core가 tty 노드 대신 serdev 컨트롤러를 등록하고, 데이터는 ldisc 대신 드라이버의 `receive_buf`로 간다.

> serdev is a bus for devices that sit behind a UART. You describe the device as a child node of the UART in the device tree, and a `serdev_device_driver` binds to it like an I2C or SPI client driver. When the serial core registers a port that has a child node, it creates a serdev controller instead of a /dev/tty node, and incoming data goes from flush_to_ldisc straight to the driver's `receive_buf` callback — no line discipline. The driver opens the port in the kernel, sets the baud rate and flow control, and owns the regulators, reset and wake GPIOs, so power sequencing and suspend/resume are handled properly. With hciattach, that knowledge lived in boot scripts, raced with other services, and the kernel's power management couldn't see it. I wrote a small serdev GNSS driver bound to a second PL011 in QEMU to see it end to end.

**꼬리 질문**: "What default does serdev apply when it opens the port?" → raw 8N1에 `CRTSCTS`를 켠다. RTS/CTS 선이 없는 장치는 드라이버가 흐름 제어를 꺼야 한다.

**Q.** How would you debug "the GNSS data is occasionally corrupted"?

**A.** 숫자부터 본다: `/proc/tty/driver/<drv>`나 `TIOCGICOUNT`의 `fe`/`pe`/`oe`. framing이면 baud·클럭·배선·노이즈, overrun이면 IRQ 지연·흐름 제어·DMA, 둘 다 0이면 소프트웨어 — termios(raw 여부, echo)와 앱의 프레임 재조립을 본다. 그 다음 로직 분석기로 선로를 찍어 기준 데이터를 확보한다.

> First I'd separate hardware from software using the counters in /proc/tty/driver or TIOCGICOUNT. Framing errors point to baud mismatch, clock error or signal integrity — I'd compute the divisor error and put a logic analyzer on the line. Overruns point to interrupt latency or missing flow control, and the fix is RTS/CTS, DMA or tuning IRQ thread priority. If the counters are clean, it's software: check that the app set raw mode and disabled echo, that it doesn't assume one read equals one sentence, and that it validates checksums and resyncs on the start character.

**Q.** How does the kernel console differ from a TTY, and what is earlycon?

**A.** printk는 tty 계층을 거치지 않고 드라이버가 등록한 `struct console`의 write로 직접 레지스터에 쓴다. `console=`가 어느 콘솔을 쓸지 정한다. earlycon은 드라이버 모델 이전부터 레지스터 주소만으로 동작하는 부트 콘솔로, 진짜 콘솔이 등록되면 꺼진다.

> Kernel messages don't go through the TTY layer. A serial driver registers a `struct console`, and printk calls its write function directly, so it works from interrupt context and during a panic. `console=` on the command line picks which console devices are used, and the last one becomes /dev/console. earlycon is a minimal boot console that only needs the register address, so it works right after the device tree is parsed, long before the real driver probes. In QEMU I saw the earlycon at time zero and the switch to the real ttyAMA0 console at about 0.45 seconds — any crash before that is invisible without earlycon.

**Q.** How does RS-485 direction control work in Linux?

**A.** 트랜시버의 DE를 보통 RTS로 제어하고, serial core의 RS-485 모드(`TIOCSRS485`, DT `linux,rs485-enabled-at-boot-time`, `rs485-rts-delay`)가 드라이버의 TX 완료 시점에 RTS를 내린다. 유저 공간에서 `tcdrain` 후 GPIO를 내리면 스케줄링 지연 때문에 응답과 충돌한다.

> RS-485 is half-duplex, so the transceiver's driver-enable has to be asserted only while we transmit, usually wired to RTS. The serial core has an RS-485 mode — set with the TIOCSRS485 ioctl or the device-tree properties — where the UART driver raises RTS before sending and drops it when the last stop bit has left the shifter, with optional delays. Doing it from user space with tcdrain and a GPIO adds scheduling latency and you collide with the slave's reply. I'd verify the timing with a logic analyzer on TX and RTS.

---

## 15. 직접 해보기

1. (개념) `/dev/ttyAMA1`을 읽는 앱의 `read()`가 블록돼 있을 때 GNSS가 한 바이트를 보냈다. 그 바이트가 앱 버퍼에 들어가기까지 거치는 함수와 컨텍스트(hardirq / kworker / 앱 프로세스)를 순서대로 적어라.
   정답/힌트: `pl011_int`(hardirq) → `tty_insert_flip_*` → `tty_flip_buffer_push` → `flush_to_ldisc`(kworker) → `n_tty_receive_buf2` → `wake_up_interruptible_poll` → `n_tty_read`가 깨어나 `copy_to_user`(앱 프로세스). canonical이면 줄 끝이 와야 깨운다.
2. (코드) 예제 8을 고쳐 u-blox UBX 프레임(`0xB5 0x62`, class, id, 2바이트 리틀엔디언 길이, payload, CK_A/CK_B Fletcher-8)을 파싱하는 리더를 짜라. 생성기에 UBX 모드를 추가해 검증하라.
   정답/힌트: 상태 기계(SYNC1 → SYNC2 → HEADER → PAYLOAD → CK)로 짜고, 체크섬은 class부터 payload 끝까지 `a += b; b += a`를 바이트마다.
3. (관찰) 예제 7의 D(VMIN=64, VTIME=2)에서 생성기 페이싱을 바이트 단위(`time.sleep(10/baud)`를 바이트마다)로 바꾸면 `read()` 크기 분포가 어떻게 달라지는지 예측하고 측정하라.
   정답/힌트: 64바이트가 모이는 시점에 깨어나므로 64 근처로 모인다. 문장 끝에서는 0.2초 간격 타이머로 나머지가 나온다.
4. (코드·커널) `h8_gnss`에 `write_wakeup`을 이용한 블로킹 송신(`serdev_device_write()`)을 쓰는 sysfs 속성 `cmd`(쓰면 그 문자열을 체크섬 붙여 송신)를 추가하라.
   정답/힌트: `DEVICE_ATTR_WO(cmd)`, store에서 NMEA 체크섬 계산 후 `serdev_device_write(serdev, buf, len, msecs_to_jiffies(500))`. `.write_wakeup = serdev_device_write_wakeup`이 이미 있다.
5. (관찰) `Image-dbg`로 부팅해 `uart_write`와 `pl011_start_tx`에 kprobe를 걸고, `echo hi > /dev/ttyAMA1`이 만드는 스택을 찍어 §11 표의 `write()` 줄과 비교하라.
   정답/힌트: 예제 5의 `kprobe_events` 방식 그대로. `n_tty_write` → `uart_write` → `__uart_start` → `pl011_start_tx`가 보여야 한다.
6. (개념) serdev 드라이버의 `receive_buf`가 `count`보다 작은 값을 반환하면 무슨 일이 일어나나?
   정답/힌트: 소비 안 된 바이트는 flip buffer에 남아 다음 `flush_to_ldisc` 때 다시 전달된다. 계속 0을 반환하면 버퍼가 차고 결국 throttle·overrun으로 이어진다.
7. (실무) 보드에서 BT가 1회 부팅 중 3회 정도 올라오지 않는다. `hci_bcm` serdev 드라이버를 쓰고 있다. 확인할 순서를 5단계로 적어라.
   정답/힌트: dmesg의 probe·펌웨어 로드 에러 → `/proc/tty/driver`의 fe/oe → DT `uart-has-rtscts`와 실제 배선 → UART 클럭으로 목표 baud 가능 여부(§3.3) → shutdown GPIO와 전원 레일 타이밍(스코프).

---

## 16. 용어 사전

| 용어 | 뜻 | 한 줄 설명 |
|---|---|---|
| TTY | teletype | 리눅스의 "터미널" 장치 계층. UART·pty·가상 콘솔이 모두 TTY다 |
| serial core | 시리얼 공통 코드 | 모든 UART 드라이버가 공유하는 `serial_core.c`. TX kfifo·termios·콘솔 처리 |
| `uart_port` / `uart_ops` | 포트 구조체 / 하드웨어 함수 표 | UART 드라이버가 채우는 것 |
| `tty_port` | 물리 포트 객체 | flip buffer·xmit_fifo·client_ops를 가짐, 장치 수명 |
| `tty_struct` | 열린 터미널 세션 | termios·ldisc·대기 큐, open~close 수명 |
| flip buffer | RX 버퍼 | ISR이 채우고 워크큐가 ldisc로 넘기는 `tty_buffer` 리스트 |
| line discipline (ldisc) | 회선 규율 | TTY와 앱 사이의 교체 가능한 프로토콜 처리기 (N_TTY, N_HCI, N_GSM0710) |
| canonical mode | 줄 모드 | 줄 끝이 올 때까지 모아서 돌려주고 편집 문자를 처리 |
| raw mode | 원시 모드 | 변환·편집·echo·신호 없이 바이트 그대로 (`cfmakeraw`) |
| VMIN / VTIME | 최소 바이트 / 타이머 | non-canonical `read()`의 반환 조건 |
| CRTSCTS | 하드웨어 흐름 제어 | RTS/CTS 선으로 송신을 멈추고 재개 |
| XON/XOFF | 소프트웨어 흐름 제어 | 데이터 안의 0x11/0x13으로 흐름 제어 |
| earlycon | 초기 콘솔 | 드라이버 probe 전부터 레지스터 주소만으로 쓰는 부트 콘솔 |
| nbcon | non-blocking console | 6.x printk의 새 콘솔 인터페이스 (`write_atomic`/`write_thread`) |
| serdev | serial device bus | UART 뒤 장치를 DT 자식 노드 + 커널 드라이버로 다루는 버스 |
| GNSS 서브시스템 | `drivers/gnss` | `/dev/gnssN`으로 원시 GNSS 데이터를 전달, 전원·baud만 관리 |
| RS-485 | 차동 반이중 버스 | RTS로 트랜시버 방향을 제어, `TIOCSRS485` |
| NMEA 0183 | GNSS 텍스트 프로토콜 | `$GPGGA,...*HH\r\n`, XOR 체크섬 |
| overrun (oe) | 수신 넘침 | 하드웨어 FIFO가 비워지기 전에 새 바이트가 옴 |
| framing error (fe) | 프레임 에러 | 정지 비트 자리에 1이 없음 — baud 불일치·노이즈 |

---

## 17. 요약 & 체크리스트

Linux에서 UART는 UART 드라이버(`uart_port`/`uart_ops`) → serial core(TX `kfifo`, termios, 콘솔) → TTY core(`tty_port`의 flip buffer, `tty_struct`) → line discipline(N_TTY) → `/dev/ttyXXX`의 다섯 층이다. RX는 IRQ에서 flip buffer에 넣고 워크큐(kworker)에서 ldisc로 올리며, 이 노트에서는 kprobe로 그 두 컨텍스트를 실제로 찍었다. 기본 termios는 사람 터미널용이라 바이너리·기계 프로토콜에서는 `ICRNL`·`ISIG`·`IXON`·`ECHO`가 데이터를 바꾸거나 먹거나 되돌려 보낸다. 그래서 앱은 `cfmakeraw` + `CLOCAL`/`CREAD` + baud를 **전부 명시**하고, `read()` 경계와 무관하게 프레임을 재조립하며, 설정을 다시 읽어 확인한다. UART 뒤의 장치(GNSS·BT·MCU)는 DT 자식 노드 + serdev 드라이버로 커널이 전원·baud·수명을 관리하는 것이 현재의 표준이고, 그러면 tty 노드 대신 serdev 컨트롤러가 생긴다. 콘솔은 tty와 별개의 printk 경로이고 `earlycon`이 부팅 초반을 책임진다. 에뮬레이터는 baud를 흉내 내지 않으므로 타이밍·처리량 결론은 실제 보드와 로직 분석기로 확인한다.

- [ ] UART 드라이버 → serial core → TTY core → ldisc → 앱의 다섯 층과 각 층의 대표 구조체를 그릴 수 있다
- [ ] RX 바이트가 IRQ → flip buffer → 워크큐 → `n_tty_receive_buf2` → reader 깨우기로 가는 경로와 각 컨텍스트를 설명할 수 있다
- [ ] `uart_ops`의 `startup`·`set_termios`·`start_tx`·`stop_rx`·`tx_empty`가 언제 불리는지 말할 수 있다
- [ ] 6.10 이후 TX 버퍼가 `tty_port.xmit_fifo`(kfifo)라는 것을 알고 옛 `circ_buf` 코드를 알아본다
- [ ] 기본 termios가 바이너리 데이터에 하는 일(ICRNL, ISIG 큐 비우기, IXON, ECHO)을 예로 들 수 있다
- [ ] `cfmakeraw` + `CLOCAL`/`CREAD` + baud 설정 + `tcgetattr` 재확인 코드를 짤 수 있다
- [ ] VMIN/VTIME 네 가지 조합의 동작을 설명하고 용도에 맞게 고를 수 있다
- [ ] raw 스트림에서 프레임을 재조립하고 체크섬으로 재동기화하는 리더를 짤 수 있다
- [ ] PL011 baud 분주와 오차를 계산하고 최대 baud가 `uartclk/16`인 이유를 말할 수 있다
- [ ] `/proc/tty/driver/*`와 `TIOCGICOUNT`로 fe/pe/oe를 보고 하드웨어/소프트웨어 문제를 가를 수 있다
- [ ] DT 자식 노드 + `serdev_device_driver`로 UART 장치 드라이버를 짜고, 왜 hciattach보다 나은지 설명할 수 있다
- [ ] `console=`, `earlycon`, RS-485(`TIOCSRS485`, DT 속성)를 설정할 수 있다

---

## 참고 자료

- man 3 termios — https://man7.org/linux/man-pages/man3/termios.3.html (`cfmakeraw`, VMIN/VTIME, 플래그 전체)
- man 4 tty_ioctl — https://man7.org/linux/man-pages/man4/tty_ioctl.4.html (`TIOCGETD`/`TIOCSETD`, `TIOCMGET`, `TIOCGICOUNT` …)
- man 1 stty — https://man7.org/linux/man-pages/man1/stty.1.html
- Michael Kerrisk, "The Linux Programming Interface", 62장 Terminals (canonical/noncanonical, VMIN/VTIME, termios 플래그)
- Linux kernel docs, "Low Level Serial API" — https://docs.kernel.org/driver-api/serial/driver.html (`uart_ops` 각 함수의 호출 조건과 락 규칙)
- Linux kernel docs, "The Linux TTY layer" — https://docs.kernel.org/driver-api/tty/index.html (tty_struct, tty_port, ldisc, tty_buffer)
- serdev 1차 자료: `include/linux/serdev.h`, `drivers/tty/serdev/core.c`, `drivers/tty/serdev/serdev-ttyport.c` (커널 소스 내 kernel-doc 주석)
- Linux kernel docs, "RS485 Serial Communications" — https://docs.kernel.org/driver-api/serial/serial-rs485.html
- Linux kernel docs, "GNSS subsystem" — https://docs.kernel.org/driver-api/gnss.html
- DT 바인딩: `Documentation/devicetree/bindings/serial/serial.yaml`(자식 노드 규칙, `current-speed`, `uart-has-rtscts`), `serial/rs485.yaml`, `net/bluetooth/brcm,bluetooth.yaml`, `gnss/`
- 소스 (elixir.bootlin.com, v6.18): `drivers/tty/serial/amba-pl011.c`, `drivers/tty/serial/serial_core.c`, `drivers/tty/tty_io.c`, `drivers/tty/tty_buffer.c`, `drivers/tty/n_tty.c`, `drivers/tty/serdev/serdev-ttyport.c`, `drivers/gnss/mtk.c`, `drivers/gnss/serial.c`
- Bootlin, "Linux kernel and driver development" 교육 자료 — https://bootlin.com/doc/training/linux-kernel/ (serial 드라이버 장)
