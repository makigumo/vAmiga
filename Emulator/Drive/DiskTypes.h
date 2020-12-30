// -----------------------------------------------------------------------------
// This file is part of vAmiga
//
// Copyright (C) Dirk W. Hoffmann. www.dirkwhoffmann.de
// Licensed under the GNU General Public License v3
//
// See https://www.gnu.org for license information
// -----------------------------------------------------------------------------

// This file must conform to standard ANSI-C to be compatible with Swift.

#ifndef _DISK_TYPES_H
#define _DISK_TYPES_H

#include "Aliases.h"

//
// Enumerations
//

enum_long( DiskType)
{
    DISK_35,
    DISK_525
};

inline bool isDiskType(DiskType value)
{
    return value >= 0 && value <= DISK_525;
}

inline const char *sDiskType(DiskType value)
{
    switch (value) {
            
        case DISK_35:   return "3.5\"";
        case DISK_525:  return "5.25\"";
        default:        return "???";
    }
}

enum_long( DiskDensity)
{
    DISK_SD,
    DISK_DD,
    DISK_HD
};

inline bool isDiskDensity(DiskDensity value)
{
    return value >= 0 && value <= DISK_HD;
}

inline const char *sDiskDensity(DiskDensity value)
{
    switch (value) {
            
        case DISK_SD:  return "SD";
        case DISK_DD:  return "DD";
        case DISK_HD:  return "HD";
        default:       return "???";
    }
}

#endif
