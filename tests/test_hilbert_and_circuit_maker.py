# SPDX-License-Identifier: Apache-2.0
"""Unit tests for Hilbert Space Explorer, Quantum Circuit Maker, QEC, Chem VQE & Chaos."""

import importlib.machinery
import importlib.util
import math
import os
import sys
import unittest
import numpy as np

REPO_ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), ".."))
for p in [os.path.join(REPO_ROOT, "runtime", "python"), os.path.join(REPO_ROOT, "src")]:
    if p not in sys.path:
        sys.path.insert(0, p)

from omniq import (
    Circuit,
    Statevector,
    DensityMatrix,
    NoiseModel,
    SurfaceCodeLattice,
    MolecularHamiltonian,
    VQESolver,
    compute_otoc,
    loschmidt_echo,
    ShorsAlgorithm,
    GroversCrypto,
    QAOA,
    QMLModel,
)


def _load_script_module(name: str, rel_path: str):
    full = os.path.join(REPO_ROOT, rel_path)
    loader = importlib.machinery.SourceFileLoader(name, full)
    spec = importlib.util.spec_from_loader(name, loader)
    mod = importlib.util.module_from_spec(spec)
    sys.modules[name] = mod
    loader.exec_module(mod)
    return mod


class TestHilbertAndCircuitMaker(unittest.TestCase):
    def test_01_hilbert_space_presets_and_qsphere_geometry(self):
        hs = _load_script_module("hilbert_mod", "bin/omniq-hilbert-space")
        for preset in ["bell_phi_plus", "singlet_psi_minus", "ghz", "w_state", "qft_spiral", "cluster_graph", "haar_random"]:
            sv, _ = hs.build_preset_state(preset, 4)
            self.assertEqual(sv.num_qubits, 4)
            self.assertAlmostEqual(float(np.sum(sv.probabilities())), 1.0, places=7)
        coords = hs.compute_qsphere_coordinates(4)
        self.assertEqual(len(coords), 16)
        self.assertAlmostEqual(coords[0][4], 1.0, places=6)   # |0000> North pole
        self.assertAlmostEqual(coords[15][4], -1.0, places=6) # |1111> South pole

    def test_02_partial_trace_bloch_and_schmidt_spectrum(self):
        bell = Circuit(2).h(0).cx(0, 1).execute()
        assert isinstance(bell, Statevector)
        bx, by, bz, pur = bell.bloch_vector(0)
        self.assertAlmostEqual(bx, 0.0, places=6)
        self.assertAlmostEqual(bz, 0.0, places=6)
        self.assertAlmostEqual(pur, 0.5, places=6)
        schmidt = bell.schmidt_coefficients()
        self.assertEqual(len(schmidt), 2)
        self.assertAlmostEqual(float(schmidt[0]), 1.0 / math.sqrt(2.0), places=5)

    def test_03_circuit_maker_grid_model_and_templates(self):
        cm = _load_script_module("cm_mod", "bin/omniq-circuit-maker")
        grid = cm.CircuitGridModel(3, 12)
        grid.place(0, "H", [0])
        grid.place(1, "CNOT", [0, 1])
        grid.place(2, "CCX", [0, 1, 2])
        sv = grid.to_circuit().execute()
        probs = sv.probabilities()
        self.assertAlmostEqual(float(probs[0] + probs[7]), 1.0, places=6)

        grover_sv = cm.build_template_circuit("grover3").execute()
        self.assertGreater(float(grover_sv.probabilities()[7]), 0.75)

    def test_04_qasm3_roundtrip_and_framework_transpilers(self):
        c = Circuit(3).h(0).rx(1, 0.75).cx(0, 1).cz(1, 2).measure_all()
        qasm3 = c.to_qasm("3.0")
        c_parsed = Circuit.from_qasm(qasm3)
        sv1 = c.execute()
        sv2 = c_parsed.execute()
        overlap = abs(np.vdot(sv1.data, sv2.data)) ** 2
        self.assertAlmostEqual(float(overlap), 1.0, places=6)
        self.assertIn("QuantumCircuit", c.to_qiskit_code())
        self.assertIn("cirq.Circuit", c.to_cirq_code())
        self.assertIn("OmniQPennyLaneDevice", c.to_pennylane_code())

    def test_05_surface_code_mwpm_and_vqe_chemistry(self):
        sc = SurfaceCodeLattice(distance=5)
        sc.inject_pauli(2, 2, "Y")
        dec = sc.decode_mwpm()
        self.assertEqual(dec["active_defects"], 4)
        self.assertTrue(sc.apply_corrections(dec))

        res = VQESolver(MolecularHamiltonian.h2(0.735), layers=1).solve(steps=25)
        self.assertLess(res["chemical_accuracy_mHa"], 1.0)

    def test_06_quantum_chaos_crypto_qaoa_and_qml(self):
        otoc = compute_otoc(3, [0.0, 0.5, 1.0])
        self.assertAlmostEqual(otoc["otoc"][0], 1.0, places=5)
        echo = loschmidt_echo(3, 0.1, [0.0, 1.0])
        self.assertAlmostEqual(echo["echo"][0], 1.0, places=5)
        self.assertIn(ShorsAlgorithm(15).factor(), ((3, 5), (5, 3)))
        self.assertEqual(GroversCrypto(4).search(13), "1101")
        qaoa = QAOA(4, [(0, 1), (1, 2), (2, 3), (3, 0)], p=1).optimize(grid_steps=5)
        self.assertEqual(qaoa["best_cut_value"], 4)
        qml = QMLModel(2, 1)
        losses = qml.fit([[0.1, 0.2], [0.9, 0.8]], [1.0, -1.0], epochs=3)
        self.assertEqual(len(losses), 3)


if __name__ == "__main__":
    unittest.main()
