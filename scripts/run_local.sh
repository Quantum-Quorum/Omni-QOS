#!/usr/bin/env bash

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "${SCRIPT_DIR}/.." && pwd)"

export PYTHONPATH="${PROJECT_ROOT}/runtime/python:${PROJECT_ROOT}/src:${PYTHONPATH:-}"
export LD_LIBRARY_PATH="${PROJECT_ROOT}/lib:${LD_LIBRARY_PATH:-}"

if [[ "${1:-}" == "--test" ]]; then
    "${PROJECT_ROOT}/bin/omniqd" --test
    "${PROJECT_ROOT}/bin/omniq-gui" --test
    "${PROJECT_ROOT}/bin/omniq-hilbert-space" --test
    "${PROJECT_ROOT}/bin/omniq-circuit-maker" --test
    echo "[PASS] run_local.sh verified."
    exit 0
fi

cleanup() {
    [[ -n "${OMNIQD_PID:-}" ]] && kill "${OMNIQD_PID}" 2>/dev/null || true
    [[ -n "${SECRET_PID:-}" ]] && kill "${SECRET_PID}" 2>/dev/null || true
    [[ -n "${TWIN_PID:-}" ]] && kill "${TWIN_PID}" 2>/dev/null || true
}
trap cleanup EXIT INT TERM

pkill -f "bin/omniqd" 2>/dev/null || true
pkill -f "bin/omniq-secret-api" 2>/dev/null || true
pkill -f "bin/omniq-lab-twin" 2>/dev/null || true

if [[ -x "${PROJECT_ROOT}/bin/omniq-lab-twin" ]]; then
    echo "[OmniQ-OS] Starting Cryo-Lab Hardware-in-the-Loop Rack Twin (SCPI/Modbus/Bluefors/QICK)..."
    python3 "${PROJECT_ROOT}/bin/omniq-lab-twin" >/tmp/omniq_lab_twin.log 2>&1 &
    TWIN_PID=$!
fi

echo "[OmniQ-OS] Starting Real-Time Quantum Daemon (omniqd :9090)..."
python3 "${PROJECT_ROOT}/bin/omniqd" 9090 >/tmp/omniqd_local.log 2>&1 &
OMNIQD_PID=$!

echo "[OmniQ-OS] Starting 7-Modality Quantum Secret API (omniq-secret-api :9443)..."
"${PROJECT_ROOT}/bin/omniq-secret-api" --daemon --port 9443 >/tmp/omniq_secret_local.log 2>&1 &
SECRET_PID=$!

sleep 0.5

if [[ -n "${DISPLAY:-}" ]]; then
    APP="${1:-cockpit}"
    case "$APP" in
        hilbert)
            echo "[OmniQ-OS] Launching Hilbert Space Explorer..."
            python3 "${PROJECT_ROOT}/bin/omniq-hilbert-space"
            ;;
        circuit|maker)
            echo "[OmniQ-OS] Launching Quantum Circuit Maker..."
            python3 "${PROJECT_ROOT}/bin/omniq-circuit-maker"
            ;;
        cryo|lab)
            echo "[OmniQ-OS] Launching Production Cryogenic Dilution Lab Console..."
            python3 "${PROJECT_ROOT}/bin/omniq-cryo-lab"
            ;;
        all)
            python3 "${PROJECT_ROOT}/bin/omniq-cryo-lab" &
            python3 "${PROJECT_ROOT}/bin/omniq-hilbert-space" &
            python3 "${PROJECT_ROOT}/bin/omniq-circuit-maker" &
            python3 "${PROJECT_ROOT}/bin/omniq-gui"
            ;;
        *)
            echo "[OmniQ-OS] Launching 5-Tier Quantum Mission-Control Cockpit..."
            python3 "${PROJECT_ROOT}/bin/omniq-gui"
            ;;
    esac
else
    echo "[OmniQ-OS] Headless terminal detected. Running qpu-smi telemetry snapshot:"
    "${PROJECT_ROOT}/bin/qpu-smi"
fi
