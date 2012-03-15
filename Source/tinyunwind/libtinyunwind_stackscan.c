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

#if __x86_64__
static bool tinyunw_address_looks_valid (uintptr_t address)
{
    /* Optimization: The entire bottom 4GB of address space is known to be
       invalid on OS X. Immediately return false if the address is in that
       range. */
    if ((address & 0xFFFFFFFF00000000) == 0)
        return false;
    
    /* The global image list is invalid if the dyld callbacks haven't been
       installed yet (image tracking has never been activated). Without an
       image list, the only way to guess at an address' validity is whether or
       not it's within the process' address space, and that's not good enough
       for checking stack data. */
    if (!tinyunw_dyld_callbacks_installed)
        return false;
    
    tinyunw_image_entry_t *entry = NULL;
    
    /* Loop over all loaded images, checking whether the address is within its
       VM range. Facilities for checking whether the address falls within a
       valid symbol are unsafe at async-signal time. */
    tinyunw_image_list_setreading(&tinyunw_loaded_images_list, true);
    while ((entry = tinyunw_image_list_next(&tinyunw_loaded_images_list, entry)) != NULL) {
        if (address >= entry->image.textSection.base && address <= entry->image.textSection.end) {
            tinyunw_image_list_setreading(&tinyunw_loaded_images_list, false);
            return true;
        }
    }
    tinyunw_image_list_setreading(&tinyunw_loaded_images_list, false);
    return false;
}
#endif

int			tinyunw_try_step_stackscan (tinyunw_real_cursor_t *cursor)
{
#if __x86_64__
    const size_t search_space = 50;
    
    /* In the future, also restrict the search to the size of the stack itself
       instead of relying on safe memory reads, as there's no guarantee that
       there's no page mapped immediately above the stack */
    for (tinyunw_word_t loc = cursor->last_stack_pointer;
         loc <= cursor->last_stack_pointer + (search_space * sizeof(tinyunw_word_t));
         loc += sizeof(tinyunw_word_t))
    {
        tinyunw_word_t data;
        
        if (tinyunw_read_unsafe_memory((const void *)loc, &data, sizeof(data)) != KERN_SUCCESS) {
            /* ran off the end of the stack; treat it as no more frames */
            return TINYUNW_ENOFRAME;
        } else if (tinyunw_address_looks_valid(data)) {
            /* This is a valid address in some loaded address space. Cross
               fingers and hope, because that's all the checks we can do at
               async signal time. Record the address, advance the saved stack
               pointer, and return success. */
            cursor->last_stack_pointer = loc + sizeof(tinyunw_word_t);
            cursor->current_context.__rip = data;
            return TINYUNW_ESUCCESS;
        }
    }
    /* Nothing found within search_space words on the stack, give up. */
    return TINYUNW_ENOFRAME;
#else
    return TINYUNW_EUNSPEC;
#endif
}
