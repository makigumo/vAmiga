// -----------------------------------------------------------------------------
// This file is part of vAmiga
//
// Copyright (C) Dirk W. Hoffmann. www.dirkwhoffmann.de
// Licensed under the GNU General Public License v3
//
// See https://www.gnu.org for license information
// -----------------------------------------------------------------------------

// This file must conform to standard ANSI-C to be compatible with Swift.

#ifndef _DRIVE_T_INC
#define _DRIVE_T_INC

//
// Enumerations
//

typedef enum : long
{
    DRIVE_35_DD,
    DRIVE_35_DD_PC,
    DRIVE_35_HD,
    DRIVE_35_HD_PC,
    DRIVE_525_SD
}
DriveType;

inline bool isDriveType(long value)
{
    return value >= DRIVE_35_DD && value <= DRIVE_525_SD;
}

inline const char *driveTypeName(DriveType type)
{
    assert(isDriveType(type));
    
    switch (type) {
        case DRIVE_35_DD:    return "Drive 3.5\" DD";
        case DRIVE_35_DD_PC: return "Drive 3.5\" DD (PC)";
        case DRIVE_35_HD:    return "Drive 3.5\" HD";
        case DRIVE_35_HD_PC: return "Drive 3.5\" HD (PC)";
        case DRIVE_525_SD:   return "Drive 5.25\" SD";
        default:             return "???";
    }
}

//
// Structures
//

typedef struct
{
     u8 side;
     u8 cylinder;
     u16 offset;
 }
DriveHead;

typedef struct
{
    /* Drive type.
     * At the moment, we only support standard 3.5" DD drives.
     */
     DriveType type;

    /* Acceleration factor.
     * This value equals the number of words that get transfered into memory
     * during a single disk DMA cycle. This value must be 1 to emulate a real
     * Amiga. If it set to, e.g., 2, the drive loads twice as fast.
     * A negative value indicates a turbo drive for which the exact value of
     * the acceleration factor has no meaning.
     */
    int16_t speed;
}
DriveConfig;

inline bool isValidDriveSpeed(int16_t speed)
{
    switch (speed) {
        case -1: case 1: case 2: case 4: case 8: return true;
    }
    return false;
}

typedef struct
{
    DriveHead head;
    bool write;
    bool motor;
}
DriveInfo;

#endif
