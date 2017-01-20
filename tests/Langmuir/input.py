from pywarpx import *

# Maximum number of time steps
max_step = 40

# number of grid points
amr.n_cell =   "64  64  64"

# Maximum allowable size of each subdomain in the problem domain; 
#    this is used to decompose the domain for parallel calculations.
amr.max_grid_size = 64

# Maximum level in hierarchy (for now must be 0, i.e., one level in total)
amr.max_level = 0

amr.plot_int = 1   # How often to write plotfiles.  "<= 0" means no plotfiles.

# Geometry
geometry.coord_sys   = 0                  # 0: Cartesian
geometry.is_periodic = "1     1     1"      # Is periodic?  
geometry.prob_lo     = "-20.e-6   -20.e-6   -20.e-6"    # physical domain
geometry.prob_hi     = " 20.e-6    20.e-6    20.e-6"

# Verbosity
warpx.verbose = 1

# Algorithms
algo.current_deposition = 3
algo.charge_deposition = 0
algo.field_gathering = 1
algo.particle_pusher = 0

# CFL
warpx.cfl = 1.0

langmuirwave.particle_xmin = -20.e-6
langmuirwave.particle_xmax = 0.e-6
langmuirwave.particle_ymin = -20.e-6
langmuirwave.particle_ymax = 20.e-6
langmuirwave.particle_zmin = -20.e-6
langmuirwave.particle_zmax = 20.e-6
langmuirwave.num_particles_per_cell = 10
langmuirwave.n_e = 1.e25  # number of electrons per m^3
langmuirwave.ux  = 0.01   # ux = gamma*beta_x

boxlib = BoxLib()
boxlib.init()

warpx.init()
warpx.evolve(max_step)
warpx.finalize()

boxlib.finalize()
