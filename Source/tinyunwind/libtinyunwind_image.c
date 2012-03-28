/*
 * Author: Landon Fuller <landonf@plausiblelabs.com>
 * Author: Gwynne Raskind <gwynne@darkrainfall.org>
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

#import "libtinyunwind_image.h"
#import "libtinyunwind_internal.h"
#import "libtinyunwind_asynclist.h"
#import <stdlib.h>
#import <dlfcn.h>
#import <libgen.h>

#ifndef SECT_EHFRAME
# define SECT_EHFRAME "__eh_frame"
#endif

#ifndef SECT_UNWINDINFO
# define SECT_UNWINDINFO "__unwind_info"
#endif

#ifndef SEG_DWARF
# define SEG_DWARF "__DWARF"
#endif

#ifndef SECT_DEBUGFRAME
# define SECT_DEBUGFRAME "__debug_frame"
#endif

static inline tinyunw_image_piece_t tinyunw_piece_from_section(struct section_64 *section, intptr_t vmaddr_slide) {
    return (tinyunw_image_piece_t){ .base = section->addr + vmaddr_slide, .end = section->addr + vmaddr_slide + section->size, .length = section->size };
}

tinyunw_image_t *tinyunw_image_alloc (void) {
    return calloc(1, sizeof(tinyunw_image_t));
}

void tinyunw_image_free (tinyunw_image_t *image) {
    if (image->path)
        free(image->path);
    if (image->name)
        free(image->name);
    free(image);
}

int tinyunw_image_parse_from_header (tinyunw_image_t *image, uintptr_t header, intptr_t vmaddr_slide) {
    Dl_info info;
    
    if (dladdr((void *)header, &info) != 0) {
        image->path = strdup(info.dli_fname);
        image->name = strdup(basename(image->path));
    }
    
    const struct mach_header_64 *header64 = (const struct mach_header_64 *) header;
    struct load_command *cmd;
    
    if (header64->magic != MH_MAGIC_64 && header64->magic != MH_CIGAM_64) {
        return TINYUNW_EINVAL;
    }
    
    image->header = header;
    image->vmaddrSlide = vmaddr_slide;
    image->textSegment = (tinyunw_image_piece_t){ 0, 0 };
    image->textSection = (tinyunw_image_piece_t){ 0, 0 };
    image->exceptionFrameSection = (tinyunw_image_piece_t){ 0, 0 };
    image->debugFrameSection = (tinyunw_image_piece_t){ 0, 0 };
    image->unwindInfoSection = (tinyunw_image_piece_t){ 0, 0 };
    
    cmd = (struct load_command *) (header64 + 1);
    
    for (uint32_t i = 0; cmd != NULL && i < header64->ncmds; ++i) {
        if (cmd->cmd == LC_SEGMENT_64) {
            struct segment_command_64 *segment = (struct segment_command_64 *) cmd;
            
            if (strcmp(segment->segname, SEG_TEXT) == 0) {
                struct section_64 *section = (struct section_64 *) (segment + 1);
                
                image->textSegment.base = segment->vmaddr + vmaddr_slide;
                image->textSegment.length = segment->vmsize;
                image->textSegment.end = image->textSegment.base + image->textSegment.length;
                for (uint32_t j = 0; section != NULL && j < segment->nsects; ++j) {
                    if (strcmp(section->sectname, SECT_TEXT) == 0) {
                        image->textSection = tinyunw_piece_from_section(section, vmaddr_slide);
                    } else if (strcmp(section->sectname, SECT_EHFRAME) == 0) {
                        image->exceptionFrameSection = tinyunw_piece_from_section(section, vmaddr_slide);
                    } else if (strcmp(section->sectname, SECT_UNWINDINFO) == 0) {
                        image->unwindInfoSection = tinyunw_piece_from_section(section, vmaddr_slide);
                    } else if (strcmp(section->sectname, SECT_DEBUGFRAME) == 0) {
                        image->debugFrameSection = tinyunw_piece_from_section(section, vmaddr_slide);
                    }
                    ++section;
                }
            } else if (strcmp(segment->segname, SEG_DWARF) == 0) {
                struct section_64 *section = (struct section_64 *) (segment + 1);
                
                for (uint32_t j = 0; section != NULL && j < segment->nsects; ++j) {
                    if (strcmp(section->sectname, SECT_EHFRAME) == 0) {
                        image->exceptionFrameSection = tinyunw_piece_from_section(section, vmaddr_slide);
                    } else if (strcmp(section->sectname, SECT_DEBUGFRAME) == 0) {
                        image->debugFrameSection = tinyunw_piece_from_section(section, vmaddr_slide);
                    }
                    ++section;
                }
            }
        }
        cmd = (struct load_command *) ((uint8_t *) cmd + cmd->cmdsize);
    }
    
    return TINYUNW_ESUCCESS;
}

void tinyunw_async_list_remove_image_by_header (tinyunw_async_list_t *list, uintptr_t header) {
    tinyunw_async_list_entry_t *entry = NULL;
    
    /* This will result in two linear searches of the async list, but it allows
       the list to be abstracted away from a single data type while violating
       module separate slightly by knowing what to do with a removed binary image.
       It's a fair tradeoff, given that the async list is useful elsewhere. */
    while ((entry = tinyunw_async_list_next(list, entry)) != NULL) {
        if (((tinyunw_image_t *)entry->data)->header == header) {
            tinyunw_async_list_remove(list, entry->data);
            tinyunw_image_free(entry->data);
            return;
        }
    }
}
