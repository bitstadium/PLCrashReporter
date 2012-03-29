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
#import <mach-o/nlist.h>

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

static tinyunw_image_piece_t tinyunw_image_make_piece (uintptr_t base, uint64_t len) {
    return (tinyunw_image_piece_t){ .base = base, .length = len, .end = base + len };
}

static int tinyunw_image_parse_from_header32 (tinyunw_image_t *image, uintptr_t header, intptr_t vmaddr_slide) {
    const struct mach_header *header32 = (const struct mach_header *) header;
    struct load_command *cmd;
    
    image->is64Bit = false;
    cmd = (struct load_command *) (header32 + 1);
    
    for (uint32_t i = 0; cmd != NULL && i < header32->ncmds; ++i) {
        if (cmd->cmd == LC_SEGMENT) {
            struct segment_command *segment = (struct segment_command *) cmd;
            
            if (strcmp(segment->segname, SEG_TEXT) == 0) {
                struct section *section = (struct section *) (segment + 1);
                
                image->textSection = tinyunw_image_make_piece(segment->vmsize + vmaddr_slide - segment->fileoff, segment->vmsize);
                for (uint32_t j = 0; section != NULL && j < segment->nsects; ++j) {
                    if (strcmp(section->sectname, SECT_TEXT) == 0) {
                        image->textSection = tinyunw_image_make_piece(section->addr + vmaddr_slide, section->size);
                    } else if (strcmp(section->sectname, SECT_EHFRAME) == 0) {
                        image->exceptionFrameSection = tinyunw_image_make_piece(section->addr + vmaddr_slide, section->size);
                    } else if (strcmp(section->sectname, SECT_UNWINDINFO) == 0) {
                        image->unwindInfoSection = tinyunw_image_make_piece(section->addr + vmaddr_slide, section->size);
                    } else if (strcmp(section->sectname, SECT_DEBUGFRAME) == 0) {
                        image->debugFrameSection = tinyunw_image_make_piece(section->addr + vmaddr_slide, section->size);
                    }
                    ++section;
                }
            } else if (strcmp(segment->segname, SEG_DWARF) == 0) {
                struct section *section = (struct section *) (segment + 1);
                
                for (uint32_t j = 0; section != NULL && j < segment->nsects; ++j) {
                    if (strcmp(section->sectname, SECT_EHFRAME) == 0) {
                        image->exceptionFrameSection = tinyunw_image_make_piece(section->addr + vmaddr_slide, section->size);
                    } else if (strcmp(section->sectname, SECT_DEBUGFRAME) == 0) {
                        image->debugFrameSection = tinyunw_image_make_piece(section->addr + vmaddr_slide, section->size);
                    }
                    ++section;
                }
            } else if (strcmp(segment->segname, SEG_LINKEDIT) == 0) {
                image->linkeditSegment = tinyunw_image_make_piece(segment->vmaddr + vmaddr_slide - segment->fileoff, segment->vmsize);
            }
        } else if (cmd->cmd == LC_SYMTAB) {
            struct symtab_command *symtab = (struct symtab_command *) cmd;
            
            image->symbolTable = tinyunw_image_make_piece(symtab->symoff, symtab->nsyms * sizeof(struct nlist));
            image->stringTable = tinyunw_image_make_piece(symtab->stroff, symtab->strsize);
        } else if (cmd->cmd == LC_DYSYMTAB) {
            struct dysymtab_command *dysymtab = (struct dysymtab_command *) cmd;
            
            image->symbolInformation.firstGlobalSymbol = dysymtab->iextdefsym;
            image->symbolInformation.numGlobalSymbols = dysymtab->nextdefsym;
            image->symbolInformation.firstLocalSymbol = dysymtab->ilocalsym;
            image->symbolInformation.numLocalSymbols = dysymtab->nlocalsym;
        }
        cmd = (struct load_command *) ((uint8_t *) cmd + cmd->cmdsize);
    }
    
    return TINYUNW_ESUCCESS;
}

static int tinyunw_image_parse_from_header64 (tinyunw_image_t *image, uintptr_t header, intptr_t vmaddr_slide) {
    const struct mach_header_64 *header64 = (const struct mach_header_64 *) header;
    struct load_command *cmd;
    
    image->is64Bit = true;
    cmd = (struct load_command *) (header64 + 1);
    
    for (uint32_t i = 0; cmd != NULL && i < header64->ncmds; ++i) {
        if (cmd->cmd == LC_SEGMENT_64) {
            struct segment_command_64 *segment = (struct segment_command_64 *) cmd;
            
            if (strcmp(segment->segname, SEG_TEXT) == 0) {
                struct section_64 *section = (struct section_64 *) (segment + 1);
                
                image->textSection = tinyunw_image_make_piece(segment->vmsize + vmaddr_slide - segment->fileoff, segment->vmsize);
                for (uint32_t j = 0; section != NULL && j < segment->nsects; ++j) {
                    if (strcmp(section->sectname, SECT_TEXT) == 0) {
                        image->textSection = tinyunw_image_make_piece(section->addr + vmaddr_slide, section->size);
                    } else if (strcmp(section->sectname, SECT_EHFRAME) == 0) {
                        image->exceptionFrameSection = tinyunw_image_make_piece(section->addr + vmaddr_slide, section->size);
                    } else if (strcmp(section->sectname, SECT_UNWINDINFO) == 0) {
                        image->unwindInfoSection = tinyunw_image_make_piece(section->addr + vmaddr_slide, section->size);
                    } else if (strcmp(section->sectname, SECT_DEBUGFRAME) == 0) {
                        image->debugFrameSection = tinyunw_image_make_piece(section->addr + vmaddr_slide, section->size);
                    }
                    ++section;
                }
            } else if (strcmp(segment->segname, SEG_DWARF) == 0) {
                struct section_64 *section = (struct section_64 *) (segment + 1);
                
                for (uint32_t j = 0; section != NULL && j < segment->nsects; ++j) {
                    if (strcmp(section->sectname, SECT_EHFRAME) == 0) {
                        image->exceptionFrameSection = tinyunw_image_make_piece(section->addr + vmaddr_slide, section->size);
                    } else if (strcmp(section->sectname, SECT_DEBUGFRAME) == 0) {
                        image->debugFrameSection = tinyunw_image_make_piece(section->addr + vmaddr_slide, section->size);
                    }
                    ++section;
                }
            } else if (strcmp(segment->segname, SEG_LINKEDIT) == 0) {
                image->linkeditSegment = tinyunw_image_make_piece(segment->vmaddr + vmaddr_slide - segment->fileoff, segment->vmsize);
            }
        } else if (cmd->cmd == LC_SYMTAB) {
            struct symtab_command *symtab = (struct symtab_command *) cmd;
            
            image->symbolTable = tinyunw_image_make_piece(symtab->symoff, symtab->nsyms * sizeof(struct nlist_64));
            image->stringTable = tinyunw_image_make_piece(symtab->stroff, symtab->strsize);
        } else if (cmd->cmd == LC_DYSYMTAB) {
            struct dysymtab_command *dysymtab = (struct dysymtab_command *) cmd;
            
            image->symbolInformation.firstGlobalSymbol = dysymtab->iextdefsym;
            image->symbolInformation.numGlobalSymbols = dysymtab->nextdefsym;
            image->symbolInformation.firstLocalSymbol = dysymtab->ilocalsym;
            image->symbolInformation.numLocalSymbols = dysymtab->nlocalsym;
        }
        cmd = (struct load_command *) ((uint8_t *) cmd + cmd->cmdsize);
    }
    
    return TINYUNW_ESUCCESS;
}

int tinyunw_image_parse_from_header (tinyunw_image_t *image, uintptr_t header, intptr_t vmaddr_slide) {
    Dl_info info;
    
    if (dladdr((void *)header, &info) != 0) {
        image->path = strdup(info.dli_fname);
        image->name = strdup(basename(image->path));
    }
    
    image->header = header;
    image->vmaddrSlide = vmaddr_slide;
    image->textSegment = (tinyunw_image_piece_t){ 0, 0 };
    image->textSection = (tinyunw_image_piece_t){ 0, 0 };
    image->exceptionFrameSection = (tinyunw_image_piece_t){ 0, 0 };
    image->debugFrameSection = (tinyunw_image_piece_t){ 0, 0 };
    image->unwindInfoSection = (tinyunw_image_piece_t){ 0, 0 };
    
    const struct mach_header *header32 = (const struct mach_header *) header;
    int result = 0;
    
    if (header32->magic == MH_MAGIC || header32->magic == MH_CIGAM) {
        result = tinyunw_image_parse_from_header32(image, header, vmaddr_slide);
    } else if (header32->magic == MH_MAGIC_64 || header32->magic == MH_CIGAM_64) {
        result = tinyunw_image_parse_from_header64(image, header, vmaddr_slide);
    } else {
        result = TINYUNW_EINVAL;
    }
    
    if (result == TINYUNW_ESUCCESS) {
        /* After all commands are parsed, update the symbol and strings tables as
           necessary relative to the __LINKEDIT segment. */
        if (image->linkeditSegment.base != 0 && image->symbolTable.base != 0) {
            image->symbolTable.base += image->linkeditSegment.base;
            image->symbolTable.end += image->linkeditSegment.base;
            image->stringTable.base += image->linkeditSegment.base;
            image->stringTable.end += image->linkeditSegment.base;
        }
    }
    
    return result;
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
