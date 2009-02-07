#include <stdarg.h>
#include "core/ma_api.h"
#include "core/symboldef.h"
#include "core/chardef.h"
#include "core/unused_api.h"
#include "core/assert_api.h"
#include "match/stamp.h"
#include "absdfstrans-imp.h"

#define MINUSINFTY (-1L)
#define REPLACEMENTSCORE(A,B) (((A) != (B) || ISSPECIAL(A))\
                                   ? lci->scorevalues.mismatchscore\
                                   : lci->scorevalues.matchscore)

typedef struct
{
  long matchscore,   /* must be positive */
       mismatchscore,/* must be negative */
       gapstart,     /* must be negative */
       gapextend;    /* must be negative */
} Scorevalues;

typedef struct
{
  Scorevalues scorevalues;
  const Uchar *query;
  unsigned long maxcollen,
                querylength,
                threshold;
} Limdfsconstinfo;

typedef struct
{
  long repcell,
       inscell,
       delcell,
       bestcell;
} Matrixvalue;

typedef struct
{
  Matrixvalue *colvalues;
  unsigned long lenval;
  unsigned long pprefixlen,
                maxvalue;
} Column;

typedef struct
{
  Limdfsstatus status;
  unsigned long qseqendpos;
  long alignmentscore;
} Idxlocaliresult;

static inline long max2 (long a,long b)
{
  return (a < b) ? b : a;
}

static inline long max3 (long a,long b,long c)
{
  long temp;

  temp = (a < b) ? b : a;
  return (temp < c) ? c : temp;
}

#ifdef SKDEBUG
static void showscorecolumn(const Column *column,
                            unsigned long querylength,
                            unsigned long currentdepth)
{
  printf("at depth %lu: ",currentdepth);
  if (column->colvalues == NULL)
  {
    gt_assert(column->lenval == 0);
    printf("empty column\n");
  } else
  {
    unsigned long idx;

    for (idx = 0; idx <= querylength; idx++)
    {
      if (column->colvalues[idx].bestcell > 0)
      {
        printf("(%lu,%ld) ",idx,column->colvalues[idx].bestcell);
      }
    }
    printf("max=%lu\n",column->maxvalue);
  }
}

void locali_showLimdfsstate(const DECLAREPTRDFSSTATE(aliasstate),
                            unsigned long currentdepth,
                            const void *dfsconstinfo)
{
  const Limdfsconstinfo *lci = (const Limdfsconstinfo *) dfsconstinfo;

  showscorecolumn((const Column *) aliasstate,lci->querylength,currentdepth);
}
#endif

#ifdef SKDEBUG
#define REALLOCMSG(COL)\
        printf("line %d: %salloc %lu entries",\
                __LINE__,(COL)->colvalues == NULL ? "m" : "re",\
                lci->maxcollen)

#define ATADDRESS(S,COL)\
        printf("%s at address %0X\n",S,(unsigned int) (COL)->colvalues)
#else
#define REALLOCMSG(COL) /* Nothing */
#define ATADDRESS(S,COL)  /* Nothing */
#endif

static void secondcolumn (const Limdfsconstinfo *lci,Column *outcol,
                          Uchar dbchar)
{
  unsigned long i;

  if (outcol->lenval < lci->maxcollen)
  {
    REALLOCMSG(outcol);
    outcol->colvalues = gt_realloc (outcol->colvalues,
                                    sizeof (Matrixvalue) * lci->maxcollen);
    outcol->lenval = lci->maxcollen;
    ATADDRESS("",outcol);
  }
  outcol->colvalues[0].repcell = MINUSINFTY;
  outcol->colvalues[0].inscell = lci->scorevalues.gapstart +
                                 lci->scorevalues.gapextend;
  outcol->colvalues[0].delcell = MINUSINFTY;
  outcol->colvalues[0].bestcell = MINUSINFTY;
  outcol->maxvalue = 0;
  outcol->pprefixlen = 0;
  for (i = 1UL; i <= lci->querylength; i++)
  {
    outcol->colvalues[i].delcell = MINUSINFTY;
    outcol->colvalues[i].inscell = MINUSINFTY;
    outcol->colvalues[i].repcell = REPLACEMENTSCORE(dbchar,lci->query[i-1]);
    outcol->colvalues[i].bestcell = max2(outcol->colvalues[i].delcell,
                                         outcol->colvalues[i].repcell);
    if (outcol->colvalues[i].bestcell > 0 &&
        outcol->colvalues[i].bestcell > (long) outcol->maxvalue)
    {
      outcol->maxvalue = (unsigned long) outcol->colvalues[i].bestcell;
      outcol->pprefixlen = i;
    }
  }
}

static void nextcolumn (const Limdfsconstinfo *lci,
                        Column *outcol,
                        const Uchar dbchar,
                        const Column *incol)
{
  unsigned long i;

  gt_assert(outcol != incol);
  gt_assert(outcol->colvalues != incol->colvalues);
  gt_assert(incol->lenval >= lci->querylength+1);
  if (outcol->lenval < lci->querylength+1)
  {
    REALLOCMSG(outcol);
    outcol->colvalues = gt_realloc (outcol->colvalues,
                                    sizeof (Matrixvalue) * lci->maxcollen);
    outcol->lenval = lci->maxcollen;
    ATADDRESS("",outcol);
  }
  gt_assert(outcol->lenval >= lci->querylength+1);
  outcol->colvalues[0].repcell = outcol->colvalues[0].delcell = MINUSINFTY;
  if (incol->colvalues[0].inscell > 0)
  {
    if (incol->colvalues[0].bestcell > 0)
    {
      outcol->colvalues[0].inscell
        = max2 (incol->colvalues[0].inscell + lci->scorevalues.gapextend,
                incol->colvalues[0].bestcell + lci->scorevalues.gapstart +
                                               lci->scorevalues.gapextend);
    } else
    {
      outcol->colvalues[0].inscell = incol->colvalues[0].inscell +
                                     lci->scorevalues.gapextend;
    }
  } else
  {
    if (incol->colvalues[0].bestcell > 0)
    {
      outcol->colvalues[0].inscell = incol->colvalues[0].bestcell +
                                     lci->scorevalues.gapstart +
                                     lci->scorevalues.gapextend;
    } else
    {
      outcol->colvalues[0].inscell = MINUSINFTY;
    }
  }
  outcol->colvalues[0].bestcell = max3 (outcol->colvalues[0].repcell,
                                        outcol->colvalues[0].inscell,
                                        outcol->colvalues[0].delcell);
  outcol->maxvalue = (unsigned long) max2 (0,outcol->colvalues[0].bestcell);
  outcol->pprefixlen = 0;
  for (i = 1UL; i <= lci->querylength; i++)
  {
    if (incol->colvalues[i-1].bestcell > 0)
    {
      outcol->colvalues[i].repcell = incol->colvalues[i-1].bestcell +
                                     REPLACEMENTSCORE(dbchar,lci->query[i-1]);
    } else
    {
      outcol->colvalues[i].repcell = MINUSINFTY;
    }
    if (incol->colvalues[i].inscell > 0)
    {
      if (incol->colvalues[i].bestcell > 0)
      {
        outcol->colvalues[i].inscell
          = max2 (incol->colvalues[i].inscell + lci->scorevalues.gapextend,
                  incol->colvalues[i].bestcell + lci->scorevalues.gapstart +
                                                 lci->scorevalues.gapextend);
      } else
      {
        outcol->colvalues[i].inscell = incol->colvalues[i].inscell +
                                       lci->scorevalues.gapextend;
      }
    } else
    {
      if (incol->colvalues[i].bestcell > 0)
      {
        outcol->colvalues[i].inscell = incol->colvalues[i].bestcell +
                                       lci->scorevalues.gapstart +
                                       lci->scorevalues.gapextend;
      } else
      {
        outcol->colvalues[i].inscell = MINUSINFTY;
      }
    }
    if (outcol->colvalues[i-1].delcell > 0)
    {
      if (outcol->colvalues[i-1].bestcell > 0)
      {
        outcol->colvalues[i].delcell
          = max2 (outcol->colvalues[i-1].delcell + lci->scorevalues.gapextend,
                  outcol->colvalues[i-1].bestcell + lci->scorevalues.gapstart +
                                                    lci->scorevalues.gapextend);
      } else
      {
        outcol->colvalues[i].delcell = outcol->colvalues[i-1].delcell +
                                       lci->scorevalues.gapextend;
      }
    } else
    {
      if (outcol->colvalues[i-1].bestcell > 0)
      {
        outcol->colvalues[i].delcell = outcol->colvalues[i-1].bestcell +
                                       lci->scorevalues.gapstart +
                                       lci->scorevalues.gapextend;
      } else
      {
        outcol->colvalues[i].delcell = MINUSINFTY;
      }
    }
    outcol->colvalues[i].bestcell = max3 (outcol->colvalues[i].repcell,
                                          outcol->colvalues[i].inscell,
                                          outcol->colvalues[i].delcell);
    if (outcol->colvalues[i].bestcell > 0 &&
        outcol->colvalues[i].bestcell > (long) outcol->maxvalue)
    {
      outcol->maxvalue = (unsigned long) outcol->colvalues[i].bestcell;
      outcol->pprefixlen = i;
    }
  }
}

static void inplacenextcolumn (const Limdfsconstinfo *lci,
                               const Uchar dbchar,
                               Column *column)
{
  unsigned long i;
  Matrixvalue nw, west;

  column->colvalues[0].repcell = column->colvalues[0].delcell = MINUSINFTY;
  if (column->colvalues[0].inscell > 0)
  {
    if (column->colvalues[0].bestcell > 0)
    {
      column->colvalues[0].inscell
        = max2 (column->colvalues[0].inscell + lci->scorevalues.gapextend,
                column->colvalues[0].bestcell + lci->scorevalues.gapstart +
                                                lci->scorevalues.gapextend);
    } else
    {
      column->colvalues[0].inscell
        = column->colvalues[0].inscell + lci->scorevalues.gapextend;
    }
  } else
  {
    if (column->colvalues[0].bestcell > 0)
    {
      column->colvalues[0].inscell = column->colvalues[0].bestcell +
                                     lci->scorevalues.gapstart +
                                     lci->scorevalues.gapextend;
    } else
    {
      column->colvalues[0].inscell = MINUSINFTY;
    }
  }
  column->colvalues[0].bestcell = max3 (column->colvalues[0].repcell,
                                        column->colvalues[0].inscell,
                                        column->colvalues[0].delcell);
  column->maxvalue = (unsigned long) max2(0,column->colvalues[0].bestcell);
  column->pprefixlen = 0;
  nw = column->colvalues[0];
  for (i = 1UL; i <= lci->querylength; i++)
  {
    west = column->colvalues[i];
    if (nw.bestcell > 0)
    {
      column->colvalues[i].repcell = nw.bestcell +
                                     REPLACEMENTSCORE(dbchar,lci->query[i-1]);
    } else
    {
      column->colvalues[i].repcell = MINUSINFTY;
    }
    if (west.inscell > 0)
    {
      if (west.bestcell > 0)
      {
        column->colvalues[i].inscell
          = max2 (west.inscell + lci->scorevalues.gapextend,
                  west.bestcell + lci->scorevalues.gapstart
                                + lci->scorevalues.gapextend);
      } else
      {
        column->colvalues[i].inscell = west.inscell +
                                       lci->scorevalues.gapextend;
      }
    } else
    {
      if (west.bestcell > 0)
      {
        column->colvalues[i].inscell = west.bestcell +
                                       lci->scorevalues.gapstart +
                                       lci->scorevalues.gapextend;
      } else
      {
        column->colvalues[i].inscell = MINUSINFTY;
      }
    }
    if (column->colvalues[i-1].delcell > 0)
    {
      if (column->colvalues[i-1].bestcell > 0)
      {
        column->colvalues[i].delcell
          = max2 (column->colvalues[i-1].delcell + lci->scorevalues.gapextend,
                  column->colvalues[i-1].bestcell + lci->scorevalues.gapstart +
                                                    lci->scorevalues.gapextend);
      } else
      {
        column->colvalues[i].delcell = column->colvalues[i-1].delcell +
                                       lci->scorevalues.gapextend;
      }
    } else
    {
      if (column->colvalues[i-1].bestcell > 0)
      {
        column->colvalues[i].delcell = column->colvalues[i-1].bestcell +
                                       lci->scorevalues.gapstart +
                                       lci->scorevalues.gapextend;
      } else
      {
        column->colvalues[i].delcell = MINUSINFTY;
      }
    }
    nw = west;
    column->colvalues[i].bestcell = max3 (column->colvalues[i].repcell,
                                          column->colvalues[i].inscell,
                                          column->colvalues[i].delcell);
    if (column->colvalues[i].bestcell > 0 &&
        column->colvalues[i].bestcell > (long) column->maxvalue)
    {
      column->maxvalue = (unsigned long) column->colvalues[i].bestcell;
      column->pprefixlen = i;
    }
  }
}

static void *locali_allocatedfsconstinfo (GT_UNUSED unsigned int alphasize)
{
  Limdfsconstinfo *lci = gt_malloc (sizeof (Limdfsconstinfo));

  lci->maxcollen = 0;
  return lci;
}

static void locali_initdfsconstinfo (void *dfsconstinfo,
                                     unsigned int alphasize,...)
{
  va_list ap;
  Limdfsconstinfo *lci = (Limdfsconstinfo *) dfsconstinfo;

  va_start (ap, alphasize);
  lci->scorevalues.matchscore = va_arg (ap, long);
  lci->scorevalues.mismatchscore = va_arg (ap, long);
  lci->scorevalues.gapstart = va_arg (ap, long);
  lci->scorevalues.gapextend = va_arg (ap, long);
  lci->threshold = va_arg (ap, unsigned long);
  lci->query = va_arg (ap, const Uchar *);
  lci->querylength = va_arg (ap, unsigned long);
  if (lci->maxcollen < lci->querylength + 1)
  {
    lci->maxcollen = lci->querylength+1;
  }
  va_end(ap);
}

static void locali_freedfsconstinfo (void **dfsconstinfo)
{
  Limdfsconstinfo *lci = (Limdfsconstinfo *) *dfsconstinfo;

  gt_free (lci);
  *dfsconstinfo = NULL;
}

static void locali_initrootLimdfsstate(DECLAREPTRDFSSTATE(aliasstate),
                                       void *dfsconstinfo)
{
  Column *column = (Column *) aliasstate;
  Limdfsconstinfo *lci = (Limdfsconstinfo *) dfsconstinfo;

  if (column->lenval < lci->maxcollen)
  {
    REALLOCMSG(column);
    column->colvalues = gt_realloc (column->colvalues,
                                    sizeof (Matrixvalue) * lci->maxcollen);
    column->lenval = lci->maxcollen;
    ATADDRESS("",column);
  }
}

static void locali_initLimdfsstackelem (DECLAREPTRDFSSTATE (aliasstate))
{
  Column *column = (Column *) aliasstate;

  column->colvalues = NULL;
  column->lenval = 0;
}

static void locali_freeLimdfsstackelem (DECLAREPTRDFSSTATE (aliasstate))
{
  Column *column = (Column *) aliasstate;

  if (column ->colvalues != NULL)
  {
    ATADDRESS("free ",column);
    gt_free(column->colvalues);
    column->colvalues = NULL;
    column->lenval = 0;
  }
}

static void locali_copyLimdfsstate (DECLAREPTRDFSSTATE(deststate),
                                    const DECLAREPTRDFSSTATE(srcstate),
                                    void *dfsconstinfo)
{
  Limdfsconstinfo *lci = (Limdfsconstinfo *) dfsconstinfo;
  Column *destcol = (Column *) deststate;
  const Column *srccol = (const Column *) srcstate;

  if (srccol->colvalues != NULL)
  {
    unsigned long idx;

    if (destcol->lenval < lci->maxcollen)
    {
      REALLOCMSG(destcol);
      destcol->colvalues = gt_realloc (destcol->colvalues,
                                       sizeof (Matrixvalue) * lci->maxcollen);
      destcol->lenval = lci->maxcollen;
      ATADDRESS("",destcol);
    }
    if (destcol->lenval < lci->querylength+1)
    {
      fprintf(stderr,"destcol->lenval = %lu < %lu lci->querylength+1\n",
                      destcol->lenval,lci->querylength+1);
      exit(EXIT_FAILURE); /* Programming error */
    }
    if (srccol->lenval < lci->querylength+1)
    {
      fprintf(stderr,"srccol->lenval = %lu < %lu lci->querylength+1\n",
                      srccol->lenval,lci->querylength+1);
      exit(EXIT_FAILURE); /* Programming error */
    }
    for (idx = 0; idx<=lci->querylength; idx++)
    {
      destcol->colvalues[idx] = srccol->colvalues[idx];
    }
  }
  destcol->maxvalue = srccol->maxvalue;
  destcol->pprefixlen = srccol->pprefixlen;
}

static void locali_fullmatchLimdfsstate (Limdfsresult *limdfsresult,
                                         DECLAREPTRDFSSTATE(aliasstate),
                                         GT_UNUSED Seqpos leftbound,
                                         GT_UNUSED Seqpos rightbound,
                                         GT_UNUSED Seqpos width,
                                         GT_UNUSED unsigned long currentdepth,
                                         void *dfsconstinfo)
{
  Column *column = (Column *) aliasstate;

  if (column->colvalues != NULL)
  {
    const Limdfsconstinfo *lci = (Limdfsconstinfo *) dfsconstinfo;

    if (column->maxvalue >= lci->threshold)
    {
      limdfsresult->status = Limdfssuccess;
      limdfsresult->distance = column->maxvalue;
      limdfsresult->pprefixlen = column->pprefixlen;
    } else
    {
      if (column->maxvalue > 0)
      {
        limdfsresult->status = Limdfscontinue;
      } else
      {
        limdfsresult->status = Limdfsstop;
      }
    }
  } else
  {
    limdfsresult->status = Limdfscontinue;
  }
}

static void locali_nextLimdfsstate (const void *dfsconstinfo,
                                    DECLAREPTRDFSSTATE (aliasoutcol),
                                    GT_UNUSED unsigned long currentdepth,
                                    Uchar currentchar,
                                    const DECLAREPTRDFSSTATE (aliasincol))
{
  const Limdfsconstinfo *lci = (const Limdfsconstinfo *) dfsconstinfo;
  Column *outcol = (Column *) aliasoutcol;
  const Column *incol = (const Column *) aliasincol;

  if (currentdepth > 1UL)
  {
    nextcolumn (lci,outcol,currentchar,incol);
  } else
  {
    secondcolumn (lci, outcol, currentchar);
  }
}

static void locali_inplacenextLimdfsstate (const void *dfsconstinfo,
                                           DECLAREPTRDFSSTATE (aliasstate),
                                           GT_UNUSED unsigned long currentdepth,
                                           Uchar currentchar)
{
  Column *column = (Column *) aliasstate;
  const Limdfsconstinfo *lci = (const Limdfsconstinfo *) dfsconstinfo;

  if (currentdepth > 1UL)
  {
    inplacenextcolumn (lci,currentchar,column);
  } else
  {
    secondcolumn (lci,column,currentchar);
  }
}

const AbstractDfstransformer *locali_AbstractDfstransformer (void)
{
  static const AbstractDfstransformer locali_adfst =
  {
    sizeof (Column),
    locali_allocatedfsconstinfo,
    locali_initdfsconstinfo,
    NULL,
    locali_freedfsconstinfo,
    locali_initrootLimdfsstate,
    locali_initLimdfsstackelem,
    locali_freeLimdfsstackelem,
    locali_copyLimdfsstate,
    locali_fullmatchLimdfsstate,
    locali_nextLimdfsstate,
    locali_inplacenextLimdfsstate,
#ifdef SKDEBUG
    locali_showLimdfsstate
#endif /*  */
  };
  return &locali_adfst;
}
