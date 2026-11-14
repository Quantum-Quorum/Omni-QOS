# SPDX-License-Identifier: Apache-2.0
"""
OmniQ-OS: Quantum Assembly Studio & Interactive Environment
Supports raw OpenQASM 3, pulse bytecode, multi-framework bridges, and GUI debugger launch.
"""

import os
import sys
import argparse
from typing import Dict
from .core import QPU
from .debugger_bridge import OmniQDebuggerBridge

# ANSI Visual Styling matching Option A Brand
CLR_CYAN = "\033[38;2;0;229;255m"
CLR_GREEN = "\033[38;2;52;211;153m"
CLR_YELLOW = "\033[38;2;251;191;36m"
CLR_PURPLE = "\033[38;2;167;139;250m"
CLR_GRAY = "\033[38;2;100;116;139m"
CLR_BOLD = "\033[1m"
CLR_RESET = "\033[0m"

BANNER = f"""{CLR_CYAN}
  ██████╗ ███╗   ███╗███╗   ██╗██╗ ██████╗       ██████╗ ███████╗
 ██╔═══██╗████╗ ████║████╗  ██║██║██╔═══██╗     ██╔═══██╗██╔════╝
 ██║   ██║██╔████╔██║██╔██╗ ██║██║██║   ██║     ██║   ██║███████╗
 ██║   ██║██║╚██╔╝██║██║╚██╗██║██║██║▄▄ ██║     ██║   ██║╚════██║
 ╚██████╔╝██║ ╚═╝ ██║██║ ╚████║██║╚██████╔╝     ╚██████╔╝███████║
  ╚═════╝ ╚═╝     ╚═╝╚═╝  ╚═══╝╚═╝ ╚══▀▀═╝       ╚═════╝ ╚══════╝
{CLR_PURPLE}  [ Quantum Assembly Studio & Multi-Framework IDE Bridge ]{CLR_RESET}
"""

BELL_CIRCUIT = """OPENQASM 3.0;
qubit[2] q;
bit[2] c;

h q[0];
cx q[0], q[1];
c[0] = measure q[0];
c[1] = measure q[1];
"""

GHZ_CIRCUIT = """OPENQASM 3.0;
qubit[3] q;
bit[3] c;

h q[0];
cx q[0], q[1];
cx q[1], q[2];
c[0] = measure q[0];
c[1] = measure q[1];
c[2] = measure q[2];
"""

def print_histogram(counts: Dict[str, int], total_shots: int):
    print(f"\n{CLR_BOLD}Measurement Histogram ({total_shots} shots):{CLR_RESET}")
    max_count = max(counts.values()) if counts else 1
    for bitstr, count in sorted(counts.items()):
        pct = (count / total_shots) * 100.0
        bar_len = int((count / max_count) * 40)
        bar = "█" * bar_len
        print(f"  |{bitstr}⟩ : {bar:<40} {count:>5} ({pct:5.1f}%)")
    print("")

def run_circuit_on_qpu(qasm: str, shots: int = 1000, launch_gui: bool = False):
    print(f"{CLR_CYAN}[OmniQ-Studio]{CLR_RESET} Allocating spatial partition on QPU...")
    with QPU() as qpu:
        # Detect qubit count
        num_q = 2
        for line in qasm.splitlines():
            if "qubit[" in line:
                try:
                    num_q = int(line.split("qubit[")[1].split("]")[0])
                except Exception:
                    pass

        part = qpu.allocate_partition(num_qubits=num_q, guard_hops=1)
        print(f"{CLR_GREEN}✓ Partition allocated:{CLR_RESET} Physical Qubits {part.qubits}, Guard Bands {part.guard_qubits}")
        print(f"{CLR_CYAN}[OmniQ-Studio]{CLR_RESET} Executing circuit ({shots} shots)...")
        job = part.submit(qasm, shots=shots)
        print(f"{CLR_GREEN}✓ Execution completed in {job.execution_time_ns} ns.{CLR_RESET}")
        print_histogram(job.counts, shots)
        part.free()

        if launch_gui:
            OmniQDebuggerBridge.launch(qasm, job.counts)

def interactive_shell():
    print(BANNER)
    print(f"Type {CLR_BOLD}'help'{CLR_RESET} for commands or {CLR_BOLD}'bell'{CLR_RESET} / {CLR_BOLD}'ghz'{CLR_RESET} to run demo circuits.\n")

    current_qasm = BELL_CIRCUIT

    while True:
        try:
            line = input(f"{CLR_CYAN}omniq-studio>{CLR_RESET} ").strip()
        except (EOFError, KeyboardInterrupt):
            print("\nExiting OmniQ Studio.")
            break

        if not line:
            continue

        parts = line.split()
        cmd = parts[0].lower()

        if cmd in ("exit", "quit", "q"):
            break
        elif cmd == "help":
            print(f"""
{CLR_BOLD}OmniQ Studio Commands:{CLR_RESET}
  {CLR_CYAN}bell{CLR_RESET}              Load and execute standard Bell state (|00> + |11>)
  {CLR_CYAN}ghz{CLR_RESET}               Load and execute 3-qubit GHZ state (|000> + |111>)
  {CLR_CYAN}show{CLR_RESET}              Display current OpenQASM 3.0 assembly
  {CLR_CYAN}run [shots]{CLR_RESET}       Execute current circuit on /dev/accel/qpu0
  {CLR_CYAN}debug{CLR_RESET}             Run circuit and launch 3D GUI Debugger (Bloch sphere)
  {CLR_CYAN}raw{CLR_RESET}               Inspect calibrated microwave pulse waveform schedule
  {CLR_CYAN}qiskit{CLR_RESET}            Run demo via Qiskit OmniQBackend adapter
  {CLR_CYAN}cirq{CLR_RESET}              Run demo via Google Cirq OmniQSampler adapter
  {CLR_CYAN}pennylane{CLR_RESET}         Run demo via PennyLane OmniQPennyLaneDevice
  {CLR_CYAN}braket{CLR_RESET}            Run demo via Amazon Braket OmniQBraketDevice
  {CLR_CYAN}smi{CLR_RESET}               Show live QPU hardware and cryogenic telemetry
  {CLR_CYAN}clear{CLR_RESET}             Clear terminal screen
  {CLR_CYAN}exit{CLR_RESET}              Exit studio
""")
        elif cmd == "show":
            print(f"\n{CLR_PURPLE}--- Current OpenQASM 3.0 Program ---{CLR_RESET}\n{current_qasm}")
        elif cmd == "bell":
            current_qasm = BELL_CIRCUIT
            run_circuit_on_qpu(current_qasm, shots=1000)
        elif cmd == "ghz":
            current_qasm = GHZ_CIRCUIT
            run_circuit_on_qpu(current_qasm, shots=1000)
        elif cmd == "run":
            shots = int(parts[1]) if len(parts) > 1 else 1000
            run_circuit_on_qpu(current_qasm, shots=shots)
        elif cmd == "debug":
            run_circuit_on_qpu(current_qasm, shots=1000, launch_gui=True)
        elif cmd == "raw":
            print(f"""
{CLR_BOLD}=== Raw Calibrated Microwave Pulse Schedule ==={CLR_RESET}
Drive Frequency:    5.0000 GHz (Local Oscillator IF: 125 MHz)
Readout Frequency:  7.2450 GHz (Dispersive Resonator)
Qubit 0:
  [0.0 ns  - 20.0 ns ]  DRAG Gaussian Pulse (X-pi/2, amp=0.450, sigma=5.0ns)
  [20.0 ns - 80.0 ns ]  Cross-Resonance Drive (CR_01, amp=0.820, cancellation=0.12)
Qubit 1:
  [20.0 ns - 80.0 ns ]  Rotary Echo Pulse (Y, amp=0.080)
Readout:
  [80.0 ns - 180.0ns ]  Heterodyne Demodulation Pulse (100 ns window)
""")
        elif cmd == "qiskit":
            from .qiskit import OmniQBackend
            print(f"{CLR_CYAN}[Qiskit Adapter]{CLR_RESET} Running via Qiskit Backend...")
            backend = OmniQBackend()
            job = backend.run(current_qasm, shots=1000)
            print(f"{CLR_GREEN}✓ Qiskit Job Completed!{CLR_RESET}")
            print_histogram(job.result().get_counts(), 1000)
        elif cmd == "cirq":
            from .cirq import OmniQSampler
            print(f"{CLR_CYAN}[Cirq Adapter]{CLR_RESET} Running via Cirq Sampler...")
            sampler = OmniQSampler()
            res = sampler.run(None, repetitions=1000)
            print(f"{CLR_GREEN}✓ Cirq Sampler Completed!{CLR_RESET}")
            print_histogram(res.histogram(), 1000)
        elif cmd == "pennylane":
            from .pennylane import OmniQPennyLaneDevice
            print(f"{CLR_CYAN}[PennyLane Adapter]{CLR_RESET} Running via PennyLane Device...")
            dev = OmniQPennyLaneDevice(wires=2, shots=1000)
            counts = dev.execute(None)
            dev.close()
            print(f"{CLR_GREEN}✓ PennyLane Tape Executed!{CLR_RESET}")
            print_histogram(counts, 1000)
        elif cmd == "braket":
            from .braket import OmniQBraketDevice
            print(f"{CLR_CYAN}[Amazon Braket Adapter]{CLR_RESET} Running via Amazon Braket Device...")
            bdev = OmniQBraketDevice()
            task = bdev.run(current_qasm, shots=1000)
            print(f"{CLR_GREEN}✓ Braket Task Completed!{CLR_RESET}")
            print_histogram(task.result().measurement_counts, 1000)
        elif cmd == "smi":
            with QPU() as qpu:
                top = qpu.get_topology()
                print(f"QPU: {top.device_name} | State: {top.operating_state.name} | Temp: {top.mixing_chamber_temp_mK:.3f} mK | Qubits: {top.total_qubits}")
        elif cmd == "clear":
            print("\033[H\033[J", end="")
        else:
            print(f"Unknown command: {cmd}. Type 'help' for options.")

def main():
    parser = argparse.ArgumentParser(description="OmniQ-OS Quantum Assembly Studio")
    parser.add_argument("file", nargs="?", help="Path to OpenQASM 3.0 circuit file to run")
    parser.add_argument("-s", "--shots", type=int, default=1000, help="Number of measurement repetitions")
    parser.add_argument("-d", "--debug", action="store_true", help="Launch interactive 3D GUI Debugger")
    parser.add_argument("--mode", choices=["asm", "raw", "qiskit", "cirq", "pennylane", "braket"],
                        default="asm", help="Execution mode or framework")
    args = parser.parse_args()

    if args.file:
        if not os.path.exists(args.file):
            print(f"Error: File not found: {args.file}")
            sys.exit(1)
        with open(args.file, "r") as f:
            code = f.read()
        run_circuit_on_qpu(code, shots=args.shots, launch_gui=args.debug)
    else:
        interactive_shell()

if __name__ == "__main__":
    main()
