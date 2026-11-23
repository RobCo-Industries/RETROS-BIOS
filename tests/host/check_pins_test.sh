#!/usr/bin/env bash
# Checks the `make check-pins` wiring with a git that always fails, as in
# the CI container:
#   - check_pins.sh passes on the real workflow and fails on a workflow
#     with a wrong commit and a wrong image tag;
#   - check-pins runs the doc link check, which passes on the real tree
#     and fails on a copy with a stale RobCo-Core link.
set -euo pipefail
# Build settings from the caller's environment or an outer make must not
# change the runs here.
unset CI MAKEFLAGS MFLAGS MAKELEVEL BUILD BOARD STAGE2 ROBCO_CORE WINDOW DOC_ROOT

root=$(cd "$(dirname "$0")/../.." && pwd)
tmp=$(mktemp -d)
trap 'rm -rf "$tmp"' EXIT

mkdir "$tmp/bin"
printf '#!/bin/sh\necho "fatal: detected dubious ownership" >&2\nexit 128\n' > "$tmp/bin/git"
chmod +x "$tmp/bin/git"
export PATH="$tmp/bin:$PATH"

fail() {
    echo "check_pins_test: $*" >&2
    exit 1
}

check="$root/external/robco-core/tools/check_pins.sh"
cd "$root"

# Workflow pins.
"$check" .github/workflows/ci.yml > "$tmp/ok.out" 2>&1 || fail "real tree failed: $(cat "$tmp/ok.out")"
grep -q '^check-pins: ok ([0-9a-f]\{40\}, ' "$tmp/ok.out" || fail "no ok line: $(cat "$tmp/ok.out")"
sed -e 's/robco-ci\.yml@[0-9a-f]*/robco-ci.yml@0000000000000000000000000000000000000000/' \
    -e 's/image-tag:.*/image-tag: wrongtag/' \
    .github/workflows/ci.yml > "$tmp/ci.yml"
if "$check" "$tmp/ci.yml" > "$tmp/bad.out" 2>&1; then
    fail "wrong pins passed"
fi
grep -q 'uses robco-ci.yml@0000000000000000000000000000000000000000, the submodule is at' \
    "$tmp/bad.out" || fail "no commit message: $(cat "$tmp/bad.out")"
grep -q 'uses image-tag wrongtag, ' "$tmp/bad.out" || fail "no tag message: $(cat "$tmp/bad.out")"

# check-pins runs both checks.
make -s -n check-pins > "$tmp/plan.out" 2>&1 || fail "make -n check-pins failed: $(cat "$tmp/plan.out")"
grep -q 'tools/check_doc_links.sh \. external/robco-core' "$tmp/plan.out" ||
    fail "check-pins does not run the link check: $(cat "$tmp/plan.out")"
grep -q 'tools/check_pins.sh .github/workflows/ci.yml' "$tmp/plan.out" ||
    fail "check-pins does not run the pin check: $(cat "$tmp/plan.out")"

# Doc links.
make -s check-doc-links > "$tmp/real.out" 2>&1 || fail "real links failed: $(cat "$tmp/real.out")"
grep -q '^check-doc-links: ok ([0-9a-f]\{40\})' "$tmp/real.out" || fail "no ok line: $(cat "$tmp/real.out")"
commit=$(sed -n 's/^check-doc-links: ok (\([0-9a-f]*\)).*$/\1/p' "$tmp/real.out")

# A copy of the Markdown files with a submodule whose .git is a file, as in a checkout.
proj=$tmp/proj
mkdir -p "$proj/external/robco-core" "$proj/gitdir"
cp "$root"/*.md "$proj/"
echo "gitdir: ../../gitdir" > "$proj/external/robco-core/.git"
echo "$commit" > "$proj/gitdir/HEAD"
make -s check-doc-links DOC_ROOT="$proj" > "$tmp/copy.out" 2>&1 || fail "copy failed: $(cat "$tmp/copy.out")"
stale=0000000000000000000000000000000000000000
echo "[spec](https://github.com/RobCo-Industries/RobCo-Core/blob/$stale/docs/spec/BOOT_PROTOCOL.md)" \
    >> "$proj/README.md"
if make -s check-doc-links DOC_ROOT="$proj" > "$tmp/stale.out" 2>&1; then
    fail "stale link passed"
fi
grep -q "README.md:[0-9]* links RobCo-Core at $stale" "$tmp/stale.out" ||
    fail "no stale message: $(cat "$tmp/stale.out")"

echo "check_pins_test: passed"
