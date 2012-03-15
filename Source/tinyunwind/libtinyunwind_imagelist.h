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

#import "libtinyunwind_image.h"

struct tinyunw_image_entry_t {
    /** The image data */
    tinyunw_image_t image;

    /** The previous image in the list, or NULL. */
    struct tinyunw_image_entry_t *prev;
    
    /** The next image in the list, or NULL. */
    struct tinyunw_image_entry_t *next;
};
typedef struct tinyunw_image_entry_t tinyunw_image_entry_t;

struct tinyunw_image_list_t {
    /** The lock used by writers. No lock is required for readers. */
    OSSpinLock write_lock;

    /** The head of the list, or NULL if the list is empty. Must only be used to iterate or delete entries. */
    tinyunw_image_entry_t *head;

    /** The tail of the list, or NULL if the list is empty. Must only be used to append new entries. */
    tinyunw_image_entry_t *tail;

    /** The list reference count. No nodes will be deallocated while the count is greater than 0. If the count
     * reaches 0, all nodes in the free list will be deallocated. */
    int32_t refcount;
};
typedef struct tinyunw_image_list_t tinyunw_image_list_t;

void tinyunw_image_list_init (tinyunw_image_list_t *list);
void tinyunw_image_list_free (tinyunw_image_list_t *list);
void tinyunw_image_list_append (tinyunw_image_list_t *list, tinyunw_image_t *image);
void tinyunw_image_list_remove (tinyunw_image_list_t *list, uintptr_t header);
void tinyunw_image_list_setreading (tinyunw_image_list_t *list, bool enable);
tinyunw_image_entry_t *tinyunw_image_list_next(tinyunw_image_list_t *list, tinyunw_image_entry_t *current);
