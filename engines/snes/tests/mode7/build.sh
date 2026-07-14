#!/usr/bin/env bash
# Assemble the Mode 7 test ROM -> ../data/mode7.sfc (ca65/ld65).
set -euo pipefail
cd "$(dirname "$0")"
ca65 --cpu 65816 mode7.s -o mode7.o
ld65 -C lorom32.cfg mode7.o -o ../data/mode7.sfc
rm -f mode7.o
echo "built ../data/mode7.sfc ($(wc -c < ../data/mode7.sfc) bytes)"
