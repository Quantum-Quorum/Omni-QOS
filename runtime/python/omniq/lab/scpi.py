from __future__ import annotations

import os
import select
import socket
import termios
import threading
import time
from typing import Dict, Optional, Tuple, Union
from urllib.parse import parse_qs, urlsplit

_BAUD_MAP: Dict[int, int] = {
    baud: getattr(termios, f"B{baud}")
    for baud in (
        1200,
        2400,
        4800,
        9600,
        19200,
        38400,
        57600,
        115200,
        230400,
        460800,
        921600,
    )
    if hasattr(termios, f"B{baud}")
}

class SCPIError(RuntimeError):
    pass

class SCPITransport:

    def __init__(self, endpoint: str, timeout: float = 3.0) -> None:
        self.endpoint = endpoint.strip()
        self.timeout = float(timeout)
        self._lock = threading.RLock()

        parsed = urlsplit(self.endpoint)
        self.scheme = parsed.scheme.lower()
        if self.scheme not in ("tcp", "usbtmc", "serial"):
            raise ValueError(
                f"Unsupported SCPI transport scheme {self.scheme!r} in {self.endpoint!r}; "
                "expected tcp://, usbtmc://, or serial://"
            )

        self.host: Optional[str] = None
        self.port: Optional[int] = None
        self.device_path: Optional[str] = None
        self.baud: int = 57600
        self.databits: int = 8
        self.parity: str = "N"
        self.stopbits: int = 1

        if self.scheme == "tcp":
            if not parsed.hostname or not parsed.port:
                raise ValueError(f"TCP SCPI endpoint requires host and port: {self.endpoint!r}")
            self.host = parsed.hostname
            self.port = int(parsed.port)
        elif self.scheme == "usbtmc":
            dev = (f"/{parsed.netloc}{parsed.path}" if parsed.netloc else parsed.path).strip()
            if not dev:
                raise ValueError(f"USBTMC SCPI endpoint requires device path: {self.endpoint!r}")
            self.device_path = dev
        elif self.scheme == "serial":
            dev = (f"/{parsed.netloc}{parsed.path}" if parsed.netloc else parsed.path).strip()
            if not dev:
                raise ValueError(f"Serial SCPI endpoint requires device path: {self.endpoint!r}")
            self.device_path = dev
            qs = parse_qs(parsed.query)
            if "baud" in qs:
                self.baud = int(qs["baud"][0])
            if "databits" in qs:
                self.databits = int(qs["databits"][0])
            if "parity" in qs:
                self.parity = qs["parity"][0].upper()
            if "stopbits" in qs:
                self.stopbits = int(qs["stopbits"][0])
            if self.baud not in _BAUD_MAP:
                raise ValueError(f"Unsupported POSIX termios baud rate: {self.baud}")

        self._sock: Optional[socket.socket] = None
        self._rx_buf: bytearray = bytearray()
        self._fd: Optional[int] = None

    def _ensure_open(self) -> None:
        if self.scheme == "tcp":
            if self._sock is not None:
                return
            assert self.host is not None and self.port is not None
            sock = socket.create_connection((self.host, self.port), timeout=self.timeout)
            try:
                sock.setsockopt(socket.IPPROTO_TCP, socket.TCP_NODELAY, 1)
            except OSError:
                pass
            sock.settimeout(self.timeout)
            self._sock = sock
            self._rx_buf.clear()
        elif self.scheme == "usbtmc":
            if self._fd is not None:
                return
            assert self.device_path is not None
            self._fd = os.open(self.device_path, os.O_RDWR)
        elif self.scheme == "serial":
            if self._fd is not None:
                return
            assert self.device_path is not None
            fd = os.open(self.device_path, os.O_RDWR | os.O_NOCTTY | os.O_NONBLOCK)
            try:
                self._configure_termios(fd)
            except Exception:
                os.close(fd)
                raise
            self._fd = fd
            self._rx_buf.clear()

    def _configure_termios(self, fd: int) -> None:
        attrs = termios.tcgetattr(fd)
        attrs[0] &= ~(
            termios.IGNBRK
            | termios.BRKINT
            | termios.PARMRK
            | termios.ISTRIP
            | termios.INLCR
            | termios.IGNCR
            | termios.ICRNL
            | termios.IXON
            | termios.IXOFF
        )
        attrs[1] &= ~termios.OPOST
        attrs[3] &= ~(
            termios.ECHO
            | termios.ECHONL
            | termios.ICANON
            | termios.ISIG
            | termios.IEXTEN
        )

        cflag = attrs[2]
        cflag &= ~(termios.CSIZE | termios.PARENB | termios.PARODD | termios.CSTOPB)
        cflag |= termios.CLOCAL | termios.CREAD
        cflag |= termios.CS7 if self.databits == 7 else termios.CS8
        if self.parity == "E":
            cflag |= termios.PARENB
        elif self.parity == "O":
            cflag |= termios.PARENB | termios.PARODD
        if self.stopbits == 2:
            cflag |= termios.CSTOPB
        attrs[2] = cflag

        speed_const = _BAUD_MAP[self.baud]
        attrs[4] = speed_const
        attrs[5] = speed_const
        attrs[6][termios.VMIN] = 0
        attrs[6][termios.VTIME] = 0

        termios.tcsetattr(fd, termios.TCSANOW, attrs)
        termios.tcflush(fd, termios.TCIOFLUSH)

    def close(self) -> None:
        with self._lock:
            if self._sock is not None:
                try:
                    self._sock.close()
                except OSError:
                    pass
                self._sock = None
            if self._fd is not None:
                try:
                    os.close(self._fd)
                except OSError:
                    pass
                self._fd = None
            self._rx_buf.clear()

    def __enter__(self) -> "SCPITransport":
        return self

    def __exit__(self, exc_type, exc, tb) -> None:
        self.close()

    def _write_raw(self, frame: bytes) -> None:
        self._ensure_open()
        if self.scheme == "tcp":
            assert self._sock is not None
            self._sock.sendall(frame)
        elif self.scheme == "usbtmc":
            assert self._fd is not None
            os.write(self._fd, frame)
        elif self.scheme == "serial":
            assert self._fd is not None
            total = 0
            deadline = time.monotonic() + self.timeout
            while total < len(frame):
                rem = deadline - time.monotonic()
                if rem <= 0:
                    raise SCPIError(f"Serial SCPI write timeout on {self.device_path}")
                _, wlist, _ = select.select([], [self._fd], [], rem)
                if not wlist:
                    raise SCPIError(f"Serial SCPI write timeout on {self.device_path}")
                total += os.write(self._fd, frame[total:])

    def _read_line_raw(self) -> str:
        self._ensure_open()
        if self.scheme == "tcp":
            assert self._sock is not None
            deadline = time.monotonic() + self.timeout
            while True:
                nl_idx = self._rx_buf.find(b"\n")
                if nl_idx != -1:
                    raw = bytes(self._rx_buf[:nl_idx])
                    del self._rx_buf[: nl_idx + 1]
                    return raw.decode("ascii", errors="replace").rstrip("\r\x00").strip()
                rem = deadline - time.monotonic()
                if rem <= 0:
                    raise SCPIError(f"TCP SCPI read timeout on {self.endpoint}")
                self._sock.settimeout(rem)
                chunk = self._sock.recv(4096)
                if not chunk:
                    raise ConnectionResetError(f"SCPI peer closed socket on {self.endpoint}")
                self._rx_buf.extend(chunk)

        elif self.scheme == "usbtmc":
            assert self._fd is not None
            rlist, _, _ = select.select([self._fd], [], [], self.timeout)
            if not rlist:
                raise SCPIError(f"USBTMC SCPI read timeout on {self.device_path}")
            data = os.read(self._fd, 4096)
            return data.decode("ascii", errors="replace").rstrip("\r\n\x00").strip()

        elif self.scheme == "serial":
            assert self._fd is not None
            deadline = time.monotonic() + self.timeout
            while True:
                nl_idx = self._rx_buf.find(b"\n")
                if nl_idx != -1:
                    raw = bytes(self._rx_buf[:nl_idx])
                    del self._rx_buf[: nl_idx + 1]
                    return raw.decode("ascii", errors="replace").rstrip("\r\x00").strip()
                rem = deadline - time.monotonic()
                if rem <= 0:
                    raise SCPIError(f"Serial SCPI read timeout on {self.device_path}")
                rlist, _, _ = select.select([self._fd], [], [], rem)
                if not rlist:
                    raise SCPIError(f"Serial SCPI read timeout on {self.device_path}")
                chunk = os.read(self._fd, 1024)
                if chunk:
                    self._rx_buf.extend(chunk)

        raise SCPIError(f"Unhandled SCPI transport scheme: {self.scheme}")

    def write(self, command: str) -> None:
        cmd = command.strip()
        if not cmd:
            return
        frame = (cmd + "\n").encode("ascii")
        with self._lock:
            try:
                self._write_raw(frame)
            except (BrokenPipeError, ConnectionResetError, OSError):
                if self.scheme == "tcp":
                    self.close()
                    self._write_raw(frame)
                else:
                    raise

    def query(self, command: str) -> str:
        cmd = command.strip()
        if not cmd:
            raise ValueError("SCPI query command cannot be empty")
        frame = (cmd + "\n").encode("ascii")
        with self._lock:
            for attempt in range(2):
                try:
                    self._write_raw(frame)
                    return self._read_line_raw()
                except (BrokenPipeError, ConnectionResetError) as exc:
                    self.close()
                    if attempt == 1 or self.scheme != "tcp":
                        raise SCPIError(f"SCPI query {cmd!r} failed on {self.endpoint}: {exc}") from exc

    def send(self, command: str) -> str:
        cmd = command.strip()
        if "?" in cmd:
            return self.query(cmd)
        self.write(cmd)
        return "OK"

def _coerce_transport(transport: Union[SCPITransport, str], timeout: float = 3.0) -> SCPITransport:
    if isinstance(transport, SCPITransport):
        return transport
    return SCPITransport(str(transport), timeout=timeout)

class LakeShore372Driver:

    CHANNEL_NAMES: Dict[int, str] = {
        1: "50K_flange",
        2: "4K_flange",
        3: "still",
        4: "magnet_shield",
        5: "cold_plate",
        6: "mxc",
    }

    def __init__(self, transport: Union[SCPITransport, str], timeout: float = 3.0) -> None:
        self.transport: SCPITransport = _coerce_transport(transport, timeout=timeout)

    def identify(self) -> str:
        return self.transport.query("*IDN?")

    def read_kelvin(self, channel: int) -> float:
        ch = int(channel)
        if not 1 <= ch <= 16:
            raise ValueError(f"Lake Shore 372 channel must be 1..16, got {ch}")
        resp = self.transport.query(f"RDGK? {ch}")
        return float(resp.split(",")[0].strip())

    def read_resistance_ohms(self, channel: int) -> float:
        ch = int(channel)
        if not 1 <= ch <= 16:
            raise ValueError(f"Lake Shore 372 channel must be 1..16, got {ch}")
        resp = self.transport.query(f"RDGR? {ch}")
        return float(resp.split(",")[0].strip())

    def read_heater_pct(self) -> float:
        resp = self.transport.query("HTR?")
        return float(resp.split(",")[0].strip())

    def set_mxc_setpoint_kelvin(self, kelvin: float) -> None:
        k = float(kelvin)
        if k < 0.0 or k > 325.0:
            raise ValueError(f"Invalid MXC setpoint {k} K")
        self.transport.write(f"SETP 0,{k:.6f}")

    def set_heater_range(self, output: int, rng: int) -> None:
        out = int(output)
        r = int(rng)
        if out not in (0, 1, 2):
            raise ValueError(f"Lake Shore 372 heater output must be 0, 1, or 2, got {out}")
        if not 0 <= r <= 8:
            raise ValueError(f"Lake Shore 372 heater range must be 0..8, got {r}")
        self.transport.write(f"RANGE {out},{r}")

    def emergency_heater_off(self) -> None:
        self.transport.write("RANGE 0,0")
        self.transport.write("RANGE 1,0")

    def get_pid(self, output: int = 0) -> Tuple[float, float, float]:
        resp = self.transport.query(f"PID? {int(output)}")
        parts = [float(x.strip()) for x in resp.split(",")]
        return (parts[0], parts[1], parts[2])

    def read_all_stages(self) -> Dict[str, float]:
        k_50 = self.read_kelvin(1)
        k_4 = self.read_kelvin(2)
        k_still = self.read_kelvin(3)
        k_cp = self.read_kelvin(5)
        k_mxc = self.read_kelvin(6)
        r_mxc = self.read_resistance_ohms(6)
        htr_pct = self.read_heater_pct()
        return {
            "50K_flange_K": round(k_50, 4),
            "4K_flange_K": round(k_4, 4),
            "still_K": round(k_still, 5),
            "cold_plate_K": round(k_cp, 5),
            "mxc_K": round(k_mxc, 6),
            "mxc_mK": round(k_mxc * 1000.0, 4),
            "mxc_ruo2_ohms": round(r_mxc, 2),
            "heater_pct": round(htr_pct, 3),
        }

    def close(self) -> None:
        self.transport.close()

class MicrowaveLODriver:

    def __init__(self, transport: Union[SCPITransport, str], timeout: float = 3.0) -> None:
        self.transport: SCPITransport = _coerce_transport(transport, timeout=timeout)

    def identify(self) -> str:
        return self.transport.query("*IDN?")

    def set_frequency_hz(self, freq_hz: float) -> None:
        f = float(freq_hz)
        if f <= 0.0:
            raise ValueError(f"Frequency must be positive, got {f} Hz")
        self.transport.write(f"SOURce:FREQuency:CW {f:.3f}")

    def get_frequency_hz(self) -> float:
        return float(self.transport.query("SOURce:FREQuency:CW?").strip())

    def set_power_dbm(self, dbm: float) -> None:
        p = float(dbm)
        self.transport.write(f"SOURce:POWer:LEVel:IMMediate:AMPLitude {p:.2f}")

    def get_power_dbm(self) -> float:
        return float(self.transport.query("SOURce:POWer?").strip())

    def set_rf_output(self, enabled: bool) -> None:
        token = "ON" if enabled else "OFF"
        self.transport.write(f"OUTPut:STATe {token}")

    def get_rf_output(self) -> bool:
        resp = self.transport.query("OUTPut:STATe?").strip().upper()
        return resp in ("1", "ON", "TRUE")

    def emergency_rf_clamp(self) -> None:
        self.transport.write("OUTPut:STATe OFF")
        self.transport.write("SOURce:POWer:LEVel:IMMediate:AMPLitude -120.0")

    def close(self) -> None:
        self.transport.close()
