#!/usr/bin/env python3
"""PREPAY_V0 Modbus Bridge Tool

Tkinter GUI (Modbus RTU master) for the MS51PC0AE prepaid energy bridge
firmware. Connect a USB-RS485 adapter to RS485 port 1 of the board
(defaults: ID 1, 9600 baud, 8E1).

Requires:  pip install pyserial
Run:       python prepaid_tool.py
"""
import csv
import datetime
import queue
import struct
import threading
import tkinter as tk
from tkinter import filedialog, messagebox, simpledialog, ttk

import serial
from serial.tools import list_ports

from modbus_rtu import (ModbusError, ModbusException, ModbusMaster,
                        ModbusTimeout)

# --------------------------------------------------------------------------
# Register map (zero-based protocol offsets; label = 40001 + offset)
# --------------------------------------------------------------------------
PARITY_NAMES = {0: "None", 1: "Even", 2: "Odd"}
PARITY_FROM_NAME = {v: k for k, v in PARITY_NAMES.items()}
BILLING_NAMES = {0: "kWh", 1: "kVAh", 2: "PF", 3: "kW", 4: "kVA", 5: "kVArh"}
OVERRIDE_NAMES = {0: "Automatic (balance control)", 1: "BMS relay override"}
LOAD_PARAMETER_NAMES = {i: f"Current Ch{i + 1}" for i in range(12)}
BAUDS = [4800, 9600, 14400, 19200, 38400, 56000, 57600, 115200]

CONTROLLER_REG_COUNT = 100
METER_REG_COUNT = 50
RTC_FIRST = 36   # 40037..40042: year, month, day, hour, minute, second
METER_BASE = 200  # 40201..40250

# offset: (name, access, kind)
REGS = {
    0: ("All-load command/status", "RW", "bool"),
    1: ("Port 1 baud rate", "RW", "baud"),
    2: ("Port 1 parity", "RW", "parity"),
    3: ("Port 1 controller ID", "RW", "id"),
    4: ("Port 2 baud rate", "RW", "baud"),
    5: ("Port 2 parity", "RW", "parity"),
    6: ("Port 2 meter ID", "RW", "id"),
    7: ("Company name [1-2]", "RW", "ascii"),
    8: ("Company name [3-4]", "RW", "ascii"),
    9: ("Company name [5-6]", "RW", "ascii"),
    10: ("Company name [7-8]", "RW", "ascii"),
    11: ("Company name [9-10]", "RW", "ascii"),
    12: ("Remaining balance", "R", "s16"),
    13: ("Mains kWh rate", "RW", "u16"),
    14: ("Mains kVAh rate", "RW", "u16"),
    15: ("DG kWh rate", "RW", "u16"),
    16: ("DG kVAh rate", "RW", "u16"),
    17: ("Solar kWh rate", "RW", "u16"),
    18: ("Solar kVAh rate", "RW", "u16"),
    19: ("Low-balance threshold", "RW", "u16"),
    20: ("Low-balance alert", "R", "bool"),
    21: ("Backlight timeout (s, 0 = always on)", "RW", "u16"),
    22: ("Mains consumption kWh total", "RW", "u16"),
    23: ("Mains consumption kVAh total", "RW", "u16"),
    24: ("DG consumption kWh total", "RW", "u16"),
    25: ("DG consumption kVAh total", "RW", "u16"),
    26: ("Solar consumption kWh total", "RW", "u16"),
    27: ("Solar consumption kVAh total", "RW", "u16"),
    28: ("Tamper event count (write 0 to clear)", "RW", "u16"),
    29: ("Billing parameter", "RW", "billing"),
    30: ("All relays disconnected", "R", "bool"),
    31: ("Max load current (A)", "RW", "u16"),
    32: ("Max load delay (s; 0 = immediate)", "RW", "u16"),
    33: ("Relay reconnect delay (min)", "RW", "u16"),
    34: ("Load parameter selection", "RW", "load_parameter"),
    35: ("Consumption after disconnection", "RW", "bool"),
    36: ("RTC year", "R", "u16"),
    37: ("RTC month", "R", "u16"),
    38: ("RTC day", "R", "u16"),
    39: ("RTC hour", "R", "u16"),
    40: ("RTC minute", "R", "u16"),
    41: ("RTC second", "R", "u16"),
    42: ("Last recharge amount", "R", "s16"),
    43: ("Recharge input (signed adjustment)", "RW", "s16"),
    44: ("Total amount deducted", "R", "u16"),
    45: ("RTC synchronization status", "RW", "rtc_sync"),
    46: ("Tamper timestamp valid", "R", "bool"),
    47: ("Tamper timestamp year", "R", "u16"),
    48: ("Tamper timestamp month", "R", "u16"),
    49: ("Tamper timestamp day", "R", "u16"),
    50: ("Tamper timestamp hour", "R", "u16"),
    51: ("Tamper timestamp minute", "R", "u16"),
    52: ("Tamper timestamp second", "R", "u16"),
    53: ("Last recharge timestamp valid", "R", "bool"),
    54: ("Last recharge timestamp year", "R", "u16"),
    55: ("Last recharge timestamp month", "R", "u16"),
    56: ("Last recharge timestamp day", "R", "u16"),
    57: ("Last recharge timestamp hour", "R", "u16"),
    58: ("Last recharge timestamp minute", "R", "u16"),
    59: ("Last recharge timestamp second", "R", "u16"),
    60: ("BMS relay override", "RW", "override"),
    61: ("Port 2 meter communication lost", "R", "bool"),
}
for _off in range(62, CONTROLLER_REG_COUNT):
    REGS[_off] = ("Reserved", "R", "u16")


def to_s16(v):
    return v - 0x10000 if v & 0x8000 else v


def decode_baud(raw):
    return 115200 if raw == 0xFFFF else raw


def encode_baud(baud):
    return 0xFFFF if baud == 115200 else baud


def ascii_pair(raw):
    out = ""
    for b in (raw >> 8, raw & 0xFF):
        out += chr(b) if 32 <= b <= 126 else ("" if b == 0 else "?")
    return out


def display_value(off, raw):
    """Return (value_text, meaning_text) for a controller register."""
    kind = REGS[off][2]
    if kind == "s16":
        return str(to_s16(raw)), ""
    if kind == "baud":
        return str(raw), f"{decode_baud(raw)} baud"
    if kind == "parity":
        return str(raw), PARITY_NAMES.get(raw, "?")
    if kind == "ascii":
        return str(raw), f"'{ascii_pair(raw)}'"
    if kind == "billing":
        return str(raw), BILLING_NAMES.get(raw, "?")
    if kind == "load_parameter":
        return str(raw), LOAD_PARAMETER_NAMES.get(raw, "?")
    if kind == "override":
        return str(raw), OVERRIDE_NAMES.get(raw, "?")
    if off == 0:
        return str(raw), "All disconnected" if raw == 1 else "Not all disconnected"
    if off == 20:
        return str(raw), "ALERT" if raw else "OK"
    return str(raw), ""


def input_hint(off):
    kind = REGS[off][2]
    return {
        "bool": "0 or 1",
        "baud": "Baud rate: " + ", ".join(str(b) for b in BAUDS),
        "parity": "0 = None, 1 = Even, 2 = Odd",
        "id": "1 - 247",
        "ascii": "Up to 2 ASCII characters",
        "s16": "Signed integer -32768 .. 32767",
        "u16": "Unsigned integer 0 .. 65535",
        "billing": "0=kWh 1=kVAh 2=PF 3=kW 4=kVA 5=kVArh",
        "load_parameter": "0 = Current Ch1 through 11 = Current Ch12",
        "override": "0 = Automatic, 1 = BMS override",
        "rtc_sync": "0 = clear, 1 = refresh RTC status",
    }[kind]


def parse_input(off, text):
    """Convert user text to a raw 16-bit register value. Raises ValueError."""
    kind = REGS[off][2]
    text = text.strip()
    if kind == "ascii":
        if len(text) > 2:
            raise ValueError("At most 2 characters per register")
        raw = 0
        padded = text.ljust(2, "\0")
        for ch in padded:
            if ch != "\0" and not 32 <= ord(ch) <= 126:
                raise ValueError("Printable ASCII only")
            raw = (raw << 8) | ord(ch)
        return raw
    try:
        n = int(text, 0)
    except ValueError:
        raise ValueError(f"'{text}' is not a whole number")
    if kind == "bool" and n not in (0, 1):
        raise ValueError("Must be 0 or 1")
    if kind == "baud":
        if n not in BAUDS and n != 0xFFFF:
            raise ValueError("Unsupported baud rate")
        return encode_baud(n)
    if kind == "parity" and n not in PARITY_NAMES:
        raise ValueError("Parity must be 0, 1 or 2")
    if kind == "id" and not 1 <= n <= 247:
        raise ValueError("ID must be 1 - 247")
    if kind == "billing" and n not in BILLING_NAMES:
        raise ValueError("Billing parameter must be 0 - 5")
    if kind == "load_parameter" and n not in LOAD_PARAMETER_NAMES:
        raise ValueError("Load parameter must be 0 - 11")
    if kind in ("override", "rtc_sync") and n not in (0, 1):
        raise ValueError("Must be 0 or 1")
    if kind == "s16":
        if not -32768 <= n <= 32767:
            raise ValueError("Out of signed 16-bit range")
        return n & 0xFFFF
    if not 0 <= n <= 0xFFFF:
        raise ValueError("Out of 16-bit range")
    return n


FLOAT_ORDERS = {
    "ABCD (high word first)": "ABCD",
    "CDAB (word swapped)": "CDAB",
    "BADC (byte swapped)": "BADC",
    "DCBA (fully swapped)": "DCBA",
}


def regs_to_float_num(a, b, order):
    """Decode two 16-bit registers (a = first, b = second) as IEEE-754."""
    a_hi, a_lo, b_hi, b_lo = a >> 8, a & 0xFF, b >> 8, b & 0xFF
    raw = {"ABCD": (a_hi, a_lo, b_hi, b_lo),
           "CDAB": (b_hi, b_lo, a_hi, a_lo),
           "BADC": (a_lo, a_hi, b_lo, b_hi),
           "DCBA": (b_lo, b_hi, a_lo, a_hi)}[order]
    return struct.unpack(">f", bytes(raw))[0]


def regs_to_float(a, b, order):
    v = regs_to_float_num(a, b, order)
    if v != v or v in (float("inf"), float("-inf")):
        return "-"
    if v != 0 and (abs(v) < 1e-3 or abs(v) >= 1e7):
        return f"{v:.3e}"
    return f"{v:.3f}"


# RISH ML14xx: each parameter is a float32 in two registers.
# Parameter number n lives at address (n - 1) * 2  (same for 3X and 4X).
ML14XX = {}


def _ml_add(first, labels, unit):
    for k, label in enumerate(labels):
        ML14XX[first + k] = (label, unit)


_ml_add(1, ["Voltage L1", "Voltage L2", "Voltage L3", "Voltage L12",
            "Voltage L23", "Voltage L31"], "V")
for _first, _name, _unit in ((7, "Current", "A"), (19, "Power", "W"),
                             (31, "VA", "VA"), (43, "VAr", "VAr"),
                             (55, "PF", ""), (67, "Angle", "deg"),
                             (79, "Wh Import", "Wh"), (91, "Wh Export", "Wh"),
                             (103, "VArh Capacitive", "VArh"),
                             (115, "VArh Inductive", "VArh"),
                             (127, "VAh", "VAh"),
                             (139, "Wh Import overflow count", "")):
    _ml_add(_first, [f"{_name} Ch{c}" for c in range(1, 13)], _unit)
ML14XX[735] = ("Frequency", "Hz")
ML14XX[742] = ("Total system active power", "W")
ML14XX[745] = ("Total system PF avg", "")
ML14XX[747] = ("Total system Wh Import", "Wh")


def parse_addr(text):
    """Accept an offset (0..) or a 4xxxx register label."""
    n = int(text.strip(), 0)
    return n - 40001 if n >= 40001 else n


# --------------------------------------------------------------------------
# Background worker (keeps the UI responsive during serial transactions)
# --------------------------------------------------------------------------
class Worker:
    def __init__(self, ui_post):
        self.q = queue.Queue()
        self.ui_post = ui_post
        self.pending = 0
        self.lock = threading.Lock()
        threading.Thread(target=self._run, daemon=True).start()

    def submit(self, fn, ok=None, err=None):
        with self.lock:
            self.pending += 1
        self.q.put((fn, ok, err))

    def _run(self):
        while True:
            fn, ok, err = self.q.get()
            try:
                try:
                    result = fn()
                except Exception as exc:  # reported on the UI thread
                    if err:
                        self.ui_post(err, exc)
                else:
                    if ok:
                        self.ui_post(ok, result)
            finally:
                with self.lock:
                    self.pending -= 1


# --------------------------------------------------------------------------
# Application
# --------------------------------------------------------------------------
class App(tk.Tk):
    def __init__(self):
        super().__init__()
        self.title("PREPAY_V0 Modbus Bridge Tool")
        self.geometry("1100x740")
        self.minsize(960, 640)

        self.ui_q = queue.Queue()
        self.worker = Worker(lambda fn, arg: self.ui_q.put((fn, (arg,))))
        self.bus = ModbusMaster(log=self._log_from_thread)
        self.regs = [0] * CONTROLLER_REG_COUNT
        self.regs_valid = False
        self.prev_tamper = None
        self.cons_dirty = set()
        self.meter_regs = [0] * METER_REG_COUNT
        self.meter_valid = False

        self._build_connection_bar()
        self.nb = ttk.Notebook(self)
        self.nb.pack(fill="both", expand=True, padx=6, pady=(0, 4))
        self._build_dashboard()
        self._build_registers()
        self._build_meter()
        self._build_settings()
        self._build_console()
        self._build_statusbar()

        self.refresh_ports()
        self._update_conn_state()
        self.after(40, self._drain_ui_queue)
        self.after(500, self._poll_tick)
        self.protocol("WM_DELETE_WINDOW", self._on_close)

    # ---- thread plumbing ---------------------------------------------
    def _drain_ui_queue(self):
        try:
            while True:
                fn, args = self.ui_q.get_nowait()
                fn(*args)
        except queue.Empty:
            pass
        self.counters_var.set(
            f"TX {self.bus.tx_count}   OK {self.bus.ok_count}   "
            f"ERR {self.bus.err_count}")
        self.after(40, self._drain_ui_queue)

    def _log_from_thread(self, direction, text):
        self.ui_q.put((self.log, (direction, text)))

    def run(self, fn, ok=None, err=None):
        if not self.bus.is_open:
            self.status("Not connected")
            return False
        self.worker.submit(fn, ok, err or self.on_error_quiet)
        return True

    def on_error_quiet(self, exc):
        self._handle_error(exc, popup=False)

    def on_error_box(self, exc):
        self._handle_error(exc, popup=True)

    def _handle_error(self, exc, popup):
        msg = str(exc) or exc.__class__.__name__
        self.log("ERR", msg)
        self.status("Error: " + msg)
        if isinstance(exc, serial.SerialException):
            self.disconnect()
        if popup:
            messagebox.showerror("Modbus error", msg)

    def status(self, text):
        self.status_var.set(text)

    # ---- connection bar -----------------------------------------------
    def _build_connection_bar(self):
        bar = ttk.LabelFrame(self, text="Connection (RS485 port 1)")
        bar.pack(fill="x", padx=6, pady=6)

        self.port_var = tk.StringVar()
        self.baud_var = tk.StringVar(value="9600")
        self.parity_var = tk.StringVar(value="Even")
        self.id_var = tk.IntVar(value=1)
        self.timeout_var = tk.StringVar(value="0.6")

        ttk.Label(bar, text="Port").grid(row=0, column=0, padx=(8, 2), pady=6)
        self.port_cb = ttk.Combobox(bar, textvariable=self.port_var, width=13)
        self.port_cb.grid(row=0, column=1)
        ttk.Button(bar, text="Refresh", command=self.refresh_ports).grid(
            row=0, column=2, padx=4)
        ttk.Label(bar, text="Baud").grid(row=0, column=3, padx=(8, 2))
        ttk.Combobox(bar, textvariable=self.baud_var, width=8,
                     values=[str(b) for b in BAUDS]).grid(row=0, column=4)
        ttk.Label(bar, text="Parity").grid(row=0, column=5, padx=(10, 2))
        ttk.Combobox(bar, textvariable=self.parity_var, width=6,
                     state="readonly",
                     values=["None", "Even", "Odd"]).grid(row=0, column=6)
        ttk.Label(bar, text="Slave ID").grid(row=0, column=7, padx=(10, 2))
        ttk.Spinbox(bar, from_=1, to=247, width=5,
                    textvariable=self.id_var).grid(row=0, column=8)
        ttk.Label(bar, text="Timeout").grid(row=0, column=9, padx=(10, 2))
        ttk.Entry(bar, textvariable=self.timeout_var, width=5).grid(
            row=0, column=10)
        self.conn_btn = ttk.Button(bar, text="Connect",
                                   command=self.toggle_connection)
        self.conn_btn.grid(row=0, column=11, padx=8)
        self.conn_lbl = tk.Label(bar, text=" DISCONNECTED ", fg="white",
                                 bg="#b00020")
        self.conn_lbl.grid(row=0, column=12, padx=(0, 8))

    def refresh_ports(self):
        ports = [p.device for p in list_ports.comports()]
        self.port_cb["values"] = ports
        if ports and not self.port_var.get():
            self.port_var.set(ports[0])

    def toggle_connection(self):
        if self.bus.is_open:
            self.disconnect()
            return
        try:
            baud = int(self.baud_var.get())
            timeout = float(self.timeout_var.get())
            self.bus.open(self.port_var.get(), baud,
                             self.parity_var.get(), timeout)
        except (ValueError, serial.SerialException) as exc:
            messagebox.showerror("Cannot open port", str(exc))
            return
        self.log("INFO", f"Opened {self.port_var.get()} "
                         f"{baud} {self.parity_var.get()} 8-1")
        self._update_conn_state()
        self.read_all()

    def disconnect(self):
        self.bus.close()
        self.regs_valid = False
        self.prev_tamper = None
        self._update_conn_state()
        self.log("INFO", "Port closed")

    def _update_conn_state(self):
        if self.bus.is_open:
            self.conn_btn.config(text="Disconnect")
            self.conn_lbl.config(text=" CONNECTED ", bg="#1b7f3a")
            self.status("Connected")
        else:
            self.conn_btn.config(text="Connect")
            self.conn_lbl.config(text=" DISCONNECTED ", bg="#b00020")
            self.status("Not connected")

    def slave(self):
        try:
            sid = int(self.id_var.get())
        except (tk.TclError, ValueError):
            sid = 0
        if not 1 <= sid <= 247:
            raise ValueError("Slave ID must be 1 - 247")
        return sid

    def _on_close(self):
        self.bus.close()
        self.destroy()

    # ---- dashboard -----------------------------------------------------
    def _build_dashboard(self):
        tab = ttk.Frame(self.nb)
        self.nb.add(tab, text="Dashboard")
        self.dv = {}  # dashboard variables
        self.dv_lbl = {}  # dashboard label widgets (for colouring)

        def var(name):
            self.dv[name] = tk.StringVar(value="-")
            return self.dv[name]

        top = ttk.Frame(tab)
        top.pack(fill="x", padx=8, pady=8)

        bal = ttk.LabelFrame(top, text="Prepaid balance")
        bal.pack(side="left", fill="y", padx=(0, 8))
        self.balance_lbl = tk.Label(bal, textvariable=var("balance"),
                                    font=("TkDefaultFont", 30, "bold"),
                                    width=8)
        self.balance_lbl.pack(padx=14, pady=(8, 2))
        tk.Label(bal, textvariable=var("alert")).pack(pady=(0, 8))

        st = ttk.LabelFrame(top, text="Status")
        st.pack(side="left", fill="both", expand=True, padx=(0, 8))
        rows = [("Company", "company"), ("Relays", "relays"),
                ("Tamper events", "tamper"),
                ("Last tamper event", "tamper_last"),
                ("Consumption after disconnect", "cad"),
                ("Device RTC", "rtc"), ("Billing parameter", "billing"),
                ("Selected consumption", "selected_consumption"),
                ("Selected unit rates", "selected_rates"),
                ("Load parameter", "load_parameter"),
                ("Port 2 communication", "port2_comm"),
                ("BMS relay override", "override")]
        for i, (label, key) in enumerate(rows):
            ttk.Label(st, text=label + ":").grid(row=i, column=0,
                                                  sticky="e", padx=6, pady=1)
            lbl = ttk.Label(st, textvariable=var(key))
            lbl.grid(row=i, column=1, sticky="w", padx=6)
            self.dv_lbl[key] = lbl

        amt = ttk.LabelFrame(top, text="Amounts")
        amt.pack(side="left", fill="both", expand=True)
        rows = [("Total amount deducted", "deducted"),
                ("Last recharge", "last_recharge"),
                ("Last recharge time", "recharge_time"),
                ("Low-balance threshold", "threshold")]
        for i, (label, key) in enumerate(rows):
            ttk.Label(amt, text=label + ":").grid(row=i, column=0,
                                                   sticky="e", padx=6, pady=1)
            ttk.Label(amt, textvariable=var(key)).grid(row=i, column=1,
                                                       sticky="w", padx=6)

        mid = ttk.Frame(tab)
        mid.pack(fill="x", padx=8, pady=4)

        cons = ttk.LabelFrame(
            mid, text="Cumulative consumption (40023-40028) - editable")
        cons.pack(side="left", fill="y", padx=(0, 8))
        self.cons_vars = {}
        self.cons_entries = {}
        for c, h in enumerate(("", "kWh", "kVAh")):
            ttk.Label(cons, text=h, font=("TkDefaultFont", 9, "bold")).grid(
                row=0, column=c, padx=10, pady=2)
        for r, (name, off_kwh, off_kvah) in enumerate((
                ("Mains", 22, 23), ("DG", 24, 25), ("Solar", 26, 27)),
                start=1):
            ttk.Label(cons, text=name).grid(row=r, column=0, padx=10)
            for col, off in ((1, off_kwh), (2, off_kvah)):
                v = tk.StringVar(value="-")
                self.cons_vars[off] = v
                e = ttk.Entry(cons, textvariable=v, width=8, justify="right")
                e.grid(row=r, column=col, padx=4, pady=1)
                e.bind("<KeyRelease>", lambda ev, o=off: self._cons_edited(o))
                e.bind("<Return>", lambda ev: self.write_consumption())
                self.cons_entries[off] = e
        cb = ttk.Frame(cons)
        cb.grid(row=4, column=0, columnspan=3, pady=(4, 4))
        ttk.Button(cb, text="Write changes",
                   command=self.write_consumption).pack(side="left", padx=4)
        ttk.Button(cb, text="Reload", command=self.reload_consumption).pack(
            side="left", padx=4)

        act = ttk.LabelFrame(mid, text="Actions")
        act.pack(side="left", fill="both", expand=True)
        for c in range(3):
            act.columnconfigure(c, weight=1, uniform="act")
        ttk.Button(act, text="Disconnect relays",
                   command=lambda: self.relay_cmd(1)).grid(
            row=0, column=0, padx=6, pady=5, sticky="ew")
        ttk.Button(act, text="Connect relays",
                   command=lambda: self.relay_cmd(0)).grid(
            row=0, column=1, padx=6, pady=5, sticky="ew")
        ttk.Button(act, text="Clear tamper count",
                   command=lambda: self.write_and_refresh(
                       28, 0, "Tamper count cleared "
                       "(only works while SW4 is secure)")).grid(
            row=0, column=2, padx=6, pady=5, sticky="ew")
        ttk.Button(act, text="Ack disconnect usage",
                   command=lambda: self.write_and_refresh(
                       35, 0, "Consumption-after-disconnect flag "
                       "acknowledged")).grid(
            row=1, column=0, padx=6, pady=5, sticky="ew")
        ttk.Button(act, text="Reset balance to 0",
                   command=self.reset_balance).grid(
            row=1, column=1, padx=6, pady=5, sticky="ew")
        ttk.Button(act, text="Sync RTC to PC time",
                   command=self.sync_rtc).grid(
            row=1, column=2, padx=6, pady=5, sticky="ew")
        rc = ttk.Frame(act)
        rc.grid(row=2, column=0, columnspan=3, sticky="w", padx=6, pady=5)
        ttk.Label(rc, text="Recharge adjustment (+/-):").pack(side="left")
        self.recharge_var = tk.StringVar(value="100")
        ttk.Entry(rc, textvariable=self.recharge_var, width=10).pack(
            side="left", padx=6)
        ttk.Button(rc, text="Apply recharge (40044)",
                   command=self.apply_recharge).pack(side="left")

        poll = ttk.Frame(tab)
        poll.pack(fill="x", padx=8, pady=8)
        ttk.Button(poll, text="Read now", command=self.read_all).pack(
            side="left")
        self.auto_var = tk.BooleanVar(value=True)
        ttk.Checkbutton(poll, text="Auto poll every", variable=self.auto_var
                        ).pack(side="left", padx=(14, 2))
        self.interval_var = tk.StringVar(value="1000")
        ttk.Entry(poll, textvariable=self.interval_var, width=6).pack(
            side="left")
        ttk.Label(poll, text="ms").pack(side="left", padx=(2, 14))
        self.poll_meter_var = tk.BooleanVar(value=True)
        ttk.Checkbutton(poll, text="Read meter data (40201-40250)",
                        variable=self.poll_meter_var).pack(side="left")
        self.last_read_var = tk.StringVar(value="")
        ttk.Label(poll, textvariable=self.last_read_var).pack(side="right")
        self._build_meter_panel(tab, before=poll)

    def _build_meter_panel(self, parent, before):
        box = ttk.LabelFrame(parent, text="Energy meter (RISH ML1400) - live")
        box.pack(fill="x", padx=8, pady=4, before=before)
        self.mv = {}
        self.meter_time_var = tk.StringVar(value="Meter not read yet")
        bold = ("TkDefaultFont", 11, "bold")

        def var(name):
            self.mv[name] = tk.StringVar(value="-")
            return self.mv[name]

        top = ttk.Frame(box)
        top.pack(fill="x", padx=6, pady=(6, 2))
        for col, (label, key) in enumerate((
                ("L1-N", "Voltage L1"), ("L2-N", "Voltage L2"),
                ("L3-N", "Voltage L3"), ("L1-L2", "Voltage L12"),
                ("L2-L3", "Voltage L23"), ("L3-L1", "Voltage L31"))):
            ttk.Label(top, text=label).grid(row=0, column=col * 2,
                                            sticky="e", padx=(12, 3))
            ttk.Label(top, textvariable=var(key), font=bold, width=9,
                      anchor="w").grid(row=0, column=col * 2 + 1)

        grid = ttk.Frame(box)
        grid.pack(fill="x", padx=6, pady=(2, 6))
        ttk.Label(grid, text="Channel", font=("TkDefaultFont", 9, "bold")
                  ).grid(row=0, column=0, sticky="w", padx=4)
        ttk.Label(grid, text="Current (A)").grid(row=1, column=0,
                                                 sticky="w", padx=4)
        ttk.Label(grid, text="Power (W)").grid(row=2, column=0,
                                               sticky="w", padx=4)
        for c in range(1, 13):
            grid.columnconfigure(c, weight=1, uniform="ch")
            ttk.Label(grid, text=str(c), font=("TkDefaultFont", 9, "bold")
                      ).grid(row=0, column=c)
            ttk.Label(grid, textvariable=var(f"Current Ch{c}")).grid(
                row=1, column=c)
            ttk.Label(grid, textvariable=var(f"Power Ch{c}")).grid(
                row=2, column=c)
        ttk.Label(box, textvariable=self.meter_time_var).pack(
            anchor="e", padx=8, pady=(0, 4))

    def meter_values(self):
        """name -> float for every ML14xx parameter present in the mirror."""
        out = {}
        order = FLOAT_ORDERS[self.float_order_var.get()]
        start = self.meter_start()
        for i in range(len(self.meter_regs) - 1):
            addr = start + i
            if addr % 2:
                continue
            info = ML14XX.get(addr // 2 + 1)
            if info:
                out[info[0]] = regs_to_float_num(
                    self.meter_regs[i], self.meter_regs[i + 1], order)
        return out

    def update_meter_dashboard(self):
        if not self.meter_valid:
            return
        vals = self.meter_values()
        for name, var in self.mv.items():
            v = vals.get(name)
            if v is None or v != v or abs(v) == float("inf"):
                var.set("-")
            elif name.startswith("Voltage"):
                var.set(f"{v:.1f} V")
            elif name.startswith("Current"):
                var.set(f"{v:.3f}")
            else:
                var.set(f"{v:.1f}")

    def update_dashboard(self):
        r = self.regs
        company = "".join(ascii_pair(r[o]) for o in range(7, 12))
        bal = to_s16(r[12])
        self.dv["balance"].set(str(bal))
        alert = r[20] == 1
        self.balance_lbl.config(fg="#b00020" if (bal < 0 or alert)
                                else "#0a7d2c")
        self.dv["alert"].set("LOW BALANCE ALERT" if alert else "Balance OK")
        self.dv["company"].set(company or "(blank)")
        self.dv["relays"].set("All disconnected" if r[0] == 1 else
                              "Connected / not all disconnected")
        self.dv["tamper"].set(str(r[28]))
        self._track_tamper(r[28])
        self.dv["tamper_last"].set(self._saved_timestamp(r, 46, 47))
        self.dv["cad"].set("YES - acknowledge needed" if r[35] else "No")
        self.dv["rtc"].set(
            f"{r[38]:02d}/{r[37]:02d}/{r[36]:04d}  "
            f"{r[39]:02d}:{r[40]:02d}:{r[41]:02d}" + self._rtc_diff_text(r))
        self.dv["billing"].set(BILLING_NAMES.get(r[29], str(r[29])))
        self.dv["selected_consumption"].set(
            self._selected_billing_values(r, consumption=True))
        self.dv["selected_rates"].set(
            self._selected_billing_values(r, consumption=False))
        self.dv["load_parameter"].set(
            LOAD_PARAMETER_NAMES.get(r[34], str(r[34])))
        self.dv["port2_comm"].set(
            "LOST" if r[61] else "OK")
        self.dv["override"].set(OVERRIDE_NAMES.get(r[60], str(r[60])))
        self.dv["deducted"].set(str(r[44]))
        self.dv["last_recharge"].set(str(to_s16(r[42])))
        self.dv["recharge_time"].set(self._saved_timestamp(r, 53, 54))
        self.dv["threshold"].set(str(r[19]))
        for o in range(22, 28):
            if o not in self.cons_dirty:
                self.cons_vars[o].set(str(r[o]))

    @staticmethod
    def _saved_timestamp(regs, valid_offset, first_field_offset):
        if regs[valid_offset] != 1:
            return "Not available"
        try:
            return datetime.datetime(
                regs[first_field_offset], regs[first_field_offset + 1],
                regs[first_field_offset + 2], regs[first_field_offset + 3],
                regs[first_field_offset + 4], regs[first_field_offset + 5]
            ).strftime("%d/%m/%Y %H:%M:%S")
        except ValueError:
            return "Invalid saved time"

    @staticmethod
    def _selected_billing_values(regs, consumption):
        billing = regs[29]
        if billing == 0:
            offsets = (22, 24, 26) if consumption else (13, 15, 17)
            unit = "kWh" if consumption else "per kWh"
        elif billing == 1:
            offsets = (23, 25, 27) if consumption else (14, 16, 18)
            unit = "kVAh" if consumption else "per kVAh"
        else:
            return "Not available for this billing parameter"
        return (f"Mains {regs[offsets[0]]}, DG {regs[offsets[1]]}, "
                f"Solar {regs[offsets[2]]} {unit}")

    # ---- tamper / RTC / consumption helpers ------------------------------
    def _track_tamper(self, count):
        lbl = self.dv_lbl["tamper"]
        lbl.configure(foreground="#b00020" if count else "#0a7d2c")
        prev = self.prev_tamper
        stamp = datetime.datetime.now().strftime("%H:%M:%S")
        if prev is None:
            if count:
                self.dv["tamper_last"].set(f"count {count} (before connect)")
            else:
                self.dv["tamper_last"].set("none")
        elif count != prev:
            if count == 0:
                self.dv["tamper_last"].set(f"cleared at {stamp}")
                self.log("INFO", "Tamper count cleared")
            else:
                self.dv["tamper_last"].set(f"event #{count} at {stamp}")
                self.log("INFO", f"TAMPER event #{count} detected")
                self.status(f"TAMPER event #{count} detected at {stamp}")
                self.bell()
        self.prev_tamper = count

    def _rtc_diff_text(self, r):
        try:
            dev = datetime.datetime(r[36], r[37], r[38], r[39], r[40], r[41])
        except ValueError:
            return "   (RTC not valid)"
        diff = (dev - datetime.datetime.now()).total_seconds()
        if abs(diff) <= 2:
            return "   (in sync with PC)"
        if abs(diff) < 120:
            return f"   ({diff:+.0f} s vs PC)"
        minutes = abs(diff) / 60
        if minutes < 120:
            return f"   (differs from PC by {minutes:.0f} min)"
        if minutes < 2880:
            return f"   (differs from PC by {minutes / 60:.0f} h)"
        return f"   (differs from PC by {minutes / 1440:.0f} days)"

    def sync_rtc(self):
        try:
            sid = self.slave()
        except ValueError as exc:
            messagebox.showerror("Slave ID", str(exc))
            return
        now = datetime.datetime.now()
        if not messagebox.askyesno(
                "Sync RTC",
                f"Set the board RTC to the PC time?\n\n"
                f"{now:%d/%m/%Y %H:%M:%S}"):
            return

        def job():
            t = datetime.datetime.now()
            self.bus.write_multiple(sid, RTC_FIRST, [
                t.year, t.month, t.day, t.hour, t.minute, t.second])
            return t, self.bus.read_holding(sid, 0, CONTROLLER_REG_COUNT)

        def ok(result):
            t, vals = result
            self.regs, self.regs_valid = vals, True
            self.refresh_views()
            msg = f"RTC synchronised to PC time {t:%d/%m/%Y %H:%M:%S}"
            self.status(msg)
            self.log("INFO", msg)

        def err(exc):
            if isinstance(exc, ModbusException) and exc.code == 3:
                text = ("The board rejected the date/time (exception 03).\n\n"
                        "If your firmware is the original version it does not "
                        "accept RTC writes yet: apply "
                        "firmware_rtc_patch/rtc_sync.patch, rebuild and flash.")
            elif isinstance(exc, ModbusException) and exc.code == 6:
                text = ("The board's LCD RTC editor is open. Finish or save "
                        "the edit on the board, then try again.")
            elif isinstance(exc, ModbusException) and exc.code == 4:
                text = "The board could not write its RTC chip (I2C fault)."
            else:
                self.on_error_box(exc)
                return
            self.log("ERR", text.splitlines()[0])
            self.status("RTC sync failed")
            messagebox.showerror("RTC sync failed", text)

        self.run(job, ok, err)

    def _cons_edited(self, off):
        shown = self.cons_vars[off].get().strip()
        if shown == str(self.regs[off]):
            self.cons_dirty.discard(off)
            self.cons_entries[off].configure(foreground="")
        else:
            self.cons_dirty.add(off)
            self.cons_entries[off].configure(foreground="#b36b00")

    def reload_consumption(self):
        self.cons_dirty.clear()
        for off, entry in self.cons_entries.items():
            entry.configure(foreground="")
            self.cons_vars[off].set(str(self.regs[off]))

    def write_consumption(self):
        if not self.cons_dirty:
            self.status("No consumption changes to write")
            return
        writes = []
        try:
            sid = self.slave()
            for off in sorted(self.cons_dirty):
                writes.append((off, parse_input(
                    off, self.cons_vars[off].get())))
        except ValueError as exc:
            messagebox.showerror("Invalid value", str(exc))
            return
        names = {22: "Mains kWh", 23: "Mains kVAh", 24: "DG kWh",
                 25: "DG kVAh", 26: "Solar kWh", 27: "Solar kVAh"}
        summary = "\n".join(f"{names[o]} ({40001 + o}):  "
                            f"{self.regs[o]}  ->  {v}" for o, v in writes)
        if not messagebox.askyesno(
                "Write consumption",
                summary + "\n\nThe board recalculates the balance from the "
                "consumption totals and rates. Continue?"):
            return

        def job():
            for off, raw in writes:
                try:
                    self.bus.write_single(sid, off, raw)
                except ModbusError as exc:
                    raise ModbusError(f"Register {40001 + off}: {exc}")
            return self.bus.read_holding(sid, 0, CONTROLLER_REG_COUNT)

        def ok(vals):
            self.cons_dirty.clear()
            for entry in self.cons_entries.values():
                entry.configure(foreground="")
            self.regs, self.regs_valid = vals, True
            self.refresh_views()
            self.status(f"{len(writes)} consumption value(s) written")
            self.log("INFO", "Consumption written: " + ", ".join(
                f"{40001 + o}={v}" for o, v in writes))

        def err(exc):
            self.reload_consumption()   # re-sync fields with the device
            self.on_error_box(exc)

        self.run(job, ok, err)

    def relay_cmd(self, value):
        if value == 1 and not messagebox.askyesno(
                "Disconnect", "Disconnect all three relays?"):
            return
        text = ("All relays disconnect command sent" if value else
                "Connect command sent (balance control may refuse it)")
        self.write_and_refresh(0, value, text)

    def apply_recharge(self):
        try:
            n = int(self.recharge_var.get(), 0)
            raw = parse_input(43, str(n))
        except ValueError as exc:
            messagebox.showerror("Invalid value", str(exc))
            return
        self.write_and_refresh(43, raw, f"Recharge adjustment {n} applied")

    def reset_balance(self):
        if messagebox.askyesno(
                "Reset balance",
                "Write 0 to 40044?\n\nThis resets the balance to zero "
                "(cumulative consumption is kept)."):
            self.write_and_refresh(43, 0, "Balance reset to zero")

    # ---- generic register write ----------------------------------------
    def write_and_refresh(self, offset, raw, message=None):
        try:
            sid = self.slave()
        except ValueError as exc:
            messagebox.showerror("Slave ID", str(exc))
            return

        def job():
            self._write_one(sid, offset, raw)
            # a new controller ID takes effect immediately
            read_sid = raw if offset == 3 else sid
            return self.bus.read_holding(read_sid, 0, CONTROLLER_REG_COUNT)

        def ok(values):
            self.regs = values
            self.regs_valid = True
            self.refresh_views()
            self.status(message or f"Wrote {raw} to register {40001 + offset}")
            self.log("INFO", f"Wrote 0x{raw:04X} to {40001 + offset}")
            self._after_port1_write(offset, raw)

        self.run(job, ok, self.on_error_box)

    def _write_one(self, sid, offset, raw):
        """Worker-thread write; handles our own port-1 line changes."""
        self.bus.write_single(sid, offset, raw)
        if offset == 1:
            self.bus.set_serial(decode_baud(raw), self.parity_var.get())
        elif offset == 2:
            self.bus.set_serial(self.bus.ser.baudrate,
                                   PARITY_NAMES.get(raw, "Even"))

    def _after_port1_write(self, offset, raw):
        """UI-thread bookkeeping when port 1 settings were changed."""
        if offset == 1:
            self.baud_var.set(str(decode_baud(raw)))
            self.log("INFO", f"PC port switched to {decode_baud(raw)} baud")
        elif offset == 2:
            self.parity_var.set(PARITY_NAMES.get(raw, "Even"))
            self.log("INFO", f"PC port parity set to "
                             f"{PARITY_NAMES.get(raw, '?')}")
        elif offset == 3:
            self.id_var.set(raw)
            self.log("INFO", f"Slave ID set to {raw}")

    # ---- reading ---------------------------------------------------------
    def read_all(self, quiet=False):
        try:
            sid = self.slave()
        except ValueError as exc:
            if not quiet:
                messagebox.showerror("Slave ID", str(exc))
            return
        with_meter = self.poll_meter_var.get()

        def job():
            vals = self.bus.read_holding(sid, 0, CONTROLLER_REG_COUNT)
            meter = None
            if with_meter:
                meter = self.bus.read_holding(sid, METER_BASE, METER_REG_COUNT)
            return vals, meter

        def ok(result):
            vals, meter = result
            self.regs, self.regs_valid = vals, True
            if meter is not None:
                self.meter_regs, self.meter_valid = meter, True
                self.meter_time_var.set(
                    "Meter updated " +
                    datetime.datetime.now().strftime("%H:%M:%S"))
            self.status("Registers read OK")
            self.refresh_views()   # may replace the status with a tamper alert
            self.last_read_var.set("Last read: " +
                                   datetime.datetime.now().strftime("%H:%M:%S"))

        self.run(job, ok, self.on_error_quiet if quiet else self.on_error_box)

    def _poll_tick(self):
        try:
            interval = max(300, int(self.interval_var.get()))
        except ValueError:
            interval = 2000
        if (self.auto_var.get() and self.bus.is_open
                and self.worker.pending == 0):
            self.read_all(quiet=True)
        self.after(interval, self._poll_tick)

    def refresh_views(self):
        self.update_dashboard()
        self.update_register_table()
        self.update_meter_table()
        self.update_meter_dashboard()

    # ---- registers tab -----------------------------------------------------
    def _build_registers(self):
        tab = ttk.Frame(self.nb)
        self.nb.add(tab, text="Registers 40001-40050")
        bar = ttk.Frame(tab)
        bar.pack(fill="x", padx=8, pady=6)
        ttk.Button(bar, text="Read all", command=self.read_all).pack(
            side="left")
        ttk.Label(bar, text="Double-click a writable row to edit it "
                            "(function 06).").pack(side="left", padx=12)

        cols = ("reg", "off", "name", "value", "meaning", "acc")
        self.tree = ttk.Treeview(tab, columns=cols, show="headings",
                                 selectmode="browse")
        for c, text, w, anchor in (
                ("reg", "Register", 70, "center"),
                ("off", "Offset", 55, "center"),
                ("name", "Name", 300, "w"),
                ("value", "Value", 90, "e"),
                ("meaning", "Meaning", 230, "w"),
                ("acc", "Access", 60, "center")):
            self.tree.heading(c, text=text)
            self.tree.column(c, width=w, anchor=anchor)
        sb = ttk.Scrollbar(tab, orient="vertical", command=self.tree.yview)
        self.tree.configure(yscrollcommand=sb.set)
        self.tree.pack(side="left", fill="both", expand=True, padx=(8, 0),
                       pady=(0, 8))
        sb.pack(side="left", fill="y", pady=(0, 8), padx=(0, 8))
        self.tree.tag_configure("ro", foreground="#707070")
        for off in range(CONTROLLER_REG_COUNT):
            name, acc, _ = REGS[off]
            self.tree.insert("", "end", iid=str(off),
                             values=(40001 + off, off, name, "", "", acc),
                             tags=("ro",) if acc == "R" else ())
        self.tree.bind("<Double-1>", self.edit_register)

    def update_register_table(self):
        for off in range(CONTROLLER_REG_COUNT):
            val, meaning = display_value(off, self.regs[off])
            name, acc, _ = REGS[off]
            self.tree.item(str(off), values=(40001 + off, off, name, val,
                                             meaning, acc))

    def edit_register(self, event):
        iid = self.tree.identify_row(event.y)
        if not iid:
            return
        off = int(iid)
        name, acc, _ = REGS[off]
        if acc == "R":
            self.status(f"{40001 + off} is read-only")
            return
        cur = display_value(off, self.regs[off])[0]
        if REGS[off][2] == "ascii":
            cur = ascii_pair(self.regs[off])
        if REGS[off][2] == "baud":
            cur = str(decode_baud(self.regs[off]))
        text = simpledialog.askstring(
            f"Write {40001 + off}",
            f"{name}\n{input_hint(off)}\n\nNew value:",
            initialvalue=cur, parent=self)
        if text is None:
            return
        try:
            raw = parse_input(off, text)
        except ValueError as exc:
            messagebox.showerror("Invalid value", str(exc))
            return
        self.write_and_refresh(off, raw)

    # ---- meter tab -----------------------------------------------------------
    def _build_meter(self):
        tab = ttk.Frame(self.nb)
        self.nb.add(tab, text="Meter data 40201-40250")
        bar = ttk.Frame(tab)
        bar.pack(fill="x", padx=8, pady=6)
        ttk.Button(bar, text="Read meter data", command=self.read_meter
                   ).pack(side="left")
        ttk.Button(bar, text="Save CSV...", command=self.save_meter_csv
                   ).pack(side="left", padx=8)
        ttk.Label(bar, text="Float32 order:").pack(side="left", padx=(14, 2))
        self.float_order_var = tk.StringVar(value="ABCD (high word first)")
        fo = ttk.Combobox(bar, textvariable=self.float_order_var,
                          state="readonly", width=24,
                          values=list(FLOAT_ORDERS))
        fo.pack(side="left")
        fo.bind("<<ComboboxSelected>>", lambda e: self.update_meter_table())
        ttk.Label(bar, text="Meter start address:").pack(side="left",
                                                          padx=(14, 2))
        self.meter_start_var = tk.StringVar(value="0")
        ent = ttk.Entry(bar, textvariable=self.meter_start_var, width=6)
        ent.pack(side="left")
        ent.bind("<Return>", lambda e: self.update_meter_table())
        ent.bind("<FocusOut>", lambda e: self.update_meter_table())
        ttk.Label(bar, text="(= METER_REGISTER_START in firmware)"
                  ).pack(side="left", padx=4)
        cols = ("reg", "off", "param", "flt", "u16", "hex")
        self.mtree = ttk.Treeview(tab, columns=cols, show="headings")
        for c, text, w, anc in (("reg", "Register", 80, "center"),
                                ("off", "Offset", 60, "center"),
                                ("param", "Meter parameter", 230, "w"),
                                ("flt", "Value (float32 = this + next reg)",
                                 230, "w"),
                                ("u16", "Raw", 80, "center"),
                                ("hex", "Hex", 80, "center")):
            self.mtree.heading(c, text=text)
            self.mtree.column(c, width=w, anchor=anc)
        sb = ttk.Scrollbar(tab, orient="vertical", command=self.mtree.yview)
        self.mtree.configure(yscrollcommand=sb.set)
        self.mtree.pack(side="left", fill="both", expand=True, padx=(8, 0),
                        pady=(0, 8))
        sb.pack(side="left", fill="y", pady=(0, 8), padx=(0, 8))
        for i in range(METER_REG_COUNT):
            self.mtree.insert("", "end", iid=str(i),
                              values=(40201 + i, METER_BASE + i, "", "", "", ""))

    def read_meter(self):
        try:
            sid = self.slave()
        except ValueError as exc:
            messagebox.showerror("Slave ID", str(exc))
            return

        def ok(vals):
            self.meter_regs, self.meter_valid = vals, True
            self.update_meter_table()
            self.status("Meter data read OK")

        self.run(lambda: self.bus.read_holding(sid, METER_BASE, METER_REG_COUNT),
                 ok, self.on_error_box)

    def meter_start(self):
        try:
            return int(self.meter_start_var.get(), 0)
        except ValueError:
            return 0

    def meter_row(self, i):
        """(parameter text, value text) for mirror register i."""
        if i + 1 >= len(self.meter_regs):
            return "", ""
        addr = self.meter_start() + i
        flt = regs_to_float(self.meter_regs[i], self.meter_regs[i + 1],
                            FLOAT_ORDERS[self.float_order_var.get()])
        if addr % 2:
            return "   (2nd word)", ""
        info = ML14XX.get(addr // 2 + 1)
        if info:
            name, unit = info
            return name, f"{flt} {unit}".strip()
        return "", flt

    def update_meter_table(self):
        if not self.meter_valid:
            return
        for i, v in enumerate(self.meter_regs):
            param, flt = self.meter_row(i)
            self.mtree.item(str(i), values=(40201 + i, METER_BASE + i, param,
                                            flt, v, f"0x{v:04X}"))

    def save_meter_csv(self):
        if not self.meter_valid:
            messagebox.showinfo("No data", "Read the meter data first.")
            return
        path = filedialog.asksaveasfilename(
            defaultextension=".csv", filetypes=[("CSV", "*.csv")],
            initialfile="meter_data.csv")
        if not path:
            return
        with open(path, "w", newline="") as f:
            w = csv.writer(f)
            w.writerow(["register", "offset", "parameter", "float32 value",
                        "raw", "hex"])
            for i, v in enumerate(self.meter_regs):
                param, flt = self.meter_row(i)
                w.writerow([40201 + i, METER_BASE + i, param, flt, v,
                            f"0x{v:04X}"])
        self.status(f"Saved {path}")

    # ---- settings tab ----------------------------------------------------------
    def _build_settings(self):
        tab = ttk.Frame(self.nb)
        self.nb.add(tab, text="Settings")
        self.sv = {}

        # serial ports of the bridge
        comm = ttk.LabelFrame(tab, text="Bridge communication settings "
                                        "(saved in EEPROM by the device)")
        comm.pack(fill="x", padx=8, pady=8)
        self.port_ctl = {}
        for col, (title, base, idlabel) in enumerate((
                ("Port 1 - controller slave (this link)", 1, "Controller ID"),
                ("Port 2 - energy meter", 4, "Meter ID"))):
            f = ttk.LabelFrame(comm, text=title)
            f.grid(row=0, column=col, padx=8, pady=6, sticky="nsew")
            comm.columnconfigure(col, weight=1)
            b = tk.StringVar(value="9600")
            p = tk.StringVar(value="Even")
            i = tk.StringVar(value="1")
            self.port_ctl[base] = (b, p, i)
            ttk.Label(f, text="Baud").grid(row=0, column=0, padx=6, pady=3,
                                           sticky="e")
            ttk.Combobox(f, textvariable=b, width=8,
                         values=[str(x) for x in BAUDS]).grid(row=0, column=1)
            ttk.Label(f, text="Parity").grid(row=1, column=0, padx=6, pady=3,
                                             sticky="e")
            ttk.Combobox(f, textvariable=p, width=8, state="readonly",
                         values=["None", "Even", "Odd"]).grid(row=1, column=1)
            ttk.Label(f, text=idlabel).grid(row=2, column=0, padx=6, pady=3,
                                            sticky="e")
            ttk.Spinbox(f, from_=1, to=247, width=6, textvariable=i).grid(
                row=2, column=1)
            btns = ttk.Frame(f)
            btns.grid(row=3, column=0, columnspan=2, pady=6)
            ttk.Button(btns, text="Read",
                       command=lambda bs=base: self.read_comm(bs)).pack(
                side="left", padx=4)
            ttk.Button(btns, text="Write",
                       command=lambda bs=base: self.write_comm(bs)).pack(
                side="left", padx=4)
        ttk.Label(comm, text="Writing port 1 changes the link you are using: "
                             "the tool switches its own port to the new "
                             "settings after the reply and verifies them."
                  ).grid(row=1, column=0, columnspan=2, padx=8, pady=(0, 6),
                         sticky="w")

        # company name
        cn = ttk.LabelFrame(tab, text="Company name / customer ID "
                                      "(40008-40012, max 10 chars)")
        cn.pack(fill="x", padx=8, pady=4)
        self.company_var = tk.StringVar()
        ttk.Entry(cn, textvariable=self.company_var, width=16).pack(
            side="left", padx=8, pady=6)
        ttk.Button(cn, text="Read", command=self.read_company).pack(
            side="left", padx=4)
        ttk.Button(cn, text="Write", command=self.write_company).pack(
            side="left", padx=4)

        # tariffs & limits
        tl = ttk.LabelFrame(tab, text="Tariffs, limits and modes")
        tl.pack(fill="x", padx=8, pady=8)
        self.tl_fields = [
            (13, "Mains kWh rate"), (14, "Mains kVAh rate"),
            (15, "DG kWh rate"), (16, "DG kVAh rate"),
            (17, "Solar kWh rate"), (18, "Solar kVAh rate"),
            (19, "Low-balance threshold"), (21, "Backlight timeout (s)"),
            (31, "Max load current (A)"),
            (32, "Overload delay (s; 0 = immediate)"),
            (33, "Reconnect delay (min)")]
        self.tl_vars = {}
        for n, (off, label) in enumerate(self.tl_fields):
            r, c = divmod(n, 2)
            ttk.Label(tl, text=f"{label} ({40001 + off})").grid(
                row=r, column=c * 2, sticky="e", padx=6, pady=3)
            v = tk.StringVar()
            self.tl_vars[off] = v
            ttk.Entry(tl, textvariable=v, width=10).grid(
                row=r, column=c * 2 + 1, sticky="w", padx=6)
        r = (len(self.tl_fields) + 1) // 2
        self.billing_var = tk.StringVar()
        self.load_parameter_var = tk.StringVar()
        self.override_var = tk.StringVar()
        for k, (label, v, names, off) in enumerate((
                ("Billing parameter (40030)", self.billing_var,
                 BILLING_NAMES, 29),
                ("Load parameter (40035)", self.load_parameter_var,
                 LOAD_PARAMETER_NAMES, 34),
                ("BMS relay override (40061)", self.override_var,
                 OVERRIDE_NAMES, 60))):
            ttk.Label(tl, text=label).grid(row=r + k, column=0, sticky="e",
                                           padx=6, pady=3)
            ttk.Combobox(tl, textvariable=v, state="readonly", width=28,
                         values=list(names.values())).grid(
                row=r + k, column=1, columnspan=3, sticky="w", padx=6)
        btn = ttk.Frame(tl)
        btn.grid(row=r + 3, column=0, columnspan=4, pady=8)
        ttk.Button(btn, text="Read from device",
                   command=self.read_tariffs).pack(side="left", padx=6)
        ttk.Button(btn, text="Write all", command=self.write_tariffs).pack(
            side="left", padx=6)

    def read_comm(self, base):
        try:
            sid = self.slave()
        except ValueError as exc:
            messagebox.showerror("Slave ID", str(exc))
            return

        def ok(vals):
            b, p, i = self.port_ctl[base]
            b.set(str(decode_baud(vals[0])))
            p.set(PARITY_NAMES.get(vals[1], "Even"))
            i.set(str(vals[2]))
            self.status("Port settings read")

        self.run(lambda: self.bus.read_holding(sid, base, 3), ok,
                 self.on_error_box)

    def write_comm(self, base):
        b, p, i = self.port_ctl[base]
        try:
            sid = self.slave()
            baud = int(b.get())
            if baud not in BAUDS:
                raise ValueError("Unsupported baud rate")
            parity = p.get()
            new_id = int(i.get())
            if not 1 <= new_id <= 247:
                raise ValueError("ID must be 1 - 247")
        except ValueError as exc:
            messagebox.showerror("Invalid value", str(exc))
            return
        if base == 1 and not messagebox.askyesno(
                "Change port 1",
                f"Change controller link to {baud} baud, {parity}, "
                f"ID {new_id}?\n\nThe tool will follow the new settings."):
            return
        values = [encode_baud(baud), PARITY_FROM_NAME[parity], new_id]

        def job():
            self.bus.write_multiple(sid, base, values)
            if base != 1:
                return None
            # device applies the new line settings after the reply
            self.bus.set_serial(baud, parity)
            import time
            time.sleep(0.25)
            try:
                self.bus.read_holding(new_id, 1, 6)
            except ModbusError as exc:
                raise ModbusError("Settings written, but the device did not "
                                  f"answer at the new settings: {exc}")
            return new_id

        def ok(result):
            if base == 1:
                self.baud_var.set(str(baud))
                self.parity_var.set(parity)
                self.id_var.set(new_id)
                self.status("Port 1 settings changed and verified")
                self.log("INFO", "Port 1 settings changed and verified")
                self.read_all(quiet=True)
            else:
                self.status("Port 2 settings written")

        self.run(job, ok, self.on_error_box)

    def read_company(self):
        try:
            sid = self.slave()
        except ValueError as exc:
            messagebox.showerror("Slave ID", str(exc))
            return

        def ok(vals):
            self.company_var.set("".join(ascii_pair(v) for v in vals))
            self.status("Company name read")

        self.run(lambda: self.bus.read_holding(sid, 7, 5), ok,
                 self.on_error_box)

    def write_company(self):
        text = self.company_var.get()
        if len(text) > 10 or any(not 32 <= ord(c) <= 126 for c in text):
            messagebox.showerror("Invalid value",
                                 "Up to 10 printable ASCII characters.")
            return
        padded = text.ljust(10, "\0")
        values = [(ord(padded[i]) << 8) | ord(padded[i + 1])
                  for i in range(0, 10, 2)]
        try:
            sid = self.slave()
        except ValueError as exc:
            messagebox.showerror("Slave ID", str(exc))
            return
        self.run(lambda: self.bus.write_multiple(sid, 7, values),
                 lambda _: (self.status("Company name written"),
                            self.read_all(quiet=True)),
                 self.on_error_box)

    def read_tariffs(self):
        try:
            sid = self.slave()
        except ValueError as exc:
            messagebox.showerror("Slave ID", str(exc))
            return

        def ok(vals):
            self.regs, self.regs_valid = vals, True
            for off, v in self.tl_vars.items():
                v.set(str(vals[off]))
            self.billing_var.set(BILLING_NAMES.get(vals[29], ""))
            self.load_parameter_var.set(LOAD_PARAMETER_NAMES.get(vals[34], ""))
            self.override_var.set(OVERRIDE_NAMES.get(vals[60], ""))
            self.refresh_views()
            self.status("Settings read")

        self.run(lambda: self.bus.read_holding(sid, 0, CONTROLLER_REG_COUNT), ok,
                 self.on_error_box)

    def write_tariffs(self):
        writes = []
        try:
            sid = self.slave()
            for off, v in self.tl_vars.items():
                if v.get().strip() != "":
                    writes.append((off, parse_input(off, v.get())))
            for off, var, names in ((29, self.billing_var, BILLING_NAMES),
                                    (34, self.load_parameter_var,
                                     LOAD_PARAMETER_NAMES),
                                    (60, self.override_var, OVERRIDE_NAMES)):
                if var.get():
                    writes.append((off, {n: k for k, n in names.items()}
                                   [var.get()]))
        except ValueError as exc:
            messagebox.showerror("Invalid value", str(exc))
            return
        if not writes:
            return

        def job():
            for off, raw in writes:
                try:
                    self.bus.write_single(sid, off, raw)
                except ModbusError as exc:
                    raise ModbusError(f"Register {40001 + off}: {exc}")
            return self.bus.read_holding(sid, 0, CONTROLLER_REG_COUNT)

        def ok(vals):
            self.regs, self.regs_valid = vals, True
            self.refresh_views()
            self.status(f"{len(writes)} registers written")

        self.run(job, ok, self.on_error_box)

    # ---- console tab -----------------------------------------------------------------
    def _build_console(self):
        tab = ttk.Frame(self.nb)
        self.nb.add(tab, text="Console")
        man = ttk.LabelFrame(tab, text="Manual request")
        man.pack(fill="x", padx=8, pady=6)
        self.fn_var = tk.StringVar(value="03 Read holding registers")
        ttk.Combobox(man, textvariable=self.fn_var, state="readonly",
                     width=26,
                     values=["03 Read holding registers",
                             "06 Write single register",
                             "16 Write multiple registers"]).grid(
            row=0, column=0, padx=6, pady=6)
        ttk.Label(man, text="Address (offset or 4xxxx)").grid(row=0, column=1)
        self.addr_var = tk.StringVar(value="40001")
        ttk.Entry(man, textvariable=self.addr_var, width=8).grid(
            row=0, column=2, padx=4)
        ttk.Label(man, text="Count / value(s), comma separated").grid(
            row=0, column=3, padx=(10, 2))
        self.vals_var = tk.StringVar(value="50")
        ttk.Entry(man, textvariable=self.vals_var, width=22).grid(
            row=0, column=4, padx=4)
        ttk.Button(man, text="Send", command=self.send_manual).grid(
            row=0, column=5, padx=8)

        frame = ttk.Frame(tab)
        frame.pack(fill="both", expand=True, padx=8, pady=(0, 4))
        self.log_txt = tk.Text(frame, height=20, wrap="none", state="disabled",
                               font=("Courier", 10))
        sb = ttk.Scrollbar(frame, command=self.log_txt.yview)
        self.log_txt.configure(yscrollcommand=sb.set)
        self.log_txt.pack(side="left", fill="both", expand=True)
        sb.pack(side="left", fill="y")
        self.log_txt.tag_configure("TX", foreground="#1060c0")
        self.log_txt.tag_configure("RX", foreground="#0a7d2c")
        self.log_txt.tag_configure("ERR", foreground="#b00020")
        self.log_txt.tag_configure("INFO", foreground="#707070")
        self.log_txt.tag_configure("RES", foreground="#8a4b00")
        row = ttk.Frame(tab)
        row.pack(fill="x", padx=8, pady=(0, 6))
        ttk.Button(row, text="Clear", command=self.clear_log).pack(side="left")
        ttk.Button(row, text="Save log...", command=self.save_log).pack(
            side="left", padx=6)

    def log(self, direction, text):
        stamp = datetime.datetime.now().strftime("%H:%M:%S.%f")[:-3]
        self.log_txt.config(state="normal")
        self.log_txt.insert("end", f"{stamp}  {direction:<4} {text}\n",
                            direction)
        n = int(self.log_txt.index("end-1c").split(".")[0])
        if n > 3000:
            self.log_txt.delete("1.0", f"{n - 3000}.0")
        self.log_txt.see("end")
        self.log_txt.config(state="disabled")

    def clear_log(self):
        self.log_txt.config(state="normal")
        self.log_txt.delete("1.0", "end")
        self.log_txt.config(state="disabled")

    def save_log(self):
        path = filedialog.asksaveasfilename(defaultextension=".txt",
                                            initialfile="modbus_log.txt")
        if path:
            with open(path, "w") as f:
                f.write(self.log_txt.get("1.0", "end"))
            self.status(f"Saved {path}")

    def send_manual(self):
        try:
            sid = self.slave()
            fn = int(self.fn_var.get()[:2])
            addr = parse_addr(self.addr_var.get())
            nums = [int(x, 0) for x in self.vals_var.get().split(",")
                    if x.strip()]
            if not 0 <= addr <= 0xFFFF or not nums:
                raise ValueError("Address / values are required")
        except ValueError as exc:
            messagebox.showerror("Invalid input", str(exc))
            return

        def job():
            if fn == 3:
                vals = self.bus.read_holding(sid, addr, nums[0])
                return "Values: " + ", ".join(
                    f"{v} (0x{v:04X})" for v in vals)
            if fn == 6:
                self.bus.write_single(sid, addr, nums[0])
                return "Write OK"
            self.bus.write_multiple(sid, addr, nums)
            return "Write OK"

        self.run(job, lambda text: self.log("RES", text), self.on_error_box)

    # ---- status bar ------------------------------------------------------------------
    def _build_statusbar(self):
        bar = ttk.Frame(self)
        bar.pack(fill="x", side="bottom")
        self.status_var = tk.StringVar()
        self.counters_var = tk.StringVar()
        ttk.Label(bar, textvariable=self.status_var).pack(side="left",
                                                          padx=8, pady=2)
        ttk.Label(bar, textvariable=self.counters_var).pack(side="right",
                                                            padx=8)


def main():
    App().mainloop()


if __name__ == "__main__":
    main()
