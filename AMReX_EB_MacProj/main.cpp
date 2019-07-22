#include <AMReX_Particles.H>
#include <AMReX.H>
#include <AMReX_ParmParse.H>
#include <AMReX_EBMultiFabUtil.H>
#include <AMReX_EB2.H>
#include <AMReX_EB2_IF.H>
#include <AMReX_MacProjector.H>
#include <AMReX_PlotFileUtil.H>
#include <AMReX_MultiFabUtil.H>
#include <AMReX_VisMF.H>
#include <AMReX_VisMF.H>
#include <AMReX_TagBox.H>
#include <AMReX_ParmParse.H>

#include <MyParticleContainer.H>

using namespace amrex;

void write_plotfile(int step_counter, const auto& geom, const auto& plotmf, const auto& pc)
{
    std::stringstream sstream;
    sstream << "plt" << std::setw(5) << std::setfill('0') << step_counter;
    std::string plotfile_name = sstream.str();

    // amrex::Print() << "Writing " << plotfile_name << std::endl;    
    
#if (AMREX_SPACEDIM == 2)
    EB_WriteSingleLevelPlotfile(plotfile_name, plotmf,
                                { "proc" ,"xvel", "yvel" },
                                  geom, 0.0, 0);
#elif (AMREX_SPACEDIM == 3)
    EB_WriteSingleLevelPlotfile(plotfile_name, plotmf,
                                { "proc", "xvel", "yvel", "zvel" },
                                  geom, 0.0, 0);
#endif

    pc.Checkpoint(plotfile_name, "Tracer", true); //Write Tracers to plotfile 
}

int main (int argc, char* argv[])
{
    amrex::Initialize(argc, argv);

    // Turn off amrex-related output
    amrex::SetVerbose(0);

    {
        int verbose = 0;
        int n_cell = 128;
        int max_grid_size = 32;
        std::string initial_tracer_file = "";
        Real max_time = 1.0;
        int max_steps = 100;
        int plot_int  = 1;
        Real time_step = 0.01;

        amrex::Vector<int> obstacles;

        // read parameters
        {
            ParmParse pp;
            pp.query("verbose", verbose);
            pp.query("n_cell", n_cell);
            pp.query("max_grid_size", max_grid_size);
            pp.query("initial_tracer_file", initial_tracer_file);
            pp.query("max_time", max_time);
            pp.query("max_steps", max_steps);
            pp.query("plot_int", plot_int);
            pp.query("time_step", time_step);

            pp.queryarr("obstacles", obstacles);

        }
        int n_cell_x = 2*n_cell;
        int num_obstacles;

        if (obstacles.empty())
        {
           amrex::Print() << " **************************************************** "     << std::endl;
           amrex::Print() << " You didn't specify any obstacles -- please try again " << std::endl;
           amrex::Print() << " ****************************************************\n "     << std::endl;
           exit(0);

        } else {

           num_obstacles = obstacles.size();

           if (num_obstacles > 9)
           {
              amrex::Print() << " **************************************************** "     << std::endl;
              amrex::Print() << " We only have 9 possible obstacles " << std::endl;
              amrex::Print() << " You specified too many -- please try again " << std::endl;
              amrex::Print() << " ****************************************************\n "     << std::endl;
              exit(0);
           } 

           for (int i = 0; i < num_obstacles; i++) 
              if (obstacles[i] < 0 || obstacles[i] > 8)
              {
                 amrex::Print() << " **************************************************** "     << std::endl;
                 amrex::Print() << " The obstacles must be identified using integers from 0 through 8 (inclusive) " << std::endl;
                 amrex::Print() << " You specified an invalid obstacle -- please try again " << std::endl;
                 amrex::Print() << " ****************************************************\n "     << std::endl;
                 exit(0);
              }

           amrex::Print() << " \n********************************************************************" << std::endl; 
           amrex::Print() << " You specified " << num_obstacles << " objects in the domain: ";
              for (int i = 0; i < num_obstacles; i++) 
                  amrex::Print() << obstacles[i] << " ";
             amrex::Print() << std::endl;
           amrex::Print() << " ********************************************************************" << std::endl; 
        } 

        Geometry geom;
        BoxArray grids;
        DistributionMapping dmap;
        {
            RealBox rb({AMREX_D_DECL(0.,0.,0.)}, {AMREX_D_DECL(2.,1.,1.)});
            Array<int,AMREX_SPACEDIM> isp{AMREX_D_DECL(0,1,1)};
            Geometry::Setup(&rb, 0, isp.data());
            Box domain(IntVect{AMREX_D_DECL(0,0,0)},
                       IntVect{AMREX_D_DECL(n_cell_x-1,n_cell-1,n_cell-1)});
            geom.define(domain);

            grids.define(domain);
            grids.maxSize(max_grid_size);

            dmap.define(grids);
        }

        Array<MultiFab,AMREX_SPACEDIM> vel;
        Array<MultiFab,AMREX_SPACEDIM> beta;
        MultiFab plotfile_mf;

        int required_coarsening_level = 0; // typically the same as the max AMR level index
        int max_coarsening_level = 100;    // typically a huge number so MG coarsens as much as possible
        // The "false" below is the boolean that determines if the fluid is inside ("true") or 
        //     outside ("false") the object(s)

        Array<EB2::SphereIF,9> sphere{
            EB2::SphereIF(0.1, {AMREX_D_DECL(0.3,0.2,0.5)}, false),
            EB2::SphereIF(0.1, {AMREX_D_DECL(0.3,0.5,0.5)}, false),
            EB2::SphereIF(0.1, {AMREX_D_DECL(0.3,0.8,0.5)}, false),
            EB2::SphereIF(0.1, {AMREX_D_DECL(0.7,0.3,0.5)}, false),
            EB2::SphereIF(0.1, {AMREX_D_DECL(0.7,0.6,0.5)}, false),
            EB2::SphereIF(0.1, {AMREX_D_DECL(0.7,0.9,0.5)}, false),
            EB2::SphereIF(0.1, {AMREX_D_DECL(1.1,0.2,0.5)}, false),
            EB2::SphereIF(0.1, {AMREX_D_DECL(1.1,0.5,0.5)}, false),
            EB2::SphereIF(0.1, {AMREX_D_DECL(1.1,0.8,0.5)}, false)};

        switch(num_obstacles) {

           case 1:
              {
              auto gshop1 = EB2::makeShop(sphere[obstacles[0]]);
              EB2::Build(gshop1, geom, required_coarsening_level, max_coarsening_level);
              break;
              }

           case 2:
              {
              amrex::Print() << "Objects " << obstacles[0] << " " << obstacles[1] << std::endl;
              auto all2 = EB2::makeUnion(sphere[obstacles[0]],sphere[obstacles[1]]);
              auto gshop2  = EB2::makeShop(all2);
              EB2::Build(gshop2, geom, required_coarsening_level, max_coarsening_level);
              break;
              }

           case 3:
              {
              auto all3 = EB2::makeUnion(sphere[obstacles[0]],sphere[obstacles[1]],sphere[obstacles[2]]);
              auto gshop3  = EB2::makeShop(all3);
              EB2::Build(gshop3, geom, required_coarsening_level, max_coarsening_level);
              break;
              }

           case 4:
              {
              auto group_1 = EB2::makeUnion(sphere[obstacles[0]],sphere[obstacles[1]],sphere[obstacles[2]]);
              auto all     = EB2::makeUnion(group_1,sphere[obstacles[3]]);
              auto gshop4  = EB2::makeShop(all);
              EB2::Build(gshop4, geom, required_coarsening_level, max_coarsening_level);
              break;
              }

           case 5:
              {
              auto group_1 = EB2::makeUnion(sphere[obstacles[0]],sphere[obstacles[1]],sphere[obstacles[2]]);
              auto group_2 = EB2::makeUnion(sphere[obstacles[3]],sphere[obstacles[4]]);
              auto all     = EB2::makeUnion(group_1,group_2);
              auto gshop5  = EB2::makeShop(all);
              EB2::Build(gshop5, geom, required_coarsening_level, max_coarsening_level);
              break;
              }

           case 6:
              {
              auto group_1 = EB2::makeUnion(sphere[obstacles[0]],sphere[obstacles[1]],sphere[obstacles[2]]);
              auto group_2 = EB2::makeUnion(sphere[obstacles[3]],sphere[obstacles[4]],sphere[obstacles[5]]);
              auto all     = EB2::makeUnion(group_1,group_2);
              auto gshop6  = EB2::makeShop(all);
              EB2::Build(gshop6, geom, required_coarsening_level, max_coarsening_level);
              break;
              }

           case 7:
              {
              auto group_1 = EB2::makeUnion(sphere[obstacles[0]],sphere[obstacles[1]],sphere[obstacles[2]]);
              auto group_2 = EB2::makeUnion(sphere[obstacles[3]],sphere[obstacles[4]],sphere[obstacles[5]]);
              auto group_3 = sphere[obstacles[6]];
              auto all     = EB2::makeUnion(group_1,group_2,group_3);
              auto gshop7  = EB2::makeShop(all);
              EB2::Build(gshop7, geom, required_coarsening_level, max_coarsening_level);
              break;
              }

           case 8:
              {
              auto group_1 = EB2::makeUnion(sphere[obstacles[0]],sphere[obstacles[1]],sphere[obstacles[2]]);
              auto group_2 = EB2::makeUnion(sphere[obstacles[3]],sphere[obstacles[4]],sphere[obstacles[5]]);
              auto group_3 = EB2::makeUnion(sphere[obstacles[6]],sphere[obstacles[7]]);
              auto all     = EB2::makeUnion(group_1,group_2,group_3);
              auto gshop8  = EB2::makeShop(all);
              EB2::Build(gshop8, geom, required_coarsening_level, max_coarsening_level);
              break;
              }

           case 9:
              {
              auto group_1 = EB2::makeUnion(sphere[obstacles[0]],sphere[obstacles[1]],sphere[obstacles[2]]);
              auto group_2 = EB2::makeUnion(sphere[obstacles[3]],sphere[obstacles[4]],sphere[obstacles[5]]);
              auto group_3 = EB2::makeUnion(sphere[obstacles[6]],sphere[obstacles[7]],sphere[obstacles[8]]);
              auto all     = EB2::makeUnion(group_1,group_2,group_3);
              auto gshop9  = EB2::makeShop(all);
              EB2::Build(gshop9, geom, required_coarsening_level, max_coarsening_level);
              break;
              }

           default:;
        }
   
        const EB2::IndexSpace& eb_is = EB2::IndexSpace::top();
        const EB2::Level& eb_level = eb_is.getLevel(geom);
   
        // options are basic, volume, or full
        EBSupport ebs = EBSupport::full;
  
        // number of ghost cells for each of the 3 EBSupport types
        Vector<int> ng_ebs = {2,2,2};
 
        // This object provides access to the EB database in the format of basic AMReX objects
        // such as BaseFab, FArrayBox, FabArray, and MultiFab
        EBFArrayBoxFactory factory(eb_level, geom, grids, dmap, ng_ebs, ebs);
        
//      const FabFactory<FArrayBox>& test_factory = (num_obstacles > 0) ? 
//          EBFArrayBoxFactory(eb_level, geom, grids, dmap, ng_ebs, ebs): FArrayBoxFactory();

        for (int idim = 0; idim < AMREX_SPACEDIM; ++idim) {
            vel[idim].define (amrex::convert(grids,IntVect::TheDimensionVector(idim)), dmap, 1, 1, MFInfo(), factory);
            beta[idim].define(amrex::convert(grids,IntVect::TheDimensionVector(idim)), dmap, 1, 0, MFInfo(), factory);
            beta[idim].setVal(1.0);
        }

        // store plotfile variables; velocity and processor id
        plotfile_mf.define(grids, dmap, AMREX_SPACEDIM+1, 0, MFInfo(), factory);

        // Initialize Particles
        MyParticleContainer MyPC(geom, dmap, grids);
        MyPC.InitFromAsciiFile(initial_tracer_file, 0);

        // set initial velocity to u=(1,0,0)
        AMREX_D_TERM(vel[0].setVal(1.0);,
                     vel[1].setVal(0.0);,
                     vel[2].setVal(0.0););

        MacProjector macproj({amrex::GetArrOfPtrs(vel)},       // mac velocity
                             {amrex::GetArrOfConstPtrs(beta)}, // beta
                             {geom});                          // Geometry

        macproj.setVerbose(verbose);
        macproj.setCGVerbose(0);

        macproj.setDomainBC({AMREX_D_DECL(LinOpBCType::Neumann,
                                          LinOpBCType::Periodic,
                                          LinOpBCType::Periodic)},
            {AMREX_D_DECL(LinOpBCType::Dirichlet,
                          LinOpBCType::Periodic,
                          LinOpBCType::Periodic)});

        Real reltol = 1.e-8;

        amrex::Print() << " \n********************************************************************" << std::endl; 
        amrex::Print() << " First let's project the initial velocity to find " << std::endl;
        amrex::Print() << "   the flow field around the obstacles ... " << std::endl;
        amrex::Print() << "******************************************************************** \n" << std::endl; 

        // macproj.setBottomSolver(MLMG::BottomSolver::hypre);
        // macproj.setBottomSolver(MLMG::BottomSolver::bicgcg);
        macproj.project(reltol);

        amrex::Print() << " \n********************************************************************" << std::endl; 
        amrex::Print() << " Done!  Now let's advect the particles ... " << std::endl;
        amrex::Print() << "******************************************************************** \n" << std::endl; 

        for (int idim = 0; idim < AMREX_SPACEDIM; ++idim) {
            vel[idim].FillBoundary(geom.periodicity());
        }

        // copy processor id into plotfile_mf
        for (MFIter mfi(vel[0]); mfi.isValid(); ++mfi)
            plotfile_mf[mfi].setVal(ParallelDescriptor::MyProc());

        // copy velocity into plotfile
        average_face_to_cellcenter(plotfile_mf,1,amrex::GetArrOfConstPtrs(vel));

#ifdef _OPENMP
#pragma omp parallel if (Gpu::notInLaunchRegion())
#endif

        Real time = 0.0;
        for (int i = 0; i < max_steps; i++)
        {
            if (time < max_time) {
                time_step = std::min(time_step, max_time - time);

                // Step Particles
                MyPC.AdvectWithUmac(vel.data(), 0, time_step);

                MyPC.Redistribute();

                // Write to a plotfile
                if (i%plot_int == 0)
                   write_plotfile(i, geom, plotfile_mf, MyPC);

                // Increment time
                time += time_step;

                // Find the maximum particle position "x" to determine the winning particle
                using ParticleType = MyParticleContainer::ParticleType;

                // This finds the particle with the maximum "x"
                Real x = MyPC.FindWinner();

                if (i%100 == 0)
                   amrex::Print() << "Timestep " << i << ", Time = " << time << " and leading particle now at " << x << std::endl;

                if (x > 1.5) 
                {
                   amrex::Print() << " \n********************************************************************" << std::endl; 
                   amrex::Print() << "We have a winner...and the winning time is " << time << std::endl;
                   amrex::Print() << "********************************************************************\n " << std::endl; 
                   write_plotfile(i, geom, plotfile_mf, MyPC);
                   break;
                }

            } else {
                // Write to a plotfile
                write_plotfile(i, geom, plotfile_mf, MyPC);
                break;
            }
        }
    }

    amrex::Finalize();
}