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
#import <mach-o/nlist.h>
#import <dlfcn.h>

#define TINYUNW_FETCH_REAL_CURSOR(param)		\
    tinyunw_real_cursor_t *cursor = (tinyunw_real_cursor_t *)(param)

bool tinyunw_tracking_images = false;
bool tinyunw_dyld_callbacks_installed = false;
tinyunw_async_list_t tinyunw_loaded_images_list;
uintptr_t tinyunw_start_symbol_start_address = 0, tinyunw_start_symbol_end_address = 0,
          tinyunw_thread_start_symbol_start_address = 0, tinyunw_thread_start_symbol_end_address = 0;

int tinyunw_read_unsafe_memory (const void *pointer, void *destination, size_t len) {
    vm_size_t read_size = len;
    
    return vm_read_overwrite(mach_task_self(), (vm_address_t)pointer, len, (pointer_t)destination, &read_size);
}

tinyunw_image_t *tinyunw_get_image_containing_address (uintptr_t address) {
#if defined(__x86_64__)
    /* Optimization: For 64-bit, the entire bottom 4GB of address space is known
       to be invalid on OS X. Immediately return NULL if the address is in that
       range. */
    if ((address & 0xFFFFFFFF00000000) == 0)
        return NULL;
#endif

    /* The global image list is invalid if the dyld callbacks haven't been
       installed yet (image tracking has never been activated). Without an
       image list, there's no way to figure out what image contains the address
       at async-signal safe time. */
    if (!tinyunw_dyld_callbacks_installed)
        return NULL;
    
    tinyunw_async_list_entry_t *entry = NULL;
    
    /* Loop over all loaded images, checking whether the address is within its
       VM range. */
    tinyunw_async_list_setreading(&tinyunw_loaded_images_list, true);
    while ((entry = tinyunw_async_list_next(&tinyunw_loaded_images_list, entry)) != NULL) {
        tinyunw_image_t *image = (tinyunw_image_t *)entry->data;
        if (address >= image->textSection.base && address <= image->textSection.end) {
            tinyunw_async_list_setreading(&tinyunw_loaded_images_list, false);
            return image;
        }
    }
    tinyunw_async_list_setreading(&tinyunw_loaded_images_list, false);
    return NULL;
}

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

static void tinyunw_lookup_start_symbols (void) {
    /* dylsm() can't look up the "start" symbol, so fake it using our own symbol
       search. */
    uintptr_t startSymbol = 0,
              threadStartSymbol = (uintptr_t)dlsym(RTLD_DEFAULT, "thread_start"),
              n = 0;
    Dl_info symInfo;
    
    /* This is extremely ridiculous and liable to be horrifyingly inefficient,
       and there needs to be a better way. */
    if (tinyunw_lookup_symbol("start", (void *)&startSymbol) == TINYUNW_ESUCCESS) {
        n = startSymbol;
        /* start() runs only 59 bytes in a normal executable, so limit the search. */
        while (dladdr((const void *)n, &symInfo) != 0 && (n - startSymbol < 0x200)) {
            if ((uintptr_t)symInfo.dli_saddr != startSymbol) {
                tinyunw_start_symbol_start_address = startSymbol;
                tinyunw_start_symbol_end_address = n - 1;
                break;
            }
            ++n;
        }
    }
    if (threadStartSymbol) {
        n = threadStartSymbol;
        /* thread_start(), by itself (not counting pthread_start()) is tiny, only 16 bytes. */
        while (dladdr((const void *)n, &symInfo) != 0 && (n - threadStartSymbol < 0x100)) {
            if ((uintptr_t)symInfo.dli_saddr != threadStartSymbol) {
                tinyunw_thread_start_symbol_start_address = threadStartSymbol;
                tinyunw_thread_start_symbol_end_address = n - 1;
                break;
            }
            ++n;
        }
    }
}

int tinyunw_setimagetracking (bool tracking_flag) {
    /* Is tracking requested and we are not already tracking? */
    if (tracking_flag && !tinyunw_tracking_images) {
        tinyunw_tracking_images = true;
        if (!tinyunw_dyld_callbacks_installed) {
            tinyunw_dyld_callbacks_installed = true;
            tinyunw_async_list_init(&tinyunw_loaded_images_list);
            _dyld_register_func_for_add_image(tinyunw_dyld_add_image);
            _dyld_register_func_for_remove_image(tinyunw_dyld_remove_image);
            /* There needs to be a better place for this. */
            tinyunw_lookup_start_symbols();
        }
    /* Is tracking not requested and we are still tracking? */
    } else if (!tracking_flag && tinyunw_tracking_images) {
        tinyunw_tracking_images = false;
        /* It is not possible to unregister the dyld callbacks. */
    }
    return TINYUNW_ESUCCESS;
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
    
    /* Never attempt to step past start() or thread_start(). */
    if ((cursor->current_context.__rip >= tinyunw_start_symbol_start_address && cursor->current_context.__rip <= tinyunw_start_symbol_end_address) ||
        (cursor->current_context.__rip >= tinyunw_thread_start_symbol_start_address && cursor->current_context.__rip <= tinyunw_thread_start_symbol_end_address) ||
        cursor->current_context.__rip == 0)
    {
        return TINYUNW_ENOFRAME;
    }
    
    /* Try compact unwinding info first. Same semantics as DWARF. */
    if (!(flags & TINYUNW_FLAG_NO_COMPACT)) {
        result = tinyunw_try_step_unwind(cursor);
        if (result == TINYUNW_ESUCCESS || (result != TINYUNW_ESUCCESS && result != TINYUNW_ENOINFO)) {
            return result;
        }
    }
    
    /* Next, try DWARF stepping. If it returns any error other than no info
       avaiable, return it immediately. DWARF can (usually) tell the difference
       between having no info to read and seeing a hard end of the call chain. */
    if (!(flags & TINYUNW_FLAG_NO_DWARF)) {
        result = tinyunw_try_step_dwarf(cursor);
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


/* These two functions are 100% identical; only the difference in types
   necessitates this annoying duplication. Since both 32-bit and 64-bit images
   can coexist, even if not in the same process, it's safest to accept both. */
static void tinyunw_search_symbols32 (struct nlist *symbols, uint32_t nsyms, tinyunw_word_t target, struct nlist **found_symbol) {
    for (uint32_t i = 0; i < nsyms; ++i) {
        /* The symbol must be defined in a section, and must not be a debugging entry. */
        if ((symbols[i].n_type & N_TYPE) != N_SECT || ((symbols[i].n_type & N_STAB) != 0)) {
            continue;
        }
        /* If we haven't already found a symbol and the address we want is
           greater than the symbol's value, save this symbol. */
        if (!*found_symbol && symbols[i].n_value <= target) {
            *found_symbol = &symbols[i];
        /* If we have found a symbol already, but if the address we want is
           greater than the current symbol's value and the current symbol is later
           than the last one found, the current one is a closer match. */
        } else if (*found_symbol && symbols[i].n_value <= target && ((*found_symbol)->n_value < symbols[i].n_value)) {
            *found_symbol = &symbols[i];
        }
    }
}

/* Code-compressed, but exactly identical to the above function. */
static void tinyunw_search_symbols64 (struct nlist_64 *symbols, uint32_t nsyms, tinyunw_word_t target, struct nlist_64 **found_symbol) {
    for (uint32_t i = 0; i < nsyms; ++i) {
        if ((symbols[i].n_type & N_TYPE) != N_SECT || ((symbols[i].n_type & N_STAB) != 0)) continue;
        if (!*found_symbol && symbols[i].n_value <= target)  *found_symbol = &symbols[i];
        else if (*found_symbol && symbols[i].n_value <= target && ((*found_symbol)->n_value < symbols[i].n_value)) *found_symbol = &symbols[i];
    }
}

/* Same idea as before. */
static struct nlist *tinyunw_search_symbol_names32 (struct nlist *symbols, uint32_t nsyms, uintptr_t strings, const char * const name) {
    for (uint32_t i = 0; i < nsyms; ++i) {
        /* The symbol must be defined in a section, and must not be a debugging entry. */
        if ((symbols[i].n_type & N_TYPE) != N_SECT || ((symbols[i].n_type & N_STAB) != 0)) {
            continue;
        }
        if (strcmp((const char * const)(strings + symbols[i].n_un.n_strx), name) == 0) {
            return &symbols[i];
        }
    }
    return NULL;
}

/* 64-bit version of above */
static struct nlist_64 *tinyunw_search_symbol_names64 (struct nlist_64 *symbols, uint32_t nsyms, uintptr_t strings, const char * const name) {
    for (uint32_t i = 0; i < nsyms; ++i) {
        if ((symbols[i].n_type & N_TYPE) != N_SECT || ((symbols[i].n_type & N_STAB) != 0)) continue;
        if (strcmp((const char * const)(strings + symbols[i].n_un.n_strx), name) == 0) return &symbols[i];
    }
    return NULL;
}

int tinyunw_lookup_symbol (const char * const name, tinyunw_word_t *start_address) {
    /* If we're not tracking images, we know we have no info. We're not even
       sure whether the IP is valid or not. Treat it as lacking info. */
    if (!tinyunw_tracking_images) {
        return TINYUNW_ENOINFO;
    }
    
    tinyunw_async_list_entry_t *entry = NULL;
    tinyunw_image_t *image = NULL;
    
    tinyunw_async_list_setreading(&tinyunw_loaded_images_list, true);
    while ((entry = tinyunw_async_list_next(&tinyunw_loaded_images_list, entry)) != NULL) {
        image = entry->data;
        
        /* If the image doesn't have a symbol table, we can't check it. We treat not
           having a string table identically, since a 64-bit image should always
           have both. We also need the dynamic symbol table information. */
        if (image->symbolTable.base == 0 || image->stringTable.base == 0 ||
            (image->symbolInformation.numGlobalSymbols == 0 && image->symbolInformation.numLocalSymbols == 0)) {
            tinyunw_async_list_setreading(&tinyunw_loaded_images_list, false);
            return TINYUNW_ENOINFO;
        }

        /* Loop through the symbol table, looking for the first symbol that matches
           the given name. Look at global symbols first, then local symbols. */
        if (image->is64Bit) {
            struct nlist_64 *found_symbol = NULL;
            struct nlist_64 *global_syms = (struct nlist_64 *)(image->symbolTable.base + image->symbolInformation.firstGlobalSymbol * sizeof(struct nlist_64)),
                            *local_syms = (struct nlist_64 *)(image->symbolTable.base + image->symbolInformation.firstLocalSymbol * sizeof(struct nlist_64));
            
            if ((found_symbol = tinyunw_search_symbol_names64(global_syms, image->symbolInformation.numGlobalSymbols, image->stringTable.base, name)) == NULL)
                found_symbol = tinyunw_search_symbol_names64(local_syms, image->symbolInformation.numLocalSymbols, image->stringTable.base, name);
            if (found_symbol) {
                if (start_address)
                    *start_address = found_symbol->n_value + image->vmaddrSlide;
                tinyunw_async_list_setreading(&tinyunw_loaded_images_list, false);
                return TINYUNW_ESUCCESS;
            }
        } else {
            struct nlist *found_symbol = NULL;
            struct nlist *global_syms = (struct nlist *)(image->symbolTable.base + image->symbolInformation.firstGlobalSymbol * sizeof(struct nlist)),
                         *local_syms = (struct nlist *)(image->symbolTable.base + image->symbolInformation.firstLocalSymbol * sizeof(struct nlist));
            
            if ((found_symbol = tinyunw_search_symbol_names32(global_syms, image->symbolInformation.numGlobalSymbols, image->stringTable.base, name)) == NULL)
                found_symbol = tinyunw_search_symbol_names32(local_syms, image->symbolInformation.numLocalSymbols, image->stringTable.base, name);
            if (found_symbol) {
                if (start_address)
                    *start_address = found_symbol->n_value + image->vmaddrSlide;
                tinyunw_async_list_setreading(&tinyunw_loaded_images_list, false);
                return TINYUNW_ESUCCESS;
            }
        }
    }
    tinyunw_async_list_setreading(&tinyunw_loaded_images_list, false);
    return TINYUNW_ENOINFO;
}

int tinyunw_get_symbol_info (tinyunw_word_t ip, tinyunw_word_t *start_address, const char ** const name) {
    /* If we're not tracking images, we know we have no info. We're not even
       sure whether the IP is valid or not. Treat it as lacking info. */
    if (!tinyunw_tracking_images) {
        return TINYUNW_ENOINFO;
    }
    
    tinyunw_image_t *image = tinyunw_get_image_containing_address(ip);
    
    /* If no image contains the IP, it's just flat invalid. */
    if (image == NULL) {
        return TINYUNW_EINVALIDIP;
    }
    
    /* If the image doesn't have a symbol table, we can't check it. We treat not
       having a string table identically, since a 64-bit image should always
       have both. We also need the dynamic symbol table information. */
    if (image->symbolTable.base == 0 || image->stringTable.base == 0 ||
        (image->symbolInformation.numGlobalSymbols == 0 && image->symbolInformation.numLocalSymbols == 0)) {
        return TINYUNW_ENOINFO;
    }
    
    /* Loop through the symbol table, looking for the first symbol that comes
       -after- the given IP. Look at global symbols first, then local symbols. */
    if (image->is64Bit) {
        struct nlist_64 *found_symbol = NULL;
        struct nlist_64 *global_syms = (struct nlist_64 *)(image->symbolTable.base + image->symbolInformation.firstGlobalSymbol * sizeof(struct nlist_64)),
                        *local_syms = (struct nlist_64 *)(image->symbolTable.base + image->symbolInformation.firstLocalSymbol * sizeof(struct nlist_64));
        
        tinyunw_search_symbols64(global_syms, image->symbolInformation.numGlobalSymbols, ip - image->vmaddrSlide, &found_symbol);
        tinyunw_search_symbols64(local_syms, image->symbolInformation.numLocalSymbols, ip - image->vmaddrSlide, &found_symbol);
        if (found_symbol)
        {
            if (start_address)
                *start_address = found_symbol->n_value + image->vmaddrSlide;
            if (name)
                *name = (const char * const)(image->stringTable.base + found_symbol->n_un.n_strx);
            return TINYUNW_ESUCCESS;
        }
    } else {
        struct nlist *found_symbol = NULL;
        struct nlist *global_syms = (struct nlist *)(image->symbolTable.base + image->symbolInformation.firstGlobalSymbol * sizeof(struct nlist)),
                     *local_syms = (struct nlist *)(image->symbolTable.base + image->symbolInformation.firstLocalSymbol * sizeof(struct nlist));
        
        tinyunw_search_symbols32(global_syms, image->symbolInformation.numGlobalSymbols, ip - image->vmaddrSlide, &found_symbol);
        tinyunw_search_symbols32(local_syms, image->symbolInformation.numLocalSymbols, ip - image->vmaddrSlide, &found_symbol);
        if (found_symbol)
        {
            if (start_address)
                *start_address = found_symbol->n_value + image->vmaddrSlide;
            if (name)
                *name = (const char * const)(image->stringTable.base + found_symbol->n_un.n_strx);
            return TINYUNW_ESUCCESS;
        }
    }
        
    return TINYUNW_ENOINFO;
}
