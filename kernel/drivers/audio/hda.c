/*
 * Intel High Definition Audio (also used by AMD and other compatible controllers).
 *
 * The controller is reset, the codecs are found through STATESTS and driven through the CORB/RIRB command rings (polled).
 * For every analog output pin (line out, speaker, headphone) a path to an output converter (DAC) is searched through the
 * connection lists of the widgets; all amplifiers on the path are unmuted, the selectors are set, the pins are switched to
 * output (EAPD on where the pin has it). Playback uses one output stream with a buffer descriptor list: a block of PCM
 * (48 kHz, 16 bit, stereo) is copied into the cyclic buffer, the stream runs once through it and stops.
 */
#include "hda.h"
#include "hdmi.h"
#include "../../lib/coop.h"
#include "../pci.h"
#include "../../mm/pmm.h"
#include "../../mm/vmm.h"
#include "../../include/string.h"
#include "../../arch/x86_64/pit.h"
#include "../net/nic_support.h"
#include "../serial.h"
#include "../../sys/cmdline.h"

#define GCAP     0x00
#define GCTL     0x08
#define STATESTS 0x0E
#define CORBLBASE 0x40
#define CORBUBASE 0x44
#define CORBWP   0x48
#define CORBRP   0x4A
#define CORBCTL  0x4C
#define CORBSIZE 0x4E
#define RIRBLBASE 0x50
#define RIRBUBASE 0x54
#define RIRBWP   0x58
#define RINTCNT  0x5A
#define RIRBCTL  0x5C
#define RIRBSIZE 0x5E

#define MAX_NODES 128
#define MAX_CONN  16
#define STREAM_TAG 1
#define FRAGMENT  4096
#define MAX_FRAGMENTS 64   /* 256 KiB: 1.36 s of 48 kHz stereo */

typedef struct {
    uint8_t type;            /* widget type */
    uint32_t caps;
    uint8_t nconn;
    uint8_t conn[MAX_CONN];
    uint32_t cfg;            /* pin configuration default */
    uint32_t pincaps;
    uint32_t amp_out;        /* output amp capabilities */
    uint32_t amp_in;
    bool visited;
} widget_t;

typedef struct { uint8_t nid, sel, in_index; } hop_t;   /* along the path from a pin to the converter */

static struct {
    bool found, ready, digital;
    int digital_pin;
    pci_device_t *pci;
    uintptr_t mmio;
    uint32_t *corb;
    uint64_t *rirb;
    uint64_t corb_phys, rirb_phys, bdl_phys, buf_phys;
    int corb_entries, rirb_entries;
    uint16_t rirb_last;
    int iss, oss;
    int cad, afg, first_node, node_count;
    uint32_t codec_id,codec_revision;
    widget_t w[MAX_NODES];
    int dac;                 /* converter that carries the stream */
    int vol_nid, vol_steps, vol_offset;   /* the amplifier that follows the volume setting */
    uint8_t volume;
    int verb_fail;           /* commands without an answer: after a few the codec is given up */
    char name[48];
    uint16_t fmt;
} H;

static inline uint32_t rd32(uint32_t r) { return *(volatile uint32_t *)(H.mmio + r); }
static inline uint16_t rd16(uint32_t r) { return *(volatile uint16_t *)(H.mmio + r); }
static inline uint8_t rd8(uint32_t r) { return *(volatile uint8_t *)(H.mmio + r); }
static inline void wr32(uint32_t r, uint32_t v) { *(volatile uint32_t *)(H.mmio + r) = v; }
static inline void wr16(uint32_t r, uint16_t v) { *(volatile uint16_t *)(H.mmio + r) = v; }
static inline void wr8(uint32_t r, uint8_t v) { *(volatile uint8_t *)(H.mmio + r) = v; }
static inline void mb(void) { __asm__ volatile("mfence" ::: "memory"); }

static bool wait8(uint32_t reg, uint8_t mask, uint8_t val, uint32_t ms) {
    uint64_t t0 = pit_get_ticks();
    unsigned spins = 0;
    while ((rd8(reg) & mask) != val) { if (++spins >= 30000000 || pit_elapsed(pit_get_ticks(), t0) > ms) return false; watchdog_pet(); }
    return true;
}

static bool wait16(uint32_t reg, uint16_t mask, uint16_t val, uint32_t ms) {
    uint64_t t0 = pit_get_ticks();
    unsigned spins = 0;
    while ((rd16(reg) & mask) != val) { if (++spins >= 30000000 || pit_elapsed(pit_get_ticks(), t0) > ms) return false; watchdog_pet(); }
    return true;
}

/* ------------------------------------------------------------ codec commands */

static bool verb(int nid, uint32_t v, uint32_t *resp) {
    if (H.verb_fail >= 3 || !H.corb_entries || !H.rirb_entries || nid < 0 || nid > 255) return false;
    uint16_t wp = (uint16_t)((rd16(CORBWP) + 1) % H.corb_entries);
    uint64_t t0 = pit_get_ticks();
    unsigned spins = 0;
    while ((rd16(CORBRP) & 0xFF) == wp) { if (++spins >= 30000000 || pit_elapsed(pit_get_ticks(), t0) > 100) { H.verb_fail++; return false; } }   /* ring full */
    H.corb[wp] = ((uint32_t)H.cad << 28) | ((uint32_t)nid << 20) | (v & 0xFFFFF);
    mb();
    wr16(CORBWP, wp);
    t0 = pit_get_ticks();
    spins = 0;
    for (;;) {
        uint16_t rwp = (uint16_t)(rd16(RIRBWP) & 0xFF);
        if (rwp >= H.rirb_entries || H.rirb_last >= H.rirb_entries) { H.verb_fail++; return false; }
        while (H.rirb_last != rwp) {
            H.rirb_last = (uint16_t)((H.rirb_last + 1) % H.rirb_entries);
            uint64_t e = H.rirb[H.rirb_last];
            if ((e >> 32) & 0x10) continue;   /* unsolicited response */
            if (((e >> 32) & 15) != (unsigned)H.cad) continue;
            if (resp) *resp = (uint32_t)e;
            wr8(0x5D, 0x05);   /* RIRBSTS: response interrupt flag and overrun flag */
            return true;
        }
        if (++spins >= 30000000 || pit_elapsed(pit_get_ticks(), t0) > 100) {
            H.verb_fail++;
            kprintf("[HDA] Verb %08x ohne Antwort (CORB Schreib %u Lese %u, RIRB %u, Steuerung %02x %02x)\n", (unsigned)H.corb[wp], wp, rd16(CORBRP), rd16(RIRBWP), rd8(CORBCTL), rd8(RIRBCTL));
            kprintf("[HDA]   CORB %08x %08x %08x %08x, RIRB %08x/%08x %08x/%08x, CORBWP %u\n", (unsigned)H.corb[0], (unsigned)H.corb[1], (unsigned)H.corb[2], (unsigned)H.corb[3], (unsigned)(uint32_t)H.rirb[0], (unsigned)(H.rirb[0] >> 32), (unsigned)(uint32_t)H.rirb[1], (unsigned)(H.rirb[1] >> 32), rd16(CORBWP));
            return false;
        }
        watchdog_pet();
    }
}

static uint32_t get_param(int nid, int p) { uint32_t r = 0; verb(nid, 0xF0000 | (uint32_t)p, &r); return r; }
static uint32_t get12(int nid, uint32_t id, uint32_t payload) { uint32_t r = 0; verb(nid, (id << 8) | payload, &r); return r; }
static void set12(int nid, uint32_t id, uint32_t payload) { verb(nid, (id << 8) | (payload & 0xFF), NULL); }
static void set_amp(int nid, uint16_t payload) { verb(nid, 0x30000 | payload, NULL); }
static bool hdmi_read(void *ctx,unsigned nid,unsigned id,unsigned payload,uint32_t *response){
    (void)ctx;return verb((int)nid,(id<<8)|(payload&255),response);
}
static bool hdmi_write(void *ctx,unsigned nid,unsigned id,unsigned payload){
    (void)ctx;return verb((int)nid,(id<<8)|(payload&255),NULL);
}
static const hdmi_codec_io hdmi_io={NULL,hdmi_read,hdmi_write};

/* ------------------------------------------------------------ controller */

static bool stop_engines(void) {
    wr8(CORBCTL, 0); wr8(RIRBCTL, 0);
    bool stopped = wait8(CORBCTL, 2, 0, 100) && wait8(RIRBCTL, 2, 0, 100);
    uint16_t caps = rd16(GCAP);
    unsigned streams = ((caps >> 12) & 15) + ((caps >> 8) & 15) + ((caps >> 3) & 31);
    for (unsigned i = 0; i < streams; i++) {
        uint32_t sd = 0x80 + i * 0x20;
        wr8(sd, rd8(sd) & ~2u);
        if (!wait8(sd, 2, 0, 100)) stopped = false;
    }
    return stopped;
}

static void retire_controller(void) {
    if (!H.mmio) return;
    bool stopped = stop_engines();
    nic_pci_master(H.pci, false);
    if (stopped) {
        wr32(GCTL, rd32(GCTL) & ~1u);
        stopped = wait16(GCTL, 1, 0, 100);
    }
    /* Keep pages quarantined if hardware did not confirm reset. */
    if (!stopped) return;
    if (H.corb_phys) nic_dma_free(H.corb_phys, 1);
    if (H.rirb_phys) nic_dma_free(H.rirb_phys, 1);
    if (H.bdl_phys) nic_dma_free(H.bdl_phys, 1);
    if (H.buf_phys) nic_dma_free(H.buf_phys, MAX_FRAGMENTS * FRAGMENT / PAGE_SIZE);
    H.corb_phys = H.rirb_phys = H.bdl_phys = H.buf_phys = 0;
}

static bool ctrl_reset(void) {
    if (!stop_engines()) return false;
    wr32(GCTL, rd32(GCTL) & ~1u);
    if (!wait16(GCTL, 1, 0, 100)) return false;
    pit_sleep_ms(2);
    wr32(GCTL, rd32(GCTL) | 1u);
    uint64_t t0 = pit_get_ticks();
    unsigned spins = 0;
    while (!(rd32(GCTL) & 1)) { if (++spins >= 30000000 || pit_elapsed(pit_get_ticks(), t0) > 1000) return false; watchdog_pet(); }
    return true;
}

static bool rings_init(void) {
    wr8(CORBCTL, 0);
    wr8(RIRBCTL, 0);
    if (!wait8(CORBCTL, 2, 0, 100) || !wait8(RIRBCTL, 2, 0, 100)) return false;
    uint64_t c = nic_dma_alloc(1), r = nic_dma_alloc(1);
    if (!c || !r) { if (c) nic_dma_free(c, 1); if (r) nic_dma_free(r, 1); return false; }
    H.corb_phys = c; H.rirb_phys = r;
    H.corb = (uint32_t *)(uintptr_t)c;
    H.rirb = (uint64_t *)(uintptr_t)r;
    uint8_t cs = rd8(CORBSIZE), rs = rd8(RIRBSIZE);
    H.corb_entries = (cs & 0x40) ? 256 : (cs & 0x20) ? 16 : 2;
    H.rirb_entries = (rs & 0x40) ? 256 : (rs & 0x20) ? 16 : 2;
    wr8(CORBSIZE, (cs & 0xFC) | (H.corb_entries == 256 ? 2 : H.corb_entries == 16 ? 1 : 0));
    wr8(RIRBSIZE, (rs & 0xFC) | (H.rirb_entries == 256 ? 2 : H.rirb_entries == 16 ? 1 : 0));
    wr32(CORBLBASE, (uint32_t)c); wr32(CORBUBASE, (uint32_t)(c >> 32));
    wr32(RIRBLBASE, (uint32_t)r); wr32(RIRBUBASE, (uint32_t)(r >> 32));
    wr16(CORBRP, 0x8000);
    if (!wait16(CORBRP, 0x8000, 0x8000, 100)) return false;
    wr16(CORBRP, 0);
    if (!wait16(CORBRP, 0x8000, 0, 100)) return false;
    wr16(CORBWP, 0);
    wr16(RIRBWP, 0x8000);
    H.rirb_last = 0;
    wr16(RINTCNT, 1);
    wr8(CORBCTL, 0x02);
    wr8(RIRBCTL, 0x03);   /* DMA on, response interrupt flag on (polled: the controller only counts responses this way) */
    return true;
}

/* ------------------------------------------------------------ widgets and paths */

static bool node_ok(int nid) { return nid >= H.first_node && nid < H.first_node + H.node_count && nid - H.first_node < MAX_NODES; }
static widget_t *W(int nid) { return &H.w[nid - H.first_node]; }

static void read_conn_list(int nid, widget_t *w) {
    w->nconn = 0;
    uint32_t len = get_param(nid, 0x0E);
    int n = (int)(len & 0x7F);
    bool lng = (len >> 7) & 1;
    int per = lng ? 2 : 4;
    int prev = -1;
    for (int i = 0; i < n && w->nconn < MAX_CONN; i += per) {
        uint32_t r = get12(nid, 0xF02, (uint32_t)i);
        for (int k = 0; k < per && i + k < n; k++) {
            uint32_t e = lng ? ((r >> (16 * k)) & 0xFFFF) : ((r >> (8 * k)) & 0xFF);
            bool range = lng ? (e & 0x8000) : (e & 0x80);
            int id = (int)(lng ? (e & 0x7FFF) : (e & 0x7F));
            if (range && prev >= 0) {   /* the entries prev+1 .. id */
                for (int j = prev + 1; j <= id && w->nconn < MAX_CONN; j++) w->conn[w->nconn++] = (uint8_t)j;
            } else if (w->nconn < MAX_CONN) w->conn[w->nconn++] = (uint8_t)id;
            prev = id;
        }
    }
}

static void scan_widgets(void) {
    for (int i = 0; i < H.node_count && i < MAX_NODES; i++) {
        int nid = H.first_node + i;
        widget_t *w = &H.w[i];
        memset(w, 0, sizeof(*w));
        w->caps = get_param(nid, 0x09);
        w->type = (uint8_t)((w->caps >> 20) & 0xF);
        if (w->caps & (1u << 8)) read_conn_list(nid, w);
        if (w->type == 4) { w->cfg = get12(nid, 0xF1C, 0); w->pincaps = get_param(nid, 0x0C); }
        bool own = (w->caps >> 3) & 1;
        if (w->caps & (1u << 2)) w->amp_out = own ? get_param(nid, 0x12) : get_param(H.afg, 0x12);
        if (w->caps & (1u << 1)) w->amp_in = own ? get_param(nid, 0x0D) : get_param(H.afg, 0x0D);
    }
}

/* depth first search from a widget to an analog output converter; fills hops from the pin towards the converter */
static bool find_dac(int nid, int depth, hop_t *hops, int *nh) {
    if (depth > 6 || !node_ok(nid)) return false;
    widget_t *w = W(nid);
    if (w->visited) return false;
    w->visited = true;
    if (w->type == 0) {   /* output converter */
        if (((w->caps & (1u << 9)) != 0) != H.digital) { w->visited = false; return false; }
        hops[(*nh)++] = (hop_t){(uint8_t)nid, 0, 0};
        w->visited = false;
        return true;
    }
    if (w->type == 2 || w->type == 3 || w->type == 4) {
        for (int i = 0; i < w->nconn; i++) {
            int save = *nh;
            hops[(*nh)++] = (hop_t){(uint8_t)nid, (uint8_t)i, (uint8_t)i};
            if (find_dac(w->conn[i], depth + 1, hops, nh)) { w->visited = false; return true; }
            *nh = save;
        }
    }
    w->visited = false;
    return false;
}

static void amp_unmute(int nid, widget_t *w, int in_index) {
    uint32_t oc = w->amp_out, ic = w->amp_in;
    if (w->caps & (1u << 2)) {
        int gain = (int)(oc & 0x7F);   /* 0 dB */
        set_amp(nid, (uint16_t)(0xB000 | gain));
    }
    if ((w->caps & (1u << 1)) && in_index >= 0) {
        int gain = (int)(ic & 0x7F);
        set_amp(nid, (uint16_t)(0x7000 | (in_index << 8) | gain));
    }
}

static int g_ndac_paths;

/* switches one output pin on: power, selectors, amplifiers, pin control, EAPD */
static bool setup_pin(int pin) {
    hop_t hops[8];
    int nh = 0;
    if (!find_dac(pin, 0, hops, &nh) || nh < 2) return false;
    int dac = hops[nh - 1].nid;
    if (H.dac && H.dac != dac) {
        /* a second converter: the stream id can only be set on one, so let the path use the first one when possible */
    }
    for (int i = 0; i < nh; i++) {
        int nid = hops[i].nid;
        widget_t *w = W(nid);
        if (w->caps & (1u << 10)) set12(nid, 0x705, 0);   /* D0 */
        if (i + 1 < nh && w->nconn > 1) set12(nid, 0x701, hops[i].sel);
        amp_unmute(nid, w, i + 1 < nh ? hops[i].in_index : -1);
    }
    widget_t *pw = W(pin);
    uint32_t ctl = 0x40;   /* output enable */
    if (((pw->cfg >> 20) & 0xF) == 2 || (pw->pincaps & (1u << 3))) ctl |= 0x80;   /* headphone amplifier */
    set12(pin, 0x707, ctl);
    if (pw->pincaps & (1u << 16)) set12(pin, 0x70C, 0x02);   /* EAPD on */
    set12(dac, 0x706, (uint32_t)(STREAM_TAG << 4));
    verb(dac, 0x20000 | H.fmt, NULL);
    if (!H.dac) {
        H.dac = dac;
        /* the amplifier that carries the volume: the converter's own, else the first on the path */
        H.vol_nid = 0;
        for (int i = nh - 1; i >= 0; i--) {
            widget_t *w = W(hops[i].nid);
            if ((w->caps & (1u << 2)) && ((w->amp_out >> 8) & 0x7F)) {
                H.vol_nid = hops[i].nid;
                H.vol_steps = (int)((w->amp_out >> 8) & 0x7F);
                H.vol_offset = (int)(w->amp_out & 0x7F);
                break;
            }
        }
    }
    g_ndac_paths++;
    return true;
}

/* HDMI stereo LPCM path. Display firmware/driver supplies standard ELD or
 * AMD's native audio descriptors; require a connected compatible sink. */
static bool setup_hdmi(int pin) {
    if(!hdmi_codec_stereo_sink(&hdmi_io,(unsigned)pin,H.codec_id))return false;
    hop_t hops[8]; int nh=0;
    if(!find_dac(pin,0,hops,&nh) || nh<2) return false;
    int dac=hops[nh-1].nid;
    widget_t *dw=W(dac);
    uint32_t pcm=get_param((dw->caps&0x10) ? dac : H.afg,0x0a);
    uint32_t fmt=get_param((dw->caps&0x10) ? dac : H.afg,0x0b);
    /* AMD's converter capability words omit supported PCM rates; its
     * validated sink descriptor above is authoritative for this stereo mode. */
    if(!hdmi_codec_is_amd(H.codec_id) && (!(pcm&(1u<<6)) || !(pcm&(1u<<17)) || !(fmt&1))) return false;
    if(!setup_pin(pin)) return false;
    if(!hdmi_codec_program_stereo(&hdmi_io,(unsigned)pin,(unsigned)dac,H.codec_id,H.codec_revision))return false;
    H.digital_pin=pin;
    snprintf(H.name,sizeof(H.name),"%s HDMI PCM monitor",hdmi_codec_is_amd(H.codec_id)?"AMD":"HDA");
    return H.verb_fail==0;
}

static bool codec_setup(void) {
    uint32_t vid = get_param(0, 0x00);
    if (!vid || vid == 0xFFFFFFFF) { kprintf("[HDA] Codec %d antwortet nicht (Kennung %08x)\n", H.cad, (unsigned)vid); return false; }
    H.codec_id=vid;H.codec_revision=get_param(0,0x02);
    uint32_t sub = get_param(0, 0x04);
    int start = (int)((sub >> 16) & 0xFF), count = (int)(sub & 0xFF);
    H.afg = 0;
    for (int i = 0; i < count; i++) {
        int nid = start + i;
        if ((get_param(nid, 0x05) & 0xFF) == 1) { H.afg = nid; break; }
    }
    if (!H.afg) { kprintf("[HDA] Codec %d: keine Audio-Funktionsgruppe (Kennung %08x, Knoten %08x, RIRB-Zeiger %u)\n", H.cad, (unsigned)vid, (unsigned)sub, (unsigned)rd16(RIRBWP)); return false; }
    set12(H.afg, 0x705, 0);   /* D0 */
    pit_sleep_ms(10);
    uint32_t wsub = get_param(H.afg, 0x04);
    H.first_node = (int)((wsub >> 16) & 0xFF);
    H.node_count = (int)(wsub & 0xFF);
    if (H.node_count <= 0 || H.node_count > MAX_NODES || H.first_node + H.node_count > 256) return false;
    scan_widgets();
    snprintf(H.name, sizeof(H.name), "HD Audio %04x:%04x", (unsigned)(vid >> 16), (unsigned)(vid & 0xFFFF));
    if (cmdline_has("hdadebug")) {
        kprintf("[HDA] Codec %d: Hersteller %08x, AFG %d, Knoten %d..%d\n", H.cad, (unsigned)vid, H.afg, H.first_node, H.first_node + H.node_count - 1);
        for (int i = 0; i < H.node_count; i++) {
            widget_t *w = &H.w[i];
            kprintf("[HDA]   Knoten %d: Typ %d caps %08x conn %d cfg %08x pincaps %08x amp_out %08x\n", H.first_node + i, w->type, (unsigned)w->caps, w->nconn, (unsigned)w->cfg, (unsigned)w->pincaps, (unsigned)w->amp_out);
        }
    }
    H.fmt = 0x0011;   /* 48 kHz, 16 bit, 2 channels */
    H.dac = 0;
    g_ndac_paths = 0;
    if(H.digital) {
        for(int i=0;i<H.node_count;i++) {
            widget_t *w=&H.w[i];
            if(w->type==4 && (w->pincaps&(1u<<7)) && (w->pincaps&(1u<<4)) && setup_hdmi(H.first_node+i)) return true;
        }
        return false;
    }
    /* every analog output pin that is connected to something */
    for (int i = 0; i < H.node_count; i++) {
        widget_t *w = &H.w[i];
        if (w->type != 4) continue;
        uint32_t cfg = w->cfg;
        int conn = (int)(cfg >> 30), dev = (int)((cfg >> 20) & 0xF);
        if (conn == 1) continue;                   /* no physical connection */
        if (dev != 0 && dev != 1 && dev != 2) continue;   /* line out, speaker, headphone out */
        if (!(w->pincaps & (1u << 4))) continue;   /* cannot output */
        setup_pin(H.first_node + i);
    }
    return g_ndac_paths > 0 && H.verb_fail == 0;
}

/* ------------------------------------------------------------ stream */

static uint32_t sd_base(void) { return 0x80 + 0x20u * (uint32_t)H.iss; }

static bool stream_alloc(void) {
    H.bdl_phys = nic_dma_alloc(1);
    H.buf_phys = nic_dma_alloc(MAX_FRAGMENTS * FRAGMENT / PAGE_SIZE);
    return H.bdl_phys && H.buf_phys;
}

/* plays one block (at most the size of the buffer) and waits until it has been played */
static bool play_block(const uint8_t *data, size_t bytes) {
    uint32_t sd = sd_base();
    size_t padded = (bytes + 127) & ~(size_t)127;
    if (padded > (size_t)MAX_FRAGMENTS * FRAGMENT) return false;
    wr8(sd, rd8(sd) & ~2u);
    if (!wait8(sd, 2, 0, 100)) { H.ready = false; return false; }
    memcpy((void *)(uintptr_t)H.buf_phys, data, bytes);
    memset((uint8_t *)(uintptr_t)H.buf_phys + bytes, 0, padded - bytes);
    /* descriptor list: fragments of 4 KiB, the last one shorter, interrupt-on-completion flag on the last */
    uint32_t *bdl = (uint32_t *)(uintptr_t)H.bdl_phys;
    int n = 0;
    for (size_t off = 0; off < padded; off += FRAGMENT, n++) {
        size_t len = padded - off < FRAGMENT ? padded - off : FRAGMENT;
        uint64_t a = H.buf_phys + off;
        bdl[n * 4 + 0] = (uint32_t)a;
        bdl[n * 4 + 1] = (uint32_t)(a >> 32);
        bdl[n * 4 + 2] = (uint32_t)len;
        bdl[n * 4 + 3] = (off + FRAGMENT >= padded) ? 1 : 0;
    }
    mb();
    /* stream reset, programming, run */
    wr8(sd + 0, rd8(sd + 0) & ~0x02);
    wr8(sd + 0, 0x01);
    if (!wait8(sd + 0, 0x01, 0x01, 100)) { H.ready = false; return false; }
    wr8(sd + 0, 0x00);
    if (!wait8(sd + 0, 0x01, 0x00, 100)) { H.ready = false; return false; }
    wr8(sd + 2, (uint8_t)(STREAM_TAG << 4));
    wr8(sd + 3, 0x1C);                               /* clear status bits */
    wr32(sd + 0x08, (uint32_t)padded);               /* cyclic buffer length */
    wr16(sd + 0x0C, (uint16_t)(n - 1));              /* last valid index */
    wr16(sd + 0x12, H.fmt);
    wr32(sd + 0x18, (uint32_t)H.bdl_phys);
    wr32(sd + 0x1C, (uint32_t)(H.bdl_phys >> 32));
    mb();
    wr8(sd + 0, 0x02 | 0x04);                        /* run, interrupt on completion (only the status bit is used) */
    uint64_t ms = (uint64_t)padded * 1000 / (48000 * 4) + 400;
    uint64_t t0 = pit_get_ticks();
    bool done = false;
    while (pit_elapsed(pit_get_ticks(), t0) < ms) {
        uint8_t status = rd8(sd + 3);
        if (status & 0x18) break; /* FIFO/descriptor error */
        if (status & 0x04) { done = true; break; }   /* buffer completion */
        coop_yield_check();
        pit_sleep_ms(1);
        watchdog_pet();
    }
    if (done) pit_sleep_ms(5);   /* the last samples are still in the FIFO */
    wr8(sd + 0, 0x00);
    if (!wait8(sd, 2, 0, 100)) done = false;
    wr8(sd + 3, 0x1C);
    if (!done) H.ready = false;
    return done;
}

bool hda_init(void) {
    if (H.ready) return true;
    retire_controller();
    memset(&H, 0, sizeof(H));
    H.volume = 80;
    /* Prefer a verified HDMI sink, then fall back to the analog controller. */
    for(unsigned pass=0;pass<2;pass++) {
    for (pci_device_t *dev = pci_get_device_list(); dev; dev = dev->next) {
        if (dev->class_id != 0x04 || dev->subclass_id != 0x03) continue;
        if ((dev->bar0 & 1) || dev->bar0 == UINT32_MAX) continue;
        retire_controller();
        memset(&H, 0, sizeof(H)); H.volume = 80; H.digital=(pass==0);
        H.pci = dev;
        H.found = true;
        uint64_t phys = (uint64_t)(dev->bar0 & ~0xFu);
        if ((dev->bar0 & 6) == 4) phys |= (uint64_t)dev->bar1 << 32;
        if (!phys || !vmm_map_mmio(phys, 0x4000)) continue;
        nic_pci_prepare(dev, false); /* D0, polling, DMA off until all buffers exist. */
        if (dev->vendor_id == 0x8086) {   /* route the audio traffic over the default traffic class */
            uint32_t v = pci_read_dword(dev->bus, dev->slot, dev->func, 0x44);
            pci_write_dword(dev->bus, dev->slot, dev->func, 0x44, v & ~0x7u);
        }
        H.mmio = (uintptr_t)phys;
        uint16_t gcap = rd16(GCAP);
        if (gcap == UINT16_MAX) { H.mmio = 0; continue; }
        H.oss = (gcap >> 12) & 0xF;
        H.iss = (gcap >> 8) & 0xF;
        if (!H.oss) continue;
        kprintf("[HDA] Controller %04x:%04x, %d Ausgabe-, %d Eingabestroeme\n", dev->vendor_id, dev->device_id, H.oss, H.iss);
        if (!ctrl_reset()) { kprintf("[HDA] Reset fehlgeschlagen\n"); continue; }
        /* codecs announce themselves after the reset */
        uint64_t t0 = pit_get_ticks();
        uint16_t st = 0;
        for (unsigned spins = 0; spins < 30000000 && pit_elapsed(pit_get_ticks(), t0) < 300; spins++) { st = rd16(STATESTS); if (st) { pit_sleep_ms(20); st = rd16(STATESTS); break; } watchdog_pet(); }
        if (!st) { kprintf("[HDA] Kein Codec\n"); continue; }
        if (!rings_init() || !stream_alloc()) { kprintf("[HDA] Kein Speicher\n"); continue; }
        nic_pci_master(dev, true);
        for (int c = 0; c < 15 && !H.ready; c++) {
            if (!(st & (1u << c))) continue;
            H.cad = c;
            H.verb_fail = 0;
            H.rirb_last = (uint16_t)(rd16(RIRBWP) & 0xFF);
            if (codec_setup()) { H.ready = true; break; }
        }
        if (H.ready) {
            hda_set_volume(H.volume);
            kprintf("[HDA] %s, %d Ausgabepfad(e), Wandler %d\n", H.name, g_ndac_paths, H.dac);
            return true;
        }
        kprintf("[HDA] No compatible %s output found\n",H.digital?"HDMI":"analog");
    }
    }
    retire_controller(); H.mmio = 0;
    return false;
}

void hda_set_volume(uint8_t vol_percent) {
    if (vol_percent > 100) vol_percent = 100;
    H.volume = vol_percent;
    if (!H.ready || !H.vol_nid) return;
    if (vol_percent == 0) { set_amp(H.vol_nid, (uint16_t)(0xB000 | 0x80)); return; }   /* mute */
    int ceiling = H.vol_offset < H.vol_steps ? H.vol_offset : H.vol_steps;
    int gain = ceiling * vol_percent / 100;
    set_amp(H.vol_nid, (uint16_t)(0xB000 | gain));
}

bool hda_play_pcm(const int16_t *samples, size_t count) {
    if (H.ready && H.digital && !hdmi_codec_stereo_sink(&hdmi_io,(unsigned)H.digital_pin,H.codec_id)) return false;
    if (!H.ready || !samples || count < 2 || (count & 1) || count > SIZE_MAX / sizeof(int16_t)) return false;
    const uint8_t *p = (const uint8_t *)samples;
    size_t bytes = count * sizeof(int16_t);
    bytes &= ~(size_t)3;   /* whole stereo frames */
    bool ok = true;
    while (bytes) {
        size_t n = bytes > (size_t)MAX_FRAGMENTS * FRAGMENT ? (size_t)MAX_FRAGMENTS * FRAGMENT : bytes;
        if (!play_block(p, n)) return false;
        p += n;
        bytes -= n;
    }
    return ok;
}

const char *hda_get_name(void) { return H.ready ? H.name : "Intel High Definition Audio (HDA)"; }

bool hda_output_ready(void) { return H.ready; }
