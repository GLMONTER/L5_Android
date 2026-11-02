#!/bin/bash
# Apply patches/<project path>/*.patch to the AOSP projects on a local "librem5" branch.
# Idempotent: a project whose branch already contains every patch subject is skipped.
# A `repo sync` resets projects to the manifest revision; just run this again afterwards.
source "$(dirname "$0")/common.sh"

cd "$REPO/patches"
find . -name '*.patch' -printf '%h\n' | sed 's|^\./||' | sort -u | while read -r proj; do
    dir=$AOSP/$proj
    [ -d "$dir/.git" ] || [ -f "$dir/.git" ] || die "$proj is not a git project (did repo sync run?)"
    mapfile -t subjects < <(for p in "$REPO/patches/$proj"/*.patch; do patch_subject "$p"; done)
    have=$(git -C "$dir" log --format=%s -n 50 2>/dev/null)
    missing=0
    for s in "${subjects[@]}"; do grep -qxF -- "$s" <<<"$have" || missing=1; done
    if [ $missing = 0 ]; then
        log "$proj: ${#subjects[@]} patch(es) already applied"
        continue
    fi
    log "$proj: applying ${#subjects[@]} patch(es)"
    git -C "$dir" checkout -q -B librem5
    git -C "$dir" -c user.name="librem5-android" -c user.email="build@localhost" \
        am -q --3way "$REPO/patches/$proj"/*.patch || die "git am failed in $proj (resolve, then git am --continue)"
done
