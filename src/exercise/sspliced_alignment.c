/*
  Copyright (c) 2008 Gordon Gremme <gremme@zbh.uni-hamburg.de>
  Copyright (c) 2008 Center for Bioinformatics, University of Hamburg

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

#include "core/cstr.h"
#include "core/ma.h"
#include "exercise/sspliced_alignment.h"

struct SSplicedAlignment {
  char *id;
  bool forward;
  GT_Array *exons; /* the exon ranges */
};

SSplicedAlignment* sspliced_alignment_new(const char *id, bool forward)
{
  SSplicedAlignment *sa;
  assert(id);
  sa = gt_malloc(sizeof *sa);
  sa->id = cstr_dup(id);
  sa->forward = forward;
  sa->exons = gt_array_new(sizeof (GT_Range));
  return sa;
}

void sspliced_alignment_delete(SSplicedAlignment *sa)
{
  if (!sa) return;
  gt_array_delete(sa->exons);
  gt_free(sa->id);
  gt_free(sa);
}

bool sspliced_alignment_is_forward(const SSplicedAlignment *sa)
{
  assert(sa);
  return sa->forward;
}

void sspliced_alignment_add_exon(SSplicedAlignment *sa, GT_Range exon)
{
  assert(sa);
  gt_array_add(sa->exons, exon);
}

unsigned long sspliced_alignment_num_of_exons(const SSplicedAlignment *sa)
{
  assert(sa);
  return gt_array_size(sa->exons);
}

GT_Range sspliced_alignment_get_exon(const SSplicedAlignment *sa,
                                  unsigned long exon_number)
{
  assert(sa);
  return *(GT_Range*) gt_array_get(sa->exons, exon_number);
}

GT_Range sspliced_alignment_genomic_range(const SSplicedAlignment *sa)
{
  GT_Range range;
  assert(sa);
  assert(gt_array_size(sa->exons));
  range.start = ((GT_Range*) gt_array_get_first(sa->exons))->start;
  range.end   = ((GT_Range*) gt_array_get_last(sa->exons))->end;
  return range;
}

static int range_compare_long_first(GT_Range range_a, GT_Range range_b)
{
  assert(range_a.start <= range_a.end && range_b.start <= range_b.end);

  if ((range_a.start == range_b.start) && (range_a.end == range_b.end))
    return 0; /* range_a == range_b */

  if ((range_a.start < range_b.start) ||
      ((range_a.start == range_b.start) && (range_a.end > range_b.end)))
    return -1; /* range_a < range_b */

  return 1; /* range_a > range_b */
}

int sspliced_alignment_compare_ptr(const SSplicedAlignment **sa_a,
                                   const SSplicedAlignment **sa_b)
{
  GT_Range range_a, range_b;
  range_a = sspliced_alignment_genomic_range(*sa_a);
  range_b = sspliced_alignment_genomic_range(*sa_b);
  return range_compare_long_first(range_a, range_b);
}
