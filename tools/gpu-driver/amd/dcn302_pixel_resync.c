/* Native DCE112 pixel-resync programming adapted from the pinned AMD Linux
 * v6.12 dce_clock_source.c path used by DCN302.  The generated header retains
 * the upstream register notice.  This is a disabled-pipeline transaction;
 * it deliberately cannot enable a PHY, PLL, VTG, or scanout. */
#include "dcn302_pixel_resync.h"
#include <string.h>

static bool io_valid(const dcn302_io *io) { return io && io->read && io->write; }
static bool separate(const void *pointer, size_t bytes, const dcn302_pixel_resync_transaction *transaction) {
    uintptr_t a = (uintptr_t)pointer, b = (uintptr_t)transaction;
    return pointer && a <= UINTPTR_MAX - bytes && b <= UINTPTR_MAX - sizeof(*transaction) &&
        (a < b ? b - a >= bytes : a - b >= sizeof(*transaction));
}
static bool rd(const dcn302_io *io, unsigned phy, uint32_t *value) {
    return io->read(io->context, dcn302_pixel_resync_bytes[phy], value);
}
static bool stopped(const dcn302_io *io, const dcn302_pixel_resync_transaction *transaction) {
    for (unsigned sweep = 0; sweep < 2; ++sweep) {
        if (!transaction->guard(io->context)) return false;
        for (unsigned pipe = 0; pipe < 5; ++pipe) {
            uint32_t control, clock, vtg;
            if (!io->read(io->context, dcn302_register_bytes[pipe][DCN302_R_CONTROL], &control) ||
                !io->read(io->context, dcn302_register_bytes[pipe][DCN302_R_CLOCK], &clock) ||
                !io->read(io->context, dcn302_register_bytes[pipe][DCN302_R_VTG], &vtg) ||
                (control & (DCN302_MASTER_ENABLE_MASK | DCN302_MASTER_ACTIVE_MASK)) ||
                (clock & DCN302_BUSY_MASK) || (vtg & DCN302_VTG_ENABLE_MASK)) return false;
        }
    }
    return true;
}
static enum dcn302_error desired(enum dcn302_hdmi_color_depth depth, bool ycbcr420, uint32_t before, uint32_t *after) {
    uint32_t deep;
    switch (depth) {
        case DCN302_HDMI_RGB8: deep = 0; break;
        case DCN302_HDMI_RGB10: deep = 1; break;
        case DCN302_HDMI_RGB12: deep = 2; break;
        case DCN302_HDMI_RGB16: deep = 3; break;
        default: return DCN302_INPUT;
    }
    /* DCN302's CS_COMMON_MASK_SH_LIST_DCN2_0 owns deep-color only. The raw
     * DOUBLE_RATE bit is not a safe generic YCbCr420 substitute. */
    if (ycbcr420) return DCN302_UNSUPPORTED;
    *after = (before & ~DCN302_PIXEL_RESYNC_OWNED_MASK) | (deep << 4);
    return DCN302_OK;
}
static bool usable(const dcn302_io *io, const dcn302_pixel_resync_transaction *transaction) {
    if (!transaction || (uintptr_t)transaction % _Alignof(dcn302_pixel_resync_transaction) ||
        (uintptr_t)io % _Alignof(dcn302_io) || !separate(io, sizeof(*io), transaction) ||
        !io_valid(io) || !transaction->prepared || !transaction->guard || transaction->phy >= 5 ||
        io->context != transaction->owner.context || io->read != transaction->owner.read ||
        io->write != transaction->owner.write || io->delay_us != transaction->owner.delay_us) return false;
    return ((transaction->before ^ transaction->after) & ~DCN302_PIXEL_RESYNC_OWNED_MASK) == 0;
}
static enum dcn302_error verify(const dcn302_io *io, const dcn302_pixel_resync_transaction *transaction, uint32_t expected) {
    uint32_t current;
    if (!stopped(io, transaction)) return DCN302_BUSY;
    if (!rd(io, transaction->phy, &current)) return DCN302_IO;
    if (current != expected) return DCN302_READBACK;
    return stopped(io, transaction) ? DCN302_OK : DCN302_BUSY;
}
static enum dcn302_error program(const dcn302_io *io, dcn302_pixel_resync_transaction *transaction, uint32_t expected) {
    uint32_t current;
    if (!stopped(io, transaction)) return DCN302_BUSY;
    if (!rd(io, transaction->phy, &current)) return DCN302_IO;
    if ((current ^ transaction->before) & ~DCN302_PIXEL_RESYNC_OWNED_MASK) return DCN302_READBACK;
    if ((current & DCN302_PIXEL_RESYNC_OWNED_MASK) != (expected & DCN302_PIXEL_RESYNC_OWNED_MASK)) {
        transaction->dirty = transaction->touched = true;
        if (!io->write(io->context, dcn302_pixel_resync_bytes[transaction->phy],
                (current & ~DCN302_PIXEL_RESYNC_OWNED_MASK) | (expected & DCN302_PIXEL_RESYNC_OWNED_MASK))) return DCN302_IO;
    }
    return verify(io, transaction, expected);
}
enum dcn302_error dcn302_pixel_resync_prepare(const dcn302_io *io, unsigned phy,
    enum dcn302_hdmi_color_depth depth, bool ycbcr420, bool (*guard)(void *),
    dcn302_pixel_resync_transaction *transaction) {
    if (!transaction || (uintptr_t)transaction % _Alignof(dcn302_pixel_resync_transaction) ||
        (uintptr_t)io % _Alignof(dcn302_io) || !separate(io, sizeof(*io), transaction)) return DCN302_INPUT;
    memset(transaction, 0, sizeof(*transaction));
    if (!io_valid(io) || phy >= 5 || !guard) return transaction->error = DCN302_INPUT;
    uint32_t second;
    if (!rd(io, phy, &transaction->before) || !rd(io, phy, &second)) return transaction->error = DCN302_IO;
    if (second != transaction->before) return transaction->error = DCN302_READBACK;
    enum dcn302_error error = desired(depth, ycbcr420, transaction->before, &transaction->after);
    if (error) return transaction->error = error;
    transaction->owner = *io;
    transaction->guard = guard;
    transaction->phy = phy;
    transaction->prepared = true;
    return transaction->error = DCN302_OK;
}
enum dcn302_error dcn302_pixel_resync_verify_installed(const dcn302_io *io,
    const dcn302_pixel_resync_transaction *transaction) {
    if (!usable(io, transaction)) return DCN302_INPUT;
    if (!transaction->applied || transaction->poisoned) return DCN302_BUSY;
    return verify(io, transaction, transaction->after);
}
enum dcn302_error dcn302_pixel_resync_restore_disabled(const dcn302_io *io,
    dcn302_pixel_resync_transaction *transaction) {
    if (!usable(io, transaction)) return DCN302_INPUT;
    enum dcn302_error error = verify(io, transaction, transaction->applied ? transaction->after : transaction->before);
    if (!error && transaction->touched) error = program(io, transaction, transaction->before);
    if (!error) error = verify(io, transaction, transaction->before);
    if (error) {
        transaction->poisoned = true;
        return transaction->error = DCN302_ROLLBACK;
    }
    transaction->dirty = transaction->applied = transaction->poisoned = transaction->touched = false;
    return transaction->error = DCN302_OK;
}
enum dcn302_error dcn302_pixel_resync_apply_disabled(const dcn302_io *io,
    dcn302_pixel_resync_transaction *transaction) {
    if (!usable(io, transaction)) return DCN302_INPUT;
    if (transaction->dirty || transaction->applied || transaction->poisoned || transaction->touched)
        return transaction->error = DCN302_BUSY;
    enum dcn302_error error = verify(io, transaction, transaction->before);
    if (!error) error = program(io, transaction, transaction->after);
    if (!error) error = verify(io, transaction, transaction->after);
    if (error) {
        if (transaction->dirty && dcn302_pixel_resync_restore_disabled(io, transaction)) return DCN302_ROLLBACK;
        return transaction->error = error;
    }
    transaction->applied = true;
    return transaction->error = DCN302_OK;
}
