////////////////////////////////////////////////////////////////////////////////
//  Copyright (c) 2019 Prashant K. Jha
//
//  Distributed under the Boost Software License, Version 1.0. (See accompanying
//  file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
////////////////////////////////////////////////////////////////////////////////

#ifndef NETFVFE_PRESSURE_H
#define NETFVFE_PRESSURE_H

#include "abstraction.hpp"

namespace netfvfe {

/*!
 * @brief Initial condition for pressure
 *
 * @param p Point at which ic is computed
 * @param es Equation system
 * @param system_name Name of system
 * @param var_name Name of the variable
 * @param value Initial condition at given point
 */
Number initial_condition_pres(const Point &p, const Parameters &es,
                             const std::string &system_name, const std::string &var_name);

/*!
 * @brief Boundary condition for pressure
 *
 * @param es Equation system
 */
void boundary_condition_pres(EquationSystems &es);

/*!
 * @brief Class to perform assembly of pressure in tissue domain
 */
class PressureAssembly : public BaseAssembly {

public:
  /*!
   * @brief Constructor
   *
   * @param model Model class
   * @param sys_name Name of system
   * @param sys System
   */
  PressureAssembly(Model *model, const std::string system_name, MeshBase &mesh,
                   TransientLinearImplicitSystem &sys)
      : BaseAssembly(model, system_name, mesh, sys, 1,
                     {sys.variable_number("pressure")}) {}

  /*!
   * @brief Assembly function
   *
   * Overrides the default assembly function. It calls assemble_1,
   * assemble_2, or assemble_3 depending on user flag
   */
  void assemble() override;

private:

  /*!
   * @brief Assembly over volume of element
   *
   * In this we simply implement the assembly under iterative nonlinear
   * scheme. In source terms, those which are linear with respect to system
   * variable, we consider implicit scheme.
   */
  void assemble_1();

  /*!
   * @brief Assemble coupling between 3d and 1d pressure
   *
   * In this function, we handle the coupling between tissue pressure and
   * blood pressure.
   */
  void assemble_1d_coupling();

  /*!
   * @brief Assembly of terms over face of element
   */
  void assemble_face();
};

} // namespace netfvfe

#endif // NETFVFE_PRESSURE_H