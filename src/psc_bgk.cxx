
#include <psc.hxx>
#include <setup_fields.hxx>
#include <setup_particles.hxx>

#include "DiagnosticsDefault.h"
#include "OutputFieldsDefault.h"
#include "psc_config.hxx"

#include "psc_bgk_util/bgk_params.hxx"
#include "psc_bgk_util/input_parser.hxx"
#include "psc_bgk_util/params_parser.hxx"

// ======================================================================
// PSC configuration
//
// This sets up compile-time configuration for the code, in particular
// what data structures and algorithms to use
//
// EDIT to change order / floating point type / cuda / 2d/3d

using Dim = dim_yz;
#ifdef USE_CUDA
using PscConfig = PscConfig1vbecCuda<Dim>;
#else
using PscConfig = PscConfig1vbecSingle<Dim>;
#endif

// ----------------------------------------------------------------------

using BgkMfields = PscConfig::Mfields;
using MfieldsState = PscConfig::MfieldsState;
using Mparticles = PscConfig::Mparticles;
using Balance = PscConfig::Balance;
using Collision = PscConfig::Collision;
using Checks = PscConfig::Checks;
using Marder = PscConfig::Marder;
using OutputParticles = PscConfig::OutputParticles;

// for parser
enum DATA_COL
{
  COL_RHO,
  COL_NE,
  COL_V_PHI,
  COL_TE,
  COL_E_RHO,
  COL_PHI,
  n_cols
};

// ======================================================================
// Global parameters

namespace
{
Parsed<4401, n_cols> parsed(COL_RHO);

std::string read_checkpoint_filename;

// Parameters specific to BGK simulation
PscBgkParams g;

// General PSC parameters (see include/psc.hxx),
PscParams psc_params;

} // namespace

// ======================================================================
// setupParameters

void setupParameters()
{
  ParsedParams parsedParams("../../src/psc_bgk_params.txt");
  g.loadParams(parsedParams);
  parsed.loadData(parsedParams.get<std::string>("path_to_data"), 1);

  psc_params.nmax = parsedParams.get<int>("nmax");
  psc_params.stats_every = parsedParams.get<int>("stats_every");

  // -- start from checkpoint:
  //
  // Uncomment when wanting to start from a checkpoint, ie.,
  // instead of setting up grid, particles and state fields here,
  // they'll be read from a file
  // FIXME: This parameter would be a good candidate to be provided
  // on the command line, rather than requiring recompilation when change.

  // read_checkpoint_filename = "checkpoint_500.bp";
}

// ======================================================================
// setupGrid
//
// This helper function is responsible for setting up the "Grid",
// which is really more than just the domain and its decomposition, it
// also encompasses PC normalization parameters, information about the
// particle kinds, etc.

Grid_t* setupGrid()
{
  auto domain = Grid_t::Domain{
    {1, g.n_grid, g.n_grid},                  // # grid points
    {1, g.box_size, g.box_size},              // physical lengths
    {0., -.5 * g.box_size, -.5 * g.box_size}, // *offset* for origin
    {1, g.n_patches, g.n_patches}};           // # patches

  auto bc =
    psc::grid::BC{{BND_FLD_PERIODIC, BND_FLD_PERIODIC, BND_FLD_PERIODIC},
                  {BND_FLD_PERIODIC, BND_FLD_PERIODIC, BND_FLD_PERIODIC},
                  {BND_PRT_PERIODIC, BND_PRT_PERIODIC, BND_PRT_PERIODIC},
                  {BND_PRT_PERIODIC, BND_PRT_PERIODIC, BND_PRT_PERIODIC}};

  auto kinds = Grid_t::Kinds(NR_KINDS);
  kinds[KIND_ELECTRON] = {g.q_e, g.m_e, "e"};
  kinds[KIND_ION] = {g.q_i, g.m_i, "i"};

  mpi_printf(MPI_COMM_WORLD, "lambda_D = %g\n",
             sqrt(parsed.get_interpolated(COL_TE, g.box_size / sqrt(2))));

  // --- generic setup
  auto norm_params = Grid_t::NormalizationParams::dimensionless();
  norm_params.nicell = 100;

  double dt = psc_params.cfl * courant_length(domain);
  Grid_t::Normalization norm{norm_params};

  Int3 ibn = {2, 2, 2};
  if (Dim::InvarX::value) {
    ibn[0] = 0;
  }
  if (Dim::InvarY::value) {
    ibn[1] = 0;
  }
  if (Dim::InvarZ::value) {
    ibn[2] = 0;
  }

  return new Grid_t{domain, bc, kinds, norm, dt, -1, ibn};
}

// ======================================================================
// writeGT

template <typename GT>
void writeGT(const GT& gt, const Grid_t& grid, const std::string& name,
             const std::vector<std::string>& compNames)
{
  WriterMRC writer;
  writer.open(name);
  writer.begin_step(grid.timestep(), grid.timestep() * grid.dt);
  writer.write(gt, grid, name, compNames);
  writer.end_step();
  writer.close();
}

// ----------------------------------------------------------------------
// writeMF

template <typename MF>
void writeMF(MF&& mfld, const std::string& name,
             const std::vector<std::string>& compNames)
{
  writeGT(view_interior(mfld.gt(), mfld.ibn()), mfld.grid(), name, compNames);
}

// ======================================================================
// helper methods

inline double getCoord(double crd)
{
  if (crd < -g.box_size / 2)
    return crd + g.box_size;
  if (crd > g.box_size / 2)
    return crd - g.box_size;
  return crd;
}

template <int LEN>
inline void setAll(double (&vals)[LEN], double newval)
{
  for (int i = 0; i < LEN; i++)
    vals[i] = newval;
}

// ======================================================================
// initializeParticles

void initializeParticles(Balance& balance, Grid_t*& grid_ptr, Mparticles& mprts,
                         BgkMfields& divGradPhi)
{
  SetupParticles<Mparticles> setup_particles(*grid_ptr);
  setup_particles.centerer = Centering::Centerer(Centering::NC);

  auto&& qDensity = -view_interior(divGradPhi.gt(), divGradPhi.ibn());

  auto npt_init = [&](int kind, double crd[3], int p, Int3 idx,
                      psc_particle_npt& npt) {
    double y = getCoord(crd[1]);
    double z = getCoord(crd[2]);
    double rho = sqrt(sqr(y) + sqr(z));

    switch (kind) {

      case KIND_ELECTRON:
        npt.n =
          (qDensity(idx[0], idx[1], idx[2], 0, p) - g.n_i * g.q_i) / g.q_e;
        if (rho != 0) {
          double v_phi = parsed.get_interpolated(COL_V_PHI, rho);
          double sign =
            (g.reverse_v ? -1 : 1) * (g.reverse_v_half && y < 0 ? -1 : 1);
          npt.p[0] = 0;
          npt.p[1] = sign * g.m_e * v_phi * -z / rho;
          npt.p[2] = sign * g.m_e * v_phi * y / rho;
        } else {
          setAll(npt.p, 0);
        }
        setAll(npt.T, parsed.get_interpolated(COL_TE, rho));
        break;

      case KIND_ION:
        npt.n = g.n_i;
        setAll(npt.p, 0);
        setAll(npt.T, 0);
        break;

      default: assert(false);
    }
  };

  partitionParticlesGeneralInit(setup_particles, balance, grid_ptr, mprts,
                                npt_init);
  setupParticlesGeneralInit(setup_particles, mprts, npt_init);
}

// ======================================================================
// fillGhosts

template <typename MF>
void fillGhosts(MF& mfld, int compBegin, int compEnd)
{
  auto ibn = mfld.ibn();
  int ibn_arr[3] = {ibn[0], ibn[1], ibn[2]};
  Bnd_<MF> bnd{mfld.grid(), ibn_arr};
  bnd.fill_ghosts(mfld, compBegin, compEnd);
}

// ======================================================================
// initializePhi

void initializePhi(BgkMfields& phi)
{
  setupScalarField(
    phi, Centering::Centerer(Centering::NC), [&](int m, double crd[3]) {
      double rho = sqrt(sqr(getCoord(crd[1])) + sqr(getCoord(crd[2])));
      return parsed.get_interpolated(COL_PHI, rho);
    });

  writeMF(phi, "phi", {"phi"});
}

// ======================================================================
// initializeGradPhi

void initializeGradPhi(BgkMfields& phi, BgkMfields& gradPhi)
{
  auto&& grad = psc::item::grad_ec(phi.gt(), phi.grid());
  view_interior(gradPhi.storage(), phi.ibn()) = grad;

  fillGhosts(gradPhi, 0, 3);

  writeMF(gradPhi, "grad_phi", {"gradx", "grady", "gradz"});
}

// ======================================================================
// initializeDivGradPhi

void initializeDivGradPhi(BgkMfields& gradPhi, BgkMfields& divGradPhi)
{
  auto&& divGrad = psc::item::div_nc(gradPhi.gt(), gradPhi.grid());
  view_interior(divGradPhi.storage(), gradPhi.ibn()) = divGrad;

  fillGhosts(divGradPhi, 0, 1);

  writeMF(divGradPhi, "div_grad_phi", {"divgrad"});
}

// ======================================================================
// initializeFields

void initializeFields(MfieldsState& mflds, BgkMfields& gradPhi)
{
  setupFields(mflds, [&](int m, double crd[3]) {
    switch (m) {
      case HX: return g.Hx;
      default: return 0.;
    }
  });

  // initialize E separately
  mflds.storage().view(_all, _all, _all, _s(EX, EX + 3)) = -gradPhi.gt();
}

// ======================================================================
// run

static void run()
{
  mpi_printf(MPI_COMM_WORLD, "*** Setting up...\n");

  // ----------------------------------------------------------------------
  // setup various parameters first

  setupParameters();

  // ----------------------------------------------------------------------
  // Set up grid, state fields, particles

  auto grid_ptr = setupGrid();
  auto& grid = *grid_ptr;
  MfieldsState mflds{grid};
  Mparticles mprts{grid};
  BgkMfields phi{grid, 1, mflds.ibn()};
  BgkMfields gradPhi{grid, 3, mflds.ibn()};
  BgkMfields divGradPhi{grid, 1, mflds.ibn()};

  // ----------------------------------------------------------------------
  // Set up various objects needed to run this case

  // -- Balance
  psc_params.balance_interval = 0;
  Balance balance{psc_params.balance_interval, .1};

  // -- Sort
  psc_params.sort_interval = 10;

  // -- Collision
  int collision_interval = 10;
  double collision_nu = .1;
  Collision collision{grid, collision_interval, collision_nu};

  // -- Checks
  ChecksParams checks_params{};
  checks_params.gauss_every_step = 200;
  // checks_params.gauss_dump_always = true;
  checks_params.gauss_threshold = 0;

  Checks checks{grid, MPI_COMM_WORLD, checks_params};

  // -- Marder correction
  double marder_diffusion = 0.9;
  int marder_loop = 3;
  bool marder_dump = false;
  psc_params.marder_interval = 1; // 5
  Marder marder(grid, marder_diffusion, marder_loop, marder_dump);

  // ----------------------------------------------------------------------
  // Set up output
  //
  // FIXME, this really is too complicated and not very flexible

  // -- output fields
  OutputFieldsParams outf_params{};
  outf_params.fields.pfield_interval = 200;
  outf_params.moments.pfield_interval = 200;
  OutputFields<MfieldsState, Mparticles, Dim> outf{grid, outf_params};

  // -- output particles
  OutputParticlesParams outp_params{};
  outp_params.every_step = 0;
  outp_params.data_dir = ".";
  outp_params.basename = "prt";
  OutputParticles outp{grid, outp_params};

  int oute_interval = -100;
  DiagEnergies oute{grid.comm(), oute_interval};

  auto diagnostics = makeDiagnosticsDefault(outf, outp, oute);

  // ----------------------------------------------------------------------
  // Set up initial conditions

  if (read_checkpoint_filename.empty()) {
    initializePhi(phi);
    initializeGradPhi(phi, gradPhi);
    initializeDivGradPhi(gradPhi, divGradPhi);
    initializeParticles(balance, grid_ptr, mprts, divGradPhi);
    initializeFields(mflds, gradPhi);
  } else {
    read_checkpoint(read_checkpoint_filename, *grid_ptr, mprts, mflds);
  }

  // ----------------------------------------------------------------------
  // Hand off to PscIntegrator to run the simulation

  auto psc =
    makePscIntegrator<PscConfig>(psc_params, *grid_ptr, mflds, mprts, balance,
                                 collision, checks, marder, diagnostics);

  psc.integrate();
}

// ======================================================================
// main

int main(int argc, char** argv)
{
  psc_init(argc, argv);

  run();

  psc_finalize();
  return 0;
}
