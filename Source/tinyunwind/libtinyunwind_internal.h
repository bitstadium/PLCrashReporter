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

#import "libtinyunwind.h"
#import "libtinyunwind_image.h"
#import "libtinyunwind_asynclist.h"
#import <assert.h>
#import <stdio.h>
#import <unistd.h>

#define TINYUNW_DEBUG(msg, args...) {\
    char output[256];\
    snprintf(output, sizeof(output), "[tinyunwind] " msg "\n", ## args); \
    write(STDERR_FILENO, output, strlen(output));\
}

/**
  * The number of saved registers in DWARF for x86_64. Also the invalid register
  * indicator for compact unwind encoding.
  *
  * @note Apple's libunwind saves a whopping 120 registers. Memory usage much?
  */
enum { TINYUNW_SAVED_REGISTER_COUNT = 17 };

/**
  * @internal
  * The "real" tinyunwind cursor, containing all the nitty gritty details. Don't
  * play around inside this structure. No really, don't.
  */
struct tinyunw_real_cursor_t
{
    /** The original, unmodified, unstepped context used to build this cursor. */
    tinyunw_context_t original_context;
    
    /** The current, modified, stepped context representing this cursor's current
      * externally-visible state. Updated by all the various steppers.
      */
    tinyunw_context_t current_context;

    
    /** DWARF stepping data */
    

    /** Compact unwind info stepping data */
    

    /** Frame pointer stepping data */

    /** Stack frame data */
    void *fp[2];


    /** Stack scan stepping data */
    
    /** Saved stack pointer for stack scans */
    tinyunw_word_t last_stack_pointer;
};
typedef struct tinyunw_real_cursor_t tinyunw_real_cursor_t;

__private_extern__ bool tinyunw_tracking_images;
__private_extern__ bool tinyunw_dyld_callbacks_installed;
__private_extern__ tinyunw_async_list_t tinyunw_loaded_images_list;

static inline tinyunw_word_t tinyunw_getreg(tinyunw_context_t *context, tinyunw_word_t reg) {
#if __x86_64__
    #define GETREG(Ur, r) case TINYUNW_X86_64_ ## Ur: return context->__ ## r
    switch (reg) {
        GETREG(RAX, rax); GETREG(RBX, rbx); GETREG(RCX, rcx); GETREG(RDX, rdx);
        GETREG(RSI, rsi); GETREG(RDI, rdi); GETREG(RSP, rsp); GETREG(RBP, rbp);
        GETREG(R8,   r8); GETREG(R9,   r9); GETREG(R10, r10); GETREG(R11, r11);
        GETREG(R12, r12); GETREG(R13, r13); GETREG(R14, r14); GETREG(R15, r15);
        GETREG(RIP, rip);
    }
    #undef GETREG
#endif
    return 0;
}

static inline void tinyunw_setreg(tinyunw_context_t *context, tinyunw_word_t reg, tinyunw_word_t value) {
#if __x86_64__
    #define SETREG(Ur, r) case TINYUNW_X86_64_ ## Ur: context->__ ## r = value; break
    switch (reg) {
        SETREG(RAX, rax); SETREG(RBX, rbx); SETREG(RCX, rcx); SETREG(RDX, rdx);
        SETREG(RSI, rsi); SETREG(RDI, rdi); SETREG(RSP, rsp); SETREG(RBP, rbp);
        SETREG(R8,   r8); SETREG(R9,   r9); SETREG(R10, r10); SETREG(R11, r11);
        SETREG(R12, r12); SETREG(R13, r13); SETREG(R14, r14); SETREG(R15, r15);
        SETREG(RIP, rip);
        default: /* ignore */
            break;
    }
    #undef SETREG
#endif
}

/**
 * @internal
 * Read memory without causing access violations.
 */
int tinyunw_read_unsafe_memory(const void *pointer, void *destination, size_t len);

/**
 * @internal
 * Try various methods of stepping through a stack.
 */
int tinyunw_try_step_dwarf(tinyunw_real_cursor_t *cursor);
int tinyunw_try_step_unwind(tinyunw_real_cursor_t *cursor);
int tinyunw_try_step_fp(tinyunw_real_cursor_t *cursor);
int tinyunw_try_step_stackscan(tinyunw_real_cursor_t *cursor);

/**
 * @internal
 * Return the image containing the given address, if any.
 */
tinyunw_image_t *tinyunw_get_image_containing_address(uintptr_t address);
