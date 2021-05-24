
#include "fields3d.hxx"
#include "psc_fields_cuda.h"

#include "cuda_base.cuh"

#include <fstream>

std::size_t mem_mfields()
{
  std::size_t mem = 0;
  for (auto mflds : MfieldsBase::instances) {
    auto mflds_cuda = dynamic_cast<MfieldsCuda*>(mflds);
    if (mflds_cuda) {
      auto dims = mflds->_grid().ldims + 2 * mflds->ibn();
      auto n_patches = mflds->_grid().n_patches();
      std::size_t bytes = sizeof(float) * dims[0] * dims[1] * dims[2] *
                          n_patches * mflds->_n_comps();
      // of << "===== MfieldsCuda # of components " << mflds->_n_comps()
      //           << " bytes " << bytes << "\n";
      mem += bytes;
    } else {
      // of << "===== MfieldsBase # of components " << mflds->_n_comps()
      //           << "\n";
    }
  }
  return mem;
}

void mem_stats(std::string file, int line, std::ostream& of)
{
  std::size_t mem_fields = mem_mfields();

  std::size_t total = mem_fields + mem_particles + mem_collisions + mem_sort +
                      mem_sort_by_block + mem_bnd + mem_heating;

  std::size_t allocated = mem_cuda_allocated();

  of << "===== MEM " << file << ":" << line << "\n";
  of << "===== fields     " << mem_fields << " bytes  # "
     << MfieldsBase::instances.size() << "\n";
  of << "===== particles  " << mem_particles << " bytes\n";
  of << "===== collisions " << mem_collisions << " bytes\n";
  of << "===== sort       " << mem_sort << " bytes\n";
  of << "===== sort_block " << mem_sort_by_block << " bytes\n";
  of << "===== bnd        " << mem_bnd << " bytes\n";
  of << "===== heating    " << mem_heating << " bytes\n";
  of << "===== alloced " << allocated << " total " << total << " unaccounted "
     << std::ptrdiff_t(allocated - total) << "\n";
}

void mem_stats_csv(std::ostream& of, int timestep, int n_patches, int n_prts)
{
  std::size_t mem_fields = mem_mfields();

  std::size_t total = mem_fields + mem_particles + mem_collisions + mem_sort +
                      mem_sort_by_block + mem_bnd + mem_heating;

  std::size_t allocated = mem_cuda_allocated();

  of << "========== STEP  " << timestep << "\n";
  of << "===== n_patches  " << n_patches << "\n";
  of << "===== n_prts     " << n_prts << "\n";

  of << "===== fields     " << mem_fields << " bytes  # "
     << MfieldsBase::instances.size() << "\n";
  of << "===== particles  " << mem_particles << " bytes\n";
  of << "===== collisions " << mem_collisions << " bytes\n";
  of << "===== sort       " << mem_sort << " bytes\n";
  of << "===== sort_block " << mem_sort_by_block << " bytes\n";
  of << "===== bnd        " << mem_bnd << " bytes\n";
  of << "===== heating    " << mem_heating << " bytes\n";
  of << "===== alloced " << allocated << " total " << total << " unaccounted "
     << std::ptrdiff_t(allocated - total) << "\n";
  of << std::endl;
}
