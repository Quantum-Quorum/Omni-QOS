OPENQASM 3.0;
include "stdgates.inc";

qubit[2] q;
bit[2] c;

x q[0];
ry(-0.2234) q[1];
cx q[1], q[0];

barrier q[0];
c[0] = measure q[0];
c[1] = measure q[1];
