#!/bin/bash
# N06G auto-bisect demo: 16 fake builds; build 11 introduces a regression,
# build 10 does not build (-> exit 125 = skip). git bisect run finds 11 in
# ~log2(16) test runs - unless a skipped build sits next to it, then it
# can only narrow to {10, 11}: the cost of an unbuildable build. The test script's exit code follows the git contract:
# 0 good, 1..127 (not 125) bad, 125 skip, >127 abort.
set -eu
R="${TMPDIR:-/tmp}/n06g_bisect_repo"
rm -rf "$R"; mkdir -p "$R"; cd "$R"
git init -q; git config user.email demo@example.com; git config user.name demo
for k in $(seq 1 16); do
  bug=0; [ "$k" -ge 11 ] && bug=1
  brk=0; [ "$k" -eq 10 ] && brk=1
  printf 'build=%d\nregression=%d\nbuild_broken=%d\n' "$k" "$bug" "$brk" > state.txt
  git add state.txt; git commit -qm "build $k"
done
cat > "${TMPDIR:-/tmp}/n06g_bisect_test.sh" <<'EOF'
#!/bin/sh
# stands in for: build image -> flash board -> run test_runner --only X --repeat N
grep -q 'build_broken=1' state.txt && { echo "  $(head -1 state.txt): cannot build -> 125 (skip)"; exit 125; }
if grep -q 'regression=1' state.txt; then echo "  $(head -1 state.txt): FAIL -> 1 (bad)"; exit 1; fi
echo "  $(head -1 state.txt): PASS -> 0 (good)"; exit 0
EOF
chmod +x "${TMPDIR:-/tmp}/n06g_bisect_test.sh"
git bisect start HEAD HEAD~15 >/dev/null    # bad = build 16, good = build 1
git bisect run "${TMPDIR:-/tmp}/n06g_bisect_test.sh" > bisect.out 2>&1 || true
grep -E '^  build=|is the first bad commit|skip|could be any of|cannot bisect' bisect.out
# translate candidate hashes back to build numbers
for h in $(grep -E '^[0-9a-f]{40}' bisect.out | cut -c1-40); do echo "    candidate: $(git log -1 --format=%s "$h")"; done
git bisect reset >/dev/null 2>&1
