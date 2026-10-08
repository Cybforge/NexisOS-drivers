#!/usr/bin/env python3
"""NexisOS signed GPU driver package ("NDPK") helpers.

Why signatures instead of one compiled-in SHA-256 per driver
------------------------------------------------------------
The first generation of the download system pinned the SHA-256 of every
driver binary inside the kernel.  That is safe, but every driver fix then
needs a new kernel/ISO build and a re-flashed USB stick.  GPU drivers for
real hardware need many bring-up iterations, so the kernel now pins one
ECDSA P-256 *public key* instead.  A driver can be updated on GitHub without
touching the OS image, but only the holder of the private key can publish
code that the kernel accepts.  Owning the GitHub token alone is not enough.

File format (all integers little endian)::

    0   4   "NDPK"
    4   4   format = 1
    8   4   module_bytes (N)       size of the NDRV module that follows
    12  4   version                monotonic per package; the kernel keeps a
                                   minimum version so old signed builds can
                                   not be replayed
    16  16  package name           ASCII, NUL padded, must equal the catalog
    32  N   NDRV module            v2 "retained" module (see nexis_gpu_v2.h)
    32+N 64 signature              ECDSA P-256 over SHA-256(bytes[0:32+N]),
                                   raw r||s, big endian, 32 bytes each

The private key lives outside the project (``~/.nexis/gpu-signing-p256.pem``,
override with ``NEXIS_GPU_SIGNING_KEY``).  It is never uploaded.  Back it up:
if it is lost the kernel has to be rebuilt with a new public key.
"""
from pathlib import Path
import hashlib, os, shutil, struct, subprocess, tempfile

MAGIC = b'NDPK'
HEADER = 32
SIGNATURE = 64
MAX_MODULE = 64 + 1024 * 1024


def _openssl():
    for candidate in (os.environ.get('NEXIS_OPENSSL'), shutil.which('openssl'),
                      r'C:\Program Files\Git\mingw64\bin\openssl.exe', '/mingw64/bin/openssl'):
        if candidate and Path(candidate).exists():
            return candidate
    raise SystemExit('openssl not found (set NEXIS_OPENSSL)')


def key_path():
    return Path(os.environ.get('NEXIS_GPU_SIGNING_KEY') or Path.home() / '.nexis' / 'gpu-signing-p256.pem')


def public_key():
    """65-byte uncompressed P-256 point (0x04 || X || Y) of the signing key."""
    der = subprocess.run([_openssl(), 'ec', '-in', str(key_path()), '-pubout', '-outform', 'DER'],
                         check=True, capture_output=True).stdout
    point = der[-65:]
    assert point[0] == 4 and len(point) == 65
    return point


def _der_to_raw(der):
    # SEQUENCE { INTEGER r, INTEGER s }
    assert der[0] == 0x30
    pos = 2 if der[1] < 0x80 else 2 + (der[1] & 0x7f)
    out = b''
    for _ in range(2):
        assert der[pos] == 2
        length = der[pos + 1]
        value = der[pos + 2:pos + 2 + length]
        pos += 2 + length
        out += value.lstrip(b'\0').rjust(32, b'\0')
    assert len(out) == 64
    return out


def sign(data):
    with tempfile.TemporaryDirectory() as tmp:
        source = Path(tmp) / 'data'
        source.write_bytes(data)
        der = subprocess.run([_openssl(), 'dgst', '-sha256', '-sign', str(key_path()), str(source)],
                             check=True, capture_output=True).stdout
    return _der_to_raw(der)


def pack(name, version, module):
    raw_name = name.encode('ascii')
    assert 0 < len(raw_name) <= 15 and 0 < len(module) <= MAX_MODULE and 0 < version < 2 ** 32
    body = MAGIC + struct.pack('<III', 1, len(module), version) + raw_name.ljust(16, b'\0') + module
    return body + sign(body)


def verify(package):
    """Host-side check used by the build script and tests (needs openssl)."""
    if len(package) < HEADER + SIGNATURE or package[:4] != MAGIC:
        return False
    fmt, size, version = struct.unpack_from('<III', package, 4)
    if fmt != 1 or size != len(package) - HEADER - SIGNATURE:
        return False
    body, sig = package[:-SIGNATURE], package[-SIGNATURE:]
    r, s = int.from_bytes(sig[:32], 'big'), int.from_bytes(sig[32:], 'big')

    def integer(v):
        b = v.to_bytes(32, 'big').lstrip(b'\0') or b'\0'
        if b[0] & 0x80:
            b = b'\0' + b
        return bytes([2, len(b)]) + b
    seq = integer(r) + integer(s)
    der = bytes([0x30, len(seq)]) + seq
    with tempfile.TemporaryDirectory() as tmp:
        t = Path(tmp)
        (t / 'data').write_bytes(body)
        (t / 'sig').write_bytes(der)
        subprocess.run([_openssl(), 'ec', '-in', str(key_path()), '-pubout', '-out', str(t / 'pub.pem')],
                       check=True, capture_output=True)
        result = subprocess.run([_openssl(), 'dgst', '-sha256', '-verify', str(t / 'pub.pem'),
                                 '-signature', str(t / 'sig'), str(t / 'data')], capture_output=True)
    return result.returncode == 0


def describe(package):
    fmt, size, version = struct.unpack_from('<III', package, 4)
    return {'name': package[16:32].rstrip(b'\0').decode(), 'version': version, 'module_bytes': size,
            'package_bytes': len(package), 'sha256': hashlib.sha256(package).hexdigest()}


if __name__ == '__main__':
    import sys
    if len(sys.argv) == 2 and sys.argv[1] == 'pubkey':
        print(public_key().hex())
    elif len(sys.argv) == 3 and sys.argv[1] == 'verify':
        data = Path(sys.argv[2]).read_bytes()
        print('valid' if verify(data) else 'INVALID', describe(data))
    else:
        raise SystemExit('usage: gpu_package.py pubkey | verify FILE')
