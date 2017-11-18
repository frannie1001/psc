
#include "vpic_iface.h"

#include "vpic_push_particles.h"
#include <vpic.h>

#include <mrc_common.h>
#include <cassert>


// ----------------------------------------------------------------------
// vpic_base_init

void vpic_base_init(int *pargc, char ***pargv)
{
  static bool vpic_base_inited = false;

  if (vpic_base_inited) {
    return;
  }
  vpic_base_inited = true;
  
  //  boot_services( &argc, &argv );
  {
    // Start up the checkpointing service.  This should be first.
    
    boot_checkpt( pargc, pargv );
    
    serial.boot( pargc, pargv );
    thread.boot( pargc, pargv );
    
    // Boot up the communications layer
    // See note above about thread-core-affinity
    
    boot_mp( pargc, pargv );
    
    mp_barrier();
    _boot_timestamp = 0;
    _boot_timestamp = uptime();
  }
}

void
vpic_simulation_get_info(vpic_simulation *vpic, struct vpic_simulation_info *info)
{
  info->num_step = vpic->num_step;

  // grid
  info->dt = vpic->grid->dt;
  info->nx[0] = vpic->grid->nx;
  info->nx[1] = vpic->grid->ny;
  info->nx[2] = vpic->grid->nz;
  info->dx[0] = vpic->grid->dx;
  info->dx[1] = vpic->grid->dy;
  info->dx[2] = vpic->grid->dz;
  info->x0[0] = vpic->grid->x0;
  info->x0[1] = vpic->grid->y0;
  info->x0[2] = vpic->grid->z0;
  info->x1[0] = vpic->grid->x1;
  info->x1[1] = vpic->grid->y1;
  info->x1[2] = vpic->grid->z1;

  // species
  info->n_kinds = num_species(vpic->species_list);
  info->kinds = new vpic_kind_info[info->n_kinds];
  species_t *sp;
  LIST_FOR_EACH( sp, vpic->species_list ) {
    info->kinds[sp->id].q = sp->q;
    info->kinds[sp->id].m = sp->m;
    info->kinds[sp->id].name = sp->name;
  }
  
  // Marder cleaning etc
  info->clean_div_e_interval = vpic->clean_div_e_interval;
  info->clean_div_b_interval = vpic->clean_div_b_interval;
  info->sync_shared_interval = vpic->sync_shared_interval;
  info->num_div_e_round = vpic->num_div_e_round;
  info->num_div_b_round = vpic->num_div_b_round;

  info->status_interval = vpic->status_interval;
}

vpic_simulation *vpic_simulation_new()
{
  extern vpic_simulation *simulation;
  assert(!simulation);

  if( world_rank==0 ) log_printf( "*** Initializing\n" );
  simulation = new vpic_simulation;
  return simulation;
}

void vpic_simulation_user_initialization(vpic_simulation *vpic)
{
  // Call the user to initialize the simulation
  TIC vpic->user_initialization(0, 0); TOC( user_initialization, 1 );
}

// ======================================================================
// vpic_diagnostics

void vpic_diagnostics(vpic_simulation *vpic)
{
  // Let the user compute diagnostics
  TIC vpic->user_diagnostics(); TOC( user_diagnostics, 1 );
}

// ======================================================================

void vpic_inc_step(vpic_simulation *vpic, int step)
{
  vpic->grid->step++;
  assert(vpic->grid->step == step);
}

// ======================================================================

void vpic_simulation_set_region_resistive_harris(vpic_simulation *vpic,
						 vpic_harris_params *prm,
						 globals_physics *phys,
						 double dx[3],
						 double thickness,
						 material_t *resistive)
{
  // Define resistive layer surrounding boundary --> set thickness=0
  // to eliminate this feature
#define resistive_layer ((prm->open_bc_x && x < dx[0]*thickness) ||	\
			 (prm->open_bc_x && x > phys->Lx-dx[0]*thickness)	\
                         || z <-phys->Lz/2+dx[2]*thickness  || z > phys->Lz/2-dx[2]*thickness )

  if (thickness > 0) {
    log_printf("Setting resistive layer of thickness %g", thickness);
    // FIXME!!!
    assert(0);
#if 0
#define field vpic->field
    grid_t *grid = vpic->grid;
    set_region_material(resistive_layer, resistive, resistive);
#undef field
#endif
  }
}

void vpic_simulation_inject_particle(vpic_simulation *vpic,
				     species_t * sp,
				     double x,  double y,  double z,
				     double ux, double uy, double uz,
				     double w,  double age, bool update_rhob)
{
  vpic->inject_particle(sp, x, y, z, ux, uy, uz, w, age, update_rhob);
}

