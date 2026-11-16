#!/usr/bin/env bash
# TOOL-M08 demo — the same repo + solution script in the Lima VM (Linux, git 2.53, GNU make 4.4.1, gcc).
#   bash demo_vm.sh > demo_vm_out.txt 2>&1
# Mac home is read-only inside the VM -> the two scripts are piped in and written to VM /tmp.
set -u
cd "$(dirname "$0")" || exit 2
limactl shell elinux -- bash -lc 'rm -rf /tmp/TOOL-M08_work/vm && mkdir -p /tmp/TOOL-M08_work/vm'
limactl shell elinux -- bash -lc 'cat > /tmp/TOOL-M08_work/vm/gen_repo.sh' < gen_repo.sh
limactl shell elinux -- bash -lc 'cat > /tmp/TOOL-M08_work/vm/test.sh && chmod +x /tmp/TOOL-M08_work/vm/test.sh' < solution_test.sh
limactl shell elinux -- bash -lc '
set -u
export GIT_CONFIG_GLOBAL=/dev/null GIT_CONFIG_NOSYSTEM=1 LC_ALL=C TZ=UTC
V=/tmp/TOOL-M08_work/vm; cd $V
echo "$(uname -sm), $(git --version), $(make --version | head -1), $(cc --version | head -1)"
bash gen_repo.sh $V/repo && cd $V/repo
rev() { git rev-list -1 --grep "^\[c$1\]" main; }
echo "### same hashes as on the Mac?"; git log --format="%H %s" -1 main; git rev-parse v1.0
echo "### c060 with gcc"; git checkout -q $(rev 060); make -s app 2>&1 | grep -m1 error
echo "### c063 with glibc"; git checkout -q $(rev 063); git clean -fdxq; make -s app; ./build/app >/dev/null; echo "rc=$?"
git checkout -q main; git clean -fdxq
echo "### git bisect run"
git bisect start main v1.0 >/dev/null
git bisect run $V/test.sh 2>&1 | grep -E "^\[|bisect-test|is the first bad commit"
git bisect log | grep -c "^git bisect \(good\|bad\)" | sed "s/^/good+bad decisions: /"
git bisect log | grep -c "^git bisect skip" | sed "s/^/skips: /"
git bisect reset >/dev/null 2>&1
echo "### broken rig: CC=gcc-99 (does not exist). Without the rig check every step is a 125 skip:"
sed "/command -v make/d" $V/test.sh > $V/test_norig.sh; chmod +x $V/test_norig.sh
git bisect start main v1.0 >/dev/null
CC=gcc-99 git bisect run $V/test_norig.sh > $V/norig.txt 2>&1; echo "git bisect run exit $?"
grep -E "only .skip|cannot" $V/norig.txt
git bisect log | grep -c "^git bisect skip" | sed "s/^/skips: /"
git bisect reset >/dev/null 2>&1
echo "### same with the rig check (exit 255 on the first step):"
git bisect start main v1.0 >/dev/null
CC=gcc-99 git bisect run $V/test.sh 2>&1 | grep -E "error"
git bisect reset >/dev/null 2>&1
'
