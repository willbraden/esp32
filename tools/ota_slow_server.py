#!/usr/bin/env python3
"""
ota_slow_server.py: a static file server that trickles every response at a
fixed rate, so a staging-rig OTA download takes as long as a real one from
GitHub (~1.45 MB over TLS took about a minute) instead of ~15 s on the LAN.

    python3 tools/ota_slow_server.py --port 8766 --rate 24000 --dir <folder with the .bin>

Serve only the firmware .bin from here. Keep manifest.json on the normal
`python3 -m http.server 8765`, and sign the manifest with this server's URL
(http://<mac-ip>:8766/<file>.bin). The device's only download limit is a stall
timeout, so a slow, steady stream is fine.
"""
import argparse, functools, http.server, time


class Slow(http.server.SimpleHTTPRequestHandler):
    rate = 24000

    def copyfile(self, src, dst):
        chunk = 1024
        while True:
            b = src.read(chunk)
            if not b:
                break
            dst.write(b)
            time.sleep(chunk / self.rate)


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--port", type=int, default=8766)
    ap.add_argument("--rate", type=int, default=24000, help="bytes per second (default 24000, ~1 min per image)")
    ap.add_argument("--dir", required=True, help="folder to serve")
    a = ap.parse_args()
    Slow.rate = a.rate
    handler = functools.partial(Slow, directory=a.dir)
    print(f"serving {a.dir} on :{a.port} at {a.rate} B/s")
    http.server.ThreadingHTTPServer(("0.0.0.0", a.port), handler).serve_forever()


if __name__ == "__main__":
    main()
