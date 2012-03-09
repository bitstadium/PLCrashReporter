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

#include "PLCrashAsync.h"
#include "PLCrashMachOImage.h"
#include <dlfcn.h>
#include <string.h>

/**
 * @internal
 * @ingroup plcrash_macho
 * @defgroup plcrash_macho_image Mach-O Binary Image Parsing
 *
 * Handles parsing basic information from Mach-O binary images loaded into a process.
 * @{
 */

/**
 * Fill in a binary image based on its mach header.
 *
 * @param image The image structure to be filled out.
 * @param header A pointer to the binary image's mach header.
 *
 * @return Returns a plcrash_error_t. PLCRASH_ESUCCESS is returned for success. Other
 *		   errors are exceptional conditions and should not normally occur.
 *
 * @warning This function is not async safe, and must be called outside of a signal handler.
 */
plcrash_error_t plcrash_macho_image_read_from_header(plcrash_macho_image_t *image, uintptr_t header)
{
    Dl_info info;

    /* Look up the image info */
    if (dladdr((void *)header, &info) == 0) {
        PLCF_DEBUG("dladdr(%p, ...) failed", (void *)header);
        return PLCRASH_EUNKNOWN;
    }

	return plcrash_macho_image_read_from_named_header(image, header, strdup(info.dli_fname));
}

/**
 * Fill in a binary image based on its mach header.
 *
 * @param image The image structure to be filled out.
 * @param header A pointer to the binary image's mach header.
 * @param name The name of the binary image. The pointer is consumed by this function.
 *
 * @return Returns a plcrash_error_t. PLCRASH_ESUCCESS is returned for success. Other
 *		   errors are exceptional conditions and should not normally occur.
 *
 * @warning This function takes ownership of the name pointer in order to remain async safe.
 */
plcrash_error_t plcrash_macho_image_read_from_named_header(plcrash_macho_image_t *image, uintptr_t header, char *name)
{
    uint32_t ncmds = 0;
    const struct mach_header *header32 = (const struct mach_header *) header;
    const struct mach_header_64 *header64 = (const struct mach_header_64 *) header;
    struct load_command *cmd;
	
    image->header = header;
	image->name = name;
    
    /* Check for 32-bit/64-bit header and extract required values */
    switch (header32->magic) {
        /* 32-bit */
        case MH_MAGIC:
        case MH_CIGAM:
            ncmds = header32->ncmds;
            image->cputype = header32->cputype;
            image->cpusubtype = header32->cpusubtype;
            cmd = (struct load_command *) (header32 + 1);
            break;

        /* 64-bit */
        case MH_MAGIC_64:
        case MH_CIGAM_64:
            ncmds = header64->ncmds;
            image->cputype = header64->cputype;
            image->cpusubtype = header64->cpusubtype;
            cmd = (struct load_command *) (header64 + 1);
            break;

        default:
            PLCF_DEBUG("Invalid Mach-O header magic value: %x", header32->magic);
            return PLCRASH_EINVAL;
    }

    /* Search for a UUID and record the text segment and section */
    for (uint32_t i = 0; cmd != NULL && i < ncmds; i++) {
        /* 32-bit text segment */
        if (cmd->cmd == LC_SEGMENT) {
            struct segment_command *segment = (struct segment_command *) cmd;
            if (strcmp(segment->segname, SEG_TEXT) == 0) {
            	struct section *section = (struct section *) (segment + 1);
                
                image->textbase = segment->vmaddr;
                image->textsize = segment->vmsize;
                for (uint32_t j = 0; section != NULL && j < segment->nsects; j++) {
                	if (strcmp(section->sectname, SECT_TEXT) == 0) {
                    	image->textsectbase = section->addr;
                        image->textsectsize = section->size;
                        break;
                    }
                    section++;
                }
            }
        }
        /* 64-bit text segment */
        else if (cmd->cmd == LC_SEGMENT_64) {
            struct segment_command_64 *segment = (struct segment_command_64 *) cmd;
            if (strcmp(segment->segname, SEG_TEXT) == 0) {
            	struct section_64 *section = (struct section_64 *) (segment + 1);
                
                image->textbase = segment->vmaddr;
                image->textsize = segment->vmsize;
                for (uint32_t j = 0; section != NULL && j < segment->nsects; j++) {
                	if (strcmp(section->sectname, SECT_TEXT) == 0) {
                    	image->textsectbase = section->addr;
                        image->textsectsize = section->size;
                        break;
                    }
                    section++;
                }
            }
        }
        /* DWARF dSYM UUID */
        else if (cmd->cmd == LC_UUID && cmd->cmdsize == sizeof(struct uuid_command)) {
            image->hasUUID = true;
            memcpy(image->uuid, ((struct uuid_command *)cmd)->uuid, sizeof(image->uuid));
        }

        cmd = (struct load_command *) ((uint8_t *) cmd + cmd->cmdsize);
    }
    
    return PLCRASH_ESUCCESS;
}

/**
 * @}
 */
