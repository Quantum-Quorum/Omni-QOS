import math
from typing import Optional, Tuple
from .algorithms import GroversAlgorithm, create_database_oracle

class ShorsAlgorithm:

    def __init__(self, N: int):
        self.N = int(N)

    def factor(self) -> Optional[Tuple[int, int]]:
        N = self.N
        if N < 4:
            return None
        if N % 2 == 0:
            return (2, N // 2)
        for a in range(2, N):
            g = math.gcd(a, N)
            if g > 1:
                return (g, N // g)
            r = 1
            val = a % N
            while val != 1 and r <= N:
                val = (val * a) % N
                r += 1
            if r % 2 == 0:
                x = pow(a, r // 2, N)
                if x != N - 1:
                    f1 = math.gcd(x - 1, N)
                    f2 = math.gcd(x + 1, N)
                    if 1 < f1 < N:
                        return (f1, N // f1)
                    if 1 < f2 < N:
                        return (f2, N // f2)
        return None

class GroversCrypto:

    def __init__(self, key_bits: int = 4):
        self.key_bits = int(key_bits)

    def search(self, target_key: int) -> str:
        grover = GroversAlgorithm(self.key_bits, create_database_oracle(target_key))
        return grover.top_solution()
