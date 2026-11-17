import math
from typing import List, Sequence
import numpy as np
from .variational_circuit import VariationalCircuit

class QMLModel:
    def __init__(self, num_qubits: int = 2, num_layers: int = 2):
        self.vqc = VariationalCircuit(num_qubits=num_qubits, num_layers=num_layers)

    def predict_raw(self, x: Sequence[float]) -> float:
        return self.vqc.forward(x)

    def predict(self, X: Sequence[Sequence[float]]) -> List[int]:
        return [1 if self.predict_raw(x) >= 0.0 else -1 for x in X]

    def fit(self, X: Sequence[Sequence[float]], y: Sequence[float], epochs: int = 12, lr: float = 0.25) -> List[float]:
        losses = []
        shift = math.pi / 2.0
        for _ in range(epochs):
            preds = np.array([self.vqc.forward(x) for x in X])
            targets = np.asarray(y, dtype=float)
            loss = float(np.mean((preds - targets) ** 2))
            losses.append(loss)

            grad = np.zeros_like(self.vqc.weights)
            it = np.nditer(self.vqc.weights, flags=["multi_index"])
            while not it.finished:
                idx = it.multi_index
                wp = self.vqc.weights.copy()
                wm = self.vqc.weights.copy()
                wp[idx] += shift
                wm[idx] -= shift
                g_val = 0.0
                for xi, yi in zip(X, targets):
                    fp = self.vqc.forward(xi, wp)
                    fm = self.vqc.forward(xi, wm)
                    df = 0.5 * (fp - fm)
                    g_val += 2.0 * (self.vqc.forward(xi) - yi) * df / len(X)
                grad[idx] = g_val
                it.iternext()
            self.vqc.weights -= lr * grad
        return losses

    def evaluate(self, X: Sequence[Sequence[float]], y: Sequence[int]) -> float:
        preds = self.predict(X)
        return sum(1 for p, t in zip(preds, y) if p == t) / max(1, len(y))
