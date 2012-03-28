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

#import <stdint.h>
#import <libkern/OSAtomic.h>
#import <stdbool.h>
#import <mach/mach.h>
#import "libtinyunwind_asynclist.h"

typedef struct tinyunw_image_piece {
    /* The first address in the "piece", adjusted for VM address slide. */
    uintptr_t base;
    
    /* The last valid address in the "piece", adjusted for VM address slide. */
    uintptr_t end;
    
    /* The length of the "piece". end = base + length. */
    size_t length;
} tinyunw_image_piece_t;

typedef struct tinyunw_image {
    /** The binary image's header address. */
    uintptr_t header;
    
    /** The binary image's VM address slide. */
    intptr_t vmaddrSlide;
    
    /** The binary image's path (may be NULL). */
    char *path;    

    /** The binary image's name (may be NULL). */
    char *name;
    
    /** The binary image's __TEXT segment. */
    tinyunw_image_piece_t textSegment;
    
    /** The binary image's __text section. */
    tinyunw_image_piece_t textSection;
    
    /** The binary image's __debug_frame section. */
    tinyunw_image_piece_t debugFrameSection;
    
    /** The binary image's __eh_frame section. */
    tinyunw_image_piece_t exceptionFrameSection;
    
    /** The binary image's __unwind_info section. */
    tinyunw_image_piece_t unwindInfoSection;
} tinyunw_image_t;

/**
  * @warning None of these routines are async-signal safe.
  */
tinyunw_image_t *tinyunw_image_alloc (void);
int tinyunw_image_parse_from_header (tinyunw_image_t *image, uintptr_t header, intptr_t vmaddr_slide);
void tinyunw_image_free (tinyunw_image_t *image);
void tinyunw_async_list_remove_image_by_header (tinyunw_async_list_t *list, uintptr_t header);
