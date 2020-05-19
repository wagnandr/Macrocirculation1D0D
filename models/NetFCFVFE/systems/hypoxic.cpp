////////////////////////////////////////////////////////////////////////////////
//  Copyright (c) 2019 Prashant K. Jha
//
//  Distributed under the Boost Software License, Version 1.0. (See accompanying
//  file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
////////////////////////////////////////////////////////////////////////////////

#include "../model.hpp"

Number netfcfvfe::initial_condition_hyp_kernel(const Point &p,
                                           const unsigned int &dim,
                                           const std::string &ic_type,
                                           const std::vector<double> &ic_center,
                                           const std::vector<double> &tum_ic_radius,
                                           const std::vector<double> &hyp_ic_radius) {

  const Point xc = util::to_point(ic_center);
  const Point dx = p - xc;

  if (ic_type == "tumor_hypoxic_spherical") {

    if (dx.norm() < tum_ic_radius[0] - 1.0E-12) {

      return 1. - util::exp_decay_function(
          dx.norm() / tum_ic_radius[0], 4.);

    } else if (dx.norm() >  tum_ic_radius[0] - 1.0E-12 and
               dx.norm() <  hyp_ic_radius[0] - 1.0E-12) {

      return util::exp_decay_function(
          (dx.norm() -  tum_ic_radius[0]) /
          (hyp_ic_radius[0] - tum_ic_radius[0]),
          4.);
    }
  } else if (ic_type == "tumor_hypoxic_elliptical") {

    // transform ellipse into ball of radius
    double small_ball_r = 0.;
    for (unsigned int i = 0; i < dim; i++)
      small_ball_r +=  tum_ic_radius[i] *  tum_ic_radius[i];
    small_ball_r = std::sqrt(small_ball_r);

    Point p_small_ball =
        util::ellipse_to_ball(p, xc,  tum_ic_radius, dim, small_ball_r);

    // transform ellipse into ball of radius
    double large_ball_r = 0.;
    for (unsigned int i = 0; i < dim; i++)
      large_ball_r +=  hyp_ic_radius[i] *  hyp_ic_radius[i];
    large_ball_r = std::sqrt(large_ball_r);

    Point p_large_ball =
        util::ellipse_to_ball(p, xc,  hyp_ic_radius, dim, large_ball_r);

    if (p_small_ball.norm() < small_ball_r - 1.0E-12) {

      return 1. -
             util::exp_decay_function(p_small_ball.norm() / small_ball_r, 4.);

    } else if (p_small_ball.norm() > small_ball_r - 1.0E-12 and
               p_small_ball.norm() < large_ball_r - 1.0E-12) {

      return util::exp_decay_function((p_small_ball.norm() - small_ball_r) /
                                      (large_ball_r - small_ball_r),
                                      4.);
    }
  }

  return 0.;
}

Number netfcfvfe::initial_condition_hyp(const Point &p, const Parameters &es,
                                    const std::string &system_name,
                                    const std::string &var_name) {

  libmesh_assert_equal_to(system_name, "Hypoxic");

  if (var_name == "hypoxic") {

    const auto *deck = es.get<InpDeck *>("input_deck");

    double val = 0.;
    for (unsigned int i=0; i<deck->d_tum_ic_data.size(); i++)
      val += initial_condition_hyp_kernel(p, deck->d_dim,
                                          deck->d_tum_ic_data[i].d_ic_type,
                                          deck->d_tum_ic_data[i].d_ic_center,
                                          deck->d_tum_ic_data[i].d_tum_ic_radius,
                                          deck->d_tum_ic_data[i].d_hyp_ic_radius);

    return val;
  }

  return 0.;
}

// Assembly class
void netfcfvfe::HypAssembly::assemble() {
  assemble_1();
}

void netfcfvfe::HypAssembly::assemble_1() {

  // Get required system alias
  // auto &hyp = d_model_p->get_hyp_assembly();
  auto &nut = d_model_p->get_nut_assembly();
  auto &tum = d_model_p->get_tum_assembly();
  auto &nec = d_model_p->get_nec_assembly();

  // Model parameters
  const auto &deck = d_model_p->get_input_deck();
  const Real dt = d_model_p->d_dt;

  // Store current and old solution
  Real hyp_old = 0.;
  Real hyp_cur = 0.;
  Real tum_cur = 0.;
  Real nut_cur = 0.;
  Real nec_cur = 0.;

  Real tum_proj = 0.;
  Real hyp_proj = 0.;
  Real nut_proj = 0.;
  Real nec_proj = 0.;

  Gradient che_grad;

  Real mobility = 0.;

  Real compute_rhs = 0.;
  Real compute_mat = 0.;

  // Looping through elements
  for (const auto &elem : d_mesh.active_local_element_ptr_range()) {

    init_dof(elem);
    nut.init_dof(elem);
    tum.init_dof(elem);
    nec.init_dof(elem);

    // init fe and element matrix and vector
    init_fe(elem);

    // get finite-volume quantities
    nut_cur = nut.get_current_sol(0);
    nut_proj = util::project_concentration(nut_cur);

    for (unsigned int qp = 0; qp < d_qrule.n_points(); qp++) {

      // Computing solution
      tum_cur = 0.; hyp_cur = 0.; hyp_old = 0.; nec_cur = 0.;
      che_grad = 0.;
      for (unsigned int l = 0; l < d_phi.size(); l++) {

        tum_cur += d_phi[l][qp] * tum.get_current_sol_var(l, 0);
        hyp_cur += d_phi[l][qp] * get_current_sol(l);
        hyp_old += d_phi[l][qp] * get_old_sol(l);
        nec_cur += d_phi[l][qp] * nec.get_current_sol(l);

        che_grad.add_scaled(d_dphi[l][qp],
                            tum.get_current_sol_var(l, 1));
      }

      // get projected solution
      hyp_proj = util::project_concentration(hyp_cur);

      mobility =
          deck.d_bar_M_H * pow(hyp_proj, 2) * pow(1. - hyp_proj, 2);

      if (deck.d_assembly_method == 1) {

        compute_rhs =
            d_JxW[qp] *
            (hyp_old + dt * deck.d_lambda_PH *
                           util::heaviside(deck.d_sigma_PH - nut_cur) *
                           (tum_cur - nec_cur));

        compute_mat =
            d_JxW[qp] * (1. + dt * deck.d_lambda_A +
                         dt * deck.d_lambda_HP *
                             util::heaviside(nut_cur - deck.d_sigma_HP) +
                         dt * deck.d_lambda_PH *
                             util::heaviside(deck.d_sigma_PH - nut_cur) +
                         dt * deck.d_lambda_HN *
                             util::heaviside(deck.d_sigma_HN - nut_cur));
      } else {

        tum_proj = util::project_concentration(tum_cur);
        nec_proj = util::project_concentration(nec_cur);

        compute_rhs =
            d_JxW[qp] *
            (hyp_old + dt * deck.d_lambda_PH *
                       util::heaviside(deck.d_sigma_PH - nut_proj) *
                       (tum_proj - nec_proj));

        compute_mat =
            d_JxW[qp] * (1. + dt * deck.d_lambda_A +
                         dt * deck.d_lambda_HP *
                         util::heaviside(nut_proj - deck.d_sigma_HP) +
                         dt * deck.d_lambda_PH *
                         util::heaviside(deck.d_sigma_PH - nut_proj) +
                         dt * deck.d_lambda_HN *
                         util::heaviside(deck.d_sigma_HN - nut_proj));
      }

      // Assembling matrix
      for (unsigned int i = 0; i < d_phi.size(); i++) {

        d_Fe(i) += compute_rhs * d_phi[i][qp];

        // gradient term
        d_Fe(i) -= d_JxW[qp] * dt * mobility * che_grad * d_dphi[i][qp];

        for (unsigned int j = 0; j < d_phi.size(); j++) {

          d_Ke(i, j) += compute_mat * d_phi[j][qp] * d_phi[i][qp];
        }
      }
    } // loop over quadrature points

    d_dof_map_sys.heterogenously_constrain_element_matrix_and_vector(
        d_Ke, d_Fe, d_dof_indices_sys);
    d_sys.matrix->add_matrix(d_Ke, d_dof_indices_sys);
    d_sys.rhs->add_vector(d_Fe, d_dof_indices_sys);
  }
}