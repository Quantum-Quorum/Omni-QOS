OPENQASM 3.0;
include "stdgates.inc";

qubit[4] q;
bit[4] c;

x q[0];
x q[2];
barrier q[0];

h q[3];
cp(pi/2) q[2], q[3];
cp(pi/4) q[1], q[3];
cp(pi/8) q[0], q[3];

h q[2];
cp(pi/2) q[1], q[2];
cp(pi/4) q[0], q[2];

h q[1];
cp(pi/2) q[0], q[1];

h q[0];

swap q[0], q[3];
swap q[1], q[2];

c[0] = measure q[0];
c[1] = measure q[1];
c[2] = measure q[2];
c[3] = measure q[3];
