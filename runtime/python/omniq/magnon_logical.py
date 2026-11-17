from .circuit import Circuit
import math

class MagnonicGate:
    BEAM_SPLITTER_5050 = "BS_5050"
    TEMPORAL_BEAM_SPLITTER_100 = "TBS_100"
    CROSS_KERR = "CROSS_KERR"
    PHASE_SHIFT = "PHASE_SHIFT"

class DualRailCompiler:

    @staticmethod
    def translate_gate(gate_type, *qubits, **kwargs):
        if gate_type == MagnonicGate.BEAM_SPLITTER_5050:
            return ('H', qubits[0])
        elif gate_type == MagnonicGate.TEMPORAL_BEAM_SPLITTER_100:
            return ('X', qubits[0])
        elif gate_type == MagnonicGate.CROSS_KERR:
            control, target = qubits[0], qubits[1]
            return ('CP', control, target, math.pi)
        elif gate_type == MagnonicGate.PHASE_SHIFT:
            angle = kwargs.get('angle', 0.0)
            return ('RZ', qubits[0], angle)
        else:
            raise ValueError(f"Unknown magnonic gate type: {gate_type}")

    @staticmethod
    def build_logical_circuit(num_logical_qubits, magnonic_operations):
        circuit = Circuit(num_logical_qubits)

        for op in magnonic_operations:
            gate_type = op[0]
            args = op[1:]
            kwargs = {}
            if args and isinstance(args[-1], dict):
                kwargs = args[-1]
                args = args[:-1]

            translated = DualRailCompiler.translate_gate(gate_type, *args, **kwargs)
            g_name = translated[0]

            if g_name == 'H':
                circuit.h(translated[1])
            elif g_name == 'X':
                circuit.x(translated[1])
            elif g_name == 'CP':
                circuit.cp(translated[3], translated[1], translated[2])
            elif g_name == 'RZ':
                circuit.rz(translated[1], translated[2])

        return circuit

def create_dual_rail_grover(num_qubits, target_value):
    ops = []

    for q in range(num_qubits):
        ops.append((MagnonicGate.BEAM_SPLITTER_5050, q))

    for q in range(num_qubits):
        if not ((target_value >> q) & 1):
            ops.append((MagnonicGate.TEMPORAL_BEAM_SPLITTER_100, q))

    if num_qubits == 2:
        ops.append((MagnonicGate.CROSS_KERR, 0, 1))
    elif num_qubits == 1:
        ops.append((MagnonicGate.PHASE_SHIFT, 0, {'angle': math.pi}))
    else:
        for i in range(num_qubits - 1):
            ops.append((MagnonicGate.CROSS_KERR, i, i + 1))

    for q in range(num_qubits):
        if not ((target_value >> q) & 1):
            ops.append((MagnonicGate.TEMPORAL_BEAM_SPLITTER_100, q))

    for q in range(num_qubits):
        ops.append((MagnonicGate.BEAM_SPLITTER_5050, q))
        ops.append((MagnonicGate.TEMPORAL_BEAM_SPLITTER_100, q))

    if num_qubits == 2:
        ops.append((MagnonicGate.CROSS_KERR, 0, 1))
    elif num_qubits == 1:
        ops.append((MagnonicGate.PHASE_SHIFT, 0, {'angle': math.pi}))
    else:
        for i in range(num_qubits - 1):
            ops.append((MagnonicGate.CROSS_KERR, i, i + 1))

    for q in range(num_qubits):
        ops.append((MagnonicGate.TEMPORAL_BEAM_SPLITTER_100, q))
        ops.append((MagnonicGate.BEAM_SPLITTER_5050, q))

    return DualRailCompiler.build_logical_circuit(num_qubits, ops)
