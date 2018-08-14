
#ifndef VPIC_SIMULATION_BASE_H
#define VPIC_SIMULATION_BASE_H

#include "VpicInterpolatorBase.h"
#include "VpicParticlesBase.h"
#include "VpicDiag.h"

// FIXME, the casts below will happily do the wrong thing if
// this the underlying base types aren't vpic / vpic-compatible layout

template<class Particles>
class VpicSimulationMixin : protected vpic_simulation
{
  typedef typename Particles::Grid Grid;

public:
  VpicSimulationMixin()
  {
    extern vpic_simulation *simulation;
    assert(!simulation);
    simulation = this;
  }

  void diagnostics()
  {
    TIC user_diagnostics(); TOC(user_diagnostics, 1);
  }
  
  void emitter()
  {
    if (emitter_list)
      TIC ::apply_emitter_list(emitter_list); TOC(emission_model, 1);
    TIC user_particle_injection(); TOC(user_particle_injection, 1);
  }

  void collision_run()
  {
    // Note: Particles should not have moved since the last performance sort
    // when calling collision operators.
    // FIXME: Technically, this placement of the collision operators only
    // yields a first order accurate Trotter factorization (not a second
    // order accurate factorization).
    
    if (collision_op_list) {
      // FIXME: originally, vpic_clear_accumulator_array() was called before this.
      // It's now called later, though. I'm not sure why that would be necessary here,
      // but it needs to be checked.
      // The assert() below doesn't unfortunately catch all cases where this might go wrong
      // (ie., it's missing the user_particle_collisions())
      
      assert(0);
      TIC ::apply_collision_op_list(collision_op_list); TOC(collision_model, 1);
    }
    TIC user_particle_collisions(); TOC(user_particle_collisions, 1);
  }

  void current_injection()
  {
    user_current_injection();
  }

  void field_injection()
  {
    user_field_injection();
  }
  
  void setParams(int num_step_, int status_interval_,
		 int sync_shared_interval_, int clean_div_e_interval_,
		 int clean_div_b_interval_)
  {
    num_step             = num_step_;
    status_interval      = status_interval_;
    sync_shared_interval = sync_shared_interval_;
    clean_div_e_interval = clean_div_e_interval_;
    clean_div_b_interval = clean_div_b_interval_;
  }

  void setTopology(int px_, int py_, int pz_)
  {
    px = px_; py = py_; pz = pz_;
  }

};


#endif
