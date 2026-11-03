#!/bin/bash
# Fresh setup: AOSP 17 checkout + GloDroid/Mesa components + this repo + project patches.
#
# Usage: scripts/setup.sh <workdir>
#   Run from a standalone clone of this repo. Creates <workdir>/aosp, syncs it (this repo is
#   checked out again at <workdir>/aosp/device/purism; use that copy from then on), applies
#   patches/ and creates the Python venv Mesa needs. Safe to re-run.
set -euo pipefail

WORK=$(mkdir -p "${1:?usage: $0 <workdir>}" && cd "$1" && pwd)
SRC_REPO=$(cd "$(dirname "$0")/.." && pwd)
AOSP=$WORK/aosp
AOSP_MANIFEST_URL=${AOSP_MANIFEST_URL:-https://android.googlesource.com/platform/manifest}
AOSP_BRANCH=${AOSP_BRANCH:-android-17.0.0_r1}

for t in repo git python3; do command -v $t >/dev/null || { echo "missing: $t"; exit 1; }; done

# Where `repo` should fetch this repo from: its upstream if it has one, else this clone.
REPO_URL=$(git -C "$SRC_REPO" remote get-url origin 2>/dev/null || echo "file://$SRC_REPO")
REPO_FETCH=${REPO_URL%/*}
REPO_NAME=${REPO_URL##*/}
REPO_REV=$(git -C "$SRC_REPO" rev-parse --abbrev-ref HEAD)

mkdir -p "$AOSP" && cd "$AOSP"
if [ ! -d .repo ]; then
    echo "== repo init $AOSP_BRANCH"
    repo init -u "$AOSP_MANIFEST_URL" -b "$AOSP_BRANCH" --depth=1 -g default,-darwin,-mips
fi

mkdir -p .repo/local_manifests
sed -e "s|@LIBREM5_FETCH@|$REPO_FETCH|" -e "s|@LIBREM5_NAME@|$REPO_NAME|" \
    -e "s|@LIBREM5_REV@|$REPO_REV|" \
    "$SRC_REPO/manifest/librem5.xml" > .repo/local_manifests/librem5.xml

echo "== repo sync (first run downloads ~110 GB)"
repo sync -c -j"${SYNC_JOBS:-8}" --no-tags --no-clone-bundle --optimized-fetch

"$AOSP/device/purism/scripts/apply-patches.sh"

if [ ! -x "$WORK/hostpy/bin/python3" ]; then
    echo "== host Python venv (Mesa, libcamera code generators)"
    python3 -m venv --system-site-packages "$WORK/hostpy"
fi
"$WORK/hostpy/bin/pip" install -q pycparser mako ply jinja2 pyyaml

cat <<EOF

Setup done. From now on work in $AOSP/device/purism:
  scripts/extract-firmware.sh   # once: Broadcom Wi-Fi/BT firmware from the phone
  scripts/build.sh              # kernel + Android + images
  scripts/flash-first.sh        # first install (wipes PureOS), download mode
  scripts/flash.sh              # updates afterwards, over adb
EOF
