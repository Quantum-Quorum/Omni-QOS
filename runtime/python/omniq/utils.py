import numpy as np
from typing import List, Optional
from .circuit import Circuit, Statevector, DensityMatrix

def create_bell_state() -> Statevector:
    circuit = Circuit(2)
    circuit.add_gate('H', [0])
    circuit.add_gate('CNOT', [0, 1])
    return circuit.execute()

def create_ghz_state(num_qubits: int) -> Statevector:
    circuit = Circuit(num_qubits)
    circuit.add_gate('H', [0])
    for i in range(1, num_qubits):
        circuit.add_gate('CNOT', [0, i])
    return circuit.execute()

def measure_expectation(state: Statevector, qubit: int, observable: str = "Z") -> float:
    return state.measure_expectation(qubit, observable)

def calculate_fidelity(state1: Statevector, state2: Statevector) -> float:
    amps1 = state1.get_amplitudes()
    amps2 = state2.get_amplitudes()

    overlap = np.sum(np.conj(amps1) * amps2)
    fidelity = np.abs(overlap) ** 2

    return float(fidelity)

def random_circuit(num_qubits: int, depth: int, seed: Optional[int] = None) -> Circuit:
    if seed is not None:
        np.random.seed(seed)

    circuit = Circuit(num_qubits)
    gates = ['H', 'X', 'Y', 'Z', 'RX', 'RY', 'RZ']

    for layer in range(depth):
        for qubit in range(num_qubits):
            gate = np.random.choice(gates)
            if gate in ['RX', 'RY', 'RZ']:
                angle = np.random.uniform(0, 2 * np.pi)
                circuit.add_gate(gate, [qubit], angle=angle)
            else:
                circuit.add_gate(gate, [qubit])

        if num_qubits > 1:
            for _ in range(num_qubits // 2):
                qubit1 = np.random.randint(0, num_qubits)
                qubit2 = np.random.randint(0, num_qubits)
                if qubit1 != qubit2:
                    circuit.add_gate('CNOT', [qubit1, qubit2])

    return circuit

def create_quantum_state_from_amplitudes(amplitudes: np.ndarray) -> Statevector:
    return Statevector(amplitudes=amplitudes)

def create_mixed_state_from_matrix(matrix: np.ndarray) -> DensityMatrix:
    return DensityMatrix(matrix=matrix)
