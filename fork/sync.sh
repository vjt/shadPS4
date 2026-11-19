#!/bin/bash
# SPDX-FileCopyrightText: 2026 shadPS4 Emulator Project
# SPDX-License-Identifier: GPL-2.0-or-later
# fork/sync.sh [--push] — rebuild this fork's main on top of the CURRENT upstream main, as branch main-next:
#   upstream main + a merge of every fix listed below that upstream has not merged yet + the "Fork:" commit
#   (README and this script) taken from the tip of fork/main.
# It never rebases or pushes the PR branches themselves: a push to a PR branch resets upstream's review.
# Remotes: origin = shadps4-emu/shadPS4, fork = vjt/shadPS4. Run it from a clean worktree, with
# git config rerere.enabled true and rerere.autoUpdate true.
# After --push: CI on main-next builds the AppImage -> test it on joker (ps4-emu.sh add/use, ps4-smoke.sh),
# and only when it plays: git push fork main-next:main --force-with-lease=main:<fork/main sha printed below>
set -euo pipefail
PUSH=${1:-}
FIXES=(            # ref to merge                     upstream PR ("-" = none yet)
  "fork/fix/tess-dynamic-hs           5738"
  "fork/fix/flatbuf-permutation       5739"
  "fork/fix/texture-cache-unmap-race  5745"
  "upstream-pr/5713                   5713"   # baggins183, DS_ORDERED_COUNT: God of War needs it
  "fork/fix/image-atomic-r64          5768"
  "fork/fix/tsharp-and-dw1            5769"
  "fork/fix/unbounded-buffer-limit    5770"
  "fork/fix/wave-reduction            5665"   # jaime7981's PR rebased on main: Shadow of the Tomb Raider needs it
)
[ -z "$(git status --porcelain)" ] || { echo "worktree not clean"; exit 1; }
git fetch -q origin main
git fetch -q fork
for e in "${FIXES[@]}"; do set -- $e
  case $1 in upstream-pr/*) git fetch -q origin "+pull/$2/head:refs/remotes/$1";; esac
done
forkc=$(git rev-parse "${FORK:-fork/main}")   # FORK=<ref> takes the "Fork:" commit from elsewhere
case "$(git log -1 --format=%s "$forkc")" in Fork:*) ;; *) echo "fork/main tip is not a 'Fork:' commit"; exit 1;; esac
echo "upstream main $(git rev-parse --short origin/main) | old fork/main $(git rev-parse --short "$forkc")"
git checkout -q --detach origin/main
for e in "${FIXES[@]}"; do set -- $e
  st=-; [ "$2" = - ] || st=$(gh pr view "$2" -R shadps4-emu/shadPS4 --json state -q .state)
  if [ "$st" = MERGED ] || git merge-base --is-ancestor "$1" HEAD; then echo "skip  #$2 $1 ($st, already upstream)"; continue; fi
  git merge -q --no-ff --no-edit -m "Merge $1 (#$2)" "$1" ||   # rerere replays conflicts resolved before
    { [ -z "$(git diff --name-only --diff-filter=U)" ] && git commit -q --no-edit; } ||
    { echo "CONFLICT merging $1 (#$2): resolve and commit (rerere records it), rerun; or drop it from FIXES"; exit 1; }
  echo "merge #$2 $1 $(git rev-parse --short "$1")"
done
git cherry-pick "$forkc" >/dev/null
git branch -f main-next HEAD
echo "main-next = $(git rev-parse --short HEAD)"
[ "$PUSH" = --push ] && git push -q -f fork main-next && echo "pushed fork/main-next"
true
