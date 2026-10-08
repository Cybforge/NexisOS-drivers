#!/usr/bin/env python3
"""Generate the Navi23 HDMI-audio register subset (AZALIA endpoints + DCCG audio DTO).

Like the other generate_dcn302_*.py tools this accepts only the checksum-pinned
AMD Linux v6.12 headers (see generate_dcn302_regs.py) and is source evidence, not a
hardware probe.  The AZALIA pin-control registers are *indirect*: each of the six
audio endpoints has an INDEX/DATA pair, and the register number goes into INDEX
(Linux: dce_audio.c read/write_indirect_azalia_reg).
"""
from pathlib import Path
import argparse, hashlib, json
from generate_dcn302_regs import definitions, expected_sources

root = Path(__file__).resolve().parents[1]
ENDPOINTS = 6          # dcn302_resource.c: .num_audio = 6
INDIRECT = [           # (name, ix register name suffix)
    ('HOT_PLUG', 'HOT_PLUG_CONTROL'), ('CHANNEL_SPEAKER', 'CHANNEL_SPEAKER'),
    ('PIN_SENSE', 'RESPONSE_PIN_SENSE'), ('SINK_INFO0', 'SINK_INFO0'), ('SINK_INFO1', 'SINK_INFO1'),
] + [('DESCRIPTOR%d' % n, 'AUDIO_DESCRIPTOR%d' % n) for n in range(14)]


def generate(reference):
    for name, digest in expected_sources.items():
        assert hashlib.sha256((reference / name).read_bytes()).hexdigest() == digest, name
    offsets = definitions((reference / 'dcn302_offset.h').read_text(encoding='utf-8'))
    masks = definitions((reference / 'dcn302_mask.h').read_text(encoding='utf-8'))
    bases = definitions((reference / 'dimgrey_cavefish_ip_offset.h').read_text(encoding='utf-8'))

    def byte_address(register):
        segment = offsets['mm' + register + '_BASE_IDX']
        return (bases['DCN_BASE__INST0_SEG%d' % segment] + offsets['mm' + register]) * 4

    index_bytes, data_bytes = [], []
    for n in range(ENDPOINTS):
        index_bytes.append(byte_address('AZF0ENDPOINT%d_AZALIA_F0_CODEC_ENDPOINT_INDEX' % n))
        data_bytes.append(byte_address('AZF0ENDPOINT%d_AZALIA_F0_CODEC_ENDPOINT_DATA' % n))
    assert index_bytes[0] == (0x34c0 + 0x386) * 4 and data_bytes[0] == (0x34c0 + 0x387) * 4
    assert all(a < 1024 * 1024 and not a & 3 for a in index_bytes + data_bytes)
    ix = {}
    for label, suffix in INDIRECT:
        values = {offsets['ixAZF0ENDPOINT%d_AZALIA_F0_CODEC_PIN_CONTROL_%s' % (n, suffix)] for n in range(ENDPOINTS)}
        assert len(values) == 1, 'indirect index differs between endpoints: ' + suffix
        ix[label] = values.pop()
    assert ix['HOT_PLUG'] == 0x54 and ix['CHANNEL_SPEAKER'] == 0x25 and ix['DESCRIPTOR0'] == 0x28 and ix['SINK_INFO0'] == 0x3a
    p = 'AZF0ENDPOINT0_AZALIA_F0_CODEC_PIN_CONTROL_'
    fields = {
        'HOT_PLUG_CLOCK_GATING_DISABLE': masks[p + 'HOT_PLUG_CONTROL__CLOCK_GATING_DISABLE_MASK'],
        'HOT_PLUG_CLOCK_ON_STATE': masks[p + 'HOT_PLUG_CONTROL__CLOCK_ON_STATE_MASK'],
        'HOT_PLUG_AUDIO_ENABLED': masks[p + 'HOT_PLUG_CONTROL__AUDIO_ENABLED_MASK'],
        'SPEAKER_ALLOCATION': masks[p + 'CHANNEL_SPEAKER__SPEAKER_ALLOCATION_MASK'],
        'CHANNEL_ALLOCATION': masks[p + 'CHANNEL_SPEAKER__CHANNEL_ALLOCATION_MASK'],
        'HDMI_CONNECTION': masks[p + 'CHANNEL_SPEAKER__HDMI_CONNECTION_MASK'],
        'DP_CONNECTION': masks[p + 'CHANNEL_SPEAKER__DP_CONNECTION_MASK'],
        'DESC_MAX_CHANNELS': masks[p + 'AUDIO_DESCRIPTOR0__MAX_CHANNELS_MASK'],
        'DESC_FREQUENCIES': masks[p + 'AUDIO_DESCRIPTOR0__SUPPORTED_FREQUENCIES_MASK'],
        'DESC_BYTE2': masks[p + 'AUDIO_DESCRIPTOR0__DESCRIPTOR_BYTE_2_MASK'],
        'DESC_FREQUENCIES_STEREO': masks[p + 'AUDIO_DESCRIPTOR0__SUPPORTED_FREQUENCIES_STEREO_MASK'],
        'SINK_MANUFACTURER_ID': masks[p + 'SINK_INFO0__MANUFACTURER_ID_MASK'],
        'SINK_PRODUCT_ID': masks[p + 'SINK_INFO0__PRODUCT_ID_MASK'],
        'SINK_DESCRIPTION_LEN': masks[p + 'SINK_INFO1__SINK_DESCRIPTION_LEN_MASK'],
        'INDEX': masks['AZF0ENDPOINT0_AZALIA_F0_CODEC_ENDPOINT_INDEX__AZALIA_ENDPOINT_REG_INDEX_MASK'],
        'DTO0_SOURCE_SEL': masks['DCCG_AUDIO_DTO_SOURCE__DCCG_AUDIO_DTO0_SOURCE_SEL_MASK'],
        'DTO_SEL': masks['DCCG_AUDIO_DTO_SOURCE__DCCG_AUDIO_DTO_SEL_MASK'],
        'DTO0_PHASE': masks['DCCG_AUDIO_DTO0_PHASE__DCCG_AUDIO_DTO0_PHASE_MASK'],
        'DTO0_MODULE': masks['DCCG_AUDIO_DTO0_MODULE__DCCG_AUDIO_DTO0_MODULE_MASK'],
    }
    for n in range(1, ENDPOINTS):   # every endpoint instance carries the same field layout
        q = 'AZF0ENDPOINT%d_AZALIA_F0_CODEC_PIN_CONTROL_' % n
        assert masks[q + 'HOT_PLUG_CONTROL__AUDIO_ENABLED_MASK'] == fields['HOT_PLUG_AUDIO_ENABLED']
        assert masks[q + 'AUDIO_DESCRIPTOR0__SUPPORTED_FREQUENCIES_MASK'] == fields['DESC_FREQUENCIES']
        assert masks[q + 'CHANNEL_SPEAKER__HDMI_CONNECTION_MASK'] == fields['HDMI_CONNECTION']
    dto = {name: byte_address(name) for name in ('DCCG_AUDIO_DTO_SOURCE', 'DCCG_AUDIO_DTO0_PHASE', 'DCCG_AUDIO_DTO0_MODULE')}
    assert dto['DCCG_AUDIO_DTO_SOURCE'] == (0xc0 + 0xab) * 4
    text = (reference / 'dcn302_offset.h').read_text(encoding='utf-8')
    text = text[:text.index('*/') + 2]
    text += '\n/* Generated native DCN302 HDMI-audio definitions (AZALIA endpoints, DCCG audio DTO). */\n'
    text += '#ifndef NEXIS_DCN302_AUDIO_REGS_H\n#define NEXIS_DCN302_AUDIO_REGS_H\n#include <stdint.h>\n'
    text += '#define DCN302_AZ_ENDPOINTS %d\n' % ENDPOINTS
    text += 'static const uint32_t dcn302_az_index_bytes[%d]={%s};\n' % (ENDPOINTS, ','.join('0x%05xu' % v for v in index_bytes))
    text += 'static const uint32_t dcn302_az_data_bytes[%d]={%s};\n' % (ENDPOINTS, ','.join('0x%05xu' % v for v in data_bytes))
    text += 'enum dcn302_az_register {%s,DCN302_AZ_REGISTER_COUNT};\n' % ','.join('DCN302_AZ_R_' + label for label, _ in INDIRECT)
    text += 'static const uint16_t dcn302_az_ix[DCN302_AZ_REGISTER_COUNT]={%s};\n' % ','.join('0x%04x' % ix[label] for label, _ in INDIRECT)
    for label, value in fields.items():
        text += '#define DCN302_AZ_%s_MASK 0x%08xu\n' % (label, value)
    text += '#define DCN302_DTO_SOURCE_BYTES 0x%05xu\n#define DCN302_DTO0_PHASE_BYTES 0x%05xu\n#define DCN302_DTO0_MODULE_BYTES 0x%05xu\n' % (
        dto['DCCG_AUDIO_DTO_SOURCE'], dto['DCCG_AUDIO_DTO0_PHASE'], dto['DCCG_AUDIO_DTO0_MODULE'])
    text += '#endif\n'
    target = root / 'tools/gpu-driver/amd/dcn302_audio_regs.h'
    target.write_text(text, encoding='utf-8', newline='\n')
    evidence = {'source': 'AMD Linux v6.12', 'upstream_sha256': expected_sources, 'endpoints': ENDPOINTS,
                'index_bytes': index_bytes, 'data_bytes': data_bytes, 'indirect_indices': ix, 'dto_bytes': dto,
                'indirect_access_verified_against': 'dce_audio.c write/read_indirect_azalia_reg',
                'generated_sha256': hashlib.sha256(target.read_bytes()).hexdigest()}
    (root / 'build' / 'dcn302-audio-register-sources.json').write_text(json.dumps(evidence, indent=2) + '\n', encoding='utf-8')
    print(json.dumps(evidence, indent=2))


if __name__ == '__main__':
    parser = argparse.ArgumentParser()
    parser.add_argument('--reference', type=Path, required=True)
    generate(parser.parse_args().reference)
