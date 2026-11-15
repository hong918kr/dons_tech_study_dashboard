#!/usr/bin/env python3
# ENV: linux
"""ELX-B12 libclosure (practice = solution + 추가 연습 P1~P4): ldd 없이 ELF만 읽어서 동적 의존성 closure(DT_NEEDED 전이 폐포 + 인터프리터)를
구하고, 그 파일들을 staging rootfs에 '보드에서 보일 경로' 그대로 복사한다.

  python3 -I practice.py test                                 # 테스트 (VM: gcc로 픽스처를 만들어 실제 ldd와 비교)
  python3 -I practice.py show  [--from DIR]... BIN...         # closure를 ldd 비슷하게 출력
  python3 -I practice.py stage ROOT [--from DIR]... BIN...    # closure를 ROOT 아래 같은 타깃 경로로 복사

경로에는 두 세계가 있다. 타깃 경로 = 보드에서 보이는 경로(/lib/libc.so.6), 호스트 경로 = 빌드 PC의 실제 파일.
roots = [DIR, ...] 를 앞에서부터 보며 DIR + 타깃경로 가 있는 첫 파일을 쓴다 (예: [rootfs, staging, sysroot]).
ldd 를 쓰지 않는 이유: ldd 는 바이너리를 (로더로) 실행한다 -> 크로스 타깃에는 못 쓰고, 신뢰 못 할 파일엔 위험하다.
"""
import filecmp
import os
import shutil
import struct
import subprocess
import sys
import tempfile

# `ld-linux-aarch64.so.1 --help` 의 system search path (Ubuntu glibc 2.43, ld.so.cache 없는 rootfs 기준)
DEFAULT_DIRS = ["/lib/aarch64-linux-gnu", "/usr/lib/aarch64-linux-gnu", "/lib", "/usr/lib"]
ARMHF_DIRS = ["/lib/arm-linux-gnueabihf", "/usr/lib/arm-linux-gnueabihf", "/lib", "/usr/lib"]

PT_LOAD, PT_DYNAMIC, PT_INTERP = 1, 2, 3
DT_NULL, DT_NEEDED, DT_STRTAB, DT_SONAME, DT_RPATH, DT_RUNPATH = 0, 1, 5, 14, 15, 29


class Elf:
    def __init__(self, path):
        self.path = path        # 호스트 경로
        self.bits = 0           # 32 / 64 (EI_CLASS)
        self.machine = 0        # e_machine: 183 AArch64, 40 ARM, 62 x86-64
        self.interp = None      # PT_INTERP 문자열. 정적 실행 파일 / 공유 라이브러리는 None
        self.needed = []        # DT_NEEDED, 파일에 적힌 순서
        self.soname = None      # DT_SONAME
        self.rpath = []         # DT_RPATH  (':' 로 나눔, $ORIGIN 은 아직 안 바꿈)
        self.runpath = []       # DT_RUNPATH


def read_elf(path):
    """ELF 헤더와 프로그램 헤더를 읽고 PT_INTERP + 동적 섹션을 채운 Elf. ELF 가 아니면 ValueError."""
    with open(path, "rb") as f:
        data = f.read()
    if len(data) < 52 or data[:4] != b"\x7fELF":
        raise ValueError(f"{path}: not an ELF file")
    if data[4] not in (1, 2) or data[5] != 1:
        raise ValueError(f"{path}: only little-endian ELF32/ELF64")
    e = Elf(path)
    e.bits = 32 if data[4] == 1 else 64
    (e.machine,) = struct.unpack_from("<H", data, 18)
    if e.bits == 64:
        (phoff,) = struct.unpack_from("<Q", data, 32)
        phentsize, phnum = struct.unpack_from("<HH", data, 54)
        fmt, idx = "<IIQQQQQQ", (0, 2, 3, 5)      # type flags OFFSET VADDR paddr FILESZ memsz align
    else:
        (phoff,) = struct.unpack_from("<I", data, 28)
        phentsize, phnum = struct.unpack_from("<HH", data, 42)
        fmt, idx = "<IIIIIIII", (0, 1, 2, 4)      # type OFFSET VADDR paddr FILESZ memsz flags align
    phdrs = []                                    # (p_type, p_offset, p_vaddr, p_filesz)
    for i in range(phnum):
        off = phoff + i * phentsize
        if off + struct.calcsize(fmt) > len(data):
            raise ValueError(f"{path}: truncated program headers")
        ph = struct.unpack_from(fmt, data, off)
        phdrs.append(tuple(ph[k] for k in idx))
    for p_type, off, _va, size in phdrs:
        if p_type == PT_INTERP:
            e.interp = data[off:off + size].split(b"\0", 1)[0].decode()
    parse_dynamic(e, data, phdrs)
    return e


# ---------------- Part A: ELF 동적 섹션 읽기 ----------------
def vaddr_to_offset(phdrs, vaddr):
    """가상 주소 -> 파일 오프셋. 그 주소를 덮는 PT_LOAD 를 찾는다."""
    for p_type, off, va, size in phdrs:
        if p_type == PT_LOAD and va <= vaddr < va + size:
            return off + (vaddr - va)
    raise ValueError(f"vaddr {vaddr:#x} not in any PT_LOAD")


def parse_dynamic(e, data, phdrs):
    """TODO 1: PT_DYNAMIC 을 (d_tag, d_val) 로 읽고 DT_STRTAB(가상 주소!) 기준 문자열로 needed/soname/rpath/runpath 를 채운다."""
    dyn = [p for p in phdrs if p[0] == PT_DYNAMIC]
    if not dyn:
        return                                    # 정적 실행 파일: 동적 섹션 없음
    _t, off, _va, size = dyn[0]
    ent = "<qQ" if e.bits == 64 else "<iI"
    step = struct.calcsize(ent)
    entries = []
    for pos in range(off, off + size - step + 1, step):
        tag, val = struct.unpack_from(ent, data, pos)
        if tag == DT_NULL:
            break
        entries.append((tag, val))
    strtab = [v for t, v in entries if t == DT_STRTAB]
    if not strtab:
        raise ValueError(f"{e.path}: PT_DYNAMIC without DT_STRTAB")
    base = vaddr_to_offset(phdrs, strtab[0])

    def s(o):
        end = data.index(b"\0", base + o)         # NUL 이 없으면 ValueError
        return data[base + o:end].decode()
    for tag, val in entries:
        if tag == DT_NEEDED:
            e.needed.append(s(val))
        elif tag == DT_SONAME:
            e.soname = s(val)
        elif tag == DT_RPATH:
            e.rpath = s(val).split(":")
        elif tag == DT_RUNPATH:
            e.runpath = s(val).split(":")


# ---------------- Part B: 로더의 탐색 순서 ----------------
def expand_origin(entry, obj_target):
    """$ORIGIN / ${ORIGIN} = 그 RPATH/RUNPATH 를 가진 객체가 있는 디렉터리(타깃 경로 기준)."""
    origin = os.path.dirname(obj_target)
    return entry.replace("${ORIGIN}", origin).replace("$ORIGIN", origin)


def search_dirs(chain, default_dirs):
    """TODO 2: chain[0] = 이 NEEDED 를 요구한 객체, chain[1] = 그것을 처음 로드시킨 객체, ..., chain[-1] = 실행 파일.
    각 원소는 (타깃 경로, Elf). glibc 순서: [요구자에 RUNPATH 가 없을 때만] chain 의 RPATH 들 -> 요구자의 RUNPATH -> 기본 경로.
    RUNPATH 가 있는 객체의 RPATH 는 없는 것으로 친다. LD_LIBRARY_PATH 와 ld.so.cache 는 rootfs 빌드에선 안 쓴다."""
    dirs = []
    req_target, req = chain[0]
    if not req.runpath:
        for target, obj in chain:
            if not obj.runpath:
                dirs += [expand_origin(d, target) for d in obj.rpath if d]
    dirs += [expand_origin(d, req_target) for d in req.runpath if d]
    dirs += default_dirs
    return [os.path.normpath(d) for d in dirs]


# ---------------- Part C: closure 와 staging ----------------
def find(target, roots):
    """타깃 경로를 roots 앞에서부터 찾아 호스트 경로를 돌려준다 (없으면 None)."""
    for r in roots:
        host = os.path.join(r, target.lstrip("/"))
        if os.path.isfile(host):
            return host
    return None


def closure(bins, roots, default_dirs=DEFAULT_DIRS):
    """TODO 3: 실행 파일들(타깃 경로)의 closure.
    반환 {"interps": [인터프리터 타깃 경로], "libs": {NEEDED 이름: 타깃 경로}, "missing": [못 찾은 이름],
          "parent": {NEEDED 이름: 그것을 처음 요구한 객체의 타깃 경로}}
    규칙: BFS, 이미 로드된 이름(또는 SONAME)은 건너뜀(순환 OK), 인터프리터의 SONAME 은 처음부터 로드된 것으로 침,
    실행 파일과 class/machine 이 다른 후보는 건너뛰고 다음 디렉터리를 본다(glibc 와 같음)."""
    res = {"interps": [], "libs": {}, "missing": [], "parent": {}}
    for b in bins:
        host = find(b, roots)
        if host is None:
            raise FileNotFoundError(b)
        exe = read_elf(host)
        loaded = {}                               # 이 프로세스에서 이미 로드된 이름 -> 타깃 경로
        if exe.interp:
            if exe.interp not in res["interps"]:
                res["interps"].append(exe.interp)
            ih = find(exe.interp, roots)
            if ih is None:
                res["missing"].append(exe.interp)
            else:
                loaded[read_elf(ih).soname or os.path.basename(exe.interp)] = exe.interp
        chain_of = {b: [(b, exe)]}
        queue = [(b, exe)]
        while queue:
            target, obj = queue.pop(0)
            for name in obj.needed:
                if name in loaded:
                    continue
                cands = [name] if "/" in name else [os.path.join(d, name) for d in search_dirs(chain_of[target], default_dirs)]
                hit = None
                for t in cands:
                    h = find(t, roots)
                    if h is None:
                        continue
                    lib = read_elf(h)
                    if (lib.bits, lib.machine) == (exe.bits, exe.machine):
                        hit = (t, lib)
                        break
                if hit is None:
                    if name not in res["missing"]:
                        res["missing"].append(name)
                    loaded[name] = None
                    continue
                t, lib = hit
                loaded[name] = t
                if lib.soname:
                    loaded.setdefault(lib.soname, t)
                res["libs"].setdefault(name, t)
                res["parent"].setdefault(name, target)
                chain_of[t] = [(t, lib)] + chain_of[target]
                queue.append((t, lib))
    return res


def stage(res, root, roots):
    """TODO 4: 인터프리터 + 라이브러리를 root + 타깃경로 로 복사(심볼릭 링크는 따라가서 내용 복사, 권한 유지).
    이미 root 안의 그 파일이면 건너뛴다. 복사한 타깃 경로 목록(정렬)을 돌려준다."""
    copied = []
    for t in sorted(set(res["interps"]) | set(res["libs"].values())):
        src = find(t, roots)
        if src is None:
            continue
        dst = os.path.join(root, t.lstrip("/"))
        if os.path.exists(dst) and os.path.samefile(src, dst):
            continue
        os.makedirs(os.path.dirname(dst), exist_ok=True)
        if os.path.lexists(dst):
            os.unlink(dst)
        shutil.copy2(src, dst)                    # copy2 는 심볼릭 링크를 따라가 내용을 복사한다
        copied.append(t)
    return copied


# ======================= 추가 연습 (practice) =======================
def why(res, name):
    """P1: '이 라이브러리는 누가 끌고 왔나' -> [name, 요구자 타깃 경로, 그 요구자의 요구자, ..., 실행 파일]."""
    by_target = {t: n for n, t in res["libs"].items()}
    out, cur = [name], name
    while cur in res["parent"]:
        req = res["parent"][cur]
        out.append(req)
        cur = by_target.get(req)
        if cur is None or cur in out:             # 실행 파일에 도착 (또는 방어적으로 순환 차단)
            break
    return out


def rpath_lint(elf, target, roots):
    """P2: RPATH/RUNPATH 항목 점검 -> [(항목, 문제)].
    빈 항목(= 현재 디렉터리), $ORIGIN 없는 상대 경로(실행 위치에 따라 바뀜), 빌드 PC 경로 유출(/home, /tmp, /var/tmp, roots 접두사),
    타깃에 없는 디렉터리."""
    out = []
    host_prefixes = ("/home/", "/tmp/", "/var/tmp/", "/Users/") + tuple(r.rstrip("/") + "/" for r in roots if r != "/")
    for kind, entries in (("RPATH", elf.rpath), ("RUNPATH", elf.runpath)):
        for d in entries:
            if d == "":
                out.append((d, f"{kind}: empty entry = current directory"))
            elif not d.startswith("/") and "$ORIGIN" not in d and "${ORIGIN}" not in d:
                out.append((d, f"{kind}: relative path without $ORIGIN"))
            elif d.startswith(host_prefixes):
                out.append((d, f"{kind}: build-host path leaked"))
            elif not any(os.path.isdir(os.path.join(r, expand_origin(d, target).lstrip("/"))) for r in roots):
                out.append((d, f"{kind}: directory not in target"))
    return out


def size_budget(res, roots):
    """P3: 스테이징될 파일들의 크기 -> ([(bytes, 타깃 경로)] 큰 것부터, 합계)."""
    rows = []
    for t in set(res["interps"]) | set(res["libs"].values()):
        h = find(t, roots)
        if h:
            rows.append((os.path.getsize(h), t))
    rows.sort(key=lambda x: (-x[0], x[1]))
    return rows, sum(b for b, _ in rows)


def soname_links(libdir):
    """P4: ldconfig -n 처럼, SONAME 과 파일 이름이 다른 공유 라이브러리마다 SONAME -> 파일 심볼릭 링크를 만든다.
    만든 링크 이름 목록(정렬)을 돌려준다. 이미 맞는 링크는 그대로 둔다."""
    made = []
    for fn in sorted(os.listdir(libdir)):
        p = os.path.join(libdir, fn)
        if os.path.islink(p) or not os.path.isfile(p):
            continue
        try:
            so = read_elf(p).soname
        except ValueError:
            continue
        if not so or so == fn:
            continue
        link = os.path.join(libdir, so)
        if os.path.islink(link) and os.readlink(link) == fn:
            continue
        if os.path.lexists(link):
            os.unlink(link)
        os.symlink(fn, link)
        made.append(so)
    return made


# ======================= 테스트 하네스 (starter 와 동일) =======================
PASSED = FAILED = 0


def CHECK(cond, msg):
    global PASSED, FAILED
    if cond:
        PASSED += 1
        print(f"PASS: {msg}")
    else:
        FAILED += 1
        print(f"FAIL: {msg}")


def sh(cmd, cwd=None):
    r = subprocess.run(cmd, cwd=cwd, shell=True, capture_output=True, text=True)
    if r.returncode != 0:
        raise RuntimeError(f"fixture build failed: {cmd}\n{r.stderr}")
    return r.stdout


def ldd_map(host):
    """실제 ldd 결과 -> (인터프리터 or None, {이름: realpath 또는 None(not found)})."""
    out = subprocess.run(["ldd", host], capture_output=True, text=True).stdout
    interp, libs = None, {}
    for line in out.splitlines():
        line = line.strip()
        if "=>" in line:
            name, rest = (x.strip() for x in line.split("=>", 1))
            libs[name] = None if rest.startswith("not found") else os.path.realpath(rest.split(" (")[0])
        elif line.startswith("/"):
            interp = line.split(" (")[0]
    return interp, libs


def ours_map(res, roots):
    m = {n: os.path.realpath(find(t, roots)) for n, t in res["libs"].items()}
    m.update({n: None for n in res["missing"] if not n.startswith("/")})
    return m


def same_as_ldd(target, roots, label):
    host = find(target, roots)
    try:
        res = closure([target], roots)
    except Exception as ex:                       # starter 의 스텁도 하네스는 끝까지 돈다
        CHECK(False, f"{label}: closure raised {type(ex).__name__}: {ex}")
        return None
    interp, libs = ldd_map(host)
    CHECK(res["interps"] == ([interp] if interp else []), f"{label}: interpreter == ldd ({interp})")
    CHECK(ours_map(res, roots) == libs, f"{label}: {len(libs)} libs, every name->path == ldd")
    return res


FIX = r"""
set -e
P='-std=c11 -O2 -Wall -Wextra -Werror'
mkdir -p usr/bin usr/lib/foo usr/lib/cyc usr/lib/wrong opt/rp/bin opt/rp/lib arm
echo 'int bar(void){return 7;}' > bar.c
echo 'int bar(void); int foo(void){return bar()+1;}' > foo.c
echo 'int foo(void); int main(void){return foo()==8?0:1;}' > app.c
echo 'int bar(void); int main(void){return bar()==7?0:1;}' > appbar.c
echo 'int main(void){return 0;}' > m.c
gcc $P -shared -fPIC -Wl,-soname,libbar.so.1 bar.c -o usr/lib/foo/libbar.so.1
gcc $P -shared -fPIC -Wl,-soname,libfoo.so.1 -Wl,-rpath,'$ORIGIN' foo.c -Lusr/lib/foo -l:libbar.so.1 -o usr/lib/foo/libfoo.so.1
gcc $P app.c -Lusr/lib/foo -l:libfoo.so.1 -Wl,-rpath-link,usr/lib/foo -Wl,-rpath,'$ORIGIN/../lib/foo' -o usr/bin/app
gcc $P -static m.c -o usr/bin/app_static
gcc $P -shared -fPIC -Wl,-soname,libgone.so.1 bar.c -o libgone.so.1
gcc $P appbar.c -L. -l:libgone.so.1 -o usr/bin/app_missing && rm libgone.so.1
echo 'int cb(void); int ca(int n){return n?cb():1;}' > ca.c
echo 'int ca(int); int cb(void){return ca(0);}' > cb.c
gcc $P -shared -fPIC -Wl,-soname,libcyca.so.1 ca.c -o usr/lib/cyc/libcyca.so.1 -Wl,--allow-shlib-undefined
gcc $P -shared -fPIC -Wl,-soname,libcycb.so.1 -Wl,-rpath,'$ORIGIN' cb.c -Lusr/lib/cyc -l:libcyca.so.1 -o usr/lib/cyc/libcycb.so.1
gcc $P -shared -fPIC -Wl,-soname,libcyca.so.1 -Wl,-rpath,'$ORIGIN' ca.c -Lusr/lib/cyc -l:libcycb.so.1 -o usr/lib/cyc/libcyca.so.1
echo 'int ca(int); int main(void){return ca(1)==1?0:1;}' > appc.c
gcc $P appc.c -Lusr/lib/cyc -l:libcyca.so.1 -Wl,-rpath-link,usr/lib/cyc -Wl,-rpath,'$ORIGIN/../lib/cyc' -o usr/bin/app_cyc
x86_64-linux-gnu-gcc $P -shared -fPIC -Wl,-soname,libbar.so.1 bar.c -o usr/lib/wrong/libbar.so.1
gcc $P appbar.c -Lusr/lib/foo -l:libbar.so.1 -Wl,-rpath,'$ORIGIN/../lib/wrong:$ORIGIN/../lib/foo' -o usr/bin/app_wrong
echo 'int q(void){return 3;}' > q.c
echo 'int q(void); int p(void){return q();}' > p.c
echo 'int p(void); int main(void){return p()==3?0:1;}' > appp.c
gcc $P -shared -fPIC -Wl,-soname,libq.so.1 q.c -o opt/rp/lib/libq.so.1
gcc $P -shared -fPIC -Wl,-soname,libp.so.1 p.c -Lopt/rp/lib -l:libq.so.1 -o opt/rp/lib/libp.so.1
gcc $P appp.c -Lopt/rp/lib -l:libp.so.1 -Wl,-rpath-link,opt/rp/lib -Wl,--disable-new-dtags -Wl,-rpath,'$ORIGIN/../lib' -o opt/rp/bin/app_rpath
gcc $P appp.c -Lopt/rp/lib -l:libp.so.1 -Wl,-rpath-link,opt/rp/lib -Wl,--enable-new-dtags -Wl,-rpath,'$ORIGIN/../lib' -o opt/rp/bin/app_runpath
arm-linux-gnueabihf-gcc $P m.c -o arm/hello
echo 'this is not an ELF' > notelf; head -c 40 usr/bin/app > trunc
"""


def run_tests():
    T = tempfile.mkdtemp(prefix="ELX-B12_t.", dir="/tmp")
    try:
        sh(FIX, cwd=T)
        R = [T, "/"]                               # 픽스처 root 먼저, 그다음 VM 의 glibc

        # --- Part A: ELF 읽기 ---
        app = read_elf(f"{T}/usr/bin/app")
        CHECK(app.interp == "/lib/ld-linux-aarch64.so.1", "A: PT_INTERP of app")
        CHECK(app.needed == ["libfoo.so.1", "libc.so.6"], f"A: DT_NEEDED of app = {app.needed}")
        CHECK(app.runpath == ["$ORIGIN/../lib/foo"] and app.rpath == [], "A: app has DT_RUNPATH only")
        foo = read_elf(f"{T}/usr/lib/foo/libfoo.so.1")
        CHECK(foo.soname == "libfoo.so.1" and foo.needed == ["libbar.so.1"] and foo.interp is None,
              "A: libfoo SONAME, NEEDED only libbar (--as-needed), no interpreter")
        rp = read_elf(f"{T}/opt/rp/bin/app_rpath")
        CHECK(rp.rpath == ["$ORIGIN/../lib"] and rp.runpath == [], "A: --disable-new-dtags -> DT_RPATH")
        arm = read_elf(f"{T}/arm/hello")
        CHECK((arm.bits, arm.machine, arm.interp, arm.needed) == (32, 40, "/lib/ld-linux-armhf.so.3", ["libc.so.6"]),
              "A: ELF32 armhf: class/machine/interp/needed")
        st = read_elf(f"{T}/usr/bin/app_static")
        CHECK(st.interp is None and st.needed == [], "A: static binary: no PT_INTERP, no DT_NEEDED")
        for bad in ("notelf", "trunc"):
            try:
                read_elf(f"{T}/{bad}")
                CHECK(False, f"A: {bad} -> ValueError")
            except ValueError:
                CHECK(True, f"A: {bad} -> ValueError")

        # --- Part A: closure 를 실제 ldd 와 비교 ---
        res = same_as_ldd("/usr/bin/app", R, "A: app")
        if res:
            CHECK(res["parent"].get("libbar.so.1") == "/usr/lib/foo/libfoo.so.1", "A: libbar was pulled in by libfoo")
        for b in ("/usr/bin/qemu-system-aarch64", "/usr/sbin/mkfs.ext4", "/usr/bin/gpioinfo"):
            if os.path.exists(b):
                same_as_ldd(b, ["/"], f"A: VM {b}")

        # --- Part B: 경계 사례 ---
        try:
            s = closure(["/usr/bin/app_static"], R)
            CHECK(s["interps"] == [] and s["libs"] == {} and s["missing"] == [], "B: static -> empty closure")
        except Exception as ex:
            CHECK(False, f"B: static raised {ex!r}")
        res = same_as_ldd("/usr/bin/app_missing", R, "B: missing lib")
        if res:
            CHECK(res["missing"] == ["libgone.so.1"], "B: missing == [libgone.so.1]")
        same_as_ldd("/usr/bin/app_cyc", R, "B: cycle libcyca<->libcycb terminates")
        res = same_as_ldd("/usr/bin/app_wrong", R, "B: x86-64 libbar earlier in RUNPATH is skipped")
        if res:
            CHECK(res["libs"].get("libbar.so.1") == "/usr/lib/foo/libbar.so.1", "B: picked aarch64 libbar")
        res = same_as_ldd("/opt/rp/bin/app_rpath", R, "B: exe DT_RPATH is inherited by libp")
        if res:
            CHECK(res["libs"].get("libq.so.1") == "/opt/rp/lib/libq.so.1", "B: libq found via exe RPATH")
        res = same_as_ldd("/opt/rp/bin/app_runpath", R, "B: exe DT_RUNPATH is NOT inherited")
        if res:
            CHECK(res["missing"] == ["libq.so.1"], "B: libq missing with RUNPATH")
        chain = [("/usr/lib/foo/libfoo.so.1", foo), ("/usr/bin/app", app)]
        CHECK(search_dirs(chain, ["/lib"]) == ["/usr/lib/foo", "/lib"], "B: search_dirs(libfoo) = its RUNPATH then default")

        # --- Part B: 크로스 타깃 (ldd 는 'not a dynamic executable') vs qemu-arm 로더 --list ---
        try:
            ra = closure(["/hello"], [f"{T}/arm", "/usr/arm-linux-gnueabihf"], ARMHF_DIRS)
            CHECK(ra["interps"] == ["/lib/ld-linux-armhf.so.3"] and ra["libs"] == {"libc.so.6": "/lib/libc.so.6"},
                  f"B: armhf closure via sysroot = {ra['libs']}")
        except Exception as ex:
            CHECK(False, f"B: armhf closure raised {ex!r}")
        lst = subprocess.run(["qemu-arm", "-L", "/usr/arm-linux-gnueabihf",
                              "/usr/arm-linux-gnueabihf/lib/ld-linux-armhf.so.3", "--list", f"{T}/arm/hello"],
                             capture_output=True, text=True).stdout
        CHECK("libc.so.6 => /lib/libc.so.6" in lst, "B: qemu-arm ld.so --list agrees (libc.so.6 => /lib/libc.so.6)")

        # --- Part C: staging ---
        S = f"{T}/stage"
        try:
            res = closure(["/usr/bin/app", "/usr/bin/app_cyc"], R)
            copied = stage(res, S, R)
            want = sorted({"/lib/ld-linux-aarch64.so.1", "/lib/aarch64-linux-gnu/libc.so.6", "/usr/lib/foo/libfoo.so.1",
                           "/usr/lib/foo/libbar.so.1", "/usr/lib/cyc/libcyca.so.1", "/usr/lib/cyc/libcycb.so.1"})
            CHECK(copied == want, f"C: staged {len(copied)} files at target paths")
            ld = f"{S}/lib/ld-linux-aarch64.so.1"
            CHECK(os.path.isfile(ld) and not os.path.islink(ld) and os.access(ld, os.X_OK), "C: loader is a regular executable file")
            CHECK(os.path.isfile(ld) and filecmp.cmp(ld, "/lib/ld-linux-aarch64.so.1", shallow=False),
                  "C: loader bytes == VM loader (symlink followed)")
            CHECK(stage(res, S, [S] + R) == [], "C: stage again with ROOT first in roots -> nothing copied")
        except Exception as ex:
            CHECK(False, f"C: stage raised {ex!r}")
        # --- 추가 연습 P1~P4 ---
        res = closure(["/usr/bin/app", "/usr/bin/app_cyc"], R)
        CHECK(why(res, "libbar.so.1") == ["libbar.so.1", "/usr/lib/foo/libfoo.so.1", "/usr/bin/app"], "P1: why(libbar)")
        CHECK(why(res, "libcycb.so.1") == ["libcycb.so.1", "/usr/lib/cyc/libcyca.so.1", "/usr/bin/app_cyc"], "P1: why(libcycb) in a cycle")
        sh("gcc -shared -fPIC -Wl,-soname,libl.so.1 -Wl,--disable-new-dtags -Wl,-rpath,'/var/tmp/build/lib::lib:$ORIGIN:$ORIGIN/nope' bar.c -o usr/lib/foo/libl.so.1", cwd=T)
        lint = rpath_lint(read_elf(f"{T}/usr/lib/foo/libl.so.1"), "/usr/lib/foo/libl.so.1", R)
        CHECK([d for d, _ in lint] == ["/var/tmp/build/lib", "", "lib", "$ORIGIN/nope"], f"P2: rpath_lint flags 4 of 5 entries: {lint}")
        CHECK(rpath_lint(app, "/usr/bin/app", R) == [], "P2: app RUNPATH $ORIGIN/../lib/foo is clean")
        rows, total = size_budget(res, R)
        CHECK(len(rows) == 6 and total == sum(os.path.getsize(find(t, R)) for _, t in rows), f"P3: 6 files, total {total} bytes")
        CHECK(rows[0][1] == "/lib/aarch64-linux-gnu/libc.so.6", f"P3: biggest is libc ({rows[0][0]} bytes)")
        L1, L2 = f"{T}/links1", f"{T}/links2"
        for d in (L1, L2):
            os.makedirs(d)
            sh(f"gcc -shared -fPIC -Wl,-soname,libbar.so.1 bar.c -o {d}/libbar.so.1.2.3 && "
               f"gcc -shared -fPIC -Wl,-soname,libfoo.so.1 foo.c -o {d}/libfoo.so.1 -Wl,--allow-shlib-undefined", cwd=T)
        CHECK(soname_links(L1) == ["libbar.so.1"], "P4: soname_links made libbar.so.1")
        sh(f"/usr/sbin/ldconfig -n -N {L2}")
        CHECK({f: os.readlink(f"{L1}/{f}") for f in os.listdir(L1) if os.path.islink(f"{L1}/{f}")} ==
              {f: os.readlink(f"{L2}/{f}") for f in os.listdir(L2) if os.path.islink(f"{L2}/{f}")}, "P4: same links as ldconfig -n")
        CHECK(soname_links(L1) == [], "P4: second run makes nothing")
    finally:
        shutil.rmtree(T, ignore_errors=True)
    total = PASSED + FAILED
    print(f"{PASSED}/{total} passed")
    return 1 if FAILED else 0


def main(argv):
    if len(argv) >= 1 and argv[0] == "test":
        return run_tests()
    if len(argv) >= 2 and argv[0] in ("show", "stage"):
        cmd, rest = argv[0], argv[1:]
        root = rest.pop(0) if cmd == "stage" else None
        roots, bins = ([root] if root else []), []
        while rest:
            a = rest.pop(0)
            if a == "--from":
                roots.append(rest.pop(0))
            else:
                bins.append(a)
        res = closure(bins, roots)
        for i in res["interps"]:
            print(f"  [interp] {i}")
        for n, t in res["libs"].items():
            print(f"  {n} => {t}   (needed by {res['parent'][n]})")
        for n in res["missing"]:
            print(f"  {n} => not found")
        if cmd == "stage":
            for t in stage(res, root, roots):
                print(f"  staged {t}")
        return 1 if res["missing"] else 0
    print(__doc__)
    return 2


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
