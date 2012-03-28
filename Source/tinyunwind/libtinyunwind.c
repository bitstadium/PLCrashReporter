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

#define TINYUNW_FETCH_REAL_CURSOR(param)		\
    tinyunw_real_cursor_t *cursor = (tinyunw_real_cursor_t *)(param)

bool tinyunw_tracking_images = false;
bool tinyunw_dyld_callbacks_installed = false;
tinyunw_async_list_t tinyunw_loaded_images_list;

int tinyunw_read_unsafe_memory(const void *pointer, void *destination, size_t len) {
    vm_size_t read_size = len;
    
    return vm_read_overwrite(mach_task_self(), (vm_address_t)pointer, len, (pointer_t)destination, &read_size);
}

tinyunw_image_t *tinyunw_get_image_containing_address(uintptr_t address) {
#if __x86_64__
    /* Optimization: The entire bottom 4GB of address space is known to be
       invalid on OS X. Immediately return NULL if the address is in that
       range. */
    if ((address & 0xFFFFFFFF00000000) == 0)
        return NULL;
    
    /* The global image list is invalid if the dyld callbacks haven't been
       installed yet (image tracking has never been activated). Without an
       image list, there's no way to figure out what image contains the address
       at async-signal safe time. */
    if (!tinyunw_dyld_callbacks_installed)
        return NULL;
    
    tinyunw_async_list_entry_t *entry = NULL;
    
    /* Loop over all loaded images, checking whether the address is within its
       VM range. Facilities for checking whether the address falls within a
       valid symbol are unsafe at async-signal time. */
    tinyunw_async_list_setreading(&tinyunw_loaded_images_list, true);
    while ((entry = tinyunw_async_list_next(&tinyunw_loaded_images_list, entry)) != NULL) {
        tinyunw_image_t *image = (tinyunw_image_t *)entry->data;
        if (address >= image->textSection.base && address <= image->textSection.end) {
            tinyunw_async_list_setreading(&tinyunw_loaded_images_list, false);
            return image;
        }
    }
    tinyunw_async_list_setreading(&tinyunw_loaded_images_list, false);
#endif
    return NULL;
}

#if __x86_64__
static void tinyunw_dyld_add_image (const struct mach_header *header, intptr_t vmaddr_slide) {
    if (tinyunw_tracking_images) {
        tinyunw_image_t *image = tinyunw_image_alloc();
        
        tinyunw_image_parse_from_header(image, (uintptr_t)header, vmaddr_slide);
        tinyunw_async_list_append(&tinyunw_loaded_images_list, image);
    }
}

static void tinyunw_dyld_remove_image (const struct mach_header *header, intptr_t vmaddr_slide) {
    /* Do NOT check here whether tracking is enabled. While failing to notice a
       newly added image is harmless, failing to notice a removed image may
       lead to crashes on attempts to read the image list. */
    tinyunw_async_list_remove_image_by_header(&tinyunw_loaded_images_list, (uintptr_t)header);
}
#endif

int tinyunw_setimagetracking (bool tracking_flag) {
#if __x86_64__
    /* Is tracking requested and we are not already tracking? */
    if (tracking_flag && !tinyunw_tracking_images) {
        tinyunw_tracking_images = true;
        if (!tinyunw_dyld_callbacks_installed) {
            tinyunw_dyld_callbacks_installed = true;
            tinyunw_async_list_init(&tinyunw_loaded_images_list);
            _dyld_register_func_for_add_image(tinyunw_dyld_add_image);
            _dyld_register_func_for_remove_image(tinyunw_dyld_remove_image);
        }
    /* Is tracking not requested and we are still tracking? */
    } else if (!tracking_flag && tinyunw_tracking_images) {
        tinyunw_tracking_images = false;
        /* It is not possible to unregister the dyld callbacks. */
    }
    return TINYUNW_ESUCCESS;
#else
    return TINYUNW_EUNSPEC;
#endif
}

int tinyunw_getthreadcontext (tinyunw_context_t *context, thread_t thread) {
#if __x86_64__
    kern_return_t kr;
    
    /* Fetch the thread states */
    mach_msg_type_number_t state_count;
    
    /* Sanity check */
    assert(sizeof(*context) == sizeof(x86_thread_state64_t));
    
    // thread state
    state_count = x86_THREAD_STATE64_COUNT;
    kr = thread_get_state(thread, x86_THREAD_STATE64, (thread_state_t)context, &state_count);
    if (kr != KERN_SUCCESS) {
        return TINYUNW_EBADFRAME;
    }
    
    return TINYUNW_ESUCCESS;
#else
    return TINYUNW_EUNSPEC;
#endif
}

int tinyunw_init_cursor (tinyunw_context_t *context, tinyunw_cursor_t *fake_cursor) {
#if __x86_64__
    TINYUNW_FETCH_REAL_CURSOR(fake_cursor);
    
    /* Basic context data. */
    cursor->original_context = *context;
    cursor->current_context = cursor->original_context;
    
    /* DWARF stepping data. */
    
    /* Compact unwind stepping data. */
    
    /* Frame pointer stepping. */
    cursor->fp[0] = NULL;
    cursor->fp[1] = NULL;
    
    /* Stack scan stepping. */
    cursor->last_stack_pointer = cursor->original_context.__rsp;
    
    return TINYUNW_ESUCCESS;
#else
    return TINYUNW_EUNSPEC;
#endif
}

int tinyunw_step (tinyunw_cursor_t *fake_cursor, tinyunw_flags_t flags) {
#if __x86_64__
    TINYUNW_FETCH_REAL_CURSOR(fake_cursor);
    
    int result = TINYUNW_ENOFRAME;
    
    /* Try DWARF stepping first. If it returns any error other than no info
       avaiable, return it immediately. DWARF can tell the difference between
       having no info to read and seeing a hard end of the call chain. */
    if (!(flags & TINYUNW_FLAG_NO_DWARF)) {
        result = tinyunw_try_step_dwarf(cursor);
        if (result == TINYUNW_ESUCCESS || (result != TINYUNW_ESUCCESS && result != TINYUNW_ENOINFO)) {
            return result;
        }
    }
    
    /* Next, try compact unwinding info. Same semantics as DWARF. */
    if (!(flags & TINYUNW_FLAG_NO_COMPACT)) {
        result = tinyunw_try_step_unwind(cursor);
        if (result == TINYUNW_ESUCCESS || (result != TINYUNW_ESUCCESS && result != TINYUNW_ENOINFO)) {
            return result;
        }
    }
    
    /* Now try frame pointers. */
    if ((flags & TINYUNW_FLAG_TRY_FRAME_POINTER)) {
        result = tinyunw_try_step_fp(cursor);
        if (result == TINYUNW_ESUCCESS || (result != TINYUNW_ESUCCESS && result != TINYUNW_ENOINFO)) {
            return result;
        }
    }
    
    /* If all else failed, try a stack scan. */
    if (!(flags & TINYUNW_FLAG_NO_STACKSCAN)) {
        result = tinyunw_try_step_stackscan(cursor);
        if (result == TINYUNW_ESUCCESS || (result != TINYUNW_ESUCCESS && result != TINYUNW_ENOINFO)) {
            return result;
        }
    }
    
    /* Everything failed (or possibly the client passed flags to disable all
       possible efforts. Return no frame. */
    return result;
#else
    return TINYUNW_EUNSPEC;
#endif
}

int tinyunw_get_register (tinyunw_cursor_t *fake_cursor, tinyunw_regnum_t regnum, tinyunw_word_t *value) {
#if __x86_64__
    TINYUNW_FETCH_REAL_CURSOR(fake_cursor);
    
    #define TINYUNW_REG(constant, field)	\
        case TINYUNW_X86_64_ ## constant:	\
            *value = cursor->current_context.__ ## field;	\
            break
    
    switch (regnum)
    {
        TINYUNW_REG(RAX, rax);
        TINYUNW_REG(RBX, rbx);
        TINYUNW_REG(RCX, rcx);
        TINYUNW_REG(RDX, rdx);
        TINYUNW_REG(RSI, rsi);
        TINYUNW_REG(RDI, rdi);
        TINYUNW_REG(RBP, rbp);
        TINYUNW_REG(RSP, rsp);
        TINYUNW_REG(R8, r8);
        TINYUNW_REG(R9, r9);
        TINYUNW_REG(R10, r10);
        TINYUNW_REG(R11, r11);
        TINYUNW_REG(R12, r12);
        TINYUNW_REG(R13, r13);
        TINYUNW_REG(R14, r14);
        TINYUNW_REG(R15, r15);
        TINYUNW_REG(RIP, rip);
        default:
            return TINYUNW_EBADREG;
    }
    return TINYUNW_ESUCCESS;
    
    #undef TINYUNW_REG
#else
    return TINYUNW_EUNSPEC;
#endif
}

const char *tinyunw_register_name (tinyunw_regnum_t regnum) {
#if __x86_64__
    #define TINYUNW_REGNAME(constant, name)	\
        case TINYUNW_X86_64_ ## constant:	\
            return #name
    
    switch (regnum)
    {
        TINYUNW_REGNAME(RAX, rax);
        TINYUNW_REGNAME(RBX, rbx);
        TINYUNW_REGNAME(RCX, rcx);
        TINYUNW_REGNAME(RDX, rdx);
        TINYUNW_REGNAME(RSI, rsi);
        TINYUNW_REGNAME(RDI, rdi);
        TINYUNW_REGNAME(RBP, rbp);
        TINYUNW_REGNAME(RSP, rsp);
        TINYUNW_REGNAME(R8, r9);
        TINYUNW_REGNAME(R9, r9);
        TINYUNW_REGNAME(R10, r10);
        TINYUNW_REGNAME(R11, r11);
        TINYUNW_REGNAME(R12, r12);
        TINYUNW_REGNAME(R13, r13);
        TINYUNW_REGNAME(R14, r14);
        TINYUNW_REGNAME(R15, r15);
        TINYUNW_REGNAME(RIP, rip);
    }
#endif
    return NULL;
}
