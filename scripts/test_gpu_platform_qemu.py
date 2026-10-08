#!/usr/bin/env python3
"""End-to-end test of the hidden GPU driver package system in QEMU.

Covers everything that does NOT need real GPU hardware:
  1. matching adapter  -> package downloaded in the background, signature
     verified, retained module loaded and activated ("direkt aktiv"),
     and the Store job list never mentions it;
  2. tampered package  -> rejected, firmware framebuffer retained;
  3. old-version package (below the catalog floor) is not exercised here, the
     host test of the verifier covers that;
  4. adapter without catalog entry (virtio-vga) -> NOTHING is downloaded.

The package server is a throw-away local HTTP server that records requests;
QEMU's user network reaches it as http://10.0.2.2:8930/ (kernel option
gpudriverlocal).  Only disposable files below build/ are touched.
"""
import argparse, http.server, json, shutil, socketserver, sys, threading
from pathlib import Path
sys.path.insert(0, str(Path(__file__).resolve().parent))
from vm import VM

ROOT = Path(__file__).resolve().parents[1]
PKG = ROOT / 'build' / 'gpu-drivers' / 'packages'
OUT = ROOT / 'build' / 'gpu-platform-tests'
OUT.mkdir(parents=True, exist_ok=True)


class Server:
    """Serves one directory and records every request path."""
    def __init__(self, directory, port=8930):
        self.requests = []
        outer = self

        class Handler(http.server.SimpleHTTPRequestHandler):
            def __init__(self, *a, **k):
                super().__init__(*a, directory=str(directory), **k)

            def log_message(self, fmt, *args):
                outer.requests.append(self.path)

        socketserver.TCPServer.allow_reuse_address = True
        self.httpd = socketserver.TCPServer(('0.0.0.0', port), Handler)
        threading.Thread(target=self.httpd.serve_forever, daemon=True).start()

    def stop(self):
        self.httpd.shutdown()
        self.httpd.server_close()


def boot(name, extra, cmdline, wait_for, timeout, port):
    vm = VM(image=str(ROOT / 'build/nexis.img'), cmdline=cmdline, mem='1024M', serial=OUT / (name + '.log'),
            port=port, extra=extra)
    ok = vm.wait_serial(wait_for, timeout)
    # let late messages (download result, Store job list) arrive
    import time
    time.sleep(4)
    log = vm.log()
    vm.quit()
    return ok, log


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--timeout', type=int, default=150)
    args = ap.parse_args()
    report = {}
    # ---- 1. matching adapter, valid package --------------------------------
    server = Server(PKG)
    ok, log = boot('match', ['-vga', 'std'], 'gpudriverlocal gpuautokeep selftest', 'Retained native module', args.timeout, 4501)
    report['match'] = {
        'driver_activated': ok,
        'package_selected': "package 'bochs'" in log,
        'requested': list(server.requests),
        'hidden_job_message': 'no Store entry' in log,
        'fb_driver_changed': 'Bochs' in log or 'Retained native module' in log,
    }
    server.stop()
    # ---- 2. tampered package ------------------------------------------------
    tampered = OUT / 'tampered'
    tampered.mkdir(exist_ok=True)
    data = bytearray((PKG / 'bochs.ndpk').read_bytes())
    data[64] ^= 0x01  # one flipped bit inside the signed module
    (tampered / 'bochs.ndpk').write_bytes(bytes(data))
    server = Server(tampered)
    ok, log = boot('tampered', ['-vga', 'std'], 'gpudriverlocal gpuautokeep', 'signature rejected', args.timeout, 4502)
    report['tampered'] = {'rejected': ok, 'activated': 'Retained native module' in log, 'requested': list(server.requests)}
    server.stop()
    # ---- 3. adapter with no catalog entry ------------------------------------
    server = Server(PKG)
    ok, log = boot('nomatch', ['-vga', 'none', '-device', 'virtio-vga'], 'gpudriverlocal gpuautokeep', 'nothing downloaded', args.timeout, 4503)
    report['nomatch'] = {'logged_no_package': ok, 'requested': list(server.requests)}
    server.stop()
    print(json.dumps(report, indent=2))
    (OUT / 'report.json').write_text(json.dumps(report, indent=2))
    good = (report['match']['driver_activated'] and report['match']['requested'] == ['/bochs.ndpk'] and
            report['tampered']['rejected'] and not report['tampered']['activated'] and
            report['nomatch']['logged_no_package'] and not report['nomatch']['requested'])
    sys.exit(0 if good else 1)


if __name__ == '__main__':
    main()
