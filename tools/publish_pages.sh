#!/bin/sh
# SPDX-License-Identifier: MPL-2.0
# Copyright (C) 2026 Charles Vestal
# Build release VERSION, its emulator and its site (fm1-base's tools/make_site.py: app/site/landing.md and its
# images, the installer, the emulator, the screen reader's words, the mirror) and push the site to the gh-pages
# branch of origin, replacing what was there.
#   tools/publish_pages.sh 0.9
set -e
cd "$(dirname "$0")/.."
V="${1:?usage: tools/publish_pages.sh VERSION}"
export JIELI_TOOLCHAIN="${JIELI_TOOLCHAIN:-$HOME/.jieli/toolchain}"   # (as build.sh)
export AC79_SDK="${AC79_SDK:-$HOME/fw-AC79_AIoT_SDK}"
exec sh base/tools/publish_site.sh app "$V"
