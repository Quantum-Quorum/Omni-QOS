OPENQASM 3.0;
include "stdgates.inc";

qubit[3] q;
bit[3] c;

ry(pi/3) q[0];
rz(pi/4) q[0];
barrier q[0];

h q[1];
cx q[1], q[2];
barrier q[1];

cx q[0], q[1];
h q[0];
barrier q[2];

cx q[1], q[2];
cz q[0], q[2];

c[0] = measure q[0];
c[1] = measure q[1];
c[2] = measure q[2];
