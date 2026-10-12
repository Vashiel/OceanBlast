"""Visible embedded-LCD smoke test on an interactive Windows desktop.
Requires Pillow. Run: python tests/windows_embedded_presentation.py EXE ROM OUTPUT.png
Keep the test window unobstructed. This checks visible color output, not gameplay fidelity.
"""
import ctypes as c
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import time
from PIL import ImageGrab

u = c.windll.user32
u.SetProcessDPIAware()
u.SendMessageW.argtypes = [c.c_void_p, c.c_uint, c.c_size_t, c.c_ssize_t]
u.FindWindowExW.argtypes = [c.c_void_p, c.c_void_p, c.c_wchar_p, c.c_wchar_p]
u.FindWindowExW.restype = c.c_void_p
exe, rom, output = map(lambda value: Path(value).resolve(), sys.argv[1:4])
wait = float(sys.argv[4]) if len(sys.argv) > 4 else 18
with tempfile.TemporaryDirectory(prefix='oceanblast-visible-test-') as folder:
    settings = Path(folder) / 'settings.ini'
    settings.write_text('[folders]\nlast_rom=' + str(rom) + '\n[audio]\nenabled=1\n[video]\nwindow_mode=2\nsmooth=0\n[system]\nnvram=0\nfps_log=1\n', encoding='utf-8')
    env = os.environ.copy()
    env['OCEANBLAST_SETTINGS_PATH'] = str(settings)
    process = subprocess.Popen([str(exe)], env=env)
    window = None
    callback_type = c.WINFUNCTYPE(c.c_bool, c.c_void_p, c.c_ssize_t)
    def visit(hwnd, unused):
        global window
        owner = c.c_ulong()
        u.GetWindowThreadProcessId(c.c_void_p(hwnd), c.byref(owner))
        if owner.value == process.pid and u.IsWindowVisible(c.c_void_p(hwnd)):
            window = hwnd
        return True
    try:
        deadline = time.monotonic() + 10
        while not window and time.monotonic() < deadline:
            u.EnumWindows(callback_type(visit), 0)
            time.sleep(0.1)
        if not window:
            raise RuntimeError('Launcher window did not appear')
        u.SetWindowPos(c.c_void_p(window), c.c_void_p(-1), 80, 80, 900, 650, 0x0040)
        u.SendMessageW(window, 0x111, 101, 0)  # Restart current ROM, through normal launcher path.
        time.sleep(wait)
        display = u.FindWindowExW(window, None, 'OceanBlastDisplayClass', None)
        lcd = u.FindWindowExW(display, None, 'OceanBlastLcdClass', None) if display else None
        if not lcd:
            raise RuntimeError('Embedded LCD child did not appear')
        rect = (c.c_long * 4)()
        point = (c.c_long * 2)(0, 0)
        u.GetClientRect(c.c_void_p(lcd), rect)
        u.ClientToScreen(c.c_void_p(lcd), point)
        box = (point[0], point[1], point[0] + rect[2], point[1] + rect[3])
        image = ImageGrab.grab(bbox=box).convert('RGB')
        output.parent.mkdir(parents=True, exist_ok=True)
        image.save(output)
        rgb = image.tobytes()
        colored = sum(max(pixel) - min(pixel) > 16 and max(pixel) > 32 for pixel in zip(rgb[0::3], rgb[1::3], rgb[2::3]))
        if colored < 500:
            raise RuntimeError(f'LCD remains black or uncolored: {colored} colored pixels')
        print(f'PASS visible embedded LCD: {colored} colored pixels')
    finally:
        if window:
            u.SendMessageW(window, 0x10, 0, 0)
        try:
            process.wait(timeout=10)
        except subprocess.TimeoutExpired:
            process.terminate()
