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

#import <pthread.h>
#import <stdint.h>
#import <stdbool.h>
#import <unistd.h>
#import <mach/mach.h>
#import <mach-o/dyld.h>

#ifndef __TINYUNWIND_H__
#define __TINYUNWIND_H__

#if __cplusplus__
extern "C" {
#endif

/**
 * @defgroup tinyunwind Backtrace Frame Walker
 *
 * Implements a backtrace API intended specifically for use on x86_64. The API
 * is fully async safe, and may be called from any signal handler. Future expansion
 * into other platforms is possible.
 *
 * The API is modeled on that of the libunwind library.
 *
 * @{
 */

/**
 * @internal
 * A processor state context. It is safe and correct to pass an x86_thread_state64_t
 * anywhere a tinyunw_context_t is expected.
 */
#if defined(__x86_64__)
typedef x86_thread_state64_t tinyunw_context_t;
#elif defined(__i386__)
typedef x86_thread_state_t tinyunw_context_t;
#elif defined(__arm__)
typedef arm_thread_state_t tinyunw_context_t;
#elif defined(__ppc__)
typedef ppc_thread_state_t tinyunw_context_t;
#else
typedef uint8_t tinyunw_context_t;
#endif

/**
  * @internal
  * An unwinding cursor, encapsulating DWARF, compact unwind, and stack scan
  * state information. This is opaque; don't peek inside!
  */
typedef struct tinyunw_cursor_t {
    uint64_t opaque[120];
} tinyunw_cursor_t;

/**
  * @internal
  * Error codes returned by tinyunwind functions. This is rather shorter than
  * libunwind's list, because tinyunwind doesn't try to do as many things.
  */
enum {
    TINYUNW_ESUCCESS            = 0,            /* no error */
    TINYUNW_ENOFRAME            = 1,            /* no more frames to unwind */
    TINYUNW_EUNSPEC             = -6540,        /* unknown error */
    TINYUNW_ENOMEM              = -6541,        /* out of memory */
    TINYUNW_EBADREG             = -6542,        /* bad register number */
    TINYUNW_EINVALIDIP          = -6545,        /* invalid IP */
    TINYUNW_EBADFRAME           = -6546,        /* bad frame */
    TINYUNW_EINVAL              = -6547,        /* unsupported operation or bad value */
    TINYUNW_ENOINFO             = -6549,        /* no unwind info available */
};

/**
  * @internal
  * A list of registers that can be retrieved from a stack frame. Note that in
  * any stack frame past the first, only the instruction pointer is guaranteed
  * to be meaningful.
  */
enum {
    TINYUNW_X86_64_RAX =  0,
    TINYUNW_X86_64_RDX =  1,
    TINYUNW_X86_64_RCX =  2,
    TINYUNW_X86_64_RBX =  3,
    TINYUNW_X86_64_RSI =  4,
    TINYUNW_X86_64_RDI =  5,
    TINYUNW_X86_64_RBP =  6,
    TINYUNW_X86_64_RSP =  7,
    TINYUNW_X86_64_R8  =  8,
    TINYUNW_X86_64_R9  =  9,
    TINYUNW_X86_64_R10 = 10,
    TINYUNW_X86_64_R11 = 11,
    TINYUNW_X86_64_R12 = 12,
    TINYUNW_X86_64_R13 = 13,
    TINYUNW_X86_64_R14 = 14,
    TINYUNW_X86_64_R15 = 15,
    TINYUNW_X86_64_RIP = 16,
};
typedef int tinyunw_regnum_t;

/**
  * @internal
  * A type which is the size of both a word and a register.
  */
#if defined(__x86_64__)
typedef uint64_t tinyunw_word_t;
#elif defined(__i386__) || defined(__arm__) || defined(__ppc__)
typedef uint32_t tinyunw_word_t;
#endif

/**
  * @internal
  * Flags for use with tinyunw_step()
  */
enum {
    /** Don't try to parse DWARF information during this step. */
    TINYUNW_FLAG_NO_DWARF = (1 << 0),
    
    /** Don't try to parse compact unwind information during this step. */
    TINYUNW_FLAG_NO_COMPACT = (1 << 1),
    
    /** Don't attempt a stack scan during this step. */
    TINYUNW_FLAG_NO_STACKSCAN = (1 << 2),
    
    /** Do attempt to use RBP stack frames during this step. If this flag is set,
      * stack frames will be tried before stack scans. */
    TINYUNW_FLAG_TRY_FRAME_POINTER = (1 << 3),
};
typedef int tinyunw_flags_t;

/**
  * Set whether or not libtinyunwind tracks binary images loaded into the current
  * process. Tracking is off by default.
  * @param tracking_flag Whether or not to track binary images.
  *
  * @return Returns TINYUNW_ESUCCESS on success. There are no failure modes for
  * this function.
  *
  * @note Tracking Mach-O binary images is done via dyld notification callbacks.
  * Turning off tracking does not flush the list of any images that may have
  * already been tracked while tracking was on.
  *
  * @warning Without binary image tracking, libtinyunwind can not use DWARF info,
  * compact unwinding, or stack scanning to unwind a stack and must resort to
  * frame pointers.
  *
  * @warning This function is not async-signal safe, nor is it thread-safe.
  */
extern int tinyunw_setimagetracking (bool tracking_flag);

/**
  * Initialize an unwinding context based on the current thread.
  * @param context The context pointer to fill in.
  *
  * @return Returns TINYUNW_ESUCCESS on success. There are no failure modes for
  * this function.
  */
extern int tinyunw_getcontext (tinyunw_context_t *context);

/**
  * Initialize an unwinding context based on an arbitrary thread.
  * @param context The context pointer to fill in.
  * @param thread The thread to read processor state from.
  *
  * @return Returns TINYUNW_ESUCCESS on success, or TINYUNW_EBADFRAME if the
  * thread appears invalid.
  */
extern int tinyunw_getthreadcontext (tinyunw_context_t *context, thread_t thread);

/**
  * Initialize a cursor for unwinding based on the given context.
  * @param context The context to use for the cursor. This value is copied. It
  * is safe and correct to pass a straight x86_thread_state64_t here.
  * @param cursor A pointer to an opaque unwinding cursor. This cursor is valid
  * indefinitely.
  *
  * @return Returns TINYUNW_ESUCCESS on success, or TINYUNW_EINVAL if the context
  * appears invalid.
  */
extern int tinyunw_init_cursor (tinyunw_context_t *context, tinyunw_cursor_t *cursor);

/**
  * Step a cursor up its stack.
  * @param cursor The cursor to step.
  * @param flags The flags for this step operation.
  *
  * @return Returns TINYUNW_ESUCCESS if a new stack frame was successfully loaded
  * into the cursor. Returns TINYUNW_ENOFRAME if all methods used for stepping
  * the stack determined that the end of the stack was reached. Note that in the
  * case of TINYUNW_ENOFRAME, the current state of the cursor is unchanged: the
  * last frame to return TINYUNW_ESUCCESS was the top of the stack. This differs
  * from libunwind's behavior.
  */
extern int tinyunw_step (tinyunw_cursor_t *cursor, tinyunw_flags_t flags);

/**
  * Read a register from a cursor's current frame.
  * @param cursor The cursor to read from.
  * @param register The register to fetch.
  * @param value A pointer in which to place the value of the register. It is safe
  * to pass NULL.
  *
  * @return Returns TINYUNW_ESUCCESS on success or TINYUNW_EBADREG if the register
  * number passed is invalid.
  *
  * @note Values will be returned for every register in every frame, but RIP is
  * the only one guaranteed to be valid after the first frame. RBP may be valid
  * if frame pointers are being scanned, and other registers may or may not be
  * valid based on what debugging/unwinding information is available and used.
  */
extern int tinyunw_get_register (tinyunw_cursor_t *cursor, tinyunw_regnum_t regnum, tinyunw_word_t *value);

/**
  * Return the human-readable name of a register.
  * @param register The register for which the name should be returned.
  *
  * @return A static string containing the NULL-terminated name of the register.
  * This string is read-only and guaranteed to live forever.
  */
extern const char *tinyunw_register_name (tinyunw_regnum_t regnum);

/**
  * Get the name and starting address of a symbol based on an IP value.
  * @param ip The IP value to use to find the symbol.
  * @param start_address A pointer in which to place the starting address of the
  * symbol. It is safe to pass NULL to indicate you do not want this information.
  * @param name A pointer to a pointer to a character buffer in which the symbol
  * name is stored. This buffer must be considered read-only; it will typically
  * point to an area within a Mach-O image (however, that can not be assumed).
  *
  * @return Returns TINYUNW_ESUCCESS if the symbol was found and its information
  * returned. Returns TINYUNW_EINVALIDIP if the IP was not anywhere within any
  * loaded binary images. Returns TINYUNW_ENOINFO if the IP was valid, but symbol
  * table information is not available, or if the symbol was not found in the
  * availble symbol table.
  *
  * @note It is not necessary to have a tinyunw_context_t or tinyunw_cursor_t
  * set up in order to use this function.
  */
extern int          tinyunw_get_symbol_info(tinyunw_word_t ip, tinyunw_word_t *start_address, const char ** const name);

#if __cplusplus__
}
#endif


#endif

/**
  * @} tinyunwind
  */
