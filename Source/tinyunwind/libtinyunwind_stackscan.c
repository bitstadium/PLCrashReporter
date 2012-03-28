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

int tinyunw_try_step_stackscan (tinyunw_real_cursor_t *cursor) {
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
        } else if (tinyunw_get_image_containing_address(data)) {
            /* This is a valid address in some loaded address space, we don't
               care which from here. Cross fingers and hope, because that's all
               the checks we can do at async signal time. Record the address,
               advance the saved stack pointer, update rbp with our best guess
               to give future frame pointer checks a chance, and return success. */
            //TINYUNW_DEBUG("Stack scan found valid-looking address 0x%llx", data);
            cursor->last_stack_pointer = loc + sizeof(tinyunw_word_t);
            cursor->current_context.__rip = data;
            tinyunw_read_unsafe_memory((const void *)(loc - sizeof(tinyunw_word_t)), &cursor->current_context.__rbp, sizeof(tinyunw_word_t));
            return TINYUNW_ESUCCESS;
        }
    }
    /* Nothing found within search_space words on the stack, give up. */
#endif
    return TINYUNW_ENOINFO;
}
