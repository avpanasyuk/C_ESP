"""Top-most banner popup for a fleet alarm.

Launched detached (via pythonw) by alarm_listener.py. Renders a centered,
always-on-top banner on the monitor under the cursor.

    pythonw popup_banner.py "<message>" [bg_hex] [seconds]

seconds = 0 -> persistent until the user dismisses it (default for alarms).

Dismissal takes a deliberate act -- a click on the banner, or Esc/Enter/Space
while it holds focus. It deliberately does NOT close on mere mouse movement:
this carries alarms (water leak, flat battery) that must survive the user
brushing the mouse without reading them.
"""

import sys
import tkinter as tk

try:
    import ctypes
    from ctypes import wintypes
    _user32 = ctypes.windll.user32
except Exception:
    _user32 = None


def cursor_monitor_geometry():
    """Return (x, y, w, h) of the monitor under the cursor, or a sane default."""
    if _user32 is None:
        return (0, 0, 1920, 1080)
    pt = wintypes.POINT()
    _user32.GetCursorPos(ctypes.byref(pt))
    MONITOR_DEFAULTTONEAREST = 2
    hmon = _user32.MonitorFromPoint(pt, MONITOR_DEFAULTTONEAREST)

    class RECT(ctypes.Structure):
        _fields_ = [("left", ctypes.c_long), ("top", ctypes.c_long),
                    ("right", ctypes.c_long), ("bottom", ctypes.c_long)]

    class MONITORINFO(ctypes.Structure):
        _fields_ = [("cbSize", ctypes.c_ulong), ("rcMonitor", RECT),
                    ("rcWork", RECT), ("dwFlags", ctypes.c_ulong)]

    mi = MONITORINFO()
    mi.cbSize = ctypes.sizeof(MONITORINFO)
    _user32.GetMonitorInfoW(hmon, ctypes.byref(mi))
    r = mi.rcMonitor
    return (r.left, r.top, r.right - r.left, r.bottom - r.top)


def text_colors(bg):
    """(main, subtext) foreground for an arbitrary banner colour.

    The colour is chosen per-rule in bsd's fleet_alarm.json, so alarms can be told apart at a
    glance -- the water leak is light blue precisely because it must not read as Claude Code's
    red attention banner. Hardcoded white text would be unreadable on any such light colour."""
    try:
        r, g, b = (int(bg[i:i + 2], 16) for i in (1, 3, 5))
    except (ValueError, IndexError):
        return "white", "#e8e8e8"           # named colour or malformed hex: assume dark
    light = 0.2126 * r + 0.7152 * g + 0.0722 * b > 140  # Rec.709 luma
    return ("black", "#404040") if light else ("white", "#e8e8e8")


def main():
    message = sys.argv[1] if len(sys.argv) > 1 else "Fleet alarm"
    bg = sys.argv[2] if len(sys.argv) > 2 else "#b00020"   # red
    seconds = float(sys.argv[3]) if len(sys.argv) > 3 else 0.0  # 0 = persistent
    fg, sub_fg = text_colors(bg)

    mx, my, mw, mh = cursor_monitor_geometry()

    root = tk.Tk()
    root.overrideredirect(True)          # no title bar
    root.attributes("-topmost", True)
    root.configure(bg=bg)

    w, h = 560, 150
    x = mx + (mw - w) // 2
    y = my + (mh - h) // 3
    root.geometry(f"{w}x{h}+{x}+{y}")

    tk.Label(root, text=message, bg=bg, fg=fg,
             font=("Segoe UI", 22, "bold"), wraplength=w - 40).pack(expand=True, fill="both")
    tk.Label(root, text="click to dismiss", bg=bg, fg=sub_fg,
             font=("Segoe UI", 9)).pack(side="bottom", pady=6)

    def close(*_):
        try:
            root.destroy()
        except Exception:
            pass

    root.bind("<Button>", close)
    root.bind("<Escape>", close)
    root.bind("<Return>", close)
    root.bind("<space>", close)
    root.focus_force()                   # so the key bindings reach us

    # Re-assert topmost: a full-screen app (game, video) that grabs the foreground
    # after us would otherwise cover an alarm the user never sees.
    def keep_on_top():
        root.attributes("-topmost", True)
        root.after(2000, keep_on_top)

    keep_on_top()
    if seconds > 0:
        root.after(int(seconds * 1000), close)
    root.mainloop()


if __name__ == "__main__":
    main()
