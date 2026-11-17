# SPDX-License-Identifier: Apache-2.0
"""
OmniQ-OS: GUI Debugger Bridge
Connects OmniQ-OS circuits to the OmniQ 3D Bloch Sphere & Entanglement Debugger
"""

import os
import sys
import subprocess
import tempfile
from typing import Optional, Dict

class OmniQDebuggerBridge:
    """
    Bridge connecting QPU execution sessions to the interactive OmniQ GUI Debugger.
    """
    @staticmethod
    def find_debugger_binary() -> Optional[str]:
        candidates = [
            # Workspace build paths
            "/Users/gouthamarcot/Documents/personal/quantum/OmniQ/build/omniq-debugger/omniq-debugger",
            "/Users/gouthamarcot/Documents/personal/quantum/OmniQ/omniq-debugger/build/omniq-debugger",
            "/usr/local/bin/omniq-debugger",
            "/usr/bin/omniq-debugger",
        ]
        for p in candidates:
            if os.path.exists(p) and os.access(p, os.X_OK):
                return p
        return None

    @classmethod
    def launch(cls, qasm_code: str, counts: Optional[Dict[str, int]] = None, non_blocking: bool = True) -> bool:
        """
        Launches the OmniQ GUI Debugger with the provided OpenQASM circuit.
        """
        # Create temporary debug session file
        fd, temp_path = tempfile.mkstemp(suffix=".qasm", prefix="omniq_debug_")
        with os.fdopen(fd, "w") as f:
            f.write(qasm_code)

        debugger_bin = cls.find_debugger_binary()

        print(f"[OmniQ Debugger Bridge] Exported session to: {temp_path}")
        if debugger_bin:
            print(f"[OmniQ Debugger Bridge] Launching Native GUI Debugger: {debugger_bin}")
            args = [debugger_bin, temp_path]
            if non_blocking:
                subprocess.Popen(args, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
            else:
                subprocess.run(args)
            return True
        else:
            print(f"[OmniQ Debugger Bridge] omniq-debugger native binary not yet built. Session file ready at: {temp_path}")
            print(f"[OmniQ Debugger Bridge] You can open it via: omniq-debugger {temp_path}")
            return False

