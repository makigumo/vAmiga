// -----------------------------------------------------------------------------
// This file is part of vAmiga
//
// Copyright (C) Dirk W. Hoffmann. www.dirkwhoffmann.de
// Licensed under the GNU General Public License v3
//
// See https://www.gnu.org for license information
// -----------------------------------------------------------------------------

#include "Amiga.h"

/* A central element in the emulation of an Amiga is the accurate modeling of
 * the DMA timeslot allocation table (Fig. 6-9 im the HRM, 3rd revision). All
 * bitplane related events are managed in the BPL_SLOT. All disk, audio, and
 * sprite related events are managed in the DAS_SLOT.
 *
 * vAmiga utilizes two event tables to schedule events in the DAS_SLOT and
 * BPL_SLOT. Assuming that sprite DMA is enabled and Denise draws 6 bitplanes
 * in lores mode starting at 0x28, the tables would look like this:
 *
 *     bplEvent[0x00] = EVENT_NONE   dasEvent[0x00] = EVENT_NONE
 *     bplEvent[0x01] = EVENT_NONE   dasEvent[0x01] = BUS_REFRESH
 *         ...                           ...
 *     bplEvent[0x28] = EVENT_NONE   dasEvent[0x28] = EVENT_NONE
 *     bplEvent[0x29] = BPL_L4       dasEvent[0x29] = DAS_S5_1
 *     bplEvent[0x2A] = BPL_L6       dasEvent[0x2A] = EVENT_NONE
 *     bplEvent[0x2B] = BPL_L2       dasEvent[0x2B] = DAS_S5_2
 *     bplEvent[0x2C] = EVENT_NONE   dasEvent[0x2C] = EVENT_NONE
 *     bplEvent[0x2D] = BPL_L3       dasEvent[0x2D] = DAS_S6_1
 *     bplEvent[0x2E] = BPL_L5       dasEvent[0x2E] = EVENT_NONE
 *     bplEvent[0x2F] = BPL_L1       dasEvent[0x2F] = DAS_S6_2
 *         ...                           ...
 *     bplEvent[0xE2] = BPL_EOL      dasEvent[0xE2] = BUS_REFRESH
 *
 * The BPL_EOL event doesn't perform DMA. It concludes the current line.
 *
 * All events in the BPL_SLOT can be superimposed by two drawing flags (bit 0
 * and bit 1) that trigger the transfer of the data registers into the shift
 * registers at the correct DMA cycle. Bit 0 controls the odd bitplanes and
 * bit 1 controls the even bitplanes. Settings these flags changes the
 * scheduled event, e.g.:
 *
 *     BPL_L4  becomes  BPL_L4_ODD   if bit 0 is set
 *     BPL_L4  becomes  BPL_L4_EVEN  if bit 1 is set
 *     BPL_L4  becomes  BPL_L4_ODD_EVEN  if both bits are set
 *
 * Each event table is accompanied by a jump table that points to the next
 * event. Given the example tables above, the jump tables would look like this:
 *
 *     nextBplEvent[0x00] = 0x29     nextDasEvent[0x00] = 0x01
 *     nextBplEvent[0x01] = 0x29     nextDasEvent[0x01] = 0x03
 *           ...                           ...
 *     nextBplEvent[0x28] = 0x29     nextDasEvent[0x28] = 0x29
 *     nextBplEvent[0x29] = 0x2A     nextDasEvent[0x29] = 0x2B
 *     nextBplEvent[0x2A] = 0x2B     nextDasEvent[0x2A] = 0x2B
 *     nextBplEvent[0x2B] = 0x2D     nextDasEvent[0x2B] = 0x2D
 *     nextBplEvent[0x2C] = 0x2D     nextDasEvent[0x2C] = 0x2D
 *     nextBplEvent[0x2D] = 0x2E     nextDasEvent[0x2D] = 0x2F
 *     nextBplEvent[0x2E] = 0x2F     nextDasEvent[0x2E] = 0x2F
 *     nextBplEvent[0x2F] = 0x31     nextDasEvent[0x2F] = 0x31
 *           ...                           ...
 *     nextBplEvent[0xE2] = 0x00     nextDasEvent[0xE2] = 0x00
 *
 * Whenever one the DMA tables is modified, the corresponding jump table
 * has to be updated, too.
 *
 * To quickly setup the event tables, vAmiga utilizes two static lookup
 * tables. Depending on the current resoution, BPU value, or DMA status,
 * segments of these lookup tables are copied to the event tables.
 *
 *      Table: bitplaneDMA[Resolution][Bitplanes][Cycle]
 *
 *             (Bitplane DMA events in a single rasterline)
 *
 *             Resolution : 0 or 1        (0 = LORES / 1 = HIRES)
 *              Bitplanes : 0 .. 6        (Bitplanes in use, BPU)
 *                  Cycle : 0 .. HPOS_MAX (DMA cycle)
 *
 *      Table: dasDMA[dmacon]
 *
 *             (Disk, Audio, and Sprite DMA events in a single rasterline)
 *
 *                 dmacon : Bits 0 .. 5 of register DMACON
 */
Agnus::Agnus(Amiga& ref) : AmigaComponent(ref)
{
    setDescription("Agnus");
    
    subComponents = vector<HardwareComponent *> {
        
        &copper,
        &blitter,
        &dmaDebugger
    };

    config.revision = AGNUS_8372;

    initLookupTables();
}

void
Agnus::initLookupTables()
{
    initBplEventTableLores();
    initBplEventTableHires();
    initDasEventTable();
}

void
Agnus::initBplEventTableLores()
{
    memset(bplDMA[0], 0, sizeof(bplDMA[0]));

    for (int bpu = 0; bpu < 7; bpu++) {

        EventID *p = &bplDMA[0][bpu][0];

        // Iterate through all 22 fetch units
        for (int i = 0; i <= 0xD8; i += 8, p += 8) {

            switch(bpu) {
                case 6: p[2] = BPL_L6;
                case 5: p[6] = BPL_L5;
                case 4: p[1] = BPL_L4;
                case 3: p[5] = BPL_L3;
                case 2: p[3] = BPL_L2;
                case 1: p[7] = BPL_L1;
            }
        }

        assert(bplDMA[0][bpu][HPOS_MAX] == EVENT_NONE);
        bplDMA[0][bpu][HPOS_MAX] = BPL_EOL;
    }
}

void
Agnus::initBplEventTableHires()
{
    memset(bplDMA[1], 0, sizeof(bplDMA[1]));

    for (int bpu = 0; bpu < 7; bpu++) {

        EventID *p = &bplDMA[1][bpu][0];

        for (int i = 0; i <= 0xD8; i += 8, p += 8) {

            switch(bpu) {
                case 6:
                case 5:
                case 4: p[0] = p[4] = BPL_H4;
                case 3: p[2] = p[6] = BPL_H3;
                case 2: p[1] = p[5] = BPL_H2;
                case 1: p[3] = p[7] = BPL_H1;
            }
        }

        assert(bplDMA[1][bpu][HPOS_MAX] == EVENT_NONE);
        bplDMA[1][bpu][HPOS_MAX] = BPL_EOL;
    }
}

void
Agnus::initDasEventTable()
{
    memset(dasDMA, 0, sizeof(dasDMA));

    for (int dmacon = 0; dmacon < 64; dmacon++) {

        EventID *p = dasDMA[dmacon];

        p[0x01] = DAS_REFRESH;

        if (dmacon & DSKEN) {
            p[0x07] = DAS_D0;
            p[0x09] = DAS_D1;
            p[0x0B] = DAS_D2;
        }

        if (dmacon & AUD0EN) p[0x0D] = DAS_A0;
        if (dmacon & AUD1EN) p[0x0F] = DAS_A1;
        if (dmacon & AUD2EN) p[0x11] = DAS_A2;
        if (dmacon & AUD3EN) p[0x13] = DAS_A3;

        if (dmacon & SPREN) {
            p[0x15] = DAS_S0_1;
            p[0x17] = DAS_S0_2;
            p[0x19] = DAS_S1_1;
            p[0x1B] = DAS_S1_2;
            p[0x1D] = DAS_S2_1;
            p[0x1F] = DAS_S2_2;
            p[0x21] = DAS_S3_1;
            p[0x23] = DAS_S3_2;
            p[0x25] = DAS_S4_1;
            p[0x27] = DAS_S4_2;
            p[0x29] = DAS_S5_1;
            p[0x2B] = DAS_S5_2;
            p[0x2D] = DAS_S6_1;
            p[0x2F] = DAS_S6_2;
            p[0x31] = DAS_S7_1;
            p[0x33] = DAS_S7_2;
        }

        p[0xDF] = DAS_SDMA;
    }
}

void
Agnus::setRevision(AgnusRevision revision)
{
    debug("setRevision(%d)\n", revision);

    assert(isAgnusRevision(revision));
    config.revision = revision;
}

long
Agnus::chipRamLimit()
{
    switch (config.revision) {

        case AGNUS_8375: return 2048;
        case AGNUS_8372: return 1024;
        default:         return 512;
    }
}

u32
Agnus::chipRamMask()
{
    switch (config.revision) {

        case AGNUS_8375: return 0x1FFFFF;
        case AGNUS_8372: return 0x0FFFFF;
        default:         return 0x07FFFF;
    }
}

void
Agnus::_powerOn()
{
}

void Agnus::_reset()
{
    RESET_SNAPSHOT_ITEMS

    // Start with a long frame
    frame = Frame();

    // Initialize statistical counters
    clearStats();

    // Initialize event tables
    for (int i = pos.h; i < HPOS_CNT; i++) bplEvent[i] = bplDMA[0][0][i];
    for (int i = pos.h; i < HPOS_CNT; i++) dasEvent[i] = dasDMA[0][i];
    updateBplJumpTable();
    updateDasJumpTable();

    // Initialize the event slots
    for (unsigned i = 0; i < SLOT_COUNT; i++) {
        slot[i].triggerCycle = NEVER;
        slot[i].id = (EventID)0;
        slot[i].data = 0;
    }

    // Schedule initial events
    scheduleAbs<RAS_SLOT>(DMA_CYCLES(HPOS_CNT), RAS_HSYNC);
    scheduleAbs<CIAA_SLOT>(CIA_CYCLES(1), CIA_EXECUTE);
    scheduleAbs<CIAB_SLOT>(CIA_CYCLES(1), CIA_EXECUTE);
    scheduleAbs<SEC_SLOT>(NEVER, SEC_TRIGGER);
    scheduleAbs<KBD_SLOT>(DMA_CYCLES(1), KBD_SELFTEST);
    scheduleAbs<VBL_SLOT>(DMA_CYCLES(HPOS_CNT * vStrobeLine() + 1), VBL_STROBE);
    scheduleAbs<IRQ_SLOT>(NEVER, IRQ_CHECK);
    scheduleNextBplEvent();
    scheduleNextDasEvent();
}

void
Agnus::_inspect()
{
    // Prevent external access to variable 'info'
    pthread_mutex_lock(&lock);

    u32 mask      = chipRamMask();

    info.vpos     = pos.v;
    info.hpos     = pos.h;

    info.dmacon   = dmacon;
    info.bplcon0  = bplcon0;
    info.bpu      = bpu();
    info.ddfstrt  = ddfstrt;
    info.ddfstop  = ddfstop;
    info.diwstrt  = diwstrt;
    info.diwstop  = diwstop;

    info.bpl1mod  = bpl1mod;
    info.bpl2mod  = bpl2mod;
    info.bltamod  = blitter.bltamod;
    info.bltbmod  = blitter.bltbmod;
    info.bltcmod  = blitter.bltcmod;
    info.bltdmod  = blitter.bltdmod;
    info.bls      = bls;

    info.coppc    = copper.coppc & mask;
    info.dskpt    = dskpt & mask;
    info.bltpt[0] = blitter.bltapt & mask;
    info.bltpt[1] = blitter.bltbpt & mask;
    info.bltpt[2] = blitter.bltcpt & mask;
    info.bltpt[3] = blitter.bltdpt & mask;
    for (unsigned i = 0; i < 6; i++) info.bplpt[i] = bplpt[i] & mask;
    for (unsigned i = 0; i < 4; i++) info.audpt[i] = audpt[i] & mask;
    for (unsigned i = 0; i < 4; i++) info.audlc[i] = audlc[i] & mask;
    for (unsigned i = 0; i < 8; i++) info.sprpt[i] = sprpt[i] & mask;

    pthread_mutex_unlock(&lock);
}

void
Agnus::_dump()
{
    msg(" actions : %X\n", hsyncActions);

    msg("   dskpt : %X\n", dskpt);
    for (unsigned i = 0; i < 4; i++) msg("audpt[%d] : %X\n", i, audpt[i]);
    for (unsigned i = 0; i < 6; i++) msg("bplpt[%d] : %X\n", i, bplpt[i]);
    for (unsigned i = 0; i < 8; i++) msg("bplpt[%d] : %X\n", i, sprpt[i]);
    
    msg("   hstrt : %d\n", diwHstrt);
    msg("   hstop : %d\n", diwHstop);
    msg("   vstrt : %d\n", diwVstrt);
    msg("   vstop : %d\n", diwVstop);

    msg("\nEvents:\n\n");
    dumpEvents();

    msg("\nBPL DMA table:\n\n");
    dumpBplEventTable();

    msg("\nDAS DMA table:\n\n");
    dumpDasEventTable();
}

void
Agnus::clearStats()
{
    for (int i = 0; i < BUS_OWNER_COUNT; i++) {
        stats.bus.raw[i] = 0;
        stats.bus.accumulated[i] = 0.0;
    }
}

void
Agnus::updateStats()
{
    const double w = 0.5;
    
    for (int i = 0; i < BUS_OWNER_COUNT; i++) {
        stats.bus.accumulated[i] = w * stats.bus.accumulated[i] + (1 - w) * stats.bus.raw[i];
        stats.bus.raw[i] = 0;
    }
}

Cycle
Agnus::cyclesInFrame()
{
    return DMA_CYCLES(frame.numLines() * HPOS_CNT);
}

Cycle
Agnus::startOfFrame()
{
    return clock - DMA_CYCLES(pos.v * HPOS_CNT + pos.h);
}

Cycle
Agnus::startOfNextFrame()
{
    return startOfFrame() + cyclesInFrame();
}

bool
Agnus::belongsToPreviousFrame(Cycle cycle)
{
    return cycle < startOfFrame();
}

bool
Agnus::belongsToCurrentFrame(Cycle cycle)
{
    return !belongsToPreviousFrame(cycle) && !belongsToNextFrame(cycle);
}

bool
Agnus::belongsToNextFrame(Cycle cycle)
{
    return cycle >= startOfNextFrame();
}

bool
Agnus::inBplDmaLine(u16 dmacon, u16 bplcon0) {

    return
    ddfVFlop            // Outside VBLANK, inside DIW
    && bpu(bplcon0)     // At least one bitplane enabled
    && bpldma(dmacon);  // Bitplane DMA enabled
}

Cycle
Agnus::beamToCycle(Beam beam)
{
    return startOfFrame() + DMA_CYCLES(beam.v * HPOS_CNT + beam.h);
}

Beam
Agnus::cycleToBeam(Cycle cycle)
{
    Beam result;

    Cycle diff = AS_DMA_CYCLES(cycle - startOfFrame());
    assert(diff >= 0);

    result.v = diff / HPOS_CNT;
    result.h = diff % HPOS_CNT;
    return result;
}

Beam
Agnus::addToBeam(Beam beam, Cycle cycles)
{
    Beam result;

    Cycle cycle = beam.v * HPOS_CNT + beam.h + cycles;
    result.v = cycle / HPOS_CNT;
    result.h = cycle % HPOS_CNT;

    return result;
}

template <BusOwner owner> bool
Agnus::busIsFree()
{
    // Deny if the bus is already in use
    if (busOwner[pos.h] != BUS_NONE) return false;

    switch (owner) {

        case BUS_COPPER:
        {
            // Deny if Copper DMA is disabled
            if (!copdma()) return false;

            // Deny in cycle E0
            if (unlikely(pos.h == 0xE0)) return false;
            return true;
        }
        case BUS_BLITTER:
        {
            // Deny if Blitter DMA is disabled
            if (!bltdma()) return false;
            return true;
        }
    }

    assert(false);
    return false;
}

template <BusOwner owner> bool
Agnus::allocateBus()
{
    // Deny if the bus has been allocated already
    if (busOwner[pos.h] != BUS_NONE) return false;

    switch (owner) {

        case BUS_COPPER:
        {
            // Assign bus to the Copper
            busOwner[pos.h] = BUS_COPPER;
            return true;
        }
        case BUS_BLITTER:
        {
            // Deny if Blitter DMA is off
            if (!bltdma()) return false;

            // Deny if the CPU has precedence
            if (bls && !bltpri()) return false;

            // Assign the bus to the Blitter
            busOwner[pos.h] = BUS_BLITTER;
            return true;
        }
    }

    assert(false);
    return false;
}

u16
Agnus::doDiskDMA()
{
    u16 result = mem.peekChip16(dskpt);
    dskpt += 2;

    assert(pos.h < HPOS_CNT);
    busOwner[pos.h] = BUS_DISK;
    busValue[pos.h] = result;
    stats.bus.raw[BUS_DISK]++;

    return result;
}

template <int channel> u16
Agnus::doAudioDMA()
{
    u16 result = mem.peekChip16(audpt[channel]);
    audpt[channel] += 2;

    assert(pos.h < HPOS_CNT);
    busOwner[pos.h] = BUS_AUDIO;
    busValue[pos.h] = result;
    stats.bus.raw[BUS_AUDIO]++;

    return result;
}

template <int bitplane> u16
Agnus::doBitplaneDMA()
{
    u16 result = mem.peekChip16(bplpt[bitplane]);
    bplpt[bitplane] += 2;

    assert(pos.h < HPOS_CNT);
    busOwner[pos.h] = BUS_BITPLANE;
    busValue[pos.h] = result;
    stats.bus.raw[BUS_BITPLANE]++;

    return result;
}

template <int channel> u16
Agnus::doSpriteDMA()
{
    u16 result = mem.peekChip16(sprpt[channel]);
    sprpt[channel] += 2;

    assert(pos.h < HPOS_CNT);
    busOwner[pos.h] = BUS_SPRITE;
    busValue[pos.h] = result;
    stats.bus.raw[BUS_SPRITE]++;

    return result;
}

u16
Agnus::doCopperDMA(u32 addr)
{
    u16 result = mem.peek16<BUS_COPPER>(addr);

    assert(pos.h < HPOS_CNT);
    busOwner[pos.h] = BUS_COPPER;
    busValue[pos.h] = result;
    stats.bus.raw[BUS_COPPER]++;

    return result;
}

u16
Agnus::doBlitterDMA(u32 addr)
{
    // Assure that the Blitter owns the bus when this function is called
    assert(busOwner[pos.h] == BUS_BLITTER);

    u16 result = mem.peek16<BUS_BLITTER>(addr);

    assert(pos.h < HPOS_CNT);
    busOwner[pos.h] = BUS_BLITTER;
    busValue[pos.h] = result;
    stats.bus.raw[BUS_BLITTER]++;

    return result;
}

void
Agnus::doDiskDMA(u16 value)
{
    mem.pokeChip16(dskpt, value);
    dskpt += 2;

    assert(pos.h < HPOS_CNT);
    busOwner[pos.h] = BUS_DISK;
    busValue[pos.h] = value;
    stats.bus.raw[BUS_DISK]++;
}

void
Agnus::doCopperDMA(u32 addr, u16 value)
{
    mem.pokeCustom16<POKE_COPPER>(addr, value);

    assert(pos.h < HPOS_CNT);
    busOwner[pos.h] = BUS_COPPER;
    busValue[pos.h] = value;
    stats.bus.raw[BUS_COPPER]++;
}

void
Agnus::doBlitterDMA(u32 addr, u16 value)
{
    mem.poke16<BUS_BLITTER>(addr, value);

    assert(pos.h < HPOS_CNT);
    assert(busOwner[pos.h] == BUS_BLITTER); // Bus is already allocated
    busValue[pos.h] = value;
    stats.bus.raw[BUS_BLITTER]++;
}

void
Agnus::clearBplEvents()
{
    for (int i = 0; i < HPOS_MAX; i++) bplEvent[i] = EVENT_NONE;
    for (int i = 0; i < HPOS_MAX; i++) nextBplEvent[i] = HPOS_MAX;

    verifyBplEvents();
}

void
Agnus::updateBplEvents(u16 dmacon, u16 bplcon0, int first, int last)
{
    assert(first >= 0 && last < HPOS_CNT);

    int channels = bpu(bplcon0);
    bool hires = Denise::hires(bplcon0);

    // Set number of bitplanes to 0 if we are not in a bitplane DMA line
    if (!inBplDmaLine(dmacon, bplcon0)) channels = 0;
    assert(channels <= 6);

    // Allocate slots
    if (hires) {
        
        for (int i = first; i <= last; i++)
            bplEvent[i] =
            inHiresDmaAreaOdd(i) ? bplDMA[1][channels][i] :
            inHiresDmaAreaEven(i) ? bplDMA[1][channels][i] : EVENT_NONE;
        
        // Add extra shift register events if the even/odd DDF windows differ
        // These events are like BPL_H0 events without performing DMA.
        for (int i = ddfHires.strtEven; i < ddfHires.strtOdd; i++)
            if ((i & 3) == 3 && bplEvent[i] == EVENT_NONE) bplEvent[i] = BPL_SR;
        for (int i = ddfHires.stopOdd; i < ddfHires.stopEven; i++)
            if ((i & 3) == 3 && bplEvent[i] == EVENT_NONE) bplEvent[i] = BPL_SR;

    } else {
        
        for (int i = first; i <= last; i++)
            bplEvent[i] =
            inLoresDmaAreaOdd(i) ? bplDMA[0][channels][i] :
            inLoresDmaAreaEven(i) ? bplDMA[0][channels][i] : EVENT_NONE;
    
        // Add extra shift register events if the even/odd DDF windows differ
        // These events are like BPL_L0 events without performing DMA.
        for (int i = ddfLores.strtEven; i < ddfLores.strtOdd; i++)
             if ((i & 7) == 7 && bplEvent[i] == EVENT_NONE) bplEvent[i] = BPL_SR;
        for (int i = ddfLores.stopOdd; i < ddfLores.stopEven; i++)
             if ((i & 7) == 7 && bplEvent[i] == EVENT_NONE) bplEvent[i] = BPL_SR;
    }
        
    // Make sure the table ends with a BPL_EOL event
    bplEvent[HPOS_MAX] = BPL_EOL;

    // Update the drawing flags and update the jump table
    updateDrawingFlags(hires);
    
    verifyBplEvents();
}

void
Agnus::updateDrawingFlags(bool hires)
{
    assert(scrollHiresEven < 8);
    assert(scrollHiresOdd  < 8);
    assert(scrollLoresEven < 8);
    assert(scrollHiresOdd  < 8);
    
    // Superimpose the drawing flags (bits 0 and 1)
    // Bit 0 is used to for odd bitplanes and bit 1 for even bitplanes
    if (hires) {
        for (int i = scrollHiresOdd; i < HPOS_CNT; i += 4)
            bplEvent[i] = (EventID)(bplEvent[i] | 1);
        for (int i = scrollHiresEven; i < HPOS_CNT; i += 4)
            bplEvent[i] = (EventID)(bplEvent[i] | 2);
    } else {
        for (int i = scrollLoresOdd; i < HPOS_CNT; i += 8)
            bplEvent[i] = (EventID)(bplEvent[i] | 1);
        for (int i = scrollLoresEven; i < HPOS_CNT; i += 8)
            bplEvent[i] = (EventID)(bplEvent[i] | 2);
    }
    updateBplJumpTable();
}

void
Agnus::verifyBplEvents()
{
    assert((bplEvent[HPOS_MAX] & 0b11111100) == BPL_EOL);
    assert(nextBplEvent[HPOS_MAX] == 0);
}

void
Agnus::clearDasEvents()
{
    updateDasEvents(0);
}

void
Agnus::updateDasEvents(u16 dmacon)
{
    assert(dmacon < 64);

    // Allocate slots and renew the jump table
    for (int i = 0; i < 0x38; i++) dasEvent[i] = dasDMA[dmacon][i];
    updateDasJumpTable(0x38);

    verifyDasEvents();
}

void
Agnus::verifyDasEvents()
{
    assert(dasEvent[0x01] == DAS_REFRESH);
    assert(dasEvent[0xDF] == DAS_SDMA);

    for (int i = 0x34; i < 0xDF; i++) {
        assert(dasEvent[i] == EVENT_NONE);
        assert(nextDasEvent[i] == 0xDF);
    }
    for (int i = 0xE0; i < HPOS_CNT; i++) {
        assert(dasEvent[i] == EVENT_NONE);
        assert(nextDasEvent[i] == 0);
    }
}

void
Agnus::updateBplJumpTable(i16 end)
{
    assert(end <= HPOS_MAX);

    u8 next = nextBplEvent[end];
    for (int i = end; i >= 0; i--) {
        nextBplEvent[i] = next;
        if (bplEvent[i]) next = i;
    }
}

void
Agnus::updateDasJumpTable(i16 end)
{
    assert(end <= HPOS_MAX);

    u8 next = nextDasEvent[end];
    for (int i = end; i >= 0; i--) {
        nextDasEvent[i] = next;
        if (dasEvent[i]) next = i;
    }
}

void
Agnus::dumpEventTable(EventID *table, char str[256][3], int from, int to)
{
    char r1[256], r2[256], r3[256], r4[256], r5[256];
    int i;

    for (i = 0; i <= to - from; i++) {

        int digit1 = (from + i) / 16;
        int digit2 = (from + i) % 16;

        r1[i] = (digit1 < 10) ? digit1 + '0' : (digit1 - 10) + 'A';
        r2[i] = (digit2 < 10) ? digit2 + '0' : (digit2 - 10) + 'A';
        r3[i] = str[table[from + i]][0];
        r4[i] = str[table[from + i]][1];
        r5[i] = str[table[from + i]][2];
    }
    r1[i] = r2[i] = r3[i] = r4[i] = r5[i] = 0;

    msg("%s\n", r1);
    msg("%s\n", r2);
    msg("%s\n", r3);
    msg("%s\n", r4);
    msg("%s\n", r5);
}

void
Agnus::dumpBplEventTable(int from, int to)
{
    char str[256][3];

    memset(str, '?', sizeof(str));
    
    // Events
    for (int i = 0; i < 4; i++) {
        str[i][0] = '.';                str[i][1] = '.';
        str[(int)BPL_L1 + i][0]  = 'L'; str[(int)BPL_L1 + i][1]  = '1';
        str[(int)BPL_L2 + i][0]  = 'L'; str[(int)BPL_L2 + i][1]  = '2';
        str[(int)BPL_L3 + i][0]  = 'L'; str[(int)BPL_L3 + i][1]  = '3';
        str[(int)BPL_L4 + i][0]  = 'L'; str[(int)BPL_L4 + i][1]  = '4';
        str[(int)BPL_L5 + i][0]  = 'L'; str[(int)BPL_L5 + i][1]  = '5';
        str[(int)BPL_L6 + i][0]  = 'L'; str[(int)BPL_L6 + i][1]  = '6';
        str[(int)BPL_H1 + i][0]  = 'H'; str[(int)BPL_H1 + i][1]  = '1';
        str[(int)BPL_H2 + i][0]  = 'H'; str[(int)BPL_H2 + i][1]  = '2';
        str[(int)BPL_H3 + i][0]  = 'H'; str[(int)BPL_H3 + i][1]  = '3';
        str[(int)BPL_H4 + i][0]  = 'H'; str[(int)BPL_H4 + i][1]  = '4';
        str[(int)BPL_EOL + i][0] = 'E'; str[(int)BPL_EOL + i][1] = 'O';
    }

    // Drawing flags
    for (int i = 1; i < 256; i += 4) str[i][2] = 'o';
    for (int i = 2; i < 256; i += 4) str[i][2] = 'e';
    for (int i = 3; i < 256; i += 4) str[i][2] = 'b';

    dumpEventTable(bplEvent, str, from, to);
}

void
Agnus::dumpBplEventTable()
{
    // Dump the event table
    msg("Event table:\n\n");
    msg("ddfstrt = %X dffstop = %X\n", ddfstrt, ddfstop);
    msg("ddfLoresOdd:  (%X - %X)\n", ddfLores.strtOdd, ddfLores.stopOdd);
    msg("ddfLoresEven: (%X - %X)\n", ddfLores.strtEven, ddfLores.stopEven);
    msg("ddfHiresOdd:  (%X - %X)\n", ddfHires.strtOdd, ddfHires.stopOdd);
    msg("ddfHiresEven: (%X - %X)\n", ddfHires.strtEven, ddfHires.stopEven);

    dumpBplEventTable(0x00, 0x4F);
    dumpBplEventTable(0x50, 0x9F);
    dumpBplEventTable(0xA0, 0xE2);

    // Dump the jump table
    msg("\nJump table:\n\n");
    int i = nextBplEvent[0];
    msg("0 -> %X", i);
    while (i) {
        assert(i < HPOS_CNT);
        assert(nextBplEvent[i] == 0 || nextBplEvent[i] > i);
        i = nextBplEvent[i];
        msg(" -> %X", i);
    }
    msg("\n");
}

void
Agnus::dumpDasEventTable(int from, int to)
{
    char str[256][3];

    memset(str, '?', sizeof(str));
    str[(int)EVENT_NONE][0]  = '.'; str[(int)EVENT_NONE][1]  = '.';
    str[(int)DAS_REFRESH][0] = 'R'; str[(int)DAS_REFRESH][1] = 'E';
    str[(int)DAS_D0][0]      = 'D'; str[(int)DAS_D0][1]      = '0';
    str[(int)DAS_D1][0]      = 'D'; str[(int)DAS_D1][1]      = '1';
    str[(int)DAS_D2][0]      = 'D'; str[(int)DAS_D2][1]      = '2';
    str[(int)DAS_A0][0]      = 'A'; str[(int)DAS_A0][1]      = '0';
    str[(int)DAS_A1][0]      = 'A'; str[(int)DAS_A1][1]      = '1';
    str[(int)DAS_A2][0]      = 'A'; str[(int)DAS_A2][1]      = '2';
    str[(int)DAS_A3][0]      = 'A'; str[(int)DAS_A3][1]      = '3';
    str[(int)DAS_S0_1][0]    = '0'; str[(int)DAS_S0_1][1]    = '1';
    str[(int)DAS_S0_2][0]    = '0'; str[(int)DAS_S0_2][1]    = '2';
    str[(int)DAS_S1_1][0]    = '1'; str[(int)DAS_S1_1][1]    = '1';
    str[(int)DAS_S1_2][0]    = '1'; str[(int)DAS_S1_2][1]    = '2';
    str[(int)DAS_S2_1][0]    = '2'; str[(int)DAS_S2_1][1]    = '1';
    str[(int)DAS_S2_2][0]    = '2'; str[(int)DAS_S2_2][1]    = '2';
    str[(int)DAS_S3_1][0]    = '3'; str[(int)DAS_S3_1][1]    = '1';
    str[(int)DAS_S3_2][0]    = '3'; str[(int)DAS_S3_2][1]    = '2';
    str[(int)DAS_S4_1][0]    = '4'; str[(int)DAS_S4_1][1]    = '1';
    str[(int)DAS_S4_2][0]    = '4'; str[(int)DAS_S4_2][1]    = '2';
    str[(int)DAS_S5_1][0]    = '5'; str[(int)DAS_S5_1][1]    = '1';
    str[(int)DAS_S5_2][0]    = '5'; str[(int)DAS_S5_2][1]    = '2';
    str[(int)DAS_S6_1][0]    = '6'; str[(int)DAS_S6_1][1]    = '1';
    str[(int)DAS_S6_2][0]    = '6'; str[(int)DAS_S6_2][1]    = '2';
    str[(int)DAS_S7_1][0]    = '7'; str[(int)DAS_S7_1][1]    = '1';
    str[(int)DAS_S7_2][0]    = '7'; str[(int)DAS_S7_2][1]    = '2';
    str[(int)DAS_SDMA][0]    = 'S'; str[(int)DAS_SDMA][1]    = 'D';

    for (int i = 1; i < 256; i++) str[i][2] = ' ';
    
    dumpEventTable(dasEvent, str, from, to);
}

void
Agnus::dumpDasEventTable()
{
    // Dump the event table
    dumpDasEventTable(0x00, 0x4F);
    dumpDasEventTable(0x50, 0x9F);
    dumpDasEventTable(0xA0, 0xE2);
}

u16
Agnus::peekVHPOSR()
{
    // 15 14 13 12 11 10 09 08 07 06 05 04 03 02 01 00
    // V7 V6 V5 V4 V3 V2 V1 V0 H8 H7 H6 H5 H4 H3 H2 H1

    i16 posh = pos.h + 4;
    i16 posv = pos.v;

    // Check if posh has wrapped over (we just added 4)
    if (posh > HPOS_MAX) {
        posh -= HPOS_CNT;
        if (++posv >= frame.numLines()) posv = 0;
    }

    // The value of posv only shows up in cycle 2 and later
    if (posh > 1) {
        return HI_LO(posv & 0xFF, posh);
    }
    
    // In cycle 0 and 1, We need to return the old value of posv
    if (posv > 0) {
        return HI_LO((posv - 1) & 0xFF, posh);
    } else {
        return HI_LO(frame.prevLastLine() & 0xFF, posh);
    }
}

void
Agnus::pokeVHPOS(u16 value)
{
    debug(2, "pokeVHPOS(%X)\n", value);
    // Don't know what to do here ...
}

u16
Agnus::peekVPOSR()
{
    u16 id;

    // 15 14 13 12 11 10 09 08 07 06 05 04 03 02 01 00
    // LF I6 I5 I4 I3 I2 I1 I0 -- -- -- -- -- -- -- V8
    u16 result = (pos.v >> 8) | (frame.isLongFrame() ? 0x8000 : 0);
    assert((result & 0x7FFE) == 0);

    // Add indentification bits
    switch (config.revision) {

        case AGNUS_8367: id = 0x00; break;
        case AGNUS_8372: id = 0x20; break;
        case AGNUS_8375: id = 0x20; break; // TODO: CHECK ON REAL MACHINE
        default: assert(false);
    }
    result |= (id << 8);

    debug(2, "peekVPOSR() = %X\n", result);
    return result;
}

void
Agnus::pokeVPOS(u16 value)
{
    debug(2, "pokeVPOS(%x) (vpos = %d lof = %d)\n", value, pos.v, frame.lof);
    // Don't know what to do here ...
}

template <PokeSource s> void
Agnus::pokeDIWSTRT(u16 value)
{
    debug(DIW_DEBUG, "pokeDIWSTRT<%s>(%X)\n", pokeSourceName(s), value);
    recordRegisterChange(DMA_CYCLES(2), REG_DIWSTRT, value);
}

template <PokeSource s> void
Agnus::pokeDIWSTOP(u16 value)
{
    debug(DIW_DEBUG, "pokeDIWSTOP<%s>(%X)\n", pokeSourceName(s), value);
    recordRegisterChange(DMA_CYCLES(2), REG_DIWSTOP, value);
}

void
Agnus::setDIWSTRT(u16 value)
{
    debug(DIW_DEBUG, "setDIWSTRT(%X)\n", value);

    // 15 14 13 12 11 10  9  8  7  6  5  4  3  2  1  0
    // V7 V6 V5 V4 V3 V2 V1 V0 H7 H6 H5 H4 H3 H2 H1 H0  and  H8 = 0, V8 = 0

    diwstrt = value;

    // Extract the upper left corner of the display window
    i16 newDiwVstrt = HI_BYTE(value);
    i16 newDiwHstrt = LO_BYTE(value);

    debug(DIW_DEBUG, "newDiwVstrt = %d newDiwHstrt = %d\n", newDiwVstrt, newDiwHstrt);

    // Invalidate the horizontal coordinate if it is out of range
    if (newDiwHstrt < 2) {
        debug(DIW_DEBUG, "newDiwHstrt is too small\n");
        newDiwHstrt = -1;
    }

    /* Check if the change already takes effect in the current rasterline.
     *
     *     old: Old trigger coordinate (diwHstrt)
     *     new: New trigger coordinate (newDiwHstrt)
     *     cur: Position of the electron beam (derivable from pos.h)
     *
     * The following cases have to be taken into accout:
     *
     *    1) cur < old < new : Change takes effect in this rasterline.
     *    2) cur < new < old : Change takes effect in this rasterline.
     *    3) new < cur < old : Neither the old nor the new trigger hits.
     *    4) new < old < cur : Already triggered. Nothing to do in this line.
     *    5) old < cur < new : Already triggered. Nothing to do in this line.
     *    6) old < new < cur : Already triggered. Nothing to do in this line.
     */

    i16 cur = 2 * pos.h;

     // (1) and (2)
    if (cur < diwHstrt && cur < newDiwHstrt) {

        debug(DIW_DEBUG, "Updating DIW hflop immediately at %d\n", cur);
        diwHFlopOn = newDiwHstrt;
    }

    // (3)
    if (newDiwHstrt < cur && cur < diwHstrt) {

        debug(DIW_DEBUG, "DIW hflop not switched on in current line\n");
        diwHFlopOn = -1;
    }

    diwVstrt = newDiwVstrt;
    diwHstrt = newDiwHstrt;

    /* Update the vertical DIW flipflop
     * This is not 100% accurate. If the vertical DIW flipflop changes in the
     * middle of a rasterline, the effect is immediately visible on a real
     * Amiga. The current emulation code only evaluates the flipflop at the end
     * of the rasterline in the drawing routine of Denise. Hence, the whole
     * line will be blacked out, not just the rest of it.
     */
    if (pos.v == diwVstrt) diwVFlop = true;
    if (pos.v == diwVstop) diwVFlop = false;
}

void
Agnus::setDIWSTOP(u16 value)
{
    debug(DIW_DEBUG, "setDIWSTOP(%X)\n", value);
    
    // 15 14 13 12 11 10  9  8  7  6  5  4  3  2  1  0
    // V7 V6 V5 V4 V3 V2 V1 V0 H7 H6 H5 H4 H3 H2 H1 H0  and  H8 = 1, V8 = !V7

    diwstop = value;

    // Extract the lower right corner of the display window
    i16 newDiwVstop = HI_BYTE(value) | ((value & 0x8000) ? 0 : 0x100);
    i16 newDiwHstop = LO_BYTE(value) | 0x100;

    debug(DIW_DEBUG, "newDiwVstop = %d newDiwHstop = %d\n", newDiwVstop, newDiwHstop);

    // Invalidate the coordinate if it is out of range
    if (newDiwHstop > 0x1C7) {
        debug(DIW_DEBUG, "newDiwHstop is too large\n");
        newDiwHstop = -1;
    }

    // Check if the change already takes effect in the current rasterline.
    i16 cur = 2 * pos.h;

    // (1) and (2) (see setDIWSTRT)
    if (cur < diwHstop && cur < newDiwHstop) {

        debug(DIW_DEBUG, "Updating hFlopOff immediately at %d\n", cur);
        diwHFlopOff = newDiwHstop;
    }

    // (3) (see setDIWSTRT)
    if (newDiwHstop < cur && cur < diwHstop) {

        debug(DIW_DEBUG, "hFlop not switched off in current line\n");
        diwHFlopOff = -1;
    }

    diwVstop = newDiwVstop;
    diwHstop = newDiwHstop;

    /* Update the vertical DIW flipflop
     * This is not 100% accurate. See comment in setDIWSTRT().
     */
    if (pos.v == diwVstrt) diwVFlop = true;
    if (pos.v == diwVstop) diwVFlop = false;
}

void
Agnus::pokeDDFSTRT(u16 value)
{
    debug(DDF_DEBUG, "pokeDDFSTRT(%X)\n", value);

    //      15 13 12 11 10 09 08 07 06 05 04 03 02 01 00
    // OCS: -- -- -- -- -- -- -- H8 H7 H6 H5 H4 H3 -- --
    // ECS: -- -- -- -- -- -- -- H8 H7 H6 H5 H4 H3 H2 --

    value &= ddfMask();
    recordRegisterChange(DMA_CYCLES(2), REG_DDFSTRT, value);
}

void
Agnus::pokeDDFSTOP(u16 value)
{
    debug(DDF_DEBUG, "pokeDDFSTOP(%X)\n", value);

    //      15 13 12 11 10 09 08 07 06 05 04 03 02 01 00
    // OCS: -- -- -- -- -- -- -- H8 H7 H6 H5 H4 H3 -- --
    // ECS: -- -- -- -- -- -- -- H8 H7 H6 H5 H4 H3 H2 --

    value &= ddfMask();
    recordRegisterChange(DMA_CYCLES(2), REG_DDFSTOP, value);
}

void
Agnus::setDDFSTRT(u16 old, u16 value)
{
    debug(DDF_DEBUG, "setDDFSTRT(%X, %X)\n", old, value);

    ddfstrt = value;

    // Tell the hsync handler to recompute the DDF window
    hsyncActions |= HSYNC_PREDICT_DDF;

    // Take immediate action if we haven't reached the old DDFSTRT cycle yet
    if (pos.h < ddfstrtReached) {

        // Check if the new position has already been passed
        if (ddfstrt <= pos.h + 2) {

            // DDFSTRT never matches in the current rasterline. Disable DMA
            ddfstrtReached = -1;
            clearBplEvents();
            scheduleNextBplEvent();

        } else {

            // Update the matching position and recalculate the DMA table
            ddfstrtReached = ddfstrt > HPOS_MAX ? -1 : ddfstrt;
            computeDDFWindow();
            updateBplEvents();
            scheduleNextBplEvent();
        }
    }
}

void
Agnus::setDDFSTOP(u16 old, u16 value)
{
    debug(DDF_DEBUG, "setDDFSTOP(%X, %X)\n", old, value);

    ddfstop = value;

    // Tell the hsync handler to recompute the DDF window
    hsyncActions |= HSYNC_PREDICT_DDF;

    // Take action if we haven't reached the old DDFSTOP cycle yet
     if (pos.h + 2 < ddfstopReached || ddfstopReached == -1) {

         // Check if the new position has already been passed
         if (ddfstop <= pos.h + 2) {

             // DDFSTOP won't match in the current rasterline
             ddfstopReached = -1;

         } else {

             // Update the matching position and recalculate the DMA table
             ddfstopReached = (ddfstop > HPOS_MAX) ? -1 : ddfstop;
             if (ddfstrtReached >= 0) {
                 computeDDFWindow();
                 updateBplEvents();
                 scheduleNextBplEvent();
             }
         }
     }
}

void
Agnus::predictDDF()
{
    DDF oldLores = ddfLores;
    DDF oldHires = ddfHires;
    DDFState oldState = ddfState;
    
    ddfstrtReached = ddfstrt < HPOS_CNT ? ddfstrt : -1;
    ddfstopReached = ddfstop < HPOS_CNT ? ddfstop : -1;
    
    computeDDFWindow();
    
    if (ddfLores != oldLores || ddfHires != oldHires || ddfState != oldState) {
        
        hsyncActions |= HSYNC_UPDATE_BPL_TABLE; // Update bitplane events
        hsyncActions |= HSYNC_PREDICT_DDF;      // Call this function again
    }
    
    debug(DDF_DEBUG, "predictDDF LORES: %d %d\n", ddfLores.strtOdd, ddfLores.stopOdd);
    debug(DDF_DEBUG, "predictDDF HIRES: %d %d\n", ddfHires.strtOdd, ddfHires.stopOdd);
}

void
Agnus::computeDDFWindow()
{
    isOCS() ? computeDDFWindowOCS() : computeDDFWindowECS();
}

#define DDF_EMPTY     0
#define DDF_STRT_STOP 1
#define DDF_STRT_D8   2
#define DDF_18_STOP   3
#define DDF_18_D8     4

void
Agnus::computeDDFWindowOCS()
{
    /* To determine the correct data fetch window, we need to distinguish
     * three kinds of DDFSTRT / DDFSTOP values.
     *
     *   0:   small : Value is smaller than the left hardware stop.
     *   1:  medium : Value complies to the specs.
     *   2:   large : Value is larger than HPOS_MAX and thus never reached.
     */
    int strt = (ddfstrtReached < 0) ? 2 : (ddfstrtReached < 0x18) ? 0 : 1;
    int stop = (ddfstopReached < 0) ? 2 : (ddfstopReached < 0x18) ? 0 : 1;

    /* Emulate the special "scan line effect" of the OCS Agnus.
     * If DDFSTRT is set to a small value, DMA is enabled every other row.
     */
    if (ddfstrtReached < 0x18) {
        if (ocsEarlyAccessLine == pos.v) {
            ddfLores.compute(ddfstrtReached, ddfstopReached, bplcon1 & 0xF);
            ddfHires.compute(ddfstrtReached, ddfstopReached, bplcon1 & 0xF);
        } else {
            ddfLores.clear();
            ddfHires.clear();
            ocsEarlyAccessLine = pos.v + 1;
        }
        return;
    }

    /* Nr | DDFSTRT | DDFSTOP | State   || Data Fetch Window   | Next State
     *  --------------------------------------------------------------------
     *  0 | small   | small   | -       || Empty               | DDF_OFF
     *  1 | small   | medium  | -       || [0x18 ; DDFSTOP]    | DDF_OFF
     *  2 | small   | large   | -       || [0x18 ; 0xD8]       | DDF_OFF
     *  3 | medium  | small   | -       || not handled         | DDF_OFF
     *  4 | medium  | medium  | -       || [DDFSTRT ; DDFSTOP] | DDF_OFF
     *  5 | medium  | large   | -       || [DDFSTRT ; 0xD8]    | DDF_OFF
     *  6 | large   | small   | -       || not handled         | DDF_OFF
     *  7 | large   | medium  | -       || not handled         | DDF_OFF
     *  8 | large   | large   | -       || Empty               | DDF_OFF
     */
    const struct { int interval; } table[9] = {
        { DDF_EMPTY     }, // 0
        { DDF_18_STOP   }, // 1
        { DDF_18_D8     }, // 2
        { DDF_EMPTY     }, // 3
        { DDF_STRT_STOP }, // 4
        { DDF_STRT_D8   }, // 5
        { DDF_EMPTY     }, // 6
        { DDF_EMPTY     }, // 7
        { DDF_EMPTY     }  // 8
    };

    int index = 3*strt + stop;
    switch (table[index].interval) {

        case DDF_EMPTY:
            ddfLores.clear();
            ddfHires.clear();
            break;
        case DDF_STRT_STOP:
            ddfLores.compute(ddfstrtReached, ddfstopReached, bplcon1 & 0xF);
            ddfHires.compute(ddfstrtReached, ddfstopReached, bplcon1 & 0xF);
            break;
        case DDF_STRT_D8:
            ddfLores.compute(ddfstrtReached, 0xD8, bplcon1 & 0xF);
            ddfHires.compute(ddfstrtReached, 0xD8, bplcon1 & 0xF);
            break;
        case DDF_18_STOP:
            ddfLores.compute(0x18, ddfstopReached, bplcon1 & 0xF);
            ddfHires.compute(0x18, ddfstopReached, bplcon1 & 0xF);
            break;
        case DDF_18_D8:
            ddfLores.compute(0x18, 0xD8, bplcon1 & 0xF);
            ddfHires.compute(0x18, 0xD8, bplcon1 & 0xF);
            break;
    }

    debug(DDF_DEBUG, "DDF Window Odd (OCS):  (%d,%d) (%d,%d)\n",
          ddfLores.strtOdd, ddfHires.strtOdd, ddfLores.stopOdd, ddfHires.stopOdd);
    debug(DDF_DEBUG, "DDF Window Even (OCS): (%d,%d) (%d,%d)\n",
          ddfLores.strtEven, ddfHires.strtEven, ddfLores.stopEven, ddfHires.stopEven);

    return;
}

void
Agnus::computeDDFWindowECS()
{
    /* To determine the correct data fetch window, we need to distinguish
     * three kinds of DDFSTRT / DDFSTOP values.
     *
     *   0:   small : Value is smaller than the left hardware stop.
     *   1:  medium : Value complies to the specs.
     *   2:   large : Value is larger than HPOS_MAX and thus never reached.
     */
    int strt = (ddfstrtReached < 0) ? 2 : (ddfstrtReached < 0x18) ? 0 : 1;
    int stop = (ddfstopReached < 0) ? 2 : (ddfstopReached < 0x18) ? 0 : 1;

    /* Nr | DDFSTRT | DDFSTOP | State   || Data Fetch Window   | Next State
     *  --------------------------------------------------------------------
     *  0 | small   | small   | DDF_OFF || Empty               | DDF_OFF
     *  1 | small   | small   | DDF_ON  || Empty               | DDF_OFF
     *  2 | small   | medium  | DDF_OFF || [0x18 ; DDFSTOP]    | DDF_OFF
     *  3 | small   | medium  | DDF_ON  || [0x18 ; DDFSTOP]    | DDF_OFF
     *  4 | small   | large   | DDF_OFF || [0x18 ; 0xD8]       | DDF_ON
     *  5 | small   | large   | DDF_ON  || [0x18 ; 0xD8]       | DDF_ON
     *  6 | medium  | small   | DDF_OFF || not handled         | -
     *  7 | medium  | small   | DDF_ON  || not handled         | -
     *  8 | medium  | medium  | DDF_OFF || [DDFSTRT ; DDFSTOP] | DDF_OFF
     *  9 | medium  | medium  | DDF_ON  || [0x18 ; DDFSTOP]    | DDF_OFF
     * 10 | medium  | large   | DDF_OFF || [DDFSTRT ; 0xD8]    | DDF_ON
     * 11 | medium  | large   | DDF_ON  || [0x18 ; 0xD8]       | DDF_ON
     * 12 | large   | small   | DDF_OFF || not handled         | -
     * 13 | large   | small   | DDF_ON  || not handled         | -
     * 14 | large   | medium  | DDF_OFF || not handled         | -
     * 15 | large   | medium  | DDF_ON  || not handled         | -
     * 16 | large   | large   | DDF_OFF || Empty               | DDF_OFF
     * 17 | large   | large   | DDF_ON  || [0x18 ; 0xD8]       | DDF_ON
     */
    const struct { int interval; DDFState state; } table[18] = {
        { DDF_EMPTY ,    DDF_OFF }, // 0
        { DDF_EMPTY ,    DDF_OFF }, // 1
        { DDF_18_STOP ,  DDF_OFF }, // 2
        { DDF_18_STOP ,  DDF_OFF }, // 3
        { DDF_18_D8 ,    DDF_ON  }, // 4
        { DDF_18_D8 ,    DDF_ON  }, // 5
        { DDF_EMPTY ,    DDF_OFF }, // 6
        { DDF_EMPTY ,    DDF_OFF }, // 7
        { DDF_STRT_STOP, DDF_OFF }, // 8
        { DDF_18_STOP ,  DDF_OFF }, // 9
        { DDF_STRT_D8 ,  DDF_ON  }, // 10
        { DDF_18_D8 ,    DDF_ON  }, // 11
        { DDF_EMPTY ,    DDF_OFF }, // 12
        { DDF_EMPTY ,    DDF_OFF }, // 13
        { DDF_EMPTY ,    DDF_OFF }, // 14
        { DDF_EMPTY ,    DDF_OFF }, // 15
        { DDF_EMPTY ,    DDF_OFF }, // 16
        { DDF_18_D8 ,    DDF_ON  }, // 17
    };

    int index = 6*strt + 2*stop + (ddfState == DDF_ON);
    switch (table[index].interval) {

        case DDF_EMPTY:
            ddfLores.clear();
            ddfHires.clear();
            break;
        case DDF_STRT_STOP:
            ddfLores.compute(ddfstrtReached, ddfstopReached, bplcon1);
            ddfHires.compute(ddfstrtReached, ddfstopReached, bplcon1);
            break;
        case DDF_STRT_D8:
            ddfLores.compute(ddfstrtReached, 0xD8, bplcon1);
            ddfHires.compute(ddfstrtReached, 0xD8, bplcon1);
            break;
        case DDF_18_STOP:
            ddfLores.compute(0x18, ddfstopReached, bplcon1);
            ddfHires.compute(0x18, ddfstopReached, bplcon1);
            break;
        case DDF_18_D8:
            ddfLores.compute(0x18, 0xD8, bplcon1);
            ddfHires.compute(0x18, 0xD8, bplcon1);
            break;
    }
    ddfState = table[index].state;

    debug(DDF_DEBUG, "DDF Window Odd (ECS):  (%d,%d) (%d,%d)\n",
          ddfLores.strtOdd, ddfHires.strtOdd, ddfLores.stopOdd, ddfHires.stopOdd);
    debug(DDF_DEBUG, "DDF Window Even (ECS): (%d,%d) (%d,%d)\n",
          ddfLores.strtEven, ddfHires.strtEven, ddfLores.stopEven, ddfHires.stopEven);

    return;
}

/*
void
Agnus::computeStandardDDFWindow(i16 strt, i16 stop)
{
    // BPLCON1 also affect the DDF window
    i16 loresStrt = strt - ((bplcon1 & 0xF) >> 1);
    i16 hiresStrt = strt - ((bplcon1 & 0x7) >> 1);

    // Align ddfstrt at the start of the next fetch unit
    int loresShift = (8 - (loresStrt & 0b111)) & 0b111;
    int hiresShift = (4 - (hiresStrt & 0b11)) & 0b11;

    // Compute the beginning of the DDF window
    ddfStrtLores = loresStrt + loresShift;
    ddfStrtHires = hiresStrt + hiresShift;

    // Compute the number of fetch units
    int fetchUnits = ((stop - strt) + 15) >> 3;

    // Compute the end of the DDF window
    ddfStopLores = MIN(ddfStrtLores + 8 * fetchUnits, 0xE0);
    ddfStopHires = MIN(ddfStrtHires + 8 * fetchUnits, 0xE0);
}
*/

void
Agnus::pokeBPLCON0(u16 value)
{
    debug(DMA_DEBUG, "pokeBPLCON0(%X)\n", value);

    if (bplcon0 != value) {
        recordRegisterChange(DMA_CYCLES(4), REG_BPLCON0_AGNUS, value);
    }
}

void
Agnus::setBPLCON0(u16 oldValue, u16 newValue)
{
    assert(oldValue != newValue);

    debug(DMA_DEBUG, "setBPLCON0(%X,%X)\n", oldValue, newValue);

    // Update variable bplcon0AtDDFStrt if DDFSTRT has not been reached yet
    if (pos.h < ddfstrtReached) bplcon0AtDDFStrt = newValue;

    // Update the bpl event table in the next rasterline
    hsyncActions |= HSYNC_UPDATE_BPL_TABLE;

    // Check if the hires bit or one of the BPU bits have been modified
    if ((oldValue ^ newValue) & 0xF000) {

        /*
        debug("oldBplcon0 = %X newBplcon0 = %X\n", oldBplcon0, newBplcon0);
        dumpBplEventTable();
         */

        /* TODO:
         * BPLCON0 is usually written in each frame.
         * To speed up, just check the hpos. If it is smaller than the start
         * of the DMA window, a standard update() is enough and the scheduled
         * update in hsyncActions (HSYNC_UPDATE_BPL_TABLE) can be omitted.
         */

        // Update the DMA allocation table
        updateBplEvents(dmacon, newValue, pos.h);

        // Since the table has changed, we also need to update the event slot
        scheduleBplEventForCycle(pos.h);
    }

    bplcon0 = newValue;
}

void
Agnus::pokeBPLCON1(u16 value)
{
    debug(DMA_DEBUG, "pokeBPLCON1(%X)\n", value);

    if (bplcon1 != value) {
        recordRegisterChange(DMA_CYCLES(1), REG_BPLCON1_AGNUS, value);
    }
}

void
Agnus::setBPLCON1(u16 oldValue, u16 newValue)
{
    assert(oldValue != newValue);
    debug(DMA_DEBUG, "setBPLCON1(%X,%X)\n", oldValue, newValue);

    bplcon1 = newValue & 0xFF;

    // Compute comparision values for the hpos counter
    scrollLoresOdd  = (bplcon1 & 0b00001110) >> 1;
    scrollLoresEven = (bplcon1 & 0b11100000) >> 5;
    scrollHiresOdd  = (bplcon1 & 0b00000110) >> 1;
    scrollHiresEven = (bplcon1 & 0b01100000) >> 5;
    
    // Update the bitplane event table starting at the current hpos
    updateBplEvents(pos.h);
    
    // Update the scheduled bitplane event according to the new table
    scheduleBplEventForCycle(pos.h);
    
    // Schedule the bitplane event table to be recomputed
    agnus.hsyncActions |= HSYNC_UPDATE_BPL_TABLE;
    
    // Schedule the DDF window to be recomputed
    hsyncActions |= HSYNC_PREDICT_DDF;
}

int
Agnus::bpu(u16 v)
{
    // Extract the three BPU bits and check for hires mode
    int bpu = (v >> 12) & 0b111;
    bool hires = GET_BIT(v, 15);

    if (hires) {
        return bpu < 5 ? bpu : 0; // Disable all channels if value is invalid
    } else {
        return bpu < 7 ? bpu : 4; // Enable four channels if value is invalid
    }
}

void
Agnus::execute()
{
    // Process pending events
    if (nextTrigger <= clock) {
        executeEventsUntil(clock);
    } else {
        assert(pos.h < 0xE2);
    }

    // Advance the internal clock and the horizontal counter
    clock += DMA_CYCLES(1);

    // pos.h++;
    assert(pos.h <= HPOS_MAX);
    pos.h = pos.h < HPOS_MAX ? pos.h + 1 : 0; // (pos.h + 1) % HPOS_CNT;

    // If this assertion hits, the HSYNC event hasn't been served
    /*
    if (pos.h > HPOS_CNT) {
        dump();
        dumpBplEventTable();
    }
    */
    assert(pos.h <= HPOS_CNT);
}

#ifdef AGNUS_EXEC_DEBUG

void
Agnus::executeUntil(Cycle targetClock)
{
    // Align to DMA cycle raster
    targetClock &= ~0b111;

    // Compute the number of DMA cycles to execute
    DMACycle dmaCycles = (targetClock - clock) / DMA_CYCLES(1);

    // Execute DMA cycles one after another
    for (DMACycle i = 0; i < dmaCycles; i++) execute();
}

#else

void
Agnus::executeUntil(Cycle targetClock)
{
    // Align to DMA cycle raster
    targetClock &= ~0b111;

    // Compute the number of DMA cycles to execute
    DMACycle dmaCycles = (targetClock - clock) / DMA_CYCLES(1);

    if (targetClock < nextTrigger && dmaCycles > 0) {

        // Advance directly to the target clock
        clock = targetClock;
        pos.h += dmaCycles;

        // If this assertion hits, the HSYNC event hasn't been served
        assert(pos.h <= HPOS_CNT);

    } else {

        // Execute DMA cycles one after another
        for (DMACycle i = 0; i < dmaCycles; i++) execute();
    }
}
#endif

void
Agnus::executeUntilBusIsFree()
{
    i16 posh = pos.h == 0 ? HPOS_MAX : pos.h - 1;

    // Check if the bus is blocked
    if (busOwner[posh] != BUS_NONE) {

        // This variable counts the number of DMA cycles the CPU will be suspended
        DMACycle delay = 0;

        // Execute Agnus until the bus is free
        do {

            posh = pos.h;
            execute();
            if (++delay == 2) bls = true;

        } while (busOwner[posh] != BUS_NONE);

        // Clear the BLS line (Blitter slow down)
        bls = false;

        // Add wait states to the CPU
        cpu.addWaitStates(AS_CPU_CYCLES(DMA_CYCLES(delay)));
    }

    // Assign bus to the CPU
    busOwner[posh] = BUS_CPU;
}

void
Agnus::recordRegisterChange(Cycle delay, u32 addr, u16 value)
{
    // Record the new register value
    changeRecorder.insert(clock + delay, RegChange { addr, value} );
    
    // Schedule the register change
    scheduleNextREGEvent();
}

void
Agnus::updateRegisters()
{
}

template <int nr> void
Agnus::executeFirstSpriteCycle()
{
    debug(SPR_DEBUG, "executeFirstSpriteCycle<%d>\n", nr);

    if (pos.v == sprVStop[nr]) {

        sprDmaState[nr] = SPR_DMA_IDLE;

        // Read in the next control word (POS part)
        u16 value = doSpriteDMA<nr>();
        agnus.pokeSPRxPOS<nr>(value);
        denise.pokeSPRxPOS<nr>(value);

    } else if (sprDmaState[nr] == SPR_DMA_ACTIVE) {

        // Read in the next data word (part A)
        u16 value = doSpriteDMA<nr>();
        denise.pokeSPRxDATA<nr>(value);
    }
}

template <int nr> void
Agnus::executeSecondSpriteCycle()
{
    debug(SPR_DEBUG, "executeSecondSpriteCycle<%d>\n", nr);

    if (pos.v == sprVStop[nr]) {

        sprDmaState[nr] = SPR_DMA_IDLE;

        // Read in the next control word (CTL part)
        u16 value = doSpriteDMA<nr>();
        agnus.pokeSPRxCTL<nr>(value);
        denise.pokeSPRxCTL<nr>(value);

    } else if (sprDmaState[nr] == SPR_DMA_ACTIVE) {

        // Read in the next data word (part B)
        u16 value = doSpriteDMA<nr>();
        denise.pokeSPRxDATB<nr>(value);
    }
}

void
Agnus::updateSpriteDMA()
{
    // When the function is called, the sprite logic already sees an inremented
    // vertical position counter
    i16 v = pos.v + 1;

    // Reset the vertical trigger coordinates in line 25
    if (v == 25 && sprdma()) {
        for (int i = 0; i < 8; i++) sprVStop[i] = 25;
        return;
     }

    // Disable DMA in the last rasterline
    if (v == frame.lastLine()) {
        for (int i = 0; i < 8; i++) sprDmaState[i] = SPR_DMA_IDLE;
        return;
    }

    // Update the DMA status for all sprites
    for (int i = 0; i < 8; i++) {
        if (v == sprVStrt[i]) sprDmaState[i] = SPR_DMA_ACTIVE;
        if (v == sprVStop[i]) sprDmaState[i] = SPR_DMA_IDLE;
    }
}

void
Agnus::hsyncHandler()
{
    assert(pos.h == 0 || pos.h == HPOS_MAX + 1);

    // Call the hsync handlers of Denise and Paula
    denise.endOfLine(pos.v);

    // Synthesize sound samples
    audioUnit.executeUntil(clock - 50 * DMA_CYCLES(HPOS_CNT));

    // Update pot counters
    if (paula.chargeX0 < 1.0) paula.potCntX0++;
    if (paula.chargeY0 < 1.0) paula.potCntY0++;
    if (paula.chargeX1 < 1.0) paula.potCntX1++;
    if (paula.chargeY1 < 1.0) paula.potCntY1++;

    // Let CIA B count the HSYNCs
    amiga.ciaB.incrementTOD();

    // Reset the horizontal counter
    pos.h = 0;

    // Advance the vertical counter
    if (++pos.v >= frame.numLines()) vsyncHandler();

    // Initialize variables which keep values for certain trigger positions
    dmaconAtDDFStrt = dmacon;
    bplcon0AtDDFStrt = bplcon0;


    //
    // DIW
    //

    if (pos.v == diwVstrt && !diwVFlop) {
        diwVFlop = true;
        debug(DIW_DEBUG, "diwVFlop = %d\n", diwVFlop);
    }
    if (pos.v == diwVstop && diwVFlop) {
        diwVFlop = false;
        debug(DIW_DEBUG, "diwVFlop = %d\n", diwVFlop);
    }

    // Horizontal DIW flipflop
    diwHFlop = (diwHFlopOff != -1) ? false : (diwHFlopOn != -1) ? true : diwHFlop;
    diwHFlopOn = diwHstrt;
    diwHFlopOff = diwHstop;


    //
    // DDF
    //

    // Update the vertical DDF flipflop
    ddfVFlop = !inLastRasterline() && diwVFlop;


    //
    // Determine the bitplane DMA status for the line to come
    //

    bool newBplDmaLine = inBplDmaLine();

    // Update the bpl event table if the value has changed
    if (newBplDmaLine ^ bplDmaLine) {
        hsyncActions |= HSYNC_UPDATE_BPL_TABLE;
        bplDmaLine = newBplDmaLine;
    }


    //
    // Determine the disk, audio and sprite DMA status for the line to come
    //

    u16 newDmaDAS;

    if (dmacon & DMAEN) {

        // Copy DMA enable bits from dmacon
        newDmaDAS = dmacon & 0b111111;

        // Disable sprites outside the sprite DMA area
        if (pos.v < 25 || pos.v >= frame.lastLine()) newDmaDAS &= 0b011111;

    } else {

        newDmaDAS = 0;
    }

    if (dmaDAS != newDmaDAS) hsyncActions |= HSYNC_UPDATE_DAS_TABLE;
    dmaDAS = newDmaDAS;

    //
    // Process pending work items
    //

    if (hsyncActions) {

        if (hsyncActions & HSYNC_PREDICT_DDF) {
            hsyncActions &= ~HSYNC_PREDICT_DDF;
            predictDDF();
        }
        if (hsyncActions & HSYNC_UPDATE_BPL_TABLE) {
            hsyncActions &= ~HSYNC_UPDATE_BPL_TABLE;
            updateBplEvents();
        }
        if (hsyncActions & HSYNC_UPDATE_DAS_TABLE) {
            hsyncActions &= ~HSYNC_UPDATE_DAS_TABLE;
            updateDasEvents(dmaDAS);
        }
    }

    // Clear the bus usage table
    for (int i = 0; i < HPOS_CNT; i++) busOwner[i] = BUS_NONE;

    // Schedule the first BPL and DAS events
    scheduleNextBplEvent();
    scheduleNextDasEvent();


    //
    // Let other components prepare for the next line
    //

    denise.beginOfLine(pos.v);
}

void
Agnus::vsyncHandler()
{
    // Advance to the next frame
    frame.next(denise.lace());

    // Reset vertical position counter
    pos.v = 0;

    // Initialize the DIW flipflops
    diwVFlop = false;
    diwHFlop = true; 
    
    // CIA A counts VSYNCs
    amiga.ciaA.incrementTOD();
        
    // Let other subcomponents do their own VSYNC stuff
    blitter.vsyncHandler();
    copper.vsyncHandler();
    denise.beginOfFrame(frame.interlaced);
    diskController.vsyncHandler();
    joystick1.execute();
    joystick2.execute();

    // Update statistics (DEPRECATED)
    amiga.updateStats();

    // Update statistics
    updateStats();
    mem.updateStats();
    
    // Count some sheep (zzzzzz) ...
    if (!amiga.getWarp()) {
        amiga.synchronizeTiming();
    }
}

void
Agnus::serviceVblEvent()
{
    assert(slot[VBL_SLOT].id == VBL_STROBE);
    assert(pos.v == 0 || pos.v == 1);
    assert(pos.h == 1);

    paula.setINTREQ(true, 1 << INT_VERTB);
    rescheduleRel<VBL_SLOT>(cyclesInFrame());
}


//
// Instantiate template functions
//

template void Agnus::executeFirstSpriteCycle<0>();
template void Agnus::executeFirstSpriteCycle<1>();
template void Agnus::executeFirstSpriteCycle<2>();
template void Agnus::executeFirstSpriteCycle<3>();
template void Agnus::executeFirstSpriteCycle<4>();
template void Agnus::executeFirstSpriteCycle<5>();
template void Agnus::executeFirstSpriteCycle<6>();
template void Agnus::executeFirstSpriteCycle<7>();

template void Agnus::executeSecondSpriteCycle<0>();
template void Agnus::executeSecondSpriteCycle<1>();
template void Agnus::executeSecondSpriteCycle<2>();
template void Agnus::executeSecondSpriteCycle<3>();
template void Agnus::executeSecondSpriteCycle<4>();
template void Agnus::executeSecondSpriteCycle<5>();
template void Agnus::executeSecondSpriteCycle<6>();
template void Agnus::executeSecondSpriteCycle<7>();



template u16 Agnus::doBitplaneDMA<0>();
template u16 Agnus::doBitplaneDMA<1>();
template u16 Agnus::doBitplaneDMA<2>();
template u16 Agnus::doBitplaneDMA<3>();
template u16 Agnus::doBitplaneDMA<4>();
template u16 Agnus::doBitplaneDMA<5>();

template u16 Agnus::doAudioDMA<0>();
template u16 Agnus::doAudioDMA<1>();
template u16 Agnus::doAudioDMA<2>();
template u16 Agnus::doAudioDMA<3>();

template void Agnus::pokeDIWSTRT<POKE_CPU>(u16 value);
template void Agnus::pokeDIWSTRT<POKE_COPPER>(u16 value);
template void Agnus::pokeDIWSTOP<POKE_CPU>(u16 value);
template void Agnus::pokeDIWSTOP<POKE_COPPER>(u16 value);

template bool Agnus::allocateBus<BUS_COPPER>();
template bool Agnus::allocateBus<BUS_BLITTER>();

template bool Agnus::busIsFree<BUS_COPPER>();
template bool Agnus::busIsFree<BUS_BLITTER>();
