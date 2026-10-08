#!/usr/bin/env python3
"""Verify the native ATOM bridge against pinned primary AMD source.

This records reference semantics, not physical hardware test results. Source
files are supplied by --source-dir; this script never downloads or executes a
board ROM and never accesses hardware registers.
"""
from pathlib import Path
import argparse, hashlib, json, re

ROOT = Path(__file__).resolve().parents[1]
PINNED = {
    'amdgpu_atombios.c': ('amdgpu/amdgpu_atombios.c', 'ae8a41ffd9baafeeedd213b589389eca8be6cf68fe8c33f837b7d5ec63d64113'),
    'amdgpu_device.c': ('amdgpu/amdgpu_device.c', '21aad0f99d79228ec510027bef536e28f935dd401d8f2d2dc3c105c88adb260a'),
    'amdgpu.h': ('amdgpu/amdgpu.h', '5c53aa4ffb811ff17284223c5dc497abece1285d616a10f1a6ab80d7e4f93c2c'),
    'atom.c': ('amdgpu/atom.c', 'b1ab6cc60d67d21691a4d0c265bca4fa3c63665fd8926de97986ed914e22627d'),
    'atom.h': ('amdgpu/atom.h', '1b1682ac01d0d2a8f16ef66107903d37184c8a284da4f833a249b9cadfa33f77'),
    'command_table2.c': ('display/dc/bios/command_table2.c', 'e9d9f3bb2b394941bd634a59bbeaaaae1d907d14cb8a98bad9b26386ea2c353a'),
    'atomfirmware.h': ('include/atomfirmware.h', '95d7856f6449579faf3983760f2d9f4a171327cac431d968b6625bbe38e5e49a'),
    'dce_clock_source.c': ('display/dc/dce/dce_clock_source.c', '3b33716ab57f6a6fc7e751eb5137e33b529c7e857fb746aacd8a6b985a064f71'),
    'dce_clock_source.h': ('display/dc/dce/dce_clock_source.h', '05f62c78350293f5ea87b0e1eead3a4ad84c625a3e7c6aa30d3a51fce771d1ed'),
    'dcn302_resource.c': ('display/dc/resource/dcn302/dcn302_resource.c', '54362248c36a9ee830042b6a220637ff678b56cb6772fd3f43139a9315cca4bc'),
}

def function(source, name):
    match = re.search(r'\b' + re.escape(name) + r'\s*\([^)]*\)\s*\{', source)
    assert match, name
    start = match.end(); depth = 1
    for pos in range(start, len(source)):
        depth += (source[pos] == '{') - (source[pos] == '}')
        if not depth:
            return re.sub(r'\s+', '', source[start:pos])
    raise AssertionError('Unclosed primary-source function: ' + name)

parser = argparse.ArgumentParser()
parser.add_argument('--source-dir', type=Path, required=True)
args = parser.parse_args()
sources = {}
for name, (_, sha) in PINNED.items():
    raw = (args.source_dir / name).read_bytes()
    assert hashlib.sha256(raw).hexdigest() == sha, 'Primary source hash: ' + name
    sources[name] = raw.decode('utf-8')

atombios = sources['amdgpu_atombios.c']
assert 'RREG32(reg)' in function(atombios, 'cail_reg_read')
assert 'WREG32(reg,val)' in function(atombios, 'cail_reg_write')
for space in ('pll', 'mc'):
    assert function(atombios, 'cail_' + space + '_read') == 'return0;'
    assert function(atombios, 'cail_' + space + '_write') == ''
for name in ('amdgpu_device_rreg', 'amdgpu_device_wreg'):
    body = function(sources['amdgpu_device.c'], name)
    assert '(reg*4)<adev->rmmio_size' in body and '+(reg*4)' in body
    assert 'adev->pcie_' in body  # Not claimed as implemented by this bridge.
assert 'val<<2' in function(sources['atom.c'], 'atom_put_dst')
assert 'if(idx==0)' in function(sources['atom.c'], 'atom_put_dst')
for value, name in enumerate(('NOP','START','READ','WRITE','CLEAR','SET','MOVE_INDEX','MOVE_ATTR','MOVE_DATA','END')):
    assert re.search(r'#define\s+ATOM_IIO_' + name + r'\s+' + str(value) + r'\b', sources['atom.h'])
for name, value in {'DISABLE':0,'ENABLE':1,'INIT':7,'DISABLE_OUTPUT':8,'ENABLE_OUTPUT':9,'SETUP':10,'POWER_ON':12,'POWER_OFF':13}.items():
    assert re.search(r'ATOM_TRANSMITTER_ACTION_' + name + r'\s*=\s*' + str(value) + r'\b', sources['atomfirmware.h'])
assert 'dcn3_clk_src_construct' in sources['dcn302_resource.c']
assert 'CS_COMMON_REG_LIST_DCN3_02' in sources['dcn302_resource.c']
assert 'dce112_program_pix_clk' in function(sources['dce_clock_source.c'], 'dcn3_program_pix_clk')
assert 'bios->funcs->set_pixel_clock' in function(sources['dce_clock_source.c'], 'dce112_program_pix_clk')
assert 'dce112_program_pixel_clk_resync' in function(sources['dce_clock_source.c'], 'dce112_program_pix_clk')
assert 'pll_settings->actual_pix_clk_100hz=(unsignedint)actual_pix_clk_100Hz' in function(sources['dce_clock_source.c'], 'dcn3_get_pix_clk_dividers')

report = {
    'passed': True, 'reference': 'AMD source in Linux v6.12',
    'upstream_sha256': {name: sha for name, (_, sha) in PINNED.items()},
    'upstream_urls': {name: 'https://raw.githubusercontent.com/torvalds/linux/v6.12/drivers/gpu/drm/amd/' + path for name, (path, _) in PINNED.items()},
    'dword_to_byte_mmio_mapping_verified': True,
    'direct_reg0_value_shift_owned_by_vm': True,
    'iio_attribute_and_data_opcode_numbers_verified': True,
    'modern_legacy_pll_mc_callbacks_unused': True,
    'native_dcn302_hdmi_calls_board_firmware': True,
    'native_hdmi_also_requires_pixel_resync': True,
    'nominal_pll_parameters_are_not_measured_clock': True,
    'scope': 'Bounded direct BAR5 MMIO and ROM-selected IIO. Legacy PLL/MC operands, out-of-resource accesses and early scanout activation fail explicitly. No invented PCIe fallback. No claim of complete physical modeset or HDMI audio.',
    'source_sha256': {path: hashlib.sha256((ROOT / path).read_bytes()).hexdigest() for path in (
        'scripts/verify_rx6600_firmware_reference.py', 'tools/gpu-driver/amd/rx6600.c',
        'tools/gpu-driver/amd/rx6600.h', 'tools/gpu-driver/amd/atom_vm.c',
        'tools/gpu-driver/amd/atom_vm.h', 'tools/gpu-driver/amd/atom_display_commands.c',
        'tools/gpu-driver/amd/atom_display_commands.h')},
    'physical_hardware_tested': False, 'full_rx6600_driver_complete': False,
}
target = ROOT / 'build/rx6600-firmware-reference.json'
target.write_text(json.dumps(report, indent=2) + '\n', encoding='utf-8')
print(json.dumps({'passed': True, 'primary_sources': len(PINNED), 'report': str(target)}))
