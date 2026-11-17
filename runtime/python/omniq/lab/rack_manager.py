# SPDX-License-Identifier: Apache-2.0
"""
Unified Cryogenic Dilution Lab Rack Manager (`LabRackManager`).

Orchestrates all 5 production hardware / Digital-Twin wire-protocol drivers:
  1. Lake Shore 372 AC Resistance Bridge (`LakeShore372Driver` — SCPI)
  2. Rohde & Schwarz SGS100A / Keysight EXG LO (`MicrowaveLODriver` — SCPI)
  3. Bluefors LD400 Gas Handling System (`BlueforsClient` — HTTP JSON)
  4. Cryogenic Manifold & HEMT Bias PLC (`CryoPLCController` — Binary Modbus/TCP)
  5. Fermilab QICK Xilinx ZCU216 RFSoC (`QickRFSocClient` — tProc v2 + JSON-RPC/XDMA)
"""

from __future__ import annotations

import configparser
import os
from typing import Any, Dict, List, Optional, Union
from urllib.parse import urlsplit, urlunsplit

from ..circuit import Circuit
from .bluefors import BlueforsClient
from .modbus_plc import CryoPLCController
from .qick_rfsoc import QickRFSocClient
from .scpi import LakeShore372Driver, MicrowaveLODriver


def _default_repo_conf_path() -> str:
    return os.path.abspath(
        os.path.join(os.path.dirname(__file__), "..", "..", "..", "..", "etc", "omniq", "lab_rack.conf")
    )


def _load_rack_config(config_path: Optional[str] = None) -> Dict[str, Any]:
    candidates: List[str] = []
    if config_path:
        candidates.append(config_path)
    else:
        candidates.extend(["/etc/omniq/lab_rack.conf", _default_repo_conf_path()])

    chosen: Optional[str] = None
    for path in candidates:
        if path and os.path.isfile(path):
            chosen = path
            break

    defaults: Dict[str, Any] = {
        "mode": "twin_loopback",
        "lakeshore372_host": "127.0.0.1",
        "lakeshore372_port": "17777",
        "microwave_lo_host": "127.0.0.1",
        "microwave_lo_port": "15025",
        "bluefors_url": "http://127.0.0.1:19098",
        "modbus_plc_host": "127.0.0.1",
        "modbus_plc_port": "15020",
        "qick_rfsoc_host": "127.0.0.1",
        "qick_rfsoc_port": "18888",
        "mxc_interlock_ceiling_mK": "50.0",
        "max_backing_pressure_mbar": "2.50",
    }
    if chosen is None:
        defaults["_config_path"] = _default_repo_conf_path()
        return defaults

    with open(chosen, "r", encoding="utf-8") as f:
        raw_text = f.read()

    has_section = any(
        ln.strip().startswith("[") and ln.strip().endswith("]")
        for ln in raw_text.splitlines()
        if ln.strip() and not ln.strip().startswith(("#", ";"))
    )
    ini_text = raw_text if has_section else "[lab_rack]\n" + raw_text

    parser = configparser.ConfigParser(inline_comment_prefixes=("#", ";"))
    parser.optionxform = str
    parser.read_string(ini_text)

    merged: Dict[str, Any] = dict(defaults)
    merged["_config_path"] = chosen
    for section in parser.sections():
        for k, v in parser.items(section):
            clean = v.strip().strip('"').strip("'")
            merged[k.strip()] = clean
    return merged


def _offset_http_url(url: str, port_offset: int) -> str:
    if port_offset == 0:
        return url
    sp = urlsplit(url)
    host = sp.hostname or "127.0.0.1"
    base_port = sp.port or (443 if sp.scheme == "https" else 80)
    new_netloc = f"{host}:{base_port + port_offset}"
    return urlunsplit((sp.scheme, new_netloc, sp.path, sp.query, sp.fragment))


class LabRackManager:
    """
    Unified controller and hardware safety-interlock supervisor for the OmniQ-OS
    cryogenic dilution refrigerator & RFSoC instrument rack.
    """

    def __init__(
        self,
        config_path: Optional[str] = None,
        port_offset: int = 0,
        timeout: float = 3.0,
    ) -> None:
        self._explicit_config_path = config_path
        self.port_offset = int(port_offset)
        self.timeout = float(timeout)
        self.config = _load_rack_config(config_path)
        self.config_path = str(self.config.get("_config_path", config_path or _default_repo_conf_path()))
        self._last_interlock_reason = "NOMINAL"
        self._init_clients()

    def _init_clients(self) -> None:
        self.mode = str(self.config.get("mode", "twin_loopback"))
        self.mxc_interlock_ceiling_mK = float(self.config.get("mxc_interlock_ceiling_mK", 50.0))
        self.max_backing_pressure_mbar = float(self.config.get("max_backing_pressure_mbar", 2.50))

        ls_host = str(self.config["lakeshore372_host"])
        ls_port = int(self.config["lakeshore372_port"]) + self.port_offset
        ls_uri = (
            str(self.config["lakeshore372_transport"])
            if ("lakeshore372_transport" in self.config and self.port_offset == 0)
            else f"tcp://{ls_host}:{ls_port}"
        )

        lo_host = str(self.config["microwave_lo_host"])
        lo_port = int(self.config["microwave_lo_port"]) + self.port_offset
        lo_uri = (
            str(self.config["microwave_lo_transport"])
            if ("microwave_lo_transport" in self.config and self.port_offset == 0)
            else f"tcp://{lo_host}:{lo_port}"
        )

        bf_url = _offset_http_url(str(self.config["bluefors_url"]), self.port_offset)
        plc_host = str(self.config["modbus_plc_host"])
        plc_port = int(self.config["modbus_plc_port"]) + self.port_offset
        qick_host = str(self.config["qick_rfsoc_host"])
        qick_port = int(self.config["qick_rfsoc_port"]) + self.port_offset

        self.lakeshore = LakeShore372Driver(ls_uri, timeout=self.timeout)
        self.microwave_lo = MicrowaveLODriver(lo_uri, timeout=self.timeout)
        self.bluefors = BlueforsClient(bf_url, timeout=self.timeout)
        self.plc = CryoPLCController(plc_host, plc_port, timeout=self.timeout)
        self.qick = QickRFSocClient(qick_host, qick_port, timeout=max(5.0, self.timeout))

    def reconnect(self) -> None:
        """Close active sockets and re-bind all 5 drivers using current `self.config`."""
        self.close()
        self._init_clients()

    def save_config(self, path: Optional[str] = None) -> str:
        """Persist current `self.config` endpoints back to `lab_rack.conf`."""
        target = path or self.config_path or _default_repo_conf_path()
        ordered_keys = [
            "mode",
            "lakeshore372_host",
            "lakeshore372_port",
            "microwave_lo_host",
            "microwave_lo_port",
            "bluefors_url",
            "modbus_plc_host",
            "modbus_plc_port",
            "qick_rfsoc_host",
            "qick_rfsoc_port",
            "mxc_interlock_ceiling_mK",
            "max_backing_pressure_mbar",
        ]
        lines = [
            "# OmniQ-OS v2.0 Cryogenic Dilution Lab Wire-Protocol Configuration",
            "# Switch mode = physical_lab and IPs to 192.168.1.x for live lab hardware.",
            "[lab_rack]",
        ]
        for k in ordered_keys:
            if k in self.config:
                lines.append(f"{k} = {self.config[k]}")
        for k, v in self.config.items():
            if not k.startswith("_") and k not in ordered_keys:
                lines.append(f"{k} = {v}")
        os.makedirs(os.path.dirname(os.path.abspath(target)), exist_ok=True)
        with open(target, "w", encoding="utf-8") as f:
            f.write("\n".join(lines) + "\n")
        self.config_path = target
        return target

    def poll_all(self, enforce_interlock: bool = False) -> Dict[str, Any]:
        """Poll telemetry from all instruments across SCPI, HTTP JSON, and Modbus/TCP."""
        ilock_eval: Optional[Dict[str, Any]] = None
        if enforce_interlock:
            ilock_eval = self.enforce_safety_interlock()

        ls_idn = self.lakeshore.identify()
        ls_stages = self.lakeshore.read_all_stages()

        lo_idn = self.microwave_lo.identify()
        lo_freq = self.microwave_lo.get_frequency_hz()
        lo_pow = self.microwave_lo.get_power_dbm()
        lo_on = self.microwave_lo.get_rf_output()

        bf_vals = self.bluefors.get_values()
        bf_stat = self.bluefors.get_status()

        plc_state = self.plc.get_manifold_state()

        mxc_mk = max(ls_stages["mxc_mK"], bf_vals["mxc_mK"])
        backing_mbar = max(bf_vals["P3_mbar"], plc_state["p_backing_mbar"])
        interlock_tripped = bool(
            plc_state["interlock_tripped"] or not plc_state["rf_hemt_bias_enable"] or not lo_on
        )
        reasons = ilock_eval["reasons"] if (ilock_eval and ilock_eval.get("reasons")) else []
        reason_str = (
            reasons[0]
            if reasons
            else (self._last_interlock_reason if interlock_tripped else "NOMINAL")
        )

        stages_unified = {
            **ls_stages,
            "50K_K": ls_stages["50K_flange_K"],
            "4K_K": ls_stages["4K_flange_K"],
        }

        return {
            "mode": self.mode,
            "config_path": self.config_path,
            "mxc_mK": round(mxc_mk, 4),
            "mxc_ceiling_mK": self.mxc_interlock_ceiling_mK,
            "backing_pressure_mbar": round(backing_mbar, 4),
            "max_backing_pressure_mbar": self.max_backing_pressure_mbar,
            "rf_output_enabled": lo_on,
            "hemt_bias_enabled": plc_state["rf_hemt_bias_enable"],
            "interlock_tripped": interlock_tripped,
            "interlock": {
                "tripped": interlock_tripped,
                "reason": reason_str,
                "reasons": reasons,
                "ceiling_mK": self.mxc_interlock_ceiling_mK,
            },
            "stages": stages_unified,
            "lakeshore372": {
                "idn": ls_idn,
                **stages_unified,
            },
            "microwave_lo": {
                "idn": lo_idn,
                "frequency_hz": lo_freq,
                "freq_hz": lo_freq,
                "power_dbm": lo_pow,
                "rf_output_enabled": lo_on,
                "rf_enabled": lo_on,
            },
            "bluefors": {
                **bf_vals,
                "values": bf_vals,
                "status": bf_stat,
            },
            "plc": plc_state,
            "modbus_plc": plc_state,
            "qick_rfsoc": {
                "host": self.qick.host,
                "port": self.qick.port,
                "pcie_xdma_present": self.qick.has_pcie_xdma(),
            },
        }

    def trip_safety_interlock(self, reason: str = "MANUAL_SAFETY_TRIP") -> Dict[str, Any]:
        """
        Immediately execute physical hardware clamp across SCPI LO, Lake Shore 372,
        and Modbus PLC (`OUTPut:STATe OFF`, `RANGE 0,0`, `Coil[0]=False`, `Reg[4]=1`).
        """
        self._last_interlock_reason = str(reason)
        self.microwave_lo.emergency_rf_clamp()
        self.lakeshore.emergency_heater_off()
        plc_after = self.plc.trip_hardware_interlock()
        return {
            "tripped": True,
            "reason": self._last_interlock_reason,
            "reasons": [self._last_interlock_reason],
            "actions": [
                "SCPI_LO_OUTPUT_STATE_OFF",
                "SCPI_LAKESHORE_RANGE_0_0",
                "MODBUS_COIL_0_FALSE_REG_4_TRIPPED",
            ],
            "rf_output_enabled": False,
            "hemt_bias_enabled": plc_after["rf_hemt_bias_enable"],
        }

    def enforce_safety_interlock(self) -> Dict[str, Any]:
        """
        Evaluate cryogenic thermal & backing-vacuum interlocks across redundant sensors.

        If `mxc_mK >= mxc_interlock_ceiling_mK` or `backing_mbar >= max_backing_pressure_mbar`,
        physically executes hardware clamp sequence across all 3 control buses:
          1. Microwave LO SCPI: `OUTPut:STATe OFF` + `-120.0 dBm`
          2. Lake Shore 372 SCPI: `RANGE 0,0` + `RANGE 1,0` (cuts MXC/still heaters)
          3. Cryo PLC Modbus/TCP: `write_coil(0, False)` + `write_register(4, 1)`
        """
        ls_mxc_mk = self.lakeshore.read_kelvin(6) * 1000.0
        bf_vals = self.bluefors.get_values()
        plc_state = self.plc.get_manifold_state()

        mxc_mk = max(ls_mxc_mk, bf_vals["mxc_mK"])
        backing_mbar = max(bf_vals["P3_mbar"], plc_state["p_backing_mbar"])

        reasons: List[str] = []
        if mxc_mk >= self.mxc_interlock_ceiling_mK:
            reasons.append(
                f"MXC_QUENCH_EXCEEDED ({mxc_mk:.2f} mK >= {self.mxc_interlock_ceiling_mK:.2f} mK)"
            )
        if backing_mbar >= self.max_backing_pressure_mbar:
            reasons.append(
                f"BACKING_PRESSURE_EXCEEDED ({backing_mbar:.3f} mbar >= {self.max_backing_pressure_mbar:.3f} mbar)"
            )

        actions: List[str] = []
        if reasons:
            self._last_interlock_reason = reasons[0]
            self.microwave_lo.emergency_rf_clamp()
            actions.append("SCPI_LO_OUTPUT_STATE_OFF")

            self.lakeshore.emergency_heater_off()
            actions.append("SCPI_LAKESHORE_RANGE_0_0")

            plc_after = self.plc.trip_hardware_interlock()
            actions.append("MODBUS_COIL_0_FALSE_REG_4_TRIPPED")

            return {
                "tripped": True,
                "reason": reasons[0],
                "reasons": reasons,
                "actions": actions,
                "mxc_mK": round(mxc_mk, 4),
                "ceiling_mK": self.mxc_interlock_ceiling_mK,
                "backing_mbar": round(backing_mbar, 4),
                "max_backing_mbar": self.max_backing_pressure_mbar,
                "rf_output_enabled": False,
                "hemt_bias_enabled": plc_after["rf_hemt_bias_enable"],
            }

        lo_on = self.microwave_lo.get_rf_output()
        already_tripped = bool(plc_state["interlock_tripped"] or not plc_state["rf_hemt_bias_enable"] or not lo_on)
        return {
            "tripped": already_tripped,
            "reason": self._last_interlock_reason if already_tripped else "NOMINAL",
            "reasons": [],
            "actions": actions,
            "mxc_mK": round(mxc_mk, 4),
            "ceiling_mK": self.mxc_interlock_ceiling_mK,
            "backing_mbar": round(backing_mbar, 4),
            "max_backing_mbar": self.max_backing_pressure_mbar,
            "rf_output_enabled": lo_on,
            "hemt_bias_enabled": plc_state["rf_hemt_bias_enable"],
        }

    def reset_safety_interlock(self, restore_lo_dbm: float = -10.0) -> Dict[str, Any]:
        """
        Re-arm Modbus HEMT/DAC bias (`Coil 0 = True`, `Reg 4 = 0`) and Microwave LO
        (`OUTPut:STATe ON`) only after MXC temperature and backing pressure return below ceiling.
        """
        ls_mxc_mk = self.lakeshore.read_kelvin(6) * 1000.0
        bf_vals = self.bluefors.get_values()
        plc_state = self.plc.get_manifold_state()

        mxc_mk = max(ls_mxc_mk, bf_vals["mxc_mK"])
        backing_mbar = max(bf_vals["P3_mbar"], plc_state["p_backing_mbar"])

        if mxc_mk >= self.mxc_interlock_ceiling_mK or backing_mbar >= self.max_backing_pressure_mbar:
            return {
                "ok": False,
                "tripped": True,
                "error": "UNSAFE_TO_RESET_INTERLOCK",
                "mxc_mK": round(mxc_mk, 4),
                "ceiling_mK": self.mxc_interlock_ceiling_mK,
                "backing_mbar": round(backing_mbar, 4),
                "max_backing_mbar": self.max_backing_pressure_mbar,
            }

        self._last_interlock_reason = "NOMINAL"
        plc_after = self.plc.reset_hardware_interlock()
        self.microwave_lo.set_power_dbm(restore_lo_dbm)
        self.microwave_lo.set_rf_output(True)
        lo_on = self.microwave_lo.get_rf_output()

        return {
            "ok": True,
            "tripped": False,
            "mxc_mK": round(mxc_mk, 4),
            "backing_mbar": round(backing_mbar, 4),
            "rf_output_enabled": lo_on,
            "hemt_bias_enabled": plc_after["rf_hemt_bias_enable"],
        }

    def send_raw_scpi(self, target: str, cmd: str) -> str:
        """Send raw IEEE-488.2 SCPI command or query to `'lakeshore'` or `'microwave_lo'`."""
        t = target.strip().lower()
        if t in ("lakeshore", "lakeshore372", "ls372", "thermometry"):
            return self.lakeshore.transport.send(cmd)
        if t in ("lo", "microwave_lo", "sgs100a", "exg", "rf_lo", "microwave"):
            return self.microwave_lo.transport.send(cmd)
        raise ValueError(
            f"Unknown SCPI target {target!r}; expected 'lakeshore372' or 'microwave_lo'"
        )

    def dispatch_qick_circuit(
        self,
        circ_or_qasm: Union[Circuit, str],
        shots: int = 1024,
    ) -> Dict[str, Any]:
        """
        Enforce cryogenic safety interlock and dispatch `circ_or_qasm` to QICK ZCU216 RFSoC.
        """
        ilock = self.enforce_safety_interlock()
        qick_res = self.qick.execute(circ_or_qasm, shots=shots, raise_on_error=False)
        qick_res["safety_interlock"] = ilock
        return qick_res

    def close(self) -> None:
        self.lakeshore.close()
        self.microwave_lo.close()
        self.plc.close()

    def __enter__(self) -> "LabRackManager":
        return self

    def __exit__(self, exc_type, exc, tb) -> None:
        self.close()
