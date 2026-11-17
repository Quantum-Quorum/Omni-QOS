# SPDX-License-Identifier: Apache-2.0
"""
OmniQ Quantum Chaos, Information Scrambling & Thermalization Diagnostics:
- Out-of-Time-Ordered Correlator (OTOC) C(t) = <W^dag(t) V^dag W(t) V>
- Loschmidt Echo L(t) = |<psi_0 | e^{i(H+dH)t} e^{-iHt} | psi_0>|^2
- Spectral Form Factor K(t) = |Tr(e^{-iHt})|^2 / d^2
- Haar Random State & Unitary Generation
"""

from __future__ import annotations

from typing import Dict, List, Optional
import numpy as np
from scipy.linalg import expm

from .circuit import Statevector


def haar_random_unitary(dim: int, seed: Optional[int] = None) -> np.ndarray:
    rng = np.random.default_rng(seed)
    z = (rng.standard_normal((dim, dim)) + 1j * rng.standard_normal((dim, dim))) / np.sqrt(2.0)
    q, r = np.linalg.qr(z)
    d = np.diag(r)
    ph = d / np.where(np.abs(d) > 1e-15, np.abs(d), 1.0)
    return q * ph


def haar_random_state(num_qubits: int, seed: Optional[int] = None) -> Statevector:
    rng = np.random.default_rng(seed)
    dim = 1 << num_qubits
    vec = rng.standard_normal(dim) + 1j * rng.standard_normal(dim)
    return Statevector(vec, normalize=True)


def chaotic_ising_hamiltonian(num_qubits: int, j_zz: float = 1.0, hx: float = 1.05, hz: float = 0.5) -> np.ndarray:
    """Non-integrable mixed-field Ising Hamiltonian H = sum J Z_i Z_{i+1} + hx X_i + hz Z_i."""
    n = max(2, int(num_qubits))
    dim = 1 << n
    I = np.eye(2, dtype=np.complex128)
    X = np.array([[0, 1], [1, 0]], dtype=np.complex128)
    Z = np.array([[1, 0], [0, -1]], dtype=np.complex128)

    def single_op(op: np.ndarray, site: int) -> np.ndarray:
        res = np.array([[1.0 + 0.0j]])
        for k in range(n):
            res = np.kron(res, op if k == site else I)
        return res

    H = np.zeros((dim, dim), dtype=np.complex128)
    for i in range(n):
        H += hx * single_op(X, i) + hz * single_op(Z, i)
    for i in range(n - 1):
        H += j_zz * (single_op(Z, i) @ single_op(Z, i + 1))
    return H


def compute_otoc(
    num_qubits: int = 4,
    times: Optional[List[float]] = None,
    qubit_v: int = 0,
    qubit_w: Optional[int] = None,
) -> Dict[str, List[float]]:
    """Computes infinite-temperature OTOC Re[Tr(W^dag(t) V^dag W(t) V)] / 2^n."""
    n = max(2, int(num_qubits))
    if qubit_w is None:
        qubit_w = n - 1
    if times is None:
        times = np.linspace(0.0, 6.0, 25).tolist()

    I = np.eye(2, dtype=np.complex128)
    Z = np.array([[1, 0], [0, -1]], dtype=np.complex128)
    X = np.array([[0, 1], [1, 0]], dtype=np.complex128)

    def local_pauli(pauli: np.ndarray, q: int) -> np.ndarray:
        res = np.array([[1.0 + 0.0j]])
        for k in range(n):
            res = np.kron(res, pauli if k == q else I)
        return res

    H = chaotic_ising_hamiltonian(n)
    V = local_pauli(Z, qubit_v)
    W0 = local_pauli(X, qubit_w)
    dim = 1 << n

    eigvals, eigvecs = np.linalg.eigh(H)
    otoc_vals: List[float] = []
    for t in times:
        phases = np.exp(-1j * eigvals * float(t))
        U = (eigvecs * phases) @ eigvecs.conj().T
        Wt = U.conj().T @ W0 @ U
        val = np.real(np.trace(Wt.conj().T @ V.conj().T @ Wt @ V)) / float(dim)
        otoc_vals.append(float(val))
    return {"times": list(times), "otoc": otoc_vals}


def loschmidt_echo(
    num_qubits: int = 4,
    perturbation: float = 0.15,
    times: Optional[List[float]] = None,
) -> Dict[str, List[float]]:
    n = max(2, int(num_qubits))
    if times is None:
        times = np.linspace(0.0, 8.0, 30).tolist()
    H0 = chaotic_ising_hamiltonian(n, hx=1.05, hz=0.50)
    H1 = chaotic_ising_hamiltonian(n, hx=1.05 + perturbation, hz=0.50 - 0.5 * perturbation)
    psi0 = Statevector.zero_state(n).data
    echoes: List[float] = []
    for t in times:
        u0 = expm(-1j * H0 * float(t))
        u1 = expm(-1j * H1 * float(t))
        overlap = np.vdot(u0 @ psi0, u1 @ psi0)
        echoes.append(float(np.abs(overlap) ** 2))
    return {"times": list(times), "echo": echoes}


def spectral_form_factor(hamiltonian: np.ndarray, times: List[float]) -> List[float]:
    eigvals = np.linalg.eigvalsh(hamiltonian)
    dim = float(len(eigvals))
    return [float((np.abs(np.sum(np.exp(-1j * eigvals * t))) ** 2) / (dim * dim)) for t in times]
