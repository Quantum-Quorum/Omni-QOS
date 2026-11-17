# SPDX-License-Identifier: Apache-2.0
"""
Quantum Optimization: QAOA Combinatorial MaxCut & General VQE Wrapper.
"""

from typing import Dict, List, Sequence, Tuple
import numpy as np

from .circuit import Circuit, Statevector
from .chem import MolecularHamiltonian, VQESolver


class QAOA:
    """Quantum Approximate Optimization Algorithm for graph MaxCut."""

    def __init__(self, num_qubits: int, edges: Sequence[Tuple[int, int]], p: int = 1):
        self.num_qubits = int(num_qubits)
        self.edges = [(int(u), int(v)) for (u, v) in edges]
        self.p = max(1, int(p))

    def build_circuit(self, gammas: Sequence[float], betas: Sequence[float]) -> Circuit:
        c = Circuit(self.num_qubits)
        for q in range(self.num_qubits):
            c.h(q)
        for layer in range(self.p):
            gamma = float(gammas[layer])
            beta = float(betas[layer])
            for (u, v) in self.edges:
                c.cx(u, v).rz(v, 2.0 * gamma).cx(u, v)
            for q in range(self.num_qubits):
                c.rx(q, 2.0 * beta)
        return c

    def evaluate_cut(self, bitstring: str) -> int:
        # bitstring[-1-q] is qubit q
        return sum(1 for (u, v) in self.edges if bitstring[-1 - u] != bitstring[-1 - v])

    def optimize(self, grid_steps: int = 9) -> Dict[str, object]:
        best_val = -1.0
        best_gb = (0.4, 0.4)
        best_state: Statevector = Statevector.zero_state(self.num_qubits)
        for g in np.linspace(0.1, 1.4, grid_steps):
            for b in np.linspace(0.1, 1.4, grid_steps):
                sv = self.build_circuit([g] * self.p, [b] * self.p).execute()
                assert isinstance(sv, Statevector)
                probs = sv.probabilities()
                exp_cut = 0.0
                for idx, pr in enumerate(probs):
                    if pr > 1e-9:
                        bstr = format(idx, f"0{self.num_qubits}b")
                        exp_cut += pr * self.evaluate_cut(bstr)
                if exp_cut > best_val:
                    best_val = exp_cut
                    best_gb = (float(g), float(b))
                    best_state = sv
        top_idx = int(np.argmax(best_state.probabilities()))
        top_bitstring = format(top_idx, f"0{self.num_qubits}b")
        return {
            "expected_cut": best_val,
            "best_bitstring": top_bitstring,
            "best_cut_value": self.evaluate_cut(top_bitstring),
            "optimal_gamma_beta": best_gb,
        }


class VQE:
    """High-level VQE wrapper over MolecularHamiltonian."""

    def __init__(self, hamiltonian: MolecularHamiltonian = None, layers: int = 2):
        self.hamiltonian = hamiltonian or MolecularHamiltonian.h2(0.735)
        self.solver = VQESolver(self.hamiltonian, layers=layers)

    def solve(self, steps: int = 40) -> Dict[str, object]:
        return self.solver.solve(steps=steps)
