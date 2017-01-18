
//
// Each problem must have its own version of SingleParticleContainer::InitData()
// to initialize the particle data on this level
//

#include <cmath>

#include <BLProfiler.H>

#include <ParticleContainer.H>
#include <WarpXConst.H>

void
SingleSpeciesContainer::InitData()
{
    BL_PROFILE("SPC::InitData()");

    // species_id 0 : the beam
    // species_id 1 : the electrons of the plasma
    // Note: the ions of the plasma are implicitly motionless, and so are not part of the simulation
    if (species_id == 0 or species_id == 1) {
	charge = -PhysConst::q_e;
	mass = PhysConst::m_e;
    } else {
	BoxLib::Abort("SingleSpeciesContainer::InitData(): species_id must be 0 or 1");
    }

    m_particles.resize(m_gdb->finestLevel()+1);

    const int lev = 0;

    const Geometry& geom = m_gdb->Geom(lev);
    const Real* dx  = geom.CellSize();

    Real weight, gamma, uz;
    Real particle_xmin, particle_xmax, particle_ymin, particle_ymax, particle_zmin, particle_zmax;
    int n_part_per_cell;
    {
      ParmParse pp("pwfa");

      // Calculate the particle weight
      n_part_per_cell = 1;
      pp.query("num_particles_per_cell", n_part_per_cell);
      weight = 1.e22;
      if (species_id == 0) {
	pp.query("beam_density", weight);
      } else if (species_id == 1) {
	pp.query("plasma_density", weight);
      }
      #if BL_SPACEDIM==3
      weight *= dx[0]*dx[1]*dx[2]/n_part_per_cell;
      #elif BL_SPACEDIM==2
      weight *= dx[0]*dx[1]/n_part_per_cell;
      #endif

      // Calculate the limits between which the particles will be initialized
      particle_xmin = particle_ymin = particle_zmin = -2.e-5;
      particle_xmax = particle_ymax = particle_zmax =  2.e-5;
      if (species_id == 0) {
	pp.query("beam_xmin", particle_xmin);
	pp.query("beam_xmax", particle_xmax);
	pp.query("beam_ymin", particle_ymin);
	pp.query("beam_ymax", particle_ymax);
	pp.query("beam_zmin", particle_zmin);
	pp.query("beam_zmax", particle_zmax);
      } else if (species_id == 1) {
	pp.query("plasma_xmin", particle_xmin);
	pp.query("plasma_xmax", particle_xmax);
	pp.query("plasma_ymin", particle_ymin);
	pp.query("plasma_ymax", particle_ymax);
	pp.query("plasma_zmin", particle_zmin);
	pp.query("plasma_zmax", particle_zmax);
      }
      uz = 0.;
      if (species_id == 0) { // relativistic beam
	gamma = 1.;
	pp.query("beam_gamma", gamma);
	uz = std::sqrt( gamma*gamma - 1 );
      }     
      uz *= PhysConst::c;
    }

    const BoxArray& ba = m_gdb->ParticleBoxArray(lev);
    const DistributionMapping& dm = m_gdb->ParticleDistributionMap(lev);

    MultiFab dummy_mf(ba, 1, 0, dm, Fab_noallocate);

    for (MFIter mfi(dummy_mf,false); mfi.isValid(); ++mfi)
    {
	int gid = mfi.index();
        Box grid = ba[gid];
        RealBox grid_box { grid,dx,geom.ProbLo() };

#if (BL_SPACEDIM == 3)
	int nx = grid.length(0), ny = grid.length(1), nz = grid.length(2);
#elif (BL_SPACEDIM == 2)
	int nx = grid.length(0), ny = 1, nz = grid.length(1);
#endif

	for (int k = 0; k < nz; k++) {
	  for (int j = 0; j < ny; j++) {
	    for (int i = 0; i < nx; i++) {
	      for (int i_part=0; i_part<n_part_per_cell;i_part++) {
		Real particle_shift = (0.5+i_part)/n_part_per_cell;
#if (BL_SPACEDIM == 3)
		Real x = grid_box.lo(0) + (i + particle_shift)*dx[0];
		Real y = grid_box.lo(1) + (j + particle_shift)*dx[1];
		Real z = grid_box.lo(2) + (k + particle_shift)*dx[2];
#elif (BL_SPACEDIM == 2)
		Real x = grid_box.lo(0) + (i + particle_shift)*dx[0];
		Real y = 0.0;
		Real z = grid_box.lo(1) + (k + particle_shift)*dx[1];
#endif   

		if (x >= particle_xmax || x < particle_xmin ||
		    y >= particle_ymax || y < particle_ymin ||
		    z >= particle_zmax || z < particle_zmin ) continue;
	      
		ParticleType p;

		p.m_id  = ParticleBase::NextID();
		p.m_cpu = ParallelDescriptor::MyProc();
		p.m_lev = lev;
		p.m_grid = gid; 

#if (BL_SPACEDIM == 3)
		p.m_pos[0] = x;
		p.m_pos[1] = y;
		p.m_pos[2] = z;
#elif (BL_SPACEDIM == 2)
		p.m_pos[0] = x;
		p.m_pos[1] = z;
#endif
		
		for (int i = 0; i < BL_SPACEDIM; i++) {
		  BL_ASSERT(p.m_pos[i] < grid_box.hi(i));
		}
		
		p.m_data[PIdx::w] = weight;
	      
		for (int i = 1; i < PIdx::nattribs; i++) {
		  p.m_data[i] = 0;
		}
		p.m_data[PIdx::uz] = uz;

		if (!ParticleBase::Where(p,m_gdb)) // this will set m_cell
		{
		    BoxLib::Abort("invalid particle");
		}
		
		BL_ASSERT(p.m_lev >= 0 && p.m_lev <= m_gdb->finestLevel());
		//
		// Add it to the appropriate PBox at the appropriate level.
		//
		m_particles[p.m_lev][p.m_grid].push_back(p);
	      }
	    }
	  } 
        }
    }

    //
    // We still need to redistribute in order to define each particle's cell, grid and level, but this 
    //    shouldn't require any inter-node communication because the particles should already be in the right grid.
    //
    Redistribute(true);
}
