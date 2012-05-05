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

#import "libtinyunwind_internal.h"
#import <mach-o/compact_unwind_encoding.h>

typedef struct tinyunw_unwind_info {
    uintptr_t ipStart, ipEnd;
    uint32_t encoding;
} tinyunw_unwind_info_t;

#if __x86_64__
static int tinyunw_unwind_find_info (tinyunw_image_t *image, uintptr_t ip, tinyunw_unwind_info_t *info);
static int tinyunw_unwind_update_state_from_info (tinyunw_image_t *image, tinyunw_unwind_info_t *info, tinyunw_context_t *context);
static int tinyunw_unwind_update_state_with_rbp (tinyunw_image_t *image, tinyunw_unwind_info_t *info, tinyunw_context_t *context);
static int tinyunw_unwind_update_state_with_frame (tinyunw_image_t *image, tinyunw_unwind_info_t *info, bool indirect, tinyunw_context_t *context);
#endif

int tinyunw_try_step_unwind (tinyunw_real_cursor_t *cursor) {
#if __x86_64__
    if (cursor->current_context.__rip == 0) {
        TINYUNW_DEBUG("RIP is null, definitely no frame.");
        return TINYUNW_ENOFRAME;
    }
    
    tinyunw_image_t *image = tinyunw_get_image_containing_address(cursor->current_context.__rip);
    
    if (!image || image->unwindInfoSection.base == 0) {
        //TINYUNW_DEBUG("No compact unwinding info in %s for RIP 0x%llx", image->name, cursor->current_context.__rip);
        return TINYUNW_ENOINFO;
    }
    
    tinyunw_unwind_info_t info;
    int result = 0;
    
    if ((result = tinyunw_unwind_find_info(image, cursor->current_context.__rip, &info)) != TINYUNW_ESUCCESS) {
        return result;
    }
    
    return tinyunw_unwind_update_state_from_info(image, &info, &cursor->current_context);
#endif
    return TINYUNW_ENOINFO;
}

#if __x86_64__
int tinyunw_unwind_find_info (tinyunw_image_t *image, uintptr_t ip, tinyunw_unwind_info_t *info) {
    struct unwind_info_section_header *header = (struct unwind_info_section_header *)(image->unwindInfoSection.base);
    
    if (header->version != UNWIND_SECTION_VERSION) {
        TINYUNW_DEBUG("Unknown compact encoding version %u in %s for RIP 0x%lx", header->version, image->name, ip);
        return TINYUNW_ENOINFO;
    }
    
    uintptr_t foffset = ip - image->header;
    struct unwind_info_section_header_index_entry *index_entries =
        (struct unwind_info_section_header_index_entry *)(image->unwindInfoSection.base + header->indexSectionOffset);
    
    //TINYUNW_DEBUG("Searching for offset %lx in %u entries starting at %p.", foffset, header->indexCount, index_entries);
    
    /* Binary search for appropos page. */
    uint32_t low = 0, high = header->indexCount, max = high - 1, mid = 0;
    
    while (low < high) {
        mid = (low + high) / 2;
        if (index_entries[mid].functionOffset <= foffset) {
            if (mid == max || (index_entries[mid + 1].functionOffset > foffset)) {
                low = mid;
                break;
            } else
                low = mid + 1;
        } else {
            high = mid;
        }
    }
    
    //TINYUNW_DEBUG("Using first level entry at index %u", low);
    
    uint32_t firstLevelOffset = index_entries[low].functionOffset, firstLevelNextOffset = index_entries[low + 1].functionOffset, encoding = 0;
    uintptr_t secondLevelAddress = image->unwindInfoSection.base + index_entries[low].secondLevelPagesSectionOffset, fstart = 0, fend = 0;

    switch (*(uint32_t *)secondLevelAddress) {
        case UNWIND_SECOND_LEVEL_REGULAR: {
            struct unwind_info_regular_second_level_page_header *pageheader = (struct unwind_info_regular_second_level_page_header *)secondLevelAddress;
            struct unwind_info_regular_second_level_entry *pageentries =
                (struct unwind_info_regular_second_level_entry *)(secondLevelAddress + pageheader->entryPageOffset);
            
            //TINYUNW_DEBUG("Regular second level entry starts at 0x%lx with %u entries at %p", secondLevelAddress, pageheader->entryCount, pageentries);
            low = 0;
            high = pageheader->entryCount;
            while (low < high) {
                mid = (low + high) / 2;
                if (pageentries[mid].functionOffset <= foffset) {
                    if (mid == pageheader->entryCount - 1) {
                        low = mid;
                        fend = image->header + firstLevelNextOffset;
                        break;
                    } else if (pageentries[mid + 1].functionOffset > foffset) {
                        low = mid;
                        fend = image->header + pageentries[low + 1].functionOffset;
                        break;
                    }
                } else {
                    high = mid;
                }
            }
            encoding = pageentries[low].encoding;
            fstart = image->header + pageentries[low].functionOffset;
            if (ip < fstart || ip > fend) {
                //TINYUNW_DEBUG("RIP 0x%lx not in unwind table.", ip);
                return TINYUNW_ENOINFO;
            }
            break;
        }
        case UNWIND_SECOND_LEVEL_COMPRESSED: {
            struct unwind_info_compressed_second_level_page_header *pageheader = (struct unwind_info_compressed_second_level_page_header *)secondLevelAddress;
            uint32_t *pageentries = (uint32_t *)(secondLevelAddress + pageheader->entryPageOffset);
            
            //TINYUNW_DEBUG("Compressed second level entry starts at 0x%lx with %u entries at %p", secondLevelAddress, pageheader->entryCount, pageentries);
            low = 0;
            high = pageheader->entryCount;
            max = high - 1;
            while (low < high) {
                mid = (low + high) / 2;
                if (UNWIND_INFO_COMPRESSED_ENTRY_FUNC_OFFSET(pageentries[mid]) <= foffset) {
                    if (mid == max || UNWIND_INFO_COMPRESSED_ENTRY_FUNC_OFFSET(pageentries[mid + 1]) > foffset) {
                        low = mid;
                        break;
                    } else {
                        low = mid + 1;
                    }
                } else {
                    high = mid;
                }
            }
            fstart = image->header + firstLevelOffset + UNWIND_INFO_COMPRESSED_ENTRY_FUNC_OFFSET(pageentries[low]);
            if (low < max) {
                fend = image->header + firstLevelOffset + UNWIND_INFO_COMPRESSED_ENTRY_FUNC_OFFSET(pageentries[low + 1]);
            } else {
                fend = image->header + firstLevelNextOffset;
            }
            //TINYUNW_DEBUG("Chose second-level entry index %u, from 0x%lx to 0x%lx", low, fstart, fend);
            if (ip < fstart || ip > fend) {
                //TINYUNW_DEBUG("RIP 0x%lx not in unwind table.", ip);
                return TINYUNW_ENOINFO;
            }
            encoding = UNWIND_INFO_COMPRESSED_ENTRY_ENCODING_INDEX(pageentries[low]);
            if (encoding < header->commonEncodingsArrayCount) {
                encoding = ((uint32_t *)(image->header + header->commonEncodingsArraySectionOffset))[encoding];
            } else {
                encoding = ((uint32_t *)(secondLevelAddress + pageheader->encodingsPageOffset))[encoding - header->commonEncodingsArrayCount];
            }
            //TINYUNW_DEBUG("Found encoding 0x%u for second-level entry.", encoding);
            break;
        }
        default:
            TINYUNW_DEBUG("Unrecognized unwind page format.");
            return TINYUNW_EINVAL;
    }
    info->encoding = encoding;
    info->ipStart = fstart;
    info->ipEnd = fend;
    return TINYUNW_ESUCCESS;
}

int tinyunw_unwind_update_state_from_info (tinyunw_image_t *image, tinyunw_unwind_info_t *info, tinyunw_context_t *context) {
    switch (info->encoding & UNWIND_X86_64_MODE_MASK) {
        case UNWIND_X86_64_MODE_COMPATIBILITY:
            /* We don't support the compatibility mode, and neither does Apple's
               libunwind implementation. Apparently, modern binaries use a zero
               encoding to mean "no unwind info for this function." */
            //TINYUNW_DEBUG("Can't unwind compatibility encoding at RIP 0x%llx", context->__rip);
            return TINYUNW_ENOINFO;
        case UNWIND_X86_64_MODE_DWARF:
            /* If DWARF is called for, pretend we have no info. DWARF will be
               up next anyway. Future optimization: Use the unwind info's FDE
               location hint to avoid a full DWARF info scan. */
            //TINYUNW_DEBUG("Skipping to DWARF unwind for RIP 0x%llx", context->__rip);
            return TINYUNW_ENOINFO;
        case UNWIND_X86_64_MODE_RBP_FRAME:
            //TINYUNW_DEBUG("Unwinding RBP state for RIP 0x%llx", context->__rip);
            return tinyunw_unwind_update_state_with_rbp(image, info, context);
        case UNWIND_X86_64_MODE_STACK_IMMD:
            //TINYUNW_DEBUG("Unwinding frameless immediate state for RIP 0x%llx", context->__rip);
            return tinyunw_unwind_update_state_with_frame(image, info, false, context);
        case UNWIND_X86_64_MODE_STACK_IND:
            //TINYUNW_DEBUG("Unwinding frameless indirect state for RIP 0x%llx", context->__rip);
            return tinyunw_unwind_update_state_with_frame(image, info, true, context);
    }
    return TINYUNW_EINVAL;
}

#define GET_BITS(value, mask) ((value >> __builtin_ctz(mask)) & ((1 << __builtin_popcount(mask)) - 1))

static const tinyunw_regnum_t regmap[0x7] = {
    [UNWIND_X86_64_REG_NONE] = -1, [UNWIND_X86_64_REG_RBX] = TINYUNW_X86_64_RBX, [UNWIND_X86_64_REG_R12] = TINYUNW_X86_64_R12,
    [UNWIND_X86_64_REG_R13] = TINYUNW_X86_64_R13, [UNWIND_X86_64_REG_R14] = TINYUNW_X86_64_R14, [UNWIND_X86_64_REG_R15] = TINYUNW_X86_64_R15,
    TINYUNW_SAVED_REGISTER_COUNT
};
        

int tinyunw_unwind_update_state_with_rbp (tinyunw_image_t *image, tinyunw_unwind_info_t *info, tinyunw_context_t *context) {
    uint32_t regoffset = GET_BITS(info->encoding, UNWIND_X86_64_RBP_FRAME_OFFSET),
             reglocs = GET_BITS(info->encoding, UNWIND_X86_64_RBP_FRAME_REGISTERS);
    uintptr_t regs = context->__rbp - regoffset * sizeof(tinyunw_word_t);
    tinyunw_word_t word = 0;
    
    for (int i = 0; i < 5; ++i) {
        if (regmap[reglocs & 0x7] == TINYUNW_SAVED_REGISTER_COUNT) {
            TINYUNW_DEBUG("Bad compact encoding register number 0x%x for RIP 0x%llx", reglocs & 0x7, context->__rip);
            return TINYUNW_EINVAL;
        } else if (regmap[reglocs & 0x07] == -1) {
            continue;
        }
        if (tinyunw_read_unsafe_memory((void *)regs, &word, sizeof(tinyunw_word_t)) == TINYUNW_ESUCCESS)
            tinyunw_setreg(context, regmap[reglocs & 0x7], word);
        regs += sizeof(tinyunw_word_t);
        reglocs >>= 3;
    }
    
    /* Update context as for a standard frame pointer. */
    uint64_t rbp = context->__rbp;
    
    tinyunw_read_unsafe_memory((void *)rbp, &(context->__rbp), sizeof(uint64_t)); /* next frame pointer */
    context->__rsp = rbp + sizeof(uint64_t) * 2; /* sp = bp + saved bp + ret addr */
    tinyunw_read_unsafe_memory((void *)(rbp + sizeof(uint64_t)), &(context->__rip), sizeof(uint64_t)); /* ip = ret addr */
    return TINYUNW_ESUCCESS;
}

int tinyunw_unwind_update_state_with_frame (tinyunw_image_t *image, tinyunw_unwind_info_t *info, bool indirect, tinyunw_context_t *context) {
    uint32_t stacksize = GET_BITS(info->encoding, UNWIND_X86_FRAMELESS_STACK_SIZE) * sizeof(tinyunw_word_t),
             stackadj = GET_BITS(info->encoding, UNWIND_X86_FRAMELESS_STACK_ADJUST),
             nregs = GET_BITS(info->encoding, UNWIND_X86_FRAMELESS_STACK_REG_COUNT),
             perm = GET_BITS(info->encoding, UNWIND_X86_FRAMELESS_STACK_REG_PERMUTATION);
    
    /* The stack size is indirected through a subl instruction. Is this correct
       for x86_64? Apple's libunwind says so, but it seems bothersome that it
       should also be remarking that the 32-bit amount subtracted from ESP
       (not RSP!) is what to use here. */
    if (indirect) {
        stacksize = *(uint32_t *)(info->ipStart + (stacksize / sizeof(tinyunw_word_t))) + stackadj * sizeof(tinyunw_word_t);
    }
    
    /* Unravel permuted registers. */
    int permregs[6] = {0};
    bool rused[7] = {false};
    uintptr_t savedregs = context->__rsp + stacksize - sizeof(tinyunw_word_t) * (nregs + 1);
    tinyunw_word_t word = 0;
    
    switch (nregs) {
        #define UNPERMUTE(n, factor)    do { permregs[n] = perm / factor; perm -= permregs[n] * factor; } while (0)
        case 6:
        case 5: UNPERMUTE(0, 120);  UNPERMUTE(1, 24);   UNPERMUTE(2, 6);    UNPERMUTE(3, 2); break;
        case 4: UNPERMUTE(0, 60);   UNPERMUTE(1, 12);   UNPERMUTE(2, 3);    break;
        case 3: UNPERMUTE(0, 20);   UNPERMUTE(1, 4);    break;
        case 2: UNPERMUTE(0, 5);    break;
        #undef UNPERMUTE
    }
    if (nregs) permregs[nregs - 1] = perm;
    /* XXX study this loop a bit more */
    for (int i = 0; i < nregs; ++i) {
        for (int j = 1, num = 0; j < 7; ++j) {
            if (!rused[j]) {
                if (num++ == permregs[i]) {
                    rused[j] = true;
                    if (regmap[j] == TINYUNW_SAVED_REGISTER_COUNT) {
                        TINYUNW_DEBUG("Bad compact encoding register number 0x%x for RIP 0x%llx", regmap[j], context->__rip);
                        return TINYUNW_EINVAL;
                    }
                    if (tinyunw_read_unsafe_memory((void *)savedregs, &word, sizeof(tinyunw_word_t)) == TINYUNW_ESUCCESS)
                        tinyunw_setreg(context, regmap[j], word);
                    savedregs += sizeof(tinyunw_word_t);
                    break;
                }
            }
        }
    }
    /* Restore IP and SP. */
    context->__rsp = savedregs + sizeof(tinyunw_word_t);
    tinyunw_read_unsafe_memory((void *)savedregs, &(context->__rip), sizeof(uint64_t));
    return TINYUNW_ESUCCESS;
}

#endif
