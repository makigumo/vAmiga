// -----------------------------------------------------------------------------
// This file is part of vAmiga
//
// Copyright (C) Dirk W. Hoffmann. www.dirkwhoffmann.de
// Licensed under the GNU General Public License v3
//
// See https://www.gnu.org for license information
// -----------------------------------------------------------------------------

#ifndef _DMS_FILE_H
#define _DMS_FILE_H

#include "ADFFile.h"

class DMSFile : public DiskFile {
    
public:
    
    ADFFile *adf = nullptr;
    
    
    //
    // Class methods
    //
    
    // Returns true iff the provided buffer contains a DMS file
    static bool isDMSBuffer(const u8 *buffer, size_t length);
    
    // Returns true iff if the provided path points to a DMS file
    static bool isDMSFile(const char *path);
    
    
    //
    // Initializing
    //
    
    DMSFile();
    
    const char *getDescription() const override { return "DMS"; }
        
    
    //
    // Methods from AmigaFile
    //
    
    FileType fileType() override { return FILETYPE_DMS; }
    u64 fnv() override { return adf->fnv(); }
    bool matchingBuffer(const u8 *buffer, size_t length) override {
        return isDMSBuffer(buffer, length); }
    bool matchingFile(const char *path) override { return isDMSFile(path); }
    bool readFromBuffer(const u8 *buffer, size_t length, FileError *error = nullptr) override;
    
    
    //
    // Methods from DiskFile
    //
    
    FSVolumeType getDos() const override { return adf->getDos(); }
    void setDos(FSVolumeType dos) override { adf->setDos(dos); }
    DiskDiameter getDiskDiameter() const override { return adf->getDiskDiameter(); }
    DiskDensity getDiskDensity() const override { return adf->getDiskDensity(); }
    long numSides() const override { return adf->numSides(); }
    long numCyls() const override { return adf->numCyls(); }
    long numSectors() const override { return adf->numSectors(); }
    BootBlockType bootBlockType() const override { return adf->bootBlockType(); }
    const char *bootBlockName() const override { return adf->bootBlockName(); }
    void readSector(u8 *target, long s) override { return adf->readSector(target, s); }
    void readSector(u8 *target, long t, long s) override { return adf->readSector(target, t, s); }
    bool encodeDisk(class Disk *disk) override { return adf->encodeDisk(disk); }
};

#endif
