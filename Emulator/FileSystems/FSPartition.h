// -----------------------------------------------------------------------------
// This file is part of vAmiga
//
// Copyright (C) Dirk W. Hoffmann. www.dirkwhoffmann.de
// Licensed under the GNU General Public License v3
//
// See https://www.gnu.org for license information
// -----------------------------------------------------------------------------

#pragma once

#include "FSTypes.h"
#include "FSDescriptors.h"

struct FSPartition : AmigaObject {
    
    // The device this partition is part of
    class FSDevice &dev;
                
    // Location of the root block
    Block rootBlock = 0;
    
    // Location of the bitmap blocks and extended bitmap blocks
    std::vector<Block> bmBlocks;
    std::vector<Block> bmExtBlocks;

    
    //
    // Initializing
    //
    
    FSPartition(FSDevice &ref) : dev(ref) { }
    FSPartition(FSDevice &ref, FSDeviceDescriptor &layout);
    
    
    //
    // Methods from AmigaObject
    //
    
private:
    
    const char *getDescription() const override { return "FSPartition"; }
    void _dump(dump::Category category, std::ostream& os) const override { };
    
        
    //
    // Creating and deleting blocks
    //
    
public:
    
    // Returns the number of required blocks to store a file of certain size
    isize requiredDataBlocks(isize fileSize) const;
    isize requiredFileListBlocks(isize fileSize) const;
    isize requiredBlocks(isize fileSize) const;

    // Seeks a free block and marks it as allocated
    Block allocateBlock();
    Block allocateBlockAbove(Block nr);
    Block allocateBlockBelow(Block nr);

    // Deallocates a block
    void deallocateBlock(Block nr);

    // Adds a new block of a certain kind
    Block addFileListBlock(Block head, Block prev);
    Block addDataBlock(isize count, Block head, Block prev);
    
    // Creates a new block of a certain kind
    FSBlock *newUserDirBlock(const string &name);
    FSBlock *newFileHeaderBlock(const string &name);
    
    
    //
    // Working with the block allocation bitmap
    //

    // Returns the bitmap block storing the allocation bit for a certain block
    FSBlock *bmBlockForBlock(Block nr);

    // Checks if a block is marked as free in the allocation bitmap
    bool isFree(Block nr) const;
    
    // Marks a block as allocated or free
    void markAsAllocated(Block nr) { setAllocationBit(nr, 0); }
    void markAsFree(Block nr) { setAllocationBit(nr, 1); }
    void setAllocationBit(Block nr, bool value);

    
private:
    
    // Locates the allocation bit for a certain block
    FSBlock *locateAllocationBit(Block nr, isize *byte, isize *bit) const;
};

typedef FSPartition* FSPartitionPtr;
