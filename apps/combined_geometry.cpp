////////////////////////////////////////////////////////////////////////////////
//  Copyright (c) 2021 Andreas Wagner.
//
//  Distributed under the Boost Software License, Version 1.0. (See accompanying
//  file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
////////////////////////////////////////////////////////////////////////////////

#include <chrono>
#include <cxxopts.hpp>
#include <macrocirculation/graph_flow_and_concentration_writer.hpp>
#include <macrocirculation/set_0d_tree_boundary_conditions.hpp>
#include <memory>

#include "petsc.h"

#include "macrocirculation/communication/mpi.hpp"
#include "macrocirculation/coupled_explicit_implicit_1d_solver.hpp"
#include "macrocirculation/dof_map.hpp"
#include "macrocirculation/embedded_graph_reader.hpp"
#include "macrocirculation/explicit_nonlinear_flow_solver.hpp"
#include "macrocirculation/graph_partitioner.hpp"
#include "macrocirculation/graph_pvd_writer.hpp"
#include "macrocirculation/graph_storage.hpp"
#include "macrocirculation/implicit_linear_flow_solver.hpp"
#include "macrocirculation/quantities_of_interest.hpp"
#include "macrocirculation/vessel_formulas.hpp"
#include "macrocirculation/nonlinear_linear_coupling.hpp"

namespace mc = macrocirculation;

constexpr std::size_t degree = 2;

int main(int argc, char *argv[]) {
  cxxopts::Options options(argv[0], "Combined geometry with explicit implicit solver");
  options.add_options()                                                                                                             //
    ("tau", "time step size", cxxopts::value<double>()->default_value(std::to_string(2.5e-4 / 16.)))                                //
    ("tau-out", "time step size for the output", cxxopts::value<double>()->default_value("1e-2"))                                   //
    ("t-end", "Simulation period for simulation", cxxopts::value<double>()->default_value("10"))                                               //
    ("h,help", "print usage");
  options.allow_unrecognised_options(); // for petsc
  auto args = options.parse(argc, argv);

  CHKERRQ(PetscInitialize(&argc, &argv, nullptr, "solves linear flow problem"));

  {
    // create_for_node the ascending aorta
    auto graph_nl = std::make_shared<mc::GraphStorage>();

    mc::EmbeddedGraphReader graph_reader;
    graph_reader.append("data/meshes/network-33-vessels-extended.json", *graph_nl);

    auto &v_in = *graph_nl->find_vertex_by_name("cw_in");
    v_in.set_to_inflow(mc::heart_beat_inflow(485.0));
    mc::naive_mesh_partitioner(*graph_nl, MPI_COMM_WORLD);

    auto graph_li = std::make_shared<mc::GraphStorage>();
    graph_reader.append("data/meshes/coarse-network-geometry.json", *graph_li);
    graph_reader.set_boundary_data("data/meshes/boundary-coarse-network-geometry.json", *graph_li);
    mc::naive_mesh_partitioner(*graph_li, MPI_COMM_WORLD);
    mc::set_0d_tree_boundary_conditions(graph_li, "bg_");

    auto coupling = std::make_shared< mc::NonlinearLinearCoupling > (MPI_COMM_WORLD, graph_nl, graph_li);
    coupling->add_coupled_vertices("cw_out_1_1", "bg_132");
    coupling->add_coupled_vertices("cw_out_1_2", "bg_141");
    coupling->add_coupled_vertices("cw_out_2_1", "bg_135");
    coupling->add_coupled_vertices("cw_out_2_2", "bg_119");

    mc::CoupledExplicitImplicit1DSolver solver(MPI_COMM_WORLD, coupling, graph_nl, graph_li, degree, degree);

    const double t_end = args["t-end"].as<double>();
    const std::size_t max_iter = 160000000;

    const auto tau = args["tau"].as<double>();
    const auto tau_out = args["tau-out"].as<double>();

    // const double tau_out = tau;
    const auto output_interval = static_cast<std::size_t>(tau_out / tau);

    // configure solver
    solver.setup(tau);

    std::vector<mc::Point> points;
    std::vector<double> p_vertex_values;
    std::vector<double> q_vertex_values;

    auto dof_map_li = solver.get_implicit_dof_map();
    auto dof_map_nl = solver.get_explicit_dof_map();
    auto solver_li = solver.get_implicit_solver();
    auto solver_nl = solver.get_explicit_solver();
    const auto &u_li = solver_li->get_solution();

    solver_nl->use_ssp_method();

    mc::GraphFlowAndConcentrationWriter csv_writer(MPI_COMM_WORLD, "output", "data", graph_nl, dof_map_nl, dof_map_nl);
    mc::GraphPVDWriter pvd_writer(MPI_COMM_WORLD, "output", "combined_geometry_solution");

    const auto begin_t = std::chrono::steady_clock::now();
    double t = 0;
    for (std::size_t it = 0; it < max_iter; it += 1) {
      solver.solve(tau, t);
      t += tau;

      if (it % output_interval == 0) {
        if (mc::mpi::rank(MPI_COMM_WORLD) == 0)
          std::cout << "iter = " << it << ", t = " << t << std::endl;

        // save solution
        csv_writer.write(it * tau, solver_nl->get_solution(), solver_nl->get_solution());

        mc::interpolate_to_vertices(MPI_COMM_WORLD, *graph_li, *dof_map_li, solver_li->p_component, u_li, points, p_vertex_values);
        mc::interpolate_to_vertices(MPI_COMM_WORLD, *graph_li, *dof_map_li, solver_li->q_component, u_li, points, q_vertex_values);

        pvd_writer.set_points(points);
        pvd_writer.add_vertex_data("p", p_vertex_values);
        pvd_writer.add_vertex_data("q", q_vertex_values);
        pvd_writer.write(t);
      }

      // break
      if (t > t_end + 1e-12)
        break;
    }

    const auto end_t = std::chrono::steady_clock::now();
    const auto elapsed_ms = std::chrono::duration_cast<std::chrono::microseconds>(end_t - begin_t).count();
    if (mc::mpi::rank(MPI_COMM_WORLD) == 0)
      std::cout << "time = " << elapsed_ms * 1e-6 << " s" << std::endl;
  }


  CHKERRQ(PetscFinalize());
}
