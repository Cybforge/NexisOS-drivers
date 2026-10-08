#!/usr/bin/env python3
"""Installed-system persistence of the hidden GPU package, QEMU only.

  1. Live session with a matching virtual adapter installs NexisOS onto a
     freshly created disposable disk.  The installer must first finish the
     background package download and then copy the *verified signed package*
     to the new root's persistent /opt/nexis-drivers/<name>.ndpk.
  2. The installed system boots twice WITHOUT the package server: the driver
     must come from the persistent cache ("direkt aktiv", no network needed).

Only files below the given directory are created or modified; no physical
device is ever opened.
"""
import argparse, json, re, struct, subprocess, sys, time
from pathlib import Path
sys.path.insert(0, str(Path(__file__).resolve().parent))
from vm import VM, BASE
from test_gpu_platform_qemu import Server

PKG = BASE / 'build' / 'gpu-drivers' / 'packages'


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--directory', type=Path, default=BASE / 'build' / 'gpu-install-test')
    args = ap.parse_args()
    d = args.directory.resolve()
    d.mkdir(parents=True, exist_ok=True)
    disk = d / 'disposable-install.img'
    if disk.exists():
        disk.unlink()
    with disk.open('xb') as f:
        f.truncate(2 * 1024 ** 3)
    report = {'disk': str(disk)}

    def boot(name, installed=False, cmdline=''):
        return VM(image=BASE / 'build/nexis.img', disks=[disk], boot_only_disks=installed, cmdline=cmdline,
                  serial=d / (name + '.log'), port=4491)

    server = Server(PKG)
    vm = boot('install', cmdline='autoinstall=sata1 autopoweroff lang=en kbd=us user=tester host=nexis-test gpudriverlocal gpuautokeep')
    try:
        assert vm.wait_serial('[INSTALL] Automatische Installation erfolgreich', 180), vm.log()[-1500:]
        report['installed'] = True
        report['driver_active_during_install'] = 'Retained native module' in vm.log()
    finally:
        vm.quit()
    server.stop()
    report['server_requests_during_install'] = list(server.requests)
    # compare the persisted bytes with the signed package (ext4 root)
    with disk.open('rb') as f:
        f.seek(1024)
        entries = f.read(128 * 128)
    linux_guid = bytes.fromhex('af3dc60f838472478e793d69d8477de4')
    root = next(entries[p:p + 128] for p in range(0, len(entries), 128) if entries[p:p + 16] == linux_guid)
    offset = struct.unpack_from('<Q', root, 32)[0] * 512
    check = subprocess.run([sys.executable, BASE / 'tests/host/ext4_check.py', disk, '--offset', str(offset),
                            '--expect-file', '/opt/nexis-drivers/bochs.ndpk', PKG / 'bochs.ndpk'], capture_output=True)
    (d / 'persist-check.log').write_bytes(check.stdout + check.stderr)
    report['package_persisted_exactly'] = check.returncode == 0
    # two reboots with the package server stopped
    for number in (1, 2):
        vm = boot('installed-%d' % number, installed=True, cmdline='gpuautokeep selftest')
        try:
            ok = vm.wait_serial('Retained native module', 90)
            time.sleep(3)
            log = vm.log()
            report['boot_%d' % number] = {'driver_from_cache': ok, 'panic': 'KERNEL PANIC' in log,
                                          'activated_before_any_download_result': ok and (log.find('Retained native module') < log.find('Download failed') or 'Download failed' not in log)}
        finally:
            vm.quit()
    print(json.dumps(report, indent=2))
    (d / 'report.json').write_text(json.dumps(report, indent=2))
    good = (report.get('installed') and report['package_persisted_exactly'] and
            all(report['boot_%d' % n]['driver_from_cache'] and not report['boot_%d' % n]['panic'] for n in (1, 2)))
    sys.exit(0 if good else 1)


if __name__ == '__main__':
    main()
