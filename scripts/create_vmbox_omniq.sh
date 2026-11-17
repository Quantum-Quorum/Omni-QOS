#!/usr/bin/env bash
# SPDX-License-Identifier: Apache-2.0
# OmniQ-OS VirtualBox ("vmbox") Automated VM Provisioner, Boot Controller & Screenshot Verifier
# Commands: start (default) | headless | stop | status | screenshot | recreate

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "${SCRIPT_DIR}/.." && pwd)"
VM_NAME="OmniQ-OS"
VDI_PATH="${PROJECT_ROOT}/omniq-quantum-disk.vdi"
FULL_ISO="${PROJECT_ROOT}/omniq-os-full.iso"
MIN_ISO="${PROJECT_ROOT}/omniq-os.iso"
SCREENSHOT_OUT="${PROJECT_ROOT}/build/vm_screen.png"

ACTION="${1:-start}"
ISO_CHOICE="${FULL_ISO}"
if [[ "${2:-}" == "--minimal" ]] || [[ ! -f "${FULL_ISO}" && -f "${MIN_ISO}" ]]; then
    ISO_CHOICE="${MIN_ISO}"
fi

if ! command -v VBoxManage >/dev/null 2>&1; then
    echo "[ERROR] VBoxManage is not installed on this host."
    exit 1
fi

vm_exists() {
    VBoxManage list vms | grep -q "\"${VM_NAME}\""
}

vm_running() {
    VBoxManage list runningvms | grep -q "\"${VM_NAME}\""
}

provision_vm() {
    if [[ ! -f "${ISO_CHOICE}" ]]; then
        echo "[ERROR] Bootable ISO not found at ${ISO_CHOICE}. Build it first!"
        exit 1
    fi

    echo "[OmniQ-VMBox] Provisioning VirtualBox VM '${VM_NAME}' (4 vCPUs, 4096 MB RAM, VMSVGA)..."
    VBoxManage createvm --name "${VM_NAME}" --ostype "Ubuntu_64" --register

    VBoxManage modifyvm "${VM_NAME}" \
        --cpus 4 \
        --memory 4096 \
        --vram 128 \
        --graphicscontroller vmsvga \
        --mouse usbtablet \
        --boot1 dvd --boot2 disk --boot3 none --boot4 none \
        --ioapic on \
        --rtcuseutc on \
        --audio-enabled off \
        --nic1 nat \
        --natpf1 "omniqd,tcp,127.0.0.1,19090,,9090" \
        --natpf1 "secretapi,tcp,127.0.0.1,19443,,9443" \
        --uart1 0x3F8 4 \
        --uartmode1 file "${PROJECT_ROOT}/build/vm_serial.log"

    if [[ ! -f "${VDI_PATH}" ]]; then
        VBoxManage createmedium disk --filename "${VDI_PATH}" --size 20480 --format VDI >/dev/null
    fi

    VBoxManage storagectl "${VM_NAME}" --name "SATA Controller" --add sata --controller IntelAhci
    VBoxManage storageattach "${VM_NAME}" --storagectl "SATA Controller" --port 0 --device 0 --type hdd --medium "${VDI_PATH}"

    VBoxManage storagectl "${VM_NAME}" --name "IDE Controller" --add ide
    VBoxManage storageattach "${VM_NAME}" --storagectl "IDE Controller" --port 0 --device 0 --type dvddrive --medium "${ISO_CHOICE}"
}

case "${ACTION}" in
    status)
        if vm_running; then
            echo "VM '${VM_NAME}' is RUNNING."
        elif vm_exists; then
            echo "VM '${VM_NAME}' is REGISTERED (powered off)."
        else
            echo "VM '${VM_NAME}' is NOT created yet."
        fi
        ;;
    stop)
        if vm_running; then
            VBoxManage controlvm "${VM_NAME}" poweroff
            echo "[OmniQ-VMBox] Powered off '${VM_NAME}'."
        fi
        ;;
    screenshot)
        mkdir -p "$(dirname "${SCREENSHOT_OUT}")"
        VBoxManage controlvm "${VM_NAME}" screenshotpng "${SCREENSHOT_OUT}"
        echo "[OmniQ-VMBox] Captured live VM screenshot -> ${SCREENSHOT_OUT}"
        ;;
    recreate)
        if vm_running; then
            VBoxManage controlvm "${VM_NAME}" poweroff || true
            sleep 2
        fi
        if vm_exists; then
            VBoxManage unregistervm "${VM_NAME}" --delete || true
        fi
        rm -f "${VDI_PATH}"
        provision_vm
        ;;
    headless|start|auto)
        mkdir -p "${PROJECT_ROOT}/build"
        if ! vm_exists; then
            provision_vm
        else
            if ! vm_running; then
                VBoxManage storageattach "${VM_NAME}" --storagectl "IDE Controller" --port 0 --device 0 --type dvddrive --medium emptydrive 2>/dev/null || true
                VBoxManage closemedium dvd "${ISO_CHOICE}" 2>/dev/null || true
                VBoxManage storageattach "${VM_NAME}" --storagectl "IDE Controller" --port 0 --device 0 --type dvddrive --medium "${ISO_CHOICE}" || true
            fi
        fi
        if ! vm_running; then
            VTYPE="gui"
            [[ "${ACTION}" == "headless" || -z "${DISPLAY:-}" ]] && VTYPE="headless"
            VBoxManage startvm "${VM_NAME}" --type "${VTYPE}"
            echo "[OmniQ-VMBox] Booted '${VM_NAME}' (${VTYPE}) from $(basename "${ISO_CHOICE}")."
        else
            echo "[OmniQ-VMBox] '${VM_NAME}' is already running."
        fi
        ;;
    *)
        echo "Usage: $0 [start|headless|stop|status|screenshot|recreate] [--minimal]"
        exit 1
        ;;
esac
