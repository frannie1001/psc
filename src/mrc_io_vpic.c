
#include "mrc_io_private.h"
#include <mrc_params.h>

#include <stdio.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <errno.h>
#include <string.h>
#include <assert.h>

#define DIMENSION (3) // Grid and vector
#define MAX_FIELD_VARS (10)
#define MAX_SPECIES (10)
#define HEADER_SIZE (123)

enum {
  VPIC_FIELD            = 1,    // Field data file
  VPIC_HYDRO            = 2,    // Hydro data file
};

enum { // Structure types
  CONSTANT              = 0,    // Structure types
  SCALAR                = 1,
  VECTOR                = 2,
  TENSOR                = 3,
  TENSOR9               = 4,
};

static const char * struct_type_to_string[] = {
  [CONSTANT] = "CONSTANT",
  [SCALAR]   = "SCALAR",
  [VECTOR]   = "VECTOR",
  [TENSOR]   = "TENSOR",
  [TENSOR9]  = "TENSOR",
};

static int struct_type_to_comp_size[] = {
  [CONSTANT] = 1,
  [SCALAR]   = 1,
  [VECTOR]   = 3,
  [TENSOR]   = 6,
  [TENSOR9]  = 9,
};

enum { // Basic data types
  FLOAT                 = 0,    
  INTEGER               = 1,
};

static const char *basic_type_to_string[] = {
  [FLOAT]   = "FLOATING_POINT",
  [INTEGER] = "INTEGER",
};

static int basic_type_to_byte_count[] = {
  [FLOAT]   = sizeof(float),
  [INTEGER] = sizeof(int),
};

// ======================================================================
// mrc_io type "vpic"

struct field_var {
  char *name;
  int struct_type;
  int basic_type;
};

struct dataset {
  char *directory;
  char *base_filename;
  int n_vars;
  struct field_var vars[MAX_FIELD_VARS];
  FILE **files;
};

struct mrc_io_vpic_global {
  float delta_t;
  float cvac;
  float eps;
  double extents[3][2];
  double delta_x[3];
  int topology[3];
};

struct mrc_io_vpic {
  // parameters
  int proc_field_len;
  int time_field_len;

  bool global_in_progress;
  bool global_done;
  struct mrc_io_vpic_global global;

  int nr_patches;
  float *buf;

  struct dataset fields;

  int n_species;
  struct dataset species[MAX_SPECIES];
};

#define mrc_io_vpic(io) mrc_to_subobj(io, struct mrc_io_vpic)

// ======================================================================

static bool
is_hydro_var(struct mrc_fld *fld, int *p_n_comp, int *p_skip, char **p_name)
{
  int n_comp, skip;
  char *name;
  if (strncmp(mrc_fld_name(fld), "n_", 2) == 0) {
    n_comp = 1;
    skip = 2;
    name = "n";
  } else if (strncmp(mrc_fld_name(fld), "v_", 2) == 0) {
    n_comp = 3;
    skip = 2;
    name = "v";
  } else if (strncmp(mrc_fld_name(fld), "p_", 2) == 0) {
    n_comp = 3;
    skip = 2;
    name = "p";
  } else if (strncmp(mrc_fld_name(fld), "T_", 2) == 0) {
    n_comp = 6;
    skip = 2;
    name = "T";
  } else {
    return false;
  }

  if (p_n_comp) *p_n_comp = n_comp;
  if (p_skip) *p_skip = skip;
  if (p_name) *p_name = name;
  return true;
}

static void
mkdir_comm(const char *path, MPI_Comm comm)
{
  int rank;
  MPI_Comm_rank(comm, &rank);
  if (rank == 0) {
    //mprintf("MKDIR %s\n", path);
    if (mkdir(path, 0777)) {
      if (errno != EEXIST) {
	perror("ERROR: mkdir");
	assert(0);
      }
    }
  }
  MPI_Barrier(comm);
}

// ======================================================================
// writing the global file

// ----------------------------------------------------------------------
// mrc_io_vpic_print_vars

static void
mrc_io_vpic_print_vars(struct mrc_io *io, FILE *file, struct dataset *ds)
{
  for (int n = 0; n < ds->n_vars; n++) {
    struct field_var *f = &ds->vars[n];
    fprintf(file, "\"%s\" %s %d %s %d\n",
	    f->name,
	    struct_type_to_string[f->struct_type],
	    struct_type_to_comp_size[f->struct_type],
	    basic_type_to_string[f->basic_type],
	    basic_type_to_byte_count[f->basic_type]);
  }
}

// ----------------------------------------------------------------------
// mrc_io_vpic_write_global_file

static void
mrc_io_vpic_write_global_file(struct mrc_io *io, FILE *file)
{
  struct mrc_io_vpic *sub = mrc_io_vpic(io);
  struct mrc_io_vpic_global *g = &sub->global;

  fprintf(file, "# generated by libmrc mrc_io 'vpic' writer'\n");
  fprintf(file, "VPIC_HEADER_VERSION %s\n", "mrc_io_vpic_1");
  fprintf(file, "DATA_HEADER_SIZE %d\n", HEADER_SIZE);
  fprintf(file, "GRID_DELTA_T %g\n", g->delta_t);
  fprintf(file, "GRID_CVAC %g\n", g->cvac);
  fprintf(file, "GRID_EPS %g\n", g->eps);
  fprintf(file, "GRID_EXTENTS_X %g %g\n", g->extents[0][0], g->extents[0][1]);
  fprintf(file, "GRID_EXTENTS_Y %g %g\n", g->extents[1][0], g->extents[1][1]);
  fprintf(file, "GRID_EXTENTS_Z %g %g\n", g->extents[2][0], g->extents[2][1]);
  fprintf(file, "GRID_DELTA_X %g\n", g->delta_x[0]);
  fprintf(file, "GRID_DELTA_Y %g\n", g->delta_x[1]);
  fprintf(file, "GRID_DELTA_Z %g\n", g->delta_x[02]);
  fprintf(file, "GRID_TOPOLOGY_X %d\n", g->topology[0]);
  fprintf(file, "GRID_TOPOLOGY_Y %d\n", g->topology[1]);
  fprintf(file, "GRID_TOPOLOGY_Z %d\n", g->topology[2]);
  fprintf(file, "FIELD_DATA_DIRECTORY %s\n", sub->fields.directory);
  fprintf(file, "FIELD_DATA_BASE_FILENAME %s\n", sub->fields.base_filename);
  fprintf(file, "FIELD_DATA_VARIABLES %d\n", sub->fields.n_vars);
  mrc_io_vpic_print_vars(io, file, &sub->fields);
  fprintf(file, "NUM_OUTPUT_SPECIES %d\n", sub->n_species);
  for (int s = 0; s < sub->n_species; s++) {
    struct dataset *ds = &sub->species[s];
    fprintf(file, "SPECIES_DATA_DIRECTORY %s\n", ds->directory);
    fprintf(file, "SPECIES_DATA_BASE_FILENAME %s\n", ds->base_filename);
    fprintf(file, "HYDRO_DATA_VARIABLES %d\n", ds->n_vars);
    mrc_io_vpic_print_vars(io, file, ds);
  }
}

// ----------------------------------------------------------------------
// mrc_io_vpic_open_global

static void
mrc_io_vpic_open_global(struct mrc_io *io) 
{
  struct mrc_io_vpic *sub = mrc_io_vpic(io);

  if (!sub->global_done) {
    sub->global_in_progress = true;
    sub->global.eps = 1.f; // FIXME?
  }
}

// ----------------------------------------------------------------------
// mrc_io_vpic_close_global

static void
mrc_io_vpic_close_global(struct mrc_io *io)
{
  struct mrc_io_vpic *sub = mrc_io_vpic(io);

  if (sub->global_in_progress) {
    char filename[strlen(io->par.outdir) + strlen(io->par.basename) + 10];
    sprintf(filename, "%s/%s.vpc", io->par.outdir, io->par.basename);
    FILE *global_file = fopen(filename, "w");
    assert(global_file);
    mrc_io_vpic_write_global_file(io, global_file);
    fclose(global_file);
    sub->global_done = true;
    sub->global_in_progress = false;
  }
}

// ----------------------------------------------------------------------
// mrc_io_vpic_add_var

static void
mrc_io_vpic_add_var(struct mrc_io *io, struct dataset *ds, const char *name,
		    int nr_comps)
{
  int n = ds->n_vars;
  assert(n < MAX_FIELD_VARS);

  if (n == 0) { // create dir, only first time around
    char dirname[strlen(io->par.outdir) + strlen(ds->directory) + 10];
    sprintf(dirname, "%s/%s", io->par.outdir, ds->directory);
    mkdir_comm(dirname, mrc_io_comm(io));
  }

  ds->vars[n].name = strdup(name);

  switch (nr_comps) {
  case 1: ds->vars[n].struct_type = SCALAR; break;
  case 3: ds->vars[n].struct_type = VECTOR; break;
  case 6: ds->vars[n].struct_type = TENSOR; break;
  default:
    mprintf("ERROR: unhandled nr_comps %d\n", nr_comps);
    assert(0);
  }
  ds->vars[n].basic_type = FLOAT;
  ds->n_vars++;
}

// ----------------------------------------------------------------------
// mrc_io_vpic_write_fld_global

static void
mrc_io_vpic_write_fld_global(struct mrc_io *io, struct mrc_fld *fld)
{
  struct mrc_io_vpic *sub = mrc_io_vpic(io);

  if (!sub->global_in_progress) {
    return;
  }

  struct mrc_io_vpic_global *g = &sub->global;
  
  struct mrc_domain *domain = fld->_domain;
  mrc_domain_get_param_int3(domain, "np", g->topology);
  struct mrc_crds *crds = mrc_domain_get_crds(domain);
  double xl[3], xh[3];
  // FIXME: "base" only really makes sense for amr..
  mrc_crds_get_dx_base(crds, g->delta_x);
  mrc_crds_get_param_double3(crds, "l", xl);
  mrc_crds_get_param_double3(crds, "h", xh);
  for (int d = 0; d < 3; d++) {
    g->extents[d][0] = xl[d];
    g->extents[d][1] = xh[d];
  }

  // is this field a moment (hydro var)?
  int n_comp, skip;
  char *name;
  if (is_hydro_var(fld, &n_comp, &skip, &name)) {
    int n_species = mrc_fld_nr_comps(fld) / n_comp;

    if (sub->n_species == 0) {
      sub->n_species = n_species;
      for (int s = 0; s < n_species; s++) {
	struct dataset *ds = &sub->species[s];
	ds->directory = strdup("hydro");
	ds->base_filename = strdup(mrc_fld_comp_name(fld, s) + skip);
      }
    } else {
      assert(sub->n_species == n_species);
    }
    for (int s = 0; s < n_species; s++) {
      mrc_io_vpic_add_var(io, &sub->species[s], name, n_comp);
    }
  } else {
    // otherwise, must be field variable
    if (sub->fields.n_vars == 0) {
      sub->fields.directory = strdup("fields");
      sub->fields.base_filename = strdup("fields");
    }
    mrc_io_vpic_add_var(io, &sub->fields, mrc_fld_name(fld), mrc_fld_nr_comps(fld));
  }
}

// ----------------------------------------------------------------------
// mrc_io_vpic_destroy_dataset

static void
mrc_io_vpic_destroy_dataset(struct mrc_io *io, struct dataset *ds)
{
  free(ds->directory);
  free(ds->base_filename);
  for (int n = 0; n < ds->n_vars; n++) {
    free(ds->vars[n].name);
  }
  assert(!ds->files);
}

// ======================================================================
// main field writing related code

// ----------------------------------------------------------------------
// mrc_io_vpic_destroy

static void
mrc_io_vpic_destroy(struct mrc_io *io)
{
  struct mrc_io_vpic *sub = mrc_io_vpic(io);

  mrc_io_vpic_destroy_dataset(io, &sub->fields);
  for (int s = 0; s < sub->n_species; s++) {
    mrc_io_vpic_destroy_dataset(io, &sub->species[s]);
  }
  free(sub->buf);
}

// ----------------------------------------------------------------------
// mrc_io_vpic_write_header

static void
mrc_io_vpic_write_header(struct mrc_io *io, FILE *file, struct mrc_fld *fld, int p,
			 int vpic_type)
{
  struct mrc_io_vpic *sub = mrc_io_vpic(io);

  struct mrc_domain *domain = fld->_domain;
  struct mrc_crds *crds = mrc_domain_get_crds(domain);

  // write magic / boiler plate
  char byteSize[5] = { sizeof(long long), sizeof(short), sizeof(int),
		       sizeof(float), sizeof(double) };
  fwrite(byteSize, 1, 5, file);
  short int cafe = 0xcafe;
  fwrite(&cafe, sizeof(short int), 1, file);
  int deadbeef = 0xdeadbeef;
  fwrite(&deadbeef, sizeof(int), 1, file);
  float floatone = 1.f;
  fwrite(&floatone, sizeof(float), 1, file);
  double doubleone = 1.;
  fwrite(&doubleone, sizeof(double), 1, file);
  
  // header

  int version = 1; // FIXME?
  int dumpType = vpic_type;
  int dumpTime = io->step;
  const int *gridSize = mrc_fld_dims(fld); // FIXME mrc_fld_spatial_dims()
  float deltaTime = sub->global.delta_t;
  double gridStep[DIMENSION];
  // FIXME: base only really makes sense for AMR
  mrc_crds_get_dx_base(crds, gridStep);
  float gridOrigin[DIMENSION] = { 0., 0., 0. }; // FIXME?
  float cvac = sub->global.cvac;
  float epsilon = sub->global.eps;
  float damp = 0.f; // FIXME?
  struct mrc_patch_info info;
  mrc_domain_get_local_patch_info(domain, p, &info);
  int rank = info.global_patch;
  int *topology = sub->global.topology;
  int totalRank = topology[0] * topology[1] * topology[2];
  int spid = 0; //???
  int spqm = 0; //???

  fwrite(&version, sizeof(int), 1, file);
  fwrite(&dumpType, sizeof(int), 1, file);
  
  fwrite(&dumpTime, sizeof(int), 1, file);
  fwrite(gridSize, sizeof(int), DIMENSION, file);
  fwrite(&deltaTime, sizeof(float), 1, file);
  fwrite(gridStep, sizeof(double), DIMENSION, file);
  fwrite(gridOrigin, sizeof(float), DIMENSION, file);
  fwrite(&cvac, sizeof(float), 1, file);
  fwrite(&epsilon, sizeof(float), 1, file);
  fwrite(&damp, sizeof(float), 1, file);
  fwrite(&rank, sizeof(int), 1, file);
  fwrite(&totalRank, sizeof(int), 1, file);
  
  fwrite(&spid, sizeof(int), 1, file);
  fwrite(&spqm, sizeof(float), 1, file);
  
  // Array size / # dims / ghost dims
  int recordSize = 1;
  int numberOfDimensions = DIMENSION;
  int ghostSize[DIMENSION] = { gridSize[0] + 2, gridSize[1] + 2, gridSize[2] + 2 };

  fwrite(&recordSize, sizeof(int), 1, file);
  fwrite(&numberOfDimensions, sizeof(int), 1, file);
  fwrite(ghostSize, sizeof(int), DIMENSION, file);
}

// ----------------------------------------------------------------------
// mrc_io_vpic_open

static void
mrc_io_vpic_open(struct mrc_io *io, const char *mode) 
{
  assert(strcmp(mode, "w") == 0); // only writing supported

  mrc_io_vpic_open_global(io);
}

// ----------------------------------------------------------------------
// mrc_io_vpic_close_files

static void
mrc_io_vpic_close_files(struct mrc_io *io, struct dataset *ds)
{
  struct mrc_io_vpic *sub = mrc_io_vpic(io);

  for (int p = 0; p < sub->nr_patches; p++) {
    fclose(ds->files[p]);
  }
  free(ds->files);
  ds->files = NULL;
}

// ----------------------------------------------------------------------
// mrc_io_vpic_close

static void
mrc_io_vpic_close(struct mrc_io *io)
{
  struct mrc_io_vpic *sub = mrc_io_vpic(io);

  mrc_io_vpic_close_global(io);

  mrc_io_vpic_close_files(io, &sub->fields);
  for (int s = 0; s < sub->n_species; s++) {
    mrc_io_vpic_close_files(io, &sub->species[s]);
  }
}

// ----------------------------------------------------------------------
// mrc_io_vpic_mkdir_time

static void
mrc_io_vpic_mkdir_time(struct mrc_io *io, const char *dir)
{
  char dirname[strlen(io->par.outdir) + strlen(dir) + 20];
  sprintf(dirname, "%s/%s/T.%d", io->par.outdir, dir, io->step);
  mkdir_comm(dirname, mrc_io_comm(io));
}

// ----------------------------------------------------------------------
// mrc_io_vpic_open_data_files

static FILE **
mrc_io_vpic_open_files(struct mrc_io *io, const char *directory,
		       const char *base_filename, struct mrc_fld *fld,
		       int vpic_type)
{
  struct mrc_io_vpic *sub = mrc_io_vpic(io);

  mrc_io_vpic_mkdir_time(io, directory);
      
  FILE **files = malloc(sub->nr_patches * sizeof(*files));

  for (int p = 0; p < sub->nr_patches; p++) {
    struct mrc_patch_info info;
    mrc_domain_get_local_patch_info(fld->_domain, p, &info);
    char filename[strlen(io->par.outdir) + strlen(directory)
		  + strlen(base_filename) + 30];
    
    sprintf(filename, "%s/%s/T.%d/%s.%0*d.%0*d", io->par.outdir,
	    directory, io->step, base_filename,
	    sub->time_field_len, io->step,
	    sub->proc_field_len, info.global_patch);
    files[p] = fopen(filename, "w");
    assert(files[p]);
    mrc_io_vpic_write_header(io, files[p], fld, p, vpic_type);
  }

  return files;
}

// ----------------------------------------------------------------------
// mrc_io_vpic_write_data

static void
mrc_io_vpic_write_data(struct mrc_io *io, struct dataset *ds, struct mrc_fld *fld,
		       int s, int n_species, int vpic_type)
{
  struct mrc_io_vpic *sub = mrc_io_vpic(io);

  if (!ds->files) {
    ds->files = mrc_io_vpic_open_files(io, ds->directory, ds->base_filename, fld, vpic_type);
  }

  int n_comp = mrc_fld_nr_comps(fld) / n_species;
  const int *dims = mrc_fld_dims(fld); // FIXME spatial_dims
  int size = (dims[0] + 2) * (dims[1] + 2) * (dims[2] + 2);
  if (!sub->buf) {
    sub->buf = malloc(size * sizeof(*sub->buf));
  }
  
  float *buf = sub->buf;
  for (int p = 0; p < mrc_fld_nr_patches(fld); p++) {
    for (int m = 0; m < n_comp; m++) {
      for (int k = 0; k < dims[2]; k++) {
	    for (int j = 0; j < dims[1]; j++) {
	      memcpy(&buf[((k+1) * (dims[1] + 2) + (j+1)) * (dims[0] + 2) + (0 + 1)],
		     &MRC_S5(fld, 0, j, k, s * n_comp + m, p), dims[0] * sizeof(*buf));
	    }
      }
      fwrite(buf, sizeof(*buf), size, ds->files[p]);
    }
  }
}

// ----------------------------------------------------------------------
// mrc_io_vpic_write_fld

static void
mrc_io_vpic_write_fld(struct mrc_io *io, const char *path, struct mrc_fld *fld)
{
  struct mrc_io_vpic *sub = mrc_io_vpic(io);

  //  mprintf("FLD  path %s\n", path);
  mrc_io_vpic_write_fld_global(io, fld);

  sub->nr_patches = mrc_fld_nr_patches(fld);
      
  if (is_hydro_var(fld, NULL, NULL, NULL)) {
    for (int s = 0; s < sub->n_species; s++) {
      mrc_io_vpic_write_data(io, &sub->species[s], fld, s, sub->n_species, VPIC_HYDRO);
    }
  } else {
    mrc_io_vpic_write_data(io, &sub->fields, fld, 0, 1, VPIC_FIELD);
  }
}

// ----------------------------------------------------------------------
// mrc_io_vpic_write_attr

static void
mrc_io_vpic_write_attr(struct mrc_io *io, const char *path, int type,
		       const char *name, union param_u *pv)
{
  struct mrc_io_vpic *sub = mrc_io_vpic(io);

  if (sub->global_in_progress) {
    if (strncmp(path, "psc-", 4) == 0) {
      //      mprintf("ATTR path %s name %s (type %d)\n", path, name, type);
      if (strcmp(name, "cc") == 0) {
	sub->global.cvac = pv->u_float;
      } else if (strcmp(name, "dt") == 0) {
	sub->global.delta_t = pv->u_float;
      }
    }
  }
}

// ----------------------------------------------------------------------
// mrc_io_vpic_descr

#define VAR(x) (void *)offsetof(struct mrc_io_vpic, x)
static struct param mrc_io_vpic_descr[] = {
  { "proc_field_len"     , VAR(proc_field_len)       , PARAM_INT(6)            },
  { "time_field_len"     , VAR(time_field_len)       , PARAM_INT(8)            },
  {},
};
#undef VAR

// ----------------------------------------------------------------------
// mrc_io_vpic_ops

struct mrc_io_ops mrc_io_vpic_ops = {
  .name        = "vpic",
  .size        = sizeof(struct mrc_io_vpic),
  .param_descr = mrc_io_vpic_descr,
  .destroy     = mrc_io_vpic_destroy,
  .open        = mrc_io_vpic_open,
  .close       = mrc_io_vpic_close,
  .write_fld   = mrc_io_vpic_write_fld,
  .write_attr  = mrc_io_vpic_write_attr,
};
