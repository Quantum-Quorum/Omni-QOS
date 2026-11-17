from __future__ import annotations

import json
import math
import re
from typing import Any, Dict, List, Optional, Sequence, Tuple, Union

import numpy as np

def _eval_angle(expr: Union[str, float, int]) -> float:
    if isinstance(expr, (int, float)):
        return float(expr)
    s = str(expr).strip().lower().replace(" ", "")
    if not s:
        return 0.0
    env = {"pi": math.pi, "sin": math.sin, "cos": math.cos, "sqrt": math.sqrt}
    if re.fullmatch(r"[0-9pi.+\-*/()e]+", s):
        try:
            return float(eval(s, {"__builtins__": {}}, env))
        except Exception:
            pass
    try:
        return float(s)
    except Exception:
        return 0.0

def _gate_matrix_1q(name: str, param: float = 0.0) -> np.ndarray:
    g = name.upper()
    s2 = 1.0 / math.sqrt(2.0)
    if g == "I":
        return np.eye(2, dtype=np.complex128)
    if g == "H":
        return np.array([[s2, s2], [s2, -s2]], dtype=np.complex128)
    if g == "X":
        return np.array([[0, 1], [1, 0]], dtype=np.complex128)
    if g == "Y":
        return np.array([[0, -1j], [1j, 0]], dtype=np.complex128)
    if g == "Z":
        return np.array([[1, 0], [0, -1]], dtype=np.complex128)
    if g == "S":
        return np.array([[1, 0], [0, 1j]], dtype=np.complex128)
    if g == "SDG":
        return np.array([[1, 0], [0, -1j]], dtype=np.complex128)
    if g == "T":
        return np.array([[1, 0], [0, np.exp(1j * math.pi / 4.0)]], dtype=np.complex128)
    if g == "TDG":
        return np.array([[1, 0], [0, np.exp(-1j * math.pi / 4.0)]], dtype=np.complex128)
    if g == "SX":
        return 0.5 * np.array([[1 + 1j, 1 - 1j], [1 - 1j, 1 + 1j]], dtype=np.complex128)
    if g == "RX":
        c, s = math.cos(param / 2.0), math.sin(param / 2.0)
        return np.array([[c, -1j * s], [-1j * s, c]], dtype=np.complex128)
    if g == "RY":
        c, s = math.cos(param / 2.0), math.sin(param / 2.0)
        return np.array([[c, -s], [s, c]], dtype=np.complex128)
    if g == "RZ":
        return np.array(
            [[np.exp(-0.5j * param), 0], [0, np.exp(0.5j * param)]],
            dtype=np.complex128,
        )
    if g in ("P", "PHASE", "U1"):
        return np.array([[1, 0], [0, np.exp(1j * param)]], dtype=np.complex128)
    raise ValueError(f"Unsupported 1-qubit gate: {name}")

class Statevector:

    def __init__(self, amplitudes: Union[np.ndarray, Sequence[complex]], normalize: bool = True):
        arr = np.asarray(amplitudes, dtype=np.complex128).reshape(-1)
        dim = arr.shape[0]
        if dim == 0 or (dim & (dim - 1)) != 0:
            raise ValueError(f"Hilbert space dimension must be a power of 2, got {dim}")
        if normalize:
            norm = np.linalg.norm(arr)
            if norm > 1e-15:
                arr = arr / norm
        self.data = arr
        self.num_qubits = int(round(math.log2(dim)))

    @classmethod
    def zero_state(cls, num_qubits: int) -> "Statevector":
        vec = np.zeros(1 << num_qubits, dtype=np.complex128)
        vec[0] = 1.0 + 0.0j
        return cls(vec, normalize=False)

    def get_amplitudes(self) -> np.ndarray:
        return self.data.copy()

    def probabilities(self) -> np.ndarray:
        return np.abs(self.data) ** 2

    def phases(self) -> np.ndarray:
        return np.angle(self.data)

    def to_density_matrix(self) -> "DensityMatrix":
        return DensityMatrix(np.outer(self.data, np.conj(self.data)))

    def reduced_density_matrix(self, keep_qubits: Sequence[int]) -> "DensityMatrix":
        return self.to_density_matrix().partial_trace_keep(keep_qubits)

    def bloch_vector(self, qubit: int) -> Tuple[float, float, float, float]:
        rho = self.reduced_density_matrix([qubit]).matrix
        bx = float(2.0 * np.real(rho[0, 1]))
        by = float(-2.0 * np.imag(rho[0, 1]))
        bz = float(np.real(rho[0, 0] - rho[1, 1]))
        purity = float(np.real(np.trace(rho @ rho)))
        return (bx, by, bz, min(1.0, max(0.5, purity)))

    def measure_expectation(self, qubit: int, observable: str = "Z") -> float:
        bx, by, bz, _ = self.bloch_vector(qubit)
        obs = observable.upper()
        if obs == "X":
            return bx
        if obs == "Y":
            return by
        if obs == "Z":
            return bz
        raise ValueError(f"Unsupported observable: {observable}")

    def entanglement_entropy(self, subsystem_a: Optional[Sequence[int]] = None) -> float:
        if self.num_qubits < 2:
            return 0.0
        if subsystem_a is None:
            subsystem_a = list(range(self.num_qubits // 2))
        rho_a = self.reduced_density_matrix(subsystem_a)
        return rho_a.von_neumann_entropy()

    def schmidt_coefficients(self, cut_qubits: Optional[int] = None) -> np.ndarray:
        if self.num_qubits < 2:
            return np.array([1.0])
        na = cut_qubits if cut_qubits is not None else (self.num_qubits // 2)
        na = max(1, min(self.num_qubits - 1, na))
        nb = self.num_qubits - na
        tensor = self.data.reshape((1 << nb, 1 << na))
        svals = np.linalg.svd(tensor, compute_uv=False)
        return svals[svals > 1e-12]

    def sample_counts(self, shots: int = 1024, seed: Optional[int] = None) -> Dict[str, int]:
        rng = np.random.default_rng(seed)
        probs = self.probabilities()
        probs = probs / np.sum(probs)
        outcomes = rng.choice(len(probs), size=int(shots), p=probs)
        counts: Dict[str, int] = {}
        for idx in outcomes:
            bstr = format(int(idx), f"0{self.num_qubits}b")
            counts[bstr] = counts.get(bstr, 0) + 1
        return counts

class DensityMatrix:

    def __init__(self, matrix: np.ndarray):
        mat = np.asarray(matrix, dtype=np.complex128)
        if mat.ndim != 2 or mat.shape[0] != mat.shape[1]:
            raise ValueError("DensityMatrix must be square")
        dim = mat.shape[0]
        if dim == 0 or (dim & (dim - 1)) != 0:
            raise ValueError("Dimension must be a power of 2")
        tr = np.real(np.trace(mat))
        if tr > 1e-15:
            mat = mat / tr
        self.matrix = mat
        self.num_qubits = int(round(math.log2(dim)))

    def purity(self) -> float:
        return float(np.clip(np.real(np.trace(self.matrix @ self.matrix)), 0.0, 1.0))

    def von_neumann_entropy(self) -> float:
        eigvals = np.linalg.eigvalsh(self.matrix)
        eigvals = eigvals[eigvals > 1e-14]
        return float(max(0.0, -np.sum(eigvals * np.log2(eigvals))))

    def partial_trace_keep(self, keep_qubits: Sequence[int]) -> "DensityMatrix":
        keep = sorted(set(int(q) for q in keep_qubits))
        if len(keep) == self.num_qubits:
            return DensityMatrix(self.matrix.copy())
        if len(keep) == 0:
            return DensityMatrix(np.array([[1.0 + 0.0j]]))

        n = self.num_qubits
        keep_axes = [n - 1 - q for q in reversed(keep)]
        trace_axes = [ax for ax in range(n) if ax not in keep_axes]

        reshaped = self.matrix.reshape([2] * (2 * n))
        perm = keep_axes + trace_axes + [ax + n for ax in keep_axes] + [ax + n for ax in trace_axes]
        trans = np.transpose(reshaped, perm)
        dk = 1 << len(keep)
        dt = 1 << len(trace_axes)
        trans = trans.reshape((dk, dt, dk, dt))
        red = np.einsum("aibi->ab", trans)
        return DensityMatrix(red)

    def bloch_vector(self, qubit: int = 0) -> Tuple[float, float, float, float]:
        sub = self.partial_trace_keep([qubit]).matrix if self.num_qubits > 1 else self.matrix
        bx = float(2.0 * np.real(sub[0, 1]))
        by = float(-2.0 * np.imag(sub[0, 1]))
        bz = float(np.real(sub[0, 0] - sub[1, 1]))
        pur = float(np.real(np.trace(sub @ sub)))
        return (bx, by, bz, pur)

    def probabilities(self) -> np.ndarray:
        p = np.real(np.diag(self.matrix))
        p = np.maximum(0.0, p)
        s = np.sum(p)
        return p / s if s > 0 else p

    def sample_counts(self, shots: int = 1024, seed: Optional[int] = None) -> Dict[str, int]:
        rng = np.random.default_rng(seed)
        probs = self.probabilities()
        outcomes = rng.choice(len(probs), size=int(shots), p=probs)
        counts: Dict[str, int] = {}
        for idx in outcomes:
            bstr = format(int(idx), f"0{self.num_qubits}b")
            counts[bstr] = counts.get(bstr, 0) + 1
        return counts

class Circuit:

    def __init__(self, num_qubits: int):
        self.num_qubits = max(1, int(num_qubits))
        self.gates: List[Tuple[Any, ...]] = []

    def add_gate(self, gate_type: str, qubits: Sequence[int], angle: Optional[float] = None) -> "Circuit":
        gt = gate_type.upper()
        qs = [int(q) for q in qubits]
        if gt in ("H", "X", "Y", "Z", "S", "SDG", "T", "TDG", "SX", "RESET", "BARRIER"):
            self.gates.append((gt, qs[0]))
        elif gt in ("RX", "RY", "RZ", "P", "PHASE"):
            self.gates.append((gt, qs[0], float(angle or 0.0)))
        elif gt in ("CNOT", "CX"):
            self.gates.append(("CNOT", qs[0], qs[1]))
        elif gt in ("CY", "CZ", "SWAP", "ISWAP"):
            self.gates.append((gt, qs[0], qs[1]))
        elif gt in ("CP", "CRZ"):
            self.gates.append((gt, qs[0], qs[1], float(angle or 0.0)))
        elif gt in ("CCX", "TOFFOLI"):
            self.gates.append(("CCX", qs[0], qs[1], qs[2]))
        elif gt in ("CSWAP", "FREDKIN"):
            self.gates.append(("CSWAP", qs[0], qs[1], qs[2]))
        elif gt == "MEASURE":
            cbit = qs[1] if len(qs) > 1 else qs[0]
            self.gates.append(("MEASURE", qs[0], cbit))
        else:
            raise ValueError(f"Unsupported gate: {gate_type}")
        return self

    def h(self, qubit: int) -> "Circuit": return self.add_gate("H", [qubit])
    def x(self, qubit: int) -> "Circuit": return self.add_gate("X", [qubit])
    def y(self, qubit: int) -> "Circuit": return self.add_gate("Y", [qubit])
    def z(self, qubit: int) -> "Circuit": return self.add_gate("Z", [qubit])
    def s(self, qubit: int) -> "Circuit": return self.add_gate("S", [qubit])
    def sdg(self, qubit: int) -> "Circuit": return self.add_gate("SDG", [qubit])
    def t(self, qubit: int) -> "Circuit": return self.add_gate("T", [qubit])
    def tdg(self, qubit: int) -> "Circuit": return self.add_gate("TDG", [qubit])
    def sx(self, qubit: int) -> "Circuit": return self.add_gate("SX", [qubit])
    def reset(self, qubit: int) -> "Circuit": return self.add_gate("RESET", [qubit])
    def barrier(self, qubit: int = 0) -> "Circuit": return self.add_gate("BARRIER", [qubit])

    def rx(self, qubit: int, angle: float) -> "Circuit": return self.add_gate("RX", [qubit], angle)
    def ry(self, qubit: int, angle: float) -> "Circuit": return self.add_gate("RY", [qubit], angle)
    def rz(self, qubit: int, angle: float) -> "Circuit": return self.add_gate("RZ", [qubit], angle)
    def phase(self, qubit: int, angle: float) -> "Circuit": return self.add_gate("PHASE", [qubit], angle)
    def p(self, qubit: int, angle: float) -> "Circuit": return self.add_gate("P", [qubit], angle)

    def cx(self, control: int, target: int) -> "Circuit": return self.add_gate("CNOT", [control, target])
    def cy(self, control: int, target: int) -> "Circuit": return self.add_gate("CY", [control, target])
    def cz(self, control: int, target: int) -> "Circuit": return self.add_gate("CZ", [control, target])
    def swap(self, qubit1: int, qubit2: int) -> "Circuit": return self.add_gate("SWAP", [qubit1, qubit2])
    def iswap(self, qubit1: int, qubit2: int) -> "Circuit": return self.add_gate("ISWAP", [qubit1, qubit2])
    def cp(self, angle: float, control: int, target: int) -> "Circuit": return self.add_gate("CP", [control, target], angle)
    def crz(self, angle: float, control: int, target: int) -> "Circuit": return self.add_gate("CRZ", [control, target], angle)
    def ccx(self, c1: int, c2: int, target: int) -> "Circuit": return self.add_gate("CCX", [c1, c2, target])
    def cswap(self, ctrl: int, q1: int, q2: int) -> "Circuit": return self.add_gate("CSWAP", [ctrl, q1, q2])

    def measure(self, qubit: int, cbit: Optional[int] = None) -> "Circuit":
        if cbit is None:
            cbit = qubit
        self.gates.append(("MEASURE", int(qubit), int(cbit)))
        return self

    def measure_all(self) -> "Circuit":
        for q in range(self.num_qubits):
            self.measure(q, q)
        return self

    def execute(
        self,
        max_steps: Optional[int] = None,
        noise_model: Any = None,
    ) -> Union[Statevector, DensityMatrix]:
        n = self.num_qubits
        psi = np.zeros([2] * n, dtype=np.complex128)
        psi[(0,) * n] = 1.0 + 0.0j

        def axis_of(q: int) -> int:
            return n - 1 - q

        def apply_1q_tensor(arr: np.ndarray, q: int, mat: np.ndarray) -> np.ndarray:
            ax = axis_of(q)
            out = np.tensordot(mat, arr, axes=([1], [ax]))
            return np.moveaxis(out, 0, ax)

        def apply_controlled_tensor(arr: np.ndarray, controls: List[int], target: int, mat: np.ndarray) -> np.ndarray:
            res = arr.copy()
            idx_ctrl = [slice(None)] * n
            for c in controls:
                idx_ctrl[axis_of(c)] = 1
            sub = arr[tuple(idx_ctrl)]
            rem_axes = [i for i in range(n) if i not in [axis_of(c) for c in controls]]
            t_sub_ax = rem_axes.index(axis_of(target))
            updated = np.moveaxis(np.tensordot(mat, sub, axes=([1], [t_sub_ax])), 0, t_sub_ax)
            res[tuple(idx_ctrl)] = updated
            return res

        gate_seq = self.gates if max_steps is None else self.gates[:max_steps]
        for g in gate_seq:
            gt = g[0].upper()
            if gt in ("MEASURE", "BARRIER"):
                continue
            if gt == "RESET":
                q = g[1]
                ax = axis_of(q)
                sl0 = [slice(None)] * n
                sl1 = [slice(None)] * n
                sl0[ax] = 0
                sl1[ax] = 1
                p0 = float(np.sum(np.abs(psi[tuple(sl0)]) ** 2))
                if p0 > 1e-12:
                    psi[tuple(sl1)] = 0.0
                    psi = psi / np.linalg.norm(psi)
                else:
                    psi = apply_1q_tensor(psi, q, _gate_matrix_1q("X"))
                continue
            if gt in ("H", "X", "Y", "Z", "S", "SDG", "T", "TDG", "SX"):
                psi = apply_1q_tensor(psi, g[1], _gate_matrix_1q(gt))
            elif gt in ("RX", "RY", "RZ", "P", "PHASE"):
                psi = apply_1q_tensor(psi, g[1], _gate_matrix_1q(gt, float(g[2])))
            elif gt in ("CNOT", "CX"):
                psi = apply_controlled_tensor(psi, [g[1]], g[2], _gate_matrix_1q("X"))
            elif gt == "CY":
                psi = apply_controlled_tensor(psi, [g[1]], g[2], _gate_matrix_1q("Y"))
            elif gt == "CZ":
                psi = apply_controlled_tensor(psi, [g[1]], g[2], _gate_matrix_1q("Z"))
            elif gt == "CP":
                psi = apply_controlled_tensor(psi, [g[1]], g[2], _gate_matrix_1q("P", float(g[3])))
            elif gt == "CRZ":
                psi = apply_controlled_tensor(psi, [g[1]], g[2], _gate_matrix_1q("RZ", float(g[3])))
            elif gt == "SWAP":
                psi = np.swapaxes(psi, axis_of(g[1]), axis_of(g[2])).copy()
            elif gt == "ISWAP":
                a1, a2 = axis_of(g[1]), axis_of(g[2])
                res = psi.copy()
                s01 = [slice(None)] * n
                s10 = [slice(None)] * n
                s01[a1], s01[a2] = 0, 1
                s10[a1], s10[a2] = 1, 0
                res[tuple(s01)] = 1j * psi[tuple(s10)]
                res[tuple(s10)] = 1j * psi[tuple(s01)]
                psi = res
            elif gt == "CCX":
                psi = apply_controlled_tensor(psi, [g[1], g[2]], g[3], _gate_matrix_1q("X"))
            elif gt == "CSWAP":
                c_ax, a1, a2 = axis_of(g[1]), axis_of(g[2]), axis_of(g[3])
                res = psi.copy()
                sc01 = [slice(None)] * n
                sc10 = [slice(None)] * n
                sc01[c_ax], sc01[a1], sc01[a2] = 1, 0, 1
                sc10[c_ax], sc10[a1], sc10[a2] = 1, 1, 0
                res[tuple(sc01)] = psi[tuple(sc10)]
                res[tuple(sc10)] = psi[tuple(sc01)]
                psi = res

        sv = Statevector(psi.reshape(-1), normalize=True)
        if noise_model is None:
            return sv

        dm = sv.to_density_matrix()
        p_depol = 0.0
        gamma_t1 = 0.0
        for ch in getattr(noise_model, "channels", []):
            if ch[0] == "DEPOLARIZING":
                p_depol = max(p_depol, float(ch[1]))
            elif ch[0] == "RELAXATION":
                t1 = max(1.0, float(ch[1]))
                gamma_t1 = max(gamma_t1, min(0.25, 0.05 / (t1 / 50.0)))
            elif ch[0] == "DUAL_RAIL_MAGNON_LOSS":
                gamma_t1 = max(gamma_t1, float(ch[1]))

        rho = dm.matrix
        dim = rho.shape[0]
        if p_depol > 0.0:
            rho = (1.0 - p_depol) * rho + (p_depol / dim) * np.eye(dim, dtype=np.complex128)
        if gamma_t1 > 0.0:
            proj0 = np.zeros_like(rho)
            proj0[0, 0] = 1.0
            rho = (1.0 - gamma_t1) * rho + gamma_t1 * proj0
        return DensityMatrix(rho)

    def sample(self, shots: int = 1024, noise_model: Any = None, seed: Optional[int] = None) -> Dict[str, int]:
        st = self.execute(noise_model=noise_model)
        return st.sample_counts(shots=shots, seed=seed)

    def to_qasm(self, version: str = "2.0") -> str:
        if str(version).startswith("3"):
            lines = [
                "OPENQASM 3.0;",
                'include "stdgates.inc";',
                f"qubit[{self.num_qubits}] q;",
                f"bit[{self.num_qubits}] c;",
            ]
        else:
            lines = [
                "OPENQASM 2.0;",
                'include "qelib1.inc";',
                f"qreg q[{self.num_qubits}];",
                f"creg c[{self.num_qubits}];",
            ]

        for g in self.gates:
            gt = g[0].upper()
            if gt in ("H", "X", "Y", "Z", "S", "SDG", "T", "TDG", "SX", "RESET", "BARRIER"):
                lines.append(f"{gt.lower()} q[{g[1]}];")
            elif gt in ("CNOT", "CX"):
                lines.append(f"cx q[{g[1]}], q[{g[2]}];")
            elif gt in ("CY", "CZ", "SWAP", "ISWAP"):
                lines.append(f"{gt.lower()} q[{g[1]}], q[{g[2]}];")
            elif gt in ("RX", "RY", "RZ"):
                lines.append(f"{gt.lower()}({g[2]:.6g}) q[{g[1]}];")
            elif gt in ("PHASE", "P"):
                lines.append(f"p({g[2]:.6g}) q[{g[1]}];")
            elif gt in ("CP", "CRZ"):
                lines.append(f"{gt.lower()}({g[3]:.6g}) q[{g[1]}], q[{g[2]}];")
            elif gt == "CCX":
                lines.append(f"ccx q[{g[1]}], q[{g[2]}], q[{g[3]}];")
            elif gt == "CSWAP":
                lines.append(f"cswap q[{g[1]}], q[{g[2]}], q[{g[3]}];")
            elif gt == "MEASURE":
                c = g[2] if len(g) > 2 and g[2] is not None else g[1]
                if str(version).startswith("3"):
                    lines.append(f"c[{c}] = measure q[{g[1]}];")
                else:
                    lines.append(f"measure q[{g[1]}] -> c[{c}];")
        return "\n".join(lines) + "\n"

    @classmethod
    def from_qasm(cls, qasm_str: str) -> "Circuit":
        nq = 1
        found_decl = False
        for m in re.finditer(r"(?:qubit\s*\[(\d+)\]|qreg\s+[a-z_][a-z0-9_]*\s*\[(\d+)\])", qasm_str, re.IGNORECASE):
            val = m.group(1) or m.group(2)
            if val:
                nq = max(nq, int(val))
                found_decl = True
        if not found_decl:
            for m in re.finditer(r"q\[(\d+)\]", qasm_str):
                nq = max(nq, int(m.group(1)) + 1)

        circ = cls(nq)
        for raw_line in qasm_str.splitlines():
            line = raw_line.split("//")[0].strip()
            if not line:
                continue
            for stmt in line.split(";"):
                s = stmt.strip()
                if not s:
                    continue
                low = s.lower()
                if low.startswith(("openqasm", "include", "qubit", "qreg", "bit", "creg")):
                    continue
                if "measure" in low:
                    qm = re.search(r"q\[(\d+)\]", low)
                    cm = re.search(r"c\[(\d+)\]", low)
                    if qm:
                        q_idx = int(qm.group(1))
                        c_idx = int(cm.group(1)) if cm else q_idx
                        circ.measure(q_idx, c_idx)
                    continue
                m = re.match(r"^([a-z0-9_]+)(?:\(([^)]*)\))?\s+(.*)$", low)
                if not m:
                    continue
                gname, angle_str, rest = m.group(1), m.group(2), m.group(3)
                qs = [int(x) for x in re.findall(r"q\[(\d+)\]", rest)]
                if not qs:
                    continue
                ang = _eval_angle(angle_str) if angle_str is not None else 0.0
                gmap = {
                    "h": "H", "x": "X", "y": "Y", "z": "Z", "s": "S", "sdg": "SDG",
                    "t": "T", "tdg": "TDG", "sx": "SX", "reset": "RESET", "barrier": "BARRIER",
                    "rx": "RX", "ry": "RY", "rz": "RZ", "p": "P", "u1": "P", "phase": "PHASE",
                    "cx": "CNOT", "cnot": "CNOT", "cy": "CY", "cz": "CZ",
                    "swap": "SWAP", "iswap": "ISWAP", "cp": "CP", "cu1": "CP", "crz": "CRZ",
                    "ccx": "CCX", "toffoli": "CCX", "cswap": "CSWAP", "fredkin": "CSWAP",
                }
                if gname in gmap:
                    circ.add_gate(gmap[gname], qs, angle=ang)
        return circ

    def to_qiskit_code(self) -> str:
        lines = [
            "from qiskit import QuantumCircuit",
            "from omniqos.qiskit import OmniQBackend",
            "",
            f"qc = QuantumCircuit({self.num_qubits}, {self.num_qubits})",
        ]
        for g in self.gates:
            gt = g[0].upper()
            if gt in ("H", "X", "Y", "Z", "S", "SDG", "T", "TDG", "SX", "RESET"):
                lines.append(f"qc.{gt.lower()}({g[1]})")
            elif gt in ("RX", "RY", "RZ", "P"):
                lines.append(f"qc.{gt.lower()}({g[2]:.6g}, {g[1]})")
            elif gt == "PHASE":
                lines.append(f"qc.p({g[2]:.6g}, {g[1]})")
            elif gt in ("CNOT", "CX"):
                lines.append(f"qc.cx({g[1]}, {g[2]})")
            elif gt in ("CY", "CZ", "SWAP", "ISWAP"):
                lines.append(f"qc.{gt.lower()}({g[1]}, {g[2]})")
            elif gt in ("CP", "CRZ"):
                lines.append(f"qc.{gt.lower()}({g[3]:.6g}, {g[1]}, {g[2]})")
            elif gt == "CCX":
                lines.append(f"qc.ccx({g[1]}, {g[2]}, {g[3]})")
            elif gt == "CSWAP":
                lines.append(f"qc.cswap({g[1]}, {g[2]}, {g[3]})")
            elif gt == "MEASURE":
                lines.append(f"qc.measure({g[1]}, {g[2]})")
        lines += [
            "",
            "backend = OmniQBackend()",
            "counts = backend.run(qc, shots=1024).result().get_counts()",
            "print(counts)",
        ]
        return "\n".join(lines) + "\n"

    def to_cirq_code(self) -> str:
        lines = [
            "import cirq",
            "from omniqos.cirq import OmniQSampler",
            "",
            f"q = cirq.LineQubit.range({self.num_qubits})",
            "circuit = cirq.Circuit(",
        ]
        for g in self.gates:
            gt = g[0].upper()
            if gt in ("H", "X", "Y", "Z", "S", "T"):
                lines.append(f"    cirq.{gt}(q[{g[1]}]),")
            elif gt in ("RX", "RY", "RZ"):
                lines.append(f"    cirq.{gt.lower()}({g[2]:.6g}).on(q[{g[1]}]),")
            elif gt in ("CNOT", "CX"):
                lines.append(f"    cirq.CNOT(q[{g[1]}], q[{g[2]}]),")
            elif gt == "CZ":
                lines.append(f"    cirq.CZ(q[{g[1]}], q[{g[2]}]),")
            elif gt == "SWAP":
                lines.append(f"    cirq.SWAP(q[{g[1]}], q[{g[2]}]),")
            elif gt == "CCX":
                lines.append(f"    cirq.TOFFOLI(q[{g[1]}], q[{g[2]}], q[{g[3]}]),")
        lines += [
            ")",
            "sampler = OmniQSampler()",
            "print(sampler.run(circuit, repetitions=1024).histogram())",
        ]
        return "\n".join(lines) + "\n"

    def to_pennylane_code(self) -> str:
        lines = [
            "import pennylane as qml",
            "from omniqos.pennylane import OmniQPennyLaneDevice",
            "",
            f"dev = OmniQPennyLaneDevice(wires={self.num_qubits}, shots=1024)",
        ]
        for g in self.gates:
            gt = g[0].upper()
            if gt in ("H", "X", "Y", "Z", "S", "T", "SX"):
                nm = "Hadamard" if gt == "H" else (f"Pauli{gt}" if gt in ("X", "Y", "Z") else gt)
                lines.append(f"# qml.{nm}(wires={g[1]})")
            elif gt in ("RX", "RY", "RZ"):
                lines.append(f"# qml.{gt}({g[2]:.6g}, wires={g[1]})")
            elif gt in ("CNOT", "CX"):
                lines.append(f"# qml.CNOT(wires=[{g[1]}, {g[2]}])")
        lines.append("print(dev.execute(None))")
        return "\n".join(lines) + "\n"

    def to_dict(self) -> Dict[str, Any]:
        gate_list = []
        for i, g in enumerate(self.gates):
            gt = g[0]
            gd: Dict[str, Any] = {"type": gt, "qubit": g[1], "step": i}
            if gt in ("CNOT", "CX", "CY", "CZ", "SWAP", "ISWAP", "MEASURE"):
                gd["target"] = g[2]
            elif gt in ("CP", "CRZ"):
                gd["target"] = g[2]
                gd["parameter"] = g[3]
            elif gt in ("CCX", "CSWAP"):
                gd["control2"] = g[2]
                gd["target"] = g[3]
            elif gt in ("RX", "RY", "RZ", "PHASE", "P"):
                gd["parameter"] = g[2]
            gate_list.append(gd)
        return {"num_qubits": self.num_qubits, "gates": gate_list}

    def save_json(self, path: str) -> None:
        with open(path, "w") as f:
            json.dump(self.to_dict(), f, indent=2)

    def debug(self, noise_model: Any = None, view_mode: Optional[str] = None):
        from .debugger import show_debugger
        return show_debugger(self, noise_model=noise_model, view_mode=view_mode)

    def show(self, view_mode: Optional[str] = None):
        return self.debug(view_mode=view_mode)

    def __str__(self) -> str:
        return f"Circuit({self.num_qubits} qubits, {len(self.gates)} gates)"

    def __repr__(self) -> str:
        return self.__str__()
