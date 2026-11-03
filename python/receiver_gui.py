"""
ESP32 data receiver: HTTP server + live plot GUI + CSV logging + Excel export.

Run:  python receiver_gui.py
The ESP32 POSTs JSON like {"ms": 1234, "temp": 24.8, "hum": 41.2} to /data.
Every numeric field is plotted automatically. Every sample is appended to a CSV
(crash-safe). Use the "Export to Excel" button to write a .xlsx.
"""
import csv
import logging
import os
import socket
import threading
from datetime import datetime

from flask import Flask, jsonify, request

PORT = 5000
CSV_PATH = "esp32_log.csv"
PLOT_WINDOW = 200  # number of most recent points shown in the plot

# ---------------------------------------------------------------- data store
rows = []                 # list of dicts, one per sample
rows_lock = threading.Lock()
csv_fields = None         # column order, fixed by the first sample


def add_sample(sample: dict) -> dict:
    """Store a sample in memory and append it to the CSV."""
    global csv_fields
    row = {"time": datetime.now().isoformat(timespec="milliseconds")}
    row.update(sample)
    with rows_lock:
        rows.append(row)
        new_file = not os.path.exists(CSV_PATH) or os.path.getsize(CSV_PATH) == 0
        if csv_fields is None:
            csv_fields = list(row.keys())
        with open(CSV_PATH, "a", newline="") as f:
            w = csv.DictWriter(f, fieldnames=csv_fields, extrasaction="ignore")
            if new_file:
                w.writeheader()
            w.writerow(row)
    return row


def export_excel(path: str) -> int:
    import pandas as pd
    with rows_lock:
        df = pd.DataFrame(rows)
    df.to_excel(path, index=False)
    return len(df)


# -------------------------------------------------------------- HTTP server
app = Flask(__name__)
logging.getLogger("werkzeug").setLevel(logging.ERROR)


@app.route("/data", methods=["POST"])
def receive():
    payload = request.get_json(silent=True)
    if not isinstance(payload, dict):
        return jsonify(error="expected a JSON object"), 400
    add_sample(payload)
    return jsonify(ok=True), 200


@app.route("/", methods=["GET"])
def index():
    with rows_lock:
        n = len(rows)
    return f"ESP32 receiver running. {n} samples received."


def start_server():
    threading.Thread(
        target=lambda: app.run(host="0.0.0.0", port=PORT, threaded=True),
        daemon=True,
    ).start()


def local_ip() -> str:
    s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    try:
        s.connect(("8.8.8.8", 80))
        return s.getsockname()[0]
    except OSError:
        return "127.0.0.1"
    finally:
        s.close()


# --------------------------------------------------------------------- GUI
def run_gui():
    import tkinter as tk
    from tkinter import filedialog, messagebox, ttk

    import matplotlib
    matplotlib.use("TkAgg")
    from matplotlib.backends.backend_tkagg import FigureCanvasTkAgg
    from matplotlib.figure import Figure

    root = tk.Tk()
    root.title("ESP32 Data Logger")
    root.geometry("900x600")

    # top bar
    top = ttk.Frame(root, padding=8)
    top.pack(fill="x")
    ip = local_ip()
    ttk.Label(
        top, text=f"Server: http://{ip}:{PORT}/data   (put this in the ESP32 code)"
    ).pack(side="left")

    status = tk.StringVar(value="Waiting for data...")
    ttk.Label(root, textvariable=status, padding=(8, 0)).pack(fill="x")

    latest = tk.StringVar(value="")
    ttk.Label(root, textvariable=latest, font=("Segoe UI", 11, "bold"),
              padding=8).pack(fill="x")

    # plot
    fig = Figure(figsize=(8, 4), dpi=100)
    ax = fig.add_subplot(111)
    canvas = FigureCanvasTkAgg(fig, master=root)
    canvas.get_tk_widget().pack(fill="both", expand=True)

    paused = tk.BooleanVar(value=False)

    def do_export():
        path = filedialog.asksaveasfilename(
            defaultextension=".xlsx",
            filetypes=[("Excel workbook", "*.xlsx")],
            initialfile=f"esp32_data_{datetime.now():%Y%m%d_%H%M%S}.xlsx",
        )
        if not path:
            return
        try:
            n = export_excel(path)
            messagebox.showinfo("Exported", f"Wrote {n} rows to\n{path}")
        except Exception as e:  # noqa: BLE001
            messagebox.showerror("Export failed", str(e))

    def do_clear():
        if messagebox.askyesno("Clear", "Clear in-memory data? (CSV file is kept)"):
            with rows_lock:
                rows.clear()

    ttk.Button(top, text="Export to Excel", command=do_export).pack(side="right", padx=4)
    ttk.Button(top, text="Clear", command=do_clear).pack(side="right", padx=4)
    ttk.Checkbutton(top, text="Pause plot", variable=paused).pack(side="right", padx=8)
    
    # Second row of buttons (Row 2)
    row2 = ttk.Frame(root, padding=(8, 0, 8, 8))
    row2.pack(fill="x")

    # Add buttons in Row 2
    # ttk.Button(row2, text="Reset Zoom", command=do_reset_zoom).pack(side="left", padx=4)
    ttk.Button(row2, text="Button 2", command=lambda: print("Clicked 2")).pack(side="left", padx=4)
    ttk.Button(row2, text="Button 3", command=lambda: print("Clicked 3")).pack(side="left", padx=4)

    def refresh():
        with rows_lock:
            snap = rows[-PLOT_WINDOW:]
            total = len(rows)
        if snap:
            keys = [
                k for k, v in snap[-1].items()
                if k not in ("time", "ms") and isinstance(v, (int, float))
            ]
            last = snap[-1]
            latest.set("   ".join(f"{k}: {last[k]}" for k in keys))
            status.set(f"{total} samples   |   last at {last['time']}   |   "
                       f"logging to {os.path.abspath(CSV_PATH)}")
            if not paused.get():
                ax.clear()
                for k in keys:
                    ax.plot(range(len(snap)),
                            [r.get(k) for r in snap], label=k)
                ax.set_xlabel(f"last {len(snap)} samples")
                ax.grid(True, alpha=0.3)
                if keys:
                    ax.legend(loc="upper left")
                canvas.draw_idle()
        root.after(500, refresh)

    refresh()
    root.mainloop()


if __name__ == "__main__":
    start_server()
    print(f"Listening on http://{local_ip()}:{PORT}/data")
    run_gui()
