#!/usr/bin/env python3
"""Publish the signed GPU driver packages (and the matching source mirror) to GitHub.

Target : https://github.com/Cybforge/NexisOS-drivers  (branch main).  The old
         account name "jojojonas169-debug" redirects there; the kernel tries
         both raw.githubusercontent.com paths.
Token  : read from the file given by NEXIS_GITHUB_TOKEN_FILE, default
         ~/Desktop/Git key (wirklich).txt.  It is only ever sent to
         api.github.com in an Authorization header and is never printed,
         logged or written anywhere.
Upload : one commit through the Git Data API.  Only an explicit allow-list is
         uploaded (below); the signing key (~/.nexis/) is not in it, and
         neither are build outputs other than the signed packages, ISO images
         or anything outside the project.  Unchanged files are skipped by
         comparing git blob hashes.

    python scripts/publish_gpu_drivers.py            # dry run: list what would change
    python scripts/publish_gpu_drivers.py --publish  # create the commit
"""
from pathlib import Path
import argparse, base64, fnmatch, hashlib, json, os, sys, urllib.error, urllib.request

ROOT = Path(__file__).resolve().parents[1]
REPO = 'Cybforge/NexisOS-drivers'
BRANCH = 'main'

# (local glob relative to ROOT, repo path prefix mapping handled below)
ALLOW = [
    'tools/gpu-driver/**', 'kernel/drivers/gpu/*', 'kernel/drivers/audio/hdmi.*', 'kernel/drivers/audio/hda.*',
    'kernel/include/bootinfo.h', 'kernel/mm/vmm.*', 'boot/efi/edid.h', 'boot/efi/efi.h', 'boot/efi/gpu_rom.h',
    'scripts/build_gpu_*.py', 'scripts/gpu_package.py', 'scripts/publish_gpu_drivers.py', 'scripts/generate_dcn302_*.py',
    'scripts/test_gpu_*.py', 'scripts/test_dcn302*.py', 'scripts/test_rx6600.py', 'scripts/test_rx6600_modeset.py',
    'scripts/test_modeset_seq.py', 'scripts/test_cta_audio.py', 'scripts/test_atom_*.py',
    'scripts/test_edid.py', 'scripts/test_hdmi_codec.py', 'scripts/verify_rx6600_firmware_reference.py',
    'scripts/import_dcn30_dml.py', 'scripts/update_edid_timings.py',
    'tests/host/test_dcn302*.c', 'tests/host/test_rx6600.c', 'tests/host/test_rx6600_modeset.c', 'tests/host/test_modeset_seq.c',
    'tests/host/test_cta_audio.c', 'tests/host/test_atom_*.c', 'tests/host/test_gpu_*.c',
    'tests/host/test_efi_*.c', 'tests/host/test_hdmi_codec.c', 'tests/host/test_dml_scope.c', 'tests/host/dml_pic_module.*',
    'tests/host/edid_exports.c', 'tests/host/gpu_*_fixture.c',
    'docs/PHYSICAL_DISPLAY_*.md', 'docs/GPU_*.md',
    'assets/skel/usr/share/licenses/nexis-display/*',
]
EXCLUDE = ['*/__pycache__/*', '*.pyc', '*.exe', '*.pdb', '*.elf', '*.o']


def allowed(rel):
    return any(fnmatch.fnmatch(rel, p) or (p.endswith('/**') and rel.startswith(p[:-2])) for p in ALLOW) and \
           not any(fnmatch.fnmatch(rel, p) for p in EXCLUDE)


def collect():
    files = {}
    for path in ROOT.rglob('*'):
        if not path.is_file():
            continue
        rel = path.relative_to(ROOT).as_posix()
        if rel.startswith(('build/', '.git/', 'third_party/', 'work/')) or not allowed(rel):
            continue
        if rel in ('tools/gpu-driver/README.md', 'tools/gpu-driver/LICENSE'):
            continue  # published once at the repository root instead
        files[rel] = path.read_bytes()
    # Signed packages and the public catalog come from the build output.
    dist = ROOT / 'build' / 'gpu-drivers'
    for pkg in sorted((dist / 'packages').glob('*.ndpk')):
        files['packages/' + pkg.name] = pkg.read_bytes()
    if (dist / 'catalog.json').exists():
        files['catalog.json'] = (dist / 'catalog.json').read_bytes()
    # The repository front page is the driver README.
    for name in ('README.md', 'LICENSE'):
        source = ROOT / 'tools' / 'gpu-driver' / name
        if source.exists():
            files[name] = source.read_bytes()
    return files


def blob_sha(data):
    return hashlib.sha1(b'blob %d\0' % len(data) + data).hexdigest()


class Api:
    def __init__(self, token):
        self.token = token

    def call(self, method, path, body=None):
        data = json.dumps(body).encode() if body is not None else None
        req = urllib.request.Request('https://api.github.com' + path, data=data, method=method, headers={
            'Authorization': 'token ' + self.token, 'Accept': 'application/vnd.github+json',
            'User-Agent': 'nexis-driver-publisher', 'Content-Type': 'application/json'})
        try:
            with urllib.request.urlopen(req, timeout=120) as r:
                return json.load(r)
        except urllib.error.HTTPError as e:
            raise SystemExit('GitHub API %s %s failed: HTTP %d %s' % (method, path, e.code, e.read().decode()[:200]))


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--publish', action='store_true')
    ap.add_argument('--message', default='Update GPU driver packages and sources')
    args = ap.parse_args()
    token_file = Path(os.environ.get('NEXIS_GITHUB_TOKEN_FILE') or Path.home() / 'Desktop' / 'Git key (wirklich).txt')
    api = Api(token_file.read_text().strip())
    ref = api.call('GET', '/repos/%s/git/ref/heads/%s' % (REPO, BRANCH))
    head = ref['object']['sha']
    commit = api.call('GET', '/repos/%s/git/commits/%s' % (REPO, head))
    tree = api.call('GET', '/repos/%s/git/trees/%s?recursive=1' % (REPO, commit['tree']['sha']))
    remote = {t['path']: t['sha'] for t in tree['tree'] if t['type'] == 'blob'}
    local = collect()
    changed = {p: d for p, d in local.items() if remote.get(p) != blob_sha(d)}
    print('%d files tracked locally, %d new or changed:' % (len(local), len(changed)))
    for p in sorted(changed):
        print('  %s %s (%d bytes)' % ('~' if p in remote else '+', p, len(changed[p])))
    if not args.publish or not changed:
        print('dry run' if not args.publish else 'nothing to publish')
        return
    entries = []
    for p, d in sorted(changed.items()):
        blob = api.call('POST', '/repos/%s/git/blobs' % REPO, {'content': base64.b64encode(d).decode(), 'encoding': 'base64'})
        entries.append({'path': p, 'mode': '100644', 'type': 'blob', 'sha': blob['sha']})
    new_tree = api.call('POST', '/repos/%s/git/trees' % REPO, {'base_tree': commit['tree']['sha'], 'tree': entries})
    message = args.message + '\n\nCo-Authored-By: Claude Sonnet 5.5 <noreply@anthropic.com>'
    new_commit = api.call('POST', '/repos/%s/git/commits' % REPO, {'message': message, 'tree': new_tree['sha'], 'parents': [head]})
    api.call('PATCH', '/repos/%s/git/refs/heads/%s' % (REPO, BRANCH), {'sha': new_commit['sha']})
    print('published commit', new_commit['sha'][:10], 'to', REPO)


if __name__ == '__main__':
    main()
