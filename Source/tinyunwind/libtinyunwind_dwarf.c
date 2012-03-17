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
#import <stdlib.h>
#import <fcntl.h>
#import <stdio.h>
#import <sys/param.h>

#define TINYUNW_DEBUG(msg, args...) {\
    char output[256];\
    snprintf(output, sizeof(output), "[tinyunwind] " msg "\n", ## args); \
    write(STDERR_FILENO, output, strlen(output));\
}

int			tinyunw_try_step_dwarf(tinyunw_real_cursor_t *cursor)
{
#if __x86_64__
    tinyunw_image_t *image = tinyunw_get_image_containing_address(cursor->current_context.__rip);
    
    /* Check whether the image that contains the current instruction has any
       debug info we can read. If not, give up immediately. */
    if (!image->dwarfInfo.nfdes)
        return TINYUNW_ENOFRAME;
    
    tinyunw_dwarf_fde_t *fde = tinyunw_dwarf_fde_list_search(&image->dwarfInfo, cursor->current_context.__rip);
    
    if (fde) {
        /* Try using it here! */
        tinyunw_dwarf_cfa_state_t state;
        
        tinyunw_dwarf_run_cfa_for_fde(fde, cursor->current_context.__rip, &state);
        TINYUNW_DEBUG("Generated state:");
        TINYUNW_DEBUG("\tCFA register: %llu + 0x%llx exp 0x%ld", state.cfaRegister, state.cfaOffset, state.cfaExpression);
        for (int i = 0; i < TINYUNW_SAVED_REGISTER_COUNT; ++i) {
            TINYUNW_DEBUG("\tRegister %d: 0x%llx @ %d", i, state.savedRegisters[i].value, state.savedRegisters[i].saveLocation);
        }
    }
#endif    
    return TINYUNW_ENOFRAME;
}

enum {
    DW_EH_PE_indirect = 0x80, /* flag to indirect through the read value */
    DW_EH_PE_omit = 0xff, /* NULL pointer */
    DW_EH_PE_ptr = 0x00, /* pointer-sized unsigned value */
    DW_EH_PE_uleb128 = 0x01, /* unsigned LE base-128 value */
    DW_EH_PE_udata2 = 0x02, /* unsigned 16-bit value */
    DW_EH_PE_udata4 = 0x03, /* unsigned 32-bit value */
    DW_EH_PE_udata8 = 0x04, /* unsigned 64-bit value */
    DW_EH_PE_sleb128 = 0x09, /* signed LE base-128 value */
    DW_EH_PE_sdata2 = 0x0a, /* signed 16-bit value */
    DW_EH_PE_sdata4 = 0x0b, /* signed 32-bit value */
    DW_EH_PE_sdata8 = 0x0c, /* signed 64-bit value */
    DW_EH_PE_absptr = 0x00, /* absolute value */
    DW_EH_PE_pcrel = 0x10, /* rel. to addr. of encoded value */
};

enum
{
    DWARF_CFA_OPCODE_MASK = 0xc0,
    DWARF_CFA_OPERAND_MASK = 0x3f,
    DW_CFA_advance_loc = 0x40,
    DW_CFA_offset = 0x80,
    DW_CFA_restore = 0xc0,
    DW_CFA_nop = 0x00,
    DW_CFA_set_loc = 0x01,
    DW_CFA_advance_loc1 = 0x02,
    DW_CFA_advance_loc2 = 0x03,
    DW_CFA_advance_loc4 = 0x04,
    DW_CFA_offset_extended = 0x05,
    DW_CFA_restore_extended = 0x06,
    DW_CFA_undefined = 0x07,
    DW_CFA_same_value = 0x08,
    DW_CFA_register = 0x09,
    DW_CFA_remember_state = 0x0a,
    DW_CFA_restore_state = 0x0b,
    DW_CFA_def_cfa = 0x0c,
    DW_CFA_def_cfa_register = 0x0d,
    DW_CFA_def_cfa_offset = 0x0e,
    DW_CFA_def_cfa_expression = 0x0f,
    DW_CFA_expression = 0x10,
    DW_CFA_offset_extended_sf = 0x11,
    DW_CFA_def_cfa_sf = 0x12,
    DW_CFA_def_cfa_offset_sf = 0x13,
    DW_CFA_val_offset = 0x14,
    DW_CFA_val_offset_sf = 0x15,
    DW_CFA_val_expression = 0x16,
    DW_CFA_lo_user = 0x1c,
    DW_CFA_GNU_window_save = 0x2d,
    DW_CFA_GNU_args_size = 0x2e,
    DW_CFA_GNU_negative_offset_extended = 0x2f,
    DW_CFA_hi_user = 0x3c,
};

/* Return from function with error if there aren't enough bytes left. */
#define ENSURE_BYTES(l, ml, b)  do { if ((ml) - (*l) < (b)) { return TINYUNW_EUNSPEC; } } while (0);
#define tinyunw_dwarf_read_u8(l, ml) ({ uint8_t v = 0; ENSURE_BYTES(l, ml, sizeof(uint8_t)); v = *(uint8_t *)(*l); (*l) += sizeof(uint8_t); v; })
#define tinyunw_dwarf_read_s8(l, ml) ({ int8_t v = 0; ENSURE_BYTES(l, ml, sizeof(int8_t)); v = *(int8_t *)(l); (*l) += sizeof(int8_t); v; })
#define tinyunw_dwarf_read_u16(l, ml) ({ uint16_t v = 0; ENSURE_BYTES(l, ml, sizeof(uint16_t)); v = *(uint16_t *)(*l); (*l) += sizeof(uint16_t); v; })
#define tinyunw_dwarf_read_s16(l, ml) ({ int16_t v = 0; ENSURE_BYTES(l, ml, sizeof(int16_t)); v = *(int16_t *)(*l); (*l) += sizeof(int16_t); v; })
#define tinyunw_dwarf_read_u32(l, ml) ({ uint32_t v = 0; ENSURE_BYTES(l, ml, sizeof(uint32_t)); v = *(uint32_t *)(*l); (*l) += sizeof(uint32_t); v; })
#define tinyunw_dwarf_read_s32(l, ml) ({ int32_t v = 0; ENSURE_BYTES(l, ml, sizeof(int32_t)); v = *(int32_t *)(*l); (*l) += sizeof(int32_t); v; })
#define tinyunw_dwarf_read_u64(l, ml) ({ uint64_t v = 0; ENSURE_BYTES(l, ml, sizeof(uint64_t)); v = *(uint64_t *)(*l); (*l) += sizeof(uint64_t); v; })
#define tinyunw_dwarf_read_s64(l, ml) ({ int64_t v = 0; ENSURE_BYTES(l, ml, sizeof(int64_t)); v = *(int64_t *)(*l); (*l) += sizeof(int64_t); v; })
#define tinyunw_dwarf_read_u(l, ml, n) ((n == 1 ? tinyunw_dwarf_read_u8(l, ml) : (n == 2 ? tinyunw_dwarf_read_u16(l, ml) : tinyunw_dwarf_read_u32(l, ml))))
#define tinyunw_dwarf_read_uleb128(l, ml) ({ uint64_t v = 0; if (_tinyunw_dwarf_read_uleb128((l), (ml), &v)) { return TINYUNW_EUNSPEC; } v; })
#define tinyunw_dwarf_read_sleb128(l, ml) ({ int64_t v = 0; if (_tinyunw_dwarf_read_sleb128((l), (ml), &v)) { return TINYUNW_EUNSPEC; } v; })
#define tinyunw_dwarf_read_leb128(l, ml, s) (s ? tinyunw_dwarf_read_sleb128(l, ml) : tinyunw_dwarf_read_uleb128(l, ml))
#define tinyunw_dwarf_read_pointer(l, ml) (sizeof(uintptr_t) == sizeof(uint64_t) ? tinyunw_dwarf_read_u64(l, ml) : tinyunw_dwarf_read_u32(l, ml))
#define tinyunw_dwarf_read_encoded_pointer(l, ml, e) ({ uintptr_t v = 0; if (_tinyunw_dwarf_read_encoded_pointer((l), (ml), (e), &v)) { return TINYUNW_EUNSPEC; } v; })

static inline int _tinyunw_dwarf_read_uleb128(uintptr_t *loc, uintptr_t maxLoc, uint64_t *value) {
    int bits = 0;
    uint8_t rb = 0, b = 0;
    
    do {
        rb = tinyunw_dwarf_read_u8(loc, maxLoc);
        b = rb & 0x7F;
        if (b >= 0x40 || (b << bits >> bits != b))
            return TINYUNW_EINVAL;
        *value |= b << bits;
        bits += 7;
    } while (rb >= 0x80);
    return TINYUNW_ESUCCESS;
}

static inline int _tinyunw_dwarf_read_sleb128(uintptr_t *loc, uintptr_t maxLoc, int64_t *value) {
    int bits = 0;
    uint8_t rb = 0;

    do {
        rb = tinyunw_dwarf_read_u8(loc, maxLoc);
        *value |= ((rb & 0x7F) << bits);
        bits += 7;
    } while (rb & 0x80);
    if ((rb & 0x40) != 0)
        *value |= (-1LL) << bits;
    return TINYUNW_ESUCCESS;
}

static int _tinyunw_dwarf_read_encoded_pointer(uintptr_t *loc, uintptr_t maxLoc, uint8_t encoding, uintptr_t *value) {
    uintptr_t startLoc = *loc;
    
    if (encoding == DW_EH_PE_omit) {
        *value = 0;
        return TINYUNW_ESUCCESS;
    }
    
    switch (encoding & 0x0F) {
        case DW_EH_PE_ptr:
            *value = tinyunw_dwarf_read_pointer(loc, maxLoc);
            break;
        case DW_EH_PE_uleb128:
            *value = tinyunw_dwarf_read_uleb128(loc, maxLoc);
            break;
        case DW_EH_PE_udata2:
            *value = tinyunw_dwarf_read_u16(loc, maxLoc);
            break;
        case DW_EH_PE_udata4:
            *value = tinyunw_dwarf_read_u32(loc, maxLoc);
            break;
        case DW_EH_PE_udata8:
            *value = tinyunw_dwarf_read_u64(loc, maxLoc);
            break;
        case DW_EH_PE_sleb128:
            *value = tinyunw_dwarf_read_sleb128(loc, maxLoc);
            break;
        case DW_EH_PE_sdata2:
            *value = tinyunw_dwarf_read_s16(loc, maxLoc);
            break;
        case DW_EH_PE_sdata4:
            *value = tinyunw_dwarf_read_s32(loc, maxLoc);
            break;
        case DW_EH_PE_sdata8:
            *value = tinyunw_dwarf_read_s64(loc, maxLoc);
            break;
        default:
            return TINYUNW_EINVAL;
    }
    
    /* Only support pc-relative and absolute encodings. Others are difficult,
       per Apple libunwind. */
    if ((encoding & 0x70) == DW_EH_PE_pcrel) {
        *value += startLoc;
    } else if ((encoding & 0x70) != DW_EH_PE_absptr) {
        return TINYUNW_EINVAL;
    }
    
    if ((encoding & DW_EH_PE_indirect) == DW_EH_PE_indirect) {
        uintptr_t p = *value;
        
        *value = tinyunw_dwarf_read_pointer(&p, maxLoc);
    }
    return TINYUNW_ESUCCESS;
}

/**
  * @internal
  * Figure out whether the next entry in the frame is a CIE or an FDE. This is a
  * "peek" routine, so the current location isn't updated.
  */
int tinyunw_dwarf_get_entry_kind(uintptr_t loc, uintptr_t maxLoc, bool isEHFrame, bool *isCIE)
{
    uint32_t entryLength = tinyunw_dwarf_read_u32(&loc, maxLoc);
    
    /* 64-bit entry */
    if (entryLength == 0xFFFFFFFFUL) {
        (void) tinyunw_dwarf_read_u64(&loc, maxLoc);

        uint64_t cieID = tinyunw_dwarf_read_u64(&loc, maxLoc);
        
        *isCIE = (cieID == (isEHFrame ? 0 : 0xFFFFFFFFFFFFFFFFULL));
    /* 32-bit entry */
    } else {
        uint32_t cieID = tinyunw_dwarf_read_u32(&loc, maxLoc);
        
        *isCIE = (cieID == (isEHFrame ? 0 : 0xFFFFFFFFUL));
    }
    return TINYUNW_ESUCCESS;
}

/**
  * @internal
  * Parse a CIE. It is already known that the entry is a CIE, so don't bother
  * checking again.
  */
int tinyunw_dwarf_parse_cie(uintptr_t *loc, uintptr_t maxLoc, bool isEHFrame, tinyunw_dwarf_cie_t *cie)
{
    memset(cie, 0, sizeof(tinyunw_dwarf_cie_t));
    cie->cieLocation = *loc;

    uint32_t entryLength32 = tinyunw_dwarf_read_u32(loc, maxLoc);
    
    /* 64-bit entry */
    if (entryLength32 == 0xFFFFFFFFUL) {
        cie->length = tinyunw_dwarf_read_u64(loc, maxLoc);
        cie->cieStart = *loc;
        cie->cieID = tinyunw_dwarf_read_u64(loc, maxLoc);
    /* 32-bit entry */
    } else {
        cie->length = entryLength32;
        cie->cieStart = *loc;
        cie->cieID = tinyunw_dwarf_read_u32(loc, maxLoc);
    }
    
    /* Version must be 1 (GCC .eh_frame) or 3 (DWARF 2) */
    cie->version = tinyunw_dwarf_read_u8(loc, maxLoc);
    if (cie->version != 0x01 && cie->version != 0x03) {
        return TINYUNW_EINVAL;
    }
    
    /* Read the augmentation string. Don't parse it yet. */
    char augstr[6] = { 0 }, c = 0;
    int i = 0;
    
    while ((c = tinyunw_dwarf_read_u8(loc, maxLoc)) != 0x00 && i < sizeof(augstr)) {
        augstr[i++] = c;
    }
    
    /* Code alignment, data alignment, return address register. */
    cie->codeAlignmentFactor = tinyunw_dwarf_read_uleb128(loc, maxLoc);
    cie->dataAlignmentFactor = tinyunw_dwarf_read_sleb128(loc, maxLoc);
    cie->returnAddressColumn = tinyunw_dwarf_read_u8(loc, maxLoc);
    
    /* Parse the augmentation string now. 'z' is only recognized as the first
       character of the string, but we notice it in the string loop for
       simplicity's sake. */
    if (augstr[0] == 'z') {
        cie->augmentationDataSize = tinyunw_dwarf_read_uleb128(loc, maxLoc);
    }
    for (i = 0; i < sizeof(augstr) && augstr[i]; ++i) {
        switch (augstr[i]) {
            case 'z':
                cie->hasAugmentationData = true;
                break;
            case 'P':
                cie->personalityRoutine = tinyunw_dwarf_read_encoded_pointer(loc, maxLoc, tinyunw_dwarf_read_u8(loc, maxLoc));
                break;
            case 'L':
                cie->lsdaEncoding = tinyunw_dwarf_read_u8(loc, maxLoc);
                break;
            case 'R':
                cie->pointerEncoding = tinyunw_dwarf_read_u8(loc, maxLoc);
                break;
            case 'S':
                cie->isSignalFrame = true;
                break;
            default:
                /* If we have the augmentation size, an unknown letter in the
                   string is okay. Otherwise, it's a hard error. */
                if (!cie->hasAugmentationData)
                    return TINYUNW_EINVAL;
                break;
        }
    }
    cie->initialInstructionsStart = *loc;
    cie->cieEnd = cie->cieStart + cie->length;
    return TINYUNW_ESUCCESS;
}

static tinyunw_dwarf_cie_t *tinyunw_dwarf_cie_at_location(tinyunw_dwarf_fde_list_t *list, uintptr_t location) {
    for (int i = 0; i < list->ncies; ++i) {
        if (list->cies[i].cieLocation == location)
            return &list->cies[i];
    }
    return NULL;
}
/**
  * @internal
  * Parse an FDE. It's already known to be an FDE, so don't recheck.
  */
int tinyunw_dwarf_parse_fde(uintptr_t *loc, uintptr_t baseLoc, uintptr_t maxLoc, bool isEHFrame, tinyunw_dwarf_fde_t *fde, tinyunw_dwarf_fde_list_t *list)
{
    memset(fde, 0, sizeof(tinyunw_dwarf_fde_t));
    fde->fdeLocation = *loc;
    
    uint32_t entryLength32 = tinyunw_dwarf_read_u32(loc, maxLoc);
    int64_t cieOffset = 0;
    uint64_t cieLocation = 0;
    
    /* An FDE of zero length marks the end of the FDE table. */
    /* 64-bit entry */
    if (entryLength32 == 0xFFFFFFFFUL) {
        fde->length = tinyunw_dwarf_read_u64(loc, maxLoc);
        if (fde->length == 0)
            return TINYUNW_ENOFRAME;
        fde->fdeStart = *loc;
        cieOffset = tinyunw_dwarf_read_s64(loc, maxLoc);
    /* 32-bit entry */
    } else {
        fde->length = entryLength32;
        if (fde->length == 0)
            return TINYUNW_ENOFRAME;
        fde->fdeStart = *loc;
        cieOffset = tinyunw_dwarf_read_s32(loc, maxLoc);
    }
    
    /* In a .eh_frame, the CIE offset is PC-relative, where PC is the current
       offset in the FDE. In a .debug_frame, the CIE offset is relative to
       the start of the section. */
    if (isEHFrame) {
        cieLocation = fde->fdeStart - cieOffset;
    } else {
        cieLocation = baseLoc + cieOffset;
    }
    
    if ((fde->cie = tinyunw_dwarf_cie_at_location(list, cieLocation)) == NULL) {
        return TINYUNW_EUNSPEC;
    }
    
    /* IP range is always an absolute value, but initial location is standard. */
    fde->initialLocation = tinyunw_dwarf_read_encoded_pointer(loc, maxLoc, fde->cie->pointerEncoding);
    fde->finalLocation = tinyunw_dwarf_read_encoded_pointer(loc, maxLoc, fde->cie->pointerEncoding & 0x0F) + fde->initialLocation;
    if (fde->cie->hasAugmentationData) {
        uint64_t augmentationLen = tinyunw_dwarf_read_uleb128(loc, maxLoc);
        uintptr_t augmentationEnd = *loc + augmentationLen, p = 0, saveLoc = *loc;
        
        if (fde->cie->lsdaEncoding != 0) {
            if ((p = tinyunw_dwarf_read_encoded_pointer(loc, maxLoc, fde->cie->lsdaEncoding & 0x0F)) != 0) {
                *loc = saveLoc;
                fde->lsdaStart = tinyunw_dwarf_read_encoded_pointer(loc, maxLoc, fde->cie->lsdaEncoding);
            }
        }
        *loc = augmentationEnd;
    }
    fde->instructionsStart = *loc;
    fde->fdeEnd = fde->fdeStart + fde->length;
    return TINYUNW_ESUCCESS;
}

/**
  * @internal
  * Parse a debug info frame (either .debug_frame or .eh_frame) into an FDE list.
  * The returned FDE list contains malloc()d memory, but can be accessed safely
  * at async signal time.
  */
int tinyunw_dwarf_parse_frame(uintptr_t loc, uintptr_t maxLoc, bool isEHFrame, tinyunw_dwarf_fde_list_t *list)
{
    uintptr_t p = loc;
    
    while (p < maxLoc) {
        bool isCIE;
        
        if (tinyunw_dwarf_get_entry_kind(p, maxLoc, isEHFrame, &isCIE)) {
            return TINYUNW_EUNSPEC;
        }
        
        if (isCIE) {
            tinyunw_dwarf_cie_t cie;
            
            if (tinyunw_dwarf_parse_cie(&p, maxLoc, isEHFrame, &cie)) {
                return TINYUNW_EUNSPEC;
            }
            tinyunw_dwarf_fde_list_add_cie(list, &cie);
            p = cie.cieEnd;
        } else {
            tinyunw_dwarf_fde_t fde;
            int result = tinyunw_dwarf_parse_fde(&p, loc, maxLoc, isEHFrame, &fde, list);
            
            if (result == TINYUNW_ENOFRAME) {
                break;
            } else if (result != TINYUNW_ESUCCESS) {
                return TINYUNW_EUNSPEC;
            }
            tinyunw_dwarf_fde_list_add_fde(list, &fde);
            p = fde.fdeEnd;
        }
    }
    return 0;
}

void tinyunw_dwarf_fde_list_free(tinyunw_dwarf_fde_list_t *list)
{
    if (list->ncies) {
        free(list->cies);
    }
    if (list->nfdes) {
        tinyunw_async_list_free(&list->fdeList);
        free(list->fdes);
    }
}

int tinyunw_dwarf_fde_list_add_cie(tinyunw_dwarf_fde_list_t *list, tinyunw_dwarf_cie_t *cie)
{
    if (list->ncies == list->cieCapacity) {
        list->cieCapacity = MAX(1, list->cieCapacity) * 2;
        list->cies = realloc(list->cies, list->cieCapacity * sizeof(tinyunw_dwarf_cie_t));
    }
    list->cies[list->ncies++] = *cie;
    return TINYUNW_ESUCCESS;
}

int tinyunw_dwarf_fde_list_add_fde(tinyunw_dwarf_fde_list_t *list, tinyunw_dwarf_fde_t *fde)
{
    if (list->nfdes == 0) {
        tinyunw_async_list_init(&list->fdeList);
    }
    if (list->nfdes == list->fdeCapacity) {
        list->fdeCapacity = MAX(1, list->fdeCapacity) * 2;
        list->fdes = realloc(list->fdes, list->fdeCapacity * sizeof(tinyunw_dwarf_fde_t));
    }
    
    int i = 0;
    
    /* Search the list for the nearest initialLocation. Optimize: Search backwards,
       as FDEs will most often be ordered within a DWARF frame. The insertion
       index is i + 1. */
    for (i = list->nfdes - 1; i >= 0; --i) {
        if (list->fdes[i].initialLocation < fde->initialLocation)
            break;
    }
    
    memmove(list->fdes + i + 2, list->fdes + i + 1, (list->nfdes - (i + 1)) * sizeof(tinyunw_dwarf_fde_t));
    list->fdes[i + 1] = *fde;
    list->nfdes++;
    tinyunw_async_list_append(&list->fdeList, &(list->fdes[i + 1]));
    return TINYUNW_ESUCCESS;
}

tinyunw_dwarf_fde_t *tinyunw_dwarf_fde_list_search(tinyunw_dwarf_fde_list_t *list, uintptr_t ip)
{
    for (int i = 0; i < list->nfdes; ++i) {
        if (ip >= list->fdes[i].initialLocation && ip <= list->fdes[i].finalLocation)
            return &list->fdes[i];
    }
    return NULL;
}

int tinyunw_dwarf_run_cfa_for_fde(tinyunw_dwarf_fde_t *fde, uintptr_t ip, tinyunw_dwarf_cfa_state_t *results)
{
    tinyunw_dwarf_cfa_state_t stateStack[16];
    int nstack = 0, result = 0;
    
    memset(stateStack, 0, sizeof(stateStack));
    result = tinyunw_dwarf_run_cfa_program(fde->cie, fde->cie->initialInstructionsStart, fde->cie->cieEnd, (uintptr_t)-1, stateStack, 16, &nstack);
    if (result == TINYUNW_ESUCCESS)
        result = tinyunw_dwarf_run_cfa_program(fde->cie, fde->instructionsStart, fde->fdeEnd, fde->initialLocation - ip, stateStack, 16, &nstack);
    if (result == TINYUNW_ESUCCESS)
        *results = stateStack[nstack];
    return result;
}

const char *tinyunw_dwarf_opname(tinyunw_word_t opcode) {
    switch (opcode) {
        #define OP(n)   case n: return #n
        OP(DW_CFA_nop);                 OP(DW_CFA_set_loc);             OP(DW_CFA_advance_loc1);    OP(DW_CFA_advance_loc2);
        OP(DW_CFA_offset);              OP(DW_CFA_restore);             OP(DW_CFA_advance_loc4);    OP(DW_CFA_offset_extended);
        OP(DW_CFA_restore_extended);    OP(DW_CFA_undefined);           OP(DW_CFA_same_value);      OP(DW_CFA_register);
        OP(DW_CFA_remember_state);      OP(DW_CFA_restore_state);       OP(DW_CFA_def_cfa);         OP(DW_CFA_def_cfa_register);
        OP(DW_CFA_def_cfa_offset);      OP(DW_CFA_def_cfa_expression);  OP(DW_CFA_expression);      OP(DW_CFA_offset_extended_sf);
        OP(DW_CFA_def_cfa_sf);          OP(DW_CFA_def_cfa_offset_sf);   OP(DW_CFA_val_offset);      OP(DW_CFA_val_offset_sf);
        OP(DW_CFA_val_expression);      OP(DW_CFA_lo_user);             OP(DW_CFA_GNU_window_save); OP(DW_CFA_GNU_args_size);
        OP(DW_CFA_hi_user);             OP(DW_CFA_GNU_negative_offset_extended);
        #undef OP
    }
    return "unknown";
}

/**
  * @internal
  * The results are placed on the top of the stack (stack[nstack]).
  */
#define TINYUNW_OP_DEBUG(msg, args...) TINYUNW_DEBUG("%s " msg, tinyunw_dwarf_opname(opcode), ## args)

int tinyunw_dwarf_run_cfa_program(tinyunw_dwarf_cie_t *cie, uintptr_t instrStart, uintptr_t instrEnd, uintptr_t ipLimit,
                                  tinyunw_dwarf_cfa_state_t *stack, int maxstack, int *nstack)
{
    uintptr_t ipCurrent = 0;
    tinyunw_dwarf_cfa_state_t initialState = stack[*nstack];

    TINYUNW_DEBUG("Starting CFA program at 0x%lx IP 0x%lx", instrStart, ipCurrent);
    
    while (instrStart < instrEnd && ipCurrent < ipLimit) {
        uint8_t opcode = tinyunw_dwarf_read_u8(&instrStart, instrEnd);
        tinyunw_word_t operand1 = 0, operand2 = 0;
        
        if ((opcode & DWARF_CFA_OPCODE_MASK) != 0) {
            operand1 = opcode & DWARF_CFA_OPERAND_MASK;
            opcode &= DWARF_CFA_OPCODE_MASK;
        }
        switch (opcode) {
            case DW_CFA_nop:
                TINYUNW_OP_DEBUG("");
                break;
            case DW_CFA_set_loc:
                ipCurrent = tinyunw_dwarf_read_encoded_pointer(&instrStart, instrEnd, cie->pointerEncoding);
                TINYUNW_OP_DEBUG("0x%lx", ipCurrent);
                break;
            case DW_CFA_advance_loc:
            case DW_CFA_advance_loc1:
            case DW_CFA_advance_loc2:
            case DW_CFA_advance_loc4:
                if (opcode != DW_CFA_advance_loc)
                    operand1 = tinyunw_dwarf_read_u(&instrStart, instrEnd, 1 << (opcode - 2));
                ipCurrent += operand1 * cie->codeAlignmentFactor;
				TINYUNW_OP_DEBUG("0x%llx ip = 0x%lx", operand1, ipCurrent);
                break;
            case DW_CFA_offset:
            case DW_CFA_offset_extended:
            case DW_CFA_offset_extended_sf:
            case DW_CFA_val_offset:
            case DW_CFA_val_offset_sf:
                if (opcode != DW_CFA_offset)
                    operand1 = tinyunw_dwarf_read_uleb128(&instrStart, instrEnd);
                operand2 = tinyunw_dwarf_read_leb128(&instrStart, instrEnd,
                    opcode == DW_CFA_offset_extended_sf || opcode == DW_CFA_val_offset_sf) * cie->dataAlignmentFactor;
                stack[*nstack].savedRegisters[operand1].saveLocation = (opcode == DW_CFA_val_offset || opcode == DW_CFA_val_offset_sf ?
                    TINYUNW_REG_OFFSET_CFA : TINYUNW_REG_CFA);
                stack[*nstack].savedRegisters[operand1].value = operand2;
                TINYUNW_OP_DEBUG("%llu = 0x%llx", operand1, operand2);
                break;
            case DW_CFA_restore:
            case DW_CFA_restore_extended:
                if (opcode != DW_CFA_restore)
                    operand1 = tinyunw_dwarf_read_uleb128(&instrStart, instrEnd);
                stack[*nstack].savedRegisters[operand1] = initialState.savedRegisters[operand1];
                TINYUNW_OP_DEBUG("%llu = 0x%llx @ %u", operand1, initialState.savedRegisters[operand1].value, initialState.savedRegisters[operand1].saveLocation);
                break;
            case DW_CFA_undefined:
            case DW_CFA_same_value:
                /* Per Apple's implementation, same_value is modeled identically to undefined. */
                operand1 = tinyunw_dwarf_read_uleb128(&instrStart, instrEnd);
                stack[*nstack].savedRegisters[operand1].saveLocation = TINYUNW_REG_UNUSED;
                TINYUNW_OP_DEBUG("%llu", operand1);
                break;
            case DW_CFA_register:
                operand1 = tinyunw_dwarf_read_uleb128(&instrStart, instrEnd);
                operand2 = tinyunw_dwarf_read_uleb128(&instrStart, instrEnd);
                stack[*nstack].savedRegisters[operand1].saveLocation = TINYUNW_REG_REG;
                stack[*nstack].savedRegisters[operand1].value = stack[*nstack].savedRegisters[operand2].value;
                TINYUNW_OP_DEBUG("%llu = %llu", operand1, operand2);
                break;
            case DW_CFA_remember_state:
                if (++*nstack >= maxstack)
                    return TINYUNW_ENOMEM;
                stack[*nstack] = stack[*nstack - 1];
                TINYUNW_OP_DEBUG("");
                break;
            case DW_CFA_restore_state:
                if (--*nstack < 0)
                    return TINYUNW_EBADFRAME;
                stack[*nstack] = stack[*nstack + 1];
                TINYUNW_OP_DEBUG("");
                break;
            case DW_CFA_def_cfa:
            case DW_CFA_def_cfa_sf:
                stack[*nstack].cfaRegister = tinyunw_dwarf_read_uleb128(&instrStart, instrEnd);
                stack[*nstack].cfaOffset = tinyunw_dwarf_read_leb128(&instrStart, instrEnd, opcode == DW_CFA_def_cfa_sf) *
                                            (opcode == DW_CFA_def_cfa_sf ? cie->dataAlignmentFactor : 1);
                TINYUNW_OP_DEBUG("%llu + %lld", stack[*nstack].cfaRegister, stack[*nstack].cfaOffset);
                break;
            case DW_CFA_def_cfa_register:
                stack[*nstack].cfaRegister = tinyunw_dwarf_read_uleb128(&instrStart, instrEnd);
                TINYUNW_OP_DEBUG("%llu", stack[*nstack].cfaRegister);
                break;
            case DW_CFA_def_cfa_offset:
            case DW_CFA_def_cfa_offset_sf:
                stack[*nstack].cfaOffset = tinyunw_dwarf_read_leb128(&instrStart, instrEnd, opcode == DW_CFA_def_cfa_sf) *
                                            (opcode == DW_CFA_def_cfa_sf ? cie->dataAlignmentFactor : 1);
                TINYUNW_OP_DEBUG("%lld", stack[*nstack].cfaOffset);
                break;
            case DW_CFA_def_cfa_expression:
                stack[*nstack].cfaRegister = 0;
                stack[*nstack].cfaExpression = instrStart;
                operand1 = tinyunw_dwarf_read_uleb128(&instrStart, instrEnd);
                instrStart += operand1;
                TINYUNW_OP_DEBUG("0x%lx-0x%lx", stack[*nstack].cfaExpression, instrStart);
                break;
            case DW_CFA_expression:
            case DW_CFA_val_expression:
                operand1 = tinyunw_dwarf_read_uleb128(&instrStart, instrEnd);
                stack[*nstack].savedRegisters[operand1].saveLocation = (opcode == DW_CFA_expression ? TINYUNW_REG_ATEXP : TINYUNW_REG_ISEXP);
                stack[*nstack].savedRegisters[operand1].value = instrStart;
                operand2 = tinyunw_dwarf_read_uleb128(&instrStart, instrEnd);
                instrStart += operand2;
                TINYUNW_OP_DEBUG("0x%llx-0x%lx", stack[*nstack].savedRegisters[operand1].value, instrStart);
                break;
            case DW_CFA_GNU_args_size: /* We don't currently use this value. */
                operand1 = tinyunw_dwarf_read_uleb128(&instrStart, instrEnd);
                TINYUNW_OP_DEBUG("%llu", operand1);
                break;
                TINYUNW_OP_DEBUG("");
                return TINYUNW_EINVAL;
            case DW_CFA_GNU_negative_offset_extended: /* Per comments in GCC, this opcode is only used by old PPC code. */
            case DW_CFA_GNU_window_save: /* This is a SPARC-specific opcode. */
            case DW_CFA_lo_user: /* Unused. */
            case DW_CFA_hi_user:
            default:
                TINYUNW_OP_DEBUG("");
                return TINYUNW_EINVAL;
        }
    }
    return TINYUNW_ESUCCESS;
}
