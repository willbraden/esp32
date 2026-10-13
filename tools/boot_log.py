#!/usr/bin/env python3
"""
boot_log.py — read the pebl weather firmware's persistent boot log and OTA
state straight from flash. Works when serial output is lost or the board only
runs for a moment, because it reads NVS, not the console.

    # board in download mode (hold BOOT, tap RESET, release BOOT):
    ~/.platformio/penv/bin/python tools/boot_log.py
    # or decode a dump you already have (esptool read_flash 0x9000 0x5000 nvs.bin):
    ~/.platformio/penv/bin/python tools/boot_log.py --dump nvs.bin
    # a diagnostic's results too:
    ~/.platformio/penv/bin/python tools/boot_log.py --ns netstress

Reading leaves the chip in download mode. Start it again with one RESET tap,
or: esptool.py --chip esp32s3 --port <port> --after watchdog_reset chip_id

Prints the boot records oldest first. Each one reads like
    #42 v1.2.0 wake=timer path=boot>wifi>loc>weather>drawn>sleep up=9.3s wifi=2100ms next-boot=deep-sleep
path is how far the boot got. next-boot is what started the following boot:
PANIC or a watchdog means this boot crashed at the end of its path; sw-restart
follows an OTA install; deep-sleep is the normal timer/button wake; power-on is
RESET or a power cycle. See src/weather/boot_log.h.
"""
import argparse, glob, os, struct, subprocess, sys, tempfile

NVS_OFFSET, NVS_SIZE = 0x9000, 0x5000      # partitions.csv
PAGE, ENTRY = 4096, 32
T_U8, T_U32, T_STR = 0x01, 0x04, 0x21
NAMESPACES = ("pebl-boot", "pebl-ota", "pebl-weather")
SECRET_NAMESPACES = ("nvs.net80211",)   # the WiFi driver's store: SSID, password, PMK


def read_nvs(dump):
    """{namespace: {key: value}} from a raw NVS partition image (format v2)."""
    pages = []
    for off in range(0, len(dump) - PAGE + 1, PAGE):
        page = dump[off:off + PAGE]
        state, seq = struct.unpack_from("<II", page, 0)
        if state in (0xFFFFFFFF, 0):          # uninitialised or corrupt
            continue
        pages.append((seq, page))
    pages.sort()                              # older pages first, so newer values win
    ns_names, raw = {}, []
    for _, page in pages:
        bitmap, i = page[32:64], 0
        while i < 126:
            state = (bitmap[i // 4] >> ((i % 4) * 2)) & 3
            e = page[64 + i * ENTRY:64 + (i + 1) * ENTRY]
            span = e[2] if 1 <= e[2] <= 126 - i else 1
            if state == 2:                    # written (3 = empty, 0 = erased)
                ns, typ = e[0], e[1]
                key = e[8:24].split(b"\0")[0].decode("latin1")
                data = e[24:32]
                if typ == T_U8:
                    val = data[0]
                elif typ == T_U32:
                    val = struct.unpack("<I", data[:4])[0]
                elif typ == T_STR:
                    size = struct.unpack("<H", data[:2])[0]
                    body = page[64 + (i + 1) * ENTRY:64 + (i + span) * ENTRY]
                    val = body[:size].rstrip(b"\0").decode("utf-8", "replace")
                else:
                    val = bytes(data)                 # blob or other type: not decoded
                if ns == 0 and typ == T_U8:
                    ns_names[val] = key
                else:
                    raw.append((ns, key, val))
            i += span
    out = {}
    for ns, key, val in raw:
        out.setdefault(ns_names.get(ns, f"ns{ns}"), {})[key] = val
    return out


def dump_from_board(port):
    esptool = os.path.expanduser("~/.platformio/packages/tool-esptoolpy/esptool.py")
    fd, path = tempfile.mkstemp(suffix=".bin")
    os.close(fd)
    cmd = [sys.executable, esptool, "--chip", "esp32s3", "--port", port,
           "--before", "no_reset", "--after", "no_reset",
           "read_flash", hex(NVS_OFFSET), hex(NVS_SIZE), path]
    r = subprocess.run(cmd, capture_output=True, text=True)
    if r.returncode != 0:
        sys.exit("esptool failed (is the board in download mode?):\n" + r.stdout[-800:] + r.stderr[-800:])
    with open(path, "rb") as f:
        data = f.read()
    os.remove(path)
    return data


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--dump", help="decode this NVS image instead of reading the board")
    ap.add_argument("--port", help="serial port (default: first /dev/cu.usbmodem*)")
    ap.add_argument("--ns", action="append", default=[], metavar="NAME",
                    help="also print this namespace's numbers and strings, e.g. --ns netstress")
    a = ap.parse_args()

    if a.dump:
        with open(a.dump, "rb") as f:
            data = f.read()[:NVS_SIZE]
    else:
        port = a.port or next(iter(sorted(glob.glob("/dev/cu.usbmodem*"))), None)
        if not port:
            sys.exit("no /dev/cu.usbmodem* port: put the board in download mode first")
        data = dump_from_board(port)

    nvs = read_nvs(data)
    boot = nvs.get("pebl-boot", {})
    records = [v for k, v in boot.items() if k.startswith("h") and isinstance(v, str)]
    records.sort(key=lambda r: int(r[1:].split(" ", 1)[0]) if r.startswith("#") else 0)
    print(f"boot log ({len(records)} archived, next boot #{boot.get('seq', 0) + 1}):")
    for r in records:
        print("  " + r)
    if "cur" in boot:
        print("  " + boot["cur"] + "  <- most recent boot (no boot after it yet)")
    if not records and "cur" not in boot:
        print("  (empty: this firmware has not logged a boot yet)")

    o = nvs.get("pebl-ota", {})
    print("ota state:", ", ".join(f"{k}={v}" for k, v in sorted(o.items())) or "(none)")
    w = nvs.get("pebl-weather", {})
    if "mode" in w:
        print(f"screen mode: {w['mode']} ({'word' if w['mode'] == 1 else 'numbers'})")
    for ns in a.ns:
        if ns in SECRET_NAMESPACES:
            sys.exit(f"refusing to print {ns}: it holds WiFi credentials")
        vals = {k: v for k, v in nvs.get(ns, {}).items() if isinstance(v, (int, str))}
        print(f"{ns}:" + ("" if vals else " (empty)"))
        for k, v in sorted(vals.items()):
            print(f"  {k} = {v}")


if __name__ == "__main__":
    main()
