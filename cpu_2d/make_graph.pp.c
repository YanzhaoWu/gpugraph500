/* Copyright (C) 2009-2010 The Trustees of Indiana University.             */
/*                                                                         */
/* Use, modification and distribution is subject to the Boost Software     */
/* License, Version 1.0. (See accompanying file LICENSE_1_0.txt or copy at */
/* http://www.boost.org/LICENSE_1_0.txt)                                   */
/*                                                                         */
/*  Authors: Jeremiah Willcock                                             */
/*           Andrew Lumsdaine                                              */

#include <stdlib.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <limits.h>
#include <assert.h>
#include <math.h>

#include <mpi.h>

/* Simplified interface to build graphs with scrambled vertices. */

#include "graph_generator.h"
#include "utils.h"

static void compute_edge_range(int rank, int size, int64_t M, int64_t* start_idx, int64_t* end_idx) {
  int64_t rankc = (int64_t)(rank);
  int64_t sizec = (int64_t)(size);
  *start_idx = rankc * (M / sizec) + (rankc < (M % sizec) ? rankc : (M % sizec));
  *end_idx = (rankc + 1) * (M / sizec) + (rankc + 1 < (M % sizec) ? rankc + 1 : (M % sizec));
}


void make_graph(int log_numverts,           // scale
                int64_t M,                  // desired number of edge
                uint64_t userseed1,         // seed 1 of random number generator
                uint64_t userseed2,         // seed 2 of random number generator
                int64_t* nedges_ptr,        // number of local edges
                packed_edge** result_ptr    // generated local edgelist
                ) {
  int rank, size;

  /* Spread the two 64-bit numbers into five nonzero values in the correct
   * range. */
  uint_fast32_t seed[5];
  make_mrg_seed(userseed1, userseed2, seed);

  MPI_Comm_rank((( MPI_Comm) ((void *) &( ompi_mpi_comm_world))), &rank);
  MPI_Comm_size((( MPI_Comm) ((void *) &( ompi_mpi_comm_world))), &size);

  int64_t start_idx, end_idx;
  compute_edge_range(rank, size, M, &start_idx, &end_idx);
  int64_t nedges = end_idx - start_idx;

  packed_edge* local_edges = (packed_edge*)xmalloc(nedges * sizeof(packed_edge));

  double start = MPI_Wtime();
  generate_kronecker_range(seed, log_numverts, start_idx, end_idx, local_edges);
  double gen_time = MPI_Wtime() - start;

  *result_ptr = local_edges;
  *nedges_ptr = nedges;

  if (rank == 0) {
    fprintf(stdout, "graph_generation:               %f s\n", gen_time);
  }
}

/* PRNG interface for implementations; takes seed in same format as given by
 * users, and creates a vector of doubles in a reproducible (and
 * random-access) way. */
void make_random_numbers(
       /* in */ int64_t nvalues    /* Number of values to generate */,
       /* in */ uint64_t userseed1 /* Arbitrary 64-bit seed value */,
       /* in */ uint64_t userseed2 /* Arbitrary 64-bit seed value */,
       /* in */ int64_t position   /* Start index in random number stream */,
       /* out */ double* result    /* Returned array of values */
) {
  int64_t i;
  uint_fast32_t seed[5];
  make_mrg_seed(userseed1, userseed2, seed);

  mrg_state st;
  mrg_seed(&st, seed);

  mrg_skip(&st, 2, 0, 2 * position); /* Each double takes two PRNG outputs */

  for (i = 0; i < nvalues; ++i) {
    result[i] = mrg_get_double_orig(&st);
  }
}
