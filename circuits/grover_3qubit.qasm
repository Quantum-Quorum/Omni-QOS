// OmniQ-OS Pre-Bundled Quantum Circuit: 3-Qubit Grover Search (Target |101>)
// Two amplitude-amplification iterations boost P(|101>) to 94.53%
OPENQASM 3.0;
include "stdgates.inc";

qubit[3] q;
bit[3] c;

// Uniform superposition |+++>
h q[0];
h q[1];
h q[2];

// --- Grover Iteration 1 ---
// Phase Oracle marking |101> (q[2]=1, q[1]=0, q[0]=1)
x q[1];
h q[2];
ccx q[0], q[1], q[2];
h q[2];
x q[1];

// Inversion about the mean (Diffuser)
h q[0];
h q[1];
h q[2];
x q[0];
x q[1];
x q[2];
h q[2];
ccx q[0], q[1], q[2];
h q[2];
x q[0];
x q[1];
x q[2];
h q[0];
h q[1];
h q[2];

// --- Grover Iteration 2 ---
x q[1];
h q[2];
ccx q[0], q[1], q[2];
h q[2];
x q[1];

h q[0];
h q[1];
h q[2];
x q[0];
x q[1];
x q[2];
h q[2];
ccx q[0], q[1], q[2];
h q[2];
x q[0];
x q[1];
x q[2];
h q[0];
h q[1];
h q[2];

c[0] = measure q[0];
c[1] = measure q[1];
c[2] = measure q[2];
