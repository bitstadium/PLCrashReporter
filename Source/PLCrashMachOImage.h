/*
 * Author: Gwynne Raskind <gwynne@darkrainfall.org>
 * Author: Landon Fuller <landonf@plausiblelabs.com>
 *
 * Copyright (c) 2008-2012 Plausible Labs Cooperative, Inc.
 * All rights reserved.
 *
 * Permission is hereby granted, free of charge, to any person
 * obtaining a copy of this software and associated documentation
 * files (the "Software"), to deal in the Software without
 * restriction, including without limitation the rights to use,
 * copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following
 * conditions:
 *
 * The above copyright notice and this permission notice shall be
 * included in all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES
 * OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT
 * HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY,
 * WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
 * OTHER DEALINGS IN THE SOFTWARE.
 */

#include <stdint.h>
#include <libkern/OSAtomic.h>
#include <stdbool.h>
#include <mach-o/dyld.h>

/**
 * @internal
 * @ingroup plcrash_macho_image
 *
 * Parsed data for a loaded Mach-O binary image
 */
typedef struct plcrash_macho_image {
    /** The binary image's header address. */
    uintptr_t header;
    
    /** The binary image's name/path. */
    char *name;
    
    /** The binary image's CPU type. */
    cpu_type_t cputype;
    
    /** The binary image's CPU subtype. */
    cpu_subtype_t cpusubtype;
    
    /** The start address of the binary image's __TEXT segment. */
    uintptr_t textbase;
    
    /** The size of the binary image's __TEXT segment. */
    uint64_t textsize;
    
    /** The start address of the binary image's __TEXT,__text section. */
    uintptr_t textsectbase;
    
    /** The mach size of the binary image's __TEXT,__text section. */
    uint64_t textsectsize;

    /** If true, the binary has an LC_UUID load command and the uuid field has been populated. */
    bool hasUUID;
    
    /** The binary image's UUID from the LC_UUID load command, if any. If the UUID
     * is available, hasUUID will be true. */
    uint8_t uuid[16];
} plcrash_macho_image_t;

plcrash_error_t plcrash_macho_image_read_from_header(plcrash_macho_image_t *image, uintptr_t header);
plcrash_error_t plcrash_macho_image_read_from_named_header(plcrash_macho_image_t *image, uintptr_t header, char *name);
