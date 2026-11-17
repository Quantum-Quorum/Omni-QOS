#!/bin/bash

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "${SCRIPT_DIR}/.." && pwd)"
BUILD_DIR="${REPO_ROOT}/build/iso"

echo "================================================================="
echo " OmniQ-OS Debian Real-Time Quantum Linux (XFCE Edition) Builder   "
echo " Target Architecture: ARM64 (aarch64)                           "
echo " Desktop Environment: XFCE 4 (Kali-Style Obsidian/Cyan Theme)   "
echo " Kernel Configuration: PREEMPT_RT Quantum Drivers                "
echo "================================================================="

if ! command -v lb >/dev/null 2>&1; then
    echo "Notice: 'lb' (live-build) is not installed on this host."
    echo "To produce physical ISO images on a Debian build server:"
    echo "  sudo apt-get update && sudo apt-get install -y live-build debootstrap"
    echo ""
    echo "[DRY-RUN] Validating directory tree and assembling chroot overlays..."
fi

mkdir -p "${BUILD_DIR}/auto"
mkdir -p "${BUILD_DIR}/config/package-lists"
mkdir -p "${BUILD_DIR}/config/hooks/live"
mkdir -p "${BUILD_DIR}/config/includes.chroot/etc/systemd/system"
mkdir -p "${BUILD_DIR}/config/includes.chroot/etc/sysctl.d"
mkdir -p "${BUILD_DIR}/config/includes.chroot/etc/security/limits.d"
mkdir -p "${BUILD_DIR}/config/includes.chroot/etc/modules-load.d"
mkdir -p "${BUILD_DIR}/config/includes.chroot/boot/grub/themes/omniq"
mkdir -p "${BUILD_DIR}/config/includes.chroot/usr/share/plymouth/themes/omniq"
mkdir -p "${BUILD_DIR}/config/includes.chroot/usr/bin"
mkdir -p "${BUILD_DIR}/config/includes.chroot/usr/src/omniq-qpu-0.1.0-rt"
mkdir -p "${BUILD_DIR}/config/includes.chroot/opt/omniqos"

cp "${SCRIPT_DIR}/live-build/auto/config" "${BUILD_DIR}/auto/config"
chmod +x "${BUILD_DIR}/auto/config"

cp "${SCRIPT_DIR}/live-build/config/package-lists/omniqos.list.chroot" \
   "${BUILD_DIR}/config/package-lists/omniqos.list.chroot"

cp "${SCRIPT_DIR}/live-build/config/hooks/live/01-quantum-rt-tuning.hook.chroot" \
   "${BUILD_DIR}/config/hooks/live/01-quantum-rt-tuning.hook.chroot"
chmod +x "${BUILD_DIR}/config/hooks/live/01-quantum-rt-tuning.hook.chroot"

cp "${SCRIPT_DIR}/systemd/omniqd.service" \
   "${BUILD_DIR}/config/includes.chroot/etc/systemd/system/omniqd.service"
cp "${SCRIPT_DIR}/systemd/qpu-realtime-tuning.service" \
   "${BUILD_DIR}/config/includes.chroot/etc/systemd/system/qpu-realtime-tuning.service"

cp "${SCRIPT_DIR}/config/etc/sysctl.d/99-quantum-rt.conf" \
   "${BUILD_DIR}/config/includes.chroot/etc/sysctl.d/99-quantum-rt.conf"
cp "${SCRIPT_DIR}/config/etc/security/limits.d/99-quantum-rt.conf" \
   "${BUILD_DIR}/config/includes.chroot/etc/security/limits.d/99-quantum-rt.conf"
cp "${SCRIPT_DIR}/config/etc/modules-load.d/omniq_qpu.conf" \
   "${BUILD_DIR}/config/includes.chroot/etc/modules-load.d/omniq_qpu.conf"

cp "${SCRIPT_DIR}/branding/grub/theme.txt" \
   "${BUILD_DIR}/config/includes.chroot/boot/grub/themes/omniq/theme.txt"
cp "${SCRIPT_DIR}/branding/plymouth/omniq.plymouth" \
   "${BUILD_DIR}/config/includes.chroot/usr/share/plymouth/themes/omniq/omniq.plymouth"
cp "${SCRIPT_DIR}/branding/plymouth/omniq.script" \
   "${BUILD_DIR}/config/includes.chroot/usr/share/plymouth/themes/omniq/omniq.script"

cp -r "${REPO_ROOT}/kernel/"* "${BUILD_DIR}/config/includes.chroot/usr/src/omniq-qpu-0.1.0-rt/"

cp -r "${REPO_ROOT}/runtime/python/"* "${BUILD_DIR}/config/includes.chroot/opt/omniqos/"

if [ -f "${REPO_ROOT}/tools/qpu-smi/qpu-smi" ]; then
    cp "${REPO_ROOT}/tools/qpu-smi/qpu-smi" "${BUILD_DIR}/config/includes.chroot/usr/bin/qpu-smi"
    chmod +x "${BUILD_DIR}/config/includes.chroot/usr/bin/qpu-smi"
fi

echo "[OmniQ-OS] Chroot filesystem layout prepared successfully in: ${BUILD_DIR}"

if command -v lb >/dev/null 2>&1; then
    cd "${BUILD_DIR}"
    echo "[OmniQ-OS] Executing lb config..."
    lb config
    echo "[OmniQ-OS] Executing lb build (requires root privileges)..."
    sudo lb build
    echo "================================================================="
    echo " SUCCESS: Generated OmniQ-OS-XFCE-Debian-arm64.iso              "
    echo "================================================================="
else
    echo "================================================================="
    echo " READY: Live-build chroot overlay assembled and validated!       "
    echo " When running on Debian/Linux build infrastructure, execute:      "
    echo "   cd ${BUILD_DIR} && sudo lb config && sudo lb build            "
    echo "================================================================="
fi

