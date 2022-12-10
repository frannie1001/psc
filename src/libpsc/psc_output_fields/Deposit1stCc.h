
#pragma once

#include "psc_bits.h"
#include <psc/deposit.hxx>

#include <kg/Vec3.h>

// FIXME, this is still too intermingled, both doing the actual deposit as well
// as the particle / patch processing OTOH, there is still opportunity for
// optimization, in particular when operator() gets called back multiple times,
// we don't have to find the IP coefficients again Obviously, the rest of the IP
// macro should be converted, too

template <typename Mfields, typename D>
class Deposit1stCc
{
public:
  using dim_t = D;
  using real_t = typename Mfields::real_t;

  Deposit1stCc(const Grid_t& grid)
    : deposit_({grid.domain.dx[0], grid.domain.dx[1], grid.domain.dx[2]},
               grid.norm.fnqs)

  {}

  template <typename PRT>
  void operator()(Mfields& mflds, const PRT& prt, int m, real_t val)
  {
    auto ib = mflds.ib();
    deposit_(prt, mflds.storage().view(_all, _all, _all, m, p_), ib, val);
  }

  template <typename Mparticles, typename F>
  void process(const Mparticles& mprts, F&& func)
  {
    auto accessor = mprts.accessor();

    for (p_ = 0; p_ < mprts.n_patches(); p_++) {
      for (auto prt : accessor[p_]) {
        func(prt);
      }
    }
  }

private:
  int p_;
  psc::deposit::Deposit1stCc<real_t, dim_t> deposit_;
};
