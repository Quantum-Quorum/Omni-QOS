"""
OmniQ: Seamless Quantum Programming Across Python and C.
"""

__version__ = "2.0.0"
__author__ = "Quantum Quorum"
__email__ = "developers@omniq.org"

from .circuit import Circuit, Statevector, DensityMatrix
from .stabilizer import CliffordSimulator
from .lab import (
    SCPITransport,
    LakeShore372Driver,
    MicrowaveLODriver,
    BlueforsClient,
    ModbusTCPClient,
    CryoPLCController,
    QickProgramCompiler,
    QickRFSocClient,
    LabRackManager,
)
from .noise import NoiseModel
from .device import Device, SimulatorDevice, QPUDevice
from .qos import QPUClient
from .crypto import ShorsAlgorithm, GroversCrypto
from .qml import VariationalCircuit, QMLModel
from .optimization import QAOA, VQE
from .qec import SurfaceCodeLattice, build_bit_flip_code_circuit
from .chem import MolecularHamiltonian, VQESolver, compute_dissociation_curve
from .chaos import compute_otoc, loschmidt_echo, haar_random_state, haar_random_unitary
from .utils import (
    create_bell_state,
    create_ghz_state,
    measure_expectation,
    calculate_fidelity,
    random_circuit,
)
from . import debugger
debugger.add_debugger_to_circuit()
from .debugger import show_debugger, QuantumDebugger
from .magnon_logical import MagnonicGate, DualRailCompiler, create_dual_rail_grover

DEFAULT_DEVICE = "default.qubit"
SUPPORTED_DEVICES = ["default.qubit", "omniq.qpu0", "lightning.qubit", "ibmq.manila", "ionq.simulator"]
QuditStatevector = Statevector
DualRailDampingChannel = NoiseModel


def version_info():
    return {"version": __version__, "author": __author__, "email": __email__}


__all__ = [
    "Circuit", "Statevector", "DensityMatrix", "NoiseModel",
    "Device", "SimulatorDevice", "QPUDevice", "QPUClient",
    "ShorsAlgorithm", "GroversCrypto", "VariationalCircuit", "QMLModel",
    "QAOA", "VQE", "SurfaceCodeLattice", "build_bit_flip_code_circuit",
    "MolecularHamiltonian", "VQESolver", "compute_dissociation_curve",
    "compute_otoc", "loschmidt_echo", "haar_random_state", "haar_random_unitary",
    "create_bell_state", "create_ghz_state", "measure_expectation", "calculate_fidelity", "random_circuit",
    "MagnonicGate", "DualRailCompiler", "create_dual_rail_grover",
    "show_debugger", "QuantumDebugger", "QuditStatevector", "DualRailDampingChannel",
    "CliffordSimulator", "SCPITransport", "LakeShore372Driver", "MicrowaveLODriver",
    "BlueforsClient", "ModbusTCPClient", "CryoPLCController", "QickProgramCompiler",
    "QickRFSocClient", "LabRackManager",
    "version_info", "__version__",
]
