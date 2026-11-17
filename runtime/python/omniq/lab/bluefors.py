from __future__ import annotations

import json
import urllib.error
import urllib.request
from typing import Any, Dict, Optional

class BlueforsAPIError(RuntimeError):
    pass

class BlueforsClient:

    def __init__(self, base_url: str = "http://127.0.0.1:19098", timeout: float = 3.0) -> None:
        self.base_url = base_url.rstrip("/")
        self.timeout = float(timeout)

    def _request_json(
        self,
        method: str,
        path: str,
        payload: Optional[Dict[str, Any]] = None,
    ) -> Dict[str, Any]:
        url = f"{self.base_url}{path}"
        body: Optional[bytes] = None
        headers = {"Accept": "application/json"}
        if payload is not None:
            body = json.dumps(payload).encode("utf-8")
            headers["Content-Type"] = "application/json"

        req = urllib.request.Request(url=url, data=body, headers=headers, method=method.upper())
        try:
            with urllib.request.urlopen(req, timeout=self.timeout) as resp:
                raw = resp.read().decode("utf-8")
                return json.loads(raw) if raw.strip() else {}
        except urllib.error.HTTPError as exc:
            detail = ""
            try:
                detail = exc.read().decode("utf-8", errors="replace")
            except Exception:
                pass
            raise BlueforsAPIError(f"Bluefors HTTP {exc.code} on {method} {url}: {detail}") from exc
        except urllib.error.URLError as exc:
            raise BlueforsAPIError(f"Bluefors connection error on {method} {url}: {exc}") from exc

    @staticmethod
    def _extract_nested_bf_val(data_node: Dict[str, Any], key: str, default: float) -> float:
        entry = data_node.get(key)
        if isinstance(entry, (int, float)):
            return float(entry)
        if isinstance(entry, dict):
            val = (
                entry.get("content", {})
                .get("latest_valid_value", {})
                .get("value", entry.get("value", default))
            )
            return float(val)
        return float(default)

    def get_values(self) -> Dict[str, Any]:
        raw = self._request_json("GET", "/values")

        if "50K_flange_K" not in raw and isinstance(raw.get("data"), dict):
            d = raw["data"]
            mxc_k = self._extract_nested_bf_val(d, "driver.lakeshore.settings.inputs.channel6.temperature", 0.0141)
            return {
                "50K_flange_K": self._extract_nested_bf_val(d, "driver.lakeshore.settings.inputs.channel1.temperature", 48.2),
                "4K_flange_K": self._extract_nested_bf_val(d, "driver.lakeshore.settings.inputs.channel2.temperature", 3.82),
                "still_flange_K": self._extract_nested_bf_val(d, "driver.lakeshore.settings.inputs.channel3.temperature", 0.815),
                "cold_plate_K": self._extract_nested_bf_val(d, "driver.lakeshore.settings.inputs.channel5.temperature", 0.095),
                "mxc_flange_K": mxc_k,
                "mxc_mK": round(mxc_k * 1000.0, 4),
                "P1_mbar": self._extract_nested_bf_val(d, "vc.pressure.p1", 845.0),
                "P2_mbar": self._extract_nested_bf_val(d, "vc.pressure.p2", 0.62),
                "P3_mbar": self._extract_nested_bf_val(d, "vc.pressure.p3", 0.85),
                "P4_mbar": self._extract_nested_bf_val(d, "vc.pressure.p4", 1.15),
                "P5_mbar": self._extract_nested_bf_val(d, "vc.pressure.p5", 412.0),
                "P6_mbar": self._extract_nested_bf_val(d, "vc.pressure.p6", 1.4e-6),
                "turbo1_hz": self._extract_nested_bf_val(d, "vc.pump.turbo1.frequency", 820.0),
                "pulse_tube_on": bool(self._extract_nested_bf_val(d, "vc.compressor.pulsetube.status", 1.0)),
                "flow_mmol_s": self._extract_nested_bf_val(d, "vc.flow.molar", 0.85),
            }

        mxc_k = float(raw.get("mxc_flange_K", 0.0141))
        mxc_mk = float(raw.get("mxc_mK", mxc_k * 1000.0))
        return {
            "50K_flange_K": float(raw["50K_flange_K"]),
            "4K_flange_K": float(raw["4K_flange_K"]),
            "still_flange_K": float(raw["still_flange_K"]),
            "cold_plate_K": float(raw["cold_plate_K"]),
            "mxc_flange_K": mxc_k,
            "mxc_mK": mxc_mk,
            "P1_mbar": float(raw["P1_mbar"]),
            "P2_mbar": float(raw["P2_mbar"]),
            "P3_mbar": float(raw["P3_mbar"]),
            "P4_mbar": float(raw["P4_mbar"]),
            "P5_mbar": float(raw["P5_mbar"]),
            "P6_mbar": float(raw["P6_mbar"]),
            "turbo1_hz": float(raw["turbo1_hz"]),
            "pulse_tube_on": bool(raw["pulse_tube_on"]),
            "flow_mmol_s": float(raw["flow_mmol_s"]),
            "heater_uW": float(raw.get("heater_uW", 0.0)),
        }

    def get_status(self) -> Dict[str, Any]:
        return self._request_json("GET", "/status")

    def inject_heat_load_uW(self, power_uW: float) -> Dict[str, Any]:
        uW = float(power_uW)
        if uW < 0.0:
            raise ValueError(f"Injected heat load cannot be negative, got {uW} uW")
        return self._request_json("POST", "/control/heater", {"power_uW": uW})

    def trigger_cooldown(self, target_mK: float = 14.0) -> Dict[str, Any]:
        return self._request_json(
            "POST",
            "/control/cooldown",
            {"target_mK": float(target_mK), "heater_uW": 0.0},
        )
