// OmniQ-OS Pre-Bundled Quantum Circuit: Bell Pair |Phi+>
// Maximally entangled 2-qubit EPR state (|00> + |11>) / sqrt(2)
OPENQASM 3.0;
include "stdgates.inc";

qubit[2] q;
bit[2] c;

h q[0];
cx q[0], q[1];

barrier q[0];
c[0] = measure q[0];
c[1] = measure q[1];
