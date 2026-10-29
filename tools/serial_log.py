"""Record a panel's serial log (the CH343 UART port) with timestamps.

Usage (pio's Python has pyserial):
  $USERPROFILE/.platformio/penv/Scripts/python.exe tools/serial_log.py [--port COMx] [--seconds 600] [--reset]
      [--no-touch] > log.txt

Finds the CH343 port (USB 1A86:55D3) unless --port is given. Opens with DTR/RTS low, so the panel keeps running;
--reset pulses RTS to reboot it and capture the boot. Long captures: run in the background and read the file after.
Useful lines: "RAM: new low", "printer:", "radar:", "HTTP -1", "task_wdt", "Guru Meditation", "rpc_core".
"""
import argparse
import sys
import time

import serial
from serial.tools import list_ports


def find_port():
    for p in list_ports.comports():
        if p.vid == 0x1A86 and p.pid == 0x55D3:
            return p.device
    sys.exit("no CH343 (UART) port found: is the panel's UART USB-C port plugged in?")


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--port")
    ap.add_argument("--seconds", type=float, default=60)
    ap.add_argument("--reset", action="store_true", help="reboot the panel first, to capture the boot")
    ap.add_argument("--no-touch", action="store_true", help="drop touch gesture lines")
    a = ap.parse_args()

    s = serial.Serial()
    s.port, s.baudrate, s.timeout = a.port or find_port(), 115200, 0.2
    s.dtr = s.rts = False  # opening with either asserted reboots the board
    s.open()
    if a.reset:
        s.rts = True
        time.sleep(0.1)
        s.rts = False
    t0 = time.time()
    while time.time() - t0 < a.seconds:
        line = s.readline()
        if not line:
            continue
        text = line.decode(errors="replace").rstrip()
        if a.no_touch and text.startswith("touch: ") and "->" in text:
            continue
        print("%7.1f %s" % (time.time() - t0, text[:240]), flush=True)


if __name__ == "__main__":
    main()
