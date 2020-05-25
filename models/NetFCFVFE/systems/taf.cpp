////////////////////////////////////////////////////////////////////////////////
//  Copyright (c) 2019 Prashant K. Jha
//
//  Distributed under the Boost Software License, Version 1.0. (See accompanying
//  file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
////////////////////////////////////////////////////////////////////////////////

#include "../model.hpp"

namespace {

double get_taf_source(const std::string &test_name, const Point &x,
                      const std::vector<int> &type,
                      const std::vector<std::vector<double>> &centers,
                      const std::vector<double> &rads) {

  if (test_name != "test_taf" and test_name != "test_taf_2")
    return 0.;

  for (int i=0; i < type.size(); i++) {

    const Point xc = util::to_point(centers[i]);
    auto d = (x - xc).norm();

    if (d < rads[i])
      return 1.;
  }

  return 0.;
}

}

Number netfcfvfe::initial_condition_taf(const Point &p, const Parameters &es,
                              const std::string &system_name, const std::string &var_name){

  libmesh_assert_equal_to(system_name,"TAF");

  return 0.;
}

// Assembly class
void netfcfvfe::TafAssembly::assemble() {
  assemble_1();
}

void netfcfvfe::TafAssembly::assemble_1() {

  // Get required system alias
  // auto &taf = d_model_p->get_ecm_assembly();
  auto &hyp = d_model_p->get_hyp_assembly();  

  // Model parameters
  const auto &deck = d_model_p->get_input_deck();
  const Real dt = d_model_p->d_dt;

  // Store current and old solution
  Real taf_old = 0.;
  Real hyp_cur = 0.;
  Real hyp_proj = 0.;

  Real compute_rhs = 0.;
  Real compute_mat = 0.;

  // Looping through elements
  for (const auto &elem : d_mesh.active_local_element_ptr_range()) {

    init_dof(elem);
    hyp.init_dof(elem);

    // init fe and element matrix and vector
    init_fe(elem);

    for (unsigned int qp = 0; qp < d_qrule.n_points(); qp++) {

      // Computing solution
      taf_old = 0.; hyp_cur = 0.;
      for (unsigned int l = 0; l < d_phi.size(); l++) {

        taf_old += d_phi[l][qp] * get_old_sol(l);
        hyp_cur += d_phi[l][qp] * hyp.get_current_sol(l);
      }

      if (deck.d_assembly_method == 1) {

        compute_rhs = d_JxW[qp] * (taf_old + dt * deck.d_lambda_TAF * hyp_cur);
        compute_mat = d_JxW[qp] * (1. + dt * deck.d_lambda_TAF * hyp_cur);

      } else {

        hyp_proj = util::project_concentration(hyp_cur);

        compute_rhs = d_JxW[qp] * (taf_old + dt * deck.d_lambda_TAF * hyp_proj);
        compute_mat = d_JxW[qp] * (1. + dt * deck.d_lambda_TAF * hyp_proj);
      }

      // add artificial source if any
      compute_rhs +=
          d_JxW[qp] * dt * deck.d_lambda_TAF *
          get_taf_source(deck.d_test_name, d_qpoints[qp],
                         deck.d_taf_source_type,
                         deck.d_taf_source_center, deck.d_taf_source_radius);

      // Assembling matrix
      for (unsigned int i = 0; i < d_phi.size(); i++) {

        d_Fe(i) += compute_rhs * d_phi[i][qp];

        for (unsigned int j = 0; j < d_phi.size(); j++) {

          d_Ke(i, j) += compute_mat * d_phi[j][qp] * d_phi[i][qp];

          // gradient term
          d_Ke(i, j) +=
              d_JxW[qp] * dt * deck.d_D_TAF * d_dphi[j][qp] * d_dphi[i][qp];
        }
      }
    } // loop over quadrature points

    d_dof_map_sys.heterogenously_constrain_element_matrix_and_vector(d_Ke, d_Fe,
                                                                     d_dof_indices_sys);
    d_sys.matrix->add_matrix(d_Ke, d_dof_indices_sys);
    d_sys.rhs->add_vector(d_Fe, d_dof_indices_sys);
  }
}