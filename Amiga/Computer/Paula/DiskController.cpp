// -----------------------------------------------------------------------------
// This file is part of vAmiga
//
// Copyright (C) Dirk W. Hoffmann. www.dirkwhoffmann.de
// Licensed under the GNU General Public License v3
//
// See https://www.gnu.org for license information
// -----------------------------------------------------------------------------

#include "Amiga.h"

DiskController::DiskController()
{
    setDescription("DiskController");
}

void
DiskController::_initialize()
{
    mem = &amiga->mem;
    agnus = &amiga->agnus;
    paula  = &amiga->paula;
    
    df[0] = &amiga->df0;
    df[1] = &amiga->df1;
    df[2] = &amiga->df2;
    df[3] = &amiga->df3;
}

void
DiskController::_powerOn()
{

}

void
DiskController::_reset()
{
    RESET_SNAPSHOT_ITEMS

    prb = 0xFF;
    selected = -1;
    dsksync = 0x4489;

    if (diskToInsert) {
        free(diskToInsert);
        diskToInsert = NULL;
    }
}

void
DiskController::_ping()
{
    for (int df = 0; df < 4; df++) {
        amiga->putMessage(connected[df] ? MSG_DRIVE_CONNECT : MSG_DRIVE_DISCONNECT, df);
    }
}

void
DiskController::_inspect()
{
    pthread_mutex_lock(&lock);

    info.selectedDrive = selected;
    info.state = state;
    info.fifoCount = fifoCount;
    info.dsklen = dsklen;
    info.dskbytr = mem->spypeekChip16(DSKBYTR);
    info.dsksync = dsksync;
    info.prb = prb;
 
    for (unsigned i = 0; i < 6; i++) {
        info.fifo[i] = (fifo >> (8 * i)) & 0xFF;
    }
    pthread_mutex_unlock(&lock);
}

void
DiskController::_dump()
{
    plainmsg("    connected : %d,%d,%d,%d\n",
             connected[0], connected[1], connected[2], connected[3]);
    plainmsg("fifoBuffering : %s\n", fifoBuffering ? "yes" : "no");
    plainmsg("     selected : %d\n", selected);
    plainmsg(" acceleration : %d\n", acceleration);
    plainmsg("        state : %s\n", driveStateName(state));
    plainmsg("     syncFlag : %s\n", syncFlag ? "true" : "false");
    plainmsg("     incoming : %X (cylcle = %lld)\n", incoming, incomingCycle);
    plainmsg("         fifo : %llX (count = %d)\n", fifo, fifoCount);
    plainmsg("\n");
    plainmsg("       dsklen : %X\n", dsklen);
    plainmsg("      dsksync : %X\n", dsksync);
    plainmsg("          prb : %X\n", prb);
    plainmsg("\n");
    plainmsg("   spinning() : %d\n", spinning());
}

bool
DiskController::spinning(unsigned driveNr)
{
    assert(driveNr < 4);
    return df[driveNr]->motor;
}

bool
DiskController::spinning()
{
    // debug("spinning = %d%d%d%d\n", df[0]->motor, df[1]->motor, df[2]->motor, df[3]->motor); 
    return df[0]->motor || df[1]->motor || df[2]->motor || df[3]->motor;
}

DiskControllerInfo
DiskController::getInfo()
{
    DiskControllerInfo result;
    
    pthread_mutex_lock(&lock);
    result = info;
    pthread_mutex_unlock(&lock);
    
    return result;
}

void
DiskController::setState(DriveState state)
{
    this->state = state;
}

void
DiskController::setConnected(int df, bool value)
{
    assert(df < 4);
    
    // We don't allow the internal drive (Df0) to be disconnected
    if (df == 0 && value == false) { return; }
    
    // Plug the drive in our out and inform the GUI
    connected[df] = value;
    amiga->putMessage(value ? MSG_DRIVE_CONNECT : MSG_DRIVE_DISCONNECT, df);
    amiga->putMessage(MSG_CONFIG);
}

Drive *
DiskController::getSelectedDrive()
{
    assert(selected < 4);
    return selected < 0 ? NULL : df[selected];
}

void
DiskController::ejectDisk(int nr, Cycle delay)
{
    assert(nr >= 0 && nr <= 3);

    debug("ejectDisk(%d, %d)\n", nr, delay);

    amiga->suspend();
    agnus->scheduleRel<DCH_SLOT>(delay, DCH_EJECT, nr);
    amiga->resume();
}

void
DiskController::insertDisk(class Disk *disk, int nr, Cycle delay)
{
    assert(disk != NULL);
    assert(nr >= 0 && nr <= 3);

    debug(DSK_DEBUG, "insertDisk(%p, %d, %d)\n", disk, nr, delay);

    amiga->suspend();

    if (df[nr]->hasDisk()) {

        // Eject the old disk first
        df[nr]->ejectDisk();

        // Make sure there is enough time between ejecting and inserting
        delay = MAX(SEC(1.5), delay);
    }

    diskToInsert = disk;
    agnus->scheduleRel<DCH_SLOT>(delay, DCH_INSERT, nr);
    
    amiga->resume();
}

void
DiskController::insertDisk(class ADFFile *file, int nr, Cycle delay)
{
    if (Disk *disk = Disk::makeWithFile(file)) {
        insertDisk(disk, nr, delay);
    }
}

void
DiskController::setWriteProtection(int nr, bool value)
{
    assert(nr >= 0 && nr <= 3);
    df[nr]->setWriteProtection(value);
}

uint16_t
DiskController::peekDSKDATR()
{
    // DSKDAT is a strobe register that cannot be accessed by the CPU
    return 0;
}

void
DiskController::pokeDSKLEN(uint16_t newDskLen)
{
    debug(DSKREG_DEBUG, "pokeDSKLEN(%X)\n", newDskLen);
    
    Drive *drive = getSelectedDrive(); 
    uint16_t oldDsklen = dsklen;

    // Initialize checksum (for debugging only)
    checksum = fnv_1a_init32();
    checkcnt = 0;

    // Remember the new value
    dsklen = newDskLen;
    
    // Disable DMA if the DMAEN bit (15) is zero
    if (!(newDskLen & 0x8000)) {
        debug(DSK_DEBUG, "dma = DRIVE_DMA_OFF\n");
        state = DRIVE_DMA_OFF;
        clearFifo();
    }
    
    // Enable DMA the DMAEN bit (bit 15) has been written twice.
    else if (oldDsklen & newDskLen & 0x8000) {

#ifdef ALIGN_DRIVE_HEAD
        if (drive) drive->head.offset = 0;
#endif
        
        // Check if the WRITE bit (bit 14) also has been written twice.
        if (oldDsklen & newDskLen & 0x4000) {
            
            debug(DSK_DEBUG, "dma = DRIVE_DMA_WRITE\n");
            state = DRIVE_DMA_WRITE;
            clearFifo();
            
        } else {
            
            // Check the WORDSYNC bit in the ADKCON register
            if (GET_BIT(paula->adkcon, 10)) {
                
                // Wait with reading until a sync mark has been found
                debug(DSK_DEBUG, "dma = DRIVE_DMA_READ_SYNC\n");
                state = DRIVE_DMA_WAIT;
                clearFifo();
                
            } else {
                
                // Start reading immediately
                debug(DSK_DEBUG, "dma = DRIVE_DMA_READ\n");
                state = DRIVE_DMA_READ;
                clearFifo();
            }
        }
    }
    
    // If the selected drive is a turbo drive, perform DMA immediately
    if (drive && drive->isTurboDrive()) performTurboDMA(drive);
}

void
DiskController::pokeDSKDAT(uint16_t value)
{
    debug(DSKREG_DEBUG, "pokeDSKDAT\n");

    // DSKDAT is a strobe register that cannot be accessed by the CPU.
}

uint16_t
DiskController::peekDSKBYTR()
{
    /* 15      DSKBYT     Indicates whether this register contains valid data.
     * 14      DMAON      Indicates whether disk DMA is actually enabled.
     * 13      DISKWRITE  Matches the WRITE bit in DSKLEN.
     * 12      WORDEQUAL  Indicates a match with the contents of DISKSYNC.
     * 11 - 8             Unused.
     *  7 - 0  DATA       Disk byte data.
     */
    
    // DATA
    uint16_t result = incoming;
    
    // DSKBYT
    assert(agnus->clock >= incomingCycle);
    if (agnus->clock - incomingCycle <= 7) SET_BIT(result, 15);
    
    // DMAON
    if (agnus->dskDMA() && state != DRIVE_DMA_OFF) SET_BIT(result, 14);

    // DSKWRITE
    if (dsklen & 0x4000) SET_BIT(result, 13);
    
    // WORDEQUAL
    if (syncFlag) SET_BIT(result, 12);

    debug(DSKREG_DEBUG, "peekDSKBYTR() = %X\n", result);
    return result;
}

void
DiskController::pokeDSKSYNC(uint16_t value)
{
    debug(DSKREG_DEBUG, "pokeDSKSYNC(%X)\n", value);
    // assert(false);
    dsksync = value;
}

uint8_t
DiskController::driveStatusFlags()
{
    uint8_t result = 0xFF;
    
    if (connected[0]) result &= df[0]->driveStatusFlags();
    if (connected[1]) result &= df[1]->driveStatusFlags();
    if (connected[2]) result &= df[2]->driveStatusFlags();
    if (connected[3]) result &= df[3]->driveStatusFlags();
    
    return result;
}

void
DiskController::PRBdidChange(uint8_t oldValue, uint8_t newValue)
{
    // debug("PRBdidChange: %X -> %X\n", oldValue, newValue);
    
    // Store a copy of the new value for reference.
    prb = newValue;
    
    selected = -1;
    
    // Iterate over all connected drives
    for (unsigned i = 0; i < 4; i++) { if (!connected[i]) continue;
        
        // Inform the drive and determine the selected one
        df[i]->PRBdidChange(oldValue, newValue);
        if (df[i]->isSelected()) {
            selected = i;
            acceleration = df[i]->getSpeed();
        }
    }
    
    // Schedule the first rotation event if at least one drive is spinning.
    if (!spinning()) {
        // debug("Cancelling DSK_SLOT events\n");
        agnus->cancel<DSK_SLOT>();
    }
    else if (!agnus->hasEvent<DSK_SLOT>()) {
        // debug("Activating DSK_SLOT events\n");
        agnus->scheduleRel<DSK_SLOT>(DMA_CYCLES(56), DSK_ROTATE);
    }
}

void
DiskController::serviceDiskEvent()
{
    if (fifoBuffering) {
        
        // Receive next byte from the selected drive.
        executeFifo();
    
        // Schedule next event.
        agnus->scheduleRel<DSK_SLOT>(DMA_CYCLES(56), DSK_ROTATE);
    }
}

void
DiskController::serviceDiskChangeEvent(EventID id, int driveNr)
{
    assert(driveNr >= 0 && driveNr <= 3);

    switch (id) {

        case DCH_INSERT:

            debug(DSK_DEBUG, "DCH_INSERT (df%d)\n", driveNr);

            assert(diskToInsert != NULL);
            df[driveNr]->insertDisk(diskToInsert);
            diskToInsert = NULL;
            break;

        case DCH_EJECT:

            debug(DSK_DEBUG, "DCH_EJECT (df%d)\n", driveNr);

            df[driveNr]->ejectDisk();
            break;

        default:
            assert(false);
    }

    agnus->cancel(DCH_SLOT);
}

void
DiskController::clearFifo()
{
    fifo = 0;
    fifoCount = 0;
}

uint8_t
DiskController::readFifo()
{
    // Don't call this function on an empty buffer.
    assert(fifoCount > 0);
    
    // Remove and return the oldest byte.
    fifoCount--;
    return (fifo >> (8 * fifoCount)) & 0xFF;
}

void
DiskController::writeFifo(uint8_t byte)
{
    assert(fifoCount <= 6);
    
    // Remove oldest word if the FIFO is full
    if (fifoCount == 6) fifoCount -= 2;
    
    // Add the new byte
    fifo = (fifo << 8) | byte;
    fifoCount++;
}

uint16_t
DiskController::readFifo16()
{
    assert(fifoHasWord());
    
    // Remove and return the oldest word.
    fifoCount -= 2;
    return (fifo >> (8 * fifoCount)) & 0xFFFF;
}

bool
DiskController::compareFifo(uint16_t word)
{
    return fifoHasWord() && (fifo & 0xFFFF) == word;
}

void
DiskController::executeFifo()
{
    // Only proceed if a drive is selected.
    Drive *drive = getSelectedDrive();
    if (drive == NULL) return;

    // Only proceed if the selected drive is not a turbo drive
    // if (drive->isTurboDrive()) return;
    
    switch (state) {
            
        case DRIVE_DMA_OFF:
            drive->rotate();
            break;
            
        case DRIVE_DMA_WAIT:
        case DRIVE_DMA_READ:
            
            // Read a byte from the drive and store a time stamp
            incoming = drive->readHead();
            incomingCycle = agnus->clock;
            
            // Write byte into the FIFO buffer.
            writeFifo(incoming);
            // if (dsksync) { debug("offset = %d incoming = %X\n", drive->head.offset, incoming); }

            // Check if we've reached a SYNC mark.
            if ((syncFlag = compareFifo(dsksync))) {
                
                // Trigger a word SYNC interrupt.
                debug(DSK_DEBUG, "SYNC IRQ (dsklen = %d)\n", dsklen);
                paula->raiseIrq(INT_DSKSYN);

                // Enable DMA if the controller was waiting for it.
                if (state == DRIVE_DMA_WAIT) {
                    debug(DSK_DEBUG, "DRIVE_DMA_SYNC_WAIT -> DRIVE_DMA_READ (%d)\n", drive->head.cylinder);
                    state = DRIVE_DMA_READ;
                    clearFifo();
                }
            }
            break;
            
        case DRIVE_DMA_WRITE:
        case DRIVE_DMA_FLUSH:
            
            if (fifoIsEmpty()) {
                
                // Switch off DMA is the last byte has been flushed out.
                if (state == DRIVE_DMA_FLUSH) state = DRIVE_DMA_OFF;
                
            } else {
                
                // Read the outgoing byte from the FIFO buffer.
                uint8_t outgoing = readFifo();
                
                // Write byte to disk.
                drive->writeHead(outgoing);
            }
            break;
    }
}

void
DiskController::performDMA()
{
    Drive *drive = getSelectedDrive();
    
    // Only proceed if a drive is selected.
    if (drive == NULL) return;
    
    // Only proceed if there are remaining bytes to read.
    if (!(dsklen & 0x3FFF)) return;
    
    // Only proceed if DMA is enabled.
    if (state != DRIVE_DMA_READ && state != DRIVE_DMA_WRITE) return;
    
    // Perform DMA
    switch (state) {
        
        case DRIVE_DMA_READ:
        performDMARead(drive);
        break;
        
        case DRIVE_DMA_WRITE:
        performDMAWrite(drive);
        break;
        
        default: assert(false);
    }
}

void
DiskController::performDMARead(Drive *drive)
{
    // Only proceed if the FIFO contains enough data.
    if (!fifoHasWord()) { return; }

    // Determine how many words we are supposed to transfer.
    uint32_t remaining = acceleration;
    
    do {
        // Read next word from the FIFO buffer.
        uint16_t word = readFifo16();
        
        // Write word into memory.
        agnus->doDiskDMA(word);
        // if (dsksync) { plainmsg("word = %x pos = %d dsklen = %d checkcnt = %d checksum = %x\n", word, drive->head.offset, dsklen & 0x3FFF, checkcnt, checksum); }

        // Compute checksum (for debugging).
        checksum = fnv_1a_it32(checksum, word);
        checkcnt++;

        // Finish up if this was the last word to transfer.
        if ((--dsklen & 0x3FFF) == 0) {

            paula->raiseIrq(INT_DSKBLK);
            state = DRIVE_DMA_OFF;
            plaindebug(DSK_CHECKSUM, "performRead: checkcnt = %d checksum = %X\n", checkcnt, checksum);
            return;
        }
        
        // If the loop repeats, do what the event handler would do in between.
        if (--remaining) {
            executeFifo();
            executeFifo();
            assert(fifoHasWord());
        }
        
    } while (remaining);
}

void
DiskController::performDMAWrite(Drive *drive)
{
    // Only proceed if the FIFO has enough free space.
    if (!fifoCanStoreWord()) return;
    
    // Determine how many words we are supposed to transfer.
    uint32_t remaining = acceleration;
    
    do {
        // Read next word from memory.
        uint16_t word = agnus->doDiskDMA(); // dmaRead();
        checksum = fnv_1a_it32(checksum, word);
        checkcnt++;
        // plaindebug("%d: %X (%X)\n", dsklen & 0x3FFF, word, dcheck);
        
        // Write word into FIFO buffer.
        assert(fifoCount <= 4);
        writeFifo(HI_BYTE(word));
        writeFifo(LO_BYTE(word));

        // Finish up if this was the last word to transfer.
        if ((--dsklen & 0x3FFF) == 0) {

            paula->raiseIrq(INT_DSKBLK);

            /* The timing-accurate approach: Set state to DRIVE_DMA_FLUSH.
             * The event handler recognises this state and switched to
             * DRIVE_DMA_OFF once the FIFO has been emptied.
             */
            
            // state = DRIVE_DMA_FLUSH;
            
            /* I'm unsure of the timing-accurate approach works properly,
             * because the disk IRQ would be triggered before the last byte
             * has been written.
             * Hence, we play safe here and flush the FIFO immediately.
             */
            while (!fifoIsEmpty()) {
                drive->writeHead(readFifo());
            }
            state = DRIVE_DMA_OFF;
            
            debug(DSK_CHECKSUM, "performWrite: checkcnt = %d checksum = %X\n", checkcnt, checksum);
            return;
        }
        
        // If the loop repeats, do what the event handler would do in between.
        if (--remaining) {
            executeFifo();
            executeFifo();
            assert(fifoCanStoreWord());
        }
        
    } while (remaining);
}

void
DiskController::performSimpleDMA()
{
    Drive *drive = getSelectedDrive();
    
    // Only proceed if a drive is selected.
    if (drive == NULL) return;
    
    // Only proceed if there are remaining bytes to read.
    if (!(dsklen & 0x3FFF)) return;

    // Only proceed if DMA is enabled.
    if (state != DRIVE_DMA_READ && state != DRIVE_DMA_WRITE) return;
    
    // Perform DMA
    switch (state) {

        case DRIVE_DMA_READ:
        performSimpleDMARead(drive);
        break;

        case DRIVE_DMA_WRITE:
        performSimpleDMAWrite(drive);
        break;
        
        default: assert(false);
    }
}

void
DiskController::performSimpleDMARead(Drive *drive)
{
    for (unsigned i = 0; i < acceleration; i++) {
        
        // Read word from disk.
        uint16_t word = drive->readHead16();
        
        // Write word into memory.
        agnus->doDiskDMA(word);

        // Compute checksum (for debugging).
        checksum = fnv_1a_it32(checksum, word);
        checkcnt++;

        if ((--dsklen & 0x3FFF) == 0) {

            paula->raiseIrq(INT_DSKBLK);
            state = DRIVE_DMA_OFF;
            debug(DSK_DEBUG, "doSimpleDMARead: checkcnt = %d checksum = %X\n", checkcnt, checksum);
            return;
        }
    }
}

void
DiskController::performSimpleDMAWrite(Drive *drive)
{
    // debug("Writing %d words to disk\n", dsklen & 0x3FFF);
    
    for (unsigned i = 0; i < acceleration; i++) {
        
        // Read word from memory
        uint16_t word = agnus->doDiskDMA();
        
        // Compute checksum (for debugging)
        checksum = fnv_1a_it32(checksum, word);
        checkcnt++;

        // Write word to disk
        drive->writeHead16(word);
        
        if ((--dsklen & 0x3FFF) == 0) {
            
            paula->raiseIrq(INT_DSKBLK);
            state = DRIVE_DMA_OFF;
            debug(DSK_DEBUG, "doSimpleDMAWrite: checkcnt = %d checksum = %X\n", checkcnt, checksum);
            return;
        }
    }
}

void
DiskController::performTurboDMA(Drive *drive)
{
    // Only proceed if there are remaining bytes to read.
    if ((dsklen & 0x3FFF) == 0) return;
    
    // Perform action depending on DMA state
    switch (state) {

        case DRIVE_DMA_WAIT:

            drive->findSyncMark();
            // fallthrough

        case DRIVE_DMA_READ:
            
            performTurboRead(drive);
            break;
            
        case DRIVE_DMA_WRITE:
            
            performTurboWrite(drive);
            break;
            
        default:
            return;
    }
    
    // Trigger disk interrupt with some delay
    paula->raiseIrq(INT_DSKBLK, DMA_CYCLES(512));
    state = DRIVE_DMA_OFF;
}

void
DiskController::performTurboRead(Drive *drive)
{
    debug(DSK_CHECKSUM, "Turbo-reading %d words from disk (offset = %d).\n", dsklen & 0x3FFF, drive->head.offset);

    for (unsigned i = 0; i < (dsklen & 0x3FFF); i++) {
        
        // Read word from disk.
        uint16_t word = drive->readHead16();
        
        // Write word into memory.
        mem->pokeChip16(agnus->dskpt, word);
        INC_DMAPTR(agnus->dskpt);
        
        // Compute checksum (for debugging)
        checksum = fnv_1a_it32(checksum, word);
        checkcnt++;
    }
        
    debug(DSK_CHECKSUM, "Turbo read %s: cyl: %d side: %d offset: %d checkcnt = %d checksum = %X\n", drive->getDescription(), drive->head.cylinder, drive->head.side, drive->head.offset, checkcnt, checksum);
}

void
DiskController::performTurboWrite(Drive *drive)
{
    plaindebug(DSK_CHECKSUM, "Turbo-writing %d words to disk.\n", dsklen & 0x3FFF);
    
    for (unsigned i = 0; i < (dsklen & 0x3FFF); i++) {
        
        // Read word from memory
        uint16_t word = mem->peekChip16(agnus->dskpt);
        INC_DMAPTR(agnus->dskpt);
        
        // Compute checksum (for debugging)
        checksum = fnv_1a_it32(checksum, word);
        checkcnt++;

        // Write word to disk
        drive->writeHead16(word);
    }
    
    plaindebug(DSK_CHECKSUM, "Turbo write %s: checkcnt = %d checksum = %X\n", drive->getDescription(), checkcnt, checksum);
}

