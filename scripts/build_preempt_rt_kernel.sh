#!/usr/bin/env bash

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "${SCRIPT_DIR}/.." && pwd)"
QPU_DRV_DIR="${REPO_ROOT}/kernel/drivers/accel/qpu"
STAGE_DIR="${REPO_ROOT}/build/rt_kernel"
SRC_WORK_DIR="${STAGE_DIR}/src"
DEFAULT_KVER="6.12.6"

usage() {
    cat <<'EOF'
Usage: build_preempt_rt_kernel.sh [--test] [--check-current] [--build [KERNEL_VERSION]] [--help]

Options:
  --test                  Run bash -n syntax verification, generate RT kconfig fragment, and check host
  --check-current         Inspect current kernel preemption model, timers, isolated cores, and QPU module
  --build [VERSION]       Download & compile Linux PREEMPT_RT bzImage + omniq_qpu.ko (default: 6.12.6)
  -h, --help              Show this help message
EOF
}

write_rt_kconfig_fragment() {
    mkdir -p "${STAGE_DIR}"
    local frag="${STAGE_DIR}/omniq-preempt-rt.config"
    cat > "${frag}" <<'EOF'
# OmniQ-OS v2.0 Hard Real-Time Quantum Control Kernel Configuration
CONFIG_EXPERT=y
CONFIG_PREEMPT_RT=y
CONFIG_HIGH_RES_TIMERS=y
CONFIG_NO_HZ_FULL=y
CONFIG_RCU_NOCB_CPU=y
CONFIG_CPU_ISOLATION=y
CONFIG_HZ_1000=y
CONFIG_HZ=1000
CONFIG_IRQ_FORCED_THREADING=y
CONFIG_PCI=y
CONFIG_PCI_MSI=y
EOF
    cat > "${STAGE_DIR}/grub_rt_cmdline.txt" <<'EOF'
isolcpus=managed_irq,domain,2,3 nohz_full=2,3 rcu_nocbs=2,3 irqaffinity=0,1 skew_tick=1 tsc=reliable processor.max_cstate=0 idle=poll
EOF
    echo "${frag}"
}

check_current_kernel() {
    local krel kver rt_sysfs="0" kcfg=""
    krel="$(uname -r)"
    kver="$(uname -v)"
    if [[ -r /sys/kernel/realtime ]]; then
        rt_sysfs="$(tr -d '[:space:]' < /sys/kernel/realtime || echo 0)"
    fi

    if [[ -r "/boot/config-${krel}" ]]; then
        kcfg="/boot/config-${krel}"
    fi

    echo "[OmniQ-RT] Host Kernel Release : ${krel}"
    echo "[OmniQ-RT] Host Kernel Version : ${kver}"
    echo "[OmniQ-RT] /sys/kernel/realtime: ${rt_sysfs}"
    if [[ -e /dev/cpu_dma_latency ]]; then
        echo "[OmniQ-RT] /dev/cpu_dma_latency: present"
    else
        echo "[OmniQ-RT] /dev/cpu_dma_latency: absent"
    fi

    if [[ -r /sys/devices/system/cpu/isolated ]]; then
        local iso
        iso="$(tr -d '\n' < /sys/devices/system/cpu/isolated)"
        echo "[OmniQ-RT] Isolated CPUs       : ${iso:-none}"
    fi

    if [[ -n "${kcfg}" ]]; then
        echo "[OmniQ-RT] Kernel Config Audit (${kcfg}):"
        for sym in CONFIG_PREEMPT_RT CONFIG_PREEMPT_DYNAMIC CONFIG_HIGH_RES_TIMERS CONFIG_NO_HZ_FULL CONFIG_RCU_NOCB_CPU; do
            local val
            val="$(grep -E "^${sym}=" "${kcfg}" 2>/dev/null || echo "${sym}=not_set")"
            echo "           ${val}"
        done
    fi

    if [[ -f "${QPU_DRV_DIR}/omniq_qpu.ko" ]]; then
        echo "[OmniQ-RT] Driver Module       : ${QPU_DRV_DIR}/omniq_qpu.ko (BUILT)"
    else
        echo "[OmniQ-RT] Driver Module       : omniq_qpu.ko (not yet built)"
    fi
}

build_rt_kernel() {
    local kver="${1:-${DEFAULT_KVER}}"
    local major minor
    major="$(echo "${kver}" | cut -d. -f1)"
    minor="$(echo "${kver}" | cut -d. -f2)"

    mkdir -p "${STAGE_DIR}" "${SRC_WORK_DIR}"
    local frag
    frag="$(write_rt_kconfig_fragment)"
    echo "[OmniQ-RT] Staging RT kconfig fragment at ${frag}"

    local tarball="linux-${kver}.tar.xz"
    local url="https://cdn.kernel.org/pub/linux/kernel/v${major}.x/${tarball}"
    local ksrc="${SRC_WORK_DIR}/linux-${kver}"

    if [[ ! -d "${ksrc}" ]]; then
        echo "[OmniQ-RT] Downloading mainline Linux ${kver} from ${url} ..."
        curl -fL --retry 3 -o "${SRC_WORK_DIR}/${tarball}" "${url}"
        tar -xf "${SRC_WORK_DIR}/${tarball}" -C "${SRC_WORK_DIR}"
    fi

    if (( major < 6 || (major == 6 && minor < 12) )); then
        local rt_base="https://cdn.kernel.org/pub/linux/kernel/projects/rt/${major}.${minor}"
        echo "[OmniQ-RT] Discovering PREEMPT_RT patch for Linux ${major}.${minor} from ${rt_base} ..."
        local patch_name
        patch_name="$(curl -fsSL "${rt_base}/" | grep -oE "patch-${kver}-rt[0-9]+\.patch\.xz" | sort -V | tail -n1 || true)"
        if [[ -n "${patch_name}" && ! -f "${ksrc}/.omniq_rt_patched" ]]; then
            curl -fL --retry 3 -o "${SRC_WORK_DIR}/${patch_name}" "${rt_base}/${patch_name}"
            xzcat "${SRC_WORK_DIR}/${patch_name}" | patch -d "${ksrc}" -p1
            touch "${ksrc}/.omniq_rt_patched"
        fi
    else
        echo "[OmniQ-RT] Linux ${kver} >= 6.12 detected: using native mainline CONFIG_PREEMPT_RT=y"
    fi

    pushd "${ksrc}" >/dev/null
    if [[ -r "/boot/config-$(uname -r)" ]]; then
        cp "/boot/config-$(uname -r)" .config
        make olddefconfig
    else
        make x86_64_defconfig
    fi

    ./scripts/config --enable EXPERT \
                     --enable PREEMPT_RT \
                     --enable HIGH_RES_TIMERS \
                     --enable NO_HZ_FULL \
                     --enable RCU_NOCB_CPU \
                     --enable CPU_ISOLATION \
                     --enable HZ_1000 \
                     --set-val HZ 1000 \
                     --disable SYSTEM_TRUSTED_KEYS \
                     --disable SYSTEM_REVOCATION_KEYS

    if ! command -v pahole >/dev/null 2>&1; then
        ./scripts/config --disable DEBUG_INFO_BTF
    fi

    make olddefconfig
    echo "[OmniQ-RT] Compiling PREEMPT_RT bzImage and module build tree (-j$(nproc)) ..."
    make -j"$(nproc)" bzImage modules_prepare

    echo "[OmniQ-RT] Compiling out-of-tree omniq_qpu.ko against ${ksrc} ..."
    make -C "${ksrc}" M="${QPU_DRV_DIR}" modules

    cp -f arch/x86/boot/bzImage "${STAGE_DIR}/bzImage-${kver}-omniq-rt"
    cp -f .config "${STAGE_DIR}/config-${kver}-omniq-rt"
    cp -f System.map "${STAGE_DIR}/System.map-${kver}-omniq-rt"
    cp -f "${QPU_DRV_DIR}/omniq_qpu.ko" "${STAGE_DIR}/omniq_qpu.ko"
    popd >/dev/null

    echo "[PASS] Staged PREEMPT_RT kernel + omniq_qpu.ko in ${STAGE_DIR}"
}

run_self_test() {
    echo "[TEST] Running bash -n syntax check on ${BASH_SOURCE[0]} ..."
    bash -n "${BASH_SOURCE[0]}"

    if [[ ! -f "${QPU_DRV_DIR}/qpu_pci.c" || ! -f "${QPU_DRV_DIR}/Makefile" ]]; then
        echo "[FAIL] Missing QPU kernel driver sources in ${QPU_DRV_DIR}" >&2
        exit 1
    fi

    local frag
    frag="$(write_rt_kconfig_fragment)"
    for required_sym in CONFIG_PREEMPT_RT=y CONFIG_HIGH_RES_TIMERS=y CONFIG_NO_HZ_FULL=y CONFIG_RCU_NOCB_CPU=y; do
        if ! grep -q "^${required_sym}$" "${frag}"; then
            echo "[FAIL] Missing ${required_sym} in ${frag}" >&2
            exit 1
        fi
    done

    check_current_kernel
    echo "[PASS] build_preempt_rt_kernel.sh syntax, kconfig fragment (${frag}), and staging layout verified."
}

if [[ $# -eq 0 ]]; then
    check_current_kernel
    exit 0
fi

while [[ $# -gt 0 ]]; do
    case "$1" in
        --test)
            run_self_test
            exit 0
            ;;
        --check-current)
            check_current_kernel
            exit 0
            ;;
        --build)
            shift
            target_kver="${1:-${DEFAULT_KVER}}"
            build_rt_kernel "${target_kver}"
            exit 0
            ;;
        -h|--help)
            usage
            exit 0
            ;;
        *)
            echo "Unknown option: $1" >&2
            usage >&2
            exit 1
            ;;
    esac
done
