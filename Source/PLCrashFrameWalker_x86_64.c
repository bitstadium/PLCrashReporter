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


#import "PLCrashFrameWalker.h"
#import "PLCrashAsync.h"
#import "PLCrashAsyncImage.h"

#import <signal.h>
#import <assert.h>
#import <stdlib.h>

#ifdef __x86_64__

#import "libtinyunwind.h"

// PLFrameWalker API
plframe_error_t plframe_cursor_init (plframe_cursor_t *cursor, ucontext_t *uap, plcrash_async_image_list_t *image_list) {
    int result = 0;

    cursor->uap = uap;
    cursor->nframe = -1;
    cursor->fp[0] = NULL;
    
    result = tinyunw_init_cursor(&(uap->uc_mcontext->__ss), &cursor->unwind_cursor);
    return plframe_error_from_tinyunwerror(result);
}

// PLFrameWalker API
plframe_error_t plframe_cursor_thread_init (plframe_cursor_t *cursor, thread_t thread, plcrash_async_image_list_t *image_list) {
    kern_return_t kr;
    ucontext_t *uap;
    
    /*
        Note: This code has been left untouched when implementing libunwind(3)
        usage, as 1) Apple's implementation of libunwind on x86_64 doesn't
        handle floating-point and vector registers, 2) libunwind's general API
        doesn't provide access to some of the other information retrieved here.
    */
    
    /* Perform basic initialization */
    uap = &cursor->_uap_data;
    uap->uc_mcontext = (void *) &cursor->_mcontext_data;
    
    /* Zero the signal mask */
    sigemptyset(&uap->uc_sigmask);
    
    /* Fetch the thread states */
    mach_msg_type_number_t state_count;
    
    /* Sanity check */
    assert(sizeof(cursor->_mcontext_data.__ss) == sizeof(x86_thread_state64_t));
    assert(sizeof(cursor->_mcontext_data.__es) == sizeof(x86_exception_state64_t));
    assert(sizeof(cursor->_mcontext_data.__fs) == sizeof(x86_float_state64_t));
    
    // thread state
    state_count = x86_THREAD_STATE64_COUNT;
    kr = thread_get_state(thread, x86_THREAD_STATE64, (thread_state_t) &cursor->_mcontext_data.__ss, &state_count);
    if (kr != KERN_SUCCESS) {
        PLCF_DEBUG("Fetch of x86-64 thread state failed with mach error: %d", kr);
        return PLFRAME_INTERNAL;
    }
    
    // floating point state
    state_count = x86_FLOAT_STATE64_COUNT;
    kr = thread_get_state(thread, x86_FLOAT_STATE64, (thread_state_t) &cursor->_mcontext_data.__fs, &state_count);
    if (kr != KERN_SUCCESS) {
        PLCF_DEBUG("Fetch of x86-64 float state failed with mach error: %d", kr);
        return PLFRAME_INTERNAL;
    }
    
    // exception state
    state_count = x86_EXCEPTION_STATE64_COUNT;
    kr = thread_get_state(thread, x86_EXCEPTION_STATE64, (thread_state_t) &cursor->_mcontext_data.__es, &state_count);
    if (kr != KERN_SUCCESS) {
        PLCF_DEBUG("Fetch of x86-64 exception state failed with mach error: %d", kr);
        return PLFRAME_INTERNAL;
    }
    
    /* Perform standard initialization and return result */
    return plframe_cursor_init(cursor, uap, image_list);
}


// PLFrameWalker API
plframe_error_t plframe_cursor_next (plframe_cursor_t *cursor) {
    
    /* Must call plframe_cursor_next() at least once to get a valid frame, but
       libtinyunwind loads a valid frame immediately, so do nothing. */
    if (cursor->nframe == -1) {
        ++cursor->nframe;
        return PLFRAME_ESUCCESS;
    } else {
        int result;
        
        result = tinyunw_step(&cursor->unwind_cursor, 0);
        if (result == TINYUNW_ESUCCESS) {
            ++cursor->nframe;
            return PLFRAME_ESUCCESS;
        /* Treat having no unwind info the same as there being no frames left. */
        } else if (result == TINYUNW_ENOFRAME || result == TINYUNW_ENOINFO) {
            return PLFRAME_ENOFRAME;
        } else {
            return plframe_error_from_tinyunwerror(result);
        }
    }
    return PLFRAME_EUNKNOWN; /* should never get here */
}


// PLFrameWalker API
#define RETGEN(name, type, uap, result) {\
    *result = (uap->uc_mcontext->__ ## type . __ ## name); \
    return PLFRAME_ESUCCESS; \
}

plframe_error_t plframe_get_reg (plframe_cursor_t *cursor, plframe_regnum_t regnum, plframe_greg_t *reg) {
    ucontext_t *uap = cursor->uap;
    tinyunw_regnum_t unwreg;
    
    if (cursor->nframe != 0) {
        if (regnum == PLFRAME_X86_64_RIP) {
            tinyunw_get_register(&cursor->unwind_cursor, TINYUNW_X86_64_RIP, reg);
            return PLFRAME_ESUCCESS;
        }
        return PLFRAME_ENOTSUP;
    }
    
    #define MAP_REG(reg)		\
        case PLFRAME_X86_64_ ## reg:	\
            unwreg = TINYUNW_X86_64_ ## reg;	\
            break
    
    switch (regnum) {
        MAP_REG(RAX);
        MAP_REG(RBX);
        MAP_REG(RCX);
        MAP_REG(RDX);
        MAP_REG(RDI);
        MAP_REG(RSI);
        MAP_REG(RBP);
        MAP_REG(RSP);
        MAP_REG(R10);
        MAP_REG(R11);
        MAP_REG(R12);
        MAP_REG(R13);
        MAP_REG(R14);
        MAP_REG(R15);
        MAP_REG(RIP);
        
        /* These registers are not available through the libtinyunwind API, as
           they either can not be easily or safely read at async-signal time
           from the current thread, or have no meaning in x86_64 anyway. */
        case PLFRAME_X86_64_RFLAGS:
            RETGEN(rflags, ss, uap, reg);
            
        case PLFRAME_X86_64_CS:
            RETGEN(cs, ss, uap, reg);
            
        case PLFRAME_X86_64_FS:
            RETGEN(fs, ss, uap, reg);
            
        case PLFRAME_X86_64_GS:
            RETGEN(gs, ss, uap, reg);
        
        default:
            return PLFRAME_ENOTSUP;
    }
    
    #undef MAP_REG
    
    return plframe_error_from_tinyunwerror(tinyunw_get_register(&cursor->unwind_cursor, unwreg, reg));
}

#undef RETGEN


// PLFrameWalker API
plframe_error_t plframe_get_freg (plframe_cursor_t *cursor, plframe_regnum_t regnum, plframe_fpreg_t *fpreg) {
    return PLFRAME_ENOTSUP;
}

// PLFrameWalker API
const char *plframe_get_regname (plframe_regnum_t regnum) {
    switch (regnum) {
        case PLFRAME_X86_64_RAX:
            return "rax";

        case PLFRAME_X86_64_RBX:
            return "rbx";
            
        case PLFRAME_X86_64_RCX:
            return "rcx";
            
        case PLFRAME_X86_64_RDX:
            return "rdx";
            
        case PLFRAME_X86_64_RDI:
            return "rdi";
            
        case PLFRAME_X86_64_RSI:
            return "rsi";
            
        case PLFRAME_X86_64_RBP:
            return "rbp";
            
        case PLFRAME_X86_64_RSP:
            return "rsp";
            
        case PLFRAME_X86_64_R10:
            return "r10";
            
        case PLFRAME_X86_64_R11:
            return "r11";
            
        case PLFRAME_X86_64_R12:
            return "r12";
            
        case PLFRAME_X86_64_R13:
            return "r13";
            
        case PLFRAME_X86_64_R14:    
            return "r14";
            
        case PLFRAME_X86_64_R15:
            return "r15";
            
        case PLFRAME_X86_64_RIP:
            return "rip";
            
        case PLFRAME_X86_64_RFLAGS:
            return "rflags";
            
        case PLFRAME_X86_64_CS:
            return "cs";
            
        case PLFRAME_X86_64_FS:
            return "fs";
            
        case PLFRAME_X86_64_GS:
            return "gs";
            
        default:
            // Unsupported register
            break;
    }
    
    /* Unsupported register is an implementation error (checked in unit tests) */
    PLCF_DEBUG("Missing register name for register id: %d", regnum);
    abort();
}

// PLFrameWalker API
plframe_error_t plframe_get_symbol (plframe_cursor_t *cursor, plframe_greg_t *symstart, const char ** const symname)
{
	plframe_greg_t ip = 0;
    plframe_error_t err = PLFRAME_ESUCCESS;
    
    if ((err = plframe_get_reg(cursor, PLFRAME_X86_64_RIP, &ip)) != PLFRAME_ESUCCESS)
        return err;
    
    return plframe_error_from_tinyunwerror(tinyunw_get_symbol_info(ip, symstart, symname));
}

#endif
