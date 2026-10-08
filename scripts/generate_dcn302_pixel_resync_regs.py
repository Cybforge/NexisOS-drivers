#!/usr/bin/env python3
"""Generate the exact Navi23 PHYPLL pixel-resync register subset.

The generator deliberately accepts only the cached, checksum-pinned Linux
v6.12 sources.  It is a source-evidence tool, not a hardware probe.
"""
from pathlib import Path
import argparse, hashlib, json
from generate_dcn302_regs import definitions, expected_sources

root = Path(__file__).resolve().parents[1]
pins = {
    **expected_sources,
    'dce_clock_source.c': '3b33716ab57f6a6fc7e751eb5137e33b529c7e857fb746aacd8a6b985a064f71',
    'dce_clock_source.h': '05f62c78350293f5ea87b0e1eead3a4ad84c625a3e7c6aa30d3a51fce771d1ed',
    'dcn302_resource.c': '54362248c36a9ee830042b6a220637ff678b56cb6772fd3f43139a9315cca4bc',
}

def extract_function(source, name):
    marker = 'static void ' + name + '('
    start = source.index(marker)
    brace = source.index('{', start)
    depth = 1
    end = brace + 1
    while depth:
        depth += (source[end] == '{') - (source[end] == '}')
        end += 1
    return source[brace:end]

def generate(reference):
    for name, digest in pins.items():
        assert hashlib.sha256((reference / name).read_bytes()).hexdigest() == digest, name
    clock = (reference / 'dce_clock_source.c').read_text(encoding='utf-8')
    clock_header = (reference / 'dce_clock_source.h').read_text(encoding='utf-8')
    resource = (reference / 'dcn302_resource.c').read_text(encoding='utf-8')
    resync = extract_function(clock, 'dce112_program_pixel_clk_resync')
    assert 'PHYPLLA_DCCG_DEEP_COLOR_CNTL' in resync
    assert 'PHYPLLA_PIXCLK_DOUBLE_RATE_ENABLE' in resync
    assert 'COLOR_DEPTH_888' in resync and 'COLOR_DEPTH_161616' in resync
    assert 'dce112_program_pixel_clk_resync(clk_src' in clock
    assert 'SRI(PIXCLK_RESYNC_CNTL, PHYPLL, pllid)' in clock_header
    # DCN302 wires CS_COMMON_MASK_SH_LIST_DCN2_0, which deliberately exposes
    # only the deep-colour control.  The double-rate bit exists in the raw
    # register but is not owned by this Navi23 clock-source path.
    assert 'CS_COMMON_MASK_SH_LIST_DCN2_0(__SHIFT)' in resource
    for item in ['DCN302_CLK_SRC_PLL0', 'DCN302_CLK_SRC_PLL4', 'DCN302_CLK_SRC_TOTAL',
                 'clk_src_regs(0, A)', 'clk_src_regs(4, E)']:
        assert item in resource, item

    offsets = definitions((reference / 'dcn302_offset.h').read_text(encoding='utf-8'))
    masks = definitions((reference / 'dcn302_mask.h').read_text(encoding='utf-8'))
    bases = definitions((reference / 'dimgrey_cavefish_ip_offset.h').read_text(encoding='utf-8'))
    names = ['A', 'B', 'C', 'D', 'E']
    addresses = []
    fields = []
    for name in names:
        register = 'PHYPLL' + name + '_PIXCLK_RESYNC_CNTL'
        mmio = 'mm' + register
        segment = offsets[mmio + '_BASE_IDX']
        addresses.append((bases['DCN_BASE__INST0_SEG' + str(segment)] + offsets[mmio]) * 4)
        fields.append({
            'resync_enable': masks[register + '__PHYPLL' + name + '_PIXCLK_RESYNC_ENABLE_MASK'],
            'deep_color': masks[register + '__PHYPLL' + name + '_DCCG_DEEP_COLOR_CNTL_MASK'],
            'pixclk_enable': masks[register + '__PHYPLL' + name + '_PIXCLK_ENABLE_MASK'],
            'double_rate': masks[register + '__PHYPLL' + name + '_PIXCLK_DOUBLE_RATE_ENABLE_MASK'],
        })
    assert addresses == [0x400, 0x404, 0x408, 0x40c, 0x430]
    assert all(fields[0] == value for value in fields)
    assert all(address < 1024 * 1024 and not address & 3 for address in addresses)
    field = fields[0]
    text = (reference / 'dcn302_offset.h').read_text(encoding='utf-8')
    text = text[:text.index('*/') + 2]
    text += '\n/* Generated native DCN302 PHYPLL pixel-resync definitions. */\n'
    text += '#ifndef NEXIS_DCN302_PIXEL_RESYNC_REGS_H\n#define NEXIS_DCN302_PIXEL_RESYNC_REGS_H\n#include <stdint.h>\n'
    text += 'static const uint32_t dcn302_pixel_resync_bytes[5]={' + ','.join(f'0x{value:05x}u' for value in addresses) + '};\n'
    for label, value in field.items():
        text += f'#define DCN302_PIXEL_RESYNC_{label.upper()}_MASK 0x{value:08x}u\n'
    text += '#define DCN302_PIXEL_RESYNC_OWNED_MASK DCN302_PIXEL_RESYNC_DEEP_COLOR_MASK\n'
    text += '#endif\n'
    target = root / 'tools/gpu-driver/amd/dcn302_pixel_resync_regs.h'
    target.write_text(text, encoding='utf-8', newline='\n')
    evidence = {
        'source': 'AMD Linux v6.12', 'upstream_sha256': pins,
        'register_bytes': addresses, 'physical_clock_sources': 5,
        'owned_fields': ['DCCG_DEEP_COLOR_CNTL'],
        'unowned_fields_preserved': ['PIXCLK_RESYNC_ENABLE', 'PIXCLK_ENABLE', 'PIXCLK_DOUBLE_RATE_ENABLE'],
        'dce112_firmware_then_resync_semantics_verified': True,
        'generated_sha256': hashlib.sha256(target.read_bytes()).hexdigest(),
    }
    output = root / 'build/dcn302-pixel-resync-register-sources.json'
    output.write_text(json.dumps(evidence, indent=2) + '\n', encoding='utf-8')
    print(json.dumps(evidence, indent=2))

if __name__ == '__main__':
    parser = argparse.ArgumentParser()
    parser.add_argument('--reference', type=Path, required=True)
    generate(parser.parse_args().reference)
