OPENQASM 3.0;
include "stdgates.inc";

qubit[5] q;
bit[5] c;

h q[0];
cx q[0], q[1];
cx q[0], q[2];
cx q[0], q[3];
barrier q[0];

x q[1];
barrier q[1];

cx q[0], q[4];
cx q[1], q[4];
cx q[2], q[4];
cx q[3], q[4];

c[4] = measure q[4];
