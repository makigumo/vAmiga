// -----------------------------------------------------------------------------
// This file is part of vAmiga
//
// Copyright (C) Dirk W. Hoffmann. www.dirkwhoffmann.de
// Licensed under the GNU General Public License v3
//
// See https://www.gnu.org for license information
// -----------------------------------------------------------------------------

#pragma once

#include "OSDebuggerTypes.h"
#include "SubComponent.h"
#include "Constants.h"

class OSDebugger : public SubComponent {
    
private:
    
    //
    // Constructing
    //
    
public:
    
    using SubComponent::SubComponent;
    
    
    //
    // Methods from AmigaObject
    //
    
private:
    
    const char *getDescription() const override { return "OSDebugger"; }
    void _dump(dump::Category category, std::ostream& os) const override { }

    
    //
    // Methods from AmigaComponent
    //
        
private:
    
    void _reset(bool hard) override { };
    
    
    //
    // Serializing
    //
    
    isize _size() override { return 0; }
    u64 _checksum() override { return 0; }
    isize _load(const u8 *buffer) override { return 0; }
    isize _save(u8 *buffer) override { return 0; }
    
    
    //
    // Translating enumeration types to strings
    //

private:
    
    string toString(os::LnType value) const;
    string toString(os::TState value) const;
    string toString(os::SigFlags value) const;
    string toString(os::TFlags value) const;

    void append(string &str, const char *cstr) const;
    
    
    //
    // Extracting elementary data types from Amiga memory
    //
    
public:
            
    void read(u32 addr, u8 *result) const;
    void read(u32 addr, u16 *result) const;
    void read(u32 addr, u32 *result) const;
    void read(u32 addr, i8 *result) const { read(addr, (u8 *)result); }
    void read(u32 addr, i16 *result) const { read(addr, (u16 *)result); }
    void read(u32 addr, i32 *result) const { read(addr, (u32 *)result); }
    void read(u32 addr, string &result) const;
    void read(u32 addr, string &result, isize limit) const;

    
    //
    // Extracting basic structures from Amiga memory
    //

public:
    
    void read(u32 addr, os::Node *result) const;
    void read(u32 addr, os::Library *result) const;
    void read(u32 addr, os::IntVector *result) const;
    void read(u32 addr, os::List *result) const;
    void read(u32 addr, os::MinList *result) const;
    void read(u32 addr, os::SoftIntList *result) const;
    void read(u32 addr, os::Task *result) const;
    void read(u32 addr, os::MsgPort *result) const;
    void read(u32 addr, os::Process *result) const;
    void read(u32 addr, os::ExecBase *result) const;
    void read(os::ExecBase *result) const;

    
    //
    // Extracting nested structures from Amiga memory
    //

public:
    
    void read(u32 addr, std::vector <os::Task> &result) const;
    void read(u32 addr, std::vector <os::Library> &result) const;
    void read(u32 addr, os::SegList &result) const;
    void read(u32 addr, std::vector <os::SegList> &result) const;
    
    
    //
    // Printing system information
    //

public:
    
    void dumpExecBase(std::ostream& s) const;
    void dumpInterrupts(std::ostream& s) const;
    void dumpLibraries(std::ostream& s) const;
    void dumpLibrary(std::ostream& s, const os::Library &lib) const;
    void dumpTasks(std::ostream& s) const;
    void dumpTask(std::ostream& s, const os::Task &task) const;
    void dumpProcess(std::ostream& s, const os::Process &process) const;

};
