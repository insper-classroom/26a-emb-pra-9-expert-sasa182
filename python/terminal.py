import json
import tkinter as tk
from tkinter import ttk, scrolledtext
import serial
import serial.tools.list_ports
import threading
import random
import time
import pyautogui

pyautogui.PAUSE = 0
pyautogui.FAILSAFE = False

SPAM_WORDS = [
    "marco", "corsi", "daniboy", "tiozao", "Otorrinolaringologista", "raposa", "Extraordinariamente", "hotel",
    "Anticonstitucional", "janela", "Inconstitucional", "lima", "Responsabilidade", "novembro", "Conscientizacao", "pato",
    "Desenvolvimento", "romeu", "Impressionante", "tatu", "Caracteristica", "vitor", "Possibilidade", "uisque",
    "Especialidade", "zebra", "Planejamento", "mundo", "Conhecimento", "dados", "Relacionamento", "pacote",
    "Procedimento", "enviar", "Monitoramento", "receber", "Comportamento", "ok", "Produtividade", "falha",
]

def baud_interval(baud: int) -> float:
    """Comfortable spam interval for the given baudrate (seconds).
    A word ~10 chars at N baud → 10*10/N seconds; we use 4× for readability."""
    char_time = 10 / baud
    return max(0.05, 10 * char_time * 4)

BAUDS = ["9600", "19200", "38400", "57600", "115200", "230400"]
DEFAULT_BAUD = "115200"
BG = "#1e1e1e"
FG = "#d4d4d4"
BG_ENTRY = "#ffffff"
FG_ENTRY = "#1e1e1e"
BG_TERMINAL = "#121212"
BG_BUTTON_CONNECT = "#2e7d32"
BG_BUTTON_DISCONNECT = "#7b2020"
BG_BUTTON_SEND = "#1565c0"
FONT_TERMINAL = ("Consolas", 10)
FONT_UI = ("Segoe UI", 9)


class PanelSerial:
    def __init__(self, parent, title, align="left"):
        self.serial = None
        self.running = False
        self.align = align
        self.spamming = False
        self.spam_thread = None

        frame = tk.Frame(parent, bg=BG)
        frame.pack(fill=tk.BOTH, expand=True, padx=6, pady=6)

        tk.Label(frame, text=title, bg=BG, fg=FG,
                 font=("Segoe UI", 10, "bold")).pack(anchor="e" if align == "right" else "w")

        top = tk.Frame(frame, bg=BG)
        top.pack(fill=tk.X, pady=(2, 4))

        self.port_var = tk.StringVar()
        self.baud_var = tk.StringVar(value=DEFAULT_BAUD)

        if align == "right":
            self.btn_clear = tk.Button(top, text="Limpar", bg="#424242", fg="white",
                                       font=FONT_UI, relief=tk.FLAT, padx=8,
                                       command=self.clear_terminal)
            self.btn_clear.pack(side=tk.RIGHT, padx=(4, 0))

            self.btn_connect = tk.Button(top, text="Conectar", bg=BG_BUTTON_CONNECT, fg="white",
                                         font=FONT_UI, relief=tk.FLAT, padx=8,
                                         command=self.toggle_connection)
            self.btn_connect.pack(side=tk.RIGHT, padx=(4, 0))

            self.baud_cb = ttk.Combobox(top, textvariable=self.baud_var, values=BAUDS,
                                        width=8, font=FONT_UI, state="readonly")
            self.baud_cb.pack(side=tk.RIGHT, padx=(4, 0))
            self.baud_cb.set(DEFAULT_BAUD)

            self.port_cb = ttk.Combobox(top, textvariable=self.port_var,
                                        width=12, font=FONT_UI, state="readonly")
            self.port_cb.pack(side=tk.RIGHT, padx=(4, 0))
        else:
            self.port_cb = ttk.Combobox(top, textvariable=self.port_var,
                                        width=12, font=FONT_UI, state="readonly")
            self.port_cb.pack(side=tk.LEFT, padx=(0, 4))

            self.baud_cb = ttk.Combobox(top, textvariable=self.baud_var, values=BAUDS,
                                        width=8, font=FONT_UI, state="readonly")
            self.baud_cb.pack(side=tk.LEFT, padx=(0, 4))
            self.baud_cb.set(DEFAULT_BAUD)

            self.btn_connect = tk.Button(top, text="Conectar", bg=BG_BUTTON_CONNECT, fg="white",
                                         font=FONT_UI, relief=tk.FLAT, padx=8,
                                         command=self.toggle_connection)
            self.btn_connect.pack(side=tk.LEFT, padx=(0, 4))

            self.btn_clear = tk.Button(top, text="Limpar", bg="#424242", fg="white",
                                       font=FONT_UI, relief=tk.FLAT, padx=8,
                                       command=self.clear_terminal)
            self.btn_clear.pack(side=tk.LEFT, padx=(0, 4))
        self.terminal = scrolledtext.ScrolledText(frame, bg=BG_TERMINAL, fg=FG,
                                                  font=FONT_TERMINAL, relief=tk.FLAT,
                                                  wrap=tk.WORD, state=tk.DISABLED)
        self.terminal.pack(fill=tk.BOTH, expand=True)

        # Configure tags once after terminal widget is created
        j = tk.RIGHT if align == "right" else tk.LEFT
        self.terminal.tag_config("info",    foreground="#4fc3f7", justify=j)
        self.terminal.tag_config("error",   foreground="#ef5350", justify=j)
        self.terminal.tag_config("sent",    foreground="#81c784", justify=j)
        self.terminal.tag_config("default", justify=j)

        bottom = tk.Frame(frame, bg=BG)
        bottom.pack(fill=tk.X, pady=(4, 0))

        self.input_var = tk.StringVar()
        self.entry = tk.Entry(bottom, textvariable=self.input_var, bg=BG_ENTRY, fg=FG_ENTRY,
                              font=FONT_TERMINAL, relief=tk.FLAT, insertbackground=FG_ENTRY)
        self.entry.pack(side=tk.LEFT, fill=tk.X, expand=True, padx=(0, 4), ipady=4)
        self.entry.bind("<Return>", lambda e: self.send())

        self.btn_send = tk.Button(bottom, text="Enviar", bg=BG_BUTTON_SEND, fg="white",
                                  font=FONT_UI, relief=tk.FLAT, padx=10,
                                  command=self.send)
        self.btn_send.pack(side=tk.LEFT)

        self.spam_var = tk.BooleanVar(value=False)
        self.chk_spam = tk.Checkbutton(
            bottom, text="Spam", variable=self.spam_var,
            bg=BG, fg="#ffb74d", selectcolor=BG,
            activebackground=BG, activeforeground="#ffb74d",
            font=FONT_UI, command=self.toggle_spam
        )
        self.chk_spam.pack(side=tk.LEFT, padx=(6, 0))

        self.mouse_mode_var = tk.BooleanVar(value=False)
        self.chk_mouse = tk.Checkbutton(
            bottom, text="Mouse", variable=self.mouse_mode_var,
            bg=BG, fg="#64b5f6", selectcolor=BG,
            activebackground=BG, activeforeground="#64b5f6",
            font=FONT_UI
        )
        self.chk_mouse.pack(side=tk.LEFT, padx=(6, 0))

        self._mouse_sync = False
        self._mouse_buf = bytearray()
        self._mouse_last = {0: 0, 1: 0}
        self._mouse_last_log = 0.0

        # Spam speed row — hidden until spam is enabled
        self.spam_row = tk.Frame(frame, bg=BG)
        # (not packed yet)

        tk.Label(self.spam_row, text="Intervalo:", bg=BG, fg="#ffb74d", font=FONT_UI).pack(side=tk.LEFT, padx=(0, 4))

        # Shared variable in ms (integer)
        self.spam_ms_var = tk.StringVar(value="500")

        self.spam_scale = tk.Scale(
            self.spam_row, from_=50, to=5000, resolution=50, orient=tk.HORIZONTAL,
            bg=BG, fg="#ffb74d", troughcolor="#333333",
            highlightthickness=0, activebackground="#ffb74d",
            sliderlength=16, length=160, font=FONT_UI,
            showvalue=False, command=self._on_scale_change
        )
        self.spam_scale.set(500)
        self.spam_scale.pack(side=tk.LEFT, padx=(0, 6))

        self.spam_entry = tk.Entry(
            self.spam_row, textvariable=self.spam_ms_var,
            bg=BG_ENTRY, fg=FG_ENTRY, font=FONT_TERMINAL,
            relief=tk.FLAT, insertbackground=FG_ENTRY, width=6,
            justify=tk.RIGHT
        )
        self.spam_entry.pack(side=tk.LEFT)
        self.spam_ms_var.trace_add("write", self._on_entry_change)

        tk.Label(self.spam_row, text="ms", bg=BG, fg="#ffb74d", font=FONT_UI).pack(side=tk.LEFT, padx=(4, 0))

        self.refresh_ports()

    def write_terminal(self, text, tag=None):
        text = text or ""
        effective_tag = tag or "default"

        def _write():
            self.terminal.config(state=tk.NORMAL)
            self.terminal.insert(tk.END, text + "\n", effective_tag)
            self.terminal.see(tk.END)
            self.terminal.config(state=tk.DISABLED)

        self.terminal.after(0, _write)

    def clear_terminal(self):
        self.terminal.config(state=tk.NORMAL)
        self.terminal.delete("1.0", tk.END)
        self.terminal.config(state=tk.DISABLED)

    def refresh_ports(self):
        ports = [p.device for p in serial.tools.list_ports.comports()]
        self.port_cb["values"] = ports
        if ports and not self.port_var.get():
            self.port_var.set(ports[0])

    def toggle_connection(self):
        if self.serial and self.serial.is_open:
            self.disconnect()
        else:
            self.connect()

    def connect(self):
        port = self.port_var.get()
        baud = int(self.baud_var.get())
        if not port:
            self.write_terminal("[nenhuma porta selecionada]\n", tag="error")
            return
        try:
            self.serial = serial.Serial(port, baud, timeout=0.1)
            self.running = True
            self.btn_connect.config(text="Desconectar", bg=BG_BUTTON_DISCONNECT)
            self.write_terminal(f"[conectado em {port} @ {baud}]\n", tag="info")
            threading.Thread(target=self._read_loop, daemon=True).start()
        except serial.SerialException as e:
            self.write_terminal(f"[erro: {e}]\n", tag="error")

    def disconnect(self):
        self.running = False
        self.spamming = False
        self.spam_var.set(False)
        self.spam_row.pack_forget()
        self._mouse_sync = False
        self._mouse_buf = bytearray()
        if self.serial:
            self.serial.close()
            self.serial = None
        self.btn_connect.config(text="Conectar", bg=BG_BUTTON_CONNECT)
        self.write_terminal("[desconectado]\n", tag="info")

    def _read_loop(self):
        while self.running and self.serial and self.serial.is_open:
            try:
                data = self.serial.read(256)
                if data:
                    if self.mouse_mode_var.get():
                        self._process_mouse_bytes(data)
                    else:
                        text = data.decode("utf-8", errors="replace")
                        self.write_terminal(text)
            except serial.SerialException:
                self.running = False
                self.terminal.after(0, lambda: self.write_terminal("[conexao perdida]\n", tag="error"))
                self.terminal.after(0, lambda: self.btn_connect.config(text="Conectar", bg=BG_BUTTON_CONNECT))
                break

    def _process_mouse_bytes(self, data):
        for b in data:
            if not self._mouse_sync:
                if b == 0xFF:
                    self._mouse_sync = True
                    self._mouse_buf = bytearray()
            else:
                self._mouse_buf.append(b)
                if len(self._mouse_buf) == 3:
                    axis = self._mouse_buf[0]
                    value = int.from_bytes(self._mouse_buf[1:3], byteorder='little', signed=True)
                    if axis == 0:
                        pyautogui.moveRel(value, 0)
                        self._mouse_last[0] = value
                    elif axis == 1:
                        pyautogui.moveRel(0, value)
                        self._mouse_last[1] = value
                    self._mouse_sync = False
                    self._mouse_buf = bytearray()

        now = time.time()
        if now - self._mouse_last_log > 0.5:
            self.write_terminal(
                f"[mouse] X={self._mouse_last[0]:+4d} Y={self._mouse_last[1]:+4d}",
                tag="info"
            )
            self._mouse_last_log = now

    def _on_scale_change(self, val):
        """Scale moved → update entry (avoid feedback loop via flag)."""
        if getattr(self, "_syncing", False):
            return
        self._syncing = True
        self.spam_ms_var.set(str(int(float(val))))
        self._syncing = False

    def _on_entry_change(self, *_):
        """Entry typed → update scale (only when value is valid)."""
        if getattr(self, "_syncing", False):
            return
        try:
            ms = int(self.spam_ms_var.get())
            if 50 <= ms <= 5000:
                self._syncing = True
                self.spam_scale.set(ms)
                self._syncing = False
        except ValueError:
            pass

    def toggle_spam(self):
        if self.spam_var.get():
            self.spam_row.pack(fill=tk.X, pady=(2, 0))
            self.spamming = True
            self.spam_thread = threading.Thread(target=self._spam_loop, daemon=True)
            self.spam_thread.start()
            self.write_terminal("[spam iniciado]\n", tag="info")
        else:
            self.spam_row.pack_forget()
            self.spamming = False
            self.write_terminal("[spam parado]\n", tag="info")

    def _spam_loop(self):
        while self.spamming:
            if self.serial and self.serial.is_open:
                word = random.choice(SPAM_WORDS)
                try:
                    self.serial.write((word + "\r\n").encode("utf-8"))
                    self.write_terminal(f"> {word}", tag="sent")
                except serial.SerialException as e:
                    self.write_terminal(f"[erro spam: {e}]\n", tag="error")
                    self.spamming = False
                    self.terminal.after(0, lambda: self.spam_var.set(False))
                    break
                try:
                    interval = max(0.05, int(self.spam_ms_var.get()) / 1000)
                except ValueError:
                    interval = self.spam_scale.get() / 1000
                time.sleep(interval)
            else:
                # serial not connected yet — wait a bit and retry
                time.sleep(0.2)

    def send(self):
        if not self.serial or not self.serial.is_open:
            return
        text = self.input_var.get()
        if not text:
            return
        try:
            self.serial.write((text + "\r\n").encode("utf-8"))
            self.write_terminal(f"> {text}\n", tag="sent")
        except serial.SerialException as e:
            self.write_terminal(f"[erro ao enviar: {e}]\n", tag="error")
        self.input_var.set("")


class App:
    def __init__(self, root):
        self.root = root
        self.settings_file = "settings.json"

        root.protocol("WM_DELETE_WINDOW", self.on_closing)
        root.title("Serial Terminal")
        root.configure(bg="#e0e0e0")
        root.geometry("733x620")
        root.minsize(500, 400)

        self._apply_style()

        toolbar = tk.Frame(root, bg="#e0e0e0")
        toolbar.pack(fill=tk.X, padx=6, pady=(6, 0))

        tk.Button(toolbar, text="Atualizar Portas", bg="#424242", fg="white",
                  font=FONT_UI, relief=tk.FLAT, padx=8,
                  command=self.refresh_all).pack(side=tk.LEFT)

        panes = tk.Frame(root, bg="#e0e0e0")
        panes.pack(fill=tk.BOTH, expand=True, padx=6, pady=6)

        panes.columnconfigure(0, weight=1, uniform="half")
        panes.columnconfigure(2, weight=1, uniform="half")
        panes.rowconfigure(0, weight=1)

        left_frame  = tk.Frame(panes, bg=BG)
        right_frame = tk.Frame(panes, bg=BG)

        left_frame.grid(row=0, column=0, sticky="nsew")
        tk.Frame(panes, bg="#e0e0e0", width=36).grid(row=0, column=1, sticky="ns")
        right_frame.grid(row=0, column=2, sticky="nsew")

        self.left  = PanelSerial(left_frame,  "Pico  (UART)")
        self.right = PanelSerial(right_frame, "HC-06  (Bluetooth)", align="right")

        self._load_settings()

    def _load_settings(self):
        try:
            with open(self.settings_file, "r") as file:
                settings = json.load(file)
            self.left.port_var.set(settings.get("pico_port", ""))
            self.left.baud_var.set(settings.get("pico_baud", DEFAULT_BAUD))
            self.right.port_var.set(settings.get("hc06_port", ""))
            self.right.baud_var.set(settings.get("hc06_baud", DEFAULT_BAUD))
        except (FileNotFoundError, json.JSONDecodeError):
            pass

    def _apply_style(self):
        style = ttk.Style()
        style.theme_use("clam")
        style.configure("TCombobox",
                        fieldbackground=BG_ENTRY, background=BG_ENTRY,
                        foreground=FG_ENTRY, arrowcolor="#555555", bordercolor=BG_ENTRY,
                        selectbackground=BG_ENTRY, selectforeground=FG_ENTRY)
        style.map("TCombobox",
                  fieldbackground=[("readonly", BG_ENTRY), ("disabled", BG_ENTRY)],
                  foreground=[("readonly", FG_ENTRY), ("disabled", FG_ENTRY)],
                  selectbackground=[("readonly", BG_ENTRY)],
                  selectforeground=[("readonly", FG_ENTRY)])

    def on_closing(self):
        settings = {
            "pico_port": self.left.port_var.get(),
            "pico_baud": self.left.baud_var.get(),
            "hc06_port": self.right.port_var.get(),
            "hc06_baud": self.right.baud_var.get()
        }
        with open(self.settings_file, "w") as file:
            json.dump(settings, file)
        self.root.destroy()

    def refresh_all(self):
        self.left.refresh_ports()
        self.right.refresh_ports()


if __name__ == "__main__":
    root = tk.Tk()
    App(root)
    root.mainloop()
