# SPDX-License-Identifier: Apache-2.0
"""
OmniQ-OS v2.0: Python ctypes bridge to C++20 libomniq_core.so and libomniq_secret.so
"""

import ctypes
import json
import os
from typing import Any, Dict, Optional


class OmniqCoreBridge:
    _instance = None

    def __new__(cls):
        if cls._instance is None:
            cls._instance = super().__new__(cls)
            cls._instance._initialized = False
        return cls._instance

    def __init__(self):
        if self._initialized:
            return
        self._initialized = True
        self.core_lib = self._load_lib("libomniq_core.so")
        self.secret_lib = self._load_lib("libomniq_secret.so")

        if self.core_lib is not None:
            self.core_lib.omniq_core_init.restype = ctypes.c_int
            self.core_lib.omniq_core_init.argtypes = []

            self.core_lib.omniq_core_get_status_json.restype = ctypes.c_int
            self.core_lib.omniq_core_get_status_json.argtypes = [ctypes.c_char_p, ctypes.c_size_t]

            self.core_lib.omniq_core_run_benchmarks_json.restype = ctypes.c_int
            self.core_lib.omniq_core_run_benchmarks_json.argtypes = [ctypes.c_char_p, ctypes.c_size_t]

            self.core_lib.omniq_arena_alloc.restype = ctypes.c_uint64
            self.core_lib.omniq_arena_alloc.argtypes = [ctypes.c_size_t]

            self.core_lib.omniq_arena_free.restype = ctypes.c_int
            self.core_lib.omniq_arena_free.argtypes = [ctypes.c_uint64]

            self.core_lib.omniq_gc_step.restype = ctypes.c_double
            self.core_lib.omniq_gc_step.argtypes = [ctypes.c_double]

            self.core_lib.omniq_shm_publish.restype = ctypes.c_int
            self.core_lib.omniq_shm_publish.argtypes = [
                ctypes.c_uint32, ctypes.c_uint32, ctypes.c_uint32, ctypes.c_double
            ]

            self.core_lib.omniq_simulate_qasm_json.restype = ctypes.c_int
            self.core_lib.omniq_simulate_qasm_json.argtypes = [
                ctypes.c_char_p, ctypes.c_int, ctypes.c_double, ctypes.c_char_p, ctypes.c_size_t
            ]

            if hasattr(self.core_lib, "omniq_simulate_clifford_json"):
                self.core_lib.omniq_simulate_clifford_json.restype = ctypes.c_int
                self.core_lib.omniq_simulate_clifford_json.argtypes = [
                    ctypes.c_char_p, ctypes.c_int, ctypes.c_char_p, ctypes.c_size_t
                ]

            if hasattr(self.core_lib, "omniq_measure_rt_jitter_json"):
                self.core_lib.omniq_measure_rt_jitter_json.restype = ctypes.c_int
                self.core_lib.omniq_measure_rt_jitter_json.argtypes = [
                    ctypes.c_int, ctypes.c_int, ctypes.c_char_p, ctypes.c_size_t
                ]

            self.core_lib.omniq_core_init()

        if self.secret_lib is not None:
            self.secret_lib.omniq_secret_ensure_key.restype = ctypes.c_int
            self.secret_lib.omniq_secret_ensure_key.argtypes = [ctypes.c_char_p, ctypes.c_size_t]

            self.secret_lib.omniq_secret_get_json.restype = ctypes.c_int
            self.secret_lib.omniq_secret_get_json.argtypes = [
                ctypes.c_char_p, ctypes.c_char_p, ctypes.c_char_p, ctypes.c_size_t
            ]

            self.secret_lib.omniq_secret_verify_audit_chain.restype = ctypes.c_int
            self.secret_lib.omniq_secret_verify_audit_chain.argtypes = [ctypes.POINTER(ctypes.c_int)]

            if hasattr(self.secret_lib, "omniq_secret_mlkem768_handshake_json"):
                self.secret_lib.omniq_secret_mlkem768_handshake_json.restype = ctypes.c_int
                self.secret_lib.omniq_secret_mlkem768_handshake_json.argtypes = [
                    ctypes.c_char_p, ctypes.c_size_t
                ]

    @staticmethod
    def _load_lib(name: str):
        here = os.path.dirname(os.path.abspath(__file__))
        candidates = [
            os.path.join(here, "..", "lib", name),
            f"/opt/omniq/lib/{name}",
            f"/usr/local/lib/{name}",
            f"/usr/lib/x86_64-linux-gnu/{name}",
            name,
        ]
        for p in candidates:
            if os.path.exists(p):
                try:
                    return ctypes.CDLL(p)
                except Exception:
                    pass
        try:
            return ctypes.CDLL(name)
        except Exception:
            return None

    def get_status(self) -> Dict[str, Any]:
        if self.core_lib is None:
            return {"engine": "Python Fallback", "shm_active": False}
        buf = ctypes.create_string_buffer(16384)
        rc = self.core_lib.omniq_core_get_status_json(buf, len(buf))
        if rc == 0:
            return json.loads(buf.value.decode("utf-8"))
        return {"error": rc}

    def run_benchmarks(self) -> Dict[str, Any]:
        if self.core_lib is None:
            return {}
        buf = ctypes.create_string_buffer(16384)
        rc = self.core_lib.omniq_core_run_benchmarks_json(buf, len(buf))
        if rc == 0:
            return json.loads(buf.value.decode("utf-8"))
        return {"error": rc}

    def publish_shm(self, mxc_uK: int, qec_ns: int, tripped: int, jitter_us: float) -> None:
        if self.core_lib is not None:
            self.core_lib.omniq_shm_publish(mxc_uK, qec_ns, tripped, jitter_us)

    def simulate_qasm(self, qasm: str, shots: int = 1024, depol_noise: float = 0.0) -> Optional[Dict[str, Any]]:
        if self.core_lib is None:
            return None
        buf = ctypes.create_string_buffer(262144)
        rc = self.core_lib.omniq_simulate_qasm_json(
            qasm.encode("utf-8"), int(shots), float(depol_noise), buf, len(buf)
        )
        if rc == 0:
            return json.loads(buf.value.decode("utf-8"))
        return None

    def simulate_clifford(self, qasm: str, shots: int = 256) -> Optional[Dict[str, Any]]:
        if self.core_lib is None or not hasattr(self.core_lib, "omniq_simulate_clifford_json"):
            return None
        buf = ctypes.create_string_buffer(1048576)
        rc = self.core_lib.omniq_simulate_clifford_json(
            qasm.encode("utf-8"), int(shots), buf, len(buf)
        )
        if rc == 0:
            return json.loads(buf.value.decode("utf-8"))
        return None

    def measure_rt_jitter(self, samples: int = 120, interval_us: int = 100) -> Dict[str, Any]:
        if self.core_lib is None or not hasattr(self.core_lib, "omniq_measure_rt_jitter_json"):
            return {"error": "omniq_measure_rt_jitter_json_unavailable"}
        buf = ctypes.create_string_buffer(16384)
        rc = self.core_lib.omniq_measure_rt_jitter_json(
            int(samples), int(interval_us), buf, len(buf)
        )
        if rc == 0:
            return json.loads(buf.value.decode("utf-8"))
        return {"error_code": rc}

    def ensure_secret_key(self) -> str:
        if self.secret_lib is None:
            return ""
        buf = ctypes.create_string_buffer(256)
        if self.secret_lib.omniq_secret_ensure_key(buf, len(buf)) == 0:
            return buf.value.decode("utf-8")
        return ""

    def get_secret_telemetry(self, key: str, modality: str = "all") -> Dict[str, Any]:
        if self.secret_lib is None:
            return {"error": "libomniq_secret_missing"}
        buf = ctypes.create_string_buffer(32768)
        rc = self.secret_lib.omniq_secret_get_json(
            key.encode("utf-8"), modality.encode("utf-8"), buf, len(buf)
        )
        if rc == 0:
            return json.loads(buf.value.decode("utf-8"))
        return {"error_code": rc}

    def mlkem768_handshake(self) -> Dict[str, Any]:
        if self.secret_lib is None or not hasattr(self.secret_lib, "omniq_secret_mlkem768_handshake_json"):
            return {"error": "omniq_secret_mlkem768_handshake_unavailable"}
        buf = ctypes.create_string_buffer(8192)
        rc = self.secret_lib.omniq_secret_mlkem768_handshake_json(buf, len(buf))
        if rc in (0, -3):
            return json.loads(buf.value.decode("utf-8"))
        return {"error_code": rc}


core_bridge = OmniqCoreBridge()
