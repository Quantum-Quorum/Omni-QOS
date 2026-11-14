# SPDX-License-Identifier: Apache-2.0
"""
OmniQ-OS: Unified CLI (qpu)
"""

import sys
import argparse
from .core import QPU
from .studio import run_circuit_on_qpu, interactive_shell

def main():
    parser = argparse.ArgumentParser(prog="qpu", description="OmniQ-OS Quantum Processing Unit Control CLI")
    subparsers = parser.add_subparsers(dest="subcommand", help="Subcommand to execute")

    # qpu info
    subparsers.add_parser("info", help="Display QPU topology and cryogenic status")

    # qpu run <file>
    run_parser = subparsers.add_parser("run", help="Execute an OpenQASM circuit file on QPU")
    run_parser.add_argument("file", help="Path to .qasm file")
    run_parser.add_argument("-s", "--shots", type=int, default=1000, help="Shot count")
    run_parser.add_argument("-d", "--debug", action="store_true", help="Launch 3D GUI debugger")

    # qpu reset <qubit_id>
    reset_parser = subparsers.add_parser("reset", help="Reset physical qubit to |0>")
    reset_parser.add_argument("qubit", type=int, help="Physical qubit ID")

    # qpu studio
    subparsers.add_parser("studio", help="Launch interactive Quantum Assembly Studio")

    args = parser.parse_args()

    if args.subcommand == "info" or not args.subcommand:
        with QPU() as qpu:
            top = qpu.get_topology()
            print(f"Device: {top.device_name} (Driver: {top.driver_version})")
            print(f"State: {top.operating_state.name} | Tech: {top.technology.name}")
            print(f"Physical Qubits: {top.total_qubits} | Couplers: {top.total_couplers}")
            print(f"Cryo MXC Temp: {top.mixing_chamber_temp_mK:.3f} mK (Threshold: 50.000 mK)")
            print(f"Avg T1: {top.avg_t1_us:.1f} us | Avg T2: {top.avg_t2_us:.1f} us")
    elif args.subcommand == "run":
        with open(args.file, "r") as f:
            code = f.read()
        run_circuit_on_qpu(code, shots=args.shots, launch_gui=args.debug)
    elif args.subcommand == "reset":
        with QPU() as qpu:
            qpu.reset_qubit(args.qubit)
            print(f"Qubit Q{args.qubit} reset to ground state |0>.")
    elif args.subcommand == "studio":
        interactive_shell()

if __name__ == "__main__":
    main()
