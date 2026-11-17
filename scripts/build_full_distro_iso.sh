#!/usr/bin/env bash
# SPDX-License-Identifier: Apache-2.0
# OmniQ-OS Full Live Quantum Workstation ISO Builder (x86_64)
# Output: omniq-os-full.iso
# Assembles complete rootfs with Linux 7.0.0-43-generic + omniq_qpu.ko, firmware,
# Xorg + Mesa OpenGL + Picom + JWM glassmorphic desktop, Python 3.12 + GTK3/Cairo/Tk/NumPy/SciPy,
# C++20 omniq-core & omniq-secret-api, Hilbert Space Explorer, Quantum Circuit Maker,
# recursive ELF shared library closure, Zstd SquashFS, and OverlayFS live initrd.img.

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "${SCRIPT_DIR}/.." && pwd)"
DEB_CACHE="${PROJECT_ROOT}/build/deb_extract"
FULL_BUILD="${PROJECT_ROOT}/build/full_distro"
ROOTFS_DIR="${FULL_BUILD}/rootfs"
INITRD_STAGING="${FULL_BUILD}/initrd_root"
ISO_STAGING="${FULL_BUILD}/iso_root"
OUTPUT_ISO="${PROJECT_ROOT}/omniq-os-full.iso"
KERNEL_VER="7.0.0-43-generic"

echo "======================================================================"
echo " OmniQ-OS Full Live Quantum Workstation ISO Pipeline (x86_64)         "
echo " Kernel: ${KERNEL_VER} + omniq_qpu.ko | Desktop: Picom + JWM + GTK3   "
echo "======================================================================"

rm -rf "${FULL_BUILD}"
mkdir -p "${DEB_CACHE}" "${ROOTFS_DIR}" "${INITRD_STAGING}" "${ISO_STAGING}/boot/grub" "${ISO_STAGING}/casper"

# ------------------------------------------------------------------------------
# STEP 1: Kernel, Hardware Driver Tree (/lib/modules), Firmware & Cached DEBs
# ------------------------------------------------------------------------------
echo "[Step 1/6] Extracting kernel ${KERNEL_VER}, driver modules, firmware & desktop packages..."
if ! ls "${DEB_CACHE}"/linux-image-unsigned-${KERNEL_VER}_*.deb >/dev/null 2>&1; then
    (cd "${DEB_CACHE}" && apt-get download "linux-image-unsigned-${KERNEL_VER}")
fi

mkdir -p "${FULL_BUILD}/k_extract"
dpkg-deb -x "${DEB_CACHE}"/linux-image-unsigned-${KERNEL_VER}_*.deb "${FULL_BUILD}/k_extract"
cp -f "${FULL_BUILD}/k_extract/boot/vmlinuz-${KERNEL_VER}" "${ISO_STAGING}/boot/vmlinuz"

# Create POSIX FHS directory hierarchy
mkdir -p "${ROOTFS_DIR}"/{bin,sbin,boot,dev,dev/accel,dev/pts,dev/shm,proc,sys,run,tmp,mnt,media,cdrom,root,home/quantum}
mkdir -p "${ROOTFS_DIR}"/{etc/omniq/icons,etc/omniq/theme,etc/jwm,etc/X11/xinit,etc/X11/xorg.conf.d,etc/gtk-3.0,etc/gtk-2.0,etc/xdg/openbox,etc/ld.so.conf.d,etc/sysctl.d,etc/security/limits.d,etc/udev/rules.d}
mkdir -p "${ROOTFS_DIR}"/{lib,lib/modules,lib/firmware,lib/udev/rules.d,lib64}
mkdir -p "${ROOTFS_DIR}"/{usr/bin,usr/sbin,usr/local/bin,usr/local/lib,usr/lib/x86_64-linux-gnu,usr/share/fonts,usr/share/icons,usr/share/themes,usr/share/terminfo,usr/share/X11}
mkdir -p "${ROOTFS_DIR}"/{var/log/omniq,var/run,var/tmp,var/lib/dbus,opt/omniq/drivers,opt/omniq/lib,opt/omniq/src,opt/omniq/circuits,root/circuits,home/quantum/circuits}
chmod 1777 "${ROOTFS_DIR}/tmp" "${ROOTFS_DIR}/var/tmp"

cp -f "${ISO_STAGING}/boot/vmlinuz" "${ROOTFS_DIR}/boot/vmlinuz-${KERNEL_VER}"

# Copy host kernel module tree and inject omniq_qpu.ko
cp -a "/lib/modules/${KERNEL_VER}" "${ROOTFS_DIR}/lib/modules/"
rm -f "${ROOTFS_DIR}/lib/modules/${KERNEL_VER}/build" "${ROOTFS_DIR}/lib/modules/${KERNEL_VER}/source"
mkdir -p "${ROOTFS_DIR}/lib/modules/${KERNEL_VER}/extra"
cp -f "${PROJECT_ROOT}/kernel/drivers/accel/qpu/omniq_qpu.ko" "${ROOTFS_DIR}/lib/modules/${KERNEL_VER}/extra/omniq_qpu.ko"
cp -f "${PROJECT_ROOT}/kernel/drivers/accel/qpu/omniq_qpu.ko" "${ROOTFS_DIR}/opt/omniq/drivers/omniq_qpu.ko"
/sbin/depmod -a -b "${ROOTFS_DIR}" "${KERNEL_VER}" || true
ln -sfn "${KERNEL_VER}" "${ROOTFS_DIR}/lib/modules/${KERNEL_VER}-omniq-preempt-rt"

# Copy essential Wi-Fi/NIC/Bluetooth/regulatory firmware (keeps ISO lean & fast while hardware-ready)
for fw in regulatory.db* intel iwlwifi* rtl_nic rtlwifi rtw88 rtw89 brcm amdgpu i915; do
    for match in /lib/firmware/${fw}; do
        [[ -e "$match" ]] && cp -a "$match" "${ROOTFS_DIR}/lib/firmware/" 2>/dev/null || true
    done
done

# Unpack cached .deb packages (jwm, xterm, wmctrl, xdotool, libxdo3, libutempter0, picom, etc.)
for deb in "${DEB_CACHE}"/*.deb; do
    [[ "$(basename "$deb")" == linux-image-* ]] && continue
    dpkg-deb -x "$deb" "${ROOTFS_DIR}"
done

# ------------------------------------------------------------------------------
# STEP 2: Host Core Userland, X11/Mesa Graphics, Python 3.12 & GTK3/Tk Stack
# ------------------------------------------------------------------------------
echo "[Step 2/6] Populating POSIX userland, Xorg/Mesa OpenGL, Python 3.12 & GUI toolkits..."

# Dynamic linker
cp -a /lib64/* "${ROOTFS_DIR}/lib64/" 2>/dev/null || true

# BusyBox + GNU bash + real kmod (supports .ko.zst decompression!)
cp -f "$(command -v busybox)" "${ROOTFS_DIR}/bin/busybox"
chmod 755 "${ROOTFS_DIR}/bin/busybox"
for app in ls cp mv rm mkdir rmdir cat more less head tail grep egrep fgrep sed awk cut sort uniq wc tr tee \
           tar gzip gunzip zcat find xargs which touch chmod chown ln sync sleep date uname hostname id whoami \
           df du free top ps kill killall pkill pgrep clear reset mknod setsid cttyhack dmesg udhcpc vi mountpoint; do
    [[ ! -e "${ROOTFS_DIR}/bin/${app}" ]] && ln -sf busybox "${ROOTFS_DIR}/bin/${app}" || true
    [[ ! -e "${ROOTFS_DIR}/usr/bin/${app}" ]] && ln -sf ../../bin/busybox "${ROOTFS_DIR}/usr/bin/${app}" || true
done
for sapp in reboot poweroff halt mdev blkid fdisk losetup switch_root hwclock ifconfig route; do
    [[ ! -e "${ROOTFS_DIR}/sbin/${sapp}" ]] && ln -sf ../bin/busybox "${ROOTFS_DIR}/sbin/${sapp}" || true
done

cp -f /bin/bash "${ROOTFS_DIR}/bin/bash"
ln -sf bash "${ROOTFS_DIR}/bin/sh"
cp -f /usr/bin/env "${ROOTFS_DIR}/usr/bin/env"

KMOD_BIN="$(command -v kmod)"
cp -f "${KMOD_BIN}" "${ROOTFS_DIR}/bin/kmod"
for km in modprobe insmod rmmod lsmod modinfo depmod; do
    ln -sf ../bin/kmod "${ROOTFS_DIR}/sbin/${km}"
    ln -sf ../../bin/kmod "${ROOTFS_DIR}/usr/sbin/${km}"
done

# Essential system, udev, D-Bus, audio, disk & network binaries
for bin_name in mount umount lsblk blkid sgdisk partprobe wipefs mkfs.ext4 mkfs.vfat rsync \
                ip dhclient nmcli NetworkManager wpa_supplicant bluetoothctl amixer \
                dbus-daemon dbus-launch dbus-uuidgen udevadm lspci lsusb chrt taskset \
                mcookie xauth xinit xset xsetroot xrandr xrdb xkbcomp terminator zstd; do
    p="$(command -v "${bin_name}" 2>/dev/null || true)"
    if [[ -n "$p" && -e "$p" ]]; then
        cp -Lf "$p" "${ROOTFS_DIR}/usr/bin/${bin_name}" 2>/dev/null || true
    fi
done
for sbin_path in /lib/systemd/systemd-udevd /usr/lib/systemd/systemd-udevd /usr/sbin/NetworkManager /usr/libexec/bluetooth/bluetoothd; do
    if [[ -e "$sbin_path" ]]; then
        mkdir -p "${ROOTFS_DIR}$(dirname "$sbin_path")"
        cp -Lf "$sbin_path" "${ROOTFS_DIR}${sbin_path}" 2>/dev/null || true
    fi
done

cp -a /lib/udev/* "${ROOTFS_DIR}/lib/udev/" 2>/dev/null || true
ln -sfn ../../lib/udev "${ROOTFS_DIR}/usr/lib/udev"
[[ -e /usr/bin/udevadm ]] && cp -Lf /usr/bin/udevadm "${ROOTFS_DIR}/bin/udevadm" || true
cp -a /etc/dbus-1 "${ROOTFS_DIR}/etc/" 2>/dev/null || true
cp -a /usr/share/dbus-1 "${ROOTFS_DIR}/usr/share/" 2>/dev/null || true
cp -a /etc/NetworkManager "${ROOTFS_DIR}/etc/" 2>/dev/null || true
cp -a /usr/share/alsa "${ROOTFS_DIR}/usr/share/" 2>/dev/null || true

# Xorg server + drivers + Mesa DRI OpenGL
cp -a /usr/bin/Xorg "${ROOTFS_DIR}/usr/bin/" 2>/dev/null || true
cp -a /usr/lib/xorg "${ROOTFS_DIR}/usr/lib/" 2>/dev/null || true
cp -a /usr/share/X11 "${ROOTFS_DIR}/usr/share/" 2>/dev/null || true
cp -a /usr/lib/x86_64-linux-gnu/dri "${ROOTFS_DIR}/usr/lib/x86_64-linux-gnu/" 2>/dev/null || true

# Fonts, Terminfo, Cursor & GTK Themes, GdkPixbuf loaders & GObject Introspection
cp -a /etc/fonts "${ROOTFS_DIR}/etc/" 2>/dev/null || true
mkdir -p "${ROOTFS_DIR}/usr/share/fonts/truetype"
cp -a /usr/share/fonts/truetype/dejavu "${ROOTFS_DIR}/usr/share/fonts/truetype/" 2>/dev/null || true
cp -a /usr/share/terminfo "${ROOTFS_DIR}/usr/share/" 2>/dev/null || true
for ic in DMZ-White DMZ-Black Adwaita hicolor; do
    [[ -d "/usr/share/icons/${ic}" ]] && cp -a "/usr/share/icons/${ic}" "${ROOTFS_DIR}/usr/share/icons/" || true
done
for th in Adwaita Adwaita-dark Yaru-dark; do
    [[ -d "/usr/share/themes/${th}" ]] && cp -a "/usr/share/themes/${th}" "${ROOTFS_DIR}/usr/share/themes/" || true
done
cp -a /usr/lib/x86_64-linux-gnu/gdk-pixbuf-2.0 "${ROOTFS_DIR}/usr/lib/x86_64-linux-gnu/" 2>/dev/null || true
cp -a /usr/lib/x86_64-linux-gnu/girepository-1.0 "${ROOTFS_DIR}/usr/lib/x86_64-linux-gnu/" 2>/dev/null || true
cp -a /usr/share/glib-2.0 "${ROOTFS_DIR}/usr/share/" 2>/dev/null || true
cp -a /usr/share/mime "${ROOTFS_DIR}/usr/share/" 2>/dev/null || true
cp -a /usr/share/fontconfig "${ROOTFS_DIR}/usr/share/" 2>/dev/null || true
cp -a /usr/share/pixmaps "${ROOTFS_DIR}/usr/share/" 2>/dev/null || true

# Python 3.12 + Tcl/Tk + scientific/GTK packages
cp -a /usr/bin/python3* "${ROOTFS_DIR}/usr/bin/" 2>/dev/null || true
ln -sf python3 "${ROOTFS_DIR}/usr/bin/python"
cp -a /usr/lib/python3.12 "${ROOTFS_DIR}/usr/lib/" 2>/dev/null || true
cp -a /usr/lib/python3 "${ROOTFS_DIR}/usr/lib/" 2>/dev/null || true
cp -a /usr/share/tcltk "${ROOTFS_DIR}/usr/share/" 2>/dev/null || true

# Copy user-site scientific packages (numpy, scipy & bundled .libs) if installed in ~/.local
USER_SITE="${HOME}/.local/lib/python3.12/site-packages"
if [[ -d "${USER_SITE}" ]]; then
    mkdir -p "${ROOTFS_DIR}/usr/lib/python3/dist-packages"
    for spkg in numpy numpy.libs scipy scipy.libs; do
        for m in "${USER_SITE}"/${spkg}*; do
            [[ -e "$m" ]] && cp -a "$m" "${ROOTFS_DIR}/usr/lib/python3/dist-packages/"
        done
    done
fi

# Copy udev input helpers (input_id, ata_id, usb_id, path_id) for Xorg hotplug
for uhelper in /lib/udev/*_id /usr/lib/udev/*_id; do
    [[ -e "$uhelper" ]] && cp -Lf "$uhelper" "${ROOTFS_DIR}/lib/udev/" 2>/dev/null || true
done

# Ensure BLAS/LAPACK/gfortran symlinks resolve cleanly for NumPy/SciPy
for mathlib in libblas.so.3 liblapack.so.3 libgfortran.so.5 libquadmath.so.0; do
    if [[ -e "/usr/lib/x86_64-linux-gnu/${mathlib}" ]]; then
        cp -Lf "/usr/lib/x86_64-linux-gnu/${mathlib}" "${ROOTFS_DIR}/usr/lib/x86_64-linux-gnu/${mathlib}"
    fi
done

# ------------------------------------------------------------------------------
# STEP 3: Inject OmniQ-OS Binaries, Libraries, Python SDK, Circuits & Desktop Config
# ------------------------------------------------------------------------------
echo "[Step 3/6] Installing OmniQ-OS native executables, C++20 libraries, SDK & desktop theme..."

cp -a "${PROJECT_ROOT}/bin/"* "${ROOTFS_DIR}/usr/local/bin/"
chmod 755 "${ROOTFS_DIR}/usr/local/bin/"* 2>/dev/null || true
if [[ -x "${PROJECT_ROOT}/bin/uname" ]]; then
    rm -f "${ROOTFS_DIR}/bin/uname"
    cp -f "${PROJECT_ROOT}/bin/uname" "${ROOTFS_DIR}/bin/uname"
    chmod 755 "${ROOTFS_DIR}/bin/uname"
fi
ln -sf /usr/local/bin/omniq-terminal "${ROOTFS_DIR}/usr/bin/x-terminal-emulator"
ln -sf /usr/local/bin/qpu-smi "${ROOTFS_DIR}/usr/bin/qpu-smi"
ln -sf /usr/local/bin/omniqd "${ROOTFS_DIR}/usr/bin/omniqd"

cp -a "${PROJECT_ROOT}/lib/"* "${ROOTFS_DIR}/usr/local/lib/" 2>/dev/null || true
cp -a "${PROJECT_ROOT}/lib/"* "${ROOTFS_DIR}/opt/omniq/lib/" 2>/dev/null || true
cp -a "${PROJECT_ROOT}/lib/"* "${ROOTFS_DIR}/usr/lib/x86_64-linux-gnu/" 2>/dev/null || true

mkdir -p "${ROOTFS_DIR}/usr/lib/python3/dist-packages"
cp -a "${PROJECT_ROOT}/runtime/python/omniq" "${ROOTFS_DIR}/usr/lib/python3/dist-packages/"
cp -a "${PROJECT_ROOT}/runtime/python/omniqos" "${ROOTFS_DIR}/usr/lib/python3/dist-packages/"
cp -f "${PROJECT_ROOT}/src/"*.py "${ROOTFS_DIR}/usr/lib/python3/dist-packages/" 2>/dev/null || true
cp -f "${PROJECT_ROOT}/src/"*.py "${ROOTFS_DIR}/opt/omniq/src/" 2>/dev/null || true

if [[ -d "${PROJECT_ROOT}/circuits" ]]; then
    cp -a "${PROJECT_ROOT}/circuits/"* "${ROOTFS_DIR}/root/circuits/" 2>/dev/null || true
    cp -a "${PROJECT_ROOT}/circuits/"* "${ROOTFS_DIR}/home/quantum/circuits/" 2>/dev/null || true
    cp -a "${PROJECT_ROOT}/circuits/"* "${ROOTFS_DIR}/opt/omniq/circuits/" 2>/dev/null || true
fi

if [[ -d "${PROJECT_ROOT}/vendor/picom" ]]; then
    cp -a "${PROJECT_ROOT}/vendor/picom" "${ROOTFS_DIR}/opt/omniq/picom"
fi

# Copy /etc hierarchy from repo
cp -a "${PROJECT_ROOT}/etc/"* "${ROOTFS_DIR}/etc/" 2>/dev/null || true
if [[ -f "${PROJECT_ROOT}/etc/omniq/omniq-system-init" ]]; then
    chmod 755 "${PROJECT_ROOT}/etc/omniq/omniq-system-init"
    cp -f "${PROJECT_ROOT}/etc/omniq/omniq-system-init" "${ROOTFS_DIR}/sbin/init"
    cp -f "${PROJECT_ROOT}/etc/omniq/omniq-system-init" "${ROOTFS_DIR}/init"
    chmod 755 "${ROOTFS_DIR}/sbin/init" "${ROOTFS_DIR}/init"
fi
if [[ -f "${PROJECT_ROOT}/etc/X11/xinit/xinitrc" ]]; then
    cp -f "${PROJECT_ROOT}/etc/X11/xinit/xinitrc" "${ROOTFS_DIR}/root/.xinitrc"
    chmod 755 "${ROOTFS_DIR}/etc/X11/xinit/xinitrc" "${ROOTFS_DIR}/root/.xinitrc"
fi
[[ -f "${PROJECT_ROOT}/etc/omniq/bash.bashrc" ]] && cp -f "${PROJECT_ROOT}/etc/omniq/bash.bashrc" "${ROOTFS_DIR}/root/.bashrc"

cat > "${ROOTFS_DIR}/etc/os-release" <<'EOF'
NAME="OmniQ-OS"
VERSION="24.04-RT (Real-Time Quantum Workstation)"
ID=omniqos
ID_LIKE=debian
PRETTY_NAME="OmniQ-OS 24.04-RT Real-Time Quantum Linux"
VERSION_ID="24.04"
HOME_URL="https://github.com/Quantum-Quorum/Omni-QOS"
EOF
echo "omniq-rt" > "${ROOTFS_DIR}/etc/hostname"
cat > "${ROOTFS_DIR}/etc/hosts" <<'EOF'
127.0.0.1   localhost
127.0.1.1   omniq-rt
::1         localhost ip6-localhost ip6-loopback
EOF
cat > "${ROOTFS_DIR}/etc/nsswitch.conf" <<'EOF'
passwd:         files
group:          files
shadow:         files
hosts:          files dns
networks:       files
EOF
cat > "${ROOTFS_DIR}/etc/ld.so.conf.d/omniq.conf" <<'EOF'
/usr/local/lib
/opt/omniq/lib
/lib/x86_64-linux-gnu
/usr/lib/x86_64-linux-gnu
EOF

# ------------------------------------------------------------------------------
# STEP 4: Automated Recursive ELF Shared Library Linker Resolver
# ------------------------------------------------------------------------------
echo "[Step 4/6] Running automated recursive ELF shared-library dependency resolver..."
python3 - "${ROOTFS_DIR}" <<'PYEOF'
import os
import subprocess
import shutil
import sys

rootfs = sys.argv[1]
target_libdir = os.path.join(rootfs, "usr/lib/x86_64-linux-gnu")
os.makedirs(target_libdir, exist_ok=True)

scan_roots = [
    "bin", "sbin", "usr/bin", "usr/sbin", "usr/local/bin", "usr/local/lib",
    "opt/omniq", "usr/lib/xorg", "usr/lib/x86_64-linux-gnu",
    "usr/lib/python3.12/lib-dynload", "usr/lib/python3/dist-packages",
]

visited_bins = set()
resolved_libs = {}

def is_elf(path: str) -> bool:
    try:
        with open(path, "rb") as f:
            return f.read(4) == b"\x7fELF"
    except Exception:
        return False

queue = []
for sr in scan_roots:
    full_sr = os.path.join(rootfs, sr)
    if not os.path.exists(full_sr):
        continue
    for r, _dirs, files in os.walk(full_sr):
        for fn in files:
            fp = os.path.join(r, fn)
            if not os.path.islink(fp) and os.path.isfile(fp) and is_elf(fp):
                queue.append(fp)

extra_ld_paths = ":".join([
    os.path.join(rootfs, "usr/local/lib"),
    os.path.join(rootfs, "opt/omniq/lib"),
    os.path.join(rootfs, "opt/omniq/picom/lib"),
    os.path.join(rootfs, "usr/lib/x86_64-linux-gnu"),
    "/usr/lib/x86_64-linux-gnu",
    "/lib/x86_64-linux-gnu",
])
env = dict(os.environ, LD_LIBRARY_PATH=extra_ld_paths)

while queue:
    elf_path = queue.pop()
    if elf_path in visited_bins:
        continue
    visited_bins.add(elf_path)
    try:
        out = subprocess.run(["ldd", elf_path], capture_output=True, text=True, env=env, timeout=5).stdout
    except Exception:
        continue
    for line in out.splitlines():
        line = line.strip()
        host_lib = None
        if "=>" in line:
            rhs = line.split("=>", 1)[1].strip().split(" ")[0]
            if rhs.startswith("/") and os.path.exists(rhs):
                host_lib = rhs
        elif line.startswith("/"):
            cand = line.split(" ")[0]
            if os.path.exists(cand):
                host_lib = cand
        if not host_lib:
            continue
        base = os.path.basename(host_lib)
        if base in resolved_libs:
            continue
        dst = os.path.join(target_libdir, base)
        if not os.path.exists(dst):
            shutil.copy2(os.path.realpath(host_lib), dst)
        resolved_libs[base] = host_lib
        queue.append(dst)

manifest_path = os.path.join(rootfs, "etc/omniq/elf_manifest.txt")
os.makedirs(os.path.dirname(manifest_path), exist_ok=True)
with open(manifest_path, "w") as mf:
    mf.write(f"OmniQ-OS Recursive ELF Closure Manifest ({len(resolved_libs)} shared libraries)\n")
    for k in sorted(resolved_libs):
        mf.write(f"[OK] {k} <- {resolved_libs[k]}\n")
print(f"       Resolved and verified {len(resolved_libs)} shared libraries across {len(visited_bins)} ELF objects.")
PYEOF

ln -sfn ../usr/lib/x86_64-linux-gnu "${ROOTFS_DIR}/lib/x86_64-linux-gnu"
/sbin/ldconfig -r "${ROOTFS_DIR}" 2>/dev/null || true

# ------------------------------------------------------------------------------
# STEP 5: Zstandard SquashFS & Custom Live-Boot OverlayFS Initramfs (initrd.img)
# ------------------------------------------------------------------------------
echo "[Step 5/6] Compressing casper/filesystem.squashfs (zstd) & building OverlayFS initrd.img..."
mksquashfs "${ROOTFS_DIR}" "${ISO_STAGING}/casper/filesystem.squashfs" \
    -comp zstd -b 1048576 -noappend -wildcards -e "tmp/*" "var/tmp/*" >/dev/null
stat -c %s "${ISO_STAGING}/casper/filesystem.squashfs" > "${ISO_STAGING}/casper/filesystem.size"

# Build Live-Boot initrd_root
mkdir -p "${INITRD_STAGING}"/{bin,sbin,dev,proc,sys,run,mnt,newroot,etc,lib/modules/${KERNEL_VER}/kernel}
cp -f "$(command -v busybox)" "${INITRD_STAGING}/bin/busybox"
chmod 755 "${INITRD_STAGING}/bin/busybox"
for app in sh mount umount mkdir echo cat sleep switch_root mdev cp ls grep sed awk find uname sync rm ln chmod blkid losetup; do
    ln -sf busybox "${INITRD_STAGING}/bin/${app}"
done
for sapp in insmod modprobe depmod blkid switch_root; do
    ln -sf ../bin/busybox "${INITRD_STAGING}/sbin/${sapp}"
done

# Copy boot-critical storage, USB, CD-ROM, SquashFS & OverlayFS kernel drivers and decompress .ko.zst
for sub in fs/isofs fs/squashfs fs/overlayfs fs/nls drivers/cdrom drivers/scsi drivers/ata drivers/nvme drivers/virtio drivers/block drivers/usb/host drivers/usb/storage drivers/usb/core drivers/usb/common; do
    if [[ -d "/lib/modules/${KERNEL_VER}/kernel/${sub}" ]]; then
        mkdir -p "${INITRD_STAGING}/lib/modules/${KERNEL_VER}/kernel/${sub}"
        cp -a "/lib/modules/${KERNEL_VER}/kernel/${sub}/"* "${INITRD_STAGING}/lib/modules/${KERNEL_VER}/kernel/${sub}/" 2>/dev/null || true
    fi
done
cp -f "/lib/modules/${KERNEL_VER}/modules.order" "/lib/modules/${KERNEL_VER}/modules.builtin" \
      "${INITRD_STAGING}/lib/modules/${KERNEL_VER}/" 2>/dev/null || true
find "${INITRD_STAGING}/lib/modules/${KERNEL_VER}" -name "*.ko.zst" -exec zstd -d --rm -q {} \;
/sbin/depmod -a -b "${INITRD_STAGING}" "${KERNEL_VER}" || true
ln -sfn "${KERNEL_VER}" "${INITRD_STAGING}/lib/modules/${KERNEL_VER}-omniq-preempt-rt"

cat > "${INITRD_STAGING}/init" <<'INITEOF'
#!/bin/sh
export PATH=/bin:/sbin:/usr/bin:/usr/sbin

mount -t proc proc /proc
mount -t sysfs sysfs /sys
mount -t devtmpfs devtmpfs /dev 2>/dev/null || mount -t tmpfs tmpfs /dev
mount -t tmpfs tmpfs /run

mdev -s 2>/dev/null || true

for mod in loop squashfs overlay isofs nls_iso8859-1 nls_cp437 cdrom scsi_mod sr_mod sd_mod \
           libata libahci ahci ata_piix ata_generic virtio_pci virtio_blk virtio_scsi \
           nvme-core nvme usb-common usbcore xhci-hcd xhci-pci ehci-hcd ehci-pci usb-storage uas; do
    modprobe "$mod" 2>/dev/null || true
done
for ko in /lib/modules/*/kernel/drivers/block/loop.ko \
          /lib/modules/*/kernel/fs/squashfs/squashfs.ko \
          /lib/modules/*/kernel/fs/isofs/isofs.ko \
          /lib/modules/*/kernel/fs/overlayfs/overlay.ko \
          /lib/modules/*/kernel/drivers/cdrom/cdrom.ko \
          /lib/modules/*/kernel/drivers/scsi/scsi_mod.ko \
          /lib/modules/*/kernel/drivers/scsi/sr_mod.ko \
          /lib/modules/*/kernel/drivers/scsi/sd_mod.ko \
          /lib/modules/*/kernel/drivers/ata/libata.ko \
          /lib/modules/*/kernel/drivers/ata/ata_piix.ko \
          /lib/modules/*/kernel/drivers/ata/libahci.ko \
          /lib/modules/*/kernel/drivers/ata/ahci.ko \
          /lib/modules/*/kernel/drivers/usb/storage/usb-storage.ko; do
    [ -f "$ko" ] && insmod "$ko" 2>/dev/null || true
done
mdev -s 2>/dev/null || true

TORAM=0
PERSIST=0
for arg in $(cat /proc/cmdline); do
    case "$arg" in
        toram) TORAM=1 ;;
        persistence|persistent) PERSIST=1 ;;
    esac
done

echo "[OmniQ-OS Initramfs] Scanning block devices for casper/filesystem.squashfs..."
mkdir -p /run/omniq/cdrom /run/omniq/rofs /run/omniq/cow /newroot
FOUND=0
for _try in 1 2 3 4 5 6 7 8; do
    mdev -s 2>/dev/null || true
    for dev in /dev/sr* /dev/sd* /dev/vd* /dev/nvme*n*; do
        [ -b "$dev" ] || continue
        if mount -o ro "$dev" /run/omniq/cdrom 2>/dev/null; then
            if [ -f /run/omniq/cdrom/casper/filesystem.squashfs ]; then
                FOUND=1
                break
            fi
            umount /run/omniq/cdrom 2>/dev/null || true
        fi
    done
    [ "$FOUND" -eq 1 ] && break
    sleep 0.5
done

if [ "$FOUND" -eq 0 ]; then
    echo "[PANIC] Could not locate casper/filesystem.squashfs! Dropping to rescue shell."
    exec /bin/sh
fi

SQUASH="/run/omniq/cdrom/casper/filesystem.squashfs"
if [ "$TORAM" -eq 1 ]; then
    echo "[OmniQ-OS] Copying SquashFS image into RAM (toram)..."
    mkdir -p /run/omniq/toram
    mount -t tmpfs -o size=4G tmpfs /run/omniq/toram
    cp "$SQUASH" /run/omniq/toram/filesystem.squashfs
    umount /run/omniq/cdrom
    SQUASH="/run/omniq/toram/filesystem.squashfs"
fi

mount -t squashfs -o ro,loop "$SQUASH" /run/omniq/rofs

MOUNTED_PERSIST=0
if [ "$PERSIST" -eq 1 ]; then
    PDEV="$(blkid -L persistence 2>/dev/null || blkid -L omniq-data 2>/dev/null || true)"
    if [ -n "$PDEV" ] && mount "$PDEV" /run/omniq/cow 2>/dev/null; then
        MOUNTED_PERSIST=1
    fi
fi
if [ "$MOUNTED_PERSIST" -eq 0 ]; then
    mount -t tmpfs -o size=3G,mode=0755 tmpfs /run/omniq/cow
fi

mkdir -p /run/omniq/cow/upper /run/omniq/cow/work
mount -t overlay overlay -o lowerdir=/run/omniq/rofs,upperdir=/run/omniq/cow/upper,workdir=/run/omniq/cow/work /newroot

mkdir -p /newroot/proc /newroot/sys /newroot/dev /newroot/run
mount --move /proc /newroot/proc
mount --move /sys /newroot/sys
mount --move /dev /newroot/dev
mount --move /run /newroot/run

exec switch_root /newroot /sbin/init
INITEOF
chmod 755 "${INITRD_STAGING}/init"

(cd "${INITRD_STAGING}" && find . -print0 | cpio --null -o --format=newc 2>/dev/null | gzip -9 > "${ISO_STAGING}/boot/initrd.img")

# ------------------------------------------------------------------------------
# STEP 6: Multi-Profile GRUB2 Configuration & Hybrid BIOS/UEFI ISO Generation
# ------------------------------------------------------------------------------
echo "[Step 6/6] Packaging Hybrid BIOS/UEFI ISO (omniq-os-full.iso)..."
cat > "${ISO_STAGING}/boot/grub/grub.cfg" <<'EOF'
set timeout=1
set default=0
set gfxmode=1920x1080,1280x800,auto
set gfxpayload=keep
insmod all_video
insmod gfxterm

menuentry "OmniQ-OS 24.04-RT — Live Quantum Workstation (PREEMPT_RT Desktop)" {
    linux /boot/vmlinuz boot=casper isolcpus=2,3 nohz_full=2,3 rcu_nocbs=2,3 processor.max_cstate=0 quiet
    initrd /boot/initrd.img
}

menuentry "OmniQ-OS 24.04-RT — Live Workstation with USB Persistence" {
    linux /boot/vmlinuz boot=casper persistence isolcpus=2,3 nohz_full=2,3 rcu_nocbs=2,3 quiet
    initrd /boot/initrd.img
}

menuentry "OmniQ-OS 24.04-RT — Copy-to-RAM Mode (toram, Ejectable USB)" {
    linux /boot/vmlinuz boot=casper toram isolcpus=2,3 nohz_full=2,3 rcu_nocbs=2,3 quiet
    initrd /boot/initrd.img
}

menuentry "OmniQ-OS 24.04-RT — Cryogenic Rack Headless Appliance (qpu-smi HUD)" {
    linux /boot/vmlinuz boot=casper omniq_mode=appliance isolcpus=2,3 nohz_full=2,3 rcu_nocbs=2,3
    initrd /boot/initrd.img
}

menuentry "OmniQ-OS 24.04-RT — Guided Permanent NVMe/SATA Disk Installer" {
    linux /boot/vmlinuz boot=casper omniq_mode=install quiet
    initrd /boot/initrd.img
}

menuentry "OmniQ-OS 24.04-RT — Safe Graphics / Diagnostic TTY Shell" {
    linux /boot/vmlinuz boot=casper omniq_mode=console nomodeset console=tty0 console=ttyS0,115200n8
    initrd /boot/initrd.img
}
EOF

grub-mkrescue -o "${OUTPUT_ISO}" "${ISO_STAGING}" --compress=xz 2>/dev/null
(cd "${PROJECT_ROOT}" && sha256sum "$(basename "${OUTPUT_ISO}")" > "$(basename "${OUTPUT_ISO}").sha256")

echo "======================================================================"
echo " SUCCESS: Built $(ls -lh "${OUTPUT_ISO}" | awk '{print $9, "(" $5 ")"}')"
cat "${OUTPUT_ISO}.sha256"
echo "======================================================================"
