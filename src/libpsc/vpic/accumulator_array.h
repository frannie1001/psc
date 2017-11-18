
#ifndef ACCUMULATOR_ARRAY_H
#define ACCUMULATOR_ARRAY_H

#include "grid.h"

// ======================================================================
// AccumulatorArray

struct AccumulatorArray : accumulator_array_t {
  AccumulatorArray(Grid g);
  ~AccumulatorArray();
};

// ----------------------------------------------------------------------
// copied from accumulator_array.c, converted from new/delete -> ctor

static int
aa_n_pipeline(void) {
  int                       n = serial.n_pipeline;
  if( n<thread.n_pipeline ) n = thread.n_pipeline;
  return n; /* max( {serial,thread,spu}.n_pipeline ) */
}

inline void
accumulator_array_ctor(accumulator_array_t * aa, grid_t * g ) {
  if( !g ) ERROR(( "Bad grid."));
  aa->n_pipeline = aa_n_pipeline();
  aa->stride     = POW2_CEIL(g->nv,2);
  aa->g          = g;
  MALLOC_ALIGNED( aa->a, (size_t)(aa->n_pipeline+1)*(size_t)aa->stride, 128 );
  CLEAR( aa->a, (size_t)(aa->n_pipeline+1)*(size_t)aa->stride );
}

inline void
accumulator_array_dtor( accumulator_array_t * aa ) {
  if( !aa ) return;
  FREE_ALIGNED( aa->a );
}

// ----------------------------------------------------------------------
// AccumulatorArray implementation

inline AccumulatorArray::AccumulatorArray(Grid grid)
{
  accumulator_array_ctor(this, grid.g_);
}

inline AccumulatorArray::~AccumulatorArray()
{
  accumulator_array_dtor(this);
}



#endif

