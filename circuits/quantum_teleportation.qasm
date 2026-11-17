// OmniQ-OS Pre-Bundled Quantum Circuit: Coherent Quantum Teleportation
// Teleports arbitrary Bloch state on q[0] across shared EPR pair (q[1], q[2]) to q[2]
OPENQASM 3.0;
include "stdgates.inc";

qubit[3] q;
bit[3] c;

// 1. Prepare payload quantum state |psi> on Alice qubit q[0]
ry(pi/3) q[0];
rz(pi/4) q[0];
barrier q[0];

// 2. Establish shared Bell resource |Phi+> between Alice q[1] and Bob q[2]
h q[1];
cx q[1], q[2];
barrier q[1];

// 3. Bell-basis joint rotation on Alice register (q[0], q[1])
cx q[0], q[1];
h q[0];
barrier q[2];

// 4. Coherent deferred-measurement Pauli frame feed-forward onto Bob q[2]
cx q[1], q[2];
cz q[0], q[2];

c[0] = measure q[0];
c[1] = measure q[1];
c[2] = measure q[2];
