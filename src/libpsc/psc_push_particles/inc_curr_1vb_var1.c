
// ======================================================================
// 1vb current deposition "var1"
//
// my original implementation, 2d (yz) only
// in theory less divergent, in particular for CUDA,
// but also definitely more complex

// ----------------------------------------------------------------------
// calc_3d_dx1

#if DIM == DIM_YZ

static inline void
calc_3d_dx1(particle_real_t dx1[3], particle_real_t x[3], particle_real_t dx[3], int off[3])
{
  if (off[2] == 0) {
    dx1[1] = .5f * off[1] - x[1];
    if (dx[1] == 0.f) {
      dx1[0] = 0.f;
      dx1[2] = 0.f;
    } else {
      dx1[0] = dx[0] / dx[1] * dx1[1];
      dx1[2] = dx[2] / dx[1] * dx1[1];
    }
  } else {
    dx1[2] = .5f * off[2] - x[2];
    if (dx[2] == 0.f) {
      dx1[0] = 0.f;
      dx1[1] = 0.f;
    } else {
      dx1[0] = dx[0] / dx[2] * dx1[2];
      dx1[1] = dx[1] / dx[2] * dx1[2];
    }
  }
}

static inline void
curr_3d_vb_cell(struct psc_fields *flds, int i[3], particle_real_t x[3], particle_real_t dx[3],
		particle_real_t fnq[3], particle_real_t dxt[3], int off[3])
{
  particle_real_t h = (1.f/12.f) * dx[0] * dx[1] * dx[2];
  particle_real_t xa[3] = { 0.,
			    x[1] + .5f * dx[1],
			    x[2] + .5f * dx[2], };
  F3_CURR(flds, JXI, 0,i[1]  ,i[2]  ) += fnq[0] * (dx[0] * (.5f - xa[1]) * (.5f - xa[2]) + h);
  F3_CURR(flds, JXI, 0,i[1]+1,i[2]  ) += fnq[0] * (dx[0] * (.5f + xa[1]) * (.5f - xa[2]) - h);
  F3_CURR(flds, JXI, 0,i[1]  ,i[2]+1) += fnq[0] * (dx[0] * (.5f - xa[1]) * (.5f + xa[2]) + h);
  F3_CURR(flds, JXI, 0,i[1]+1,i[2]+1) += fnq[0] * (dx[0] * (.5f + xa[1]) * (.5f + xa[2]) - h);

  F3_CURR(flds, JYI, 0,i[1]  ,i[2]  ) += fnq[1] * dx[1] * (.5f - xa[2]);
  F3_CURR(flds, JYI, 0,i[1]  ,i[2]+1) += fnq[1] * dx[1] * (.5f + xa[2]);

  F3_CURR(flds, JZI, 0,i[1]  ,i[2]  ) += fnq[2] * dx[2] * (.5f - xa[1]);
  F3_CURR(flds, JZI, 0,i[1]+1,i[2]  ) += fnq[2] * dx[2] * (.5f + xa[1]);

  if (dxt) {
    dxt[0] -= dx[0];
    dxt[1] -= dx[1];
    dxt[2] -= dx[2];
    x[1] += dx[1] - off[1];
    x[2] += dx[2] - off[2];
    i[1] += off[1];
    i[2] += off[2];
  }
}

// ----------------------------------------------------------------------
// calc_j / CALC_JXYZ_3D

#if PSC_PARTICLES_AS_CUDA2

static inline void
calc_j(struct psc_fields *flds, particle_real_t *xm, particle_real_t *xp,
       int *lf, int *lg, particle_t *prt, particle_real_t *vxi)
{
  int i[3] = { 0, lg[1], lg[2] };					
  int idiff[3] = { 0, lf[1] - lg[1], lf[2] - lg[2] };			
  particle_real_t dx[3] = { 0., xp[1] - xm[1], xp[2] - xm[2] };		
  particle_real_t x[3] = { 0., xm[1] - (i[1] + .5f), xm[2] - (i[2] + .5f) }; 
  									
  particle_real_t dx1[3];
  int off[3];
  int first_dir, second_dir = -1;
  /* FIXME, make sure we never div-by-zero? */
  if (idiff[1] == 0 && idiff[2] == 0) {
    first_dir = -1;
  } else if (idiff[1] == 0) {
    first_dir = 2;
  } else if (idiff[2] == 0) {
    first_dir = 1;
  } else {
    dx1[1] = .5f * idiff[1] - x[1];
    if (dx[1] == 0.f) {
      dx1[2] = 0.f;
    } else {
      dx1[2] = dx[2] / dx[1] * dx1[1];
    }
    if (particle_cuda2_real_abs(x[2] + dx1[2]) > .5f) {
      first_dir = 2;
    } else {
      first_dir = 1;
    }
    second_dir = 3 - first_dir;
  }

  int kind = cuda_float_as_int(prt->xi4.w);
  particle_real_t fnq[3] = { particle_cuda2_wni(prt) * prm.fnqx_kind[kind],
				   particle_cuda2_wni(prt) * prm.fnqy_kind[kind],
				   particle_cuda2_wni(prt) * prm.fnqz_kind[kind] };
  dx[0] = vxi[0] * prm.dt * prm.dxi[0];

  if (first_dir >= 0) {
    off[3 - first_dir] = 0;
    off[first_dir] = idiff[first_dir];
    calc_3d_dx1(dx1, x, dx, off);
    curr_3d_vb_cell(flds, i, x, dx1, fnq, dx, off);
  }

  if (second_dir >= 0) {
    off[first_dir] = 0;
    off[second_dir] = idiff[second_dir];
    calc_3d_dx1(dx1, x, dx, off);
    curr_3d_vb_cell(flds, i, x, dx1, fnq, dx, off);
  }

  curr_3d_vb_cell(flds, i, x, dx, fnq, NULL, NULL);
}

#else

static inline void
calc_j(struct psc_fields *flds, particle_real_t *xm, particle_real_t *xp,
       int *lf, int *lg, particle_t *prt, particle_real_t *vxi)
{
  int i[3] = { 0, lg[1], lg[2] };
  int idiff[3] = { 0, lf[1] - lg[1], lf[2] - lg[2] };
  particle_real_t dx[3] = { 0., xp[1] - xm[1], xp[2] - xm[2] };
  particle_real_t x[3] = { 0., xm[1] - (i[1] + .5f), xm[2] - (i[2] + .5f) };

  particle_real_t dx1[3];
  int off[3];
  int first_dir, second_dir = -1;
  /* FIXME, make sure we never div-by-zero? */
  if (idiff[1] == 0 && idiff[2] == 0) {
    first_dir = -1;
  } else if (idiff[1] == 0) {
    first_dir = 2;
  } else if (idiff[2] == 0) {
    first_dir = 1;
  } else {
    dx1[1] = .5f * idiff[1] - x[1];
    if (dx[1] == 0.f) {
      dx1[2] = 0.f;
    } else {
      dx1[2] = dx[2] / dx[1] * dx1[1];
    }
    if (particle_real_abs(x[2] + dx1[2]) > .5f) {
      first_dir = 2;
    } else {
      first_dir = 1;
    }
    second_dir = 3 - first_dir;
  }

  particle_real_t fnq[3] = { particle_wni(prt) * prm.fnqx_kind[prt->kind],
			     particle_wni(prt) * prm.fnqy_kind[prt->kind],
			     particle_wni(prt) * prm.fnqz_kind[prt->kind] };
  dx[0] = vxi[0] * prm.dt * prm.dxi[0];

  if (first_dir >= 0) {
    off[3 - first_dir] = 0;
    off[first_dir] = idiff[first_dir];
    calc_3d_dx1(dx1, x, dx, off);
    curr_3d_vb_cell(flds, i, x, dx1, fnq, dx, off);
  }

  if (second_dir >= 0) {
    off[first_dir] = 0;
    off[second_dir] = idiff[second_dir];
    calc_3d_dx1(dx1, x, dx, off);
    curr_3d_vb_cell(flds, i, x, dx1, fnq, dx, off);
  }

  curr_3d_vb_cell(flds, i, x, dx, fnq, NULL, NULL);
}

#endif

#endif // DIM

// ======================================================================
// TBD (FIXME)

#if DIM == DIM_YZ

// ----------------------------------------------------------------------
// calc_dx1

static inline void
calc_dx1(particle_real_t dx1[2], particle_real_t x[2], particle_real_t dx[2], int off[2])
{
  if (off[1] == 0) {
    dx1[0] = .5f * off[0] - x[0];
   if (dx[0] == 0.f) {
    dx1[1] = 0.f;
   } else {
    dx1[1] = dx[1] / dx[0] * dx1[0];
   }
  } else {
    dx1[1] = .5f * off[1] - x[1];
   if (dx[1] == 0.f) {
    dx1[0] = 0.f;
   } else {
    dx1[0] = dx[0] / dx[1] * dx1[1];
   }
  }
}

static inline void
curr_2d_vb_cell(struct psc_fields *flds, int i[2], particle_real_t x[2], particle_real_t dx[2],
		particle_real_t fnq[2], particle_real_t dxt[2], int off[2])
{
  F3_CURR(flds, JYI, 0,i[0],i[1]  ) += fnq[0] * dx[0] * (.5f - x[1] - .5f * dx[1]);
  F3_CURR(flds, JYI, 0,i[0],i[1]+1) += fnq[0] * dx[0] * (.5f + x[1] + .5f * dx[1]);
  F3_CURR(flds, JZI, 0,i[0],i[1]  ) += fnq[1] * dx[1] * (.5f - x[0] - .5f * dx[0]);
  F3_CURR(flds, JZI, 0,i[0]+1,i[1]) += fnq[1] * dx[1] * (.5f + x[0] + .5f * dx[0]);
  if (dxt) {
    dxt[0] -= dx[0];
    dxt[1] -= dx[1];
    x[0] += dx[0] - off[0];
    x[1] += dx[1] - off[1];
    i[0] += off[0];
    i[1] += off[1];
  }
}

#define CALC_JX_2D(flds, part, vxi)					\
  do {									\
    int lf[3];								\
    particle_real_t of[3];						\
    find_idx_off_1st_rel(&part->xi, lf, of, 0.f, prm.dxi);		\
    									\
    particle_real_t fnqx = vxi[0] * particle_wni(part) * prm.fnqx_kind[part->kind]; \
    F3_CURR(flds, JXI, 0,lf[1]  ,lf[2]  ) += (1.f - of[1]) * (1.f - of[2]) * fnqx; \
    F3_CURR(flds, JXI, 0,lf[1]+1,lf[2]  ) += (      of[1]) * (1.f - of[2]) * fnqx; \
    F3_CURR(flds, JXI, 0,lf[1]  ,lf[2]+1) += (1.f - of[1]) * (      of[2]) * fnqx; \
    F3_CURR(flds, JXI, 0,lf[1]+1,lf[2]+1) += (      of[1]) * (      of[2]) * fnqx; \
  } while (0)

#define CALC_JYZ_2D(flds, xm, xp)						\
  do {									\
    int i[2] = { lg[1], lg[2] };					\
    int idiff[2] = { lf[1] - lg[1], lf[2] - lg[2] };			\
    particle_real_t dx[2] = { xp[1] - xm[1], xp[2] - xm[2] };		\
    particle_real_t x[2] = { xm[1] - (i[0] + .5f), xm[2] - (i[1] + .5f) }; \
    									\
    particle_real_t dx1[2];						\
    int off[2];								\
    int first_dir, second_dir = -1;					\
    /* FIXME, make sure we never div-by-zero? */			\
    if (idiff[0] == 0 && idiff[1] == 0) {				\
      first_dir = -1;							\
    } else if (idiff[0] == 0) {						\
      first_dir = 1;							\
    } else if (idiff[1] == 0) {						\
      first_dir = 0;							\
    } else {								\
      dx1[0] = .5f * idiff[0] - x[0];					\
      if (dx[0] == 0.f) {						\
	dx1[1] = 0.f;							\
    } else {								\
	dx1[1] = dx[1] / dx[0] * dx1[0];				\
      }									\
      if (particle_real_abs(x[1] + dx1[1]) > .5f) {			\
	first_dir = 1;							\
      } else {								\
	first_dir = 0;							\
      }									\
      second_dir = 1 - first_dir;					\
    }									\
    									\
    particle_real_t fnq[2] = { particle_wni(prt) * prm.fnqy_kind[prt->kind], \
			       particle_wni(prt) * prm.fnqz_kind[prt->kind] }; \
    									\
    if (first_dir >= 0) {						\
      off[1-first_dir] = 0;						\
      off[first_dir] = idiff[first_dir];				\
      calc_dx1(dx1, x, dx, off);					\
      curr_2d_vb_cell(flds, i, x, dx1, fnq, dx, off);			\
    }									\
    									\
    if (second_dir >= 0) {						\
      off[first_dir] = 0;						\
      off[second_dir] = idiff[second_dir];				\
      calc_dx1(dx1, x, dx, off);					\
      curr_2d_vb_cell(flds, i, x, dx1, fnq, dx, off);			\
    }									\
    									\
    curr_2d_vb_cell(flds, i, x, dx, fnq, NULL, NULL);			\
  } while (0)

#endif

