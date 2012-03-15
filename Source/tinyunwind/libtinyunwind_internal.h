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
#import "libtinyunwind_imagelist.h"
#import <assert.h>

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
__private_extern__ tinyunw_image_list_t tinyunw_loaded_images_list;

int tinyunw_read_unsafe_memory(const void *pointer, void *destination, size_t len);
int tinyunw_try_step_dwarf(tinyunw_real_cursor_t *cursor);
int tinyunw_try_step_unwind(tinyunw_real_cursor_t *cursor);
int tinyunw_try_step_fp(tinyunw_real_cursor_t *cursor);
int tinyunw_try_step_stackscan(tinyunw_real_cursor_t *cursor);
