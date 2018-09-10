/*
 * Copyright © 2018 Adobe Systems Incorporated.
 *
 *  This is part of HarfBuzz, a text shaping library.
 *
 * Permission is hereby granted, without written agreement and without
 * license or royalty fees, to use, copy, modify, and distribute this
 * software and its documentation for any purpose, provided that the
 * above copyright notice and the following two paragraphs appear in
 * all copies of this software.
 *
 * IN NO EVENT SHALL THE COPYRIGHT HOLDER BE LIABLE TO ANY PARTY FOR
 * DIRECT, INDIRECT, SPECIAL, INCIDENTAL, OR CONSEQUENTIAL DAMAGES
 * ARISING OUT OF THE USE OF THIS SOFTWARE AND ITS DOCUMENTATION, EVEN
 * IF THE COPYRIGHT HOLDER HAS BEEN ADVISED OF THE POSSIBILITY OF SUCH
 * DAMAGE.
 *
 * THE COPYRIGHT HOLDER SPECIFICALLY DISCLAIMS ANY WARRANTIES, INCLUDING,
 * BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND
 * FITNESS FOR A PARTICULAR PURPOSE.  THE SOFTWARE PROVIDED HEREUNDER IS
 * ON AN "AS IS" BASIS, AND THE COPYRIGHT HOLDER HAS NO OBLIGATION TO
 * PROVIDE MAINTENANCE, SUPPORT, UPDATES, ENHANCEMENTS, OR MODIFICATIONS.
 *
 * Adobe Author(s): Michiharu Ariza
 */

#include "hb-ot-cff-common.hh"
#include "hb-ot-cff2-table.hh"
#include "hb-subset-cff-common.hh"

using namespace CFF;

/**
 * hb_plan_subset_cff_fdselect
 * Determine an optimal FDSelect format according to a provided plan.
 *
 * Return value: FDSelect format, size, and ranges for the most compact subset FDSelect
 * along with a font index remapping table
 **/

bool
hb_plan_subset_cff_fdselect (const hb_vector_t<hb_codepoint_t> &glyphs,
                            unsigned int fdCount,
                            const FDSelect &src, /* IN */
                            unsigned int &subset_fd_count /* OUT */,
                            unsigned int &subst_fdselect_size /* OUT */,
                            unsigned int &subst_fdselect_format /* OUT */,
                            hb_vector_t<hb_codepoint_t> &subst_first_glyphs /* OUT */,
                            Remap &fdmap /* OUT */)
{
  subset_fd_count = 0;
  subst_fdselect_size = 0;
  subst_fdselect_format = 0;
  unsigned int  num_ranges = 0;

  unsigned int subset_num_glyphs = glyphs.len;
  if (subset_num_glyphs == 0)
    return true;

  {
    /* use hb_set to determine the subset of font dicts */
    hb_set_t  *set = hb_set_create ();
    if (set == &Null (hb_set_t))
      return false;
    hb_codepoint_t  prev_fd = CFF_UNDEF_CODE;
    for (hb_codepoint_t i = 0; i < subset_num_glyphs; i++)
    {
      hb_codepoint_t  fd = src.get_fd (glyphs[i]);
      set->add (fd);
      
      if (fd != prev_fd)
      {
        num_ranges++;
        prev_fd = fd;
        subst_first_glyphs.push (i);
      }
    }

    subset_fd_count = set->get_population ();
    if (subset_fd_count == fdCount)
    {
      /* all font dicts belong to the subset. no need to subset FDSelect & FDArray */
      hb_set_destroy (set);
      return true;
    }

    /* create a fdmap */
    if (!fdmap.reset (fdCount))
    {
      hb_set_destroy (set);
      return false;
    }

    hb_codepoint_t  fd = CFF_UNDEF_CODE;
    while (set->next (&fd))
      fdmap.add (fd);
    assert (fdmap.get_count () == subset_fd_count);
    hb_set_destroy (set);
  }

  /* determine which FDSelect format is most compact */
  if (subset_fd_count > 0xFF)
  {
    assert (src.format == 4);
    subst_fdselect_format = 4;
    subst_fdselect_size = FDSelect4::min_size + FDSelect4_Range::static_size * num_ranges;
  }
  else
  {
    unsigned int format0_size = FDSelect0::min_size + HBUINT8::static_size * subset_num_glyphs;
    unsigned int format3_size = FDSelect3::min_size + FDSelect3_Range::static_size * num_ranges;

    if (format0_size <= format3_size)
    {
      // subst_fdselect_format = 0;
      subst_fdselect_size = format0_size;
      subst_first_glyphs.fini ();
    }
    else
    {
      subst_fdselect_format = 3;
      subst_fdselect_size = format3_size;
    }
  }

  return true;
}

template <typename FDSELECT3_4>
static inline bool
serialize_fdselect_3_4 (hb_serialize_context_t *c,
                          unsigned int num_glyphs,
                          const FDSelect &src,
                          unsigned int size,
                          const hb_vector_t<hb_codepoint_t> &first_glyphs,
                          const Remap &fdmap)
{
  TRACE_SERIALIZE (this);
  FDSELECT3_4 *p = c->allocate_size<FDSELECT3_4> (size);
  if (unlikely (p == nullptr)) return_trace (false);
  p->nRanges.set (first_glyphs.len);
  for (unsigned int i = 0; i < first_glyphs.len; i++)
  {
    hb_codepoint_t  glyph = first_glyphs[i];
    p->ranges[i].first.set (glyph);
    p->ranges[i].fd.set (fdmap[src.get_fd (glyph)]);
  }
  p->sentinel().set (num_glyphs);
  return_trace (true);
}

/**
 * hb_serialize_cff_fdselect
 * Serialize a subset FDSelect format planned above.
 **/
bool
hb_serialize_cff_fdselect (hb_serialize_context_t *c,
                          const hb_vector_t<hb_codepoint_t> &glyphs,
                          const FDSelect &src,
                          unsigned int fd_count,
                          unsigned int fdselect_format,
                          unsigned int size,
                          const hb_vector_t<hb_codepoint_t> &first_glyphs,
                          const Remap &fdmap)
{
  TRACE_SERIALIZE (this);
  FDSelect  *p = c->allocate_min<FDSelect> ();
  if (unlikely (p == nullptr)) return_trace (false);
  p->format.set (fdselect_format);
  size -= FDSelect::min_size;

  switch (fdselect_format)
  {
    case 0:
    {
      FDSelect0 *p = c->allocate_size<FDSelect0> (size);
      if (unlikely (p == nullptr)) return_trace (false);
      for (unsigned int i = 0; i < glyphs.len; i++)
        p->fds[i].set (fdmap[src.get_fd (glyphs[i])]);
      break;
    }
    
    case 3:
      return serialize_fdselect_3_4<FDSelect3> (c,
                                                glyphs.len,
                                                src,
                                                size,
                                                first_glyphs,
                                                fdmap);
    
    case 4:
      return serialize_fdselect_3_4<FDSelect4> (c,
                                                glyphs.len,
                                                src,
                                                size,
                                                first_glyphs,
                                                fdmap);

    default:
      assert(false);
  }
  
  return_trace (true);
}
