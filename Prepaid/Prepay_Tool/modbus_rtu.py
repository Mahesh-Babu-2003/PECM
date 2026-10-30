"""Minimal Modbus RTU master (functions 03, 06, 16) for the PREPAY_V0 bridge.

No GUI dependencies, so it can be tested or reused on its own.
"""
import threading
import time

import serial

PARITY_MAP = {"None": serial.PARITY_NONE,
              "Even": serial.PARITY_EVEN,
              "Odd": serial.PARITY_ODD}

EXCEPTION_NAMES = {
    1: "Illegal function",
    2: "Illegal data address",
    3: "Illegal data value",
    4: "Slave device failure",
    6: "Slave device busy",
}


def crc16(data: bytes) -> int:
    crc = 0xFFFF
    for b in data:
        crc ^= b
        for _ in range(8):
            crc = (crc >> 1) ^ 0xA001 if crc & 1 else crc >> 1
    return crc


def add_crc(frame: bytes) -> bytes:
    c = crc16(frame)
    return frame + bytes((c & 0xFF, c >> 8))


def hexs(data: bytes) -> str:
    return " ".join(f"{b:02X}" for b in data)


class ModbusError(Exception):
    pass


class ModbusTimeout(ModbusError):
    pass


class ModbusCRCError(ModbusError):
    pass


class ModbusException(ModbusError):
    def __init__(self, function, code):
        self.function = function
        self.code = code
        name = EXCEPTION_NAMES.get(code, "Unknown exception")
        super().__init__(f"Modbus exception {code:02X} ({name}) "
                         f"for function {function:02X}")


class ModbusMaster:
    def __init__(self, log=None):
        self.ser = None
        self.log = log or (lambda direction, text: None)
        self.lock = threading.RLock()
        self.timeout = 0.6
        self.tx_count = 0
        self.ok_count = 0
        self.err_count = 0
        self._last_activity = 0.0

    # ---- connection -------------------------------------------------
    @property
    def is_open(self):
        return self.ser is not None and self.ser.is_open

    def open(self, port, baud, parity="Even", timeout=0.6):
        with self.lock:
            self.close()
            self.timeout = timeout
            self.ser = serial.Serial(port=port, baudrate=baud,
                                     bytesize=8, parity=PARITY_MAP[parity],
                                     stopbits=1, timeout=timeout,
                                     write_timeout=1.0)

    def close(self):
        with self.lock:
            if self.ser is not None:
                try:
                    self.ser.close()
                finally:
                    self.ser = None

    def set_serial(self, baud, parity):
        """Change line settings on the already-open port."""
        with self.lock:
            self.ser.baudrate = baud
            self.ser.parity = PARITY_MAP[parity]

    # ---- low level --------------------------------------------------
    def _read_exact(self, n):
        buf = self.ser.read(n)
        return buf

    def transact(self, request: bytes) -> bytes:
        """Send request (without CRC) and return the validated response
        frame (with CRC). Raises ModbusError subclasses on failure."""
        with self.lock:
            if not self.is_open:
                raise ModbusError("Serial port is not open")
            frame = add_crc(request)
            slave, function = frame[0], frame[1]

            # honour the 3.5 character inter-frame gap
            char_time = 11.0 / max(self.ser.baudrate, 1)
            gap = max(0.005, 3.5 * char_time)
            wait = self._last_activity + gap - time.monotonic()
            if wait > 0:
                time.sleep(wait)

            self.ser.reset_input_buffer()
            self.ser.write(frame)
            self.ser.flush()
            self.tx_count += 1
            self.log("TX", hexs(frame))

            try:
                head = self._read_exact(5)
                if len(head) == 0:
                    raise ModbusTimeout("No response (timeout)")
                if len(head) < 5:
                    self.log("RX", hexs(head) + "  (incomplete)")
                    raise ModbusError("Incomplete response")

                if head[1] & 0x80:
                    total = 5
                elif head[1] == 0x03:
                    total = 5 + head[2]
                elif head[1] in (0x06, 0x10):
                    total = 8
                else:
                    total = 5
                resp = head
                if total > 5:
                    rest = self._read_exact(total - 5)
                    resp = head + rest
                self.log("RX", hexs(resp))
                if len(resp) < total:
                    raise ModbusError("Incomplete response")

                body, rx_crc = resp[:-2], resp[-2] | (resp[-1] << 8)
                if crc16(body) != rx_crc:
                    raise ModbusCRCError("CRC error in response")
                if resp[0] != slave:
                    raise ModbusError(
                        f"Response from ID {resp[0]}, expected {slave}")
                if resp[1] & 0x80:
                    raise ModbusException(resp[1] & 0x7F, resp[2])
                if resp[1] != function:
                    raise ModbusError(
                        f"Unexpected function {resp[1]:02X} in response")
                self.ok_count += 1
                return resp
            except ModbusError:
                self.err_count += 1
                raise
            finally:
                self._last_activity = time.monotonic()

    # ---- functions --------------------------------------------------
    def read_holding(self, slave, addr, count):
        req = bytes((slave, 0x03, addr >> 8, addr & 0xFF,
                     count >> 8, count & 0xFF))
        resp = self.transact(req)
        if resp[2] != count * 2:
            raise ModbusError("Unexpected byte count in response")
        data = resp[3:3 + resp[2]]
        return [(data[i] << 8) | data[i + 1] for i in range(0, len(data), 2)]

    def write_single(self, slave, addr, value):
        value &= 0xFFFF
        req = bytes((slave, 0x06, addr >> 8, addr & 0xFF,
                     value >> 8, value & 0xFF))
        resp = self.transact(req)
        if resp[2:6] != req[2:6]:
            raise ModbusError("Write echo does not match request")

    def write_multiple(self, slave, addr, values):
        n = len(values)
        req = bytearray((slave, 0x10, addr >> 8, addr & 0xFF,
                         n >> 8, n & 0xFF, n * 2))
        for v in values:
            v &= 0xFFFF
            req += bytes((v >> 8, v & 0xFF))
        resp = self.transact(bytes(req))
        if resp[2:6] != bytes(req[2:6]):
            raise ModbusError("Write response does not match request")
