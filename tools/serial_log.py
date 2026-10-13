#!/usr/bin/env python3
"""
serial_log.py: read the pebl board's USB serial without fighting it.

The board uses the ESP32-S3's native USB-Serial/JTAG. That has two quirks:
  * The port disappears whenever the chip deep-sleeps and comes back on wake,
    so a logger has to keep reconnecting.
  * Opening the port with DTR/RTS asserted can reset the chip (rst:0x15
    USB_UART_CHIP_RESET). This opens with both deasserted. Reopening after a
    re-enumeration can still reset it, so do not trust "wake=" lines that
    follow a reconnect (see the verify-pebl skill, serial-diagnostics.md).

Usage (run with the PlatformIO python, which has pyserial):
    ~/.platformio/penv/bin/python tools/serial_log.py                # 120 s, print everything
    ~/.platformio/penv/bin/python tools/serial_log.py --until Sleeping --timeout 90
    ~/.platformio/penv/bin/python tools/serial_log.py --quiet        # hide ROM/boot noise
"""
import argparse, glob, sys, time

import serial

NOISE = ("load:", "SPIWP", "mode:DIO", "entry ", "Build:", "ESP-ROM", "Saved PC",
         "perimanSetPinBus", "STA.cpp", "esp_core_dump")


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--port", help="serial port (default: first /dev/cu.usbmodem*)")
    ap.add_argument("--timeout", type=float, default=120, help="seconds before giving up (default 120)")
    ap.add_argument("--until", help="stop after a line containing this text")
    ap.add_argument("--quiet", action="store_true", help="hide ROM boot and driver noise")
    a = ap.parse_args()

    t0, s = time.time(), None
    while time.time() - t0 < a.timeout:
        if s is None:
            ports = [a.port] if a.port else sorted(glob.glob("/dev/cu.usbmodem*"))
            if not ports or not glob.glob(ports[0]):
                time.sleep(0.3)
                continue
            try:
                s = serial.Serial()
                s.port, s.baudrate, s.timeout = ports[0], 115200, 0.3
                s.dtr = s.rts = False          # deasserted BEFORE open: do not reset the chip
                s.open()
            except (serial.SerialException, OSError):
                s = None
                time.sleep(0.3)
                continue
        try:
            chunk = s.read(4096)
        except (serial.SerialException, OSError):
            s = None                            # port vanished: chip went to deep sleep
            continue
        for line in chunk.decode(errors="replace").splitlines():
            if not line.strip() or (a.quiet and any(n in line for n in NOISE)):
                continue
            print(time.strftime("%H:%M:%S"), line, flush=True)
            if a.until and a.until in line:
                return 0
    return 1 if a.until else 0


if __name__ == "__main__":
    sys.exit(main())
