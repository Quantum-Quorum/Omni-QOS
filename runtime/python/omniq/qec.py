from __future__ import annotations

from dataclasses import dataclass, field
from typing import Dict, List, Optional, Set, Tuple
import numpy as np

from .circuit import Circuit

def build_bit_flip_code_circuit(logical_one: bool = False, error_qubit: Optional[int] = None) -> Circuit:
    c = Circuit(5)
    if logical_one:
        c.x(0)
    c.cx(0, 1).cx(0, 2)
    if error_qubit is not None and 0 <= error_qubit <= 2:
        c.x(error_qubit)
    c.cx(0, 3).cx(1, 3)
    c.cx(1, 4).cx(2, 4)
    return c

@dataclass
class StabilizerPlaquette:
    plaquette_id: int
    kind: str
    row: int
    col: int
    data_qubits: List[Tuple[int, int]]

@dataclass
class SurfaceCodeLattice:
    distance: int = 5
    x_errors: Set[Tuple[int, int]] = field(default_factory=set)
    z_errors: Set[Tuple[int, int]] = field(default_factory=set)
    plaquettes: List[StabilizerPlaquette] = field(default_factory=list)

    def __post_init__(self):
        if self.distance < 3 or self.distance % 2 == 0:
            raise ValueError("Surface code distance must be an odd integer >= 3")
        self._build_plaquettes()

    def _build_plaquettes(self) -> None:
        self.plaquettes.clear()
        d = self.distance
        pid = 0
        for r in range(d - 1):
            for c in range(d - 1):
                kind = "Z" if (r + c) % 2 == 0 else "X"
                dqs = [(r, c), (r, c + 1), (r + 1, c), (r + 1, c + 1)]
                self.plaquettes.append(StabilizerPlaquette(pid, kind, r, c, dqs))
                pid += 1

    def clear_errors(self) -> None:
        self.x_errors.clear()
        self.z_errors.clear()

    def inject_pauli(self, r: int, c: int, pauli: str = "X") -> None:
        p = pauli.upper()
        coord = (int(r), int(c))
        if p in ("X", "Y"):
            if coord in self.x_errors:
                self.x_errors.remove(coord)
            else:
                self.x_errors.add(coord)
        if p in ("Z", "Y"):
            if coord in self.z_errors:
                self.z_errors.remove(coord)
            else:
                self.z_errors.add(coord)

    def inject_random_noise(self, p_phys: float = 0.03, seed: Optional[int] = None) -> None:
        rng = np.random.default_rng(seed)
        self.clear_errors()
        for r in range(self.distance):
            for c in range(self.distance):
                u = rng.random()
                if u < p_phys / 3.0:
                    self.inject_pauli(r, c, "X")
                elif u < 2.0 * p_phys / 3.0:
                    self.inject_pauli(r, c, "Z")
                elif u < p_phys:
                    self.inject_pauli(r, c, "Y")

    def extract_syndromes(self) -> Dict[int, int]:
        syn: Dict[int, int] = {}
        for pl in self.plaquettes:
            parity = 0
            err_set = self.x_errors if pl.kind == "Z" else self.z_errors
            for dq in pl.data_qubits:
                if dq in err_set:
                    parity ^= 1
            syn[pl.plaquette_id] = parity
        return syn

    def decode_mwpm(self) -> Dict[str, object]:
        syn = self.extract_syndromes()
        z_defects = [pl for pl in self.plaquettes if pl.kind == "Z" and syn[pl.plaquette_id] == 1]
        x_defects = [pl for pl in self.plaquettes if pl.kind == "X" and syn[pl.plaquette_id] == 1]

        matches: List[Tuple[Tuple[float, float], Tuple[float, float], str]] = []
        corrections_x: List[Tuple[int, int]] = []
        corrections_z: List[Tuple[int, int]] = []

        def match_group(defects: List[StabilizerPlaquette], kind: str):
            unmatched = list(defects)
            d = self.distance
            while unmatched:
                u = unmatched.pop(0)
                ux, uy = u.col + 0.5, u.row + 0.5
                if kind == "Z":
                    b_dist = min(u.col + 1, d - 1 - u.col)
                    bx = 0.0 if (u.col + 1) <= (d - 1 - u.col) else float(d - 1)
                    by = uy
                else:
                    b_dist = min(u.row + 1, d - 1 - u.row)
                    bx = ux
                    by = 0.0 if (u.row + 1) <= (d - 1 - u.row) else float(d - 1)

                best_j = -1
                best_d = float(b_dist) + 0.25
                for j, v in enumerate(unmatched):
                    md = abs(u.row - v.row) + abs(u.col - v.col)
                    if md < best_d:
                        best_d = float(md)
                        best_j = j

                if best_j >= 0:
                    v = unmatched.pop(best_j)
                    vx, vy = v.col + 0.5, v.row + 0.5
                    matches.append(((ux, uy), (vx, vy), kind))
                    r_cur, c_cur = u.row, u.col
                    while r_cur != v.row:
                        step_r = max(r_cur, r_cur + (1 if v.row > r_cur else -1))
                        (corrections_x if kind == "Z" else corrections_z).append((step_r, c_cur + 1))
                        r_cur += 1 if v.row > r_cur else -1
                    while c_cur != v.col:
                        step_c = max(c_cur, c_cur + (1 if v.col > c_cur else -1))
                        (corrections_x if kind == "Z" else corrections_z).append((r_cur + 1, step_c))
                        c_cur += 1 if v.col > c_cur else -1
                else:
                    matches.append(((ux, uy), (bx, by), kind))
                    if kind == "Z":
                        if bx == 0.0:
                            for cc in range(u.col + 1):
                                corrections_x.append((u.row + 1 if u.row + 1 < d else u.row, cc))
                        else:
                            for cc in range(u.col + 1, d):
                                corrections_x.append((u.row, cc))
                    else:
                        if by == 0.0:
                            for rr in range(u.row + 1):
                                corrections_z.append((rr, u.col + 1 if u.col + 1 < d else u.col))
                        else:
                            for rr in range(u.row + 1, d):
                                corrections_z.append((rr, u.col))

        match_group(z_defects, "Z")
        match_group(x_defects, "X")

        return {
            "syndromes": syn,
            "active_defects": len(z_defects) + len(x_defects),
            "matches": matches,
            "corrections_x": corrections_x,
            "corrections_z": corrections_z,
            "decoder_latency_ns": 485 + 6 * (len(z_defects) + len(x_defects)),
        }

    def apply_corrections(self, decoded: Dict[str, object]) -> bool:
        for r, c in decoded.get("corrections_x", []):
            if 0 <= r < self.distance and 0 <= c < self.distance:
                self.inject_pauli(r, c, "X")
        for r, c in decoded.get("corrections_z", []):
            if 0 <= r < self.distance and 0 <= c < self.distance:
                self.inject_pauli(r, c, "Z")
        log_x = sum(1 for (r, c) in self.x_errors if r == 0) % 2
        log_z = sum(1 for (r, c) in self.z_errors if c == 0) % 2
        return (log_x == 0) and (log_z == 0)
