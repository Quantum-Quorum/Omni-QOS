# OmniQ-OS Python SDK (`omniqos`)

Python SDK, framework adapters, and interactive quantum assembly studio for **OmniQ-OS: Real-Time Quantum Linux**.

## Features
* **Universal Hardware Interface**: Direct access to `/dev/accel/qpu0` with transparent simulation fallback.
* **Framework Adapters**:
  * **Qiskit**: `omniqos.qiskit.OmniQBackend`
  * **Cirq**: `omniqos.cirq.OmniQDevice` & `OmniQSampler`
  * **PennyLane**: `omniqos.pennylane.OmniQPennyLaneDevice`
  * **Amazon Braket**: `omniqos.braket.OmniQBraketDevice`
* **Quantum Assembly Studio (`omniq-studio`)**:
  * Interactive OpenQASM 3.0 editor & compiler
  * Raw pulse schedule inspector
  * Live histogram visualizer
  * One-click bridge to the OmniQ 3D GUI Debugger

