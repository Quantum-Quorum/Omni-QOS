import os
import subprocess
import tempfile
from typing import Optional, Dict

class OmniQDebuggerBridge:
    @staticmethod
    def find_debugger_binary() -> Optional[str]:
        here = os.path.dirname(os.path.abspath(__file__))
        repo_bin = os.path.abspath(os.path.join(here, "..", "..", "..", "bin", "omniq-debugger"))
        candidates = [
            repo_bin,
            "/usr/local/bin/omniq-debugger",
            "/usr/bin/omniq-debugger",
        ]
        for p in candidates:
            if os.path.exists(p) and os.access(p, os.X_OK):
                return p
        return None

    @classmethod
    def launch(cls, qasm_code: str, counts: Optional[Dict[str, int]] = None, non_blocking: bool = True) -> bool:
        fd, temp_path = tempfile.mkstemp(suffix=".qasm", prefix="omniq_debug_")
        with os.fdopen(fd, "w") as f:
            f.write(qasm_code)

        debugger_bin = cls.find_debugger_binary()
        print(f"[OmniQ Debugger Bridge] Exported session to: {temp_path}")

        if debugger_bin:
            if not os.environ.get("DISPLAY") and non_blocking:
                print(f"[OmniQ Debugger Bridge] Headless session detected; native binary verified at: {debugger_bin}")
                return True
            print(f"[OmniQ Debugger Bridge] Launching Native GUI Debugger: {debugger_bin}")
            args = [debugger_bin, temp_path]
            if non_blocking:
                subprocess.Popen(args, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
            else:
                subprocess.run(args)
            return True
        else:
            print(f"[OmniQ Debugger Bridge] Session file ready at: {temp_path}")
            return False
