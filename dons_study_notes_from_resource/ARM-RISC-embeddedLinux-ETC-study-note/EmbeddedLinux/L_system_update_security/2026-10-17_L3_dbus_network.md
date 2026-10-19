# L3. D-Bus와 네트워크 구성 — 시스템 버스로 데몬을 제어하고, networkd·NetworkManager로 정적 IP를 박는다

> **이 노트를 다 읽으면**: D-Bus의 객체 모델(버스 이름·객체 경로·인터페이스·메서드·시그널·프로퍼티·타입 시그니처)과 정책 파일·activation을 그림으로 설명하고 `busctl`로 systemd·networkd를 직접 들여다볼 수 있다 · 시스템 버스에 내 "device manager" 데몬(메서드+프로퍼티+시그널)을 올리고, 정책이 없을 때 나는 AccessDenied를 재현·해결할 수 있다 · systemd-networkd로 DHCP/정적 IP/브리지/VLAN을 설정하고 `network-online.target`이 부팅을 2분 붙잡는 이유를 `systemd-analyze`로 증명할 수 있다 · NetworkManager로 바꿔 nmcli 프로필과 keyfile을 만들고, 제품에 networkd와 NM 중 무엇을 쓸지 근거를 대고 고를 수 있다
> **왜 필요한가**: "데몬의 장치 제어를 다른 프로세스에 어떻게 노출하나?", "부팅이 network-online.target에서 2분 멈춘다", "공장 서비스 포트에 고정 IP를 어떻게 박나?", "networkd냐 NetworkManager냐?" — 시스템 통합 담당이 입사 첫 달에 반드시 만나는 장면이다
> **Don 기준 난이도**: 호스트-디바이스 메일박스 프로토콜(명령·응답·비동기 이벤트), 레지스터 맵을 "이름 있는 필드"로 노출하는 감각은 이미 안다 / D-Bus의 이름·정책·activation 규칙, Linux 네트워크 설정 데몬들의 역할 분담과 부팅 순서는 새로 배운다
> **선행 노트**: A7(IPC, 6.2절 D-Bus 미리보기), A9(소켓, 11절 netlink), I6(net_device), K6(Ubuntu 기반 rootfs), L1(systemd unit·target) — 컨테이너 네트워크(netns, veth)는 L8, udev의 `.link` 이름 규칙은 L2

---

## 0. 큰 그림 — 이게 왜 필요한가

센서 키트 장비를 떠올려 보자. Jetson Orin 위에 카메라 데몬, IMU 데몬, 웹 UI, 진단 툴, OTA 에이전트(L4)가 돈다. 이 프로세스들은 서로에게 **"모드를 capture로 바꿔라"**, **"지금 온도는?"**, **"링크가 끊겼다"** 같은 작은 메시지를 주고받아야 한다. 또 장비에는 이더넷 포트가 2~3개 있다: LiDAR가 붙는 포트(고정 IP), 차량/로봇 내부 네트워크(VLAN), 공장·현장 정비용 서비스 포트(고정 IP + 링크로컬 백업), 그리고 가끔 Wi-Fi나 LTE 모뎀.

이 두 가지 일 — **프로세스 사이의 제어 통로**와 **네트워크 설정** — 은 Linux에서 놀랄 만큼 단단히 엮여 있다. 네트워크 설정 데몬(systemd-networkd, NetworkManager)이 바로 D-Bus로 "명령을 받고 상태를 알리는" 대표적인 서비스이기 때문이다. `nmcli`, `networkctl`, `timedatectl`, `hostnamectl`, `systemctl`은 전부 D-Bus 클라이언트다.

```svg
<svg viewBox="0 0 680 400" xmlns="http://www.w3.org/2000/svg">
<defs><marker id="ah1" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto-start-reverse"><path d="M0,0 L10,5 L0,10 z" fill="currentColor"/></marker></defs>
<text x="20" y="24" font-size="13" font-weight="bold">클라이언트 (CLI · 앱)</text>
<rect x="20" y="34" width="120" height="30" rx="5" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
<text x="80" y="54" font-size="12" text-anchor="middle">systemctl</text>
<rect x="150" y="34" width="120" height="30" rx="5" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
<text x="210" y="54" font-size="12" text-anchor="middle">networkctl</text>
<rect x="280" y="34" width="120" height="30" rx="5" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
<text x="340" y="54" font-size="12" text-anchor="middle">nmcli</text>
<rect x="410" y="34" width="120" height="30" rx="5" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
<text x="470" y="54" font-size="12" text-anchor="middle">웹 UI · 진단 툴</text>
<rect x="540" y="34" width="120" height="30" rx="5" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
<text x="600" y="54" font-size="12" text-anchor="middle">busctl</text>
<line x1="340" y1="66" x2="340" y2="94" stroke="currentColor" stroke-width="1.3" marker-end="url(#ah1)"/>
<rect x="20" y="96" width="640" height="40" rx="6" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
<text x="340" y="114" font-size="13" text-anchor="middle" font-weight="bold">D-Bus system bus — dbus-daemon 또는 dbus-broker</text>
<text x="340" y="130" font-size="12" text-anchor="middle">/run/dbus/system_bus_socket (AF_UNIX) · 이름 관리 · 정책 검사 · 시그널 배달</text>
<line x1="340" y1="138" x2="340" y2="164" stroke="currentColor" stroke-width="1.3" marker-end="url(#ah1)"/>
<text x="20" y="160" font-size="13" font-weight="bold">서비스 (데몬)</text>
<rect x="20" y="168" width="100" height="44" rx="5" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
<text x="70" y="186" font-size="12" text-anchor="middle">systemd</text>
<text x="70" y="202" font-size="12" text-anchor="middle">PID 1</text>
<rect x="128" y="168" width="100" height="44" rx="5" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
<text x="178" y="186" font-size="12" text-anchor="middle">logind</text>
<text x="178" y="202" font-size="12" text-anchor="middle">hostnamed</text>
<rect x="236" y="168" width="100" height="44" rx="5" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
<text x="286" y="186" font-size="12" text-anchor="middle">networkd</text>
<text x="286" y="202" font-size="12" text-anchor="middle">resolved</text>
<rect x="344" y="168" width="100" height="44" rx="5" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
<text x="394" y="186" font-size="12" text-anchor="middle">Network</text>
<text x="394" y="202" font-size="12" text-anchor="middle">Manager</text>
<rect x="452" y="168" width="100" height="44" rx="5" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
<text x="502" y="186" font-size="12" text-anchor="middle">timesyncd</text>
<text x="502" y="202" font-size="12" text-anchor="middle">BlueZ · MM</text>
<rect x="560" y="168" width="100" height="44" rx="5" fill="#d0564a" fill-opacity="0.12" stroke="#d0564a"/>
<text x="610" y="186" font-size="12" text-anchor="middle">l3-devmgr</text>
<text x="610" y="202" font-size="12" text-anchor="middle">(내 데몬)</text>
<line x1="340" y1="214" x2="340" y2="244" stroke="currentColor" stroke-width="1.3" marker-end="url(#ah1)"/>
<text x="352" y="234" font-size="12">rtnetlink · ioctl · /sys · 장치 파일</text>
<rect x="20" y="246" width="640" height="96" rx="6" fill="#888" fill-opacity="0.10" stroke="#888"/>
<text x="32" y="266" font-size="13" font-weight="bold">Linux kernel</text>
<rect x="32" y="276" width="190" height="54" rx="5" fill="#888" fill-opacity="0.12" stroke="#888"/>
<text x="127" y="298" font-size="12" text-anchor="middle">net_device (I6)</text>
<text x="127" y="316" font-size="12" text-anchor="middle">eth0 · eth1 · wlan0 · br0</text>
<rect x="234" y="276" width="200" height="54" rx="5" fill="#888" fill-opacity="0.12" stroke="#888"/>
<text x="334" y="298" font-size="12" text-anchor="middle">IP 주소 · 라우트 · 이웃</text>
<text x="334" y="316" font-size="12" text-anchor="middle">netns (L8)</text>
<rect x="446" y="276" width="202" height="54" rx="5" fill="#888" fill-opacity="0.12" stroke="#888"/>
<text x="547" y="298" font-size="12" text-anchor="middle">센서 드라이버</text>
<text x="547" y="316" font-size="12" text-anchor="middle">IIO · V4L2 (I2, I5)</text>
<text x="340" y="370" font-size="12" text-anchor="middle">D-Bus는 "제어·상태" 통로다. 센서 데이터·프레임은 shm·fd·소켓으로 따로 간다 (A7)</text>
<text x="340" y="390" font-size="12" text-anchor="middle">네트워크 설정 데몬은 D-Bus로 명령을 받고, rtnetlink로 커널을 설정한다</text>
</svg>
```
그림 1 — 장비 안의 제어 평면. CLI와 앱은 D-Bus 시스템 버스에 붙고, 버스 데몬이 이름·정책·배달을 맡는다. 네트워크 데몬은 버스에서 명령을 받아 rtnetlink로 커널 `net_device`를 설정한다.

bare-metal 비유로 정리하면:

| Linux 개념 | Don이 아는 것 |
|---|---|
| D-Bus 시스템 버스 | 호스트와 여러 디바이스가 붙은 공용 메일박스 버스 + 중재자(arbiter) |
| well-known bus name | 버스 위의 고정 "장치 주소" (재시작해도 같은 주소) |
| unique name `:1.42` | 연결될 때마다 새로 받는 세션 ID (재시작하면 바뀜) |
| object path + interface | 장치 안의 레지스터 블록 + 그 블록의 레지스터 맵 정의 |
| method call / return | 명령 큐에 넣은 명령과 완료 응답 (NVMe SQ/CQ의 한 쌍) |
| signal | 비동기 이벤트 통지 (AER, 인터럽트 기반 상태 통지) |
| property | 읽고 쓸 수 있는 "이름 있는 레지스터 필드" |
| 정책 파일 | MPU/방화벽 규칙: 누가 어느 주소를 차지하고 어디로 쓸 수 있나 |
| D-Bus activation | 첫 접근 때 전원을 켜는 lazy power-up |
| networkd/NM | 링크·주소·라우트를 관리하는 상주 펌웨어 태스크 |

이 노트의 순서: D-Bus 개념(1절) → 버스 데몬과 정책(2절) → `busctl`로 실물 보기(3절) → 내 D-Bus 서비스 만들기(4절) → 커널 네트워크와 iproute2(5절) → systemd-networkd(6절) → `network-online.target`과 부팅 지연(7절) → NetworkManager(8절) → 두 장비를 정적 IP로 잇기·서비스 포트·시간 동기·트러블슈팅(9~10절) → 커널 내부(11절).

### 0.1 실습 환경

모든 실행 출력은 Lima VM(`elinux`, 호스트 커널 `7.0.0-38-generic`) 안의 **QEMU 게스트**에서 얻었다. 게스트는 랩 커널 `Image-sec`(6.18.55-sec) + `rootfs-sysd.ext4`(Ubuntu 24.04.5 arm64, systemd 255, dbus, networkd/resolved/timesyncd, NetworkManager 설치·비활성, python3-dbus)다. 공용 이미지는 읽기 전용이라 `/var/tmp/L3/`에 복사본을 만들어 부팅했다. QEMU는 KVM 없이 TCG로 돌아서 **부팅 시간·지연 숫자는 실제 하드웨어 값이 아니다** — 상대 비교로만 본다.

```sh
uname -r; systemctl --version | head -1; dbus-daemon --version | head -1
systemctl is-active dbus dbus-broker systemd-networkd NetworkManager
```

```text
6.18.55-sec
systemd 255 (255.4-1ubuntu8.18)
D-Bus Message Bus Daemon 1.14.10
active
inactive
active
inactive
```

게스트 부팅 명령의 뼈대(VM 안, 이미지 복사본 사용):

```sh
cp --sparse=always /var/lab/out/rootfs-sysd.ext4 /var/tmp/L3/l3_a.ext4
qemu-system-aarch64 -machine virt -cpu cortex-a57 -smp 2 -m 1G -display none -monitor none \
  -kernel /var/lab/out/Image-sec -drive file=/var/tmp/L3/l3_a.ext4,if=virtio,format=raw \
  -netdev user,id=n0 -device virtio-net-pci,netdev=n0 \
  -netdev user,id=n1,net=10.0.3.0/24 -device virtio-net-pci,netdev=n1 \
  -chardev socket,id=c0,path=/var/tmp/L3/l3_a.sock,server=on,wait=off,logfile=/var/tmp/L3/l3_a.log \
  -serial chardev:c0 -append "console=ttyAMA0 root=/dev/vda rw rootwait"
```

시리얼 콘솔을 UNIX 소켓으로 빼고, 작은 Python 스크립트가 그 소켓에 명령을 쓰고 끝 표시가 나올 때까지 출력을 읽는 방식으로 게스트 셸을 자동 조작했다. 파일은 base64로 쪼개 같은 소켓으로 넣었다. 이 이미지로 TCG 부팅은 로그인까지 약 1.5분, 부팅 완료(`systemd-analyze`)까지 1.5~3분 걸렸다.

---

## 1. D-Bus 개념 — 버스, 이름, 객체, 인터페이스, 멤버

### 1.1 직관

A7에서 본 UNIX domain socket은 **1:1 전화선**이다. 서버가 소켓 파일을 만들고, 클라이언트가 그 경로로 접속한다. 프로세스가 열 개면 전화선도 열 개, 프로토콜(프레이밍·직렬화·인증)도 열 개를 각자 짠다.

D-Bus는 그 위에 **교환원이 있는 전화국**을 얹는다. 모든 프로세스가 버스 데몬 하나에 UNIX socket으로 접속하고, 메시지에 "받는 사람 이름"을 적어 보내면 데몬이 배달한다. 덕분에 공짜로 얻는 것:

- **이름으로 찾기** (service discovery): `org.freedesktop.NetworkManager`라는 이름만 알면 된다. 소켓 경로나 PID는 몰라도 된다.
- **타입 있는 메시지**: 정수·문자열·배열·딕셔너리가 표준 형식으로 직렬화된다(marshalling).
- **브로드캐스트**: 시그널 하나를 관심 있는 모든 구독자에게 뿌린다.
- **보안 검사의 한 지점**: 누가 어떤 이름을 가질 수 있고, 누가 누구에게 무엇을 보낼 수 있는지를 데몬이 정책 파일로 검사한다. 보낸 쪽 UID/PID는 데몬이 커널(`SO_PEERCRED`, A7)에서 직접 얻으니 위조할 수 없다.
- **생명주기 통지**: 서비스가 죽으면 데몬이 `NameOwnerChanged` 시그널을 뿌린다.
- **필요할 때 띄우기** (activation): 아직 안 떠 있는 서비스 이름으로 메시지가 오면 데몬이 systemd에게 그 서비스를 시작시킨다.

### 1.2 정의 — 메시지 하나의 주소 체계

D-Bus 메시지는 "어느 **버스**의, 어느 **연결(이름)**의, 어느 **객체**의, 어느 **인터페이스**의, 어느 **멤버**"로 주소가 정해진다.

| 층 | 예 | 뜻 |
|---|---|---|
| bus | system bus / session bus | 버스 데몬 인스턴스. 시스템 버스는 장비 전체에 하나, 세션 버스는 로그인 사용자마다 하나 |
| unique name | `:1.42` | 연결마다 데몬이 붙여 주는 이름. 다시 쓰지 않는다 |
| well-known name | `org.freedesktop.network1` | 서비스가 요청해서 소유하는 이름. 역도메인 표기 |
| object path | `/org/freedesktop/network1/link/_32` | 한 연결 안의 객체 트리. 파일 경로처럼 생겼다 |
| interface | `org.freedesktop.network1.Link` | 객체가 구현하는 멤버들의 묶음(C로 치면 함수 포인터 표) |
| member | `Reconfigure`, `OperationalState` | 메서드·시그널·프로퍼티 이름 |

**system bus vs session bus**: 임베디드 장비에서는 거의 항상 **system bus**다. 소켓은 `/run/dbus/system_bus_socket`이고 정책이 엄격하다(기본이 "거부"). session bus는 데스크톱 로그인 세션(GNOME 등)용이고 같은 사용자끼리라 기본이 "허용"이다. 헤드리스 장비에는 session bus가 아예 없는 경우가 많다.

**unique vs well-known name**: 서비스는 접속하면 먼저 `:1.42` 같은 unique name을 받고, 그다음 `RequestName("com.example.L3.DeviceManager")`로 well-known name을 요청한다. 클라이언트는 well-known name으로 보내고, 데몬이 현재 소유자의 unique name으로 바꿔 배달한다. 서비스가 재시작하면 unique name은 바뀌지만 well-known name은 같다. 그래서 **시그널을 구독할 때 sender를 unique name으로 고정해 두면 재시작 뒤에 신호를 놓친다** — 흔한 버그다.

### 1.3 메시지 네 종류

| 타입 | 방향 | 펌웨어 비유 | 예 |
|---|---|---|---|
| `METHOD_CALL` | 클라이언트 → 서비스 | 명령 제출 | `StartUnit("ssh.service","replace")` |
| `METHOD_RETURN` | 서비스 → 호출자 | 완료 응답 + 결과 | 잡 객체 경로 `o` |
| `ERROR` | 서비스(또는 데몬) → 호출자 | 에러 상태 코드 | `org.freedesktop.DBus.Error.AccessDenied` |
| `SIGNAL` | 서비스 → 구독자 전부 | 비동기 이벤트(인터럽트) | `PropertiesChanged`, `JobRemoved` |

메서드 호출은 요청-응답 짝이 serial 번호로 묶인다(`reply_serial`). 응답이 기본 25초 안에 안 오면 클라이언트 라이브러리가 `NoReply` 에러를 낸다. 시그널은 응답이 없고, **match rule**(예: `type='signal',interface='org.freedesktop.DBus.Properties'`)을 데몬에 등록한 연결에만 배달된다.

**프로퍼티**는 별도 메시지 타입이 아니다. 표준 인터페이스 `org.freedesktop.DBus.Properties`의 메서드 `Get(ss)`, `Set(ssv)`, `GetAll(s)`와 시그널 `PropertiesChanged(sa{sv}as)`로 구현된 **관례**다. 그래서 `busctl get-property`는 내부적으로 `Properties.Get` 메서드 호출이다.

표준 인터페이스 세 개는 거의 모든 객체가 구현한다:

- `org.freedesktop.DBus.Introspectable.Introspect()` → 객체의 인터페이스·멤버를 XML로 돌려준다.
- `org.freedesktop.DBus.Properties` → 위의 Get/Set/GetAll.
- `org.freedesktop.DBus.Peer.Ping()` → 살아 있나 확인.

여기에 `org.freedesktop.DBus.ObjectManager.GetManagedObjects()`가 있으면 객체 트리 전체와 프로퍼티를 한 번에 받을 수 있다(BlueZ, NetworkManager의 일부, ModemManager가 쓴다).

### 1.4 타입 시그니처

D-Bus는 값의 타입을 문자 하나로 적는다. `busctl call`에서 인자 앞에 이 시그니처를 직접 써야 하니 외워 두자.

| 문자 | 타입 | C 대응 |
|---|---|---|
| `y` | byte | `uint8_t` |
| `b` | boolean | 32비트 0/1 |
| `n` `q` | int16 / uint16 | `int16_t` `uint16_t` |
| `i` `u` | int32 / uint32 | `int32_t` `uint32_t` |
| `x` `t` | int64 / uint64 | `int64_t` `uint64_t` |
| `d` | double | `double` |
| `s` | string (UTF-8) | `const char *` |
| `o` | object path | 경로 문자열 |
| `g` | signature | 시그니처 문자열 |
| `h` | UNIX fd | 소켓으로 넘기는 fd (`SCM_RIGHTS`) |
| `v` | variant (타입 + 값) | tagged union |
| `aX` | X의 배열 | 길이 + 원소들 |
| `(…)` | struct | 구조체 |
| `a{KV}` | dict (K→V) | 키-값 배열 |

자주 보는 조합: `a{sv}`(문자열 키 → 아무 값, "옵션 딕셔너리"), `a(sssssouso)`(systemd `ListUnits`의 결과: 유닛 하나가 9필드 구조체), `ao`(객체 경로 배열). `h`가 있다는 점이 중요하다: D-Bus로 **협상은 하고 큰 데이터는 fd로 넘기는** 패턴(A7 6.2절)이 여기서 가능하다. logind의 `TakeDevice`가 `/dev/input`·`/dev/dri` fd를 이렇게 넘긴다.

### 1.5 Introspection XML

`Introspect()`가 돌려주는 XML은 그 객체의 "레지스터 맵 정의서"다. 4절에서 만들 서비스를 sd-bus나 GDBus로 구현하면 대략 이렇게 생긴다 (손으로 쓴 예 — 실행 출력이 아니다. dbus-python 구현에서는 `<property>`가 빠진다는 것을 4.5절에서 실제로 본다):

```text
<node>
  <interface name="com.example.L3.DeviceManager1">
    <method name="SetMode">
      <arg direction="in"  type="s" name="mode"/>
      <arg direction="out" type="b"/>
    </method>
    <signal name="ModeChanged">
      <arg type="s" name="old"/>
      <arg type="s" name="new"/>
    </signal>
    <property name="Mode" type="s" access="read"/>
  </interface>
  <node name="child"/>      <!-- 하위 객체가 있으면 이름만 나온다 -->
</node>
```

`busctl introspect`는 이 XML을 표로 바꿔 보여 주고, `busctl tree`는 `<node name=…>`를 따라 재귀적으로 내려가 객체 트리를 그린다. 이 XML에서 C/C++ 프록시 코드를 생성하는 도구(`gdbus-codegen`, `sdbus-c++-xml2cpp`)도 있다 — 인터페이스 정의를 먼저 XML로 고정하고 양쪽 코드를 생성하는 것이 제품에서 권장되는 방식이다.

```svg
<svg viewBox="0 0 680 360" xmlns="http://www.w3.org/2000/svg">
<defs><marker id="ah2" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto-start-reverse"><path d="M0,0 L10,5 L0,10 z" fill="currentColor"/></marker></defs>
<rect x="10" y="10" width="660" height="340" rx="8" fill="#e08a3c" fill-opacity="0.05" stroke="#e08a3c"/>
<text x="24" y="30" font-size="13" font-weight="bold">system bus (/run/dbus/system_bus_socket)</text>
<rect x="24" y="44" width="300" height="292" rx="6" fill="#3f9a6b" fill-opacity="0.06" stroke="#3f9a6b"/>
<text x="36" y="64" font-size="13" font-weight="bold">연결 :1.42 (PID 812, User=l3dev)</text>
<text x="36" y="84" font-size="12">well-known: com.example.L3.DeviceManager</text>
<rect x="36" y="96" width="276" height="28" rx="4" fill="#888" fill-opacity="0.12" stroke="#888"/>
<text x="48" y="115" font-size="12">path /com/example/L3/DeviceManager</text>
<rect x="48" y="134" width="264" height="104" rx="4" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
<text x="58" y="152" font-size="12" font-weight="bold">interface ...DeviceManager1</text>
<text x="58" y="172" font-size="12">method  SetMode(s) → b</text>
<text x="58" y="192" font-size="12">signal  ModeChanged(ss)</text>
<text x="58" y="212" font-size="12">property Mode : s (read)</text>
<text x="58" y="230" font-size="12">property Temperature : d (read)</text>
<rect x="48" y="246" width="264" height="36" rx="4" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
<text x="58" y="268" font-size="12">org.freedesktop.DBus.Properties</text>
<rect x="48" y="290" width="264" height="36" rx="4" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
<text x="58" y="312" font-size="12">org.freedesktop.DBus.Introspectable</text>
<rect x="346" y="44" width="310" height="292" rx="6" fill="#3f9a6b" fill-opacity="0.06" stroke="#3f9a6b"/>
<text x="358" y="64" font-size="13" font-weight="bold">연결 :1.3 (systemd-networkd)</text>
<text x="358" y="84" font-size="12">well-known: org.freedesktop.network1</text>
<rect x="358" y="96" width="286" height="28" rx="4" fill="#888" fill-opacity="0.12" stroke="#888"/>
<text x="370" y="115" font-size="12">/org/freedesktop/network1</text>
<line x1="380" y1="126" x2="380" y2="146" stroke="currentColor" stroke-width="1.2" marker-end="url(#ah2)"/>
<rect x="370" y="148" width="274" height="28" rx="4" fill="#888" fill-opacity="0.12" stroke="#888"/>
<text x="382" y="167" font-size="12">.../network1/link/_32  (ifindex 2)</text>
<line x1="380" y1="178" x2="380" y2="198" stroke="currentColor" stroke-width="1.2" marker-end="url(#ah2)"/>
<rect x="370" y="200" width="274" height="62" rx="4" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
<text x="382" y="218" font-size="12" font-weight="bold">org.freedesktop.network1.Link</text>
<text x="382" y="236" font-size="12">property OperationalState : s</text>
<text x="382" y="254" font-size="12">method Reconfigure()</text>
<text x="358" y="290" font-size="12">경로의 _32는 ifindex "2"를 이스케이프한 것</text>
<text x="358" y="310" font-size="12">(경로 원소는 숫자로 시작할 수 없다)</text>
</svg>
```
그림 2 — D-Bus 객체 모델. 버스 위의 연결이 이름을 갖고, 연결 안에 객체 경로 트리가 있으며, 각 객체가 인터페이스(멤버 묶음)를 구현한다. 메시지 주소는 "이름 + 경로 + 인터페이스 + 멤버"다.

### 1.6 함정

- 이름 규칙: bus name과 interface 이름은 점으로 나눈 원소 2개 이상, 원소는 숫자로 시작 불가, 최대 255자. object path 원소는 `[A-Za-z0-9_]`만 된다. 그래서 networkd는 ifindex 2를 `_32`(문자 '2'의 16진 코드 0x32)로 이스케이프한다.
- 인터페이스 이름에 버전 숫자를 붙이는 관례(`network1`, `DeviceManager1`)는 **API를 깨는 변경을 할 때 `2`를 새로 만들고 1을 유지**하기 위해서다. 펌웨어 커맨드 세트에 버전 필드를 두는 것과 같다.
- D-Bus 메시지 최대 크기는 128 MiB(사양), 하지만 버스 데몬이 연결별 메모리 한도를 건다. 큰 데이터는 fd(`h`)로.

---

## 2. 버스 데몬과 정책 — 누가 이름을 갖고, 누가 무엇을 보낼 수 있나

### 2.1 dbus-daemon vs dbus-broker

| | dbus-daemon | dbus-broker |
|---|---|---|
| 출신 | freedesktop 참조 구현 (libdbus와 같이) | Red Hat·systemd 개발자들이 새로 작성 |
| 구조 | 단일 프로세스, 정책·activation 직접 처리 | `dbus-broker-launch`(설정 파싱·activation을 systemd에 위임) + `dbus-broker`(배달 전용) |
| activation | 직접 `fork`/`exec` 가능 (`Exec=`), systemd에 위임도 가능 | **반드시 systemd 경유** (`SystemdService=`) |
| 성능·자원 | 충분히 빠름 | 더 빠르고 연결별 자원 회계가 엄격 |
| 쓰는 곳 | Debian/Ubuntu 기본, Yocto `dbus` 레시피, Buildroot | Fedora·RHEL·Arch 기본, Yocto `dbus-broker` 레시피 |
| 정책 파일 | `/usr/share/dbus-1/system.d/`, `/etc/dbus-1/system.d/` | 같은 XML 형식을 그대로 읽는다 |

클라이언트 입장에서는 프로토콜이 같아 차이가 없다. 실무에서 차이가 드러나는 곳은 activation(`Exec=`만 있고 `SystemdService=`가 없는 서비스 파일은 broker에서 안 뜬다)과 에러 메시지 문구다.

### 2.2 정책 파일

시스템 버스 설정의 뼈대는 `/usr/share/dbus-1/system.conf`다. 핵심은 **기본 거부** — 아무도 이름을 가질 수 없고(`<deny own="*"/>`), 아무도 메서드를 부를 수 없다(`<deny send_type="method_call"/>`). 시그널과 "요청받은 응답"만 기본 허용이다. 실제 파일 내용은 3.4절 예제 4에서 이 이미지의 것을 그대로 읽는다.

그래서 서비스마다 **정책 조각 파일**을 `system.d/`에 넣어 예외를 연다. 패키지가 설치하는 것은 `/usr/share/dbus-1/system.d/<name>.conf`, 관리자가 덮어쓰는 것은 `/etc/dbus-1/system.d/`. 규칙의 적용 순서는 `context="default"` → `group=` → `user=` → `context="mandatory"`이고, 같은 층에서는 나중 규칙이 이긴다.

| 규칙 | 뜻 |
|---|---|
| `<allow own="NAME"/>` | 이 이름을 RequestName 할 수 있다 |
| `<allow send_destination="NAME"/>` | 이 이름에게 메시지를 보낼 수 있다 |
| `send_interface=` `send_member=` `send_path=` | 보낼 수 있는 대상을 더 좁힌다 |
| `<allow receive_sender="NAME"/>` | 받는 쪽 제한 (거의 안 씀) |
| `<policy user="u">` `<policy group="g">` | 특정 사용자·그룹에만 적용 |

**"서비스가 이름을 못 가진다"는 증상의 원인 1순위는 정책 파일 누락**이다. 데몬을 새로 만들어 장비에 올렸는데 정책 파일을 패키지에 안 넣었거나, `User=`를 바꿨는데 정책의 `user=`를 안 바꾼 경우. 4절에서 직접 재현한다.

세밀한 권한(예: "운영자 그룹만 Wi-Fi 비밀번호를 바꿀 수 있다")은 정책 파일이 아니라 서비스 안에서 **polkit**에 묻는 방식으로 한다. 정책 파일은 "이 인터페이스로 말을 걸 수 있나"까지, polkit은 "이 동작을 해도 되나"다. NetworkManager·systemd·logind가 그렇게 한다. polkit이 없는 작은 장비라면 서비스가 `GetConnectionUnixUser`로 보낸 사람 UID를 데몬에게 물어 직접 판단한다.

### 2.3 activation — 처음 불릴 때 띄운다

`/usr/share/dbus-1/system-services/<name>.service`(INI 형식) 파일이 "이 이름으로 메시지가 오면 무엇을 띄울지"를 적는다.

```text
[D-BUS Service]
Name=org.freedesktop.network1
Exec=/bin/false
User=root
SystemdService=dbus-org.freedesktop.network1.service
```

버스 데몬은 소유자가 없는 이름으로 메시지가 오면 메시지를 붙잡아 두고, systemd에 `dbus-org.freedesktop.network1.service`를 시작하라고 요청한다(`Exec=/bin/false`는 "직접 띄우지 마라"는 관례). 이것은 systemd의 `Alias=`로 `systemd-networkd.service`에 연결돼 있다. 서비스가 이름을 획득하면 붙잡아 둔 메시지가 배달된다.

systemd 쪽에서는 서비스 유닛을 `Type=dbus` + `BusName=<name>`으로 적는다. 그러면 systemd는 **그 이름이 버스에 나타나는 순간을 "시작 완료"로 본다**(L1의 `Type=notify`처럼 준비 완료 신호를 받는 방식). `After=dbus.service`는 systemd가 자동으로 넣어 준다.

```svg
<svg viewBox="0 0 680 330" xmlns="http://www.w3.org/2000/svg">
<defs><marker id="ah3" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto-start-reverse"><path d="M0,0 L10,5 L0,10 z" fill="currentColor"/></marker></defs>
<rect x="10" y="40" width="130" height="230" rx="6" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
<text x="75" y="30" font-size="13" text-anchor="middle" font-weight="bold">클라이언트</text>
<text x="75" y="64" font-size="12" text-anchor="middle">:1.50 (uid 1001)</text>
<rect x="250" y="40" width="180" height="230" rx="6" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
<text x="340" y="30" font-size="13" text-anchor="middle" font-weight="bold">버스 데몬</text>
<rect x="262" y="76" width="156" height="58" rx="4" fill="#d0564a" fill-opacity="0.12" stroke="#d0564a"/>
<text x="340" y="96" font-size="12" text-anchor="middle">① 정책 검사</text>
<text x="340" y="114" font-size="12" text-anchor="middle">uid 1001 → 이 이름의</text>
<text x="340" y="128" font-size="12" text-anchor="middle">이 인터페이스로 send?</text>
<rect x="262" y="146" width="156" height="50" rx="4" fill="#888" fill-opacity="0.12" stroke="#888"/>
<text x="340" y="166" font-size="12" text-anchor="middle">② 이름 → unique 변환</text>
<text x="340" y="184" font-size="12" text-anchor="middle">소유자 없으면 activation</text>
<rect x="262" y="208" width="156" height="50" rx="4" fill="#888" fill-opacity="0.12" stroke="#888"/>
<text x="340" y="228" font-size="12" text-anchor="middle">③ 시그널: match rule</text>
<text x="340" y="246" font-size="12" text-anchor="middle">맞는 구독자에만 복사</text>
<rect x="540" y="40" width="130" height="230" rx="6" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
<text x="605" y="30" font-size="13" text-anchor="middle" font-weight="bold">서비스</text>
<text x="605" y="64" font-size="12" text-anchor="middle">:1.42 (l3dev)</text>
<line x1="142" y1="100" x2="258" y2="100" stroke="currentColor" stroke-width="1.3" marker-end="url(#ah3)"/>
<text x="146" y="92" font-size="12">METHOD_CALL</text>
<line x1="420" y1="170" x2="536" y2="170" stroke="currentColor" stroke-width="1.3" marker-end="url(#ah3)"/>
<text x="436" y="162" font-size="12">배달</text>
<line x1="536" y1="200" x2="422" y2="200" stroke="currentColor" stroke-width="1.3" marker-end="url(#ah3)"/>
<text x="434" y="194" font-size="12">RETURN · SIGNAL</text>
<line x1="258" y1="232" x2="144" y2="232" stroke="currentColor" stroke-width="1.3" marker-end="url(#ah3)"/>
<text x="148" y="224" font-size="12">RETURN · SIGNAL</text>
<line x1="258" y1="124" x2="144" y2="150" stroke="#d0564a" stroke-width="1.3" marker-end="url(#ah3)"/>
<text x="148" y="166" font-size="12">ERROR</text>
<text x="148" y="182" font-size="12">AccessDenied</text>
<text x="340" y="296" font-size="12" text-anchor="middle">보낸 쪽 uid·pid는 데몬이 SO_PEERCRED로 커널에서 얻는다 (위조 불가)</text>
<text x="340" y="316" font-size="12" text-anchor="middle">거부된 메시지는 서비스까지 가지 않는다 — 서비스 로그에는 아무것도 없다</text>
</svg>
```
그림 3 — 메시지 한 통의 여정. 데몬이 정책을 먼저 검사하고(거부되면 데몬이 직접 ERROR를 돌려준다), 이름을 소유자 unique name으로 바꾸고, 필요하면 activation을 건다. 시그널은 match rule이 맞는 구독자에게만 복사된다.

함정: 정책 거부는 **서비스가 아닌 버스 데몬이** 돌려준다. 그래서 서비스 로그(`journalctl -u`)를 아무리 봐도 흔적이 없다. 데몬 쪽 로그(`journalctl -u dbus`)나 `busctl monitor`에서 보인다.

---

## 3. `busctl`로 실물 보기 — systemd와 networkd의 D-Bus API

`busctl`은 systemd에 들어 있는 D-Bus 만능 도구다(sd-bus 기반). 이름 목록 → 객체 트리 → 인터페이스 → 호출 순서로 내려간다. 옛 도구 `dbus-send`/`dbus-monitor`(libdbus 기반)도 거의 모든 장비에 있으니 둘 다 읽을 줄 알아야 한다. 게스트에서는 `SYSTEMD_PAGER=cat SYSTEMD_COLORS=0`으로 페이저와 색을 껐다.

### 3.1 예제 1 — 버스 위에 누가 있나 (`busctl list`)

무엇을 확인하나: 시스템 버스의 모든 이름(unique + well-known)과, 아직 안 떠 있지만 activation 가능한 이름.

```sh
busctl list --no-pager | cut -c1-110
```

```text
NAME                           PID PROCESS         USER             CONNECTION    UNIT                      SE
:1.0                           156 systemd-network systemd-network  :1.0          systemd-networkd.service  - 
:1.1                           168 systemd-resolve systemd-resolve  :1.1          systemd-resolved.service  - 
:1.2                           169 systemd-timesyn systemd-timesync :1.2          systemd-timesyncd.service - 
:1.3                             1 systemd         root             :1.3          init.scope                - 
:1.4                           284 systemd-logind  root             :1.4          systemd-logind.service    - 
:1.7                           343 systemd         root             :1.7          user@0.service            - 
:1.9                           465 busctl          root             :1.9          session-1.scope           1 
io.netplan.Netplan               - -               -                (activatable) -                         - 
org.freedesktop.DBus             1 systemd         root             -             init.scope                - 
org.freedesktop.PolicyKit1       - -               -                (activatable) -                         - 
org.freedesktop.hostname1        - -               -                (activatable) -                         - 
org.freedesktop.import1          - -               -                (activatable) -                         - 
org.freedesktop.locale1          - -               -                (activatable) -                         - 
org.freedesktop.login1         284 systemd-logind  root             :1.4          systemd-logind.service    - 
org.freedesktop.machine1         - -               -                (activatable) -                         - 
org.freedesktop.network1       156 systemd-network systemd-network  :1.0          systemd-networkd.service  - 
org.freedesktop.nm_dispatcher    - -               -                (activatable) -                         - 
org.freedesktop.nm_priv_helper   - -               -                (activatable) -                         - 
org.freedesktop.portable1        - -               -                (activatable) -                         - 
org.freedesktop.resolve1       168 systemd-resolve systemd-resolve  :1.1          systemd-resolved.service  - 
org.freedesktop.systemd1         1 systemd         root             :1.3          init.scope                - 
org.freedesktop.timedate1        - -               -                (activatable) -                         - 
org.freedesktop.timesync1      169 systemd-timesyn systemd-timesync :1.2          systemd-timesyncd.service - 
```

출력에서 볼 것:

- `org.freedesktop.network1`의 CONNECTION이 `:1.0`이다. well-known name은 unique name의 **별명**이다. PID·사용자·유닛까지 데몬이 커널에서 얻어 보여 준다.
- `(activatable)` 줄: hostname1, timedate1, locale1은 평소에 떠 있지 않다. `hostnamectl`을 치는 순간 activation으로 떴다가 일정 시간 놀면 스스로 종료한다. 메모리가 빠듯한 장비에 좋은 패턴이다.
- `org.freedesktop.DBus` 자체가 PID 1로 보인다. 버스 데몬의 소켓을 systemd가 만들어(`dbus.socket`, 소켓 activation) 넘겨줬기 때문이다. 실제 데몬은 따로 있다:

```sh
ps -o pid,user,args -C dbus-daemon
ls -l /run/dbus/system_bus_socket
systemctl status dbus.socket --no-pager | head -5
```

```text
    PID USER     COMMAND
    278 message+ @dbus-daemon --system --address=systemd: --nofork --nopidfile --systemd-activation --syslog-only
srw-rw-rw- 1 root root 0 Oct 18 04:28 /run/dbus/system_bus_socket
● dbus.socket - D-Bus System Message Bus Socket
     Loaded: loaded (/usr/lib/systemd/system/dbus.socket; static)
     Active: active (running) since Sun 2026-10-18 04:28:24 UTC; 1min 25s ago
   Triggers: ● dbus.service
     Listen: /run/dbus/system_bus_socket (Stream)
```

이 이미지(Ubuntu 24.04)는 **dbus-daemon 1.14.10**을 쓴다(`dbus-broker`는 inactive). `--address=systemd:`는 "systemd가 준 소켓 fd를 받아 쓴다"는 뜻이고, 데몬은 `messagebus` 사용자로 돈다. 소켓 권한이 `srw-rw-rw-`인 것에 주목: **누구나 접속은 된다**. 막는 것은 소켓 권한이 아니라 2.2절의 정책이다.

### 3.2 예제 2 — 객체 트리와 인터페이스 (`busctl tree`, `introspect`)

무엇을 확인하나: networkd가 내보내는 객체 경로와, 링크 객체 하나의 메서드·프로퍼티.

```sh
busctl tree org.freedesktop.network1 --no-pager
busctl introspect org.freedesktop.network1 /org/freedesktop/network1/link/_32 --no-pager | cut -c1-120 | head -40
```

```text
└─ /org
  └─ /org/freedesktop
    ├─ /org/freedesktop/LogControl1
    └─ /org/freedesktop/network1
      ├─ /org/freedesktop/network1/link
      │ ├─ /org/freedesktop/network1/link/_31
      │ ├─ /org/freedesktop/network1/link/_32
      │ └─ /org/freedesktop/network1/link/_33
      └─ /org/freedesktop/network1/network
        ├─ /org/freedesktop/network1/network/_320_2dwired
        ├─ /org/freedesktop/network1/network/_380_2d6rd_2dtunnel
        ├─ /org/freedesktop/network1/network/_380_2dcontainer_2dvb
        ├─ /org/freedesktop/network1/network/_380_2dcontainer_2dve
        ├─ /org/freedesktop/network1/network/_380_2dcontainer_2dvz
        ├─ /org/freedesktop/network1/network/_380_2dvm_2dvt
        └─ /org/freedesktop/network1/network/_380_2dwifi_2dadhoc
NAME                                  TYPE      SIGNATURE RESULT/VALUE                              FLAGS
org.freedesktop.DBus.Introspectable   interface -         -                                         -
.Introspect                           method    -         s                                         -
org.freedesktop.DBus.Peer             interface -         -                                         -
.GetMachineId                         method    -         s                                         -
.Ping                                 method    -         -                                         -
org.freedesktop.DBus.Properties       interface -         -                                         -
.Get                                  method    ss        v                                         -
.GetAll                               method    s         a{sv}                                     -
.Set                                  method    ssv       -                                         -
.PropertiesChanged                    signal    sa{sv}as  -                                         -
org.freedesktop.network1.DHCPv4Client interface -         -                                         -
.State                                property  s         "bound"                                   emits-change
org.freedesktop.network1.Link         interface -         -                                         -
.Describe                             method    -         s                                         -
.ForceRenew                           method    -         -                                         -
.Reconfigure                          method    -         -                                         -
.Renew                                method    -         -                                         -
...
.AddressState                         property  s         "routable"                                emits-change
.AdministrativeState                  property  s         "configured"                              emits-change
.BitRates                             property  (tt)      18446744073709551615 18446744073709551615 -
.CarrierState                         property  s         "carrier"                                 emits-change
.IPv4AddressState                     property  s         "routable"                                emits-change
.IPv6AddressState                     property  s         "off"                                     emits-change
.OnlineState                          property  s         "online"                                  emits-change
.OperationalState                     property  s         "routable"                                emits-change
```

(`RevertDNS`부터 `SetNTP`까지 메서드 13줄은 `...`로 생략했다.) 출력에서 볼 것:

- `link/_31`, `_32`, `_33` = ifindex 1(lo), 2(enp0s1), 3(enp0s2). `_32`는 문자 `'2'`(0x32)의 이스케이프다. `network/_320_2dwired`는 `20-wired`(`'2'`=0x32, `'-'`=0x2d)라는 `.network` 파일 이름이다.
- 프로퍼티의 `emits-change` 플래그: 이 값이 바뀌면 `PropertiesChanged` 시그널이 나온다. 그러니 **링크 상태를 폴링하지 말고 시그널을 구독**하면 된다. 웹 UI가 "링크 끊김"을 즉시 보여 주는 방법이 이것이다.
- 생략한 줄 중 `SetDNS a(iay)`: (주소 패밀리 int, 주소 바이트 배열)의 배열. 시그니처만 보고 C 구조를 떠올릴 수 있어야 한다.
- `BitRates (tt)`의 `18446744073709551615`는 `UINT64_MAX` = "모름"이다(virtio NIC는 속도가 없다).

### 3.3 예제 3 — 메서드 호출과 프로퍼티 읽기 (`call`, `get-property`)

무엇을 확인하나: systemd에게 유닛 객체 경로를 물어 그 상태를 읽고, networkd에게 링크 설명(JSON)을 받는다. `systemctl is-active`와 `networkctl status`가 내부에서 하는 일이다.

```sh
busctl call org.freedesktop.systemd1 /org/freedesktop/systemd1 \
  org.freedesktop.systemd1.Manager GetUnit s systemd-networkd.service
busctl get-property org.freedesktop.systemd1 \
  /org/freedesktop/systemd1/unit/systemd_2dnetworkd_2eservice \
  org.freedesktop.systemd1.Unit ActiveState SubState
busctl get-property org.freedesktop.systemd1 /org/freedesktop/systemd1 \
  org.freedesktop.systemd1.Manager Version NNames
busctl call org.freedesktop.network1 /org/freedesktop/network1/link/_32 \
  org.freedesktop.network1.Link Describe | cut -c1-300
busctl --json=short call org.freedesktop.network1 /org/freedesktop/network1/link/_32 \
  org.freedesktop.network1.Link Describe | jq -r '.data[0]' \
  | jq -c '{Name,OperationalState,Addresses:[.Addresses[]?|{a:(.Address|map(tostring)|join(".")),p:.PrefixLength}]}'
```

```text
o "/org/freedesktop/systemd1/unit/systemd_2dnetworkd_2eservice"
s "active"
s "running"
s "255.4-1ubuntu8.18"
u 262
s "{\"Index\":2,\"Name\":\"enp0s1\",\"Type\":\"ether\",\"Driver\":\"virtio_net\",\"Flags\":69699,\"FlagsString\":\"up,broadcast,running,multicast,lower-up\",\"KernelOperationalState\":6,\"KernelOperationalStateString\":\"up\",\"MTU\":1500,\"MinimumMTU\":68,\"MaximumMTU\":65535,\"HardwareAddress\":[8
{"Name":"enp0s1","OperationalState":"routable","Addresses":[{"a":"10.0.2.15","p":24}]}
```

출력에서 볼 것: 각 줄 앞의 문자(`o`, `s`, `u`)가 1.4절의 타입 시그니처다. `busctl call`의 인자는 **시그니처 다음에 값**(`s systemd-networkd.service`)이다. `--json=short`를 쓰면 스크립트에서 `jq`로 다루기 쉽다. 장비의 헬스체크 스크립트가 `ip`/`networkctl` 출력을 grep하는 대신 이렇게 D-Bus JSON을 읽으면 출력 형식 변경에 깨지지 않는다.

같은 프로퍼티를 옛 도구로 읽으면 (예제 4의 앞부분):

```sh
dbus-send --system --print-reply --dest=org.freedesktop.systemd1 \
  /org/freedesktop/systemd1/unit/systemd_2dtimesyncd_2eservice \
  org.freedesktop.DBus.Properties.Get \
  string:org.freedesktop.systemd1.Unit string:ActiveState
```

```text
method return time=1792297810.493964 sender=:1.3 -> destination=:1.17 serial=625 reply_serial=2
   variant       string "active"
```

`reply_serial=2`가 요청의 serial을 가리킨다 — 요청과 응답을 묶는 태그다(NVMe의 Command ID와 같다). `dbus-send`는 Buildroot의 기본 `dbus` 패키지에도 들어 있어서, systemd가 없는 BusyBox 장비에서도 쓸 수 있다.

### 3.4 예제 4 — 정책 파일을 실제로 읽기

무엇을 확인하나: 2.2절의 "기본 거부"가 이 이미지에 정말 그렇게 적혀 있는지, networkd가 어떤 구멍을 뚫는지, activation 파일이 어떻게 생겼는지.

```sh
grep -v '^ *$' /usr/share/dbus-1/system.conf | sed -n '/<policy context="default">/,/<\/policy>/p'
grep -n includedir /usr/share/dbus-1/system.conf
sed -n '/<busconfig>/,$p' /usr/share/dbus-1/system.d/org.freedesktop.network1.conf | grep -v '^ *$'
cat /usr/share/dbus-1/system-services/org.freedesktop.network1.service
```

```text
  <policy context="default">
    <!-- All users can connect to system bus -->
    <allow user="*"/>
    <!-- Holes must be punched in service configuration files for
         name ownership and sending method calls -->
    <deny own="*"/>
    <deny send_type="method_call"/>
    <!-- Signals and reply messages (method returns, errors) are allowed
         by default -->
    <allow send_type="signal"/>
    <allow send_requested_reply="true" send_type="method_return"/>
    <allow send_requested_reply="true" send_type="error"/>
    <!-- All messages may be received by default -->
    <allow receive_type="method_call"/>
    <allow receive_type="method_return"/>
    <allow receive_type="error"/>
    <allow receive_type="signal"/>
    <!-- Allow anyone to talk to the message bus -->
    <allow send_destination="org.freedesktop.DBus"
           send_interface="org.freedesktop.DBus" />
    <allow send_destination="org.freedesktop.DBus"
           send_interface="org.freedesktop.DBus.Introspectable"/>
    <allow send_destination="org.freedesktop.DBus"
           send_interface="org.freedesktop.DBus.Properties"/>
    <allow send_destination="org.freedesktop.DBus"
           send_interface="org.freedesktop.DBus.Containers1"/>
    <!-- But disallow some specific bus services -->
    <deny send_destination="org.freedesktop.DBus"
          send_interface="org.freedesktop.DBus"
          send_member="UpdateActivationEnvironment"/>
    <deny send_destination="org.freedesktop.DBus"
          send_interface="org.freedesktop.DBus.Debug.Stats"/>
    <deny send_destination="org.freedesktop.DBus"
          send_interface="org.freedesktop.systemd1.Activator"/>
  </policy>
132:  <includedir>system.d</includedir>
134:  <includedir>/etc/dbus-1/system.d</includedir>
<busconfig>
        <policy user="systemd-network">
                <allow own="org.freedesktop.network1"/>
                <allow send_destination="org.freedesktop.network1"/>
                <allow receive_sender="org.freedesktop.network1"/>
        </policy>
        <policy context="default">
                <allow send_destination="org.freedesktop.network1"/>
                <allow receive_sender="org.freedesktop.network1"/>
        </policy>
</busconfig>
#  SPDX-License-Identifier: LGPL-2.1-or-later
#
#  This file is part of systemd.
#
#  systemd is free software; you can redistribute it and/or modify it
#  under the terms of the GNU Lesser General Public License as published by
#  the Free Software Foundation; either version 2.1 of the License, or
#  (at your option) any later version.

[D-BUS Service]
Name=org.freedesktop.network1
Exec=/bin/false
User=root
SystemdService=dbus-org.freedesktop.network1.service
```

출력에서 볼 것: 주석 그대로 "Holes must be punched in service configuration files"다. networkd의 정책은 **이름 소유는 `systemd-network` 사용자만**, 메시지 보내기는 누구나 허용한다. 그럼 아무나 `SetDNS`를 부를 수 있나? 아니다 — 정책은 통과시키고, networkd 안에서 **polkit**으로 권한을 검사한다(2.2절). 정책 파일과 서비스 내부 검사, 두 층이다.

### 3.5 예제 5 — 유닛 재시작을 버스에서 지켜보기 (`busctl monitor`)

무엇을 확인하나: 유닛 하나를 재시작할 때 어떤 메시지들이 버스를 지나가는지. 원본 출력은 메시지 하나가 수십 줄이라, 게스트에 넣은 작은 awk 스크립트(`monsum.sh`, 본문 아래)로 한 줄 요약했다.

```sh
#!/bin/sh
# monsum.sh FILE : busctl monitor 출력을 한 줄 요약으로
awk '/^(‣|>|<) Type=/{t=$2; sub("Type=","",t); c=$0; sub(/.*Cookie=/,"",c); sub(/ .*/,"",c)}
/^  Sender=/{p="-";i="";m="";d="";s=""
  for(k=1;k<=NF;k++){n=index($k,"="); key=substr($k,1,n-1); v=substr($k,n+1)
    if(key=="Sender")s=v; if(key=="Destination")d=v; if(key=="Path")p=v
    if(key=="Interface")i=v; if(key=="Member")m=v; if(key=="ReplyCookie")m="reply_to=" v}
  sub("/org/freedesktop/systemd1","…",p); sub("org.freedesktop.","",i)
  printf "%-6s %-14s %-6s→%-6s %-36s %s\n", c, t, s, (d==""?"*":d), (i==""?m:i "." m), p}' "$1"
```

먼저 `systemctl restart`로 해 봤다:

```sh
(timeout 15 busctl monitor org.freedesktop.systemd1 > /tmp/mon.txt 2>&1 &); sleep 3
systemctl restart systemd-timesyncd; sleep 8
awk '/^(‣|>|<) Type=/{t=$2} /^  Sender=/{for(k=1;k<=NF;k++){split($k,a,"="); if(a[1]=="Member")m=a[2]}; print t, m}' /tmp/mon.txt | sort | uniq -c | sort -rn
```

```text
     31 Type=signal PropertiesChanged
      1 Type=signal JobRemoved
      1 Type=signal JobNew
```

**메서드 호출이 하나도 없다.** 시그널만 33개다. root의 `systemctl`은 버스를 거치지 않고 systemd의 **사설 소켓**으로 직접 말하기 때문이다:

```sh
strace -f -e trace=connect systemctl restart systemd-timesyncd 2>&1 | grep -o 'connect(.*sun_path=[^}]*' | sort -u
```

```text
connect(3, {sa_family=AF_UNIX, sun_path="/run/systemd/private"
```

`/run/systemd/private`는 root 전용 피어-투-피어 D-Bus 연결이다(버스 데몬 없이 D-Bus 프로토콜만 쓴다). 그래서 **dbus-daemon이 죽어도 root는 `systemctl`로 시스템을 복구할 수 있다** — 설계 의도다. 이번에는 버스를 거치는 `busctl call`로 재시작한다:

```sh
(timeout 12 busctl monitor org.freedesktop.systemd1 > /tmp/mon2.txt 2>&1 &); sleep 2
busctl call org.freedesktop.systemd1 /org/freedesktop/systemd1 \
  org.freedesktop.systemd1.Manager RestartUnit ss systemd-timesyncd.service replace
sleep 8; sh /root/l3/monsum.sh /tmp/mon2.txt
```

```text
o "/org/freedesktop/systemd1/job/1035"
2      method_call    :1.21 →org.freedesktop.systemd1 systemd1.Manager.RestartUnit         …
659    method_call    :1.3  →org.freedesktop.DBus DBus.GetConnectionUnixUser           /org/freedesktop/DBus
659    method_return  org.freedesktop.DBus→:1.3                                        -
660    method_call    :1.3  →org.freedesktop.DBus DBus.AddMatch                        /org/freedesktop/DBus
660    method_return  org.freedesktop.DBus→:1.3                                        -
661    method_call    :1.3  →org.freedesktop.DBus DBus.GetNameOwner                    /org/freedesktop/DBus
661    method_return  org.freedesktop.DBus→:1.3                                        -
662    signal         :1.3  →*      DBus.Properties.PropertiesChanged    …/unit/systemd_2dtimesyncd_2eservice
663    signal         :1.3  →*      DBus.Properties.PropertiesChanged    …/unit/systemd_2dtimesyncd_2eservice
664    signal         :1.3  →*      systemd1.Manager.JobNew              …
2      method_return  :1.3  →:1.21                                       -
666    signal         :1.3  →*      DBus.Properties.PropertiesChanged    …/unit/systemd_2dtimesyncd_2eservice
...
688    signal         :1.3  →*      systemd1.Manager.JobRemoved          …
...
```

(뒤쪽 `PropertiesChanged` 줄 수십 개는 `...`로 생략했다.) 출력에서 볼 것 — 이것이 D-Bus 서비스가 요청을 처리하는 전형적인 순서다:

1. 클라이언트 `:1.21`이 `RestartUnit`을 보낸다(cookie 2).
2. systemd(`:1.3`)가 버스 데몬에 **`GetConnectionUnixUser`로 보낸 사람의 UID를 묻는다**. root이면 통과, 아니면 polkit에 묻는다. 서비스 쪽 권한 검사가 여기서 일어난다.
3. `AddMatch`/`GetNameOwner`: 클라이언트가 사라지면 알 수 있게 감시를 건다.
4. 잡을 만들고(`JobNew`) 곧바로 **응답을 먼저 돌려준다**(`method_return` reply to 2, 결과는 잡 경로 `o`). 재시작이 끝나기를 기다리지 않는다 — 비동기 명령의 "접수 완료" 응답이다.
5. 진행 상황은 `PropertiesChanged` 시그널로, 완료는 `JobRemoved` 시그널로 알린다. `systemctl`은 `JobRemoved`를 기다렸다가 결과를 출력한다.

펌웨어로 치면 "명령 접수 응답 → 진행률 이벤트 → 완료 이벤트" 3단 프로토콜이다. 오래 걸리는 동작(펌웨어 업데이트, 캘리브레이션)을 D-Bus로 노출할 때 이 패턴을 그대로 따라 하면 된다(4.6절).

---

## 4. 내 D-Bus 서비스 만들기 — 센서 키트 "device manager"

### 4.1 무엇을 만드나

센서 키트의 `l3-devmgr` 데몬: 카메라·IMU 파이프라인 모드를 `idle`/`capture`/`calib` 중 하나로 바꾸는 **메서드** `SetMode(s) → b`, 현재 모드와 온도를 보여 주는 **프로퍼티** `Mode`, `Temperature`, 모드가 바뀔 때 알리는 **시그널** `ModeChanged(ss)` + 표준 `PropertiesChanged`. 권한: 읽기는 누구나, 모드 변경은 root와 `l3ops` 그룹만. 서비스는 전용 계정 `l3dev`로 돌고, 첫 호출 때 D-Bus activation으로 뜬다.

필요한 파일 5개:

| 파일 | 위치 | 역할 |
|---|---|---|
| `l3-devmgr.py` | `/usr/local/bin/` | 서비스 본체 |
| `l3-devmgr.service` | `/etc/systemd/system/` | systemd 유닛 (`Type=dbus`) |
| `com.example.L3.DeviceManager.conf` | `/etc/dbus-1/system.d/` | 버스 정책 |
| `com.example.L3.DeviceManager.service` | `/usr/share/dbus-1/system-services/` | activation 파일 |
| `l3-client.py` | `/usr/local/bin/` | 테스트 클라이언트 |

### 4.2 예제 6 — 서비스 본체 (python3-dbus)

무엇을 확인하는 코드인가: dbus-python으로 객체 하나를 시스템 버스에 내보내고, 메서드·시그널·프로퍼티를 구현한다.

한 가지 실무 사정: 이 이미지에는 `python3-dbus`는 있지만 **`python3-gi`(GLib 바인딩)가 없다**. dbus-python의 메인 루프 연동(`DBusGMainLoop`)은 GLib 메인 루프가 돌아야 콜백을 부르므로, `libglib-2.0.so.0`(이미 설치돼 있다)를 `ctypes`로 직접 불러 루프를 돌렸다. 장비 이미지에 패키지 하나가 빠져 있을 때 흔히 쓰는 우회다. `python3-gi`가 있으면 `from gi.repository import GLib; GLib.MainLoop().run()`이 정석이다.

```python
#!/usr/bin/python3
# l3-devmgr: 센서 키트 "device manager" — 시스템 버스에 메서드·프로퍼티·시그널을 노출
import ctypes, dbus, dbus.service, dbus.mainloop.glib
# 이미지에 python3-gi가 없어서 GLib 메인 루프를 ctypes로 직접 돌린다
glib = ctypes.CDLL("libglib-2.0.so.0")
glib.g_main_loop_new.restype = ctypes.c_void_p
glib.g_main_loop_run.argtypes = [ctypes.c_void_p]

BUS   = "com.example.L3.DeviceManager"
PATH  = "/com/example/L3/DeviceManager"
IFACE = "com.example.L3.DeviceManager1"
MODES = ("idle", "capture", "calib")

class DevMgr(dbus.service.Object):
    def __init__(self, conn):
        super().__init__(conn, PATH)
        self.mode, self.temp = "idle", 41.5

    @dbus.service.method(IFACE, in_signature="s", out_signature="b")
    def SetMode(self, mode):
        if mode not in MODES:
            raise dbus.exceptions.DBusException(
                "unknown mode: " + mode, name=IFACE + ".Error.InvalidMode")
        old, self.mode = self.mode, mode
        print(f"SetMode {old} -> {mode}", flush=True)      # → journald
        self.ModeChanged(old, mode)                        # 우리 시그널
        self.PropertiesChanged(IFACE, {"Mode": mode}, [])  # 표준 시그널
        return True

    @dbus.service.signal(IFACE, signature="ss")
    def ModeChanged(self, old, new):
        pass

    # --- org.freedesktop.DBus.Properties (dbus-python은 직접 구현해야 한다)
    @dbus.service.method(dbus.PROPERTIES_IFACE, in_signature="ss", out_signature="v")
    def Get(self, iface, prop):
        return self.GetAll(iface)[prop]

    @dbus.service.method(dbus.PROPERTIES_IFACE, in_signature="s", out_signature="a{sv}")
    def GetAll(self, iface):
        if iface != IFACE:
            raise dbus.exceptions.DBusException(
                iface, name="org.freedesktop.DBus.Error.UnknownInterface")
        return {"Mode": dbus.String(self.mode),
                "Temperature": dbus.Double(self.temp)}

    @dbus.service.signal(dbus.PROPERTIES_IFACE, signature="sa{sv}as")
    def PropertiesChanged(self, iface, changed, invalidated):
        pass

dbus.mainloop.glib.DBusGMainLoop(set_as_default=True)
bus = dbus.SystemBus()
name = dbus.service.BusName(BUS, bus, do_not_queue=True)  # 여기서 RequestName
obj = DevMgr(bus)
print("l3-devmgr: owning", BUS, "as", bus.get_unique_name(), flush=True)
glib.g_main_loop_run(glib.g_main_loop_new(None, False))
```

포인트:

- 메서드 데코레이터의 `in_signature`/`out_signature`가 1.4절 시그니처다. 에러는 **이름 있는 D-Bus 에러**(`...Error.InvalidMode`)로 던진다. 클라이언트가 문자열 파싱 없이 에러 종류로 분기할 수 있다(펌웨어의 status code 표와 같다).
- 시그널 메서드는 몸체가 비어 있다. 부르는 순간 데코레이터가 메시지를 만들어 보낸다.
- `BusName(..., do_not_queue=True)`가 `RequestName`이다. 이름을 못 얻으면 큐에서 기다리지 말고 바로 실패하라는 뜻이다 — systemd가 실패를 감지하게 하려면 이게 맞다.
- dbus-python은 프로퍼티를 기본 제공하지 않아서 `Properties` 인터페이스를 직접 구현했다. 그 결과 **Introspect XML에 `<property>`가 안 나온다**(아래 4.5절). sd-bus(`SD_BUS_PROPERTY`)·GDBus·sdbus-c++는 프로퍼티를 vtable로 선언하므로 XML에 나오고 `PropertiesChanged`도 라이브러리가 대신 보낸다. 제품 코드라면 C/C++에서는 sd-bus나 sdbus-c++, Python에서는 `dbus-next`/`dasbus` 같은 최신 라이브러리를 고려하자.

유닛 파일:

```text
[Unit]
Description=L3 sensor-kit device manager (D-Bus)

[Service]
Type=dbus
BusName=com.example.L3.DeviceManager
ExecStart=/usr/local/bin/l3-devmgr.py
User=l3dev
Environment=PYTHONUNBUFFERED=1
```

`Type=dbus`이므로 systemd는 `BusName`이 버스에 나타나야 "started"로 본다(L1). `[Install]`이 없다 — 부팅 때 띄우지 않고 activation으로만 띄울 생각이다.

### 4.3 예제 7 — 정책 파일 없이 띄우면: AccessDenied

무엇을 확인하나: 2.2절에서 말한 "원인 1순위"를 직접 재현한다. 계정을 만들고 유닛만 설치한 뒤 시작한다.

```sh
useradd -r -s /usr/sbin/nologin l3dev; groupadd l3ops
useradd -m -G l3ops op1; useradd -m op2
install -m755 /root/l3/l3-devmgr.py /root/l3/l3-client.py /usr/local/bin/
install -m644 /root/l3/l3-devmgr.service /etc/systemd/system/; systemctl daemon-reload
id l3dev; id op1; id op2
systemctl start l3-devmgr; echo "start rc=$?"
journalctl -u l3-devmgr --no-pager -o cat --since=-20s | tail -12
```

```text
uid=995(l3dev) gid=987(l3dev) groups=987(l3dev)
uid=1000(op1) gid=1001(op1) groups=1001(op1),1000(l3ops)
uid=1001(op2) gid=1002(op2) groups=1002(op2)
Job for l3-devmgr.service failed because the control process exited with error code.
See "systemctl status l3-devmgr.service" and "journalctl -xeu l3-devmgr.service" for details.
start rc=1
             ^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^
  File "/usr/lib/python3/dist-packages/dbus/bus.py", line 299, in request_name
    return self.call_blocking(BUS_DAEMON_NAME, BUS_DAEMON_PATH,
           ^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^
  File "/usr/lib/python3/dist-packages/dbus/connection.py", line 634, in call_blocking
    reply_message = self.send_message_with_reply_and_block(
                    ^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^
dbus.exceptions.DBusException: org.freedesktop.DBus.Error.AccessDenied: Connection ":1.24" is not allowed to own the service "com.example.L3.DeviceManager" due to security policies in the configuration file
l3-devmgr.service: Main process exited, code=exited, status=1/FAILURE
l3-devmgr.service: Failed with result 'exit-code'.
Failed to start l3-devmgr.service - L3 sensor-kit device manager (D-Bus).
l3-devmgr.service: Consumed 1.481s CPU time.
```

root로 직접 띄워도 같다:

```sh
timeout 5 python3 /usr/local/bin/l3-devmgr.py 2>&1 | tail -1 | cut -c1-200
```

```text
dbus.exceptions.DBusException: org.freedesktop.DBus.Error.AccessDenied: Connection ":1.25" is not allowed to own the service "com.example.L3.DeviceManager" due to security policies in the configuration file
```

출력에서 볼 것: `RequestName`이 `AccessDenied`로 거부됐다 — **root도 예외가 아니다**(`<deny own="*"/>`가 모두에게 적용된다). "root로 돌리니까 되겠지"는 시스템 버스에서 통하지 않는다. 메시지 문구 "due to security policies in the configuration file"을 보면 바로 정책 파일을 의심하자.

### 4.4 예제 8 — 정책을 넣고 권한별로 호출하기

무엇을 확인하나: 4.1절의 권한 설계(읽기는 모두, 쓰기는 root·l3ops)를 정책 파일로 표현하고, 사용자 셋으로 시험한다.

```text
<!DOCTYPE busconfig PUBLIC "-//freedesktop//DTD D-BUS Bus Configuration 1.0//EN"
 "http://www.freedesktop.org/standards/dbus/1.0/busconfig.dtd">
<busconfig>
  <!-- 이름 소유: 서비스 계정 l3dev만 -->
  <policy user="l3dev">
    <allow own="com.example.L3.DeviceManager"/>
  </policy>
  <!-- 누구나: 읽기(Introspect, Properties) -->
  <policy context="default">
    <allow send_destination="com.example.L3.DeviceManager"
           send_interface="org.freedesktop.DBus.Introspectable"/>
    <allow send_destination="com.example.L3.DeviceManager"
           send_interface="org.freedesktop.DBus.Properties"/>
  </policy>
  <!-- 제어(SetMode): root와 l3ops 그룹만 -->
  <policy user="root">
    <allow send_destination="com.example.L3.DeviceManager"/>
  </policy>
  <policy group="l3ops">
    <allow send_destination="com.example.L3.DeviceManager"
           send_interface="com.example.L3.DeviceManager1"/>
  </policy>
</busconfig>
```

```sh
install -m644 /root/l3/com.example.L3.DeviceManager.conf /etc/dbus-1/system.d/
sleep 2; journalctl -u dbus --no-pager -o cat | tail -3
systemctl start l3-devmgr; echo "start rc=$?"
systemctl status l3-devmgr --no-pager | sed -n '1,9p'
busctl list --no-pager | grep -E '^NAME|L3' | cut -c1-120
```

```text
[system] Monitoring connection :1.20 closed.
Unknown username "whoopsie" in message bus configuration file
[system] Reloaded configuration
start rc=0
● l3-devmgr.service - L3 sensor-kit device manager (D-Bus)
     Loaded: loaded (/etc/systemd/system/l3-devmgr.service; static)
     Active: active (running) since Sun 2026-10-18 04:32:39 UTC; 232ms ago
   Main PID: 663 (l3-devmgr.py)
      Tasks: 1 (limit: 1165)
     Memory: 7.2M (peak: 7.2M)
        CPU: 775ms
     CGroup: /system.slice/l3-devmgr.service
             └─663 /usr/bin/python3 /usr/local/bin/l3-devmgr.py
NAME                           PID PROCESS         USER             CONNECTION    UNIT                      SESSION DESC
com.example.L3.DeviceManager   663 l3-devmgr.py    l3dev            :1.26         l3-devmgr.service         -       -
```

출력에서 볼 것: dbus-daemon은 `system.d/` 디렉터리를 inotify로 감시해서 **파일을 넣기만 해도 설정을 다시 읽는다**("Reloaded configuration"). 확실히 하려면 `busctl call org.freedesktop.DBus / org.freedesktop.DBus ReloadConfig` 또는 `systemctl reload dbus`. 그 위의 "Unknown username whoopsie"는 다른 패키지 정책 파일이 이 이미지에 없는 사용자를 가리킨다는 경고로, 무시해도 된다(이런 경고가 쌓인 장비 로그는 흔하다). 이제 이름이 `l3dev` 사용자 소유로 올라왔다.

클라이언트 (dbus-python):

```python
#!/usr/bin/python3
# l3-client: 메서드 호출 + 프로퍼티 읽기 + 시그널 구독
import sys, time, ctypes, dbus, dbus.mainloop.glib
glib = ctypes.CDLL("libglib-2.0.so.0")      # python3-gi 대신

BUS, PATH = "com.example.L3.DeviceManager", "/com/example/L3/DeviceManager"
IFACE = "com.example.L3.DeviceManager1"

dbus.mainloop.glib.DBusGMainLoop(set_as_default=True)
bus = dbus.SystemBus()
proxy = bus.get_object(BUS, PATH)            # 필요하면 여기서 activation
dev = dbus.Interface(proxy, IFACE)
props = dbus.Interface(proxy, dbus.PROPERTIES_IFACE)

def on_mode(old, new, sender=None):
    print(f"signal ModeChanged {old} -> {new} from {sender}")

bus.add_signal_receiver(on_mode, signal_name="ModeChanged",
                        dbus_interface=IFACE, path=PATH, sender_keyword="sender")

print("Mode before:", props.Get(IFACE, "Mode"))
try:
    print("SetMode reply:", dev.SetMode(sys.argv[1] if len(sys.argv) > 1 else "capture"))
except dbus.exceptions.DBusException as e:
    print("error:", e.get_dbus_name(), "-", e.get_dbus_message())
print("GetAll:", ", ".join(f"{k}={v}" for k, v in props.GetAll(IFACE).items()))

end = time.time() + 0.5                      # 0.5초 동안 시그널 받기
while time.time() < end:
    glib.g_main_context_iteration(None, False)
    time.sleep(0.01)
```

`add_signal_receiver`가 버스 데몬에 match rule을 등록한다. **sender를 unique name이 아니라 well-known name(또는 지정 안 함)으로** 걸어 두면 서비스가 재시작해도 계속 받는다(1.2절의 함정).

```sh
echo "== root";            l3-client.py capture
echo "== op1 (l3ops)";     runuser -u op1 -- l3-client.py calib
echo "== op2 (no group)";  runuser -u op2 -- l3-client.py idle
echo "== bad arg";         l3-client.py turbo
```

```text
== root
Mode before: idle
SetMode reply: 1
GetAll: Mode=capture, Temperature=41.5
signal ModeChanged idle -> capture from :1.26
== op1 (l3ops)
Mode before: capture
SetMode reply: 1
GetAll: Mode=calib, Temperature=41.5
signal ModeChanged capture -> calib from :1.26
== op2 (no group)
Mode before: calib
error: org.freedesktop.DBus.Error.AccessDenied - Rejected send message, 1 matched rules; type="method_call", sender=":1.30" (uid=1001 pid=674 comm="/usr/bin/python3 /usr/local/bin/l3-client.py idle" label="unconfined") interface="com.example.L3.DeviceManager1" member="SetMode" error name="(unset)" requested_reply="0" destination=":1.26" (uid=995 pid=663 comm="/usr/bin/python3 /usr/local/bin/l3-devmgr.py" label="unconfined")
GetAll: Mode=calib, Temperature=41.5
== bad arg
Mode before: calib
error: com.example.L3.DeviceManager1.Error.InvalidMode - unknown mode: turbo
GetAll: Mode=calib, Temperature=41.5
```

출력에서 볼 것:

- **op2는 프로퍼티는 읽었지만(`Mode before`, `GetAll`) `SetMode`는 거부됐다.** 에러 문구가 디버깅에 필요한 것을 다 준다: 보낸 쪽 uid/pid/명령줄, 인터페이스·멤버, 받는 쪽 uid/pid, 그리고 AppArmor/SELinux 레이블(`label="unconfined"`, L6). 이 거부는 버스 데몬이 했으므로 `l3-devmgr`의 저널에는 op2의 흔적이 없다.
- `bad arg`는 반대로 서비스가 직접 만든 에러다. 이름(`...Error.InvalidMode`)만 보면 누가 거부했는지 구분된다: `org.freedesktop.DBus.Error.*`는 버스 또는 표준 에러, 내 이름공간은 서비스.
- 시그널의 sender는 unique name `:1.26`으로 온다. 시그널 헤더에는 항상 unique name이 실린다.

### 4.5 예제 9 — busctl로 내 서비스 들여다보기와 시그널 순서

무엇을 확인하나: 다른 팀원이 내 서비스를 코드 없이 시험하는 방법, 그리고 메서드 처리 중 메시지 순서.

```sh
busctl introspect com.example.L3.DeviceManager /com/example/L3/DeviceManager --no-pager
busctl get-property com.example.L3.DeviceManager /com/example/L3/DeviceManager com.example.L3.DeviceManager1 Mode
busctl call com.example.L3.DeviceManager /com/example/L3/DeviceManager org.freedesktop.DBus.Properties GetAll s com.example.L3.DeviceManager1
(timeout 6 busctl monitor com.example.L3.DeviceManager > /tmp/mon3.txt 2>&1 &); sleep 2
busctl call com.example.L3.DeviceManager /com/example/L3/DeviceManager com.example.L3.DeviceManager1 SetMode s idle
sleep 4; sh /root/l3/monsum.sh /tmp/mon3.txt
grep -A12 'Member=ModeChanged' /tmp/mon3.txt | grep -E 'STRING|MESSAGE'
journalctl -u l3-devmgr -o cat --no-pager | grep -E 'SetMode|owning'
```

```text
NAME                                TYPE      SIGNATURE RESULT/VALUE FLAGS
com.example.L3.DeviceManager1       interface -         -            -
.SetMode                            method    s         b            -
.ModeChanged                        signal    ss        -            -
org.freedesktop.DBus.Introspectable interface -         -            -
.Introspect                         method    -         s            -
org.freedesktop.DBus.Properties     interface -         -            -
.Get                                method    ss        v            -
.GetAll                             method    s         a{sv}        -
.PropertiesChanged                  signal    sa{sv}as  -            -
s "calib"
a{sv} 2 "Mode" s "calib" "Temperature" d 41.5
b true
2      method_call    :1.36 →com.example.L3.DeviceManager com.example.L3.DeviceManager1.SetMode /com/example/L3/DeviceManager
25     signal         :1.26 →*      com.example.L3.DeviceManager1.ModeChanged /com/example/L3/DeviceManager
26     signal         :1.26 →*      DBus.Properties.PropertiesChanged    /com/example/L3/DeviceManager
2      method_return  :1.26 →:1.36                                       -
  MESSAGE "ss" {
          STRING "calib";
          STRING "idle";
  MESSAGE "sa{sv}as" {
          STRING "com.example.L3.DeviceManager1";
l3-devmgr: owning com.example.L3.DeviceManager as :1.26
SetMode idle -> capture
SetMode capture -> calib
SetMode calib -> idle
```

출력에서 볼 것:

- introspect 표에 `Mode`/`Temperature` **프로퍼티 줄이 없다** — 4.2절에서 말한 dbus-python의 한계다. `get-property`는 그래도 동작한다(Get 메서드를 직접 구현했으니까). 그러나 `busctl introspect`만 보는 동료나 코드 생성기는 프로퍼티가 있는 줄 모른다. 이것이 sd-bus/GDBus를 권하는 이유다.
- `a{sv} 2 "Mode" s "calib" "Temperature" d 41.5`: dict 원소 2개, 각 값이 variant라 타입 문자(`s`, `d`)가 값 앞에 붙는다.
- 순서: method_call → **시그널 2개 → method_return**. 내 코드가 `return True` 전에 시그널을 보냈기 때문이다. 클라이언트가 "응답을 받은 뒤 시그널 구독"을 하면 이미 지나간 시그널을 놓친다. 그래서 **구독을 먼저 걸고 호출한다**(4.4절 클라이언트가 그 순서다).

### 4.6 예제 10 — D-Bus activation

무엇을 확인하나: 서비스를 멈춰 두고 activation 파일만 넣은 뒤, 첫 호출이 서비스를 띄우는지.

```text
[D-BUS Service]
Name=com.example.L3.DeviceManager
Exec=/bin/false
User=l3dev
SystemdService=l3-devmgr.service
```

```sh
systemctl stop l3-devmgr
install -m644 /root/l3/dbus-activation.service \
  /usr/share/dbus-1/system-services/com.example.L3.DeviceManager.service
busctl call org.freedesktop.DBus /org/freedesktop/DBus org.freedesktop.DBus ReloadConfig; sleep 1
busctl list --no-pager | grep L3 | cut -c1-100
systemctl is-active l3-devmgr
time busctl get-property com.example.L3.DeviceManager /com/example/L3/DeviceManager com.example.L3.DeviceManager1 Mode
systemctl is-active l3-devmgr
journalctl -u dbus -o cat --no-pager --since=-30s | grep -i activ
busctl list --no-pager | grep L3 | cut -c1-100
```

```text
com.example.L3.DeviceManager     - -               -                (activatable) -                 
inactive
s "idle"

real	0m2.018s
user	0m0.037s
sys	0m0.145s
active
[system] Activating via systemd: service name='com.example.L3.DeviceManager' unit='l3-devmgr.service' requested by ':1.39' (uid=0 pid=701 comm="busctl get-property com.example.L3.DeviceManager /" label="unconfined")
[system] Successfully activated service 'com.example.L3.DeviceManager'
com.example.L3.DeviceManager   702 l3-devmgr.py    l3dev            :1.40         l3-devmgr.service 
```

출력에서 볼 것: 이름이 `(activatable)`로 보이다가, 첫 `get-property`가 **2.0초** 걸려 응답했다(TCG 에뮬레이션에서 Python 시작 시간 — 실제 하드웨어에서는 훨씬 짧다). 그 사이 버스 데몬이 메시지를 붙잡고 systemd에 `l3-devmgr.service`를 시작시켰고, 이름이 올라오자 배달했다. 응답 내용 `"idle"`은 새 프로세스의 초기값이다 — **activation 서비스는 상태를 메모리에만 두면 재시작 때 잃는다**. 모드를 유지해야 하면 `/var/lib/l3-devmgr/`(유닛의 `StateDirectory=`)에 저장한다.

activation의 장단점: 부팅 시간·메모리를 아끼지만, 첫 호출의 지연이 생기고 "언제 떠 있는지"가 덜 명확하다. 센서 파이프라인처럼 항상 돌아야 하는 데몬은 `[Install] WantedBy=multi-user.target`으로 부팅 때 띄우고, activation 파일은 "죽었다가 누가 부르면 다시 뜨는" 보험으로 함께 둔다.

### 4.7 예제 11 — C에서 부르기: sd-bus

무엇을 확인하는 코드인가: 같은 서비스를 C로, systemd의 `sd-bus` API(libsystemd)로 호출한다. 실무의 C 데몬은 이 API를 많이 쓴다(networkd·logind 자체가 sd-bus로 짜여 있다). 이 이미지에는 `libsystemd-dev` 헤더가 없어서 쓰는 함수 선언만 직접 적었다(libsystemd는 심볼 버전이 붙은 안정 ABI라 가능하다 — 제품 코드라면 `#include <systemd/sd-bus.h>`를 쓴다). VM에서 `gcc -Wall -Wextra`로 경고 없이 빌드하고, 바이너리를 게스트로 옮겨 실행했다.

```c
/* sdbus_client.c — sd-bus(libsystemd)로 l3-devmgr 호출하기
 * 이미지에 libsystemd-dev가 없어서 필요한 선언만 직접 적었다 (ABI는 안정적이다) */
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <stdlib.h>

typedef struct sd_bus sd_bus;
typedef struct sd_bus_message sd_bus_message;
typedef struct { const char *name; const char *message; int _need_free; } sd_bus_error;
int  sd_bus_open_system(sd_bus **ret);
int  sd_bus_call_method(sd_bus *b, const char *dest, const char *path, const char *iface,
                        const char *member, sd_bus_error *err, sd_bus_message **reply,
                        const char *types, ...);
int  sd_bus_message_read(sd_bus_message *m, const char *types, ...);
int  sd_bus_get_property_trivial(sd_bus *b, const char *dest, const char *path,
                                 const char *iface, const char *member,
                                 sd_bus_error *err, char type, void *ret);
sd_bus_message *sd_bus_message_unref(sd_bus_message *m);
sd_bus *sd_bus_flush_close_unref(sd_bus *b);
void sd_bus_error_free(sd_bus_error *e);

#define DEST  "com.example.L3.DeviceManager"
#define PATH  "/com/example/L3/DeviceManager"
#define IFACE "com.example.L3.DeviceManager1"

int main(int argc, char **argv)
{
    sd_bus *bus = NULL;
    sd_bus_message *reply = NULL;
    sd_bus_error err = {0};
    const char *mode = argc > 1 ? argv[1] : "capture";
    int ok = 0, r;
    double temp = 0;

    if ((r = sd_bus_open_system(&bus)) < 0) {
        fprintf(stderr, "open: %s\n", strerror(-r)); return 1;
    }
    r = sd_bus_call_method(bus, DEST, PATH, IFACE, "SetMode", &err, &reply, "s", mode);
    if (r < 0) {
        fprintf(stderr, "SetMode failed: %s: %s\n", err.name, err.message);
    } else {
        sd_bus_message_read(reply, "b", &ok);
        printf("SetMode(%s) -> %s\n", mode, ok ? "true" : "false");
    }
    sd_bus_error_free(&err);
    r = sd_bus_get_property_trivial(bus, DEST, PATH, IFACE, "Temperature", &err, 'd', &temp);
    if (r < 0) fprintf(stderr, "Get: %s\n", err.message);
    else       printf("Temperature = %.1f\n", temp);
    sd_bus_error_free(&err);
    sd_bus_message_unref(reply);
    sd_bus_flush_close_unref(bus);
    return r < 0;
}
```

```sh
# VM에서 빌드 (경고 0개), 링크하는 심볼 버전 확인
gcc -std=gnu11 -Wall -Wextra -O2 sdbus_client.c -o sdbus_client /usr/lib/aarch64-linux-gnu/libsystemd.so.0
objdump -T sdbus_client | grep -E 'GLIBC_2\.(3[4-9]|4)|LIBSYSTEMD'
# 게스트에서 실행
sdbus_client capture; runuser -u op2 -- sdbus_client idle; echo "rc=$?"
```

```text
0000000000000000      DF *UND*	0000000000000000 (LIBSYSTEMD_221) sd_bus_get_property_trivial
0000000000000000      DF *UND*	0000000000000000 (LIBSYSTEMD_221) sd_bus_message_unref
0000000000000000      DF *UND*	0000000000000000 (LIBSYSTEMD_221) sd_bus_open_system
0000000000000000      DF *UND*	0000000000000000 (LIBSYSTEMD_221) sd_bus_message_read
0000000000000000      DF *UND*	0000000000000000 (LIBSYSTEMD_221) sd_bus_call_method
0000000000000000      DF *UND*	0000000000000000 (LIBSYSTEMD_221) sd_bus_error_free
0000000000000000      DF *UND*	0000000000000000 (LIBSYSTEMD_222) sd_bus_flush_close_unref
0000000000000000      DF *UND*	0000000000000000 (GLIBC_2.34) __libc_start_main
SetMode(capture) -> true
Temperature = 41.5
SetMode failed: org.freedesktop.DBus.Error.AccessDenied: Rejected send message, 1 matched rules; type="method_call", sender=":1.43" (uid=1001 pid=714 comm="sdbus_client idle" label="unconfined") interface="com.example.L3.DeviceManager1" member="SetMode" error name="(unset)" requested_reply="0" destination="com.example.L3.DeviceManager" (uid=995 pid=702 comm="/usr/bin/python3 /usr/local/bin/l3-devmgr.py" label="unconfined")
Temperature = 41.5
rc=0
```

출력에서 볼 것: `LIBSYSTEMD_221` 같은 심볼 버전 덕분에 VM(systemd 259 계열)에서 빌드한 바이너리가 게스트의 systemd 255 라이브러리에서 그대로 돈다. 에러 처리 관례는 커널과 같다: 음수 errno 반환 + `sd_bus_error`에 이름·메시지. Python 클라이언트와 같은 정책이 적용돼 op2는 같은 AccessDenied를 받았다 — 정책은 언어와 무관하게 버스에서 집행된다.

서비스 쪽을 sd-bus로 짜면 vtable 하나로 끝난다 (가이드, 실행 안 함 — 헤더가 없다):

```c
static const sd_bus_vtable devmgr_vtable[] = {
    SD_BUS_VTABLE_START(0),
    SD_BUS_METHOD("SetMode", "s", "b", method_set_mode, SD_BUS_VTABLE_UNPRIVILEGED),
    SD_BUS_PROPERTY("Mode", "s", prop_get_mode, 0, SD_BUS_VTABLE_PROPERTY_EMITS_CHANGE),
    SD_BUS_PROPERTY("Temperature", "d", prop_get_temp, 0, 0),
    SD_BUS_SIGNAL("ModeChanged", "ss", 0),
    SD_BUS_VTABLE_END
};
/* sd_bus_add_object_vtable(bus, NULL, PATH, IFACE, devmgr_vtable, ctx);
 * sd_bus_request_name(bus, BUS, 0);
 * 모드가 바뀌면 sd_bus_emit_signal(...) + sd_bus_emit_properties_changed(bus, PATH, IFACE, "Mode", NULL); */
```

드라이버의 `file_operations` 표(G 모듈)와 모양이 같다: 이름 → 콜백 함수의 정적 표를 등록하면 프레임워크가 디스패치한다.

---

## 5. 커널 쪽 네트워크 기초 — `net_device`와 iproute2

네트워크 설정 데몬이 하는 일은 결국 커널의 네 가지 객체를 만지는 것이다: **링크**(`net_device`, I6) · **주소** · **라우트** · **이웃(ARP/NDP)**. 이걸 직접 만지는 도구가 iproute2(`ip`, `bridge`, `ss`)이고, `ip`는 A9 11절의 rtnetlink 소켓으로 커널과 대화한다. networkd와 NetworkManager도 같은 rtnetlink를 쓴다 — 데몬은 "설정 파일을 읽어 `ip` 명령을 대신 쳐 주고, 바뀌면 다시 맞춰 주는 상주 루프"라고 보면 된다.

### 5.1 예제 12 — 지금 상태 읽기

무엇을 확인하나: 게스트의 NIC 두 개(QEMU user-mode 네트워크 두 개, 기본 이미지의 "`e*`에 DHCP" 설정)의 링크·주소·라우트·DNS.

```sh
ip -br link; ip -4 -br addr; ip route
ip -d link show enp0s1 | head -3
ethtool -i enp0s1 2>/dev/null | head -2
udevadm info /sys/class/net/enp0s1 | grep -E 'ID_NET_NAME|ID_NET_LINK_FILE|ID_NET_DRIVER'
resolvectl status --no-pager | head -20
ls -l /etc/resolv.conf; grep -v '^#' /etc/resolv.conf
```

```text
lo               UNKNOWN        00:00:00:00:00:00 <LOOPBACK,UP,LOWER_UP> 
enp0s1           UP             52:54:00:12:34:56 <BROADCAST,MULTICAST,UP,LOWER_UP> 
enp0s2           UP             52:54:00:12:34:57 <BROADCAST,MULTICAST,UP,LOWER_UP> 
lo               UNKNOWN        127.0.0.1/8 
enp0s1           UP             10.0.2.15/24 metric 1024 
enp0s2           UP             10.0.3.15/24 metric 1024 
default via 10.0.3.2 dev enp0s2 proto dhcp src 10.0.3.15 metric 1024 
default via 10.0.2.2 dev enp0s1 proto dhcp src 10.0.2.15 metric 1024 
10.0.2.0/24 dev enp0s1 proto kernel scope link src 10.0.2.15 metric 1024 
10.0.2.2 dev enp0s1 proto dhcp scope link src 10.0.2.15 metric 1024 
10.0.2.3 dev enp0s1 proto dhcp scope link src 10.0.2.15 metric 1024 
10.0.3.0/24 dev enp0s2 proto kernel scope link src 10.0.3.15 metric 1024 
10.0.3.2 dev enp0s2 proto dhcp scope link src 10.0.3.15 metric 1024 
10.0.3.3 dev enp0s2 proto dhcp scope link src 10.0.3.15 metric 1024 
2: enp0s1: <BROADCAST,MULTICAST,UP,LOWER_UP> mtu 1500 qdisc pfifo_fast state UP mode DEFAULT group default qlen 1000
    link/ether 52:54:00:12:34:56 brd ff:ff:ff:ff:ff:ff promiscuity 0  allmulti 0 minmtu 68 maxmtu 65535 numtxqueues 1 numrxqueues 1 gso_max_size 65536 gso_max_segs 65535 tso_max_size 65536 tso_max_segs 65535 gro_max_size 65536 parentbus virtio parentdev virtio0 
driver: virtio_net
version: 1.0.0
E: ID_NET_DRIVER=virtio_net
E: ID_NET_NAME_MAC=enx525400123456
E: ID_NET_NAME_PATH=enp0s1
E: ID_NET_LINK_FILE=/usr/lib/systemd/network/99-default.link
E: ID_NET_NAME=enp0s1
Global
         Protocols: -LLMNR -mDNS -DNSOverTLS DNSSEC=no/unsupported
  resolv.conf mode: stub

Link 2 (enp0s1)
    Current Scopes: DNS
         Protocols: +DefaultRoute -LLMNR -mDNS -DNSOverTLS DNSSEC=no/unsupported
Current DNS Server: 10.0.2.3
       DNS Servers: 10.0.2.3

Link 3 (enp0s2)
    Current Scopes: DNS
         Protocols: +DefaultRoute -LLMNR -mDNS -DNSOverTLS DNSSEC=no/unsupported
Current DNS Server: 10.0.3.3
       DNS Servers: 10.0.3.3
lrwxrwxrwx 1 root root 39 Oct 18 04:26 /etc/resolv.conf -> ../run/systemd/resolve/stub-resolv.conf

nameserver 127.0.0.53
options edns0 trust-ad
search .
```

출력에서 볼 것 — 이 짧은 출력에 제품 버그가 하나 숨어 있다:

- **기본 라우트(default)가 두 개, metric이 둘 다 1024**다. "`e*` 전부 DHCP"라는 편한 설정이 서비스 포트에도 게이트웨이를 받아 왔다. 커널은 같은 metric이면 먼저 들어온 쪽을 쓰므로, 인터넷 트래픽이 **어느 포트로 나갈지가 부팅마다 바뀔 수 있다**. 서비스 포트·센서 포트에는 게이트웨이를 받지 않게 해야 한다(6절의 정적 설정, NM에서는 `ipv4.never-default`).
- 이름 `enp0s1`은 udev의 predictable naming이 PCI 경로(`ID_NET_NAME_PATH`)로 붙였다. 처음엔 커널이 `eth0`로 만들고 udev가 바꾼다(아래 저널의 "renamed to enp0s1"). 이름 정책은 `.link` 파일(`99-default.link`)이 정한다 — L2에서 자세히.
- `/etc/resolv.conf` → systemd-resolved의 **stub**(`127.0.0.53`). 앱의 DNS 질의는 로컬 resolved로 가고, resolved가 링크별 DNS 서버(DHCP로 받은 10.0.2.3, 10.0.3.3)로 보낸다. 링크별 DNS를 관리할 수 있다는 게 장점이지만, resolved가 죽으면 이름 해석이 전부 멈춘다. 아주 작은 장비는 `resolv.conf`를 정적 파일로 두기도 한다.

### 5.2 `ip` 명령 요약

| 하고 싶은 것 | 명령 | 데몬이 같은 일을 할 때 |
|---|---|---|
| 링크 올리기/내리기 | `ip link set dev eth1 up` | `[Link] ActivationPolicy=` |
| 주소 붙이기 | `ip addr add 192.168.77.10/24 dev eth1` | `[Network] Address=` |
| 기본 라우트 | `ip route add default via 192.168.1.1` | `Gateway=` |
| VLAN | `ip link add link eth1 name eth1.100 type vlan id 100` | `.netdev` `Kind=vlan` |
| 브리지 | `ip link add br0 type bridge; ip link set eth1 master br0` | `.netdev` `Kind=bridge` + `Bridge=br0` |
| 이벤트 보기 | `ip monitor link address route` | 데몬이 rtnetlink 멀티캐스트 그룹 구독 |
| 이웃 테이블 | `ip neigh` | — |

`ip`로 설정한 것은 **휘발성**이다 — 재부팅하면 사라지고, 데몬이 관리하는 링크라면 데몬이 곧 자기 설정으로 덮어쓴다. "`ip addr add`로 넣었는데 몇 초 뒤 사라졌다"는 데몬이 링크를 관리 중이라는 뜻이다. 네트워크 네임스페이스(netns, `ip netns`)와 veth는 컨테이너 노트 L8에서 다룬다.

---

## 6. systemd-networkd — 파일 세 종류로 끝나는 고정 설정

### 6.1 직관과 정의

systemd-networkd는 **선언형** 설정 데몬이다. "이 이름/MAC의 링크에는 이 주소" 같은 규칙을 파일로 적어 두면, 링크가 나타날 때(부팅, USB 이더넷 꽂기, 드라이버 재로드) 규칙에 맞춰 설정하고 계속 유지한다. 파일 종류 셋:

| 확장자 | 누가 읽나 | 무엇을 정하나 | 예 |
|---|---|---|---|
| `.link` | **udev** (`net_setup_link` 빌트인) | 링크 이름, MAC, MTU, offload 같은 **장치 속성** | `MACAddress=` 매치 → `Name=lidar0` |
| `.netdev` | networkd | **가상 장치를 만든다** | bridge, vlan, bond, vxlan, wireguard, dummy |
| `.network` | networkd | 링크에 **L3 설정**을 입힌다 | DHCP, `Address=`, `Gateway=`, `DNS=`, `VLAN=`, `Bridge=` |

찾는 곳은 `/etc/systemd/network/`(관리자) > `/run/systemd/network/`(런타임) > `/usr/lib/systemd/network/`(패키지) 순이고, 같은 이름이면 앞쪽이 이긴다. 한 링크에는 **사전순으로 처음 매치되는 `.network` 하나만** 적용된다 — 그래서 `10-`, `20-` 같은 숫자 접두를 붙인다. `[Match]`는 `Name=`(glob), `MACAddress=`, `Path=`, `Driver=`, `Type=` 등으로 고른다. 이름은 바뀔 수 있으니 **센서 포트는 `Path=`나 `MACAddress=`로 매치**하는 편이 안전하다.

### 6.2 예제 13 — 업링크 DHCP + 서비스 포트 정적 IP + VLAN + 브리지

무엇을 확인하나: 실제 장비 구성을 흉내 낸 `.network`/`.netdev` 여섯 개를 넣고 적용한다. 기본 `20-wired.network`(`e*` 전부 DHCP)는 치웠다.

```text
# /etc/systemd/network/10-uplink.network — 업링크: DHCP
[Match]
Name=enp0s1

[Network]
DHCP=ipv4

[Link]
RequiredForOnline=routable
```

```text
# /etc/systemd/network/20-svc.network — 서비스(정비) 포트: 고정 IP + 링크로컬 백업, 게이트웨이 없음
[Match]
Name=enp0s2

[Network]
Address=192.168.77.10/24
LinkLocalAddressing=ipv4
VLAN=svc.100

[Link]
RequiredForOnline=no
```

```text
# 30-svc-vlan.netdev                 # 31-svc-vlan.network
[NetDev]                             [Match]
Name=svc.100                         Name=svc.100
Kind=vlan
                                     [Network]
[VLAN]                               Address=10.100.0.10/24
Id=100
                                     [Link]
                                     RequiredForOnline=no
```

```text
# 40-br0.netdev                      # 41-br0.network
[NetDev]                             [Match]
Name=br0                             Name=br0
Kind=bridge
                                     [Network]
                                     Address=172.31.0.1/24
                                     ConfigureWithoutCarrier=yes

                                     [Link]
                                     RequiredForOnline=no
```

첫 적용에서 실패가 났다 — 그대로 보여 준다(실무에서 똑같이 난다):

```sh
networkctl reload; networkctl reconfigure enp0s1 enp0s2; sleep 12
networkctl list --no-pager
journalctl -u systemd-networkd -o cat --no-pager --since=-60s | tail -15
zgrep -E 'CONFIG_VLAN_8021Q=|CONFIG_BRIDGE=' /proc/config.gz
```

```text
IDX LINK   TYPE     OPERATIONAL SETUP
  1 lo     loopback carrier     unmanaged
  2 enp0s1 ether    routable    configured
  3 enp0s2 ether    carrier     failed

3 links listed.
br0: netdev could not be created: Operation not supported
enp0s2: Reconfiguring with /etc/systemd/network/20-svc.network.
enp0s2: DHCP lease lost
enp0s1: Reconfiguring with /etc/systemd/network/10-uplink.network.
enp0s1: DHCP lease lost
enp0s2: Could not create stacked netdev: Unknown device type. Operation not supported
enp0s2: Failed
enp0s1: Configuring with /etc/systemd/network/10-uplink.network.
enp0s1: DHCPv4 address 10.0.2.15/24, gateway 10.0.2.2 acquired from 10.0.2.2
CONFIG_BRIDGE=m
CONFIG_VLAN_8021Q=m
```

`Operation not supported` / `Unknown device type` = **커널이 그 종류의 netdev를 모른다**. `8021q`·`bridge`가 모듈(`=m`)인데 로드할 수 없었다. 원인은 처음 받은 랩 이미지에서 모듈이 `/lib/modules/6.18.55-sec/`가 아니라 `/lib/modules/` 바로 아래에 깔려 있어 `modprobe`가 못 찾은 것이었다(랩 담당에게 보고해 수정됨). 내 복사본에서 디렉터리를 옮기고 `depmod` → `modprobe` 했다:

```sh
cd /lib/modules && mkdir 6.18.55-sec && mv build kernel modules.* 6.18.55-sec/ && depmod
modprobe 8021q && modprobe bridge && lsmod | grep -E '8021q|bridge'
systemctl restart systemd-networkd; sleep 15
journalctl -u systemd-networkd -o cat --no-pager --since=-20s | grep -E 'br0|svc|netdev' | head
networkctl list --no-pager
ip -br addr | cut -c1-110
ip -d link show svc.100 | sed -n '1,3p' | cut -c1-140
ip route
```

```text
bridge                282624  0
ipv6                  499712  13 bridge
8021q                  28672  0
garp                   12288  1 8021q
mrp                    20480  1 8021q
stp                    12288  2 bridge,garp
llc                    16384  3 bridge,stp,garp
br0: netdev ready
enp0s2: Configuring with /etc/systemd/network/20-svc.network.
br0: Configuring with /etc/systemd/network/41-br0.network.
br0: Link UP
svc.100: netdev ready
svc.100: Configuring with /etc/systemd/network/31-svc-vlan.network.
svc.100: Link UP
svc.100: Gained carrier
svc.100: Gained IPv6LL
IDX LINK    TYPE     OPERATIONAL SETUP
  1 lo      loopback carrier     unmanaged
  2 enp0s1  ether    routable    configured
  3 enp0s2  ether    routable    configured
  4 br0     bridge   no-carrier  configured
  5 svc.100 vlan     routable    configured

5 links listed.
lo               UNKNOWN        127.0.0.1/8 ::1/128 
enp0s1           UP             10.0.2.15/24 metric 1024 fec0::5054:ff:fe12:3456/64 fe80::5054:ff:fe12:3456/64
enp0s2           UP             169.254.192.101/16 metric 2048 192.168.77.10/24 
br0              DOWN           172.31.0.1/24 
svc.100@enp0s2   UP             10.100.0.10/24 fe80::5054:ff:fe12:3457/64 
5: svc.100@enp0s2: <BROADCAST,MULTICAST,UP,LOWER_UP> mtu 1500 qdisc noqueue state UP mode DEFAULT group default qlen 1000
    link/ether 52:54:00:12:34:57 brd ff:ff:ff:ff:ff:ff promiscuity 0  allmulti 0 minmtu 0 maxmtu 65535 
    vlan protocol 802.1Q id 100 <REORDER_HDR> addrgenmode eui64 numtxqueues 1 numrxqueues 1 gso_max_size 65536 gso_max_segs 65535 tso_max_si
default via 10.0.2.2 dev enp0s1 proto dhcp src 10.0.2.15 metric 1024 
10.0.2.0/24 dev enp0s1 proto kernel scope link src 10.0.2.15 metric 1024 
10.0.2.2 dev enp0s1 proto dhcp scope link src 10.0.2.15 metric 1024 
10.0.2.3 dev enp0s1 proto dhcp scope link src 10.0.2.15 metric 1024 
10.100.0.0/24 dev svc.100 proto kernel scope link src 10.100.0.10 
169.254.0.0/16 dev enp0s2 proto kernel scope link src 169.254.192.101 metric 2048 
172.31.0.0/24 dev br0 proto kernel scope link src 172.31.0.1 linkdown 
192.168.77.0/24 dev enp0s2 proto kernel scope link src 192.168.77.10 
```

출력에서 볼 것:

- **netdev 생성 실패는 한 번 나면 `networkctl reload`로 다시 시도되지 않았다.** 모듈을 넣은 뒤 networkd를 재시작해야 생겼다. 제품에서는 `/etc/modules-load.d/`에 `8021q`, `bridge`를 적어 networkd보다 먼저 로드되게 하거나(systemd-modules-load는 sysinit 단계), 커널에 built-in으로 넣는다.
- `lsmod`에 `ipv6`가 보인다: 이 커널은 IPv6도 모듈이다. bridge가 의존해서 같이 올라왔고, 그 뒤 링크에 `fe80::`/`fec0::` 주소가 생겼다. 커널 config 하나가 네트워크 동작을 크게 바꾼다(K6에서 본 `net/ipv6/...` sysctl 경고와 같은 뿌리).
- 기본 라우트가 이제 **하나**(enp0s1)다. 서비스 포트는 `192.168.77.0/24` 연결 라우트만 있다 — 5.1절의 버그가 사라졌다.
- `LinkLocalAddressing=ipv4`는 정적 주소와 **함께** `169.254.192.101/16`을 붙였다(metric 2048, 정적 주소보다 덜 선호). DHCP 실패 시에만 쓰는 "fallback"이 아니라 항상 붙는다는 점을 기억하자. 9.3절에서 정비 노트북이 IP를 몰라도 접속하는 데 쓴다.
- `svc.100@enp0s2`: VLAN 링크가 부모 위에 쌓였다(`vlan protocol 802.1Q id 100`). MAC은 부모와 같다.
- `br0`는 포트가 없어 `no-carrier`/`linkdown`이지만 `ConfigureWithoutCarrier=yes`로 주소를 받았다. 포트를 붙이려면 그 링크의 `.network`에 `Bridge=br0`. 컨테이너 veth를 붙이는 장면은 L8.

### 6.3 예제 14 — `networkctl status`

무엇을 확인하나: 링크 하나에 어떤 파일이 적용됐고 online 판정에 들어가는지.

```sh
networkctl status enp0s2 --no-pager -n 0 | cut -c1-110
networkctl status enp0s1 --no-pager -n 0 | grep -E 'State|Address|Gateway|DNS|Network File|Online'
```

```text
● 3: enp0s2
                   Link File: /usr/lib/systemd/network/99-default.link
                Network File: /etc/systemd/network/20-svc.network
                       State: routable (configured)
                Online state: unknown
                        Type: ether
                        Path: platform-4010000000.pcie-pci-0000:00:02.0
                      Driver: virtio_net
                      Vendor: Red Hat, Inc.
                       Model: Virtio network device
            Hardware Address: 52:54:00:12:34:57
                         MTU: 1500 (min: 68, max: 65535)
                       QDisc: pfifo_fast
IPv6 Address Generation Mode: none
    Number of Queues (Tx/Rx): 1/1
            Auto negotiation: no
                     Address: 192.168.77.10
                              169.254.192.101
           Activation Policy: up
         Required For Online: no

                Network File: /etc/systemd/network/10-uplink.network
                       State: routable (configured)
                Online state: online
            Hardware Address: 52:54:00:12:34:56
IPv6 Address Generation Mode: eui64
                     Address: 10.0.2.15 (DHCP4 via 10.0.2.2)
                     Gateway: 10.0.2.2
                         DNS: 10.0.2.3
         Required For Online: yes
```

출력에서 볼 것: `Link File`(udev가 적용한 이름 규칙)과 `Network File`(networkd 설정)이 따로 나온다 — "왜 이 설정이 안 먹지?"의 첫 확인 지점이다. `Path: platform-4010000000.pcie-pci-0000:00:02.0`이 `[Match] Path=`에 쓸 값이다. `Required For Online: no`인 enp0s2는 `Online state: unknown`(판정 대상 아님)이다. 다음 절의 핵심이 이 한 줄이다.

### 6.4 자주 쓰는 키 정리

| 섹션.키 | 뜻 | 메모 |
|---|---|---|
| `[Network] DHCP=` | `yes`/`ipv4`/`ipv6`/`no` | 서버가 없으면 영원히 configuring |
| `[Network] Address=` | 정적 주소 (여러 줄 가능) | `/24` 같은 prefix 필수 |
| `[Network] Gateway=` | 기본 라우트 | 서비스·센서 포트에는 넣지 않는다 |
| `[Network] DNS=` `Domains=` | 링크별 DNS | resolved로 전달된다 |
| `[Network] LinkLocalAddressing=` | `ipv4`/`ipv6`/`yes`/`no` | 169.254/16, fe80::/64 |
| `[Network] VLAN=` `Bridge=` `Bond=` | 이 링크 위에 쌓을 netdev, 소속 | `.netdev`가 있어야 한다 |
| `[Network] ConfigureWithoutCarrier=` | 케이블 없어도 주소 | 브리지·정비 포트 |
| `[Network] IgnoreCarrierLoss=` | 잠깐 끊겨도 주소 유지 | 케이블 흔들림, PHY 재협상 |
| `[DHCPv4] UseGateway=` `UseDNS=` | DHCP로 받은 것 중 버릴 것 | 서비스 포트에서 `UseGateway=no` |
| `[DHCPv4] ClientIdentifier=` | `mac`/`duid` | machine-id 기반 DUID 주의(K6) |
| `[Link] RequiredForOnline=` | online 판정에 포함? | `no` 또는 원하는 최소 상태 |
| `[Link] ActivationPolicy=` | `up`/`down`/`manual`… | 링크를 내려 두는 포트 |

---

## 7. `network-online.target` — 부팅이 2분 멈추는 이유

### 7.1 직관

systemd에는 네트워크 관련 target이 셋 있다(L1).

| target | 뜻 | 누가 쓰나 |
|---|---|---|
| `network-pre.target` | 네트워크 설정 **전**에 끝내야 할 것 (방화벽 규칙) | nftables.service 등이 `Before=` |
| `network.target` | 네트워크 **관리 데몬이 시작됨** — 주소가 있다는 뜻이 아니다 | 대부분의 데몬이 `After=` (종료 순서용) |
| `network-online.target` | **설정이 "online"으로 판정됨** | 원격 서버에 즉시 접속해야 하는 서비스 |

`network-online.target`은 평소엔 아무도 원하지 않으면 활성화되지 않는다(**수동 target**). 어떤 유닛이 `Wants=network-online.target`을 하는 순간, 그 target이 끌어오는 `systemd-networkd-wait-online.service`(NM이면 `NetworkManager-wait-online.service`)가 부팅에 들어간다. wait-online은 **모든 관리 링크가 `RequiredForOnline` 조건을 만족할 때까지** 기다리고, 기본 **타임아웃이 120초**다.

```svg
<svg viewBox="0 0 680 330" xmlns="http://www.w3.org/2000/svg">
<defs><marker id="ah4" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto-start-reverse"><path d="M0,0 L10,5 L0,10 z" fill="currentColor"/></marker></defs>
<line x1="20" y1="300" x2="660" y2="300" stroke="currentColor" stroke-width="1.2" marker-end="url(#ah4)"/>
<text x="640" y="320" font-size="12" text-anchor="end">부팅 시간 →</text>
<rect x="20" y="20" width="150" height="30" rx="4" fill="#888" fill-opacity="0.12" stroke="#888"/>
<text x="95" y="40" font-size="12" text-anchor="middle">network-pre.target</text>
<rect x="60" y="62" width="170" height="30" rx="4" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
<text x="145" y="82" font-size="12" text-anchor="middle">systemd-networkd</text>
<rect x="160" y="104" width="150" height="30" rx="4" fill="#888" fill-opacity="0.12" stroke="#888"/>
<text x="235" y="124" font-size="12" text-anchor="middle">network.target</text>
<text x="320" y="124" font-size="12">← 데몬 시작됨 (주소 아직 없음)</text>
<rect x="160" y="146" width="380" height="30" rx="4" fill="#d0564a" fill-opacity="0.12" stroke="#d0564a"/>
<text x="350" y="166" font-size="12" text-anchor="middle">systemd-networkd-wait-online (최대 120 s)</text>
<rect x="540" y="188" width="120" height="30" rx="4" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
<text x="600" y="208" font-size="12" text-anchor="middle">network-online</text>
<rect x="540" y="230" width="120" height="30" rx="4" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
<text x="600" y="250" font-size="12" text-anchor="middle">uploader.service</text>
<text x="20" y="208" font-size="12">링크마다 RequiredForOnline 조건 확인:</text>
<text x="20" y="228" font-size="12">업링크 routable ✓ · 서비스 포트 DHCP 대기 ✗</text>
<text x="20" y="248" font-size="12">→ 하나라도 ✗면 타임아웃까지 기다린다</text>
<text x="20" y="280" font-size="12">After=network.target 만 쓴 데몬은 여기서 기다리지 않는다</text>
</svg>
```
그림 4 — 네트워크 target의 순서. `network.target`은 "데몬이 떴다"일 뿐이고, `network-online.target`은 wait-online이 모든 필수 링크를 확인해야 도달한다. 필수 링크 하나가 영원히 준비되지 않으면 120초 타임아웃까지 뒤쪽 유닛이 전부 밀린다.

### 7.2 예제 15 — 일부러 2분 멈추게 만들기

무엇을 확인하나: 새 이미지 복사본에 (1) 업링크 DHCP, (2) **DHCP 서버가 없는 두 번째 포트에 DHCP**(RequiredForOnline 기본값 = yes), (3) `network-online.target`을 원하는 앱 유닛을 넣고 부팅한다. 두 번째 NIC는 상대가 없는 QEMU socket 네트워크(`-netdev socket,listen=127.0.0.1:41330`)다.

```text
# /etc/systemd/network/20-svc.network
# 실수 재현: DHCP 서버가 없는 포트에 DHCP + RequiredForOnline 기본값(yes)
[Match]
Name=enp0s2

[Network]
DHCP=ipv4
```

```text
# /etc/systemd/system/l3-needs-online.service (multi-user.target.wants에 링크)
[Unit]
Description=L3 app that wants the network "online" (e.g. cloud uploader)
Wants=network-online.target
After=network-online.target

[Service]
Type=oneshot
ExecStart=/bin/echo l3: network is online
RemainAfterExit=yes

[Install]
WantedBy=multi-user.target
```

로그인 프롬프트가 뜬 직후(1분쯤) 잡 목록:

```sh
uptime; systemctl list-jobs --no-pager; networkctl list --no-pager
```

```text
 04:46:23 up 1 min,  1 user,  load average: 7.53, 2.44, 0.86
JOB UNIT                                 TYPE  STATE
2   multi-user.target                    start waiting
99  systemd-update-utmp-runlevel.service start waiting
130 podman-restart.service               start waiting
120 l3-needs-online.service              start waiting
129 podman-auto-update.service           start waiting
121 network-online.target                start waiting
1   graphical.target                     start waiting
122 systemd-networkd-wait-online.service start running

8 jobs listed.
IDX LINK   TYPE     OPERATIONAL SETUP
  1 lo     loopback carrier     unmanaged
  2 enp0s1 ether    off         pending
  3 enp0s2 ether    off         pending

3 links listed.
```

로그인은 됐지만 **부팅은 끝나지 않았다**: `multi-user.target`이 `waiting`, 그 앞을 wait-online이 막고 있다. 내 유닛만이 아니라 이미지에 원래 있던 `podman-restart.service`, `podman-auto-update.service`도 `network-online.target`을 원한다. 첫 게스트에서 미리 확인해 둔 출력:

```sh
systemctl list-dependencies --reverse network-online.target
```

```text
network-online.target
○ ├─podman-auto-update.service
● └─podman-restart.service
```

**내가 안 넣어도 누군가는 network-online을 끌어온다** — 패키지를 깔면 생기는 일이다. 끝날 때까지 기다려 잰다:

```sh
while systemctl list-jobs --no-legend | grep -q .; do sleep 5; done
systemd-analyze; systemd-analyze blame --no-pager | head -6
systemd-analyze critical-chain l3-needs-online.service --no-pager
journalctl -b -u systemd-networkd-wait-online -o cat --no-pager
networkctl list --no-pager
```

```text
Startup finished in 6.599s (kernel) + 2min 54.589s (userspace) = 3min 1.189s 
graphical.target reached after 2min 53.036s in userspace.
2min 1.272s systemd-networkd-wait-online.service
    54.420s dev-vda.device
    37.908s systemd-udev-trigger.service
    24.044s apparmor.service
    15.072s podman-clean-transient.service
    12.324s podman-auto-update.service
The time when unit became active or started is printed after the "@" character.
The time the unit took to start is printed after the "+" character.

l3-needs-online.service +1.480s
└─network-online.target @2min 40.172s
  └─network.target @41.944s
    ...
Starting systemd-networkd-wait-online.service - Wait for Network to be Configured...
Timeout occurred while waiting for network connectivity.
systemd-networkd-wait-online.service: Main process exited, code=exited, status=1/FAILURE
systemd-networkd-wait-online.service: Failed with result 'exit-code'.
Failed to start systemd-networkd-wait-online.service - Wait for Network to be Configured.
IDX LINK   TYPE     OPERATIONAL SETUP
  1 lo     loopback carrier     unmanaged
  2 enp0s1 ether    routable    configured
  3 enp0s2 ether    degraded    configuring
```

출력에서 볼 것:

- (critical-chain의 아래쪽 10줄은 `...`로 생략.) `blame` 1위가 **`systemd-networkd-wait-online.service` 2min 1.272s** — 120초 타임아웃 + 시작 비용. 저널에 "Timeout occurred while waiting for network connectivity"가 찍히고 유닛은 `failed`.
- 그런데 `network-online.target`은 **결국 도달했고**(@2min 40s) 내 앱도 실행됐다. target은 wait-online을 `Wants`(약한 의존)로 끌어오므로, 실패해도 2분 늦게 그냥 진행한다. 즉 증상은 "부팅 실패"가 아니라 **"이유 없이 2분 느린 부팅 + failed 유닛 1개"**다. 필드에서 "가끔 부팅이 2분 걸린다"는 신고가 오면 이걸 먼저 의심한다.
- enp0s2는 `degraded`(링크로컬 IPv6만 있음) / `configuring`(DHCP 대기 중) — 영원히 `routable`이 안 된다.
- 같은 설정으로 한 번 더 부팅했더니 `2min 1.886s systemd-networkd-wait-online.service`, `network-online.target @2min 43.367s`로 재현됐다.

### 7.3 예제 16 — 고치고 다시 재기

무엇을 확인하나: 서비스 포트를 online 판정에서 빼면(`RequiredForOnline=no`) 어떻게 되는지.

```text
# 20-svc.network 끝에 추가
[Link]
RequiredForOnline=no
```

```sh
# poweroff 후 같은 QEMU 명령으로 다시 부팅
while systemctl list-jobs --no-legend | grep -q .; do sleep 5; done
systemd-analyze; systemd-analyze blame --no-pager | grep -E 'wait-online'
systemd-analyze critical-chain l3-needs-online.service --no-pager | sed -n '3,7p'
journalctl -b -o short-monotonic --no-pager -u systemd-networkd -u systemd-networkd-wait-online -u l3-needs-online \
  | grep -E 'Started|renamed|DHCPv4 address|Gained carrier|Finished|network is online' | cut -c1-130
```

```text
Startup finished in 3.064s (kernel) + 1min 52.201s (userspace) = 1min 55.265s 
graphical.target reached after 1min 50.550s in userspace.
1min 19.912s systemd-networkd-wait-online.service

l3-needs-online.service +2.820s
└─network-online.target @1min 35.886s
  └─systemd-networkd-wait-online.service @15.877s +1min 19.912s
    └─systemd-networkd.service @12.829s +2.960s
[   18.347574] labsysd systemd-networkd[165]: lo: Gained carrier
[   18.859289] labsysd systemd[1]: Started systemd-networkd.service - Network Configuration.
[   38.102069] labsysd systemd-networkd[165]: eth0: Interface name change detected, renamed to enp0s1.
[   38.193167] labsysd systemd-networkd[165]: eth1: Interface name change detected, renamed to enp0s2.
[   97.021621] labsysd systemd-networkd[165]: enp0s2: Gained carrier
[   97.265995] labsysd systemd-networkd[165]: enp0s1: Gained carrier
[   97.446033] labsysd systemd-networkd[165]: enp0s1: DHCPv4 address 10.0.2.15/24, gateway 10.0.2.2 acquired from 10.0.2.2
[   98.858372] labsysd systemd[1]: Finished systemd-networkd-wait-online.service - Wait for Network to be Configured.
[  101.872309] labsysd echo[511]: l3: network is online
[  101.969723] labsysd systemd[1]: Finished l3-needs-online.service - L3 app that wants the network "online" (e.g. cloud uploader)
```

출력에서 볼 것: wait-online이 **성공**(`Finished`)했고 network-online이 @1min 35.9s로 당겨졌다(약 65초 단축). 남은 1분 20초는 "업링크가 실제로 routable이 될 때까지"의 진짜 대기다. 저널을 보면 이름 변경(38 s) 뒤 carrier/DHCP(97 s)까지 약 60초가 비는데, 이 구간은 networkd가 udev의 장치 초기화 완료를 기다리는 시간이고 TCG 에뮬레이션에서 udev가 매우 느린 탓이다(`systemd-udev-trigger` 37~51 s, K6에서 본 것과 같은 현상). **실제 SoC에서는 이 구간이 1초 안팎**이다 — 숫자는 상대 비교로만 보자.

```svg
<svg viewBox="0 0 680 250" xmlns="http://www.w3.org/2000/svg">
<text x="20" y="22" font-size="13" font-weight="bold">부팅별 측정 (QEMU TCG — 실제 HW 값 아님)</text>
<rect x="420" y="10" width="12" height="12" fill="#4a7bd0" fill-opacity="0.6" stroke="#4a7bd0"/>
<text x="438" y="21" font-size="12">wait-online 소요</text>
<rect x="545" y="10" width="12" height="12" fill="#e08a3c" fill-opacity="0.6" stroke="#e08a3c"/>
<text x="563" y="21" font-size="12">online 도달 시각</text>
<text x="190" y="60" font-size="12" text-anchor="end">잘못된 설정 #1</text>
<rect x="200" y="44" width="269.6" height="14" fill="#4a7bd0" fill-opacity="0.6" stroke="#4a7bd0"/>
<text x="475" y="56" font-size="12">121.3 s</text>
<rect x="200" y="60" width="356.0" height="14" fill="#e08a3c" fill-opacity="0.6" stroke="#e08a3c"/>
<text x="562" y="72" font-size="12">160.2 s</text>
<text x="190" y="112" font-size="12" text-anchor="end">잘못된 설정 #2</text>
<rect x="200" y="96" width="270.9" height="14" fill="#4a7bd0" fill-opacity="0.6" stroke="#4a7bd0"/>
<text x="476" y="108" font-size="12">121.9 s</text>
<rect x="200" y="112" width="363.1" height="14" fill="#e08a3c" fill-opacity="0.6" stroke="#e08a3c"/>
<text x="569" y="124" font-size="12">163.4 s</text>
<text x="190" y="164" font-size="12" text-anchor="end">RequiredForOnline=no</text>
<rect x="200" y="148" width="177.6" height="14" fill="#3f9a6b" fill-opacity="0.6" stroke="#3f9a6b"/>
<text x="383" y="160" font-size="12">79.9 s</text>
<rect x="200" y="164" width="213.1" height="14" fill="#e08a3c" fill-opacity="0.6" stroke="#e08a3c"/>
<text x="419" y="176" font-size="12">95.9 s</text>
<line x1="200" y1="190" x2="600" y2="190" stroke="currentColor" stroke-width="1"/>
<line x1="200" y1="190" x2="200" y2="195" stroke="currentColor"/>
<text x="200" y="208" font-size="12" text-anchor="middle">0</text>
<line x1="333.3" y1="190" x2="333.3" y2="195" stroke="currentColor"/>
<text x="333.3" y="208" font-size="12" text-anchor="middle">60</text>
<line x1="466.7" y1="190" x2="466.7" y2="195" stroke="currentColor"/>
<text x="466.7" y="208" font-size="12" text-anchor="middle">120</text>
<line x1="600" y1="190" x2="600" y2="195" stroke="currentColor"/>
<text x="600" y="208" font-size="12" text-anchor="middle">180 s</text>
<line x1="466.7" y1="36" x2="466.7" y2="190" stroke="#d0564a" stroke-dasharray="4 3"/>
<text x="340" y="236" font-size="12" text-anchor="middle">빨간 점선 = wait-online 기본 타임아웃 120 s (고친 뒤의 79.9 s는 TCG의 느린 udev가 대부분)</text>
</svg>
```
그림 5 — 직접 잰 세 번의 부팅. 잘못된 설정에서는 wait-online이 매번 120초 타임아웃에 걸린다. 서비스 포트를 online 판정에서 빼면 업링크가 준비되는 순간 끝난다.

### 7.4 고치는 방법들 — 무엇을 언제

| 방법 | 어떻게 | 언제 |
|---|---|---|
| 링크를 판정에서 빼기 | 그 `.network`에 `[Link] RequiredForOnline=no` | 서비스·센서 포트 (가장 흔한 정답) |
| 최소 상태 낮추기 | `RequiredForOnline=degraded` 또는 `carrier` | 주소는 필요 없고 링크만 살아 있으면 될 때 |
| "하나만 되면 online" | wait-online 드롭인: `ExecStart=` 비우고 `ExecStart=/usr/lib/systemd/systemd-networkd-wait-online --any` | 업링크가 이더넷·Wi-Fi·LTE 중 아무거나일 때 |
| 특정 인터페이스만 | `--interface=enp0s1` (여러 번 가능) | 업링크가 정해져 있을 때 |
| 타임아웃 줄이기 | `--timeout=15` | 증상 완화일 뿐 원인 해결 아님 |
| 앱이 online을 원하지 않게 | `Wants=network-online.target` 삭제, 앱이 재시도 | **가장 좋은 설계**: 네트워크는 언제든 끊긴다 |

마지막 줄이 핵심이다. 필드 장비의 네트워크는 부팅 후에도 끊겼다 붙는다. "부팅 때 한 번 online을 기다리고 그 뒤엔 영원히 된다고 가정"하는 앱은 어차피 깨진다. 업로더는 `After=network.target`만 걸고 **연결 실패 시 백오프 재시도**하거나, networkd/NM의 D-Bus 시그널(`OperationalState` `PropertiesChanged`)을 구독해 연결이 생기면 시작한다. wait-online은 NFS root, 부팅 때 반드시 원격 마운트가 필요한 경우처럼 정말 필요한 곳에만 둔다.

함정: networkd와 NM을 둘 다 켜 두면 `systemd-networkd-wait-online`과 `NetworkManager-wait-online`이 **둘 다** network-online.target에 걸린다. 쓰지 않는 쪽의 wait-online이 관리할 링크가 없어 타임아웃까지 간다(8.2절에서 NM을 켤 때 출력에 그 심볼릭 링크가 생기는 게 보인다).

---

## 8. NetworkManager — 변하는 네트워크를 위한 정책 엔진

### 8.1 직관

networkd가 "이 포트엔 이 주소" 같은 **고정 배선**에 강하다면, NetworkManager(NM)는 **상황이 바뀌는 네트워크**에 강하다: Wi-Fi AP를 찾아 붙고 비밀번호를 관리하고(wpa_supplicant를 D-Bus로 조종), LTE 모뎀을 올리고(ModemManager를 D-Bus로 조종), 우선순위에 따라 이더넷 → Wi-Fi → LTE로 넘어가고(autoconnect-priority, route metric), VPN을 붙인다. 연결 설정 단위를 **connection profile**이라 부르고, 장치(device)에 프로필을 "activate"한다. 노트북·데스크톱 Ubuntu, Jetson의 JetPack 이미지가 기본으로 NM을 쓴다.

```svg
<svg viewBox="0 0 680 350" xmlns="http://www.w3.org/2000/svg">
<defs><marker id="ah5" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto-start-reverse"><path d="M0,0 L10,5 L0,10 z" fill="currentColor"/></marker></defs>
<text x="165" y="20" font-size="13" text-anchor="middle" font-weight="bold">systemd-networkd</text>
<text x="505" y="20" font-size="13" text-anchor="middle" font-weight="bold">NetworkManager</text>
<rect x="20" y="30" width="290" height="44" rx="5" fill="#888" fill-opacity="0.12" stroke="#888"/>
<text x="165" y="48" font-size="12" text-anchor="middle">/etc/systemd/network/*.network</text>
<text x="165" y="66" font-size="12" text-anchor="middle">*.netdev · *.link (udev가 읽음)</text>
<rect x="360" y="30" width="300" height="44" rx="5" fill="#888" fill-opacity="0.12" stroke="#888"/>
<text x="510" y="48" font-size="12" text-anchor="middle">system-connections/*.nmconnection</text>
<text x="510" y="66" font-size="12" text-anchor="middle">(Ubuntu: netplan YAML → 생성)</text>
<line x1="165" y1="76" x2="165" y2="100" stroke="currentColor" stroke-width="1.3" marker-end="url(#ah5)"/>
<line x1="510" y1="76" x2="510" y2="100" stroke="currentColor" stroke-width="1.3" marker-end="url(#ah5)"/>
<rect x="20" y="102" width="290" height="56" rx="5" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
<text x="165" y="124" font-size="12" text-anchor="middle">networkd (약 1.7 MB 바이너리)</text>
<text x="165" y="142" font-size="12" text-anchor="middle">DHCP·IPv4LL·RA 클라이언트 내장</text>
<rect x="360" y="102" width="300" height="56" rx="5" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
<text x="510" y="124" font-size="12" text-anchor="middle">NetworkManager (약 3.5 MB 바이너리)</text>
<text x="510" y="142" font-size="12" text-anchor="middle">프로필 · 우선순위 · 플러그인 · polkit</text>
<line x1="165" y1="160" x2="165" y2="246" stroke="currentColor" stroke-width="1.3" marker-end="url(#ah5)"/>
<text x="175" y="200" font-size="12">rtnetlink</text>
<rect x="360" y="176" width="140" height="44" rx="5" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
<text x="430" y="194" font-size="12" text-anchor="middle">wpa_supplicant</text>
<text x="430" y="212" font-size="12" text-anchor="middle">Wi-Fi (D-Bus)</text>
<rect x="520" y="176" width="140" height="44" rx="5" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
<text x="590" y="194" font-size="12" text-anchor="middle">ModemManager</text>
<text x="590" y="212" font-size="12" text-anchor="middle">LTE/5G (D-Bus)</text>
<line x1="430" y1="160" x2="430" y2="174" stroke="currentColor" stroke-width="1.3" marker-end="url(#ah5)"/>
<line x1="590" y1="160" x2="590" y2="174" stroke="currentColor" stroke-width="1.3" marker-end="url(#ah5)"/>
<line x1="640" y1="160" x2="640" y2="246" stroke="currentColor" stroke-width="1.3" marker-end="url(#ah5)"/>
<text x="612" y="240" font-size="12" text-anchor="end">rtnetlink</text>
<rect x="20" y="248" width="640" height="34" rx="5" fill="#888" fill-opacity="0.10" stroke="#888"/>
<text x="340" y="270" font-size="12" text-anchor="middle">커널: net_device · 주소 · 라우트 · nl80211(Wi-Fi) · cdc_mbim/qmi_wwan(모뎀)</text>
<rect x="20" y="294" width="640" height="44" rx="5" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
<text x="340" y="312" font-size="12" text-anchor="middle">둘 다 D-Bus API를 내놓고, DNS는 resolved에게 D-Bus로 넘긴다</text>
<text x="340" y="330" font-size="12" text-anchor="middle">networkd: org.freedesktop.network1 · NM: org.freedesktop.NetworkManager</text>
</svg>
```
그림 6 — networkd와 NetworkManager의 구조. networkd는 파일 → 데몬 → rtnetlink로 단순하다. NM은 프로필을 관리하고 Wi-Fi·모뎀은 별도 데몬에 D-Bus로 위임한다. 바이너리 크기는 이 이미지에서 잰 값(`ls -l`)이다.

### 8.2 예제 17 — networkd를 끄고 NM을 켜면: "unmanaged"

무엇을 확인하나: 같은 게스트에서 networkd를 끄고(이 복사본 한정) NM으로 바꾼다.

```sh
systemctl disable --now systemd-networkd.service systemd-networkd.socket systemd-networkd-wait-online.service 2>&1 | tail -2
ip link del svc.100; ip link del br0; ip addr flush dev enp0s2
systemctl enable --now NetworkManager 2>&1 | tail -1; sleep 15
nmcli --version; nmcli general status; nmcli device status
ls /usr/lib/NetworkManager/conf.d/ /etc/NetworkManager/conf.d/
cat /usr/lib/NetworkManager/conf.d/*.conf | grep -v '^#' | grep -v '^$'
```

```text
Disabling 'systemd-networkd.service', but its triggering units are still active:
systemd-networkd.socket
Created symlink /etc/systemd/system/network-online.target.wants/NetworkManager-wait-online.service → /usr/lib/systemd/system/NetworkManager-wait-online.service.
nmcli tool, version 1.46.0
STATE         CONNECTIVITY  WIFI-HW  WIFI     WWAN-HW  WWAN    
disconnected  unknown       missing  enabled  missing  enabled 
DEVICE  TYPE      STATE      CONNECTION 
enp0s1  ethernet  unmanaged  --         
enp0s2  ethernet  unmanaged  --         
lo      loopback  unmanaged  --         
/etc/NetworkManager/conf.d/:
default-wifi-powersave-on.conf

/usr/lib/NetworkManager/conf.d/:
10-default-firewall-use-iptables.conf  10-dns-resolved.conf  10-globally-managed-devices.conf
[main]
firewall-backend = iptables
[main]
dns=systemd-resolved
[keyfile]
unmanaged-devices=*,except:type:wifi,except:type:gsm,except:type:cdma
```

출력에서 볼 것: NM을 켰는데 **이더넷이 전부 `unmanaged`**다. Ubuntu의 NM 패키지는 `10-globally-managed-devices.conf`로 "Wi-Fi와 모뎀만 관리하고 이더넷은 손대지 마라"를 기본값으로 둔다 — Ubuntu Server는 이더넷을 netplan → networkd로 관리한다는 전제다. Ubuntu 기반 장비 이미지에서 "NM을 깔았는데 아무것도 안 한다"의 정체가 이것이다. 또 NM을 enable하자 `NetworkManager-wait-online.service`가 network-online.target에 걸렸다(7.4절 함정).

`/etc` 쪽 드롭인으로 덮는다:

```sh
printf '[keyfile]\nunmanaged-devices=none\n' > /etc/NetworkManager/conf.d/90-l3-manage-ethernet.conf
systemctl restart NetworkManager; sleep 15
nmcli device status
nmcli -f NAME,UUID,TYPE,DEVICE,FILENAME connection show
```

```text
DEVICE  TYPE      STATE                   CONNECTION         
enp0s2  ethernet  connected               Wired connection 2 
enp0s1  ethernet  connected (externally)  enp0s1             
lo      loopback  connected (externally)  lo                 
NAME                UUID                                  TYPE      DEVICE  FILENAME                                                               
Wired connection 2  31d63d0f-23ed-3a2b-9459-b84881c92746  ethernet  enp0s2  /run/NetworkManager/system-connections/Wired connection 2.nmconnection 
enp0s1              0d403546-8b0f-40d9-b01d-3a483da54275  ethernet  enp0s1  /run/NetworkManager/system-connections/enp0s1.nmconnection             
lo                  e263ae4f-5519-47ac-a7a4-325680168035  loopback  lo      /run/NetworkManager/system-connections/lo.nmconnection                 
Wired connection 1  6e1ee536-056d-32af-833c-8e74892d7cec  ethernet  --      /run/NetworkManager/system-connections/Wired connection 1.nmconnection 
```

출력에서 볼 것: NM이 프로필 없는 이더넷마다 **"Wired connection N"이라는 자동 DHCP 프로필**을 메모리(`/run`)에 만들었다. enp0s1은 `connected (externally)`다: NM이 만들지 않은 기존 설정(networkd가 붙여 둔 IPv6 주소 등)이 링크에 남아 있어서, NM이 그것을 "외부에서 한 설정"으로 보고 그대로 둔 것이다. 장비에서는 자동 프로필이 서비스 포트에 DHCP를 걸어 5.1절과 같은 문제를 만든다. `[main] no-auto-default=*`로 끄는 것이 보통이다.

### 8.3 예제 18 — nmcli로 정적 프로필 만들기, 그리고 keyfile은 어디에

무엇을 확인하나: 업링크 DHCP, 서비스 포트 정적 IP(게이트웨이 금지) 프로필을 만들고 저장 위치를 본다.

```sh
nmcli con add type ethernet ifname enp0s1 con-name uplink ipv4.method auto ipv6.method disabled connection.autoconnect-priority 10
nmcli con add type ethernet ifname enp0s2 con-name svc-port ipv4.method manual \
  ipv4.addresses 192.168.77.10/24 ipv4.never-default yes ipv6.method disabled
nmcli con up uplink; nmcli con up svc-port; sleep 3
nmcli device status
```

```text
Error: Failed to add 'uplink' connection: Timeout was reached
Error: Failed to add 'svc-port' connection: Timeout was reached
Connection successfully activated (D-Bus active path: /org/freedesktop/NetworkManager/ActiveConnection/4)
Connection successfully activated (D-Bus active path: /org/freedesktop/NetworkManager/ActiveConnection/5)
DEVICE  TYPE      STATE                   CONNECTION 
enp0s1  ethernet  connected               uplink     
enp0s2  ethernet  connected               svc-port   
lo      loopback  connected (externally)  lo         
```

`add`가 "Timeout was reached"로 실패했다고 하는데 `up`은 성공했다. `nmcli`는 NM에 D-Bus `AddConnection` 메서드를 보내고 기본 25초 안에 응답을 기다리는데, 그 사이 NM이 **netplan으로 설정을 저장하느라** TCG에서 25초를 넘겼다. 실제로는 저장됐다:

```sh
nmcli -f NAME,DEVICE,FILENAME con show --active
ls -l /etc/NetworkManager/system-connections/ /etc/netplan/ /run/NetworkManager/system-connections/
```

```text
NAME      DEVICE  FILENAME                                                                                            
uplink    enp0s1  /run/NetworkManager/system-connections/netplan-NM-26902121-67cf-42aa-b3d8-932879f6fd37.nmconnection 
svc-port  enp0s2  /run/NetworkManager/system-connections/netplan-NM-30d288b4-26fd-4636-94e4-de4b6922ba57.nmconnection 
lo        lo      /run/NetworkManager/system-connections/lo.nmconnection                                              
/etc/NetworkManager/system-connections/:
total 0

/etc/netplan/:
total 8
-rw------- 1 root root 508 Oct 18 04:41 90-NM-26902121-67cf-42aa-b3d8-932879f6fd37.yaml
-rw------- 1 root root 525 Oct 18 04:42 90-NM-30d288b4-26fd-4636-94e4-de4b6922ba57.yaml

/run/NetworkManager/system-connections/:
total 20
-rw------- 1 root root 272 Oct 18 04:40 'Wired connection 1.nmconnection'
-rw------- 1 root root 272 Oct 18 04:40 'Wired connection 2.nmconnection'
-rw------- 1 root root 314 Oct 18 04:40  lo.nmconnection
-rw------- 1 root root 317 Oct 18 04:42  netplan-NM-26902121-67cf-42aa-b3d8-932879f6fd37.nmconnection
-rw------- 1 root root 342 Oct 18 04:42  netplan-NM-30d288b4-26fd-4636-94e4-de4b6922ba57.nmconnection
```

**Ubuntu 24.04의 NM은 프로필을 `/etc/NetworkManager/system-connections/`가 아니라 netplan YAML(`/etc/netplan/90-NM-<uuid>.yaml`)로 저장**하고, 그 YAML에서 keyfile을 `/run`에 생성해 읽는다. 업스트림 NM(Fedora, Yocto, Buildroot)은 keyfile을 `/etc/NetworkManager/system-connections/<이름>.nmconnection`에 바로 쓴다. 두 파일을 다 보자:

```sh
cat /etc/netplan/90-NM-30d288b4-26fd-4636-94e4-de4b6922ba57.yaml
cat /run/NetworkManager/system-connections/netplan-NM-30d288b4-26fd-4636-94e4-de4b6922ba57.nmconnection
ip -4 -br addr; ip route
```

```text
network:
  version: 2
  ethernets:
    NM-30d288b4-26fd-4636-94e4-de4b6922ba57:
      renderer: NetworkManager
      match:
        name: "enp0s2"
      addresses:
      - "192.168.77.10/24"
      wakeonlan: true
      networkmanager:
        uuid: "30d288b4-26fd-4636-94e4-de4b6922ba57"
        name: "svc-port"
        passthrough:
          ethernet._: ""
          ipv4.never-default: "true"
          ipv6.addr-gen-mode: "default"
          ipv6.method: "disabled"
          ipv6.ip6-privacy: "-1"
          proxy._: ""
----
[connection]
id=svc-port
type=ethernet
uuid=30d288b4-26fd-4636-94e4-de4b6922ba57
interface-name=enp0s2

[ethernet]
wake-on-lan=1

[ipv4]
method=manual
address1=192.168.77.10/24
#Netplan: passthrough setting
never-default=true

[ipv6]
#Netplan: passthrough override
method=disabled
#Netplan: passthrough setting
addr-gen-mode=default

[proxy]
----
lo               UNKNOWN        127.0.0.1/8 
enp0s1           UP             10.0.2.15/24 
enp0s2           UP             192.168.77.10/24 
default via 10.0.2.2 dev enp0s1 proto dhcp src 10.0.2.15 metric 101 
10.0.2.0/24 dev enp0s1 proto kernel scope link src 10.0.2.15 metric 101 
192.168.77.0/24 dev enp0s2 proto kernel scope link src 192.168.77.10 metric 102 
```

(`----`는 두 `cat` 사이에 넣은 구분선이다.) 출력에서 볼 것:

- keyfile은 INI 형식이고 섹션 이름이 nmcli 속성 이름(`ipv4.method` → `[ipv4] method=`)과 1:1이다. 파일 권한은 `600` — Wi-Fi 비밀번호(`[wifi-security] psk=`)가 평문으로 들어가기 때문이다. 공장에서 프로필을 미리 굽는다면 이 파일을 이미지에 넣고 권한을 600으로 맞춘다(아니면 NM이 무시한다).
- netplan이 모르는 NM 속성은 `passthrough`로 YAML에 보존된다.
- 라우트 metric이 101/102: NM은 장치 종류와 순서로 metric을 매긴다(이더넷 100번대, Wi-Fi 600번대). `never-default`로 서비스 포트에는 기본 라우트가 없다.

**netplan** 짧게: Ubuntu의 네트워크 설정 "앞단"이다. `/etc/netplan/*.yaml`을 쓰고 `netplan apply`하면 `renderer:`에 따라 networkd용 `.network` 파일(`/run/systemd/network/`) 또는 NM용 keyfile(`/run/NetworkManager/`)을 생성한다. Ubuntu/Jetson 장비에서는 "netplan YAML이 원본, 나머지는 생성물"이라는 사실을 잊지 말자 — `/run`의 생성 파일을 고쳐 봐야 재부팅하면 사라진다. 자체 이미지(Yocto/Buildroot)에는 보통 netplan이 없다.

### 8.4 예제 19 — NM도 D-Bus 서비스다

무엇을 확인하나: nmcli가 하는 일이 전부 D-Bus 호출이라는 것, 그리고 프로필을 껐다 켤 때 버스에 흐르는 메시지.

```sh
busctl tree org.freedesktop.NetworkManager --no-pager | head -24
busctl get-property org.freedesktop.NetworkManager /org/freedesktop/NetworkManager org.freedesktop.NetworkManager Version State
busctl get-property org.freedesktop.NetworkManager /org/freedesktop/NetworkManager/Devices/3 org.freedesktop.NetworkManager.Device Interface State
(timeout 20 busctl monitor org.freedesktop.NetworkManager > /tmp/mon4.txt 2>&1 &); sleep 2
nmcli con down svc-port; nmcli con up svc-port; sleep 3
awk '/Member=/{for(k=1;k<=NF;k++) if($k ~ /^(Interface|Member)=/) printf "%s ", $k; print ""}' /tmp/mon4.txt \
  | sed 's/org.freedesktop.NetworkManager/NM/g; s/Interface=//; s/Member=//' | sort | uniq -c | sort -rn | head -12
```

```text
└─ /org
  └─ /org/freedesktop
    └─ /org/freedesktop/NetworkManager
      ├─ /org/freedesktop/NetworkManager/ActiveConnection
      │ ├─ /org/freedesktop/NetworkManager/ActiveConnection/1
      │ ├─ /org/freedesktop/NetworkManager/ActiveConnection/4
      │ └─ /org/freedesktop/NetworkManager/ActiveConnection/5
      ├─ /org/freedesktop/NetworkManager/AgentManager
      ├─ /org/freedesktop/NetworkManager/DHCP4Config
      │ └─ /org/freedesktop/NetworkManager/DHCP4Config/2
      ├─ /org/freedesktop/NetworkManager/Devices
      │ ├─ /org/freedesktop/NetworkManager/Devices/1
      │ ├─ /org/freedesktop/NetworkManager/Devices/2
      │ └─ /org/freedesktop/NetworkManager/Devices/3
      ├─ /org/freedesktop/NetworkManager/DnsManager
      ...
      └─ /org/freedesktop/NetworkManager/Settings
s "1.46.0"
u 70
s "enp0s2"
u 100
Connection 'svc-port' successfully deactivated (D-Bus active path: /org/freedesktop/NetworkManager/ActiveConnection/5)
Connection successfully activated (D-Bus active path: /org/freedesktop/NetworkManager/ActiveConnection/7)
    118 org.freedesktop.DBus.Properties PropertiesChanged 
     16 NM.Device StateChanged 
     11 org.freedesktop.nm_dispatcher Action2 
     10 NM.Settings.Connection GetSettings 
      8 NM.Connection.Active StateChanged 
      4 org.freedesktop.resolve1.Manager SetLinkMulticastDNS 
      4 org.freedesktop.resolve1.Manager SetLinkLLMNR 
      4 org.freedesktop.resolve1.Manager SetLinkDomains 
      4 org.freedesktop.resolve1.Manager SetLinkDefaultRoute 
      4 org.freedesktop.resolve1.Manager SetLinkDNS 
      4 org.freedesktop.resolve1.Manager SetLinkDNSOverTLS 
      3 org.freedesktop.DBus.ObjectManager InterfacesRemoved 
```

출력에서 볼 것:

- 트리의 `IP4Config`/`IP6Config` 객체 8줄은 `...`로 생략했다. 연결마다 IP 설정 객체가 따로 있다.
- `State u 70` = `NM_STATE_CONNECTED_GLOBAL`, 장치 `State u 100` = `NM_DEVICE_STATE_ACTIVATED`. NM의 상태 값은 libnm 헤더(`nm-dbus-interface.h`)의 enum이다. 웹 UI나 헬스체크는 이 숫자를 D-Bus로 읽거나 `StateChanged` 시그널을 구독한다.
- 프로필 하나를 껐다 켜는 데 메시지가 170개 넘게 흘렀다. 그중 **NM이 resolved에게 `SetLinkDNS` 등을 D-Bus로 호출**하고(DNS 설정을 넘김), **`nm_dispatcher`에게 `Action2`**(연결 이벤트마다 `/etc/NetworkManager/dispatcher.d/` 스크립트 실행)를 보낸다. 데몬끼리 D-Bus로 협업하는 실제 모습이다.
- dispatcher 스크립트는 "링크가 올라오면 무엇을 하라"를 붙이는 공식 확장점이다(networkd 쪽은 `networkd-dispatcher` 별도 패키지, 또는 D-Bus 시그널 구독).

메모리: 이 게스트에서 잰 상주 메모리(RSS)는 NetworkManager **18,452 KB**, systemd-networkd **8,900 KB**였다(`ps -o rss`; NM은 프로필 3개 활성, networkd는 링크 3개). 패키지 설치 크기는 `network-manager` 8,288 KB + `libnm0` 1,661 KB + netplan 관련 790 KB이고, networkd는 이미 깔린 `systemd` 패키지 안에 들어 있다.

### 8.5 networkd vs NetworkManager — 제품에서 고르기

| 기준 | systemd-networkd | NetworkManager |
|---|---|---|
| 설정 모델 | 선언형 파일, 매치 규칙 | 프로필 + 자동 연결 정책 |
| Wi-Fi | wpa_supplicant를 따로 설정해 붙여야 함 (iwd도 가능) | wpa_supplicant/iwd를 직접 조종, 스캔·로밍·비밀 관리 |
| 셀룰러 | 직접 지원 없음 | ModemManager 연동 (APN, SIM PIN) |
| 여러 업링크 전환 | 라우트 metric을 손으로 설계 | 우선순위·연결성 검사로 자동 |
| VPN | WireGuard(.netdev) | WireGuard, OpenVPN 등 플러그인 |
| 크기·의존성 | systemd에 포함, 작다 | 별도 패키지, glib·libnm·polkit 등 |
| 부팅 동작 | 단순·결정적 | 정책이 많아 경우의 수가 많다 |
| CAN, VLAN, bridge, bond | `.netdev`로 깔끔 (CAN은 I10 10.2절) | 지원하지만 CAN은 약함 |
| API | D-Bus `org.freedesktop.network1` (+ varlink) | 풍부한 D-Bus API, libnm, nmcli |
| 런타임 변경 | 파일 수정 + `networkctl reload` | nmcli/D-Bus로 즉시, 영속 저장 |

```svg
<svg viewBox="0 0 680 330" xmlns="http://www.w3.org/2000/svg">
<defs><marker id="ah6" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto-start-reverse"><path d="M0,0 L10,5 L0,10 z" fill="currentColor"/></marker></defs>
<rect x="210" y="10" width="260" height="34" rx="6" fill="#888" fill-opacity="0.12" stroke="#888"/>
<text x="340" y="32" font-size="13" text-anchor="middle" font-weight="bold">제품의 네트워크는 어떤가?</text>
<line x1="340" y1="46" x2="340" y2="66" stroke="currentColor" stroke-width="1.3" marker-end="url(#ah6)"/>
<rect x="150" y="68" width="380" height="40" rx="6" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
<text x="340" y="86" font-size="12" text-anchor="middle">Wi-Fi 접속·로밍, LTE 모뎀, 업링크 자동 전환,</text>
<text x="340" y="102" font-size="12" text-anchor="middle">사용자가 UI에서 네트워크를 고르는가?</text>
<line x1="150" y1="88" x2="96" y2="134" stroke="currentColor" stroke-width="1.3" marker-end="url(#ah6)"/>
<text x="92" y="116" font-size="12" text-anchor="end">예</text>
<line x1="530" y1="88" x2="584" y2="134" stroke="currentColor" stroke-width="1.3" marker-end="url(#ah6)"/>
<text x="588" y="116" font-size="12">아니오</text>
<rect x="20" y="136" width="200" height="64" rx="6" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
<text x="120" y="158" font-size="13" text-anchor="middle" font-weight="bold">NetworkManager</text>
<text x="120" y="176" font-size="12" text-anchor="middle">+ ModemManager</text>
<text x="120" y="192" font-size="12" text-anchor="middle">+ wpa_supplicant</text>
<rect x="460" y="136" width="200" height="64" rx="6" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
<text x="560" y="158" font-size="13" text-anchor="middle" font-weight="bold">systemd-networkd</text>
<text x="560" y="176" font-size="12" text-anchor="middle">고정 배선·정적 IP</text>
<text x="560" y="192" font-size="12" text-anchor="middle">VLAN·브리지·CAN</text>
<rect x="240" y="136" width="200" height="64" rx="6" fill="#888" fill-opacity="0.12" stroke="#888"/>
<text x="340" y="158" font-size="13" text-anchor="middle" font-weight="bold">BSP 기본값 따르기?</text>
<text x="340" y="176" font-size="12" text-anchor="middle">JetPack·Ubuntu = NM</text>
<text x="340" y="192" font-size="12" text-anchor="middle">Yocto 최소 = networkd</text>
<rect x="20" y="220" width="640" height="96" rx="6" fill="#d0564a" fill-opacity="0.08" stroke="#d0564a"/>
<text x="32" y="240" font-size="12" font-weight="bold">어느 쪽이든 지킬 것</text>
<text x="32" y="260" font-size="12">· 한 링크는 한 데몬만 관리 (NM이면 networkd 링크를 unmanaged로, 또는 그 반대)</text>
<text x="32" y="280" font-size="12">· 센서·서비스 포트: 정적 IP, 게이트웨이 없음, RequiredForOnline=no / never-default</text>
<text x="32" y="300" font-size="12">· 혼용 가능: 고정 포트는 networkd, Wi-Fi/LTE만 NM (unmanaged-devices로 경계를 긋는다)</text>
</svg>
```
그림 7 — 선택 차트. "변하는 네트워크"가 있으면 NM, 고정 배선뿐이면 networkd. BSP가 이미 하나를 쓰고 있으면 바꿀 이유가 확실할 때만 바꾼다. 둘을 섞을 때는 링크 단위로 경계를 명확히 긋는다.

---

## 9. 두 장비를 정적 IP로 잇기 · 서비스 포트 · 링크로컬

### 9.1 예제 20 — 게스트 두 대를 QEMU socket 네트워크로 연결

무엇을 확인하나: 실제 장비 두 대(예: 센서 키트와 정비 노트북, 또는 메인 컴퓨터와 LiDAR 처리 보드)를 케이블 하나로 직결하고 정적 IP로 통신하는 장면. QEMU의 `-netdev socket`은 두 VM의 NIC를 TCP 연결 하나로 이어 주는 "가상 크로스 케이블"이다.

```sh
# VM 쪽 — 게스트 W (listen), 게스트 P (connect). 각자 이미지 복사본, 업링크는 user 네트워크
qemu-system-aarch64 ... -drive file=l3_w.ext4,if=virtio,format=raw \
  -netdev user,id=n0 -device virtio-net-pci,netdev=n0 \
  -netdev socket,id=n1,listen=127.0.0.1:41330 -device virtio-net-pci,netdev=n1,mac=52:54:00:77:00:01 ...
qemu-system-aarch64 ... -drive file=l3_p.ext4,if=virtio,format=raw \
  -netdev user,id=n0 -device virtio-net-pci,netdev=n0 \
  -netdev socket,id=n1,connect=127.0.0.1:41330 -device virtio-net-pci,netdev=n1,mac=52:54:00:77:00:02 ...
```

게스트 P의 서비스 포트 설정 — 이름이 아니라 **MAC으로 매치**했다(이름 규칙이 바뀌어도 안전):

```text
# P: /etc/systemd/network/20-svc.network
[Match]
MACAddress=52:54:00:77:00:02

[Network]
Address=192.168.77.2/24

[Link]
RequiredForOnline=no
```

W는 실행 중에 바꿨다:

```sh
# 게스트 W
printf '[Match]\nName=enp0s2\n\n[Network]\nAddress=192.168.77.1/24\n\n[Link]\nRequiredForOnline=no\n' > /etc/systemd/network/20-svc.network
networkctl reload; networkctl reconfigure enp0s2; sleep 5
ip -4 -br addr show enp0s2; ps -o rss,comm -C systemd-networkd
```

```text
enp0s2           UP             192.168.77.1/24 
  RSS COMMAND
 8900 systemd-network
```

```sh
# 게스트 P (부팅 완료 후)
hostname; networkctl list --no-pager; ip -4 -br addr
ping -c 3 -W 2 192.168.77.1; ip neigh show dev enp0s2
```

```text
l3peer
IDX LINK   TYPE     OPERATIONAL SETUP
  1 lo     loopback carrier     unmanaged
  2 enp0s1 ether    routable    configured
  3 enp0s2 ether    routable    configured

3 links listed.
lo               UNKNOWN        127.0.0.1/8 
enp0s1           UP             10.0.2.15/24 metric 1024 
enp0s2           UP             192.168.77.2/24 
PING 192.168.77.1 (192.168.77.1) 56(84) bytes of data.
64 bytes from 192.168.77.1: icmp_seq=1 ttl=64 time=12.9 ms
64 bytes from 192.168.77.1: icmp_seq=2 ttl=64 time=3.54 ms
64 bytes from 192.168.77.1: icmp_seq=3 ttl=64 time=2.96 ms

--- 192.168.77.1 ping statistics ---
3 packets transmitted, 3 received, 0% packet loss, time 2009ms
rtt min/avg/max/mdev = 2.957/6.477/12.935/4.572 ms
192.168.77.1 lladdr 52:54:00:77:00:01 REACHABLE 
```

출력에서 볼 것: 두 장비가 게이트웨이도 DHCP도 없이 정적 IP로 통신한다. 첫 핑이 12.9 ms로 긴 것은 ARP 해석(첫 패킷 전에 상대 MAC을 물음) 때문이고, `ip neigh`에 상대 MAC `52:54:00:77:00:01`이 `REACHABLE`로 남았다. RTT 3 ms는 TCG 두 대 + 호스트 TCP 소켓을 거친 값이라 실제 케이블(수십 µs)과 비교하면 안 된다.

이 게스트 P를 부팅하는 데 1분 45초(로그인 프롬프트까지)가 걸렸다는 것도 기록해 둔다. 게스트 두 대가 같은 VM에서 동시에 TCG로 돈 값이다.

### 9.2 공장·현장 서비스 포트 설계

센서 키트에 "정비용 이더넷 포트"를 둔다면 흔한 요구는 이렇다: 현장 엔지니어가 노트북을 꽂으면 **IP를 몰라도** 접속되어야 하고, 장비의 인터넷 업링크에는 영향이 없어야 하며, 공장에서는 수백 대가 같은 설정이어야 한다.

| 요구 | 설정 |
|---|---|
| 항상 같은 주소 | 정적 `Address=192.168.77.10/24` (모든 장비 동일 — 한 번에 한 대만 연결하는 직결 포트이므로 충돌 없음) |
| 업링크와 분리 | `Gateway=` 없음, DHCP 끔, `RequiredForOnline=no` |
| IP를 몰라도 접속 | `LinkLocalAddressing=ipv4` + mDNS(`MulticastDNS=yes`, resolved) 또는 IPv6 링크로컬 |
| 노트북에 주소 주기 | `DHCPServer=yes` + `[DHCPServer] PoolOffset=` (networkd 내장 DHCP 서버) |
| 공격면 줄이기 | 서비스 포트에서만 SSH/진단 포트 허용 (nftables, L6) |
| 케이블 흔들림 | `IgnoreCarrierLoss=3s` |

같은 정적 IP를 모든 장비에 쓰는 것은 **직결 포트**에서만 괜찮다. 공장 라인에서 여러 대를 한 스위치에 물리면 IP 충돌이 난다 — 그때는 링크로컬(무작위 + 충돌 검사)이나 MAC 기반 주소, 공장 DHCP 서버를 쓴다.

### 9.3 예제 21 — 링크로컬 주소로 서로 찾기

무엇을 확인하나: 정적 주소를 모르는 상황을 흉내 내, 양쪽에 IPv4 링크로컬(RFC 3927, 169.254.0.0/16)을 켜고 그 주소로 통신한다.

```sh
# W와 P 양쪽에서
sed -i 's/^\[Network\]/[Network]\nLinkLocalAddressing=ipv4/' /etc/systemd/network/20-svc.network
networkctl reload; networkctl reconfigure enp0s2; sleep 12
ip -4 -br addr show enp0s2
```

```text
enp0s2           UP             169.254.34.222/16 metric 2048 192.168.77.1/24 
```

```text
enp0s2           UP             169.254.61.62/16 metric 2048 192.168.77.2/24 
```

(첫 줄이 W, 둘째 줄이 P.) W에서 P의 링크로컬 주소로:

```sh
cat /etc/systemd/network/20-svc.network
ping -c 2 -W 2 169.254.61.62
ping -c 1 -W 2 192.168.77.2 | tail -2
ip route get 169.254.61.62
```

```text
[Match]
Name=enp0s2

[Network]
LinkLocalAddressing=ipv4
Address=192.168.77.1/24

[Link]
RequiredForOnline=no
PING 169.254.61.62 (169.254.61.62) 56(84) bytes of data.
64 bytes from 169.254.61.62: icmp_seq=1 ttl=64 time=5.49 ms
64 bytes from 169.254.61.62: icmp_seq=2 ttl=64 time=3.63 ms

--- 169.254.61.62 ping statistics ---
2 packets transmitted, 2 received, 0% packet loss, time 1004ms
rtt min/avg/max/mdev = 3.631/4.560/5.490/0.929 ms
1 packets transmitted, 1 received, 0% packet loss, time 0ms
rtt min/avg/max/mdev = 1.544/1.544/1.544/0.000 ms
169.254.61.62 dev enp0s2 src 169.254.34.222 uid 0 
    cache 
```

출력에서 볼 것: 각 장비가 **스스로 고른** 169.254.x.y 주소(ARP로 충돌 검사 후 확정)로 통신했다. 정적 주소와 공존하고, 커널은 목적지에 맞는 출발 주소(`src 169.254.34.222`)를 고른다. 주소는 장비마다 다르니 사람이 직접 치기는 어렵다 — 실무에서는 mDNS(`sensorkit-1234.local`)나 IPv6 링크로컬 + 장비 라벨의 MAC으로 찾는다.

---

## 10. 시간 동기와 트러블슈팅 순서

### 10.1 예제 22 — timesyncd는 네트워크에 기대고 있다

무엇을 확인하나: SNTP 클라이언트 systemd-timesyncd의 상태와 부팅 순서상 위치.

```sh
timedatectl status --no-pager
timedatectl timesync-status --no-pager | head -12
systemctl cat systemd-timesyncd --no-pager | grep -E '^(After|Before|Wants|Requires)='
```

```text
               Local time: Sun 2026-10-18 04:37:56 UTC
           Universal time: Sun 2026-10-18 04:37:56 UTC
                 RTC time: Sun 2026-10-18 04:37:57
                Time zone: Etc/UTC (UTC, +0000)
System clock synchronized: yes
              NTP service: active
          RTC in local TZ: no
       Server: 185.125.190.57 (ntp.ubuntu.com)
Poll interval: 17min 4s (min: 32s; max 34min 8s)
         Leap: normal
      Version: 4
      Stratum: 2
    Reference: 1D586304
    Precision: 1us (-25)
Root distance: 4.408ms (max: 5s)
       Offset: -4.847ms
        Delay: 149.267ms
       Jitter: 6.856ms
 Packet count: 5
After=systemd-sysusers.service
Before=time-set.target sysinit.target shutdown.target
Wants=time-set.target
```

출력에서 볼 것: timesyncd는 **sysinit 단계에서 일찍 시작**하지만(`Before=sysinit.target`) 실제 동기화는 네트워크가 된 뒤에야 한다. QEMU user 네트워크로 인터넷의 `ntp.ubuntu.com`에 닿아 offset −4.8 ms로 맞췄다. 장비 쪽 함정:

- **RTC가 없거나 배터리가 없는 보드**는 부팅 때 시계가 1970년(또는 timesyncd가 마지막으로 저장한 `/var/lib/systemd/timesync/clock` 시각)이다. 이 상태에서 TLS 인증서 검사(OTA 서버 접속, L4)는 "아직 유효하지 않은 인증서"로 실패한다.
- 시간이 맞아야 하는 서비스는 `After=time-sync.target` + `Wants=time-sync.target`을 걸고, **`systemd-time-wait-sync.service`를 enable**해야 그 target이 실제 동기화를 기다린다(그렇지 않으면 time-sync.target은 즉시 도달한다). 이것도 network-online처럼 부팅을 붙잡을 수 있으니 같은 원칙(정말 필요한 곳만)을 적용한다.
- `timesyncd`는 SNTP 클라이언트일 뿐이다. 로봇·차량에서 센서 타임스탬프 정밀도가 필요하면 chrony나 PTP(linuxptp, Q3)를 쓴다. 이 VM 호스트(Lima Ubuntu)는 chrony를 쓰고 있다.

### 10.2 "네트워크가 안 된다" — 아래층부터

| 순서 | 질문 | 명령 | 다음 층으로 넘어가는 조건 |
|---|---|---|---|
| 1 | 장치가 있나, 드라이버는? | `ip -br link`, `dmesg`, `ethtool -i` | 인터페이스가 보인다 (I6) |
| 2 | 링크(carrier)가 올라왔나? | `ip link` 의 `LOWER_UP`, `ethtool eth0` 의 Link detected | `LOWER_UP` (PHY·케이블) |
| 3 | 누가 관리하나? | `networkctl list`, `nmcli device` | 한 데몬만 managed |
| 4 | 어떤 설정 파일이 적용됐나? | `networkctl status IF`, `nmcli -f all con show` | 의도한 파일 |
| 5 | 주소가 있나? | `ip -br addr` | 기대한 주소·prefix |
| 6 | 라우트는? 기본 라우트가 하나인가? | `ip route`, `ip route get 8.8.8.8` | 올바른 출구 |
| 7 | 이웃(L2)이 보이나? | `ping 게이트웨이`, `ip neigh` | `REACHABLE` |
| 8 | DNS는? | `resolvectl status`, `resolvectl query host` | 이름 해석 OK |
| 9 | 방화벽? | `nft list ruleset` (L6) | 허용 |
| 10 | 앱 쪽? | `ss -tulpn`, 앱 로그 | 소켓 상태 (A9) |
| 부팅만 느리다 | wait-online? | `systemd-analyze blame`, `critical-chain` | 7절 |
| 데몬 제어가 안 된다 | D-Bus 정책? | `journalctl -u dbus`, `busctl monitor` | 2절, 4.3절 |

펌웨어 디버깅과 같다: 물리 → 링크 → 프로토콜 순으로 아래층부터 확인하고, 각 층에 "통과 조건"을 정해 두면 헤매지 않는다.

---

## 11. 커널 안에서는 무슨 일이 일어나나

D-Bus와 네트워크 설정은 둘 다 유저 공간 데몬이지만, 바닥에서 쓰는 커널 기능은 몇 개 안 된다: **AF_UNIX 소켓 + 자격 증명**(D-Bus), **rtnetlink**(네트워크 설정), **net_device 등록**(드라이버).

### 11.1 예제 23 — D-Bus 메시지를 strace로 보기

무엇을 확인하나: `busctl get-property` 한 번이 커널에서 어떤 syscall이 되는지.

```sh
strace -f -e trace=socket,connect,sendmsg,recvmsg,writev,getsockopt -s 40 \
  busctl get-property org.freedesktop.network1 /org/freedesktop/network1 org.freedesktop.network1.Manager OperationalState 2>&1 \
  | cut -c1-150 | head -24
```

```text
socket(AF_UNIX, SOCK_STREAM|SOCK_CLOEXEC|SOCK_NONBLOCK, 0) = 3
getsockopt(3, SOL_SOCKET, SO_RCVBUF, [212992], [4]) = 0
getsockopt(3, SOL_SOCKET, SO_RCVBUF, [8388608], [4]) = 0
getsockopt(3, SOL_SOCKET, SO_SNDBUF, [212992], [4]) = 0
getsockopt(3, SOL_SOCKET, SO_SNDBUF, [8388608], [4]) = 0
connect(3, {sa_family=AF_UNIX, sun_path="/run/dbus/system_bus_socket"}, 30) = 0
getsockopt(3, SOL_SOCKET, SO_PEERCRED, {pid=1, uid=0, gid=0}, [12]) = 0
getsockopt(3, SOL_SOCKET, SO_PEERSEC, "unconfined", [64 => 10]) = 0
getsockopt(3, SOL_SOCKET, SO_PEERGROUPS, 0xaaaaffb01310, [256 => 0]) = 0
getsockopt(3, SOL_SOCKET, SO_ACCEPTCONN, [0], [4]) = 0
sendmsg(3, {msg_name=NULL, msg_namelen=0, msg_iov=[{iov_base="\0AUTH EXTERNAL\r\nDATA\r\n", iov_len=22}, {iov_base="NEGOTIATE_UNIX_FD\r\n", iov_len=19
recvmsg(3, {msg_name=NULL, msg_namelen=0, msg_iov=[{iov_base="DATA\r\nOK 281f81b37ec5d1dc4e0598396ad4538"..., iov_len=256}], msg_iovlen=1, msg_control
sendmsg(3, {msg_name=NULL, msg_namelen=0, msg_iov=[{iov_base="l\1\0\1\0\0\0\0\1\0\0\0m\0\0\0\1\1o\0\25\0\0\0/org/freedesktop"..., iov_len=128}], msg_i
recvmsg(3, {msg_namelen=0}, MSG_DONTWAIT|MSG_CMSG_CLOEXEC) = -1 EAGAIN (Resource temporarily unavailable)
recvmsg(3, {msg_name=NULL, msg_namelen=0, msg_iov=[{iov_base="l\2\1\1\n\0\0\0\1\0\0\0=\0\0\0\6\1s\0\5\0\0\0", iov_len=24}], msg_iovlen=1, msg_controll
recvmsg(3, {msg_name=NULL, msg_namelen=0, msg_iov=[{iov_base=":1.13\0\0\0\5\1u\0\1\0\0\0\10\1g\0\1s\0\0\7\1s\0\24\0\0\0org.free"..., iov_len=66}], msg
sendmsg(3, {msg_name=NULL, msg_namelen=0, msg_iov=[{iov_base="l\1\0\1=\0\0\0\2\0\0\0\220\0\0\0\1\1o\0\31\0\0\0/org/freedesktop"..., iov_len=160}, {iov
recvmsg(3, {msg_name=NULL, msg_namelen=0, msg_iov=[{iov_base="l\4\1\1\n\0\0\0\2\0\0\0\215\0\0\0\1\1o\0\25\0\0\0", iov_len=24}], msg_iovlen=1, msg_cont
recvmsg(3, {msg_name=NULL, msg_namelen=0, msg_iov=[{iov_base="/org/freedesktop/DBus\0\0\0\2\1s\0\24\0\0\0org.free"..., iov_len=146}], msg_iovlen=1, ms
recvmsg(3, {msg_name=NULL, msg_namelen=0, msg_iov=[{iov_base="l\2\1\1\21\0\0\0>\0\0\0-\0\0\0\5\1u\0\2\0\0\0", iov_len=24}], msg_iovlen=1, msg_controll
recvmsg(3, {msg_name=NULL, msg_namelen=0, msg_iov=[{iov_base="\6\1s\0\5\0\0\0:1.13\0\0\0\10\1g\0\1v\0\0\7\1s\0\4\0\0\0:1.2\0\0\0\0"..., iov_len=57}], 
s "routable"
+++ exited with 0 +++
```

한 줄씩 읽으면 D-Bus 프로토콜 전체가 보인다:

1. `socket(AF_UNIX, SOCK_STREAM)` + `connect("/run/dbus/system_bus_socket")` — A7의 UNIX stream socket 그대로다.
2. `getsockopt(SO_PEERCRED)` = `{pid=1, uid=0}`: 클라이언트도 상대의 자격 증명을 확인한다. pid가 1인 이유는 3.1절 — 소켓을 만든 것이 systemd다. 같은 호출을 **버스 데몬이 클라이언트 쪽에 대해** 해서 uid/pid를 얻고, 그것으로 정책을 검사한다. `SO_PEERSEC`는 LSM 레이블(L6)이다.
3. `"\0AUTH EXTERNAL\r\n..."`: 첫 바이트 `\0`은 자격 증명 바이트, `AUTH EXTERNAL`은 "내 신원은 커널이 보증하는 소켓 자격 증명으로 대신한다"는 SASL 메커니즘이다. `NEGOTIATE_UNIX_FD`로 fd 전달(`h` 타입, `SCM_RIGHTS`)을 협상한다.
4. 메시지 헤더 첫 4바이트: `l`(little endian) · 타입(`\1` METHOD_CALL, `\2` METHOD_RETURN, `\4` SIGNAL) · 플래그 · 프로토콜 버전 1. 첫 메시지는 `Hello()`이고 응답으로 unique name `:1.13`을 받는다. 그다음 시그널(`\4`, `NameAcquired`)이 오고, `Properties.Get` 호출과 그 응답이 이어진다. 응답 끝에 보낸 쪽 `:1.2`(networkd)가 보인다.

즉 버스 데몬은 커널 입장에서 **UNIX 소켓을 많이 가진 평범한 프로세스**다. 커널 안에 D-Bus가 있는 것이 아니다(커널 내 D-Bus를 넣으려던 kdbus/bus1 시도는 메인라인에 들어가지 못했다). 그래서 메시지 하나가 클라이언트 → 데몬 → 서비스로 **두 번 복사되고 컨텍스트 스위치가 두 번** 일어난다 — A7에서 D-Bus를 데이터 경로로 쓰지 말라고 한 이유다.

```svg
<svg viewBox="0 0 680 300" xmlns="http://www.w3.org/2000/svg">
<defs><marker id="ah7" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto-start-reverse"><path d="M0,0 L10,5 L0,10 z" fill="currentColor"/></marker></defs>
<rect x="20" y="20" width="140" height="40" rx="5" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
<text x="90" y="44" font-size="12" text-anchor="middle">nmcli · busctl</text>
<rect x="200" y="20" width="140" height="40" rx="5" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
<text x="270" y="44" font-size="12" text-anchor="middle">dbus-daemon</text>
<rect x="380" y="20" width="140" height="40" rx="5" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
<text x="450" y="44" font-size="12" text-anchor="middle">networkd · NM</text>
<rect x="540" y="20" width="120" height="40" rx="5" fill="#888" fill-opacity="0.12" stroke="#888"/>
<text x="600" y="44" font-size="12" text-anchor="middle">ip (iproute2)</text>
<line x1="20" y1="90" x2="660" y2="90" stroke="currentColor" stroke-dasharray="5 4"/>
<text x="24" y="84" font-size="12">user</text>
<text x="24" y="106" font-size="12">kernel</text>
<rect x="20" y="116" width="320" height="60" rx="5" fill="#888" fill-opacity="0.10" stroke="#888"/>
<text x="180" y="138" font-size="12" text-anchor="middle">AF_UNIX: unix_stream_sendmsg()</text>
<text x="180" y="158" font-size="12" text-anchor="middle">SO_PEERCRED = sk_peer_pid/cred (connect 시점)</text>
<rect x="380" y="116" width="280" height="60" rx="5" fill="#888" fill-opacity="0.10" stroke="#888"/>
<text x="520" y="138" font-size="12" text-anchor="middle">NETLINK_ROUTE: rtnetlink_rcv_msg()</text>
<text x="520" y="158" font-size="10.5" text-anchor="middle">RTM_NEWADDR · RTM_NEWROUTE · RTM_NEWLINK</text>
<line x1="90" y1="62" x2="90" y2="114" stroke="currentColor" stroke-width="1.3" marker-end="url(#ah7)"/>
<line x1="270" y1="114" x2="270" y2="62" stroke="currentColor" stroke-width="1.3" marker-end="url(#ah7)"/>
<line x1="450" y1="62" x2="450" y2="114" stroke="currentColor" stroke-width="1.3" marker-end="url(#ah7)"/>
<line x1="600" y1="62" x2="600" y2="114" stroke="currentColor" stroke-width="1.3" marker-end="url(#ah7)"/>
<rect x="380" y="196" width="280" height="44" rx="5" fill="#3f9a6b" fill-opacity="0.10" stroke="#3f9a6b"/>
<text x="520" y="216" font-size="12" text-anchor="middle">net_device · 주소 · 라우트(FIB)</text>
<text x="520" y="232" font-size="12" text-anchor="middle">dev_open() → 드라이버 ndo_open (I6)</text>
<line x1="520" y1="178" x2="520" y2="194" stroke="currentColor" stroke-width="1.3" marker-end="url(#ah7)"/>
<rect x="20" y="196" width="320" height="44" rx="5" fill="#d0564a" fill-opacity="0.10" stroke="#d0564a"/>
<text x="180" y="216" font-size="12" text-anchor="middle">드라이버: register_netdev() → RTM_NEWLINK</text>
<text x="180" y="232" font-size="12" text-anchor="middle">netif_carrier_on/off() → 링크 이벤트</text>
<line x1="342" y1="218" x2="378" y2="218" stroke="currentColor" stroke-width="1.3" marker-end="url(#ah7)"/>
<text x="340" y="270" font-size="12" text-anchor="middle">rtnetlink 멀티캐스트 그룹(RTNLGRP_LINK 등)으로 커널 → 데몬 이벤트가 올라간다</text>
<text x="340" y="290" font-size="12" text-anchor="middle">D-Bus는 순수 유저 공간 프로토콜 — 커널에는 AF_UNIX 소켓만 보인다</text>
</svg>
```
그림 8 — 커널에서 보면. D-Bus는 AF_UNIX 소켓 위의 유저 공간 프로토콜이고, 네트워크 데몬은 rtnetlink로 커널 객체(`net_device`, 주소, 라우트)를 바꾼다. 드라이버의 `register_netdev()`와 `netif_carrier_on/off()`가 rtnetlink 이벤트가 되어 데몬에 도착한다.

### 11.2 드라이버를 쓸 때 다시 만나는 곳

- **`netif_carrier_on()/off()`** (I6): 드라이버가 PHY 링크 상태를 커널에 알리는 함수. 이것이 `IFF_LOWER_UP` → rtnetlink `RTM_NEWLINK` 이벤트 → networkd의 "Gained carrier" → D-Bus `CarrierState` `PropertiesChanged` → 웹 UI까지 이어진다. 드라이버가 carrier를 잘못 보고하면(PHY 인터럽트 누락) 위의 모든 층이 틀린 상태를 보인다. 7절의 wait-online 지연도 "carrier가 늦게 올라옴"이 원인인 경우가 많다.
- **`register_netdev()` 순서** (I6 1.2절): 등록 직후 udev(`.link` 이름 변경) → networkd/NM이 곧바로 `ndo_open`을 부를 수 있다. probe가 덜 끝난 상태에서 등록하면 설정 데몬이 레이스를 일으킨다.
- **`.link`의 `Name=`과 `NamePolicy=`**: udev가 `RTM_SETLINK`(IFLA_IFNAME)로 이름을 바꾼다. 링크가 up이면 이름 변경이 실패한다(`-EBUSY`) — 그래서 이름은 networkd가 링크를 올리기 전에 정해져야 한다(L2).
- **커널 config**: VLAN(`CONFIG_VLAN_8021Q`), 브리지(`CONFIG_BRIDGE`), `CONFIG_IPV6`, `CONFIG_VETH`, `CONFIG_WIREGUARD` 등이 모듈이면 데몬보다 먼저 로드돼야 한다. 6.2절의 `Operation not supported`가 그 증상이다(K3/K4에서 이미지에 넣을 config를 정할 때 다시 본다).
- 드라이버가 D-Bus를 직접 쓰는 일은 없다. 드라이버는 sysfs·netlink·uevent로 유저 공간에 알리고, **D-Bus로 번역하는 것은 데몬의 일**이다(udev → systemd → logind, rtnetlink → networkd, nl80211 → wpa_supplicant). 4절의 `l3-devmgr`이 바로 그런 번역 데몬이다: 아래로는 IIO/V4L2 장치 파일(I2, I5), 위로는 D-Bus API.

---

## 12. Embedded 실무에서 다시 보기

### 장면 1 — 센서 키트의 제어 API를 D-Bus로

카메라·IMU 파이프라인을 관리하는 `sensorkitd`가 있고, 웹 UI·진단 CLI·OTA 에이전트가 그것을 제어해야 한다. 설계:

- 인터페이스를 **XML로 먼저 고정**(`com.acme.SensorKit1`): 메서드 `SetMode(s)`, `StartCalibration() → o`(잡 객체 경로), 프로퍼티 `Mode`, `Temperature`, `FrameRate`, 시그널 `Fault(us)`. 오래 걸리는 동작은 systemd처럼 "잡 객체 + 완료 시그널"(3.5절).
- 권한: 정책 파일로 읽기는 모두, 제어는 `sensorkit-ops` 그룹. 웹 UI 백엔드 계정만 그 그룹에.
- 데이터는 D-Bus에 싣지 않는다. 프레임은 dmabuf fd를 `h` 타입으로 한 번 넘기고 이후는 공유 메모리, 텔레메트리는 UNIX socket/shm 링(A7).
- `Type=dbus` 유닛 + `Restart=on-failure` + watchdog(L1). 클라이언트는 well-known name으로 구독해 재시작에 견딘다.

### 장면 2 — "가끔 부팅이 2분 걸린다"

필드 리포트: 장비 몇 대가 부팅 후 2분 동안 클라우드에 안 붙는다. `systemd-analyze blame` 1위가 `systemd-networkd-wait-online` 2min. 원인: LTE 모뎀을 뺀 SKU에서도 `wwan0`용 `.network`가 남아 있어 RequiredForOnline 대상이었다(7.2절 재현과 같은 구조). 처방: SKU별 `.network`를 정리하고 업링크 후보만 `--any`로, 업로더는 `Wants=network-online.target`을 빼고 재시도 로직으로.

### 장면 3 — Jetson(Ubuntu) 이미지에서 고정 IP

JetPack 기반 장비는 NM이 기본이다. LiDAR 포트 `eth1`을 고정 IP로 박으려면 nmcli로 프로필을 만들되(8.3절), Ubuntu에선 저장 위치가 netplan일 수 있음을 확인하고, 이미지 빌드 단계에서는 keyfile(600 권한)이나 netplan YAML을 이미지에 넣는다. LiDAR 포트에는 `ipv4.never-default yes`, MTU 9000이 필요하면 `802-3-ethernet.mtu 9000`. 자동 프로필이 LiDAR 포트에 DHCP를 걸지 않게 `no-auto-default=*`.

### 장면 4 — 정비 포트와 공장 라인

정비 포트는 `192.168.77.10/24` 정적 + IPv4LL + networkd 내장 DHCP 서버(노트북에 주소를 줌), nftables로 SSH는 이 포트에서만. 공장 라인에서는 장비가 스위치 하나에 여러 대 물리므로 정적 IP를 쓰지 않고 공장 DHCP + MAC 화이트리스트, 또는 링크로컬 + mDNS 이름(`sk-<serial>.local`)으로 찾는다. 9절의 실험이 이 두 경우다.

---

## 13. 흔한 실수와 증상

| 실수 | 증상 | 원인 | 고치는 법 |
|---|---|---|---|
| D-Bus 정책 파일을 패키지에 안 넣음 | 서비스 시작 실패, `AccessDenied ... not allowed to own the service` | 시스템 버스는 `deny own="*"`가 기본 | `system.d/<name>.conf`에 `<allow own>` (4.3절) |
| 유닛의 `User=`만 바꿈 | 위와 같은 AccessDenied | 정책의 `policy user=`와 불일치 | 정책도 같이 바꾼다 |
| 정책 거부를 서비스 로그에서 찾음 | 로그에 아무것도 없음 | 거부는 버스 데몬이 함 | `journalctl -u dbus`, `busctl monitor` |
| 시그널을 unique name으로 구독 | 서비스 재시작 후 이벤트가 안 옴 | unique name은 재시작마다 바뀜 | well-known name으로 구독, `NameOwnerChanged` 처리 |
| 호출 후에 구독 | 첫 이벤트를 놓침 | 서비스가 응답 전에 시그널을 보냄 (4.5절) | 구독 먼저, 호출 나중 |
| 센서 데이터를 D-Bus로 | CPU 사용률 급증, 지연 | 데몬 경유 복사 2번 + 마샬링 | D-Bus는 제어만, 데이터는 shm/fd/socket |
| 모든 포트에 DHCP (`Name=e*`) | 기본 라우트 여러 개, 트래픽이 엉뚱한 포트로 | 서비스 포트도 게이트웨이를 받음 (5.1절) | 포트별 `.network`, `Gateway` 없음, `never-default` |
| RequiredForOnline 기본값 방치 | 부팅 2분 지연, wait-online failed | 주소를 못 받는 링크를 기다림 (7.2절) | `RequiredForOnline=no`, `--any`, 앱 재시도 |
| networkd와 NM 동시 관리 | 주소가 생겼다 사라짐, 두 wait-online | 두 데몬이 같은 링크를 덮어씀 | 링크마다 한 데몬, `unmanaged-devices` |
| Ubuntu에서 NM을 켬 | 이더넷이 `unmanaged` | `10-globally-managed-devices.conf` (8.2절) | `/etc/NetworkManager/conf.d/`로 덮기 |
| VLAN/브리지 모듈 미로드 | `netdev could not be created: Operation not supported` | `=m` 모듈이 없거나 못 찾음 (6.2절) | `modules-load.d`, built-in, `depmod` 확인 |
| RTC 없는 보드에서 바로 TLS | 부팅 직후 OTA 접속 실패 (인증서 not yet valid) | 시계가 과거 | `time-sync.target` + `systemd-time-wait-sync` |

---

## 14. 면접에서 이렇게 말한다

**Q.** "How would you expose device control from a daemon to other processes?"

**A.** 시스템 서비스라면 D-Bus 시스템 버스에 well-known name으로 올린다. 인터페이스를 introspection XML로 먼저 정의하고(메서드·프로퍼티·시그널, 버전 붙인 이름), 정책 파일로 소유는 서비스 계정만, 제어는 특정 그룹만 허용한다. systemd 유닛은 `Type=dbus`, 필요하면 activation. 오래 걸리는 동작은 즉시 잡 객체를 돌려주고 완료는 시그널로. 대용량 데이터는 D-Bus에 싣지 않고 fd 전달이나 공유 메모리로 따로 보낸다. D-Bus가 과하면 SEQPACKET UNIX 소켓 + `SO_PEERCRED`로 직접 짠다.

> I'd put it on the D-Bus system bus under a well-known name, with the interface defined up front in introspection XML — versioned, with methods for commands, properties for state, and signals for events. A policy file lets only the service account own the name and only an operators group call the control methods; reads can be open. The unit is Type=dbus, optionally bus-activated. Long operations return a job object right away and signal completion. Bulk data like frames never goes over the bus — I pass an fd or use shared memory. If D-Bus is too heavy for the product, a SEQPACKET UNIX socket with SO_PEERCRED checks does the same job with less machinery.

**꼬리 질문**: "Why not just a UNIX socket?" → 발견(이름), 타입 있는 직렬화, 브로드캐스트, 정책 한 곳, 표준 도구(busctl)를 공짜로 얻는다. 대신 데몬 경유 비용과 의존성이 생긴다.

**Q.** "Your boot hangs for two minutes at network-online.target. Why, and how do you fix it?"

**A.** 누군가 `Wants=network-online.target`을 했고, 그래서 끌려온 wait-online이 필수 링크 중 하나가 online이 되기를 기본 120초 동안 기다린 것이다. 보통 DHCP 서버가 없는 포트나 케이블이 없는 포트가 `RequiredForOnline` 대상으로 남아 있다. `systemd-analyze blame`과 `critical-chain`, `networkctl list`로 확인하고, 그 링크에 `RequiredForOnline=no`를 주거나 wait-online을 `--any`/`--interface`로 좁힌다. 근본적으로는 앱이 online을 기다리지 않고 재시도하게 만든다. 실제로 재현해 보니 wait-online이 정확히 2분 1초였다.

> Something pulled in network-online.target, which brings in systemd-networkd-wait-online, and that waits — up to 120 seconds by default — for every link marked RequiredForOnline to come up. Usually it's a port with no DHCP server or no cable. I'd confirm with systemd-analyze blame and critical-chain plus networkctl, then either set RequiredForOnline=no on that link or narrow wait-online with --any or --interface. The real fix is that applications shouldn't block on "online" at all — the network drops in the field anyway, so they should retry with backoff. When I reproduced it, wait-online took exactly two minutes and the target was still reached afterwards, so the symptom is a slow boot, not a failed one.

**Q.** "systemd-networkd or NetworkManager on a product?"

**A.** 고정 배선(센서 포트, VLAN, 브리지, 정적 IP)만 있으면 networkd — 작고, systemd에 들어 있고, 선언형이라 결정적이다. Wi-Fi 스캔·로밍, LTE 모뎀, 업링크 자동 전환, 사용자가 UI에서 네트워크를 고르는 제품이면 NM(+ wpa_supplicant, ModemManager). BSP가 이미 하나(Jetson/Ubuntu는 NM)를 쓰면 이유 없이 바꾸지 않는다. 섞을 수도 있지만 링크마다 관리 데몬을 하나로 정한다.

> If the network is fixed wiring — sensor ports, VLANs, a bridge, static addresses — networkd: it's small, already part of systemd, declarative and predictable. If the product has Wi-Fi scanning and roaming, a cellular modem, automatic uplink failover, or users picking networks in a UI, NetworkManager with wpa_supplicant and ModemManager. If the BSP already ships one — Jetson's Ubuntu uses NetworkManager — I don't switch without a reason. You can mix them, but each link gets exactly one owner, enforced with unmanaged-devices.

**Q.** "A service fails to start with 'not allowed to own the service'. What's going on?"

**A.** 시스템 버스는 기본이 이름 소유 거부다. 서비스 이름에 대한 `<allow own>` 정책이 없거나, 정책의 `user=`와 유닛의 `User=`가 다르다. root라도 마찬가지다. 정책 파일을 `/usr/share/dbus-1/system.d/`(패키지) 또는 `/etc/dbus-1/system.d/`에 넣으면 dbus-daemon이 다시 읽는다.

> The system bus denies name ownership by default, even to root. Either the package didn't install a policy file with an allow-own rule for that name, or the unit's User= doesn't match the policy's user. Drop the conf into system.d — dbus-daemon picks it up — and check journalctl -u dbus for the denial.

**Q.** "How do you set a static IP on an embedded Linux device?"

**A.** 일회성은 `ip addr add`, 영속 설정은 데몬으로. networkd면 `[Match]`(이름보다 MAC이나 Path) + `[Network] Address=`, 게이트웨이는 업링크에만, `RequiredForOnline=no`. NM이면 `nmcli con add ... ipv4.method manual ipv4.addresses ... ipv4.never-default yes`, keyfile을 이미지에 넣을 땐 600 권한. Ubuntu면 netplan이 원본일 수 있다는 점을 확인한다.

> For a one-off, ip addr add. For something persistent, the network daemon: with networkd a .network file matched by MAC or path, Address= on the link, a gateway only on the uplink, and RequiredForOnline=no for service ports. With NetworkManager, nmcli con add with ipv4.method manual and never-default, or a keyfile with mode 600 baked into the image. On Ubuntu I check whether netplan is the source of truth, because NetworkManager there stores profiles as netplan YAML.

**Q.** "What's the difference between network.target and network-online.target?"

**A.** `network.target`은 "네트워크 관리 데몬이 시작됨"일 뿐 주소가 있다는 뜻이 아니다 — 주로 종료 순서용이다. `network-online.target`은 wait-online이 설정 완료를 확인했다는 뜻이고, 원하는 유닛이 있을 때만 활성화된다.

> network.target only means the network management stack has been started — mostly useful for shutdown ordering. network-online.target means a wait-online service has confirmed links are configured, and it's only pulled into the boot when some unit asks for it.

**Q.** "Signals vs properties vs methods — when do you use each?"

**A.** 메서드는 명령(응답 있음), 프로퍼티는 현재 상태(읽기, 바뀌면 `PropertiesChanged`), 시그널은 상태로 표현 안 되는 이벤트(폴트 발생, 버튼 눌림). 상태를 시그널로만 알리면 늦게 붙은 클라이언트가 현재 값을 모르니, 상태는 프로퍼티 + 변경 시그널로 둔다.

> Methods for commands that need a reply, properties for current state — readable any time and announced with PropertiesChanged — and custom signals for events that aren't state, like a fault or a button press. If state is only ever sent as signals, a client that connects late can't learn the current value, so state belongs in properties.

---

## 15. 직접 해보기

1. **개념**: unique name과 well-known name의 차이를 쓰고, 서비스가 재시작할 때 클라이언트 코드에서 무엇이 깨질 수 있는지 한 문장으로 설명하라.
   정답/힌트: unique name은 연결마다 새로 받는다. sender를 unique name으로 고정한 match rule이나 캐시한 이름이 재시작 후 무효가 된다.

2. **busctl 탐험**: `busctl tree org.freedesktop.login1`로 세션 객체를 찾고 `busctl introspect`로 `org.freedesktop.login1.Manager`의 `Reboot` 메서드 시그니처를 확인하라. 왜 이 메서드는 일반 사용자에게도 열려 있는데 실제로는 거부될 수 있나?
   정답/힌트: 정책은 통과하고 logind가 polkit에 묻는다(2.2절의 두 층). 시그니처는 `b`(interactive).

3. **정책 연습**: 4.4절 정책을 바꿔, `l3ops` 그룹은 `SetMode("calib")`를 못 하게 하고 싶다. 정책 파일로 가능한가? 안 되면 어디서 막나?
   정답/힌트: 정책은 인자 값을 못 본다(인터페이스·멤버·경로까지). 서비스 안에서 `GetConnectionUnixUser`/polkit으로 검사하거나, 메서드를 `StartCalibration`으로 분리해 `send_member`로 막는다.

4. **코드 과제**: `l3-devmgr`에 1초마다 `Temperature`를 바꾸고 `PropertiesChanged`를 보내는 타이머를 추가하라(GLib `g_timeout_add`를 ctypes로, 또는 python3-gi가 있는 환경에서). `busctl monitor`로 확인하라.
   정답/힌트: ctypes 콜백은 `ctypes.CFUNCTYPE(ctypes.c_int, ctypes.c_void_p)`로 만들고 참조를 전역에 유지해야 GC되지 않는다.

5. **networkd 과제**: 서비스 포트에 networkd 내장 DHCP 서버를 켜서(`DHCPServer=yes`, `[DHCPServer] PoolOffset=100 PoolSize=20`) 9.1절의 상대 게스트가 DHCP로 주소를 받게 하라. 받은 리스를 `networkctl status`에서 확인하라.
   정답/힌트: 서버 쪽에 정적 `Address=`가 있어야 한다. 클라이언트 쪽은 `DHCP=ipv4`.

6. **부팅 지연 관찰**: 7.2절 설정에서 wait-online을 `--any`로 바꾸는 드롭인(`systemctl edit systemd-networkd-wait-online`)을 만들고 `systemd-analyze critical-chain`으로 효과를 재라. 그다음 `--timeout=10`만 준 경우와 비교하라.
   정답/힌트: 드롭인에서 `ExecStart=`를 먼저 비워야 한다. `--any`는 업링크가 되는 순간 끝나고, `--timeout`은 10초 뒤 실패로 끝난다.

7. **strace로 보기**: `strace -f -e trace=sendmsg,recvmsg -x nmcli general status`를 실행해 첫 메시지 헤더의 타입 바이트를 찾아라. NM이 꺼져 있을 때는 무엇이 돌아오나?
   정답/힌트: `l\1...` METHOD_CALL, 응답 `l\2` 또는 `l\3`(ERROR). NM이 없으면 activation 파일이 없으므로 `ServiceUnknown` 에러가 데몬에서 온다.

---

## 16. 용어 사전

| 용어 | 뜻 | 한 줄 설명 |
|---|---|---|
| D-Bus | 메시지 버스 IPC | 버스 데몬을 통해 이름으로 메시지를 주고받는 표준 |
| system bus | 시스템 전체 버스 | `/run/dbus/system_bus_socket`, 기본 거부 정책 |
| session bus | 사용자 세션 버스 | 데스크톱 로그인용, 헤드리스 장비엔 없음 |
| unique name | `:1.42` | 연결마다 데몬이 주는 일회성 이름 |
| well-known name | `org.freedesktop.network1` | 서비스가 소유하는 고정 이름 |
| object path | `/org/.../link/_32` | 연결 안의 객체 주소 |
| interface | 멤버 묶음 | 메서드·시그널·프로퍼티의 이름공간 |
| signature | 타입 문자열 | `s`, `a{sv}`, `(tt)` 등 |
| introspection | 자기 기술 | `Introspect()`가 돌려주는 XML |
| match rule | 구독 필터 | 시그널을 받을 조건을 데몬에 등록 |
| activation | 지연 시작 | 이름으로 메시지가 오면 서비스를 띄움 |
| dbus-broker | 대체 버스 구현 | activation을 systemd에 위임, Fedora 기본 |
| polkit | 권한 판정 데몬 | 서비스가 "이 동작 허용?"을 묻는 곳 |
| sd-bus | systemd의 D-Bus 라이브러리 | libsystemd, vtable로 객체 등록 |
| rtnetlink | `NETLINK_ROUTE` | 링크·주소·라우트를 설정하는 커널 소켓 |
| `.link` | udev 링크 설정 | 이름·MAC·MTU |
| `.netdev` | 가상 장치 정의 | bridge, vlan, bond, wireguard |
| `.network` | L3 설정 | DHCP, 정적 주소, 라우트, DNS |
| RequiredForOnline | online 판정 포함 여부 | wait-online이 기다리는 조건 |
| network-online.target | "온라인" 동기 지점 | 원하는 유닛이 있을 때만 활성 |
| connection profile | NM 설정 단위 | keyfile `.nmconnection`으로 저장 |
| keyfile | NM 설정 파일 형식 | INI, 권한 600 |
| netplan | Ubuntu 설정 앞단 | YAML → networkd/NM 설정 생성 |
| IPv4LL | 링크로컬 주소 | 169.254.0.0/16, 스스로 고르고 충돌 검사 |
| stub resolver | resolved의 127.0.0.53 | 앱 DNS 질의를 받아 링크별 서버로 |
| SNTP | 간이 NTP | timesyncd가 쓰는 시간 동기 |

---

## 17. 요약 & 체크리스트

D-Bus는 UNIX 소켓 위에 버스 데몬을 두어 **이름으로 찾고, 타입 있는 메서드를 부르고, 상태를 프로퍼티로 읽고, 이벤트를 시그널로 받는** 시스템 제어 평면이다. 시스템 버스는 기본이 거부라서 서비스마다 정책 파일로 이름 소유와 호출 권한을 열어야 하고(빠지면 root도 `AccessDenied`), activation으로 필요할 때 서비스를 띄울 수 있다. systemd·logind·networkd·resolved·NetworkManager가 모두 이 API를 내놓고, `busctl`로 코드 없이 탐색·호출·감시할 수 있다. 네트워크 설정 데몬은 rtnetlink로 커널의 링크·주소·라우트를 맞추는 상주 루프다. systemd-networkd는 `.link`/`.netdev`/`.network` 파일로 고정 배선(정적 IP, VLAN, 브리지)을 결정적으로 설정하고, NetworkManager는 프로필과 정책으로 Wi-Fi·LTE·업링크 전환을 다룬다. `network-online.target`은 wait-online이 필수 링크를 기다리는 동기 지점이라, 주소를 못 받는 링크 하나가 부팅을 120초 붙잡는다 — 직접 재 보니 정확히 2분 1초였고, `RequiredForOnline=no`로 사라졌다. 센서·서비스 포트는 정적 IP·게이트웨이 없음·online 판정 제외가 기본이고, 앱은 online을 기다리기보다 재시도해야 한다.

- [ ] system bus와 session bus, unique name과 well-known name의 차이를 설명할 수 있다
- [ ] 객체 경로·인터페이스·메서드·시그널·프로퍼티와 타입 시그니처(`a{sv}`, `(tt)`, `h`)를 읽을 수 있다
- [ ] `busctl list/tree/introspect/call/get-property/monitor`로 systemd와 networkd를 탐색할 수 있다
- [ ] 시스템 버스의 기본 거부 정책과 정책 파일 구조를 설명하고, "not allowed to own" 에러를 고칠 수 있다
- [ ] python3-dbus나 sd-bus로 메서드·프로퍼티·시그널을 가진 서비스와 클라이언트를 짤 수 있다
- [ ] `Type=dbus` 유닛과 activation 파일로 서비스를 필요할 때 띄울 수 있다
- [ ] `ip link/addr/route`로 상태를 읽고, 기본 라우트가 여러 개인 문제를 찾아낼 수 있다
- [ ] networkd로 DHCP·정적 IP·VLAN·브리지를 설정하고 `networkctl status`로 적용 파일을 확인할 수 있다
- [ ] `network-online.target`이 부팅을 2분 붙잡는 이유를 `systemd-analyze`로 증명하고 고칠 수 있다
- [ ] NM을 켜서 nmcli로 정적 프로필을 만들고 keyfile/netplan 저장 위치를 설명할 수 있다
- [ ] 제품에 networkd와 NM 중 무엇을 쓸지 근거를 대고 고를 수 있다
- [ ] 네트워크 장애를 장치 → carrier → 관리 데몬 → 주소 → 라우트 → 이웃 → DNS 순서로 좁혀 갈 수 있다

## 참고 자료

- [D-Bus Specification](https://dbus.freedesktop.org/doc/dbus-specification.html) — 메시지 형식, 타입 시그니처, 이름 규칙, 인증
- `dbus-daemon(1)` — 정책 파일 문법(`<policy>`, `<allow>`, `<deny>`)과 activation: [man7.org dbus-daemon(1)](https://man7.org/linux/man-pages/man1/dbus-daemon.1.html)
- `busctl(1)`: [man7.org busctl(1)](https://man7.org/linux/man-pages/man1/busctl.1.html)
- `sd-bus(3)`, `sd_bus_call_method(3)`, `sd_bus_add_object_vtable(3)` — systemd man 페이지 (freedesktop.org/software/systemd/man)
- Lennart Poettering, "The new sd-bus API of systemd" (0pointer.net 블로그, 2015)
- `systemd.network(5)`, `systemd.netdev(5)`, `systemd.link(5)`, `networkctl(1)`, `systemd-networkd-wait-online.service(8)` — systemd man 페이지
- systemd 문서 "Running Services After the Network Is Up": [systemd.io/NETWORK_ONLINE](https://systemd.io/NETWORK_ONLINE/)
- `systemd.special(7)` — `network.target`, `network-online.target`, `time-sync.target`
- NetworkManager 문서: `nmcli(1)`, `nm-settings-keyfile(5)`, `NetworkManager.conf(5)` — [networkmanager.dev](https://networkmanager.dev/docs/)
- netplan 문서: [netplan.readthedocs.io](https://netplan.readthedocs.io/)
- `ip(8)`, `ip-link(8)`, `ip-address(8)`, `ip-route(8)` (iproute2), `rtnetlink(7)`, `unix(7)`(SO_PEERCRED) — man7.org
- Michael Kerrisk, "The Linux Programming Interface" — 57장(UNIX domain sockets), 61장(소켓 옵션)
- 커널 소스: `net/unix/af_unix.c`, `net/core/rtnetlink.c`, `net/8021q/`, `net/bridge/` — elixir.bootlin.com
- 이전 노트: A7(IPC, D-Bus 미리보기), A9(netlink), I6(net_device, carrier), I10(CAN의 networkd 설정), K6(Ubuntu rootfs, machine-id), L1(systemd), L2(udev, `.link`), L6(nftables, LSM), L8(netns, veth)
