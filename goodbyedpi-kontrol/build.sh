#!/usr/bin/env bash
# GoodbyeDPI-Kontrol.exe'yi Linux üzerinde MinGW-w64 ile çapraz derler.
# Gereksinimler: x86_64-w64-mingw32-gcc, x86_64-w64-mingw32-windres, python3, curl, unzip
set -euo pipefail
cd "$(dirname "$0")"

GDPI_URL="https://github.com/cagritaskn/GoodbyeDPI-Turkey/releases/download/release-0.2.3rc3-turkey/goodbyedpi-0.2.3rc3-turkey.zip"
CC=x86_64-w64-mingw32-gcc
WINDRES=x86_64-w64-mingw32-windres

mkdir -p build dist
if [ ! -f build/goodbyedpi.zip ]; then
  curl -fsSL -o build/goodbyedpi.zip "$GDPI_URL"
fi
rm -rf build/zip
unzip -q -o build/goodbyedpi.zip -d build/zip
cp build/zip/x86_64/goodbyedpi.exe build/zip/x86_64/WinDivert.dll build/zip/x86_64/WinDivert64.sys build/
mkdir -p dist/licenses
cp build/zip/licenses/* dist/licenses/

python3 tools/make_icons.py build

$WINDRES -I src -O coff -o build/app.res src/app.rc
$CC -O2 -s -municode -mwindows -std=c11 -Wall -Wextra \
    -o dist/GoodbyeDPI-Kontrol.exe src/main.c build/app.res \
    -static-libgcc -lshell32 -lcomctl32 -luser32 -lgdi32 -ladvapi32 -lsecur32

ls -l dist/GoodbyeDPI-Kontrol.exe
