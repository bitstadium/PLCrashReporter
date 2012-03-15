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

struct tinyunw_image_piece_t {
    /* The first address in the "piece", adjusted for VM address slide. */
    uintptr_t base;
    
    /* The last valid address in the "piece", adjusted for VM address slide. */
    uintptr_t end;
    
    /* The length of the "piece". end = base + length. */
    size_t length;
};
typedef struct tinyunw_image_piece_t tinyunw_image_piece_t;

struct tinyunw_image_t {
    /** The binary image's header address. */
    uintptr_t header;
    
    /** The binary image's VM address slide. */
    intptr_t vmaddrSlide;
    
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
};
typedef struct tinyunw_image_t tinyunw_image_t;

/**
  * @warning Not async-signal safe.
  */
int tinyunw_make_image_from_header(tinyunw_image_t *image, uintptr_t header, intptr_t vmaddr_slide);
