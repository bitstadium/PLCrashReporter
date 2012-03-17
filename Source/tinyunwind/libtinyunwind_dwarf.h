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

#import <stdint.h>
#import <libkern/OSAtomic.h>
#import <stdbool.h>
#import <mach/mach.h>
#import "libtinyunwind_asynclist.h"

/**
  * @defgroup tinyunwind_dwarf DWARF parsing routines for libtinyunwind.
  *
  * @{
  */

/**
  * A parsed Common Information Entry.
  *
  * DWARF data may be in 32 or 64 bits in the file; for simplicity, all data
  * is upconverted to 64-bit in parsed structures.
  */
struct tinyunw_dwarf_cie_t {
    /** The CIE's raw offset within the debug information section. */
    uintptr_t cieLocation;
    
    /** The CIE's starting and ending offset within the debug info section, minus length. */
    uintptr_t cieStart, cieEnd;
    
    /** The CIE's length, not counting the length field. This is the parsed
      * length value, not the raw length bytes. */
    size_t length;
    
    /** The CIE ID. Not particularly useful, included for completeness. */
    uint64_t cieID;
    
    /** The CIE version. Either 1 (GCC) or 3 (DWARF 2 spec). */
    uint8_t version;
    
    /** Flag whether FDEs based on this CIE have augmentation data. */
    bool hasAugmentationData;
    
    /** Augmentation data size, if hasAugmentationData is true. */
    uint64_t augmentationDataSize;
    
    /** Personality routine pointer, if any. */
    uintptr_t personalityRoutine;
    
    /** Language-Specific Data Area encoding. */
    uint8_t lsdaEncoding;
    
    /** Pointer encoding format for FDEs. */
    uint8_t pointerEncoding;
    
    /** Signal frame flag. */
    bool isSignalFrame;
    
    /** Code alignment factor. Factored out of advance location instructions. */
    uint64_t codeAlignmentFactor;
    
    /** Data alignment factor. Factored out of offset instructions. */
    int64_t dataAlignmentFactor;
    
    /** Return address register column. */
    uint8_t returnAddressColumn;
    
    /** The raw offset in the debug info section to the initial instructions.
      * This is not a set of parsed instructions because that is only done at
      * DWARF step time. This structure is meant to reduce memory allocation,
      * not encapsulate all available information. */
    uintptr_t initialInstructionsStart;
};
typedef struct tinyunw_dwarf_cie_t tinyunw_dwarf_cie_t;

/**
  * A parsed Frame Descriptor Entry.
  */
struct tinyunw_dwarf_fde_t {
    /** The FDE's raw offset within the debug information section. */
    uintptr_t fdeLocation;
    
    /** The FDE's starting and ending offset within the debug info section, minus length. */
    uintptr_t fdeStart, fdeEnd;
    
    /** The FDE's length. Same notation as CIE length. */
    size_t length;
    
    /** The CIE associated with this FDE. */
    tinyunw_dwarf_cie_t *cie;
    
    /** The absolute IP of the first instruction this FDE refers to. */
    uintptr_t initialLocation;
    
    /** The absolute IP of the last instruction this FDE refers to. */
    uintptr_t finalLocation;
    
    /** The raw offset in the debug info section to this FDE's instructions.
      * See notation for tinyunw_dwarf_cie_t.initialInstructionsStart.
      */
    uintptr_t instructionsStart;
    
    /** The offset in the debug info section to the FDE's LSDA. */
    uintptr_t lsdaStart;
};
typedef struct tinyunw_dwarf_fde_t tinyunw_dwarf_fde_t;

/**
  * A list of parsed FDEs, including associated CIE lookup table.
  */
struct tinyunw_dwarf_fde_list_t {
    /** The number of CIEs in thist list. */
    int ncies;
    
    /** The number of allocated spaces for CIEs in this list. Only used during parsing. */
    int cieCapacity;
    
    /** The data storage area for CIEs. */
    tinyunw_dwarf_cie_t *cies;
    
    /** The number of FDEs in this list. */
    int nfdes;
    
    /** The number of allocated spaces for FDEs in this list. Only used during parsing. */
    int fdeCapacity;
    
    /** The data storage area for FDEs. */
    tinyunw_dwarf_fde_t *fdes;
    
    /** The async-safe FDE list. */
    tinyunw_async_list_t fdeList;
};
typedef struct tinyunw_dwarf_fde_list_t tinyunw_dwarf_fde_list_t;

/**
  * The number of saved registers in DWARF for x86_64.
  *
  * @note Apple's libunwind saves a whopping 120 registers. Memory usage much?
  */
enum { TINYUNW_SAVED_REGISTER_COUNT = 17 };

/**
  * The location of a register value in a CFA state.
  */
enum {
    TINYUNW_REG_UNUSED = 0,
    TINYUNW_REG_CFA,
    TINYUNW_REG_OFFSET_CFA,
    TINYUNW_REG_REG,
    TINYUNW_REG_ATEXP,
    TINYUNW_REG_ISEXP
};

/**
  * A saved state obtained from running a DWARF CFA program.
  */
struct tinyunw_dwarf_cfa_state_t {
    /** The saved registers. */
    struct tinyunw_dwarf_saved_register_t {

        /** The location from which the register value is obtained. */
        int saveLocation;
        
        /** The actual register value. */
        tinyunw_word_t value;
    } savedRegisters[TINYUNW_SAVED_REGISTER_COUNT];
    
    /** The CFA register. */
    tinyunw_word_t cfaRegister;
    int64_t cfaOffset;
    uintptr_t cfaExpression;
};
typedef struct tinyunw_dwarf_cfa_state_t tinyunw_dwarf_cfa_state_t;


/** General parsing routines. */

/**
  * @internal
  * Parse a debug info frame (either .debug_frame or .eh_frame) into an FDE list.
  * The returned FDE list contains malloc()d memory, but can be accessed safely
  * at async signal time.
  */
int tinyunw_dwarf_parse_frame(uintptr_t loc, uintptr_t maxLoc, bool isEHFrame, tinyunw_dwarf_fde_list_t *list);


/** FDE list management routines. */

/**
  * @internal
  * Free the memory used by the FDE list. Does not free the list pointer itself!
  */
void tinyunw_dwarf_fde_list_free(tinyunw_dwarf_fde_list_t *list);
int tinyunw_dwarf_fde_list_add_cie(tinyunw_dwarf_fde_list_t *list, tinyunw_dwarf_cie_t *cie);
/**
  * @internal
  * FDEs are added to the list sorted by initialLocation. This can be an expensive
  * operation if entries need to be shuffled around.
  */
int tinyunw_dwarf_fde_list_add_fde(tinyunw_dwarf_fde_list_t *list, tinyunw_dwarf_fde_t *fde);
/**
  * @internal
  * Does a linear search of the ordered FDE list for the first entry containing
  * the given address. This routine is async-signal safe. Because it must iterate
  * an async-signal safe list, it can not do a more efficient search.
  */
tinyunw_dwarf_fde_t *tinyunw_dwarf_fde_list_search(tinyunw_dwarf_fde_list_t *list, uintptr_t ip);


/** CFA program routines. */

int tinyunw_dwarf_run_cfa_for_fde(tinyunw_dwarf_fde_t *fde, uintptr_t ip, tinyunw_dwarf_cfa_state_t *results);

/**
  * @internal
  * The results are placed on the top of the stack (stack[nstack]).
  */
int tinyunw_dwarf_run_cfa_program(tinyunw_dwarf_cie_t *cie, uintptr_t instrStart, uintptr_t instrEnd, uintptr_t ipLimit,
                                  tinyunw_dwarf_cfa_state_t *stack, int maxstack, int *nstack);
