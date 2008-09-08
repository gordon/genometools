/*
  Copyright (c) 2006-2008 Gordon Gremme <gremme@zbh.uni-hamburg.de>
  Copyright (c) 2006-2008 Center for Bioinformatics, University of Hamburg

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

#include <assert.h>
#include "core/array.h"
#include "core/bioseq.h"
#include "core/cstr.h"
#include "core/disc_distri.h"
#include "core/dynalloc.h"
#include "core/error.h"
#include "core/fa.h"
#include "core/fasta.h"
#include "core/fasta_reader.h"
#include "core/fasta_reader_fsm.h"
#include "core/fasta_reader_rec.h"
#include "core/fasta_reader_seqit.h"
#include "core/fileutils.h"
#include "core/gc_content.h"
#include "core/grep.h"
#include "core/ma.h"
#include "core/md5_fingerprint.h"
#include "core/parseutils.h"
#include "core/range.h"
#include "core/sig.h"
#include "core/str.h"
#include "core/undef.h"
#include "core/unused_api.h"
#include "core/xansi.h"
#include "core/xposix.h"

typedef struct {
  GT_StrArray *md5_fingerprints;
} BioseqFingerprints;

struct Bioseq {
  bool use_stdin;
  GT_Str *sequence_file;
  Seq **seqs;
  GT_Array *descriptions,
        *sequence_ranges;
  char *raw_sequence;
  size_t raw_sequence_length,
         allocated;
  Alpha *alpha;
  BioseqFingerprints *fingerprints;
};

static bool read_fingerprints(GT_StrArray *md5_fingerprints,
                              GT_Str  *fingerprints_filename,
                              unsigned long num_of_seqs)
{
  bool reading_succeeded = true;
  FILE *fingerprint_file = NULL;
  assert(md5_fingerprints && fingerprints_filename);
  /* open file */
  if (file_exists(gt_str_get(fingerprints_filename)))
    fingerprint_file = fa_xfopen(gt_str_get(fingerprints_filename), "r");
  else
    reading_succeeded = false;
  /* reading file (each line contains a single MD5 sum) */
  if (reading_succeeded) {
    GT_Str *line = gt_str_new();
    while (gt_str_read_next_line(line, fingerprint_file) != EOF) {
      gt_strarray_add(md5_fingerprints, line);
      gt_str_reset(line);
    }
    gt_str_delete(line);
    if (gt_strarray_size(md5_fingerprints) < num_of_seqs) {
      /* premature end of file (e.g., due to aborted construction) */
      reading_succeeded = false;
      gt_strarray_set_size(md5_fingerprints, 0);
    }
    else
      assert(gt_strarray_size(md5_fingerprints) == num_of_seqs);
  }
  fa_xfclose(fingerprint_file);
  return reading_succeeded;
}

static void add_fingerprints(GT_StrArray *md5_fingerprints, Bioseq *bs)
{
  unsigned long i;
  assert(md5_fingerprints && bs);
  for (i = 0; i < bioseq_number_of_sequences(bs); i++) {
    char *md5 = md5_fingerprint(bioseq_get_sequence(bs, i),
                                bioseq_get_sequence_length(bs, i));
    gt_strarray_add_cstr(md5_fingerprints, md5);
    gt_free(md5);
  }
}

static void strarray_dump_to_file(GT_StrArray *sa, FILE *outfp)
{
  unsigned long i;
  assert(sa && outfp);
  for (i = 0; i < gt_strarray_size(sa); i++) {
    xfputs(gt_strarray_get(sa, i), outfp);
    xfputc('\n', outfp);
  }
}

static void write_fingerprints(GT_StrArray *md5_fingerprints,
                               GT_Str *fingerprints_filename)
{
  FILE *fingerprints_file;
  assert(md5_fingerprints && fingerprints_filename);
  fingerprints_file = fa_xfopen(gt_str_get(fingerprints_filename), "w");
  strarray_dump_to_file(md5_fingerprints, fingerprints_file);
  fa_xfclose(fingerprints_file);
}

static BioseqFingerprints* bioseq_fingerprints_new(Bioseq *bs)
{
  BioseqFingerprints *bsf;
  bool reading_succeeded = false;
  GT_Str *fingerprints_filename;
  assert(bs);
  bsf = gt_calloc(1, sizeof *bsf);
  bsf->md5_fingerprints = gt_strarray_new();
  fingerprints_filename = gt_str_clone(bs->sequence_file);
  gt_str_append_cstr(fingerprints_filename, GT_BIOSEQ_FINGERPRINTS);
  if (!bs->use_stdin && file_exists(gt_str_get(fingerprints_filename)) &&
      !file_is_newer(gt_str_get(bs->sequence_file),
                     gt_str_get(fingerprints_filename))) {
    /* only try to read the fingerprint file if the sequence file was not
       modified in the meantime */
    reading_succeeded = read_fingerprints(bsf->md5_fingerprints,
                                          fingerprints_filename,
                                          bioseq_number_of_sequences(bs));
  }
  if (!reading_succeeded) {
    add_fingerprints(bsf->md5_fingerprints, bs);
    if (!bs->use_stdin)
      write_fingerprints(bsf->md5_fingerprints, fingerprints_filename);
  }
  gt_str_delete(fingerprints_filename);
  return bsf;
}

static void bioseq_fingerprints_delete(BioseqFingerprints *bsf)
{
  if (!bsf) return;
  gt_strarray_delete(bsf->md5_fingerprints);
  gt_free(bsf);
}

static const char* bioseq_fingerprints_get(BioseqFingerprints *bsf,
                                           unsigned long idx)
{
  assert(bsf);
  return gt_strarray_get(bsf->md5_fingerprints, idx);
}

typedef struct {
  FILE *bioseq_index,
       *bioseq_raw;
  unsigned long offset;
  Bioseq *bs;
} Construct_bioseq_files_info;

static int proc_description(const char *description, unsigned long length,
                            void *data, GT_UNUSED GT_Error *err)
{
  Construct_bioseq_files_info *info = (Construct_bioseq_files_info*) data;
  char *description_cstr;
  gt_error_check(err);
  if (info->bs->use_stdin) {
    description_cstr = cstr_dup(description);
    gt_array_add(info->bs->descriptions, description_cstr);
  }
  else {
    if (length)
      xfputs(description, info->bioseq_index);
    xfputc('\n', info->bioseq_index);
  }
  return 0;
}

static int proc_sequence_part(const char *seqpart, unsigned long length,
                              void *data, GT_UNUSED GT_Error *err)
{
  Construct_bioseq_files_info *info = (Construct_bioseq_files_info*) data;
  gt_error_check(err);
  assert(seqpart);
  if (info->bs->use_stdin) {
    info->bs->raw_sequence = gt_dynalloc(info->bs->raw_sequence,
                                         &info->bs->allocated,
                                         info->bs->raw_sequence_length +
                                         length);
    memcpy(info->bs->raw_sequence + info->bs->raw_sequence_length, seqpart,
           length);
    info->bs->raw_sequence_length += length;
  }
  else
    xfputs(seqpart, info->bioseq_raw);
  return 0;
}

static int proc_sequence_length(unsigned long sequence_length, void *data,
                                GT_UNUSED GT_Error *err)
{
  Construct_bioseq_files_info *info = (Construct_bioseq_files_info*) data;
  GT_Range range;
  gt_error_check(err);
  if (info->bs->use_stdin) {
    range.start = info->offset;
    range.end = info->offset + sequence_length - 1;
    gt_array_add(info->bs->sequence_ranges, range);
  }
  else {
    fprintf(info->bioseq_index, "%lu\n", info->offset);
    assert(sequence_length);
    fprintf(info->bioseq_index, "%lu\n", info->offset + sequence_length - 1);
  }
  info->offset += sequence_length;
  return 0;
}

/* this global variables are necessary for the signal handler below */
static Construct_bioseq_files_info bioseq_files_info;
static const char *bioseq_index_filename,
                  *bioseq_raw_filename;

/* removes the incomplete bioseq files */
static void remove_bioseq_files(int sigraised)
{
  /* we don't care if fclose() succeeds, xunlink() will take care of it */
  (void) fclose(bioseq_files_info.bioseq_index);
  (void) fclose(bioseq_files_info.bioseq_raw);
  xunlink(bioseq_index_filename);
  xunlink(bioseq_raw_filename);
  (void) xsignal(sigraised, SIG_DFL);
  xraise(sigraised);
}

static int fill_bioseq(Bioseq *bs, const char *index_filename,
                       const char *raw_filename, GT_Error *err)
{
  FILE *index_file;
  GT_Str *index_line;
  unsigned long line_number = 1;
  char *description;
  GT_Range range;
  int had_err = 0;

  gt_error_check(err);

  /* parse the index file and fill the sequence to index mapping */
  index_line = gt_str_new();
  index_file = fa_xfopen(index_filename, "r");

  while (!had_err && gt_str_read_next_line(index_line, index_file) != EOF) {
    switch (line_number % 3) {
      case 1:
        /* process description */
        description = cstr_dup(gt_str_get(index_line));
        gt_array_add(bs->descriptions, description);
        break;
      case 2:
        /* process sequence start */
        if (parse_ulong(&range.start, gt_str_get(index_line))) {
          gt_error_set(err, "could not parse bioseq start in line %lu of file "
                         "\"%s\"", line_number, index_filename);
          had_err = -1;
        }
        break;
      case 0:
        /* process sequence end */
        if (parse_ulong(&range.end, gt_str_get(index_line))) {
          gt_error_set(err,
                    "could not parse bioseq end in line %lu of file \"%s\"",
                    line_number, index_filename);
          had_err = -1;
        }
        else {
          assert(range.start <= range.end); /* XXX */
          gt_array_add(bs->sequence_ranges, range);
        }
        break;
    }
    line_number++;
    gt_str_reset(index_line);
  }

  if (!had_err) {
    /* the number of descriptions equals the number of sequence ranges */
    assert(gt_array_size(bs->descriptions) == gt_array_size(bs->sequence_ranges));
    /* map the raw file */
    bs->raw_sequence = fa_xmmap_read(raw_filename, &bs->raw_sequence_length);
  }

  fa_xfclose(index_file);
  gt_str_delete(index_line);

  return had_err;
}

static int construct_bioseq_files(Bioseq *bs, GT_Str *bioseq_index_file,
                                  GT_Str *bioseq_raw_file,
                                  FastaReaderType fasta_reader_type, GT_Error *err)
{
  FastaReader *fasta_reader = NULL;
  GT_Str *sequence_filename;
  int had_err;

  gt_error_check(err);

  /* open files & init */
  if (!bs->use_stdin) {
    bioseq_files_info.bioseq_index = fa_xfopen((const char *)
                                               gt_str_get(bioseq_index_file), "w");
    bioseq_files_info.bioseq_raw = fa_xfopen(gt_str_get(bioseq_raw_file), "w");
  }
  bioseq_files_info.offset = 0;
  bioseq_files_info.bs = bs;

  /* register the signal handler to remove incomplete files upon termination */
  if (!bs->use_stdin) {
    bioseq_index_filename = gt_str_get(bioseq_index_file);
    bioseq_raw_filename = gt_str_get(bioseq_raw_file);
    sig_register_all(remove_bioseq_files);
  }

  /* read fasta file */
  sequence_filename = bs->use_stdin ? NULL : bs->sequence_file;
  switch (fasta_reader_type) {
    case FASTA_READER_REC:
      fasta_reader = fasta_reader_rec_new(sequence_filename);
      break;
    case FASTA_READER_FSM:
      fasta_reader = fasta_reader_fsm_new(sequence_filename);
      break;
    case FASTA_READER_SEQIT:
      fasta_reader = fasta_reader_seqit_new(sequence_filename);
      break;
    default: assert(0);
  }
  had_err = fasta_reader_run(fasta_reader, proc_description, proc_sequence_part,
                             proc_sequence_length, &bioseq_files_info, err);
  fasta_reader_delete(fasta_reader);

  /* unregister the signal handler */
  if (!bs->use_stdin)
    sig_unregister_all();

  /* close files */
  if (!bs->use_stdin) {
    fa_xfclose(bioseq_files_info.bioseq_index);
    fa_xfclose(bioseq_files_info.bioseq_raw);
    if (had_err) {
      xunlink(bioseq_index_filename);
      xunlink(bioseq_raw_filename);
    }
  }

  return had_err;
}

static int bioseq_fill(Bioseq *bs, bool recreate,
                       FastaReaderType fasta_reader_type, GT_Error *err)
{
  GT_Str *bioseq_index_file = NULL,
      *bioseq_raw_file = NULL;
  int had_err = 0;

  assert(!bs->raw_sequence);

  /* construct file names */
  if (!bs->use_stdin) {
    bioseq_index_file = gt_str_clone(bs->sequence_file);
    gt_str_append_cstr(bioseq_index_file, GT_BIOSEQ_INDEX);
    bioseq_raw_file = gt_str_clone(bs->sequence_file);
    gt_str_append_cstr(bioseq_raw_file, GT_BIOSEQ_RAW);
  }

  /* construct the bioseq files if necessary */
  if (recreate || bs->use_stdin ||
      !file_exists(gt_str_get(bioseq_index_file)) ||
      !file_exists(gt_str_get(bioseq_raw_file)) ||
      file_is_newer(gt_str_get(bs->sequence_file), gt_str_get(bioseq_index_file)) ||
      file_is_newer(gt_str_get(bs->sequence_file), gt_str_get(bioseq_raw_file))) {
    had_err = construct_bioseq_files(bs, bioseq_index_file, bioseq_raw_file,
                                     fasta_reader_type, err);
  }

  if (!had_err && !bs->use_stdin) {
    /* fill the bioseq */
    had_err = fill_bioseq(bs, gt_str_get(bioseq_index_file),
                          gt_str_get(bioseq_raw_file), err);
  }

  /* free */
  gt_str_delete(bioseq_index_file);
  gt_str_delete(bioseq_raw_file);

  return had_err;
}

static Bioseq* bioseq_new_with_recreate_and_type(GT_Str *sequence_file,
                                                 bool recreate,
                                                 FastaReaderType
                                                 fasta_reader_type,
                                                 GT_Error *err)
{
  Bioseq *bs;
  int had_err = 0;
  gt_error_check(err);
  bs = gt_calloc(1, sizeof (Bioseq));
  if (!strcmp(gt_str_get(sequence_file), "-"))
    bs->use_stdin = true;
  if (!bs->use_stdin && !file_exists(gt_str_get(sequence_file))) {
    gt_error_set(err, "sequence file \"%s\" does not exist or is not readable",
              gt_str_get(sequence_file));
    had_err = -1;
  }
  if (!had_err) {
    bs->sequence_file = gt_str_ref(sequence_file);
    bs->descriptions = gt_array_new(sizeof (char*));
    bs->sequence_ranges = gt_array_new(sizeof (GT_Range));
    had_err = bioseq_fill(bs, recreate, fasta_reader_type, err);
  }
  if (had_err) {
    bioseq_delete(bs);
    return NULL;
  }
  return bs;
}

Bioseq* bioseq_new(const char *sequence_file, GT_Error *err)
{
  Bioseq *bs;
  GT_Str *seqfile;
  gt_error_check(err);
  seqfile = gt_str_new_cstr(sequence_file);
  bs = bioseq_new_with_recreate_and_type(seqfile, false, FASTA_READER_REC, err);
  gt_str_delete(seqfile);
  return bs;
}

Bioseq* bioseq_new_recreate(const char *sequence_file, GT_Error *err)
{
  Bioseq *bs;
  GT_Str *seqfile;
  gt_error_check(err);
  seqfile = gt_str_new_cstr(sequence_file);
  bs = bioseq_new_with_recreate_and_type(seqfile, true, FASTA_READER_REC, err);
  gt_str_delete(seqfile);
  return bs;
}

Bioseq* bioseq_new_str(GT_Str *sequence_file, GT_Error *err)
{
  return bioseq_new_with_recreate_and_type(sequence_file, false,
                                           FASTA_READER_REC, err);
}

Bioseq* bioseq_new_with_fasta_reader(const char *sequence_file,
                                     FastaReaderType fasta_reader, GT_Error *err)
{
  Bioseq *bs;
  GT_Str *seqfile;
  gt_error_check(err);
  seqfile = gt_str_new_cstr(sequence_file);
  bs = bioseq_new_with_recreate_and_type(seqfile, true, fasta_reader, err);
  gt_str_delete(seqfile);
  return bs;
}

void bioseq_delete(Bioseq *bs)
{
  unsigned long i;
  if (!bs) return;
  bioseq_fingerprints_delete(bs->fingerprints);
  gt_str_delete(bs->sequence_file);
  if (bs->seqs) {
    for (i = 0; i < gt_array_size(bs->descriptions); i++)
      seq_delete(bs->seqs[i]);
    gt_free(bs->seqs);
  }
  for (i = 0; i < gt_array_size(bs->descriptions); i++)
    gt_free(*(char**) gt_array_get(bs->descriptions, i));
  gt_array_delete(bs->descriptions);
  gt_array_delete(bs->sequence_ranges);
  if (bs->use_stdin)
    gt_free(bs->raw_sequence);
  else
    fa_xmunmap(bs->raw_sequence);
  alpha_delete(bs->alpha);
  gt_free(bs);
}

static void determine_alpha_if_necessary(Bioseq *bs)
{
  assert(bs);
  if (!bs->alpha) {
    bs->alpha = alpha_guess(bioseq_get_raw_sequence(bs),
                            bioseq_get_raw_sequence_length(bs));
  }
}

Alpha* bioseq_get_alpha(Bioseq *bs)
{
  assert(bs);
  determine_alpha_if_necessary(bs);
  assert(bs->alpha);
  return bs->alpha;
}

Seq* bioseq_get_seq(Bioseq *bs, unsigned long idx)
{
  assert(bs);
  assert(idx < gt_array_size(bs->descriptions));
  if (!bs->seqs)
    bs->seqs = gt_calloc(gt_array_size(bs->descriptions), sizeof (Seq*));
  determine_alpha_if_necessary(bs);
  if (!bs->seqs[idx]) {
    bs->seqs[idx] = seq_new(bioseq_get_sequence(bs, idx),
                            bioseq_get_sequence_length(bs, idx),
                            bs->alpha);
    seq_set_description(bs->seqs[idx], bioseq_get_description(bs, idx));
  }
  return bs->seqs[idx];
}

const char* bioseq_get_description(Bioseq *bs, unsigned long idx)
{
  assert(bs);
  return *(char**) gt_array_get(bs->descriptions, idx);
}

const char* bioseq_get_sequence(Bioseq *bs, unsigned long idx)
{
  GT_Range sequence_range;
  assert(bs);
  sequence_range = *(GT_Range*) gt_array_get(bs->sequence_ranges, idx);
  return bs->raw_sequence + sequence_range.start;
}

const char* bioseq_get_raw_sequence(Bioseq *bs)
{
  assert(bs);
  return bs->raw_sequence;
}

const char* bioseq_get_md5_fingerprint(Bioseq *bs, unsigned long idx)
{
  assert(bs && idx < bioseq_number_of_sequences(bs));
  if (!bs->fingerprints)
    bs->fingerprints = bioseq_fingerprints_new(bs);
  assert(bioseq_fingerprints_get(bs->fingerprints, idx));
  return bioseq_fingerprints_get(bs->fingerprints, idx);
}

unsigned long bioseq_get_sequence_length(Bioseq *bs, unsigned long idx)
{
  GT_Range sequence_range;
  assert(bs);
  sequence_range = *(GT_Range*) gt_array_get(bs->sequence_ranges, idx);
  return gt_range_length(sequence_range);
}

unsigned long bioseq_get_raw_sequence_length(Bioseq *bs)
{
  assert(bs);
  return bs->raw_sequence_length;
}

unsigned long bioseq_number_of_sequences(Bioseq *bs)
{
  assert(bs);
  return gt_array_size(bs->descriptions);
}

void bioseq_show_as_fasta(Bioseq *bs, unsigned long width)
{
  unsigned long i;

  assert(bs);

  for (i = 0; i < bioseq_number_of_sequences(bs); i++) {
    fasta_show_entry(bioseq_get_description(bs, i), bioseq_get_sequence(bs, i),
                     bioseq_get_sequence_length(bs, i), width);
  }
}

void bioseq_show_sequence_as_fasta(Bioseq *bs, unsigned long seqnum,
                                   unsigned long width)
{
  assert(bs);
  assert(seqnum < bioseq_number_of_sequences(bs));

  fasta_show_entry(bioseq_get_description(bs, seqnum),
                   bioseq_get_sequence(bs, seqnum),
                   bioseq_get_sequence_length(bs, seqnum), width);

}

void bioseq_show_gc_content(Bioseq *bs)
{
  Alpha *dna_alpha;
  assert(bs);
  determine_alpha_if_necessary(bs);
  dna_alpha = alpha_new_dna();
  if (alpha_is_compatible_with_alpha(bs->alpha, dna_alpha)) {
    printf("showing GC-content for sequence file \"%s\"\n",
           gt_str_get(bs->sequence_file));
    gc_content_show(bioseq_get_raw_sequence(bs),
                    bioseq_get_raw_sequence_length(bs), bs->alpha);
  }
  alpha_delete(dna_alpha);
}

void bioseq_show_stat(Bioseq *bs)
{
  unsigned long i, num_of_seqs;
  assert(bs);
  num_of_seqs = bioseq_number_of_sequences(bs);
  printf("showing statistics for sequence file \"%s\"\n",
         gt_str_get(bs->sequence_file));
  printf("number of sequences: %lu\n", num_of_seqs);
  printf("total length: %lu\n", bioseq_get_raw_sequence_length(bs));
  for (i = 0; i < num_of_seqs; i++) {
    printf("sequence #%lu length: %lu\n", i+1,
           bioseq_get_sequence_length(bs, i));
  }
}

void bioseq_show_seqlengthdistri(Bioseq *bs)
{
  DiscDistri *d;
  unsigned long i;
  assert(bs);
  d = disc_distri_new();
  for (i = 0; i < bioseq_number_of_sequences(bs); i++)
    disc_distri_add(d, bioseq_get_sequence_length(bs, i));
  printf("sequence length distribution:\n");
  disc_distri_show(d);
  disc_distri_delete(d);
}
