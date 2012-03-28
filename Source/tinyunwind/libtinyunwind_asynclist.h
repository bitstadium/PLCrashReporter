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
#import <libkern/OSAtomic.h>

typedef struct tinyunw_async_list_entry_t {
    /** The list data. This pointer is NOT considered owned by the entry. */
    void *data;

    /** The previous entry in the list, or NULL. */
    struct tinyunw_async_list_entry_t *prev;
    
    /** The next entry in the list, or NULL. */
    struct tinyunw_async_list_entry_t *next;
} tinyunw_async_list_entry_t;

typedef struct tinyunw_async_list {
    /** The lock used by writers. No lock is required for readers. */
    OSSpinLock write_lock;

    /** The head of the list, or NULL if the list is empty. Must only be used to iterate or delete entries. */
    tinyunw_async_list_entry_t *head;

    /** The tail of the list, or NULL if the list is empty. Must only be used to append new entries. */
    tinyunw_async_list_entry_t *tail;

    /** The list reference count. No nodes will be deallocated while the count is greater than 0. If the count
     * reaches 0, all nodes in the free list will be deallocated. */
    int32_t refcount;
} tinyunw_async_list_t;

/**
  * @note The async list routines do not take ownership of pointers. They will not
  * be released on removal from the list or list deallocation. You are responsible
  * for ensuring your pointers do not leak.
  */

void tinyunw_async_list_init (tinyunw_async_list_t *list);
void tinyunw_async_list_free (tinyunw_async_list_t *list);
void tinyunw_async_list_append (tinyunw_async_list_t *list, void *data);
void tinyunw_async_list_remove (tinyunw_async_list_t *list, void *data);
void tinyunw_async_list_setreading (tinyunw_async_list_t *list, bool enable);
tinyunw_async_list_entry_t *tinyunw_async_list_next (tinyunw_async_list_t *list, tinyunw_async_list_entry_t *current);
