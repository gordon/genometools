/*
  Copyright (c) 2007 Gordon Gremme <gremme@zbh.uni-hamburg.de>
  Copyright (c) 2007 Center for Bioinformatics, University of Hamburg

  Permission to use, copy, modify, and distribute this software for any
  purpose with or without fee is hereby granted, provided that the above
  copyright notice and this permission notice appear in all copies.

  THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
  WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
  MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
  ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
  WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
  ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
  OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
*/

#ifndef MA_H
#define MA_H

#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include "core/error.h"

/* the memory allocator module */

void          gt_ma_init(bool bookkeeping);
#define       gt_malloc(size)\
              gt_malloc_mem(size, __FILE__, __LINE__)
void*         gt_malloc_mem(size_t size, const char*, int);
#define       gt_calloc(nmemb, size)\
              gt_calloc_mem(nmemb, size, __FILE__, __LINE__)
void*         gt_calloc_mem(size_t nmemb, size_t size, const char*, int);
#define       gt_realloc(ptr, size)\
              gt_realloc_mem(ptr, size, __FILE__, __LINE__)
void*         gt_realloc_mem(void *ptr, size_t size, const char*, int);
#define       gt_free(ptr)\
              gt_free_mem(ptr, __FILE__, __LINE__)
void          gt_free_mem(void *ptr, const char*, int);
void          gt_free_func(void *ptr);
unsigned long gt_ma_get_space_peak(void); /* in bytes */
void          gt_ma_show_space_peak(FILE*);
/* check if all allocated memory has been freed, prints to stderr */
int           gt_ma_check_space_leak(void);
void          gt_ma_clean(void);

#endif
