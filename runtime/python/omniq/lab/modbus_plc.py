# SPDX-License-Identifier: Apache-2.0
"""
Pure-Python Binary RFC Modbus/TCP Client & Cryogenic Gas-Manifold PLC Controller.

Implements IEEE/IETF Modbus Application Protocol (MBAP) 7-byte Big-Endian framing
(`struct.pack(">HHHBB...", tx_id, proto_id=0, length, unit_id=1, func_code, ...)`):
  - `0x01` Read Coils
  - `0x03` Read Holding Registers
  - `0x05` Write Single Coil (`0xFF00` / `0x0000`)
  - `0x06` Write Single Register

Cryogenic Dilution Manifold PLC Mapping:
  Coils:
    - `0`: `RF_HEMT_BIAS_ENABLE` (Cryogenic 4K HEMT LNA & RF DAC bias relay)
    - `1`: `PULSE_TUBE_COMPRESSOR` (Cryomech PT420 rotary valve & helium compressor)
    - `2`: `TURBO_PUMP_ENABLE` (Pfeiffer HiPace 800M 3He circulation turbo pump)
    - `3..23`: `VALVE_V1..V21` (Pneumatic VCR manifold solenoid valves V1..V21)

  Holding Registers (16-bit unsigned words):
    - `0`: `MXC_uK_low16` (Mixing chamber temperature in microkelvin, bits 15..0)
    - `1`: `MXC_uK_high16` (Mixing chamber temperature in microkelvin, bits 31..16)
    - `2`: `P_still_uBar` (Still headspace pressure in microbar, 1 mbar = 1000 uBar)
    - `3`: `P_backing_uBar` (Turbo backing line pressure in microbar)
    - `4`: `INTERLOCK_STATE` (`0 = NOMINAL_ARMED`, `1 = HARDWARE_QUENCH_TRIPPED`)
    - `5`: `TURBO_SPEED_HZ` (Turbo molecular rotor frequency in Hz)
"""

from __future__ import annotations

import socket
import struct
import threading
from typing import Any, Dict, List, Optional


MODBUS_EXCEPTIONS: Dict[int, str] = {
    0x01: "ILLEGAL_FUNCTION",
    0x02: "ILLEGAL_DATA_ADDRESS",
    0x03: "ILLEGAL_DATA_VALUE",
    0x04: "SERVER_DEVICE_FAILURE",
    0x05: "ACKNOWLEDGE",
    0x06: "SERVER_DEVICE_BUSY",
}


class ModbusException(RuntimeError):
    """Raised on Modbus protocol exception responses or MBAP framing errors."""

    def __init__(self, message: str, exception_code: Optional[int] = None) -> None:
        super().__init__(message)
        self.exception_code = exception_code


class ModbusTCPClient:
    """
    Thread-safe pure-Python binary RFC Modbus/TCP client using 7-byte Big-Endian MBAP headers.
    """

    def __init__(
        self,
        host: str = "127.0.0.1",
        port: int = 15020,
        unit_id: int = 1,
        timeout: float = 3.0,
    ) -> None:
        self.host = str(host)
        self.port = int(port)
        self.unit_id = int(unit_id) & 0xFF
        self.timeout = float(timeout)
        self._lock = threading.RLock()
        self._sock: Optional[socket.socket] = None
        self._tx_id: int = 0

    def _next_tx_id(self) -> int:
        self._tx_id = (self._tx_id % 65535) + 1
        return self._tx_id

    def _ensure_open(self) -> socket.socket:
        if self._sock is None:
            sock = socket.create_connection((self.host, self.port), timeout=self.timeout)
            try:
                sock.setsockopt(socket.IPPROTO_TCP, socket.TCP_NODELAY, 1)
            except OSError:
                pass
            sock.settimeout(self.timeout)
            self._sock = sock
        return self._sock

    def close(self) -> None:
        with self._lock:
            if self._sock is not None:
                try:
                    self._sock.close()
                except OSError:
                    pass
                self._sock = None

    def __enter__(self) -> "ModbusTCPClient":
        return self

    def __exit__(self, exc_type, exc, tb) -> None:
        self.close()

    @staticmethod
    def _recv_exact(sock: socket.socket, num_bytes: int) -> bytes:
        buf = bytearray()
        while len(buf) < num_bytes:
            chunk = sock.recv(num_bytes - len(buf))
            if not chunk:
                raise ConnectionResetError("Modbus/TCP peer closed connection mid-frame")
            buf.extend(chunk)
        return bytes(buf)

    def _transact(self, func_code: int, pdu_payload: bytes) -> bytes:
        with self._lock:
            tx_id = self._next_tx_id()
            # MBAP Header (7B): >H (tx_id), >H (protocol_id=0), >H (length=1+1+len(payload)), >B (unit_id)
            # Followed by Function Code (>B) + PDU payload
            mbap_len = 2 + len(pdu_payload)
            request = struct.pack(">HHHBB", tx_id, 0, mbap_len, self.unit_id, func_code & 0xFF) + pdu_payload

            for attempt in range(2):
                try:
                    sock = self._ensure_open()
                    sock.sendall(request)
                    mbap_rx = self._recv_exact(sock, 7)
                    rx_tx_id, rx_proto_id, rx_len, rx_unit = struct.unpack(">HHHB", mbap_rx)
                    if rx_proto_id != 0:
                        raise ModbusException(f"Invalid MBAP protocol_id {rx_proto_id} (expected 0)")
                    if rx_len < 2:
                        raise ModbusException(f"Invalid MBAP length {rx_len}")
                    pdu_rx = self._recv_exact(sock, rx_len - 1)
                    if rx_tx_id != tx_id:
                        raise ModbusException(
                            f"MBAP transaction_id mismatch: expected {tx_id}, got {rx_tx_id}"
                        )
                    if rx_unit != self.unit_id:
                        raise ModbusException(
                            f"MBAP unit_id mismatch: expected {self.unit_id}, got {rx_unit}"
                        )
                    rx_fc = pdu_rx[0]
                    if rx_fc & 0x80:
                        exc_code = pdu_rx[1] if len(pdu_rx) > 1 else 0x04
                        exc_name = MODBUS_EXCEPTIONS.get(exc_code, f"EXCEPTION_0x{exc_code:02X}")
                        raise ModbusException(
                            f"Modbus exception on FC 0x{func_code:02X}: {exc_name} (0x{exc_code:02X})",
                            exception_code=exc_code,
                        )
                    if rx_fc != func_code:
                        raise ModbusException(
                            f"Unexpected Modbus function code 0x{rx_fc:02X} (expected 0x{func_code:02X})"
                        )
                    return pdu_rx[1:]
                except (BrokenPipeError, ConnectionResetError) as exc:
                    self.close()
                    if attempt == 1:
                        raise ModbusException(
                            f"Modbus/TCP transaction failed on {self.host}:{self.port}: {exc}"
                        ) from exc
            raise ModbusException("Unreachable Modbus/TCP retry state")

    # ------------------------------------------------------------------
    # Standard Modbus Function Codes (0x01, 0x03, 0x05, 0x06)
    # ------------------------------------------------------------------
    def read_coils(self, start: int, count: int) -> List[bool]:
        """FC `0x01`: Read `count` discrete coils starting at `start`."""
        s, n = int(start), int(count)
        if s < 0 or not 1 <= n <= 2000:
            raise ValueError(f"Invalid read_coils range start={s}, count={n}")
        resp = self._transact(0x01, struct.pack(">HH", s, n))
        byte_count = resp[0]
        expected_bytes = (n + 7) // 8
        if byte_count < expected_bytes or len(resp) < 1 + expected_bytes:
            raise ModbusException("Truncated FC 0x01 coil payload")
        coil_bytes = resp[1 : 1 + byte_count]
        return [bool((coil_bytes[i // 8] >> (i % 8)) & 0x01) for i in range(n)]

    def read_holding_registers(self, start: int, count: int) -> List[int]:
        """FC `0x03`: Read `count` 16-bit unsigned holding registers starting at `start`."""
        s, n = int(start), int(count)
        if s < 0 or not 1 <= n <= 125:
            raise ValueError(f"Invalid read_holding_registers range start={s}, count={n}")
        resp = self._transact(0x03, struct.pack(">HH", s, n))
        byte_count = resp[0]
        if byte_count != 2 * n or len(resp) < 1 + 2 * n:
            raise ModbusException("Truncated FC 0x03 register payload")
        return list(struct.unpack(f">{n}H", resp[1 : 1 + 2 * n]))

    def write_coil(self, addr: int, state: bool) -> bool:
        """FC `0x05`: Write single coil (`0xFF00` for ON, `0x0000` for OFF)."""
        a = int(addr)
        if a < 0 or a > 0xFFFF:
            raise ValueError(f"Invalid coil address: {a}")
        val_word = 0xFF00 if state else 0x0000
        resp = self._transact(0x05, struct.pack(">HH", a, val_word))
        echo_addr, echo_val = struct.unpack(">HH", resp[:4])
        if echo_addr != a or echo_val != val_word:
            raise ModbusException(f"FC 0x05 echo mismatch: ({echo_addr}, 0x{echo_val:04X})")
        return bool(state)

    def write_register(self, addr: int, value: int) -> int:
        """FC `0x06`: Write single 16-bit holding register."""
        a = int(addr)
        v = int(value) & 0xFFFF
        if a < 0 or a > 0xFFFF:
            raise ValueError(f"Invalid register address: {a}")
        resp = self._transact(0x06, struct.pack(">HH", a, v))
        echo_addr, echo_val = struct.unpack(">HH", resp[:4])
        if echo_addr != a or echo_val != v:
            raise ModbusException(f"FC 0x06 echo mismatch: ({echo_addr}, {echo_val})")
        return echo_val


class CryoPLCController:
    """
    High-level Cryogenic Dilution Gas-Manifold & Safety Interlock PLC Controller.
    """

    COIL_RF_HEMT_BIAS_ENABLE = 0
    COIL_PULSE_TUBE_COMPRESSOR = 1
    COIL_TURBO_PUMP_ENABLE = 2
    COIL_VALVE_V1_OFFSET = 3
    NUM_MANIFOLD_VALVES = 21
    TOTAL_COILS = 24  # 0..23

    REG_MXC_UK_LOW16 = 0
    REG_MXC_UK_HIGH16 = 1
    REG_P_STILL_UBAR = 2
    REG_P_BACKING_UBAR = 3
    REG_INTERLOCK_STATE = 4
    REG_TURBO_SPEED_HZ = 5
    TOTAL_REGISTERS = 6  # 0..5

    def __init__(
        self,
        host: str = "127.0.0.1",
        port: int = 15020,
        unit_id: int = 1,
        timeout: float = 3.0,
    ) -> None:
        self.host = str(host)
        self.port = int(port)
        self.client = ModbusTCPClient(host=host, port=port, unit_id=unit_id, timeout=timeout)

    def read_coils(self, start: int, count: int) -> List[bool]:
        return self.client.read_coils(start, count)

    def read_holding_registers(self, start: int, count: int) -> List[int]:
        return self.client.read_holding_registers(start, count)

    def write_coil(self, addr: int, state: bool) -> bool:
        return self.client.write_coil(addr, state)

    def write_register(self, addr: int, value: int) -> int:
        return self.client.write_register(addr, value)

    def get_manifold_state(self) -> Dict[str, Any]:
        """Read all 24 PLC coils and 6 holding registers into structured manifold state."""
        coils = self.client.read_coils(0, self.TOTAL_COILS)
        regs = self.client.read_holding_registers(0, self.TOTAL_REGISTERS)

        mxc_uk = ((regs[self.REG_MXC_UK_HIGH16] & 0xFFFF) << 16) | (
            regs[self.REG_MXC_UK_LOW16] & 0xFFFF
        )
        valve_list = [
            bool(coils[self.COIL_VALVE_V1_OFFSET + i])
            for i in range(self.NUM_MANIFOLD_VALVES)
        ]
        valve_map = {
            f"V{idx}": valve_list[idx - 1]
            for idx in range(1, self.NUM_MANIFOLD_VALVES + 1)
        }
        return {
            "rf_hemt_bias_enable": coils[self.COIL_RF_HEMT_BIAS_ENABLE],
            "rf_hemt_bias": coils[self.COIL_RF_HEMT_BIAS_ENABLE],
            "pulse_tube_compressor": coils[self.COIL_PULSE_TUBE_COMPRESSOR],
            "turbo_pump_enable": coils[self.COIL_TURBO_PUMP_ENABLE],
            "valves": valve_list,
            "valve_map": valve_map,
            "open_valve_count": sum(1 for v in valve_list if v),
            "mxc_uK": mxc_uk,
            "mxc_mK": round(mxc_uk / 1000.0, 3),
            "p_still_uBar": regs[self.REG_P_STILL_UBAR],
            "p_still_mbar": round(regs[self.REG_P_STILL_UBAR] / 1000.0, 4),
            "p_backing_uBar": regs[self.REG_P_BACKING_UBAR],
            "p_backing_mbar": round(regs[self.REG_P_BACKING_UBAR] / 1000.0, 4),
            "interlock_state": regs[self.REG_INTERLOCK_STATE],
            "interlock_tripped": regs[self.REG_INTERLOCK_STATE] != 0,
            "turbo_speed_hz": regs[self.REG_TURBO_SPEED_HZ],
            "raw_coils": coils,
            "raw_registers": regs,
        }

    def set_valve(self, v_idx: int, open_state: bool) -> bool:
        """Actuate pneumatic gas-manifold solenoid valve `V1..V21` (Coils `3..23`)."""
        idx = int(v_idx)
        if not 1 <= idx <= self.NUM_MANIFOLD_VALVES:
            raise ValueError(f"Manifold valve index must be 1..{self.NUM_MANIFOLD_VALVES}, got {idx}")
        coil_addr = self.COIL_VALVE_V1_OFFSET + (idx - 1)
        return self.client.write_coil(coil_addr, bool(open_state))

    def trip_hardware_interlock(self) -> Dict[str, Any]:
        """
        Trip PLC hardware quench interlock:
          - Writes Coil `0 = False` (cuts 4K HEMT LNA & RF DAC bias supply)
          - Writes Holding Register `4 = 1` (`HARDWARE_QUENCH_TRIPPED`)
        """
        self.client.write_coil(self.COIL_RF_HEMT_BIAS_ENABLE, False)
        self.client.write_register(self.REG_INTERLOCK_STATE, 1)
        return self.get_manifold_state()

    def reset_hardware_interlock(self) -> Dict[str, Any]:
        """
        Reset PLC hardware interlock to nominal armed state:
          - Writes Holding Register `4 = 0` (`NOMINAL_ARMED`)
          - Writes Coil `0 = True` (restores 4K HEMT LNA & RF DAC bias supply)
        """
        self.client.write_register(self.REG_INTERLOCK_STATE, 0)
        self.client.write_coil(self.COIL_RF_HEMT_BIAS_ENABLE, True)
        return self.get_manifold_state()

    def close(self) -> None:
        self.client.close()
