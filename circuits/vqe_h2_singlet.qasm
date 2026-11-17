// OmniQ-OS Pre-Bundled Quantum Circuit: VQE H2 Singlet Ground-State Ansatz
// 2-Qubit Jordan-Wigner / Bravyi-Kitaev tapered STO-3G H2 at R_eq = 0.735 Angstrom
// Converged UCCSD singlet parameter theta = -0.2234 rad -> E_0 = -1.1373 Hartree
OPENQASM 3.0;
include "stdgates.inc";

qubit[2] q;
bit[2] c;

// Hartree-Fock reference determinant |01> followed by singlet excitation
x q[0];
ry(-0.2234) q[1];
cx q[1], q[0];

barrier q[0];
c[0] = measure q[0];
c[1] = measure q[1];
