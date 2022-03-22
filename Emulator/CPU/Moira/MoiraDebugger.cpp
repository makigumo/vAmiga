// -----------------------------------------------------------------------------
// This file is part of Moira - A Motorola 68k emulator
//
// Copyright (C) Dirk W. Hoffmann. www.dirkwhoffmann.de
// Licensed under the GNU General Public License v3
//
// See https://www.gnu.org for license information
// -----------------------------------------------------------------------------

#include "Moira.h"

#include <cstring>
#include <cstdio>

namespace moira {

//
// Guard
//

bool
Guard::eval(u32 addr, Size S)
{
    if (this->addr >= addr && this->addr < addr + S && this->enabled) {
        
        if (!ignore) return true;
        ignore--;
    }
    return false;
}


//
// Guards
//

Guards::~Guards()
{
    assert(guards);
    delete [] guards;
}

Guard *
Guards::guardNr(long nr)const
{
    return nr < count ? &guards[nr] : nullptr;
}

Guard *
Guards::guardAt(u32 addr) const
{
    for (int i = 0; i < count; i++) {
        if (guards[i].addr == addr) return &guards[i];
    }

    return nullptr;
}

std::optional<u32>
Guards::guardAddr(long nr) const
{
    if (nr < count) return guards[nr].addr;
    return { };
}

void
Guards::setAt(u32 addr)
{
    if (isSetAt(addr)) return;

    if (count >= capacity) {

        Guard *newguards = new Guard[2 * capacity];
        for (long i = 0; i < capacity; i++) newguards[i] = guards[i];
        delete [] guards;
        guards = newguards;
        capacity *= 2;
    }

    guards[count++].addr = addr;
    setNeedsCheck(true);
}

void
Guards::remove(long nr)
{
    if (nr < count) removeAt(guards[nr].addr);
}

void
Guards::removeAt(u32 addr)
{
    for (int i = 0; i < count; i++) {

        if (guards[i].addr == addr) {

            for (int j = i; j + 1 < count; j++) guards[j] = guards[j + 1];
            count--;
            break;
        }
    }
    setNeedsCheck(count != 0);
}

void
Guards::replace(long nr, u32 addr)
{
    if (nr >= count || isSetAt(addr)) return;
    
    guards[nr].addr = addr;
}

bool
Guards::isEnabled(long nr) const
{
    Guard *guard = guardNr(nr);
    return guard != nullptr && guard->enabled;
}

bool
Guards::isEnabledAt(u32 addr) const
{
    Guard *guard = guardAt(addr);
    return guard != nullptr && guard->enabled;
}

bool
Guards::isDisabled(long nr) const
{
    Guard *guard = guardNr(nr);
    return guard != nullptr && !guard->enabled;
}

bool
Guards::isDisabledAt(u32 addr) const
{
    Guard *guard = guardAt(addr);
    return guard != nullptr && !guard->enabled;
}

void
Guards::setEnable(long nr, bool val)
{
    Guard *guard = guardNr(nr);
    if (guard) guard->enabled = val;
}

void
Guards::setEnableAt(u32 addr, bool val)
{
    Guard *guard = guardAt(addr);
    if (guard) guard->enabled = val;
}

void
Guards::ignore(long nr, long count)
{
    Guard *guard = guardNr(nr);
    if (guard) guard->ignore = count;
}

bool
Guards::eval(u32 addr, Size S)
{
    for (int i = 0; i < count; i++) {
        
        if (guards[i].eval(addr, S)) {
            
            hit = guards[i];
            return true;
        }
    }
    return false;
}

void
Breakpoints::setNeedsCheck(bool value)
{
    if (value) {
        moira.flags |= Moira::CPU_CHECK_BP;
    } else {
        moira.flags &= ~Moira::CPU_CHECK_BP;
    }
}

void
Watchpoints::setNeedsCheck(bool value)
{
    if (value) {
        moira.flags |= Moira::CPU_CHECK_WP;
    } else {
        moira.flags &= ~Moira::CPU_CHECK_WP;
    }
}

void
Catchpoints::setNeedsCheck(bool value)
{
    if (value) {
        moira.flags |= Moira::CPU_CHECK_CP;
    } else {
        moira.flags &= ~Moira::CPU_CHECK_CP;
    }
}

void
Debugger::reset()
{
    breakpoints.setNeedsCheck(breakpoints.elements() != 0);
    watchpoints.setNeedsCheck(watchpoints.elements() != 0);
}

void
Debugger::stepInto()
{
    softStop = UINT64_MAX;
    breakpoints.setNeedsCheck(true);
}

void
Debugger::stepOver()
{
    char tmp[64];
    softStop = moira.getPC() + moira.disassemble(moira.getPC(), tmp);
    breakpoints.setNeedsCheck(true);
}

bool
Debugger::softstopMatches(u32 addr)
{
    if (addr != softStop && softStop != UINT64_MAX) return false;
    
    // Soft breakpoints are deleted when reached
    softStop = UINT64_MAX - 1;
    breakpoints.setNeedsCheck(breakpoints.elements() != 0);
    breakpointPC = -1;
    
    return true;
}

bool
Debugger::breakpointMatches(u32 addr)
{
    if (!breakpoints.eval(addr)) return false;
        
    breakpointPC = moira.reg.pc;
    return true;
}

bool
Debugger::watchpointMatches(u32 addr, Size S)
{
    if (!watchpoints.eval(addr, S)) return false;
    
    watchpointPC = moira.reg.pc0;
    return true;
}

bool
Debugger::catchpointMatches(u32 vectorNr)
{
    if (!catchpoints.eval(vectorNr)) return false;
    return true;
}

void
Debugger::enableLogging()
{
    moira.flags |= Moira::CPU_LOG_INSTRUCTION;
}

void
Debugger::disableLogging()
{
    moira.flags &= ~Moira::CPU_LOG_INSTRUCTION;
}

int
Debugger::loggedInstructions()
{
    return logCnt < logBufferCapacity ? (int)logCnt : logBufferCapacity;
}

void
Debugger::logInstruction()
{
    logBuffer[logCnt % logBufferCapacity] = moira.reg;
    logCnt++;
}

Registers &
Debugger::logEntryRel(int n)
{
    assert(n < loggedInstructions());
    return logBuffer[(logCnt - 1 - n) % logBufferCapacity];
}

Registers &
Debugger::logEntryAbs(int n)
{
    assert(n < loggedInstructions());
    return logEntryRel(loggedInstructions() - n - 1);
}

string
Debugger::vectorName(u8 vectorNr)
{
    if (vectorNr >= 12 && vectorNr <= 14) {
        return "Reserved";
    }
    if (vectorNr >= 16 && vectorNr <= 23) {
        return "Reserved";
    }
    if (vectorNr >= 48 && vectorNr <= 63) {
        return "Reserved";
    }
    if (vectorNr >= 25 && vectorNr <= 31) {
        return "Level " + std::to_string(vectorNr - 24) + " interrupt";
    }
    if (vectorNr >= 32 && vectorNr <= 47) {
        return "Trap #" + std::to_string(vectorNr - 32);
    }
    if (vectorNr >= 64 && vectorNr <= 255) {
        return "User interrupt vector";
    }
    switch (vectorNr) {
    
        case 0:     return "Reset SP";
        case 1:     return "Reset PC";
        case 2:     return "Bus error";
        case 3:     return "Address error";
        case 4:     return "Illegal instruction";
        case 5:     return "Division by zero";
        case 6:     return "CHK instruction";
        case 7:     return "TRAPV instruction";
        case 8:     return "Privilege violation";
        case 9:     return "Trace";
        case 10:    return "Line A instruction";
        case 11:    return "Line F instruction";
        case 15:    return "Uninitialized IRQ vector";
        case 24:    return "Spurious interrupt";

        default:
            fatalError;
    }
}

void
Debugger::jump(u32 addr)
{
    moira.reg.pc = addr;
    moira.fullPrefetch<POLLIPL>();
}

}
