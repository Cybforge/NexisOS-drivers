#!/usr/bin/env python3
"""QEMU test of the start-up countdown and the trial prompt of "trial" GPU packages.

The shipped catalog marks only real hardware drivers as trial packages, and QEMU has no such hardware.  This test
therefore needs an image whose catalog marks the virtual Bochs adapter as trial (set "trial": true for "bochs" in
tools/gpu-driver/catalog.json, run scripts/build_gpu_catalog.py and scripts/build.py, copy build/nexis.img to
build/gpu-countdown-test/trial.img, then restore the catalog and rebuild).  Pass the copy with --image.

Scenarios (kernel option gpudriverlocal, package served by a local HTTP server):
  1. no key            -> countdown notice, module is NOT started before ~10 s, starts after that, trial prompt follows,
                          no confirmation -> automatic revert after 15 s;
  2. Esc in countdown  -> module is never started, firmware display retained;
  3. gpuautokeep       -> no countdown, module starts immediately.
Only disposable files below build/ are touched.
"""
import argparse, http.server, json, socketserver, sys, threading, time
from pathlib import Path
sys.path.insert(0, str(Path(__file__).resolve().parent))
from vm import VM

ROOT = Path(__file__).resolve().parents[1]
PKG = ROOT / 'build' / 'gpu-drivers' / 'packages'
OUT = ROOT / 'build' / 'gpu-countdown-test'
OUT.mkdir(parents=True, exist_ok=True)


class Server:
    def __init__(self, directory, port=8930):
        handler = lambda *a, **k: http.server.SimpleHTTPRequestHandler(*a, directory=str(directory), **k)
        handler.log_message = lambda *a: None
        socketserver.TCPServer.allow_reuse_address = True
        self.httpd = socketserver.TCPServer(('0.0.0.0', port), handler)
        threading.Thread(target=self.httpd.serve_forever, daemon=True).start()

    def stop(self):
        self.httpd.shutdown()
        self.httpd.server_close()


def wait_for(vm, text, timeout):
    """Seconds until `text` shows up in the serial log, or None."""
    t0 = time.time()
    return time.time() - t0 if vm.wait_serial(text, timeout) else None


def run(name, image, cmdline, port):
    return VM(image=str(image), cmdline=cmdline, mem='1024M', serial=OUT / (name + '.log'), port=port, extra=['-vga', 'std'])


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--image', required=True, help='image built with a trial-marked bochs package')
    ap.add_argument('--timeout', type=int, default=120)
    args = ap.parse_args()
    image = Path(args.image)
    report = {}
    server = Server(PKG)
    try:
        # 1. nothing pressed: countdown, start, trial prompt, automatic revert
        vm = run('countdown', image, 'gpudriverlocal', 4511)
        verified = vm.wait_serial('starting in 10 s unless Esc', args.timeout)
        t0 = time.time()
        started = vm.wait_serial('Retained native module', 30)
        waited = time.time() - t0
        early = 'Retained native module' in vm.log()[:vm.log().find('starting in 10 s')] if verified else None
        reverted = vm.wait_serial('Display mode reverted to firmware output', 40)
        log = vm.log()
        vm.quit()
        report['countdown'] = {'announced': verified, 'started_after_s': round(waited, 1), 'started': started,
                               'not_started_before_announce': early is False, 'auto_reverted': reverted,
                               'no_crash_marker_left': 'did not finish starting' not in log}
        # 2. Esc during the countdown
        vm = run('escape', image, 'gpudriverlocal', 4512)
        verified = vm.wait_serial('starting in 10 s unless Esc', args.timeout)
        time.sleep(1.5)
        vm.key('esc')
        skipped = vm.wait_serial('skipped by the user', 20)
        time.sleep(14)  # well past the 10 s mark
        log = vm.log()
        vm.quit()
        report['escape'] = {'announced': verified, 'skipped': skipped, 'never_started': 'Retained native module' not in log}
        # 3. gpuautokeep: no countdown
        vm = run('autokeep', image, 'gpudriverlocal gpuautokeep', 4513)
        started = vm.wait_serial('Retained native module', args.timeout)
        time.sleep(3)
        log = vm.log()
        vm.quit()
        report['autokeep'] = {'started': started, 'no_countdown': 'starting in 10 s' not in log}
    finally:
        server.stop()
    print(json.dumps(report, indent=2))
    (OUT / 'report.json').write_text(json.dumps(report, indent=2))
    c, e, a = report['countdown'], report['escape'], report['autokeep']
    good = (c['announced'] and c['started'] and c['started_after_s'] >= 7 and c['not_started_before_announce'] and c['auto_reverted'] and
            c['no_crash_marker_left'] and e['announced'] and e['skipped'] and e['never_started'] and a['started'] and a['no_countdown'])
    sys.exit(0 if good else 1)


if __name__ == '__main__':
    main()
