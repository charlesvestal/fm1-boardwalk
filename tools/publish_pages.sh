#!/bin/sh
# SPDX-License-Identifier: MPL-2.0
# Copyright (C) 2026 Charles Vestal
# Build release VERSION, its emulator and its site (fm1-base's tools/make_site.py: app/site/landing.md and its
# images, the installer, the emulator, the screen reader's words, the mirror) and push the site to the gh-pages
# branch of origin, replacing what was there.
#   tools/publish_pages.sh 0.9
#   tools/publish_pages.sh 0.9 app/build/boardwalk-0.9.fwsc   (a package made elsewhere, the release's attested
#                                                               one: the site from it, no local release build)
set -e
cd "$(dirname "$0")/.."
V="${1:?usage: tools/publish_pages.sh VERSION [PACKAGE]}"
export JIELI_TOOLCHAIN="${JIELI_TOOLCHAIN:-$HOME/.jieli/toolchain}"   # (as build.sh)
export AC79_SDK="${AC79_SDK:-$HOME/fw-AC79_AIoT_SDK}"
[ -n "${2:-}" ] || exec sh base/tools/publish_site.sh app "$V"
# as base/tools/publish_site.sh, with the package given
PKG="$(cd "$(dirname "$2")" && pwd)/$(basename "$2")"
[ -f "$PKG" ] || { echo "publish_pages.sh: no package $2"; exit 1; }
PY="${PYTHON:-python3}"
"$PY" base/tools/build.py app --web --release "$V"
"$PY" base/tools/make_site.py app "$PKG" "$V" app/build/site
REMOTE="$(git remote get-url origin)"
SHA="$(git rev-parse --short HEAD)"
cd app/build/site
rm -rf .git
git init -q -b gh-pages
git add -A
git commit -q -m "Site for boardwalk $V (from $SHA)"
git push -q -f "$REMOTE" gh-pages
rm -rf .git
# ask GitHub Pages to build it now: a push alone has left the first deploy stuck on "building"
REPO="$(printf %s "$REMOTE" | sed -E 's#(git@github.com:|https://github.com/)##; s#\.git$##')"
gh api -X POST "repos/$REPO/pages/builds" >/dev/null 2>&1 || echo "(could not ask GitHub Pages to build: check the repository's Pages settings)"
echo "pushed the site for boardwalk $V to gh-pages"
