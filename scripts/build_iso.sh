#!/usr/bin/env bash

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "${SCRIPT_DIR}/.." && pwd)"
DEB_CACHE="${PROJECT_ROOT}/build/deb_extract"
MIN_BUILD="${PROJECT_ROOT}/build/minimal"
INITRD_STAGING="${MIN_BUILD}/initrd_root"
ISO_STAGING="${MIN_BUILD}/iso_root"
OUTPUT_ISO="${PROJECT_ROOT}/omniq-os.iso"
KERNEL_VER="7.0.0-43-generic"

echo "================================================================="
echo " OmniQ-OS Minimal Real-Time Cryo-Rack Appliance ISO Builder      "
echo " Target: x86_64 | Kernel: ${KERNEL_VER} | Output: omniq-os.iso   "
echo "================================================================="

rm -rf "${MIN_BUILD}"
mkdir -p "${DEB_CACHE}" "${INITRD_STAGING}" "${ISO_STAGING}/boot/grub"

if ! ls "${DEB_CACHE}"/linux-image-unsigned-${KERNEL_VER}_*.deb >/dev/null 2>&1; then
    (cd "${DEB_CACHE}" && apt-get download "linux-image-unsigned-${KERNEL_VER}")
fi

mkdir -p "${MIN_BUILD}/k_extract"
dpkg-deb -x "${DEB_CACHE}"/linux-image-unsigned-${KERNEL_VER}_*.deb "${MIN_BUILD}/k_extract"
cp -f "${MIN_BUILD}/k_extract/boot/vmlinuz-${KERNEL_VER}" "${ISO_STAGING}/boot/vmlinuz"

mkdir -p "${INITRD_STAGING}"/{bin,sbin,dev,dev/accel,proc,sys,run,tmp,etc/omniq,usr/bin,usr/local/bin,opt/omniq/drivers,root}

gcc -O2 -Wall -static -o "${INITRD_STAGING}/init" "${PROJECT_ROOT}/src/init.c"
cp -f "${INITRD_STAGING}/init" "${INITRD_STAGING}/sbin/init"
chmod 755 "${INITRD_STAGING}/init" "${INITRD_STAGING}/sbin/init"

gcc -O2 -Wall -static -pthread \
    -I"${PROJECT_ROOT}/runtime/libomniq-qpu/include" \
    -I"${PROJECT_ROOT}/kernel/include/uapi" \
    -o "${INITRD_STAGING}/usr/local/bin/qpu-smi" \
    "${PROJECT_ROOT}/tools/qpu-smi/main.c" \
    "${PROJECT_ROOT}/runtime/libomniq-qpu/libomniq_qpu.a" -lpthread
ln -sf /usr/local/bin/qpu-smi "${INITRD_STAGING}/usr/bin/qpu-smi"

BUSYBOX_BIN="$(command -v busybox)"
cp -f "${BUSYBOX_BIN}" "${INITRD_STAGING}/bin/busybox"
chmod 755 "${INITRD_STAGING}/bin/busybox"

for app in sh ash ls cp mv rm mkdir rmdir cat echo grep sed awk head tail cut sort uniq wc \
           dmesg ps top free df du mount umount sleep sync kill clear reset touch chmod chown mknod; do
    ln -sf busybox "${INITRD_STAGING}/bin/${app}"
done
for sapp in insmod rmmod modprobe lsmod depmod mdev reboot poweroff halt cttyhack switch_root; do
    ln -sf ../bin/busybox "${INITRD_STAGING}/sbin/${sapp}"
done

ln -sf busybox "${INITRD_STAGING}/bin/uname"

if [[ -f "${PROJECT_ROOT}/kernel/drivers/accel/qpu/omniq_qpu.ko" ]]; then
    cp -f "${PROJECT_ROOT}/kernel/drivers/accel/qpu/omniq_qpu.ko" "${INITRD_STAGING}/opt/omniq/drivers/omniq_qpu.ko"
fi

cat > "${INITRD_STAGING}/etc/os-release" <<'EOF'
NAME="OmniQ-OS"
VERSION="24.04-RT (Cryo Appliance)"
ID=omniqos
PRETTY_NAME="OmniQ-OS 24.04-RT Real-Time Quantum Linux"
VERSION_ID="24.04"
EOF
echo "omniq-rt" > "${INITRD_STAGING}/etc/hostname"

(cd "${INITRD_STAGING}" && find . -print0 | cpio --null -o --format=newc 2>/dev/null | gzip -9 > "${ISO_STAGING}/boot/initrd.img")

cat > "${ISO_STAGING}/boot/grub/grub.cfg" <<'EOF'
set timeout=3
set default=0

menuentry "OmniQ-OS 24.04-RT - Cryogenic Rack Real-Time Appliance (< 600 ns QEC)" {
    linux /boot/vmlinuz isolcpus=2,3 nohz_full=2,3 rcu_nocbs=2,3 idle=poll processor.max_cstate=0 quiet
    initrd /boot/initrd.img
}

menuentry "OmniQ-OS 24.04-RT - Headless Serial Console (ttyS0 115200)" {
    linux /boot/vmlinuz console=tty0 console=ttyS0,115200n8 isolcpus=2,3 nohz_full=2,3 rcu_nocbs=2,3
    initrd /boot/initrd.img
}
EOF

grub-mkrescue -o "${OUTPUT_ISO}" "${ISO_STAGING}" --compress=xz 2>/dev/null
(cd "${PROJECT_ROOT}" && sha256sum "$(basename "${OUTPUT_ISO}")" > "$(basename "${OUTPUT_ISO}").sha256")

echo "================================================================="
echo " SUCCESS: Built $(ls -lh "${OUTPUT_ISO}" | awk '{print $9, "(" $5 ")"}')"
cat "${OUTPUT_ISO}.sha256"
echo "================================================================="
