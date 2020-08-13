////////////////////////////////////////////////////////////////////////////////
//  Copyright (c) 2019 Prashant K. Jha
//
//  Distributed under the Boost Software License, Version 1.0. (See accompanying
//  file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
////////////////////////////////////////////////////////////////////////////////

#include "../model.hpp"

Number twosp::initial_condition_tum(const Point &p, const Parameters &es,
                              const std::string &system_name, const std::string &var_name){

  libmesh_assert_equal_to(system_name,"Tumor");

  if (var_name == "tumor") {

    const auto *deck = es.get<InpDeck *>("input_deck");

    const unsigned int dim = deck->d_dim;
    const unsigned int num_ic = deck->d_tum_ic_data.size();
    if (num_ic == 0)
      return 0.;

    for (unsigned int ic = 0; ic < num_ic; ic++) {

      auto data = deck->d_tum_ic_data[ic];

      const std::string type =data.d_ic_type;
      const Point xc = Point(data.d_ic_center[0], data.d_ic_center[1],
                             data.d_ic_center[2]);
      const Point dx = p - xc;

      if (type == "tumor_spherical" or
          type == "tumor_spherical_sharp") {
        if (dx.norm() < data.d_tum_ic_radius[0] - 1.0E-12) {

          // out << "here tum ic\n";

          if (type == "tumor_spherical_sharp")
            return 1.;
          else
            return util::exp_decay_function(dx.norm() / data.d_tum_ic_radius[0],
                                          4.);
        }
      } else if (type == "tumor_elliptical") {

        if (util::is_inside_ellipse(p, xc, data.d_tum_ic_radius, deck->d_dim))
          return 1.;

        
      }
    }

    return 0.;
  }

  return 0.;
}

// Assembly class
void twosp::TumAssembly::assemble() {

  assemble_1();
}

void twosp::TumAssembly::assemble_1() {

  // Get required system alias
  // auto &tum = d_model_p->get_tum_assembly();
  auto &nut = d_model_p->get_nut_assembly();

  // Model parameters
  const auto &deck = d_model_p->get_input_deck();
  const Real dt = d_model_p->d_dt;

  // Store current and old solution
  Real tum_old = 0.;
  Real tum_cur = 0.;
  Real nut_cur = 0.;

  Real nut_proj = 0.;
  Real tum_proj = 0.;
  
  Real mobility = 0.;

  Real compute_rhs_tum = 0.;
  Real compute_rhs_mu = 0.;
  Real compute_mat_tum = 0.;

  // Looping through elements
  for (const auto &elem : d_mesh.active_local_element_ptr_range()) {

    init_dof(elem);
    nut.init_dof(elem);

    // init fe and element matrix and vector
    init_fe(elem);

    for (unsigned int qp = 0; qp < d_qrule.n_points(); qp++) {

      // Computing solution
      tum_old = 0.; tum_cur = 0.; nut_cur = 0.;
      for (unsigned int l = 0; l < d_phi.size(); l++) {

        tum_old += d_phi[l][qp] * get_old_sol_var(l, 0);
        tum_cur += d_phi[l][qp] * get_current_sol_var(l, 0);
        nut_cur += d_phi[l][qp] * nut.get_current_sol(l);
      }

      // get projected solution
      tum_proj = util::project_concentration(tum_cur);
      nut_proj = util::project_concentration(nut_cur);

      mobility = deck.d_bar_M_P * pow(tum_proj, 2) * pow(1. - tum_proj, 2) +
                 deck.d_bar_M_H * pow(tum_proj, 2) * pow(1. - tum_proj, 2);

      if (deck.d_assembly_method == 1) {

        // compute quantities independent of dof loop
        compute_rhs_tum =
            d_JxW[qp] * (tum_old + dt * deck.d_lambda_P * nut_cur * tum_cur);

        compute_rhs_mu =
            d_JxW[qp] * (deck.d_bar_E_phi_T * tum_old *
                             (4.0 * pow(tum_old, 2) - 6.0 * tum_old - 1.) -
                         deck.d_chi_c * nut_cur);

        compute_mat_tum =
            d_JxW[qp] * (1. + dt * deck.d_lambda_A +
                         dt * deck.d_lambda_P * nut_cur * tum_cur);
      } else {

        // compute quantities independent of dof loop
        compute_rhs_tum =
            d_JxW[qp] * (tum_old + dt * deck.d_lambda_P * nut_proj * tum_proj);

        compute_rhs_mu =
            d_JxW[qp] * (deck.d_bar_E_phi_T * tum_old *
                         (4.0 * pow(tum_old, 2) - 6.0 * tum_old - 1.) -
                         deck.d_chi_c * nut_proj);

        compute_mat_tum =
            d_JxW[qp] * (1. + dt * deck.d_lambda_A +
                         dt * deck.d_lambda_P * nut_proj * tum_proj);
      }

      // Assembling matrix
      for (unsigned int i = 0; i < d_phi.size(); i++) {

        //-- Tumor --//
        d_Fe_var[0](i) += compute_rhs_tum * d_phi[i][qp];

        //-- Chemical Potential --//
        d_Fe_var[1](i) += compute_rhs_mu * d_phi[i][qp];

        for (unsigned int j = 0; j < d_phi.size(); j++) {

          //-- Tumor --//
          d_Ke_var[0][0](i, j) += compute_mat_tum * d_phi[j][qp] * d_phi[i][qp];

          // coupling with chemical potential
          d_Ke_var[0][1](i, j) +=
              d_JxW[qp] * dt * mobility * d_dphi[j][qp] * d_dphi[i][qp];

          //-- Chemical_tumor --//
          d_Ke_var[1][1](i, j) += d_JxW[qp] * d_phi[j][qp] * d_phi[i][qp];

          // coupling with tumor
          d_Ke_var[1][0](i, j) -= d_JxW[qp] * 3.0 * deck.d_bar_E_phi_T * d_phi[j][qp] * d_phi[i][qp];

          d_Ke_var[1][0](i, j) -=
              d_JxW[qp] * pow(deck.d_epsilon_T, 2) * d_dphi[j][qp] * d_dphi[i][qp];
        }
      }
    } // loop over quadrature points

    d_dof_map_sys.heterogenously_constrain_element_matrix_and_vector(d_Ke, d_Fe,
                                                               d_dof_indices_sys);
    d_sys.matrix->add_matrix(d_Ke, d_dof_indices_sys);
    d_sys.rhs->add_vector(d_Fe, d_dof_indices_sys);
  }
}