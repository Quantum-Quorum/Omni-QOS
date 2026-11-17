// OmniQ-OS Pre-Bundled Quantum Circuit: Rotated Surface Code Stabilizer Plaquette
// 4 Data Qubits q[0..3] + 1 Syndrome Ancilla q[4] detecting single-qubit X bit-flip
OPENQASM 3.0;
include "stdgates.inc";

qubit[5] q;
bit[5] c;

// Prepare logical GHZ-like stabilized plaquette state on data qubits q[0..3]
h q[0];
cx q[0], q[1];
cx q[0], q[2];
cx q[0], q[3];
barrier q[0];

// Inject physical Pauli-X bit-flip error on data qubit q[1]
x q[1];
barrier q[1];

// Weight-4 Z-stabilizer parity check fan-in onto syndrome ancilla q[4]
cx q[0], q[4];
cx q[1], q[4];
cx q[2], q[4];
cx q[3], q[4];

// Measure syndrome ancilla c[4] (excites to 1 when odd X-error parity occurs)
c[4] = measure q[4];
