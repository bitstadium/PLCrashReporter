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

#import "libtinyunwind_asynclist.h"
#import <stdlib.h>

/**
 * Initialize a new async list and issue a memory barrier
 *
 * @param list The list structure to be initialized.
 *
 * @warning This method is not async safe.
 */
void tinyunw_async_list_init (tinyunw_async_list_t *list) {
    memset(list, 0, sizeof(*list));
    list->write_lock = OS_SPINLOCK_INIT;
}

/**
 * Free any binary async list resources.
 *
 * @warning This method is not async safe.
 */
void tinyunw_async_list_free (tinyunw_async_list_t *list) {
    tinyunw_async_list_entry_t *next = list->head;
    while (next != NULL) {
        /* Save the current pointer and fetch the next pointer. */
        tinyunw_async_list_entry_t *cur = next;
        next = cur->next;
        free(cur);
    }
}

/**
 * Append a new data entry to @a list.
 *
 * @param list The list to which the async record should be appended.
 * @param data The data pointer. This pointer is not owned by the list.
 *
 * @warning This method is not async safe.
 */
void tinyunw_async_list_append (tinyunw_async_list_t *list, void *data) {
    /* Initialize the new entry. */
    tinyunw_async_list_entry_t *new = calloc(1, sizeof(tinyunw_async_list_entry_t));
    new->data = data;
    
    /* Update the entry and issue a memory barrier to ensure a consistent view. */
    OSMemoryBarrier();
    
    /* Lock the list from other writers. */
    OSSpinLockLock(&list->write_lock); {

        /* If this is the first entry, initialize the list. */
        if (list->tail == NULL) {

            /* Update the list tail. This need not be done atomically, as tail is never accessed by a lockless reader. */
            list->tail = new;

            /* Atomically update the list head; this will be iterated upon by lockless readers. */
            if (!OSAtomicCompareAndSwapPtrBarrier(NULL, new, (void **) (&list->head))) {
                /* Should never occur */
            }
        }
        
        /* Otherwise, append to the end of the list */
        else {
            /* Atomically slot the new record into place; this may be iterated on by a lockless reader. */
            if (!OSAtomicCompareAndSwapPtrBarrier(NULL, new, (void **) (&list->tail->next))) {
            }

            /* Update the prev and tail pointers. This is never accessed without a lock, so no additional barrier
             * is required here. */
            new->prev = list->tail;
            list->tail = new;
        }
    } OSSpinLockUnlock(&list->write_lock);
}

/**
 * Remove a data entry from @a list.
 *
 * @param data The data pointer to search for and remove.
 *
 * @warning This method is not async safe.
 */
void tinyunw_async_list_remove (tinyunw_async_list_t *list, void *data) {
    /* Lock the list from other writers. */
    OSSpinLockLock(&list->write_lock); {
        /* Find the record. */
        tinyunw_async_list_entry_t *item = list->head;
        while (item != NULL) {
            if (item->data == data)
                break;

            item = item->next;
        }
        
        /* If not found, nothing to do */
        if (item == NULL) {
            OSSpinLockUnlock(&list->write_lock);
            return;
        }

        /*
         * Atomically make the item unreachable by readers.
         *
         * This serves as a synchronization point -- after the CAS, the item is no longer reachable via the list.
         */
        if (item == list->head) {
            if (!OSAtomicCompareAndSwapPtrBarrier(item, item->next, (void **) &list->head)) {
            }
        } else {
            /* There MUST be a non-NULL prev pointer, as this is not HEAD. */
            if (!OSAtomicCompareAndSwapPtrBarrier(item, item->next, (void **) &item->prev->next)) {
            }
        }
        
        /* Now that the item is unreachable, update the prev/tail pointers. These are never accessed without a lock,
         * and need not be updated atomically. */
        if (item->next != NULL) {
            /* Item is not the tail (otherwise next would be NULL), so simply update the next item's prev pointer. */
            item->next->prev = item->prev;
        } else {
            /* Item is the tail (next is NULL). Simply update the tail record. */
            list->tail = item->prev;
        }

        /* If a reader is active, simply spin until inactive. */
        while (list->refcount > 0) {
        }

        free(item);
    } OSSpinLockUnlock(&list->write_lock);
}

/**
 * Retain or release the list for reading. This method is async-safe.
 *
 * This must be issued prior to attempting to iterate the list, and must called again once reads have completed.
 *
 * @param list The list to be be retained or released for reading.
 * @param enable If true, the list will be retained. If false, released.
 */
void tinyunw_async_list_setreading (tinyunw_async_list_t *list, bool enable) {
    if (enable) {
        /* Increment and issue a barrier. Once issued, no items will be deallocated while a reference is held. */
        OSAtomicIncrement32Barrier(&list->refcount);
    } else {
        /* Increment and issue a barrier. Once issued, items may again be deallocated. */
        OSAtomicDecrement32Barrier(&list->refcount);
    }
}

/**
 * Return the next entry. This method is async-safe. If no additional entries are available, will return NULL;
 *
 * @param list The list to be iterated.
 * @param current The current entry, or NULL to start iteration.
 */
tinyunw_async_list_entry_t *tinyunw_async_list_next (tinyunw_async_list_t *list, tinyunw_async_list_entry_t *current) {
    if (current != NULL)
        return current->next;

    return list->head;
}
