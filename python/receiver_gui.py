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

    # Flattens nested payload dicts (e.g., {"sensor1": {"temp": 24}} -> {"sensor1_temp": 24})
    for key, val in sample.items():
        if isinstance(val, dict):
            for sub_key, sub_val in val.items():
                row[f"{key}_{sub_key}"] = sub_val
        else:
            row[key] = val
    
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
    import os
    import tkinter as tk
    from datetime import datetime
    from tkinter import filedialog, messagebox, ttk

    import matplotlib

    matplotlib.use("TkAgg")
    from matplotlib.backends.backend_tkagg import FigureCanvasTkAgg
    from matplotlib.figure import Figure

    root = tk.Tk()
    root.title("ESP32 Sensor Monitor & PID Controller")
    root.geometry("1100x750")
    root.minsize(900, 600)

    # Use a modern TTK theme
    style = ttk.Style()
    style.theme_use("clam")

    # Main Grid Layout: Left Control Panel (Fixed Width) | Right Plots (Expandable)
    main_container = ttk.Frame(root, padding=10)
    main_container.pack(fill="both", expand=True)

    sidebar = ttk.Frame(main_container, width=280)
    sidebar.pack(side="left", fill="y", padx=(0, 10))

    plot_frame = ttk.Frame(main_container)
    plot_frame.pack(side="right", fill="both", expand=True)

    # -------------------------------------------------------------
    # 1. CONNECTION & READOUT STATUS
    # -------------------------------------------------------------
    info_frame = ttk.LabelFrame(sidebar, text=" System Status ", padding=10)
    info_frame.pack(fill="x", pady=(0, 10))

    ip = local_ip()
    ttk.Label(
        info_frame,
        text=f"Server: http://{ip}:{PORT}/data",
        font=("Segoe UI", 8, "italic"),
        wraplength=240,
    ).pack(anchor="w")

    status = tk.StringVar(value="Waiting for data...")
    ttk.Label(
        info_frame, textvariable=status, font=("Segoe UI", 9), wraplength=240
    ).pack(anchor="w", pady=(4, 0))

    latest = tk.StringVar(value="No data")
    ttk.Label(
        info_frame,
        textvariable=latest,
        font=("Segoe UI", 9, "bold"),
        foreground="#1f77b4",
        wraplength=240,
    ).pack(anchor="w", pady=(6, 0))

    # -------------------------------------------------------------
    # 2. NUMERIC PARAMETERS (kP, kI, CFM, Water Target)
    # -------------------------------------------------------------
    param_frame = ttk.LabelFrame(sidebar, text=" Control Inputs ", padding=10)
    param_frame.pack(fill="x", pady=(0, 10))

    kp_var = tk.StringVar(value="1.0")
    ki_var = tk.StringVar(value="0.1")
    cfm_var = tk.StringVar(value="100")
    water_var = tk.StringVar(value="5.0")

    # Grid alignment for parameters
    fields = [
        ("kP (Proportional):", kp_var),
        ("kI (Integral):", ki_var),
        ("CFM Target:", cfm_var),
        ("Water/Day (gal):", water_var),
    ]

    for idx, (label_text, var) in enumerate(fields):
        ttk.Label(param_frame, text=label_text).grid(
            row=idx, column=0, sticky="w", pady=4
        )
        ttk.Entry(param_frame, textvariable=var, width=10, justify="right").grid(
            row=idx, column=1, sticky="e", pady=4, padx=(8, 0)
        )

    def do_send_params():
        # Place code here to transmit parameters to ESP32
        messagebox.showinfo(
            "Parameters Updated",
            f"Sent Parameters:\nkP: {kp_var.get()}\nkI: {ki_var.get()}\nCFM: {cfm_var.get()}\nWater/Day: {water_var.get()}",
        )

    ttk.Button(
        param_frame, text="Update Parameters", command=do_send_params
    ).grid(row=len(fields), column=0, columnspan=2, pady=(10, 0), sticky="ew")

    # -------------------------------------------------------------
    # 3. ACTIONS & CONTROLS (Pause, Export, Clear, Abort)
    # -------------------------------------------------------------
    actions_frame = ttk.LabelFrame(sidebar, text=" Actions ", padding=10)
    actions_frame.pack(fill="x", pady=(0, 10))

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
        except Exception as e:
            messagebox.showerror("Export failed", str(e))

    def do_clear():
        if messagebox.askyesno(
            "Clear Data", "Clear in-memory data? (CSV file is retained)"
        ):
            with rows_lock:
                rows.clear()

    def do_abort():
        if messagebox.askyesno(
            "EMERGENCY ABORT",
            "Are you sure you want to trigger emergency abort?",
            icon="warning",
        ):
            print("🚨 ABORT SIGNAL SENT")
            # Implement your ESP32 stop/abort payload transmission here

    ttk.Checkbutton(actions_frame, text="Pause Graphs", variable=paused).pack(
        anchor="w", pady=(0, 6)
    )
    ttk.Button(
        actions_frame, text="Export to Excel", command=do_export
    ).pack(fill="x", pady=2)
    ttk.Button(actions_frame, text="Clear Plot Memory", command=do_clear).pack(
        fill="x", pady=2
    )

    # Highlighted Abort Button
    style.configure(
        "Abort.TButton", font=("Segoe UI", 9, "bold"), foreground="#d9534f"
    )
    ttk.Button(
        actions_frame,
        text="🛑 ABORT PROCESS",
        style="Abort.TButton",
        command=do_abort,
    ).pack(fill="x", pady=(12, 0))

    # -------------------------------------------------------------
    # 4. TRIPLE SUBPLOT CANVAS (Temp, Pressure, Humidity)
    # -------------------------------------------------------------
    fig = Figure(figsize=(8, 7), dpi=100)
    fig.subplots_adjust(
        hspace=0.35, left=0.08, right=0.96, top=0.96, bottom=0.07
    )

    # Create 3 subplots sharing the X axis
    ax_temp, ax_press, ax_hum = fig.subplots(3, 1, sharex=True)

    canvas = FigureCanvasTkAgg(fig, master=plot_frame)
    canvas.get_tk_widget().pack(fill="both", expand=True)

    def refresh():
        with rows_lock:
            snap = rows[-PLOT_WINDOW:]
            total = len(rows)

        if snap:
            last = snap[-1]
            status.set(f"Total Samples: {total} | Last update: {last.get('time', 'N/A')}")

            # Display formatted telemetry keys in side text
            sensor_readings = {}
            for k, v in last.items():
                if k in ("time", "ms") or not isinstance(v, (int, float)):
                    continue
                parts = k.split("_", 1)
                s_name = parts[0] if len(parts) > 1 else "data"
                metric = parts[1] if len(parts) > 1 else k
                sensor_readings.setdefault(s_name, []).append(f"{metric}:{v}")

            readout_lines = [
                f"[{s.upper()}] " + ", ".join(items)
                for s, items in sensor_readings.items()
            ]
            latest.set("\n".join(readout_lines) if readout_lines else "No numerical data")

            if not paused.get():
                ax_temp.clear()
                ax_press.clear()
                ax_hum.clear()

                x_range = range(len(snap))

                # Helper function to dynamically locate matching data key
                def find_key(target):
                    for k in last.keys():
                        if target.lower() in k.lower() and isinstance(
                            last[k], (int, float)
                        ):
                            return k
                    return None

                sensor_colors = {
                    "sensor1": "#1f77b4",  # Blue
                    "sensor2": "#ff7f0e",  # Orange
                    "sensor3": "#2ca02c",  # Green
                }
                fallback_colors = ["#d62728", "#9467bd", "#8c564b", "#e377c2"]

                # Collect ALL matching sensor keys present in the data
                all_keys = [
                    k for k, v in last.items()
                    if k not in ("time", "ms") and isinstance(v, (int, float))
                ]
                temp_keys = [k for k in all_keys if "temp" in k.lower()]
                press_keys = [k for k in all_keys if "press" in k.lower()]
                hum_keys = [k for k in all_keys if "hum" in k.lower()]

                def get_color(key, idx):
                    sensor_prefix = key.split("_")[0].lower()
                    return sensor_colors.get(sensor_prefix, fallback_colors[idx % len(fallback_colors)])

                def format_label(key):
                    parts = key.split("_")
                    return f"{parts[0].capitalize()} ({parts[1]})" if len(parts) == 2 else key

                # Loop through and plot ALL temperature keys (sensor1_temp, sensor2_temp, sensor3_temp)
                for i, k in enumerate(temp_keys):
                    ax_temp.plot(
                        x_range,
                        [r.get(k, 0) for r in snap],
                        color=get_color(k, i),
                        label=format_label(k),
                        linewidth=1.5,
                    )
                for i, k in enumerate(press_keys):
                    ax_press.plot(
                        x_range,
                        [r.get(k, 0) for r in snap],
                        color=get_color(k, i),
                        label=format_label(k),
                        linewidth=1.5,
                    )
                for i, k in enumerate(hum_keys):
                    ax_hum.plot(
                        x_range,
                        [r.get(k, 0) for r in snap],
                        color=get_color(k, i),
                        label=format_label(k),
                        linewidth=1.5,
                    )
                ax_temp.set_ylabel("Temp (°C)")
                ax_temp.grid(True, linestyle="--", alpha=0.5)
                ax_temp.legend(loc="upper left", fontsize=8)

                # Subplot 2: Pressure
                if press_k:
                    ax_press.plot(
                        x_range,
                        [r.get(press_k, 0) for r in snap],
                        color="#1f77b4",
                        label=press_k,
                    )
                ax_press.set_ylabel("Pressure (hPa)")
                ax_press.grid(True, linestyle="--", alpha=0.5)
                ax_press.legend(loc="upper left", fontsize=8)

                # Subplot 3: Humidity
                if hum_k:
                    ax_hum.plot(
                        x_range,
                        [r.get(hum_k, 0) for r in snap],
                        color="#2ca02c",
                        label=hum_k,
                    )
                ax_hum.set_ylabel("Humidity (%)")
                ax_hum.grid(True, linestyle="--", alpha=0.5)
                ax_hum.legend(loc="upper left", fontsize=8)

                ax_hum.set_xlabel(f"Last {len(snap)} samples")

                canvas.draw_idle()

        root.after(500, refresh)

    refresh()
    root.mainloop()


if __name__ == "__main__":
    start_server()
    print(f"Listening on http://{local_ip()}:{PORT}/data")
    run_gui()
