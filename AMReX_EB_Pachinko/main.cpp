#include <AMReX.H>
#include <AMReX_ParmParse.H>
#include <AMReX_EBMultiFabUtil.H>
#include <AMReX_EB2.H>
#include <AMReX_EB2_IF.H>
#include <AMReX_MacProjector.H>
#include <AMReX_PlotFileUtil.H>
#include <AMReX_MultiFabUtil.H>
#include <AMReX_TracerParticles.H>

using namespace amrex;

void write_plotfile(int step_counter, const auto& geom, const auto& plotmf, const auto& pc)
{
    std::stringstream sstream;
    sstream << "plt" << std::setw(5) << std::setfill('0') << step_counter;
    std::string plotfile_name = sstream.str();

    amrex::Print() << "Writing " << plotfile_name << std::endl;    

    EB_WriteSingleLevelPlotfile(plotfile_name, plotmf,
                                {"before-vx", "before-vy",
#if (AMREX_SPACEDIM == 3)
                                        "before-vz",
#endif
                                        "divu-before",
                                        "after-vx", "after-vy",
#if (AMREX_SPACEDIM == 3)
                                        "after-vz",       
#endif
                                        "divu-after"},
                                geom, 0.0, 0);    

    pc.Checkpoint(plotfile_name, "Tracer", true); //Write Tracers to plotfile 
}


int main (int argc, char* argv[])
{
    amrex::Initialize(argc, argv);

    {
        int verbose = 1;
        int n_cell = 128;
        int max_grid_size = 32;
        int is_periodic = 0;
        std::string initial_tracer_file = "";
        Real max_time = 1.0;
        int max_steps = 100;
        Real time_step = 0.01;

        // read parameters
        {
            ParmParse pp;
            pp.query("verbose", verbose);
            pp.query("n_cell", n_cell);
            pp.query("max_grid_size", max_grid_size);
            pp.query("is_periodic", is_periodic);
            pp.query("initial_tracer_file", initial_tracer_file);
            pp.query("max_time", max_time);
            pp.query("max_steps", max_steps);
            pp.query("time_step", time_step);            
        }

        Geometry geom;
        BoxArray grids;
        DistributionMapping dmap;
        {
            RealBox rb({AMREX_D_DECL(0.,0.,0.)}, {AMREX_D_DECL(1.,1.,1.)});
            Array<int,AMREX_SPACEDIM> isp{AMREX_D_DECL(is_periodic,is_periodic,is_periodic)};
            Geometry::Setup(&rb, 0, isp.data());
            Box domain(IntVect{AMREX_D_DECL(0,0,0)},
                       IntVect{AMREX_D_DECL(n_cell-1,n_cell-1,n_cell-1)});
            geom.define(domain);
            
            grids.define(domain);
            grids.maxSize(max_grid_size);

            dmap.define(grids);
        }

        int required_coarsening_level = 0; // typically the same as the max AMR level index
        int max_coarsening_level = 100;    // typically a huge number so MG coarsens as much as possible
        // build a simple geometry using the "eb2." parameters in the inputs file
        EB2::Build(geom, required_coarsening_level, max_coarsening_level);

        const EB2::IndexSpace& eb_is = EB2::IndexSpace::top();
        const EB2::Level& eb_level = eb_is.getLevel(geom);

        // options are basic, volume, or full
        EBSupport ebs = EBSupport::full;

        // number of ghost cells for each of the 3 EBSupport types
        Vector<int> ng_ebs = {2,2,2};

        // This object provides access to the EB database in the format of basic AMReX objects
        // such as BaseFab, FArrayBox, FabArray, and MultiFab
        EBFArrayBoxFactory factory(eb_level, geom, grids, dmap, ng_ebs, ebs);

        // Initialize Particles
        TracerParticleContainer TracerPC(geom, dmap, grids);
        TracerPC.SetVerbose(10);
        TracerPC.InitFromAsciiFile(initial_tracer_file, 0);

        // store plotfile variables; velocity-before, div-before, velocity-after, div-after
        MultiFab plotfile_mf;
        plotfile_mf.define(grids, dmap, 2*AMREX_SPACEDIM+2, 0, MFInfo(), factory);

        Array<MultiFab,AMREX_SPACEDIM> vel;
        Array<MultiFab,AMREX_SPACEDIM> beta;
        for (int idim = 0; idim < AMREX_SPACEDIM; ++idim) {
            vel[idim].define (amrex::convert(grids,IntVect::TheDimensionVector(idim)), dmap, 1, 1, MFInfo(), factory);
            beta[idim].define(amrex::convert(grids,IntVect::TheDimensionVector(idim)), dmap, 1, 0, MFInfo(), factory);
            beta[idim].setVal(1.0);
        }

        // set initial velocity to u=(1,0,0)
        AMREX_D_TERM(vel[0].setVal(1.0);,
                     vel[1].setVal(0.0);,
                     vel[2].setVal(0.0););

        // copy velocity into plotfile
        average_face_to_cellcenter(plotfile_mf,0,amrex::GetArrOfConstPtrs(vel));
        
        // compute and output divergence, then copy into plofile
        MultiFab divu(grids, dmap, 1, 0, MFInfo(), factory);
        EB_computeDivergence(divu, amrex::GetArrOfConstPtrs(vel), geom);
        amrex::Print() << "\nmax-norm of divu before projection is " << divu.norm0() << "\n" << std::endl;
        plotfile_mf.copy(divu,0,AMREX_SPACEDIM,1);
        
        MacProjector macproj({amrex::GetArrOfPtrs(vel)},       // mac velocity
                             {amrex::GetArrOfConstPtrs(beta)}, // beta 
                             {geom});                          // Geometry

        macproj.setVerbose(verbose);

        macproj.setDomainBC({AMREX_D_DECL(LinOpBCType::Neumann,
                                          LinOpBCType::Neumann,
                                          LinOpBCType::Neumann)},
                            {AMREX_D_DECL(LinOpBCType::Neumann,
                                          LinOpBCType::Neumann,
                                          LinOpBCType::Neumann)});

        Real reltol = 1.e-12;
        macproj.project(reltol);

        for (int idim = 0; idim < AMREX_SPACEDIM; ++idim) {
            vel[idim].FillBoundary(geom.periodicity());
        }

        // copy velocity into plotfile
        average_face_to_cellcenter(plotfile_mf,AMREX_SPACEDIM+1,amrex::GetArrOfConstPtrs(vel));

        // compute and output divergence, then copy into plofile
        EB_computeDivergence(divu, amrex::GetArrOfConstPtrs(vel), geom);
        amrex::Print() << "\nmax-norm of divu after projection is " << divu.norm0() << "\n" << std::endl;
        plotfile_mf.copy(divu,0,2*AMREX_SPACEDIM+1,1);

        Real time = 0.0;
        for (int i = 0; i < max_steps; i++)
        {
            if (time < max_time) {
                time_step = std::min(time_step, max_time - time);

                amrex::Print() << "\nTimestep " << i << ", Time = " << time << std::endl;
                amrex::Print() << "Advecting particles with Umac for timestep " << time_step << std::endl;
                // Step Particles
                TracerPC.AdvectWithUmac(vel.data(), 0, time_step);

                // Write to a plotfile
                write_plotfile(i, geom, plotfile_mf, TracerPC);

                // Increment time
                time += time_step;
            } else {
                // Write to a plotfile
                write_plotfile(i, geom, plotfile_mf, TracerPC);
                break;
            }
        }
    }

    amrex::Finalize();
}