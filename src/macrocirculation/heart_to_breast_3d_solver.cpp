////////////////////////////////////////////////////////////////////////////////
//  Copyright (c) 2021 Prashant K. Jha.
//
//  Distributed under the Boost Software License, Version 1.0. (See accompanying
//  file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
////////////////////////////////////////////////////////////////////////////////

#include "heart_to_breast_3d_solver.hpp"
#include "heart_to_breast_1d_solver.hpp"
#include "random_dist.hpp"
#include "tree_search.hpp"
#include "vtk_io_libmesh.hpp"
#include "vtk_writer.hpp"
#include <fmt/format.h>

namespace macrocirculation {

namespace {
// creates outlet at random location and assigns outlet radii randomly
void set_perfusion_pts(std::string out_dir,
                       int num_pts,
                       std::vector<lm::Point> &pts,
                       std::vector<double> &radii,
                       lm::EquationSystems &eq_sys,
                       HeartToBreast3DSolver &model) {

  // initialize random number generator
  int seed = 0;
  srand(seed);

  const auto &input = eq_sys.parameters.get<HeartToBreast3DSolverInputDeck *>("input_deck");
  const auto &mesh = eq_sys.get_mesh();

  // get length of the domain
  auto bbox = lm::MeshTools::create_bounding_box(mesh);
  auto xc = 0.5 * bbox.min() + 0.5 * bbox.max();
  auto l = (bbox.min() - bbox.max()).norm();

  // create list of element centers for tree search
  int nelems = mesh.n_elem();
  std::vector<lm::Point> elem_centers(nelems, lm::Point());
  for (const auto &elem : mesh.element_ptr_range())
    elem_centers[elem->id()] = elem->centroid();

  // randomly select desired number of element centers as outlet perfusion points
  int npts = num_pts;
  pts.resize(npts);
  radii.resize(npts);
  std::vector<int> sel_elems;
  double min_dist = l / npts;
  for (int i = 0; i < 10 * npts; i++) {
    if (sel_elems.size() == npts)
      break;

    // get random integer between 0 and nelems - 1
    int e = rand() % (nelems - 1);
    if (locate_in_set(e, sel_elems) != -1)
      continue;

    // e is not in existing list so check if it is a good candidate
    bool not_suitable = false;
    for (auto ee : sel_elems) {
      auto dx = elem_centers[ee] - elem_centers[e];
      if (dx.norm() < min_dist) {
        not_suitable = true;
        break;
      }
    }

    if (not_suitable)
      continue;

    // add element to the list
    sel_elems.push_back(e);
  }

  // if at this point we do not have enough elements in sel_elems than exit
  if (sel_elems.size() < npts) {
    std::cerr << "Error: Increase threshold for creating random points for perfusion\n";
    exit(EXIT_FAILURE);
  }

  // add cooardinates and radius (based on uniform distribution)
  DistributionSample<UniformDistribution> uni_dist(min_dist / 10., min_dist / 3., seed);
  for (int i = 0; i < npts; i++) {
    pts[i] = elem_centers[sel_elems[i]];
    radii[i] = uni_dist();
  }
}
} // namespace

// input class definitions

HeartToBreast3DSolverInputDeck::HeartToBreast3DSolverInputDeck(const std::string &filename)
    : d_rho_cap(1.), d_rho_tis(1.), d_K_cap(1.e-9), d_K_tis(1.e-11),
      d_Lp_art_cap(1.e-6), d_Lc_cap(1e-12),
      d_Dnut_cap(1e-6), d_Dtis_cap(1.e-6), d_Lnut_cap(1.),
      d_Sc_cap(1e2),
      d_rnut_cap(0.9), d_rnut_art_cap(0.9),
      d_T(1.), d_dt(0.01), d_h(0.1), d_mesh_file(""), d_out_dir(""),
      d_perf_regularized(false),
      d_perf_fn_type("const"), d_perf_neigh_size({1., 4.}),
      d_debug_lvl(0) {
  if (!filename.empty())
    read_parameters(filename);
}

void HeartToBreast3DSolverInputDeck::read_parameters(const std::string &filename) {
  GetPot input(filename);
  d_rho_cap = input("rho_cap", 1.);
  d_rho_tis = input("rho_tis", 1.);
  d_K_cap = input("K_cap", 1.);
  d_K_tis = input("K_tis", 1.);
  d_Lp_art_cap = input("Lp_art_cap", 1.);
  d_Lc_cap = input("Lc_cap", 1.);
  d_Dnut_cap = input("Dnut_cap", 1.);
  d_Dtis_cap = input("Dtis_cap", 1.);
  d_Lnut_cap = input("Lnut_cap", 1.);
  d_Sc_cap = input("Sc_cap", 1.);
  d_rnut_cap = input("rnut_cap", 1.);
  d_rnut_art_cap = input("rnut_art_cap", 1.);
  d_T = input("T", 1.);
  d_dt = input("dt", 0.01);
  d_h = input("h", 0.1);
  d_mesh_file = input("mesh_file", "");
  d_out_dir = input("out_dir", "");
  d_perf_regularized = input("regularized_source", 1) == 0;
  d_perf_fn_type = input("perf_fn_type", "linear");
  d_perf_neigh_size.first = input("perf_neigh_size_min", 1.);
  d_perf_neigh_size.second = input("perf_neigh_size_max", 4.);
  d_debug_lvl = input("debug_lvl", 0);
}

std::string HeartToBreast3DSolverInputDeck::print_str() {
  std::ostringstream oss;
  oss << "rho_cap = " << d_rho_cap << "\n";
  oss << "rho_tis = " << d_rho_tis << "\n";
  oss << "K_cap = " << d_K_cap << "\n";
  oss << "K_tis = " << d_K_tis << "\n";
  oss << "L_art_cap = " << d_Lp_art_cap << "\n";
  oss << "Lc_cap = " << d_Lc_cap << "\n";
  oss << "Sc_cap = " << d_Sc_cap << "\n";
  oss << "T = " << d_T << "\n";
  oss << "dt = " << d_dt << "\n";
  oss << "h = " << d_h << "\n";
  oss << "mesh_file = " << d_mesh_file << "\n";
  oss << "out_dir = " << d_out_dir << "\n";
  return oss.str();
}

// solver class definitions
HeartToBreast3DSolver::HeartToBreast3DSolver(MPI_Comm mpi_comm,
                                             lm::Parallel::Communicator *libmesh_comm,
                                             HeartToBreast3DSolverInputDeck &input,
                                             lm::ReplicatedMesh &mesh,
                                             lm::EquationSystems &eq_sys,
                                             lm::TransientLinearImplicitSystem &p_cap,
                                             lm::TransientLinearImplicitSystem &p_tis,
                                             lm::TransientLinearImplicitSystem &nut_cap,
                                             lm::TransientLinearImplicitSystem &nut_tis,
                                             lm::ExplicitSystem &K_cap_field,
                                             lm::ExplicitSystem &K_tis_field,
                                             lm::ExplicitSystem &Lp_art_cap_field,
                                             lm::ExplicitSystem &Lp_cap_tis_field,
                                             lm::ExplicitSystem &Lnut_cap_tis_field,
                                             lm::ExplicitSystem &Dnut_cap_field,
                                             lm::ExplicitSystem &Dnut_tis_field,
                                             Logger &log)
    : BaseModel(libmesh_comm, mesh, eq_sys, log, "HeartToBreast3DSolver"),
      d_input(input),
      d_p_cap(this, d_mesh, p_cap),
      d_p_tis(this, d_mesh, p_tis),
      d_K_cap_field(K_cap_field),
      d_K_tis_field(K_tis_field),
      d_Lp_art_cap_field(Lp_art_cap_field),
      d_Lp_cap_tis_field(Lp_cap_tis_field) {

  d_dt = input.d_dt;
  d_log("created HeartToBreast3DSolver object\n");
}

void HeartToBreast3DSolver::write_perfusion_output(std::string out_file) {

  auto vtu_writer = VTKWriter(out_file);
  add_points(d_perf_pts, vtu_writer.d_d_p);
  add_array("Radius", d_perf_radii, vtu_writer.d_d_p);
  add_array("Ball_Radius", d_perf_ball_radii, vtu_writer.d_d_p);
  add_array("pv", d_perf_pres, vtu_writer.d_d_p);
  add_array("pcap", d_perf_p_3d_weighted, vtu_writer.d_d_p);
  vtu_writer.write();
}

void HeartToBreast3DSolver::setup() {
  // TODO setup other things
  //d_eq_sys.init();
}
double HeartToBreast3DSolver::get_time() const {
  return d_time;
}
void HeartToBreast3DSolver::solve() {
  // solve for capillary and tissue pressure
  auto solve_clock = std::chrono::steady_clock::now();
  d_p_cap.solve();
  d_log("capillary pressure solve time = " + std::to_string(time_diff(solve_clock, std::chrono::steady_clock::now())) + "\n");

  solve_clock = std::chrono::steady_clock::now();
  d_p_tis.solve();
  d_log("tissue pressure solve time = " + std::to_string(time_diff(solve_clock, std::chrono::steady_clock::now())) + "\n");

  solve_clock = std::chrono::steady_clock::now();
  d_nut_cap.solve();
  d_log("capillary nutrient solve time = " + std::to_string(time_diff(solve_clock, std::chrono::steady_clock::now())) + "\n");

  solve_clock = std::chrono::steady_clock::now();
  d_nut_tis.solve();
  d_log("tissue nutrient solve time = " + std::to_string(time_diff(solve_clock, std::chrono::steady_clock::now())) + "\n");
}
void HeartToBreast3DSolver::write_output() {
  static int out_n = 0;
  VTKIO(d_mesh).write_equation_systems(d_input.d_out_dir + "/output_3D_" + std::to_string(out_n) + ".pvtu", d_eq_sys);
  write_perfusion_output(d_input.d_out_dir + "/output_3D_perf_" + std::to_string(out_n) + ".vtu");
  out_n++;
}
void HeartToBreast3DSolver::set_output_folder(std::string output_dir) {
}
void HeartToBreast3DSolver::setup_1d3d(const std::vector<VesselTipCurrentCouplingData> &data_1d) {
  if (d_input.d_perf_regularized) {
    std::cout << "Setting up regularized perfusion sources\n";
    setup_1d3d_reg_source(data_1d);
  } else {
    std::cout << "Setting up uniform partitioned perfusion sources\n";
    setup_1d3d_partition(data_1d);
  }
}

void HeartToBreast3DSolver::setup_1d3d_reg_source(const std::vector<VesselTipCurrentCouplingData> &data_1d) {

  auto num_perf_outlets = data_1d.size();
  if (num_perf_outlets == 0) {
    std::cerr << "Error outlet 1d data should not be empty.\n";
    exit(EXIT_FAILURE);
  }

  // step 1: copy relevant data
  for (const auto &a : data_1d) {
    d_perf_pts.push_back(lm::Point(a.p.x, a.p.y, a.p.z));
    d_perf_radii.push_back(a.radius);
    d_perf_pres.push_back(a.pressure);
    d_perf_pres_vein.push_back(40000.); // FIXME
    d_perf_nut.push_back(1.); // FIXME
    d_perf_nut_vein.push_back(0.); // FIXME
    d_perf_ball_radii.push_back(0.);
    d_perf_coeff_a.push_back(0.);
    d_perf_coeff_b.push_back(0.);
    d_perf_p_3d_weighted.push_back(0.);
    d_perf_nut_3d_weighted.push_back(0.);
  }

  //
  std::vector<double> perf_flow_capacity;
  for (const auto &r : d_perf_radii)
    perf_flow_capacity.push_back(std::pow(r, 3));
  double max_r3 = max(perf_flow_capacity);
  double min_r3 = min(perf_flow_capacity);

  // step 2: setup perfusion neighborhood
  // instead of point source, we have volume source supported over a ball.
  // radius of ball is proportional to the outlet radius^3 and varies from [ball_r_min, ball_r_max]
  double ball_r_min = d_input.d_perf_neigh_size.first;  // * d_input.d_h; // avoid point sources
  double ball_r_max = d_input.d_perf_neigh_size.second; // * d_input.d_h; // avoid too large neighborhood
  std::cout << "ball r = ";
  for (size_t i = 0; i < num_perf_outlets; i++) {
    d_perf_ball_radii[i] = ball_r_min + (ball_r_max - ball_r_min) * (perf_flow_capacity[i] - min_r3) / (max_r3 - min_r3);
    std::cout << d_perf_ball_radii[i] << "; ";
  }
  std::cout << std::endl;

  //  create outlet functions (we choose linear \phi(r) = 1 - r
  for (size_t i = 0; i < num_perf_outlets; i++) {
    if (d_input.d_perf_fn_type == "const")
      d_perf_fns.push_back(std::make_unique<ConstOutletRadial>(d_perf_pts[i], d_perf_ball_radii[i]));
    else if (d_input.d_perf_fn_type == "linear")
      d_perf_fns.push_back(std::make_unique<LinearOutletRadial>(d_perf_pts[i], d_perf_ball_radii[i]));
    else if (d_input.d_perf_fn_type == "gaussian")
      d_perf_fns.push_back(std::make_unique<GaussianOutletRadial>(d_perf_pts[i], d_perf_ball_radii[i], 0.5 * d_perf_ball_radii[i]));
    else {
      std::cerr << "Error: input flag for outlet weight function is invalid.\n";
      exit(EXIT_FAILURE);
    }
  }

  // step 3: for each outlet, create a list of elements and node affected by outlet source and also create coefficients
  d_perf_elems_3D.resize(num_perf_outlets);
  for (size_t I = 0; I < num_perf_outlets; I++) {
    auto &I_out_fn = d_perf_fns[I];
    std::vector<lm::dof_id_type> I_elems;
    for (const auto &elem : d_mesh.active_local_element_ptr_range()) {
      // check if element is inside the ball
      bool any_node_inside_ball = false;
      for (const auto &node : elem->node_index_range()) {
        const lm::Point nodei = elem->node_ref(node);
        auto dx = d_perf_pts[I] - nodei;
        if (dx.norm() < I_out_fn->d_r - 1.e-10) {
          any_node_inside_ball = true;
          break;
        }
      }

      if (any_node_inside_ball)
        add_unique(I_elems, elem->id());
    } // loop over elems

    d_perf_elems_3D[I] = I_elems;
    std::cout << "nelems (I = " << I << ") = " << I_elems.size() << "\n";
  } // loop over outlets

  // compute normalizing coefficient for each outlet weight function
  // note that elements associated to each outlet is now distributed
  // so we first compute local value and then global
  std::vector<double> local_out_normal_const(num_perf_outlets, 0.);
  for (size_t I = 0; I < num_perf_outlets; I++) {
    auto &out_fn_I = d_perf_fns[I];
    double c = 0.;
    // loop over elements
    for (const auto &elem_id : d_perf_elems_3D[I]) {
      const auto &elem = d_mesh.elem_ptr(elem_id);
      // init dof map
      d_p_cap.init_dof(elem);
      // init fe
      d_p_cap.init_fe(elem);
      // loop over quad points
      for (unsigned int qp = 0; qp < d_p_cap.d_qrule.n_points(); qp++) {
        c += d_p_cap.d_JxW[qp] * (*out_fn_I)(d_p_cap.d_qpoints[qp]);
      } // quad point loop
    }   // elem loop
    local_out_normal_const[I] = c;
  } // outlet loop

  // we now need to communicate among all processors to compute the global coefficient at processor 0
  std::vector<double> out_normal_c;
  comm_local_to_global(local_out_normal_const, out_normal_c);

  // last thing is to set the normalizing constant of outlet weight function
  for (size_t I = 0; I < num_perf_outlets; I++) {
    auto &out_fn_I = d_perf_fns[I];
    out_fn_I->set_normalize_const(1. / out_normal_c[I]);
    std::cout << "c (I = " << I << ") = " << (*out_fn_I).d_c << "\n";
  }

  // step 4: compute coefficients that we need to exchange with the network system
  std::vector<unsigned int> Lp_cap_dof_indices;
  std::vector<double> local_out_coeff_a(num_perf_outlets, 0.);
  std::vector<double> local_out_coeff_b(num_perf_outlets, 0.);
  std::vector<double> local_out_p_3d_weighted(num_perf_outlets, 0.);
  std::vector<double> local_out_nut_3d_weighted(num_perf_outlets, 0.);
  for (size_t I = 0; I < num_perf_outlets; I++) {
    auto &out_fn_I = d_perf_fns[I];
    double a = 0.;
    double b = 0.;
    double p_3d_w = 0.; // 3D weighted pressure at outlet
    double nut_3d_w = 0.;
    // loop over elements
    for (const auto &elem_id : d_perf_elems_3D[I]) {
      const auto &elem = d_mesh.elem_ptr(elem_id);
      // init dof map
      d_p_cap.init_dof(elem);
      d_nut_cap.init_dof(elem);
      d_Lp_art_cap_field.get_dof_map().dof_indices(elem, Lp_cap_dof_indices);

      // init fe
      d_p_cap.init_fe(elem);

      // get Lp at this element
      double Lp_elem = d_Lp_art_cap_field.current_solution(Lp_cap_dof_indices[0]);

      // loop over quad points
      for (unsigned int qp = 0; qp < d_p_cap.d_qrule.n_points(); qp++) {
        a += d_p_cap.d_JxW[qp] * (*out_fn_I)(d_p_cap.d_qpoints[qp]) * Lp_elem;

        // get pressure at quad point
        double p_qp = 0.;
        double nut_qp = 0.;
        for (unsigned int l = 0; l < d_p_cap.d_phi.size(); l++) {
          p_qp += d_p_cap.d_phi[l][qp] * d_p_cap.get_current_sol(l);
          nut_qp += d_p_cap.d_phi[l][qp] * d_nut_cap.get_current_sol(l);
        }

        b += d_p_cap.d_JxW[qp] * (*out_fn_I)(d_p_cap.d_qpoints[qp]) * Lp_elem * p_qp;

        p_3d_w += d_p_cap.d_JxW[qp] * (*out_fn_I)(d_p_cap.d_qpoints[qp]) * p_qp;
        nut_3d_w += d_p_cap.d_JxW[qp] * (*out_fn_I)(d_p_cap.d_qpoints[qp]) * nut_qp;
      } // quad point loop
    }   // elem loop

    local_out_coeff_a[I] = a;
    local_out_coeff_b[I] = b;
    local_out_p_3d_weighted[I] = p_3d_w;
    local_out_nut_3d_weighted[I] = nut_3d_w;
  } // outlet loop
  std::cout << std::endl;

  // sum distributed coefficients and sync with all processors
  comm_local_to_global(local_out_coeff_a, d_perf_coeff_a);
  comm_local_to_global(local_out_coeff_b, d_perf_coeff_b);
  comm_local_to_global(local_out_p_3d_weighted, d_perf_p_3d_weighted);
  comm_local_to_global(local_out_nut_3d_weighted, d_perf_nut_3d_weighted);

  // at this point, all processors must have
  // 1. same d_c for outlet weight function
  // 2. same values of coefficients a and b

  // to verify that all processor have same values of coefficients a and b and normalizing constant
  if (d_input.d_debug_lvl > 0) {
    std::ofstream of;
    of.open(fmt::format("{}outlet_coefficients_t_{:5.3f}_proc_{}.txt", d_input.d_out_dir, d_time, get_comm()->rank()));
    of << "x, y, z, r, ball_r, c, a, b, p_3d_weighted\n";
    for (size_t I = 0; I < num_perf_outlets; I++) {
      auto &out_fn_I = d_perf_fns[I];
      of << d_perf_pts[I](0) << ", " << d_perf_pts[I](1) << ", " << d_perf_pts[I](2) << ", "
         << d_perf_radii[I] << ", " << d_perf_ball_radii[I] << ", "
         << (*out_fn_I).d_c << ", " << d_perf_coeff_a[I] << ", "
         << d_perf_coeff_b[I] << ", " << d_perf_p_3d_weighted[I] << ", " << d_perf_nut_3d_weighted[I] << "\n";
    }
    of.close();
  }
}

void HeartToBreast3DSolver::setup_1d3d_partition(const std::vector<VesselTipCurrentCouplingData> &data_1d) {

  auto num_perf_outlets = data_1d.size();
  if (num_perf_outlets == 0) {
    std::cerr << "Error outlet 1d data should not be empty.\n";
    exit(EXIT_FAILURE);
  }

  // step 1: copy relevant data
  for (const auto &a : data_1d) {
    d_perf_pts.push_back(lm::Point(a.p.x, a.p.y, a.p.z));
    d_perf_radii.push_back(a.radius);
    d_perf_pres.push_back(a.pressure);
    d_perf_pres_vein.push_back(40000.); // FIXME
    d_perf_nut.push_back(1.); // FIXME
    d_perf_nut_vein.push_back(0.); // FIXME
    d_perf_ball_radii.push_back(0.);
    d_perf_coeff_a.push_back(0.);
    d_perf_coeff_b.push_back(0.);
    d_perf_p_3d_weighted.push_back(0.);
    d_perf_nut_3d_weighted.push_back(0.);
  }

  //  create outlet functions (for this case, it will be constant function)
  for (size_t i = 0; i < num_perf_outlets; i++) {
    if (d_input.d_perf_fn_type == "const")
      d_perf_fns.push_back(std::make_unique<ConstOutletRadial>(d_perf_pts[i], DBL_MAX)); // so that it is practically 1 for any point
    else {
      std::cerr << "Error: input flag for outlet weight function is invalid for partitioned perfusion.\n";
      exit(EXIT_FAILURE);
    }
  }

  // step 3: for each outlet, create a list of elements and node affected by outlet source and also create coefficients
  d_perf_elems_3D.resize(num_perf_outlets);
  for (const auto &elem : d_mesh.active_local_element_ptr_range()) {
    auto xc = elem->centroid();

    // find the closest outlet to the center of this element
    double dist = (xc - d_perf_pts[0]).norm();
    long I_found = 0;
    for (size_t I = 0; I < num_perf_outlets; I++) {
      double dist2 = (xc - d_perf_pts[I]).norm();
      if (dist2 < dist) {
        I_found = I;
        dist = dist2;
      }
    }

    // add element to outlet
    d_perf_elems_3D[I_found].push_back(elem->id());
  }

  for (size_t I = 0; I < num_perf_outlets; I++)
    std::cout << "nelems (I = " << I << ") = " << d_perf_elems_3D[I].size() << "\n";

  // compute normalizing coefficient for each outlet weight function
  // note that elements associated to each outlet is now distributed
  // so we first compute local value and then global
  std::vector<double> local_out_normal_const(num_perf_outlets, 0.);
  for (size_t I = 0; I < num_perf_outlets; I++) {
    auto &out_fn_I = d_perf_fns[I];
    double c = 0.;
    // loop over elements
    for (const auto &elem_id : d_perf_elems_3D[I]) {
      const auto &elem = d_mesh.elem_ptr(elem_id);
      c += elem->volume();
    } // elem loop
    local_out_normal_const[I] = c;
  } // outlet loop

  // we now need to communicate among all processors to compute the global coefficient at processor 0
  std::vector<double> out_normal_c;
  comm_local_to_global(local_out_normal_const, out_normal_c);

  // last thing is to set the normalizing constant of outlet weight function
  for (size_t I = 0; I < num_perf_outlets; I++) {
    auto &out_fn_I = d_perf_fns[I];
    out_fn_I->set_normalize_const(1. / out_normal_c[I]);
    std::cout << "c (I = " << I << ") = " << (*out_fn_I).d_c << "\n";
  }

  // step 4: compute coefficients that we need to exchange with the network system
  std::vector<unsigned int> Lp_cap_dof_indices;
  std::vector<double> local_out_coeff_a(num_perf_outlets, 0.);
  std::vector<double> local_out_coeff_b(num_perf_outlets, 0.);
  std::vector<double> local_out_p_3d_weighted(num_perf_outlets, 0.);
  std::vector<double> local_out_nut_3d_weighted(num_perf_outlets, 0.);
  for (size_t I = 0; I < num_perf_outlets; I++) {
    auto &out_fn_I = d_perf_fns[I];
    double a = 0.;
    double b = 0.;
    double p_3d_w = 0.; // 3D weighted pressure at outlet
    double nut_3d_w = 0.;
    // loop over elements
    for (const auto &elem_id : d_perf_elems_3D[I]) {
      const auto &elem = d_mesh.elem_ptr(elem_id);
      // init dof map
      d_p_cap.init_dof(elem);
      d_nut_cap.init_dof(elem);
      d_Lp_art_cap_field.get_dof_map().dof_indices(elem, Lp_cap_dof_indices);

      // init fe
      d_p_cap.init_fe(elem);

      // get Lp at this element
      double Lp_elem = d_Lp_art_cap_field.current_solution(Lp_cap_dof_indices[0]);

      // loop over quad points
      for (unsigned int qp = 0; qp < d_p_cap.d_qrule.n_points(); qp++) {
        a += d_p_cap.d_JxW[qp] * (*out_fn_I)(d_p_cap.d_qpoints[qp]) * Lp_elem;

        // get pressure at quad point
        double p_qp = 0.;
        double nut_qp = 0.;
        for (unsigned int l = 0; l < d_p_cap.d_phi.size(); l++) {
          p_qp += d_p_cap.d_phi[l][qp] * d_p_cap.get_current_sol(l);
          nut_qp += d_p_cap.d_phi[l][qp] * d_nut_cap.get_current_sol(l);
        }

        b += d_p_cap.d_JxW[qp] * (*out_fn_I)(d_p_cap.d_qpoints[qp]) * Lp_elem * p_qp;

        p_3d_w += d_p_cap.d_JxW[qp] * (*out_fn_I)(d_p_cap.d_qpoints[qp]) * p_qp;
        nut_3d_w += d_p_cap.d_JxW[qp] * (*out_fn_I)(d_p_cap.d_qpoints[qp]) * nut_qp;
      } // quad point loop
    }   // elem loop

    local_out_coeff_a[I] = a;
    local_out_coeff_b[I] = b;
    local_out_p_3d_weighted[I] = p_3d_w;
    local_out_nut_3d_weighted[I] = nut_3d_w;
  } // outlet loop

  // sum distributed coefficients and sync with all processors
  comm_local_to_global(local_out_coeff_a, d_perf_coeff_a);
  comm_local_to_global(local_out_coeff_b, d_perf_coeff_b);
  comm_local_to_global(local_out_p_3d_weighted, d_perf_p_3d_weighted);
  comm_local_to_global(local_out_nut_3d_weighted, d_perf_nut_3d_weighted);

  // at this point, all processors must have
  // 1. same d_c for outlet weight function
  // 2. same values of coefficients a and b

  // to verify that all processor have same values of coefficients a and b and normalizing constant
  if (d_input.d_debug_lvl > 0) {
    std::ofstream of;
    of.open(fmt::format("{}outlet_coefficients_t_{:5.3f}_proc_{}.txt", d_input.d_out_dir, d_time, get_comm()->rank()));
    of << "x, y, z, r, ball_r, c, a, b, p_3d_weighted\n";
    for (size_t I = 0; I < num_perf_outlets; I++) {
      auto &out_fn_I = d_perf_fns[I];
      of << d_perf_pts[I](0) << ", " << d_perf_pts[I](1) << ", " << d_perf_pts[I](2) << ", "
         << d_perf_radii[I] << ", " << d_perf_ball_radii[I] << ", "
         << (*out_fn_I).d_c << ", " << d_perf_coeff_a[I] << ", "
         << d_perf_coeff_b[I] << ", " << d_perf_p_3d_weighted[I] << ", " << d_perf_nut_3d_weighted[I] << "\n";
    }
    of.close();
  }
}

void HeartToBreast3DSolver::comm_local_to_global(const std::vector<double> &local, std::vector<double> &global) {

  auto n = local.size();

  // collect data of all processors in processor 0
  const auto &comm = get_comm();
  std::vector<double> recv_c = local;
  comm->gather(0, recv_c);

  // in rank 0, compute coefficients
  if (comm->rank() == 0) {
    // resize of appropriate size
    global.resize(n);
    for (auto &a : global)
      a = 0.;

    // compute
    for (int i = 0; i < comm->size(); i++) {
      for (size_t I = 0; I < n; I++)
        global[I] += recv_c[i * n + I];
    }
  } else
    // in rank other than 0, set the data size to zero so that 'allgather' works properly
    global.resize(0);

  // do allgather (since rank \neq 0 has no data and only rank 0 has data, this should work)
  comm->allgather(global);
}
std::vector<VesselTipCurrentCouplingData3D> HeartToBreast3DSolver::get_vessel_tip_data_3d() {
  std::vector<VesselTipCurrentCouplingData3D> data;
  for (size_t i = 0; i < d_perf_pts.size(); i++) {
    VesselTipCurrentCouplingData3D d;
    d.d_a = d_perf_coeff_a[i];
    d.d_b = d_perf_coeff_b[i];
    d.d_p_3d_w = d_perf_p_3d_weighted[i];
    d.d_nut_3d_w = d_perf_nut_3d_weighted[i];
  }

  return data;
}

void HeartToBreast3DSolver::update_3d_data() {
  auto num_perf_outlets = d_perf_pts.size();

  // recompute coefficients b and avg 3d pressure
  std::vector<unsigned int> Lp_cap_dof_indices;
  std::vector<double> local_out_coeff_b(num_perf_outlets, 0.);
  std::vector<double> local_out_p_3d_weighted(num_perf_outlets, 0.);
  std::vector<double> local_out_nut_3d_weighted(num_perf_outlets, 0.);
  for (size_t I = 0; I < num_perf_outlets; I++) {
    auto &out_fn_I = d_perf_fns[I];
    double b = 0.;
    double p_3d_w = 0.; // 3D weighted pressure at outlet
    double nut_3d_w = 0.;
    // loop over elements
    for (const auto &elem_id : d_perf_elems_3D[I]) {
      const auto &elem = d_mesh.elem_ptr(elem_id);
      // init dof map
      d_p_cap.init_dof(elem);
      d_nut_cap.init_dof(elem);
      d_Lp_art_cap_field.get_dof_map().dof_indices(elem, Lp_cap_dof_indices);

      // init fe
      d_p_cap.init_fe(elem);

      // get Lp at this element
      double Lp_elem = d_Lp_art_cap_field.current_solution(Lp_cap_dof_indices[0]);

      // loop over quad points
      for (unsigned int qp = 0; qp < d_p_cap.d_qrule.n_points(); qp++) {
        // get pressure at quad point
        double p_qp = 0.;
        double nut_qp = 0.;
        for (unsigned int l = 0; l < d_p_cap.d_phi.size(); l++) {
          p_qp += d_p_cap.d_phi[l][qp] * d_p_cap.get_current_sol(l);
          nut_qp += d_p_cap.d_phi[l][qp] * d_nut_cap.get_current_sol(l);
        }

        b += d_p_cap.d_JxW[qp] * (*out_fn_I)(d_p_cap.d_qpoints[qp]) * Lp_elem * p_qp;

        p_3d_w += d_p_cap.d_JxW[qp] * (*out_fn_I)(d_p_cap.d_qpoints[qp]) * p_qp;
        nut_3d_w += d_p_cap.d_JxW[qp] * (*out_fn_I)(d_p_cap.d_qpoints[qp]) * nut_qp;
      } // quad point loop
    }   // elem loop

    local_out_coeff_b[I] = b;
    local_out_p_3d_weighted[I] = p_3d_w;
    local_out_nut_3d_weighted[I] = nut_3d_w;
  } // outlet loop

  // sum distributed coefficients and sync with all processors
  comm_local_to_global(local_out_coeff_b, d_perf_coeff_b);
  comm_local_to_global(local_out_p_3d_weighted, d_perf_p_3d_weighted);
  comm_local_to_global(local_out_nut_3d_weighted, d_perf_nut_3d_weighted);
}

void HeartToBreast3DSolver::update_1d_data(const std::vector<VesselTipCurrentCouplingData> &data_1d) {
  if (d_perf_pts.size() != data_1d.size()) {
    std::cerr << "1D data passed should match the 1D data currently stored in the 3D solver.\n";
    exit(EXIT_FAILURE);
  }

  for (size_t i = 0; i < data_1d.size(); i++) {
    d_perf_pres[i] = data_1d[i].pressure;
    d_perf_pres_vein[i] = 40000.; // FIXME
    d_perf_nut[i] = 1.; // FIXME
    d_perf_nut_vein[i] = 0.; // FIXME
  }
}

void HeartToBreast3DSolver::set_conductivity_fields() {
  std::vector<unsigned int> dof_indices;
  for (const auto &elem : d_mesh.active_local_element_ptr_range()) {
    d_Lp_art_cap_field.get_dof_map().dof_indices(elem, dof_indices);
    d_Lp_art_cap_field.solution->set(dof_indices[0], d_input.d_Lp_art_cap);

    d_Lp_cap_tis_field.get_dof_map().dof_indices(elem, dof_indices);
    d_Lp_cap_tis_field.solution->set(dof_indices[0], d_input.d_Lc_cap * d_input.d_Sc_cap);

    d_K_cap_field.get_dof_map().dof_indices(elem, dof_indices);
    d_K_cap_field.solution->set(dof_indices[0], d_input.d_K_cap);

    d_K_tis_field.get_dof_map().dof_indices(elem, dof_indices);
    d_K_tis_field.solution->set(dof_indices[0], d_input.d_K_tis);

    d_Lnut_cap_tis_field.get_dof_map().dof_indices(elem, dof_indices);
    d_Lnut_cap_tis_field.solution->set(dof_indices[0], d_input.d_Lnut_cap * d_input.d_Sc_cap);

    d_Dnut_cap_field.get_dof_map().dof_indices(elem, dof_indices);
    d_Dnut_cap_field.solution->set(dof_indices[0], d_input.d_Dnut_cap);

    d_Dnut_tis_field.get_dof_map().dof_indices(elem, dof_indices);
    d_Dnut_tis_field.solution->set(dof_indices[0], d_input.d_Dtis_cap);
  }
  d_Lp_art_cap_field.solution->close();
  d_Lp_art_cap_field.update();

  d_Lp_cap_tis_field.solution->close();
  d_Lp_cap_tis_field.update();

  d_K_cap_field.solution->close();
  d_K_cap_field.update();

  d_K_tis_field.solution->close();
  d_K_tis_field.update();

  d_Lnut_cap_tis_field.solution->close();
  d_Lnut_cap_tis_field.update();

  d_Dnut_cap_field.solution->close();
  d_Dnut_cap_field.update();

  d_Dnut_tis_field.solution->close();
  d_Dnut_tis_field.update();
}

} // namespace macrocirculation