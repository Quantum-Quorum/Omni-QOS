#!/bin/bash
# ==============================================================================
# OmniQ-OS: Real-Time Quantum Linux
# Script: scripts/vendor_picom.sh
# Extracts picom, libconfig9, libev4t64, libepoxy0 from build/deb_extract/*.deb
# into vendor/picom/{bin,lib}, resolves non-glibc shared libraries via ldd, and
# extracts wmctrl, xdotool, and libxdo.so* into bin/ and lib/.
# ==============================================================================
set -euo pipefail

PROJECT_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
DEB_DIR="$PROJECT_ROOT/build/deb_extract"
OUT="$PROJECT_ROOT/vendor/picom"

if [ "${1:-}" = "--test" ]; then
    bash -n "$0"
    if [ -x "$OUT/bin/picom" ] && [ -x "$PROJECT_ROOT/bin/wmctrl" ] && [ -x "$PROJECT_ROOT/bin/xdotool" ]; then
        LD_LIBRARY_PATH="$OUT/lib" "$OUT/bin/picom" --version >/dev/null
        echo "[+] vendor_picom.sh verification passed."
        exit 0
    fi
    echo "[-] vendor_picom.sh artifacts missing; run without --test first." >&2
    exit 1
fi

WORK="$(mktemp -d)"
trap 'rm -rf "$WORK"' EXIT

echo "[*] Extracting picom, libconfig9, libev4t64, libepoxy0 from $DEB_DIR..."
mkdir -p "$WORK/picom_root"
for pkg in picom libconfig9 libev4t64 libepoxy0; do
    deb_file="$(ls "$DEB_DIR"/${pkg}_*.deb 2>/dev/null | head -n 1 || true)"
    if [ -z "$deb_file" ]; then
        echo "[-] Missing package ${pkg}_*.deb in $DEB_DIR" >&2
        exit 1
    fi
    dpkg -x "$deb_file" "$WORK/picom_root"
done

rm -rf "$OUT"
mkdir -p "$OUT/bin" "$OUT/lib"
cp -a "$WORK/picom_root/usr/bin/picom" "$OUT/bin/picom"
chmod 755 "$OUT/bin/picom"

# Copy extracted shared libraries from the deb packages first
if [ -d "$WORK/picom_root/usr/lib/x86_64-linux-gnu" ]; then
    for f in "$WORK/picom_root/usr/lib/x86_64-linux-gnu"/*.so*; do
        [ -e "$f" ] || continue
        cp -aL "$f" "$OUT/lib/$(basename "$f")"
    done
fi

# Bundle every non-glibc shared object resolved by ldd from extracted + host libs
echo "[*] Resolving and bundling non-glibc shared libraries via ldd..."
LD_LIBRARY_PATH="$WORK/picom_root/usr/lib/x86_64-linux-gnu:$OUT/lib" ldd "$OUT/bin/picom" \
  | awk '/=> \// {print $3}' \
  | while read -r lib; do
        base="$(basename "$lib")"
        case "$base" in
            libc.so*|libm.so*|libdl.so*|libpthread.so*|librt.so*|ld-linux*) continue ;;
        esac
        cp -aL "$lib" "$OUT/lib/$base"
    done

echo "[*] Extracting wmctrl, xdotool, and libxdo3 into bin/ and lib/..."
mkdir -p "$WORK/wm_root" "$PROJECT_ROOT/bin" "$PROJECT_ROOT/lib"
for pkg in wmctrl xdotool libxdo3; do
    deb_file="$(ls "$DEB_DIR"/${pkg}_*.deb 2>/dev/null | head -n 1 || true)"
    if [ -z "$deb_file" ]; then
        echo "[-] Missing package ${pkg}_*.deb in $DEB_DIR" >&2
        exit 1
    fi
    dpkg -x "$deb_file" "$WORK/wm_root"
done

cp -a "$WORK/wm_root/usr/bin/wmctrl" "$PROJECT_ROOT/bin/wmctrl"
cp -a "$WORK/wm_root/usr/bin/xdotool" "$PROJECT_ROOT/bin/xdotool"
chmod 755 "$PROJECT_ROOT/bin/wmctrl" "$PROJECT_ROOT/bin/xdotool"

for xdo_lib in "$WORK/wm_root/usr/lib/x86_64-linux-gnu"/libxdo.so*; do
    [ -e "$xdo_lib" ] || continue
    cp -a "$xdo_lib" "$PROJECT_ROOT/lib/"
done

echo "[+] picom bundled: $(du -sh "$OUT" | cut -f1) in vendor/picom"
LD_LIBRARY_PATH="$OUT/lib" "$OUT/bin/picom" --version
LD_LIBRARY_PATH="$PROJECT_ROOT/lib" "$PROJECT_ROOT/bin/xdotool" --version | head -n 1
echo "[+] wmctrl, xdotool, and libxdo.so* installed in bin/ and lib/"
