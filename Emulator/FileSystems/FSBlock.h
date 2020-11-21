// -----------------------------------------------------------------------------
// This file is part of vAmiga
//
// Copyright (C) Dirk W. Hoffmann. www.dirkwhoffmann.de
// Licensed under the GNU General Public License v3
//
// See https://www.gnu.org for license information
// -----------------------------------------------------------------------------

#ifndef _FS_BLOCKS_H
#define _FS_BLOCKS_H

#include "Utils.h"
#include "FSTypes.h"
#include "FSObjects.h"

struct FSBlock {
    
    // The volume this block belongs to
    class FSVolume &volume;
    
    // The sector number of this block
    u32 nr;
    
    // Outcome of the last integrity check (0 = OK, n = n-th corrupted block)
    u32 corrupted = 0;
    
    // The actual block data
    u8 *data = nullptr;

    
    //
    // Constants and static methods
    //

    // Search limit to avoid infinite list walks
    static const long searchLimit = 255;
        
        
    //
    // Constructing and destructing
    //
    
    FSBlock(FSVolume &ref, u32 nr) : volume(ref) { this->nr = nr; }
    virtual ~FSBlock() { }

    static FSBlock *makeWithType(FSVolume &ref, u32 nr, FSBlockType type);
    
    
    //
    // Querying block properties
    //

    // Returns the type of this block
    virtual FSBlockType type() = 0; 

    // Returns the role of a certain byte in this block
    virtual FSItemType itemType(u32 byte) { return FSI_UNKNOWN; }
    
    // Returns the type and subtype identifiers of this block
    virtual u32 typeID();
    virtual u32 subtypeID();

    // Returns the name or path of this block
    char *assemblePath();

    
    //
    // Integrity checking
    //

    // Scans all long words in this block and returns the number of errors
    unsigned check(bool strict);

    // Checks the integrity of a certain byte in this block
    virtual FSError check(u32 pos, u8 *expected, bool strict) { return FS_OK; }
        
    
    //
    // Reading and writing block data
    //

    // Reads or writes a long word in Big Endian format
    static u32 read32(const u8 *p);
    static void write32(u8 *p, u32 value);
    static void inc32(u8 *p) { write32(p, read32(p) + 1); }
    static void dec32(u8 *p) { write32(p, read32(p) - 1); }

    // Computes the address of a long word inside the block
    u8 *addr(int nr); 
    
    // Reads, writes, or modifies the n-th long word
    u32 get32(i32 n) { return read32(addr(n)); }
    void set32(i32 n, u32 val) { write32(addr(n), val); }
    void inc32(i32 n) { inc32(addr(n)); }
    void dec32(i32 n) { dec32(addr(n)); }

    // Returns the location of the checksum inside this block
    virtual u32 checksumLocation() { return (u32)-1; }
    // bool hasChecksum() { return checksumLocation() != (u32)-1; }
    
    // Computes a checksum for this block
    u32 checksum();
    
    // Updates the checksum in this block
    void updateChecksum();
    
    
    //
    // Debugging
    //
    
    // Prints the full path of this block
    void printPath();

    // Prints some debug information for this block
    virtual void dump() { };
    virtual void dumpData();

    
    //
    // Importing and exporting
    //
    
public:
    
    // Imports this block from a buffer (bsize must match the volume block size)
    virtual void importBlock(const u8 *src, size_t bsize);

    // Exports this block to a buffer (bsize must match the volume block size)
    virtual void exportBlock(u8 *dst, size_t bsize);

private:
    
    // Updates the checksum for this block (called prior to exporting)
    // virtual void updateChecksum() { }
    
                
    //
    // Geting and setting names and comments
    //
    
public:
    
    virtual FSName getName() { return FSName(""); }
    virtual void setName(FSName name) { }
    virtual bool isNamed(FSName &other) { return false; }

    virtual FSComment getComment() { return FSComment(""); }
    virtual void setComment(FSComment name) { }

    
    //
    // Getting and settting date and time
    //
    
    virtual FSTime getCreationDate() { return FSTime((time_t)0); }
    virtual void setCreationDate(FSTime t) { }

    virtual FSTime getModificationDate() { return FSTime((time_t)0); }
    virtual void setModificationDate(FSTime t) { }
    
    
    //
    // Getting and setting file properties
    //
    
    virtual u32 getProtectionBits() { return 0; }
    virtual void setProtectionBits(u32 val) { }

    virtual u32 getFileSize() { return 0; }
    virtual void setFileSize(u32 val) { }

    
    //
    // Chaining blocks
    //

    // Link to the parent directory block
    virtual u32 getParentDirRef() { return 0; }
    virtual void setParentDirRef(u32 ref) { }
    FSBlock *getParentBlock();
    
    // Link to the file header block
    virtual u32 getFileHeaderRef() { return 0; }
    virtual void setFileHeaderRef(u32 ref) { }
    struct FSFileHeaderBlock *getFileHeaderBlock();

    // Link to the next block with the same hash
    virtual u32 getNextHashRef() { return 0; }
    virtual void setNextHashRef(u32 ref) { }
    FSBlock *getNextHashBlock();

    // Link to the next extension block
    virtual u32 getNextListBlockRef() { return 0; }
    virtual void setNextListBlockRef(u32 ref) { }
    struct FSFileListBlock *getNextExtensionBlock();

    // Link to the first data block
    virtual u32 getFirstDataBlockRef() { return 0; }
    virtual void setFirstDataBlockRef(u32 ref) { }
    struct FSDataBlock *getFirstDataBlock();

    // Link to the next data block
    virtual u32 getNextDataBlockRef() { return 0; }
    virtual void setNextDataBlockRef(u32 ref) { }
    struct FSDataBlock *getNextDataBlock();

    
    //
    // Working with hash tables
    //
    
    // Returns the hash table size
    virtual u32 hashTableSize() { return 0; }

    // Returns a hash value for this block
    virtual u32 hashValue() { return 0; }

    // Looks up an item in the hash table
    u32 hashLookup(u32 nr);
    FSBlock *hashLookup(FSName name);

    // Adds a reference to the hash table
    void addToHashTable(u32 ref);
    
    // Checks the integrity of the hash table (DEPRECATED)
    // bool checkHashTable(bool verbose);

    // Checks the integrity of a hash table entry
    // FSError checkHashTableItem(u32 item);

    // Dumps the contents of the hash table for debugging
    void dumpHashTable();

    
    //
    // Working with data blocks
    //
    
    // Returns the maximum number of storable data block references
    u32 getMaxDataBlockRefs();

    // Gets or sets the number of data block references in this block
    virtual u32 getNumDataBlockRefs() { return 0; }
    virtual void setNumDataBlockRefs(u32 val) { }
    virtual void incNumDataBlockRefs() { }

    // Adds a data block reference to this block
    virtual bool addDataBlockRef(u32 first, u32 ref) { return false; }

    // Adds data bytes to this block
    virtual size_t addData(const u8 *buffer, size_t size) { return 0; }
};

//
// Convenience macros used inside the check() methods
//

typedef FSBlock* BlockPtr;

#define EXPECT_VALUE(val,exp) \
if ((val) != (exp)) { *expected = (exp); return FS_UNEXPECTED_VALUE; }
#define EXPECT_00(val) \
if ((val) != 0x00) return FS_EXPECTED_00;
#define EXPECT_01(val) \
if ((val) != 0x01) return FS_EXPECTED_01;
#define EXPECT_02(val) \
if ((val) != 0x02) return FS_EXPECTED_02;
#define EXPECT_03(val) \
if ((val) != 0x03) return FS_EXPECTED_03;
#define EXPECT_08(val) \
if ((val) != 0x08) return FS_EXPECTED_08;
#define EXPECT_10(val) \
if ((val) != 0x10) return FS_EXPECTED_10;
#define EXPECT_FD(val) \
if ((val) != 0xFD) return FS_EXPECTED_FD;
#define EXPECT_FF(val) \
if ((val) != 0xFF) return FS_EXPECTED_FF;
#define EXPECT_DOS_REVISION(val) \
if (!isFSVolumeType(val)) return FS_EXPECTED_DOS_REVISION;
#define EXPECT_REF(val) \
if (!volume.block(val)) return FS_EXPECTED_REF;
#define EXPECT_SELFREF(val) \
if ((val) != nr) return FS_EXPECTED_SELFREF;
#define EXPECT_FILEHEADER_REF(val) \
if (FSError e = volume.checkBlockType(val, FS_FILEHEADER_BLOCK); e != FS_OK) return e;
#define EXPECT_HASH_REF(val) \
if (FSError e = volume.checkBlockType(val, FS_FILEHEADER_BLOCK, FS_USERDIR_BLOCK); e != FS_OK) return e;
#define EXPECT_OPTIONAL_HASH_REF(val) \
if (val) { EXPECT_HASH_REF(val) }
#define EXPECT_PARENT_DIR_REF(val) \
if (FSError e = volume.checkBlockType(val, FS_ROOT_BLOCK, FS_USERDIR_BLOCK); e != FS_OK) return e;
#define EXPECT_FILELIST_REF(val) \
if (FSError e = volume.checkBlockType(val, FS_FILELIST_BLOCK); e != FS_OK) return e;
#define EXPECT_OPTIONAL_FILELIST_REF(val) \
if (val) { EXPECT_FILELIST_REF(val) }
#define EXPECT_DATABLOCK_REF(val) \
if (FSError e = volume.checkBlockType(val, FS_DATA_BLOCK); e != FS_OK) return e;
#define EXPECT_OPTIONAL_DATABLOCK_REF(val) \
if (val) { EXPECT_DATABLOCK_REF(val) }
#define EXPECT_DATABLOCK_NUMBER(val) \
if ((val) == 0) return FS_EXPECTED_DATABLOCK_NR;
#define EXPECT_HASHTABLE_SIZE(val) \
if ((val) != 72) return FS_INVALID_HASHTABLE_SIZE;
#define EXPECT_CHECKSUM(val) \
if ((val) != checksum()) return FS_INVALID_CHECKSUM;
#define EXPECT_RANGE(val,min,max) \
if ((val) < min || (val) > max) return FS_OUT_OF_RANGE;

#define EXPECT_00000001(val,offset) \
if (offset == 0 && BYTE3(val) != 0x00) return FS_EXPECTED_00; \
if (offset == 1 && BYTE2(val) != 0x00) return FS_EXPECTED_00; \
if (offset == 2 && BYTE1(val) != 0x00) return FS_EXPECTED_00; \
if (offset == 3 && BYTE0(val) != 0x01) return FS_EXPECTED_01;

#define EXPECT_00000002(val,offset) \
if (offset == 0 && BYTE3(val) != 0x00) return FS_EXPECTED_00; \
if (offset == 1 && BYTE2(val) != 0x00) return FS_EXPECTED_00; \
if (offset == 2 && BYTE1(val) != 0x00) return FS_EXPECTED_00; \
if (offset == 3 && BYTE0(val) != 0x02) return FS_EXPECTED_02;

#define EXPECT_00000008(val,offset) \
if (offset == 0 && BYTE3(val) != 0x00) return FS_EXPECTED_00; \
if (offset == 1 && BYTE2(val) != 0x00) return FS_EXPECTED_00; \
if (offset == 2 && BYTE1(val) != 0x00) return FS_EXPECTED_00; \
if (offset == 3 && BYTE0(val) != 0x08) return FS_EXPECTED_08;

#define EXPECT_00000010(val,offset) \
if (offset == 0 && BYTE3(val) != 0x00) return FS_EXPECTED_00; \
if (offset == 1 && BYTE2(val) != 0x00) return FS_EXPECTED_00; \
if (offset == 2 && BYTE1(val) != 0x00) return FS_EXPECTED_00; \
if (offset == 3 && BYTE0(val) != 0x10) return FS_EXPECTED_10;

#define EXPECT_FFFFFFFD(val,offset) \
if (offset == 0 && BYTE3(val) != 0xFF) return FS_EXPECTED_FF; \
if (offset == 1 && BYTE2(val) != 0xFF) return FS_EXPECTED_FF; \
if (offset == 2 && BYTE1(val) != 0xFF) return FS_EXPECTED_FF; \
if (offset == 3 && BYTE0(val) != 0xFD) return FS_EXPECTED_FD;

#endif
