# SPDX-License-Identifier: Apache-2.0
"""
Fermilab QICK `tProc` v2 FPGA Assembly & RF-DAC Waveform Compiler + RFSoC Client.

Targets Xilinx Zynq UltraScale+ RFSoC ZCU216 (`xczu49dr-ffvf1760-2-e`):
  - `tProc` v2 fabric clock: `f_fabric = 430.08 MHz` (`2.325149 ns` / tick)
  - RF-DAC sampling clock:   `f_dac    = 6881.28 MSPS` (16x fabric interpolation)
  - RF-ADC sampling clock:   `f_adc    = 4300.80 MSPS` (10x fabric decimation)
  - Fast QEC syndrome loop:  `< 500 ns` (`waiti` + `read` + `bitwi` + `condj` = 180 ticks = 418.5 ns)
"""

from __future__ import annotations

import json
import math
import os
import socket
import threading
from typing import Any, Dict, List, Optional, Tuple, Union

from ..circuit import Circuit


class QickExecutionError(RuntimeError):
    """Raised when QICK RFSoC execution is rejected (e.g. `ERR_RF_BIAS_CLAMPED`)."""

    def __init__(self, code: str, message: str) -> None:
        super().__init__(f"{code}: {message}")
        self.code = code


class QickProgramCompiler:
    """
    Compiles `omniq.circuit.Circuit` or OpenQASM 2.0/3.0 into authentic Fermilab
    QICK `tProc` v2 assembly and 16-bit signed I/Q DRAG Gaussian & Cross-Resonance
    RF-DAC envelope descriptors.
    """

    F_FABRIC_MHZ: float = 430.08
    T_TICK_NS: float = 1000.0 / 430.08  # 2.3251488095238093 ns
    DAC_SAMPLES_PER_ENVELOPE: int = 64

    # Gate durations in tProc fabric ticks (2.325 ns/tick)
    TICKS_1Q_DRAG: int = 16          # 37.20 ns single-qubit DRAG Gaussian pulse
    TICKS_2Q_CR_ECHO: int = 96       # 223.21 ns echoed Cross-Resonance ZX90 pulse
    TICKS_READOUT_PULSE: int = 128   # 297.62 ns dispersive readout resonator pulse
    TICKS_SYNDROME_WAIT: int = 180   # 418.53 ns (< 500 ns!) fast ADC demod + condj feedback

    def __init__(
        self,
        base_qubit_freq_mhz: float = 4850.0,
        qubit_detuning_step_mhz: float = 62.5,
        base_readout_freq_mhz: float = 6820.0,
        readout_step_mhz: float = 45.0,
        anharmonicity_mhz: float = -215.0,
        drag_alpha: float = 0.5,
    ) -> None:
        self.base_qubit_freq_mhz = float(base_qubit_freq_mhz)
        self.qubit_detuning_step_mhz = float(qubit_detuning_step_mhz)
        self.base_readout_freq_mhz = float(base_readout_freq_mhz)
        self.readout_step_mhz = float(readout_step_mhz)
        self.anharmonicity_mhz = float(anharmonicity_mhz)
        self.drag_alpha = float(drag_alpha)

    def qubit_freq_mhz(self, qubit: int) -> float:
        return round(self.base_qubit_freq_mhz + self.qubit_detuning_step_mhz * int(qubit), 3)

    def readout_freq_mhz(self, qubit: int) -> float:
        return round(self.base_readout_freq_mhz + self.readout_step_mhz * int(qubit), 3)

    @staticmethod
    def _freq_to_nco_word(freq_mhz: float) -> int:
        """Convert RF/IF frequency in MHz to 32-bit unsigned QICK NCO phase-increment word."""
        # Normalized against 6881.28 MHz Nyquist synthesis zone
        frac = (freq_mhz % 6881.28) / 6881.28
        return int(round(frac * ((1 << 32) - 1))) & 0xFFFFFFFF

    @staticmethod
    def _rad_to_phase_word(angle_rad: float) -> int:
        """Convert radians to 16-bit QICK NCO digital phase offset word."""
        frac = (angle_rad % (2.0 * math.pi)) / (2.0 * math.pi)
        return int(round(frac * 65535.0)) & 0xFFFF

    def _synthesize_drag_gaussian(
        self,
        ch: int,
        freq_mhz: float,
        gain_dac: int,
        length_cycles: int,
        phase_offset_rad: float = 0.0,
    ) -> Dict[str, Any]:
        """Generate 16-bit signed I/Q DRAG Gaussian envelope (`I_dac16`, `Q_dac16`)."""
        n = self.DAC_SAMPLES_PER_ENVELOPE
        mu = 0.5 * (n - 1)
        sigma = n / 4.5
        pedestal = math.exp(-0.5 * ((0.0 - mu) / sigma) ** 2)
        norm = max(1e-9, 1.0 - pedestal)
        # DRAG quadrature scale: -alpha / (2 * pi * anharmonicity * sigma_t)
        sigma_us = (length_cycles * self.T_TICK_NS * 1e-3) / 4.5
        drag_beta = -self.drag_alpha / (2.0 * math.pi * self.anharmonicity_mhz * max(1e-5, sigma_us))
        drag_beta = max(-0.45, min(0.45, drag_beta))

        cos_p = math.cos(phase_offset_rad)
        sin_p = math.sin(phase_offset_rad)

        i_samples: List[int] = []
        q_samples: List[int] = []
        for k in range(n):
            x = (k - mu) / sigma
            gauss = (math.exp(-0.5 * x * x) - pedestal) / norm
            dgauss = -x * math.exp(-0.5 * x * x)
            env_i = gauss
            env_q = drag_beta * dgauss
            # Rotate envelope by azimuthal axis (e.g. X vs Y rotation)
            rot_i = env_i * cos_p - env_q * sin_p
            rot_q = env_i * sin_p + env_q * cos_p
            i_samples.append(max(-32767, min(32767, int(round(32767.0 * rot_i)))))
            q_samples.append(max(-32767, min(32767, int(round(32767.0 * rot_q)))))

        return {
            "envelope_type": "DRAG_GAUSSIAN",
            "ch": int(ch),
            "freq_mhz": float(freq_mhz),
            "gain_dac": int(max(-32767, min(32767, gain_dac))),
            "length_cycles": int(length_cycles),
            "I_dac16": i_samples,
            "Q_dac16": q_samples,
        }

    def _synthesize_cross_resonance(
        self,
        ch: int,
        target_freq_mhz: float,
        gain_dac: int,
        length_cycles: int,
    ) -> Dict[str, Any]:
        """Generate 16-bit signed flat-top Gaussian-edge Cross-Resonance (CR) envelope."""
        n = self.DAC_SAMPLES_PER_ENVELOPE
        ramp = max(4, n // 6)
        sigma = ramp / 3.0
        i_samples: List[int] = []
        q_samples: List[int] = []
        for k in range(n):
            if k < ramp:
                x = (k - ramp) / sigma
                amp = math.exp(-0.5 * x * x)
                deriv = -x * amp * 0.12
            elif k >= n - ramp:
                x = (k - (n - 1 - ramp)) / sigma
                amp = math.exp(-0.5 * x * x)
                deriv = -x * amp * 0.12
            else:
                amp = 1.0
                deriv = 0.0
            # Active Stark/IX cancellation quadrature offset (-0.08 * amp + edge derivative)
            q_val = -0.08 * amp + deriv
            i_samples.append(max(-32767, min(32767, int(round(32767.0 * amp)))))
            q_samples.append(max(-32767, min(32767, int(round(32767.0 * q_val)))))

        return {
            "envelope_type": "CROSS_RESONANCE",
            "ch": int(ch),
            "freq_mhz": float(target_freq_mhz),
            "gain_dac": int(max(-32767, min(32767, gain_dac))),
            "length_cycles": int(length_cycles),
            "I_dac16": i_samples,
            "Q_dac16": q_samples,
        }

    def _synthesize_readout_flattop(
        self,
        ch: int,
        ro_freq_mhz: float,
        gain_dac: int,
        length_cycles: int,
    ) -> Dict[str, Any]:
        """Generate 16-bit signed dispersive readout resonator flat-top envelope."""
        n = self.DAC_SAMPLES_PER_ENVELOPE
        ramp = 6
        i_samples: List[int] = []
        q_samples: List[int] = [0] * n
        for k in range(n):
            if k < ramp:
                amp = 0.5 * (1.0 - math.cos(math.pi * (k + 1) / ramp))
            elif k >= n - ramp:
                amp = 0.5 * (1.0 - math.cos(math.pi * (n - k) / ramp))
            else:
                amp = 1.0
            i_samples.append(max(-32767, min(32767, int(round(32767.0 * amp)))))

        return {
            "envelope_type": "READOUT_FLATTOP",
            "ch": int(ch),
            "freq_mhz": float(ro_freq_mhz),
            "gain_dac": int(max(-32767, min(32767, gain_dac))),
            "length_cycles": int(length_cycles),
            "I_dac16": i_samples,
            "Q_dac16": q_samples,
        }

    def compile(self, circ_or_qasm: Union[Circuit, str]) -> Dict[str, Any]:
        """
        Compile an `omniq.circuit.Circuit` or OpenQASM 2.0/3.0 string into QICK `tProc` v2
        assembly, 16-bit signed DAC I/Q waveforms, and ZCU216 cycle/nanosecond timing.
        """
        if isinstance(circ_or_qasm, str):
            circ = Circuit.from_qasm(circ_or_qasm)
            qasm_src = circ_or_qasm
        elif isinstance(circ_or_qasm, Circuit):
            circ = circ_or_qasm
            qasm_src = circ.to_qasm("2.0")
        else:
            raise TypeError(f"Expected Circuit or QASM string, got {type(circ_or_qasm)!r}")

        nq = circ.num_qubits
        frame_z: Dict[int, float] = {q: 0.0 for q in range(nq)}
        waveforms: List[Dict[str, Any]] = []
        wf_cache: Dict[Tuple[Any, ...], int] = {}

        def register_waveform(wf: Dict[str, Any]) -> int:
            key = (
                wf["envelope_type"],
                wf["ch"],
                wf["freq_mhz"],
                wf["gain_dac"],
                wf["length_cycles"],
                wf["I_dac16"][0],
                wf["Q_dac16"][len(wf["Q_dac16"]) // 4],
            )
            if key not in wf_cache:
                addr_idx = len(waveforms)
                wf["wf_id"] = addr_idx
                waveforms.append(wf)
                wf_cache[key] = addr_idx
            return wf_cache[key]

        asm: List[str] = [
            "// QICK tProc v2 Program — Xilinx Zynq UltraScale+ RFSoC ZCU216",
            f"// Fabric Clock: f_fabric = {self.F_FABRIC_MHZ:.2f} MHz ({self.T_TICK_NS:.3f} ns/tick)",
            f"// Target Qubits: {nq} | Fast Syndrome Feedback Budget: < 500.0 ns",
            "regwi 0, $r0, 0                  // zero reference register",
            "regwi 0, $r1, 1                  // ADC trigger & bitmask constant",
            "synci 10                         // align master tProc v2 timeline (23.25 ns)",
        ]
        total_cycles = 10
        measured_qubits: List[int] = []
        feedback_idx = 0

        def emit_1q_drag(qubit: int, theta: float, axis_phi: float, comment: str) -> None:
            nonlocal total_cycles
            ch = qubit % 8
            f_mhz = self.qubit_freq_mhz(qubit)
            nco_freq = self._freq_to_nco_word(f_mhz)
            net_phase = axis_phi - frame_z[qubit]
            phase_word = self._rad_to_phase_word(net_phase)
            gain_scale = max(-1.0, min(1.0, theta / math.pi))
            gain_dac = int(round(30000.0 * gain_scale))
            if gain_dac == 0 and abs(theta) > 1e-6:
                gain_dac = 1 if theta > 0 else -1
            wf = self._synthesize_drag_gaussian(
                ch=ch,
                freq_mhz=f_mhz,
                gain_dac=gain_dac,
                length_cycles=self.TICKS_1Q_DRAG,
                phase_offset_rad=axis_phi,
            )
            wf_id = register_waveform(wf)
            mode_word = (wf_id << 16) | self.TICKS_1Q_DRAG
            asm.append(f"// {comment} on q[{qubit}] (ch={ch}, f={f_mhz:.2f} MHz)")
            asm.append(f"regwi 0, $r2, {nco_freq}         // NCO freq word")
            asm.append(f"regwi 0, $r3, {phase_word}       // Virtual-Z + axis phase word")
            asm.append(f"regwi 0, $r4, {gain_dac}         // 16-bit signed DAC gain")
            asm.append(f"regwi 0, $r5, {mode_word}        // BRAM env #{wf_id}, len={self.TICKS_1Q_DRAG} ticks")
            asm.append(f"pulse {ch}, $r2, $r3, $r4, $r5, @0")
            asm.append(f"synci {self.TICKS_1Q_DRAG}")
            total_cycles += self.TICKS_1Q_DRAG

        def emit_2q_cr(ctrl: int, targ: int, label: str) -> None:
            nonlocal total_cycles
            ch_ctrl = ctrl % 8
            f_targ = self.qubit_freq_mhz(targ)
            nco_freq = self._freq_to_nco_word(f_targ)
            phase_word = self._rad_to_phase_word(-frame_z[targ])
            gain_dac = 26500
            wf = self._synthesize_cross_resonance(
                ch=ch_ctrl,
                target_freq_mhz=f_targ,
                gain_dac=gain_dac,
                length_cycles=self.TICKS_2Q_CR_ECHO,
            )
            wf_id = register_waveform(wf)
            mode_word = (wf_id << 16) | self.TICKS_2Q_CR_ECHO
            asm.append(
                f"// {label} Cross-Resonance Echo q[{ctrl}] -> q[{targ}] "
                f"(ch={ch_ctrl} @ f_targ={f_targ:.2f} MHz)"
            )
            asm.append(f"regwi 0, $r6, {nco_freq}         // CR drive at target transmon freq")
            asm.append(f"regwi 0, $r7, {phase_word}       // CR phase alignment")
            asm.append(f"regwi 0, $r8, {gain_dac}         // CR DAC amplitude")
            asm.append(f"regwi 0, $r9, {mode_word}        // BRAM CR env #{wf_id}, len={self.TICKS_2Q_CR_ECHO} ticks")
            asm.append(f"pulse {ch_ctrl}, $r6, $r7, $r8, $r9, @0")
            asm.append(f"synci {self.TICKS_2Q_CR_ECHO}")
            total_cycles += self.TICKS_2Q_CR_ECHO

        def emit_readout_and_syndrome_feedback(qubit: int, active_reset: bool = False) -> None:
            nonlocal total_cycles, feedback_idx
            feedback_idx += 1
            ro_ch = 8 + (qubit % 4)
            adc_ch = qubit % 4
            f_ro = self.readout_freq_mhz(qubit)
            nco_ro = self._freq_to_nco_word(f_ro)
            gain_ro = 18000
            wf_ro = self._synthesize_readout_flattop(
                ch=ro_ch,
                ro_freq_mhz=f_ro,
                gain_dac=gain_ro,
                length_cycles=self.TICKS_READOUT_PULSE,
            )
            wf_id = register_waveform(wf_ro)
            mode_word = (wf_id << 16) | self.TICKS_READOUT_PULSE
            lbl = f"SYND_CLEAN_{feedback_idx}_Q{qubit}"

            asm.append(
                f"// Dispersive Readout & Fast <500ns Syndrome Feedback on q[{qubit}] "
                f"(ro_ch={ro_ch}, adc={adc_ch}, f_ro={f_ro:.2f} MHz)"
            )
            asm.append("seti 0, 0, $r1, @0               // Trigger RF-ADC demodulator window")
            asm.append(f"regwi 0, $r10, {nco_ro}          // Readout resonator NCO freq")
            asm.append(f"regwi 0, $r11, {gain_ro}         // Readout resonator DAC gain")
            asm.append(f"regwi 0, $r12, {mode_word}       // BRAM RO env #{wf_id}, len={self.TICKS_READOUT_PULSE}")
            asm.append(f"pulse {ro_ch}, $r10, $r0, $r11, $r12, @0")
            asm.append("seti 0, 0, $r0, @12              // Clear ADC trigger strobe")
            asm.append(f"waiti 0, @{self.TICKS_SYNDROME_WAIT} // Wait {self.TICKS_SYNDROME_WAIT * self.T_TICK_NS:.1f} ns (<500 ns)")
            asm.append(f"read 0, {adc_ch}, upper, $r13    // Latch demodulated IQ threshold bit")
            asm.append("bitwi 0, $r14, $r13, & 0x0001    // Extract binary syndrome bit")
            asm.append(f"condj 0, $r14, ==, $r0, @{lbl}   // Fast branch if |0> / no syndrome flip")
            if active_reset:
                # Active X pi-pulse reset when qubit measured in |1>
                emit_1q_drag(qubit, math.pi, 0.0, "ACTIVE_RESET_X_RECOVERY")
            else:
                asm.append("synci 4                          // Conditional Pauli-frame tag update")
                total_cycles += 4
            asm.append(f"{lbl}:")
            asm.append(f"synci {self.TICKS_SYNDROME_WAIT}")
            total_cycles += self.TICKS_SYNDROME_WAIT

        for gate in circ.gates:
            gt = gate[0].upper()
            if gt == "BARRIER":
                asm.append("synci 2                          // Hardware barrier fence")
                total_cycles += 2
            elif gt == "Z":
                frame_z[gate[1]] += math.pi
                asm.append(f"// Virtual-Z(pi) on q[{gate[1]}] (0 ns NCO frame update)")
            elif gt == "S":
                frame_z[gate[1]] += 0.5 * math.pi
                asm.append(f"// Virtual-Z(pi/2) [S] on q[{gate[1]}]")
            elif gt == "SDG":
                frame_z[gate[1]] -= 0.5 * math.pi
                asm.append(f"// Virtual-Z(-pi/2) [SDG] on q[{gate[1]}]")
            elif gt == "T":
                frame_z[gate[1]] += 0.25 * math.pi
                asm.append(f"// Virtual-Z(pi/4) [T] on q[{gate[1]}]")
            elif gt == "TDG":
                frame_z[gate[1]] -= 0.25 * math.pi
                asm.append(f"// Virtual-Z(-pi/4) [TDG] on q[{gate[1]}]")
            elif gt in ("RZ", "P", "PHASE"):
                ang = float(gate[2])
                frame_z[gate[1]] += ang
                asm.append(f"// Virtual-Z({ang:.5f} rad) [{gt}] on q[{gate[1]}]")
            elif gt == "H":
                q = gate[1]
                frame_z[q] += 0.5 * math.pi
                emit_1q_drag(q, 0.5 * math.pi, 0.0, "H = RZ(pi/2)·SX·RZ(pi/2)")
                frame_z[q] += 0.5 * math.pi
            elif gt == "X":
                emit_1q_drag(gate[1], math.pi, 0.0, "X_180 DRAG")
            elif gt == "Y":
                emit_1q_drag(gate[1], math.pi, 0.5 * math.pi, "Y_180 DRAG")
            elif gt == "SX":
                emit_1q_drag(gate[1], 0.5 * math.pi, 0.0, "SX_90 DRAG")
            elif gt == "RX":
                emit_1q_drag(gate[1], float(gate[2]), 0.0, f"RX({gate[2]:.4f}) DRAG")
            elif gt == "RY":
                emit_1q_drag(gate[1], float(gate[2]), 0.5 * math.pi, f"RY({gate[2]:.4f}) DRAG")
            elif gt in ("CNOT", "CX"):
                c, t = gate[1], gate[2]
                emit_2q_cr(c, t, "CNOT")
                emit_1q_drag(t, 0.5 * math.pi, 0.0, "CNOT_LOCAL_ZX_CLEANUP")
            elif gt in ("CZ", "CY", "SWAP", "ISWAP"):
                c, t = gate[1], gate[2]
                emit_2q_cr(c, t, gt)
                emit_1q_drag(t, 0.5 * math.pi, 0.0, f"{gt}_LOCAL_FRAME")
            elif gt in ("CP", "CRZ"):
                c, t, ang = gate[1], gate[2], float(gate[3])
                emit_2q_cr(c, t, f"{gt}({ang:.4f})")
            elif gt in ("CCX", "CSWAP"):
                c1, c2, t = gate[1], gate[2], gate[3]
                emit_2q_cr(c1, t, f"{gt}_STAGE1")
                emit_2q_cr(c2, t, f"{gt}_STAGE2")
            elif gt == "RESET":
                emit_readout_and_syndrome_feedback(gate[1], active_reset=True)
            elif gt == "MEASURE":
                measured_qubits.append(gate[1])
                emit_readout_and_syndrome_feedback(gate[1], active_reset=False)

        # Ensure at least one readout + fast feedback sequence if circuit omitted explicit measure
        if not measured_qubits:
            for q in range(nq):
                measured_qubits.append(q)
                emit_readout_and_syndrome_feedback(q, active_reset=False)

        asm.append("synci 8                          // Finalize tProc v2 shot buffer")
        total_cycles += 8

        assembly_text = "\n".join(asm) + "\n"
        instr_lines = [
            ln for ln in asm if ln.strip() and not ln.strip().startswith("//") and not ln.strip().endswith(":")
        ]
        duration_ns = round(total_cycles * self.T_TICK_NS, 3)
        syndrome_latency_ns = round(self.TICKS_SYNDROME_WAIT * self.T_TICK_NS, 3)

        return {
            "assembly": assembly_text,
            "instruction_count": len(instr_lines),
            "waveforms": waveforms,
            "f_fabric_mhz": self.F_FABRIC_MHZ,
            "t_tick_ns": round(self.T_TICK_NS, 6),
            "total_cycles": total_cycles,
            "duration_ns": duration_ns,
            "syndrome_feedback_ns": syndrome_latency_ns,
            "num_qubits": nq,
            "qasm": qasm_src,
        }


class QickRFSocClient:
    """
    Fermilab QICK ZCU216 Execution Client.

    Automatically detects Xilinx PCIe XDMA character device `/dev/xdma0_h2c_0` when
    physically mounted on PCIe, or dispatches compiled `tProc` v2 assembly + 16-bit
    DAC waveform descriptors via TCP JSON-RPC to `:18888`.
    """

    PCIE_H2C_DEV = "/dev/xdma0_h2c_0"
    PCIE_C2H_DEV = "/dev/xdma0_c2h_0"

    def __init__(
        self,
        host: str = "127.0.0.1",
        port: int = 18888,
        timeout: float = 5.0,
        compiler: Optional[QickProgramCompiler] = None,
    ) -> None:
        self.host = str(host)
        self.port = int(port)
        self.timeout = float(timeout)
        self.compiler = compiler or QickProgramCompiler()
        self._lock = threading.RLock()
        self._rpc_id = 0

    def has_pcie_xdma(self) -> bool:
        """Return True if Xilinx XDMA PCIe H2C DMA node `/dev/xdma0_h2c_0` is present."""
        return os.path.exists(self.PCIE_H2C_DEV)

    def _next_rpc_id(self) -> int:
        self._rpc_id += 1
        return self._rpc_id

    def _execute_pcie_xdma(self, payload: Dict[str, Any]) -> Dict[str, Any]:
        blob = (json.dumps(payload) + "\n").encode("utf-8")
        h2c_fd = os.open(self.PCIE_H2C_DEV, os.O_WRONLY)
        try:
            os.write(h2c_fd, blob)
        finally:
            os.close(h2c_fd)

        if os.path.exists(self.PCIE_C2H_DEV):
            c2h_fd = os.open(self.PCIE_C2H_DEV, os.O_RDONLY)
            try:
                raw = os.read(c2h_fd, 65536).decode("utf-8", errors="replace").strip("\x00\r\n")
                if raw:
                    return json.loads(raw)
            finally:
                os.close(c2h_fd)
        return {"ok": True, "transport": "pcie_xdma"}

    def _execute_tcp_jsonrpc(self, method: str, params: Dict[str, Any]) -> Dict[str, Any]:
        req_id = self._next_rpc_id()
        rpc_req = {
            "jsonrpc": "2.0",
            "id": req_id,
            "method": method,
            "params": params,
        }
        frame = (json.dumps(rpc_req) + "\n").encode("utf-8")
        with self._lock:
            with socket.create_connection((self.host, self.port), timeout=self.timeout) as sock:
                try:
                    sock.setsockopt(socket.IPPROTO_TCP, socket.TCP_NODELAY, 1)
                except OSError:
                    pass
                sock.settimeout(self.timeout)
                sock.sendall(frame)
                rx_buf = bytearray()
                while True:
                    nl = rx_buf.find(b"\n")
                    if nl != -1:
                        line = bytes(rx_buf[:nl]).decode("utf-8")
                        break
                    chunk = sock.recv(8192)
                    if not chunk:
                        line = rx_buf.decode("utf-8")
                        break
                    rx_buf.extend(chunk)

        if not line.strip():
            raise RuntimeError(f"Empty JSON-RPC response from QICK RFSoC {self.host}:{self.port}")
        resp = json.loads(line)
        if "error" in resp and resp["error"] is not None:
            err = resp["error"]
            code = str(err.get("data", {}).get("code", err.get("code", "ERR_QICK_RPC")))
            msg = str(err.get("message", "QICK tProc execution error"))
            return {
                "ok": False,
                "error": code,
                "message": msg,
                "transport": "tcp_jsonrpc",
            }
        result = resp.get("result", {})
        if isinstance(result, dict):
            result.setdefault("transport", "tcp_jsonrpc")
        return result

    def execute(
        self,
        circ_or_qasm: Union[Circuit, str],
        shots: int = 1024,
        raise_on_error: bool = False,
    ) -> Dict[str, Any]:
        """
        Compile `circ_or_qasm` to QICK `tProc` v2 assembly + 16-bit DAC waveforms and
        dispatch to Xilinx RFSoC over PCIe XDMA (`/dev/xdma0_h2c_0`) or TCP JSON-RPC (`:18888`).
        """
        compiled = self.compiler.compile(circ_or_qasm)
        params = {
            "shots": int(shots),
            "qasm": compiled["qasm"],
            "num_qubits": compiled["num_qubits"],
            "assembly": compiled["assembly"],
            "instruction_count": compiled["instruction_count"],
            "waveforms": compiled["waveforms"],
            "f_fabric_mhz": compiled["f_fabric_mhz"],
            "t_tick_ns": compiled["t_tick_ns"],
            "total_cycles": compiled["total_cycles"],
            "duration_ns": compiled["duration_ns"],
            "syndrome_feedback_ns": compiled["syndrome_feedback_ns"],
        }

        if self.has_pcie_xdma() and os.access(self.PCIE_H2C_DEV, os.W_OK):
            hw_res = self._execute_pcie_xdma(params)
        else:
            hw_res = self._execute_tcp_jsonrpc("qick.execute_tproc", params)

        out = {
            **hw_res,
            "assembly": compiled["assembly"],
            "instruction_count": compiled["instruction_count"],
            "waveforms": compiled["waveforms"],
            "f_fabric_mhz": compiled["f_fabric_mhz"],
            "t_tick_ns": compiled["t_tick_ns"],
            "total_cycles": compiled["total_cycles"],
            "duration_ns": compiled["duration_ns"],
            "syndrome_feedback_ns": compiled["syndrome_feedback_ns"],
        }
        if not out.get("ok", True) and raise_on_error:
            raise QickExecutionError(str(out.get("error", "ERR_QICK")), str(out.get("message", "")))
        return out
