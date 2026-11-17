from __future__ import annotations

import os
import random
import re
import sys
import time
from typing import Any, Dict, List, Optional, Tuple

_REPO_ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", "..", ".."))
_SRC_DIR = os.path.join(_REPO_ROOT, "src")
if _SRC_DIR not in sys.path and os.path.isdir(_SRC_DIR):
    sys.path.insert(0, _SRC_DIR)

try:
    from omniq_core_bridge import OmniqCoreBridge
except Exception:
    OmniqCoreBridge = None

class _PythonCHPTableau:

    def __init__(self, num_qubits: int):
        self.n = max(1, min(1024, int(num_qubits)))
        self.mask = (1 << self.n) - 1
        rows = 2 * self.n + 1
        self.x: List[int] = [0] * rows
        self.z: List[int] = [0] * rows
        self.r: List[int] = [0] * rows
        for i in range(self.n):
            bit = 1 << i
            self.x[i] = bit
            self.z[self.n + i] = bit

    def clone(self) -> "_PythonCHPTableau":
        cp = _PythonCHPTableau.__new__(_PythonCHPTableau)
        cp.n = self.n
        cp.mask = self.mask
        cp.x = self.x.copy()
        cp.z = self.z.copy()
        cp.r = self.r.copy()
        return cp

    def h(self, a: int) -> None:
        bit = 1 << a
        for row in range(2 * self.n):
            xa = (self.x[row] >> a) & 1
            za = (self.z[row] >> a) & 1
            self.r[row] ^= xa & za
            if xa != za:
                self.x[row] ^= bit
                self.z[row] ^= bit

    def s(self, a: int) -> None:
        bit = 1 << a
        for row in range(2 * self.n):
            xa = (self.x[row] >> a) & 1
            za = (self.z[row] >> a) & 1
            self.r[row] ^= xa & za
            if xa:
                self.z[row] ^= bit

    def sdg(self, a: int) -> None:
        bit = 1 << a
        for row in range(2 * self.n):
            xa = (self.x[row] >> a) & 1
            za = (self.z[row] >> a) & 1
            self.r[row] ^= xa & (za ^ 1)
            if xa:
                self.z[row] ^= bit

    def x_gate(self, a: int) -> None:
        for row in range(2 * self.n):
            self.r[row] ^= (self.z[row] >> a) & 1

    def z_gate(self, a: int) -> None:
        for row in range(2 * self.n):
            self.r[row] ^= (self.x[row] >> a) & 1

    def y_gate(self, a: int) -> None:
        for row in range(2 * self.n):
            self.r[row] ^= ((self.x[row] ^ self.z[row]) >> a) & 1

    def sx(self, a: int) -> None:
        self.h(a)
        self.s(a)
        self.h(a)

    def sxdg(self, a: int) -> None:
        self.h(a)
        self.sdg(a)
        self.h(a)

    def cx(self, a: int, b: int) -> None:
        if a == b:
            return
        ma = 1 << a
        mb = 1 << b
        for row in range(2 * self.n):
            xa = (self.x[row] >> a) & 1
            za = (self.z[row] >> a) & 1
            xb = (self.x[row] >> b) & 1
            zb = (self.z[row] >> b) & 1
            self.r[row] ^= (xa & zb) & (xb ^ za ^ 1)
            if xa:
                self.x[row] ^= mb
            if zb:
                self.z[row] ^= ma

    def cz(self, a: int, b: int) -> None:
        if a == b:
            return
        self.h(b)
        self.cx(a, b)
        self.h(b)

    def cy(self, a: int, b: int) -> None:
        if a == b:
            return
        self.sdg(b)
        self.cx(a, b)
        self.s(b)

    def swap(self, a: int, b: int) -> None:
        if a == b:
            return
        self.cx(a, b)
        self.cx(b, a)
        self.cx(a, b)

    def rowsum(self, h_row: int, i_row: int) -> None:
        mask = self.mask
        x1 = self.x[i_row]
        z1 = self.z[i_row]
        x2 = self.x[h_row]
        z2 = self.z[h_row]
        nx1 = x1 ^ mask
        nz1 = z1 ^ mask
        nx2 = x2 ^ mask
        nz2 = z2 ^ mask
        pos = (x1 & z1 & nx2 & z2) | (x1 & nz1 & x2 & z2) | (nx1 & z1 & x2 & nz2)
        neg = (x1 & z1 & x2 & nz2) | (x1 & nz1 & nx2 & z2) | (nx1 & z1 & x2 & z2)
        sum_g = pos.bit_count() - neg.bit_count()
        m = (2 * self.r[h_row] + 2 * self.r[i_row] + sum_g) % 4
        self.r[h_row] = 1 if m == 2 else 0
        self.x[h_row] = x2 ^ x1
        self.z[h_row] = z2 ^ z1

    def measure(self, a: int, rng: random.Random) -> Tuple[int, bool]:
        p = -1
        for row in range(self.n, 2 * self.n):
            if (self.x[row] >> a) & 1:
                p = row
                break
        if p >= 0:
            destab = p - self.n
            for row in range(2 * self.n):
                if row != p and row != destab and ((self.x[row] >> a) & 1):
                    self.rowsum(row, p)
            self.x[destab] = self.x[p]
            self.z[destab] = self.z[p]
            self.r[destab] = self.r[p]
            self.x[p] = 0
            self.z[p] = 1 << a
            outcome = rng.getrandbits(1)
            self.r[p] = outcome
            return outcome, False

        scratch = 2 * self.n
        self.x[scratch] = 0
        self.z[scratch] = 0
        self.r[scratch] = 0
        for i in range(self.n):
            if (self.x[i] >> a) & 1:
                self.rowsum(scratch, self.n + i)
        return self.r[scratch], True

    def reset_qubit(self, a: int, rng: random.Random) -> None:
        m, _ = self.measure(a, rng)
        if m == 1:
            self.x_gate(a)

    def format_stabilizer(self, idx: int) -> str:
        row = self.n + idx
        sign = "-" if self.r[row] else "+"
        if self.n <= 64:
            chars = [sign]
            xr = self.x[row]
            zr = self.z[row]
            for q in range(self.n):
                xq = (xr >> q) & 1
                zq = (zr >> q) & 1
                if xq and zq:
                    chars.append("Y")
                elif xq:
                    chars.append("X")
                elif zq:
                    chars.append("Z")
                else:
                    chars.append("I")
            return "".join(chars)
        parts = []
        xr = self.x[row]
        zr = self.z[row]
        for q in range(self.n):
            xq = (xr >> q) & 1
            zq = (zr >> q) & 1
            if not xq and not zq:
                continue
            if len(parts) >= 12:
                parts.append("...")
                break
            ch = "Y" if (xq and zq) else ("X" if xq else "Z")
            parts.append(f"{ch}{q}")
        return sign + ("*".join(parts) if parts else "I")

def _simulate_clifford_python(qasm: str, shots: int = 256) -> Dict[str, Any]:
    t0 = time.perf_counter_ns()
    stmts: List[str] = []
    for raw_line in qasm.splitlines():
        line = raw_line.split("//", 1)[0]
        for st in line.split(";"):
            s = st.strip()
            if s:
                stmts.append(s)

    max_q = 1
    max_c = 0
    ops: List[Tuple[str, int, int]] = []
    unitary_gates = 0
    has_explicit_meas = False
    seen_nonunitary = False
    mid_circuit_nonunitary = False

    idx_re = re.compile(r"([a-zA-Z_]\w*)\s*\[\s*(\d+)\s*\]")

    for st in stmts:
        low = st.lower()
        if low.startswith(("openqasm", "include", "barrier")):
            continue
        if low.startswith(("qubit[", "qreg ")):
            m = re.search(r"\[\s*(\d+)\s*\]", low)
            if m:
                max_q = max(max_q, min(1024, int(m.group(1))))
            continue
        if low.startswith(("bit[", "creg ")):
            m = re.search(r"\[\s*(\d+)\s*\]", low)
            if m:
                max_c = max(max_c, min(1024, int(m.group(1))))
            continue
        if "measure" in low:
            has_explicit_meas = True
            seen_nonunitary = True
            refs = idx_re.findall(low)
            if refs:
                if "=" in low and len(refs) >= 2:
                    c_idx = int(refs[0][1])
                    q_idx = int(refs[1][1])
                elif len(refs) >= 2:
                    q_idx = int(refs[0][1])
                    c_idx = int(refs[1][1])
                else:
                    q_idx = int(refs[0][1])
                    c_idx = q_idx
                max_q = max(max_q, q_idx + 1)
                max_c = max(max_c, c_idx + 1)
                ops.append(("measure", q_idx, c_idx))
            continue

        parts = low.split(None, 1)
        if not parts:
            continue
        gname = parts[0]
        refs = idx_re.findall(parts[1] if len(parts) > 1 else "")
        if not refs:
            continue
        if gname == "reset":
            q_idx = int(refs[0][1])
            max_q = max(max_q, q_idx + 1)
            seen_nonunitary = True
            mid_circuit_nonunitary = True
            ops.append(("reset", q_idx, 0))
            continue
        if gname in ("h", "s", "sdg", "x", "y", "z", "sx", "sxdg"):
            q_idx = int(refs[0][1])
            max_q = max(max_q, q_idx + 1)
            if seen_nonunitary:
                mid_circuit_nonunitary = True
            ops.append((gname, q_idx, 0))
            unitary_gates += 1
        elif gname in ("cx", "cnot", "cy", "cz", "swap") and len(refs) >= 2:
            qa, qb = int(refs[0][1]), int(refs[1][1])
            max_q = max(max_q, qa + 1, qb + 1)
            if seen_nonunitary:
                mid_circuit_nonunitary = True
            ops.append(("cx" if gname == "cnot" else gname, qa, qb))
            unitary_gates += 1

    nq = max(1, min(1024, max_q))
    out_w = max(1, min(1024, max_c if has_explicit_meas else nq))
    n_shots = max(1, min(100000, int(shots)))
    rng = random.Random(0x4F4D4E4951 ^ len(qasm))

    def apply_u(tab: _PythonCHPTableau, op: Tuple[str, int, int]) -> None:
        g, a, b = op
        if g == "h":
            tab.h(a)
        elif g == "s":
            tab.s(a)
        elif g == "sdg":
            tab.sdg(a)
        elif g == "x":
            tab.x_gate(a)
        elif g == "y":
            tab.y_gate(a)
        elif g == "z":
            tab.z_gate(a)
        elif g == "sx":
            tab.sx(a)
        elif g == "sxdg":
            tab.sxdg(a)
        elif g == "cx":
            tab.cx(a, b)
        elif g == "cy":
            tab.cy(a, b)
        elif g == "cz":
            tab.cz(a, b)
        elif g == "swap":
            tab.swap(a, b)

    split_idx = 0
    while split_idx < len(ops) and ops[split_idx][0] not in ("measure", "reset"):
        split_idx += 1

    base = _PythonCHPTableau(nq)
    for i in range(split_idx):
        apply_u(base, ops[i])

    counts: Dict[str, int] = {}
    det_meas = 0
    rnd_meas = 0
    stab_sample: List[str] = []

    if not mid_circuit_nonunitary:
        stab_sample = [base.format_stabilizer(i) for i in range(min(nq, 16))]
        stab_x = base.x[nq : 2 * nq]
        ref = base.clone()
        meas_map = [(op[1], op[2]) for op in ops[split_idx:] if op[0] == "measure"] if has_explicit_meas else [(q, q) for q in range(nq)]
        measured = [False] * nq
        anchor = 0
        for q, _ in meas_map:
            if not measured[q]:
                m, is_det = ref.measure(q, rng)
                if is_det:
                    det_meas += 1
                else:
                    rnd_meas += 1
                measured[q] = True
                if m:
                    anchor |= 1 << q
        for q in range(nq):
            if not measured[q]:
                m, _ = ref.measure(q, rng)
                if m:
                    anchor |= 1 << q

        for s_idx in range(n_shots):
            vec = anchor
            if s_idx > 0 and rnd_meas > 0:
                rbits = rng.getrandbits(nq)
                while rbits:
                    lsb = rbits & -rbits
                    k = lsb.bit_length() - 1
                    vec ^= stab_x[k]
                    rbits ^= lsb
            chars = ["0"] * out_w
            for q, c in meas_map:
                if 0 <= c < out_w and ((vec >> q) & 1):
                    chars[out_w - 1 - c] = "1"
            bs = "".join(chars)
            counts[bs] = counts.get(bs, 0) + 1
    else:
        for s_idx in range(n_shots):
            tab = base.clone()
            chars = ["0"] * out_w
            s_det = 0
            s_rnd = 0
            for op in ops[split_idx:]:
                if op[0] == "reset":
                    tab.reset_qubit(op[1], rng)
                elif op[0] == "measure":
                    m, is_det = tab.measure(op[1], rng)
                    if is_det:
                        s_det += 1
                    else:
                        s_rnd += 1
                    if 0 <= op[2] < out_w:
                        chars[out_w - 1 - op[2]] = "1" if m else "0"
                else:
                    apply_u(tab, op)
            if not has_explicit_meas:
                for q in range(nq):
                    m, is_det = tab.measure(q, rng)
                    if is_det:
                        s_det += 1
                    else:
                        s_rnd += 1
                    chars[out_w - 1 - q] = "1" if m else "0"
            if s_idx == 0:
                det_meas = s_det
                rnd_meas = s_rnd
                stab_sample = [tab.format_stabilizer(i) for i in range(min(nq, 16))]
            bs = "".join(chars)
            counts[bs] = counts.get(bs, 0) + 1

    exec_ns = time.perf_counter_ns() - t0
    return {
        "backend": "python_chp_tableau",
        "num_qubits": nq,
        "num_gates": unitary_gates,
        "execution_time_ns": exec_ns,
        "stabilizer_generators_sample": stab_sample,
        "counts": counts,
        "deterministic_measurements": det_meas,
        "random_measurements": rnd_meas,
    }

class CliffordSimulator:

    def __init__(self, num_qubits: int = 2):
        if num_qubits < 1 or num_qubits > 1024:
            raise ValueError("CliffordSimulator supports 1..1024 qubits")
        self.num_qubits = int(num_qubits)
        self._statements: List[str] = []
        self._max_cbit: int = 0
        self._has_measures: bool = False

    def h(self, q: int) -> "CliffordSimulator":
        self._statements.append(f"h q[{int(q)}];")
        return self

    def s(self, q: int) -> "CliffordSimulator":
        self._statements.append(f"s q[{int(q)}];")
        return self

    def sdg(self, q: int) -> "CliffordSimulator":
        self._statements.append(f"sdg q[{int(q)}];")
        return self

    def x(self, q: int) -> "CliffordSimulator":
        self._statements.append(f"x q[{int(q)}];")
        return self

    def y(self, q: int) -> "CliffordSimulator":
        self._statements.append(f"y q[{int(q)}];")
        return self

    def z(self, q: int) -> "CliffordSimulator":
        self._statements.append(f"z q[{int(q)}];")
        return self

    def sx(self, q: int) -> "CliffordSimulator":
        self._statements.append(f"sx q[{int(q)}];")
        return self

    def cx(self, ctrl: int, targ: int) -> "CliffordSimulator":
        self._statements.append(f"cx q[{int(ctrl)}], q[{int(targ)}];")
        return self

    cnot = cx

    def cy(self, ctrl: int, targ: int) -> "CliffordSimulator":
        self._statements.append(f"cy q[{int(ctrl)}], q[{int(targ)}];")
        return self

    def cz(self, q0: int, q1: int) -> "CliffordSimulator":
        self._statements.append(f"cz q[{int(q0)}], q[{int(q1)}];")
        return self

    def swap(self, q0: int, q1: int) -> "CliffordSimulator":
        self._statements.append(f"swap q[{int(q0)}], q[{int(q1)}];")
        return self

    def reset(self, q: int) -> "CliffordSimulator":
        self._statements.append(f"reset q[{int(q)}];")
        return self

    def measure(self, q: int, cbit: Optional[int] = None) -> "CliffordSimulator":
        cb = int(q) if cbit is None else int(cbit)
        self._has_measures = True
        self._max_cbit = max(self._max_cbit, cb + 1)
        self._statements.append(f"measure q[{int(q)}] -> c[{cb}];")
        return self

    def measure_all(self) -> "CliffordSimulator":
        for q in range(self.num_qubits):
            self.measure(q, q)
        return self

    def to_qasm(self) -> str:
        lines = ["OPENQASM 3.0;", f"qubit[{self.num_qubits}] q;"]
        if self._has_measures:
            lines.append(f"bit[{max(self.num_qubits, self._max_cbit)}] c;")
        lines.extend(self._statements)
        return "\n".join(lines) + "\n"

    def run(
        self,
        shots: int = 256,
        qasm: Optional[str] = None,
        use_native: bool = True,
    ) -> Dict[str, Any]:
        qasm_str = qasm if qasm is not None else self.to_qasm()
        if use_native and OmniqCoreBridge is not None:
            res = OmniqCoreBridge().simulate_clifford(qasm_str, shots=shots)
            if res is not None:
                return res
        return _simulate_clifford_python(qasm_str, shots=shots)

    simulate = run

    @classmethod
    def simulate_qasm(cls, qasm: str, shots: int = 256, use_native: bool = True) -> Dict[str, Any]:
        if use_native and OmniqCoreBridge is not None:
            res = OmniqCoreBridge().simulate_clifford(qasm, shots=shots)
            if res is not None:
                return res
        return _simulate_clifford_python(qasm, shots=shots)

    run_qasm = simulate_qasm

    @staticmethod
    def surface_code_memory_qasm(distance: int = 3, rounds: int = 3) -> str:
        d = int(distance)
        r_cnt = max(1, int(rounds))
        if d < 3 or d % 2 == 0:
            raise ValueError("Rotated surface code distance must be an odd integer >= 3")
        n_data = d * d
        n_anc = d * d - 1
        total_q = n_data + n_anc
        if total_q > 1024:
            raise ValueError(f"Surface code distance {d} requires {total_q} > 1024 qubits")

        def dq(row: int, col: int) -> int:
            return row * d + col

        stabilizers: List[Tuple[str, List[int]]] = []
        for r in range(d - 1):
            for c in range(d - 1):
                kind = "Z" if (r + c) % 2 == 0 else "X"
                stabilizers.append((kind, [dq(r, c), dq(r, c + 1), dq(r + 1, c), dq(r + 1, c + 1)]))
        for c in range(0, d - 1, 2):
            stabilizers.append(("X", [dq(0, c), dq(0, c + 1)]))
        for c in range(1, d - 1, 2):
            stabilizers.append(("X", [dq(d - 1, c), dq(d - 1, c + 1)]))
        for r in range(1, d - 1, 2):
            stabilizers.append(("Z", [dq(r, 0), dq(r + 1, 0)]))
        for r in range(0, d - 1, 2):
            stabilizers.append(("Z", [dq(r, d - 1), dq(r + 1, d - 1)]))

        lines = [
            "OPENQASM 3.0;",
            f"qubit[{total_q}] q;",
            f"bit[{total_q}] c;",
        ]

        for _rnd in range(r_cnt):
            for idx, (kind, data_list) in enumerate(stabilizers):
                anc = n_data + idx
                lines.append(f"reset q[{anc}];")
                if kind == "Z":
                    for qd in data_list:
                        lines.append(f"cx q[{qd}], q[{anc}];")
                else:
                    lines.append(f"h q[{anc}];")
                    for qd in data_list:
                        lines.append(f"cx q[{anc}], q[{qd}];")
                    lines.append(f"h q[{anc}];")
                lines.append(f"measure q[{anc}] -> c[{anc}];")

        for qd in range(n_data):
            lines.append(f"measure q[{qd}] -> c[{qd}];")

        return "\n".join(lines) + "\n"
