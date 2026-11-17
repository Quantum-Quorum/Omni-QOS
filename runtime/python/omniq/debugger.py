import json
import os
import subprocess
import tempfile
from pathlib import Path
from typing import Any, Optional

class QuantumDebugger:
    def __init__(self, circuit: Any = None):
        self.circuit = circuit
        self.debugger_path = self._find_debugger()

    @staticmethod
    def _find_debugger() -> Optional[str]:
        repo_bin = Path(__file__).resolve().parents[3] / "bin" / "omniq-debugger"
        possible_paths = [
            repo_bin,
            Path("/usr/local/bin/omniq-debugger"),
            Path("/usr/bin/omniq-debugger"),
            Path.home() / ".local" / "bin" / "omniq-debugger",
        ]
        for path in possible_paths:
            if path.exists() and os.access(path, os.X_OK):
                return str(path)
        return None

    def show(self, circuit: Any = None, noise_model: Any = None, view_mode: Optional[str] = None) -> str:
        if circuit is not None:
            self.circuit = circuit

        temp_file = tempfile.NamedTemporaryFile(suffix=".json", prefix="omniq_circuit_", delete=False)
        temp_path = temp_file.name
        temp_file.close()

        data = self.circuit.to_dict() if self.circuit is not None and hasattr(self.circuit, "to_dict") else {}
        if self.circuit is not None and hasattr(self.circuit, "to_qasm"):
            data["qasm"] = self.circuit.to_qasm("3.0")
        if noise_model is not None and hasattr(noise_model, "to_dict"):
            data["noise_model"] = noise_model.to_dict()
        if view_mode:
            data["initial_view"] = view_mode

        with open(temp_path, "w") as f:
            json.dump(data, f, indent=2)

        if self.debugger_path and os.environ.get("DISPLAY"):
            subprocess.Popen([self.debugger_path, temp_path], stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        return temp_path

    def debug(self, circuit: Any = None, noise_model: Any = None, view_mode: Optional[str] = None) -> str:
        return self.show(circuit=circuit, noise_model=noise_model, view_mode=view_mode)

    def inspect(self, circuit: Any = None, view_mode: Optional[str] = None) -> str:
        return self.show(circuit=circuit, view_mode=view_mode)

def show_debugger(circuit: Any = None, noise_model: Any = None, view_mode: Optional[str] = None) -> str:
    debugger = QuantumDebugger(circuit)
    return debugger.show(noise_model=noise_model, view_mode=view_mode)

def add_debugger_to_circuit() -> None:
    try:
        from .circuit import Circuit

        def _debug(self, noise_model=None, view_mode=None):
            return show_debugger(self, noise_model=noise_model, view_mode=view_mode)

        def _show(self, view_mode=None):
            return _debug(self, view_mode=view_mode)

        Circuit.debug = _debug
        Circuit.show = _show
    except ImportError:
        pass
