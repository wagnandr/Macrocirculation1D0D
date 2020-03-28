////////////////////////////////////////////////////////////////////////////////
//  Copyright (c) 2019 Prashant K. Jha
//
//  Distributed under the Boost Software License, Version 1.0. (See accompanying
//  file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
////////////////////////////////////////////////////////////////////////////////

#ifndef AVAFV_SYSTEMS_H
#define AVAFV_SYSTEMS_H

// systems
#include "tumor.hpp"
#include "nutrient.hpp"
#include "hypoxic.hpp"
#include "necrotic.hpp"
#include "taf.hpp"
#include "grad_taf.hpp"

namespace avafv {

/*!
 * @brief Compute matrix contribution from diffusion and advection
 *
 * @param sys System for which assembly is performed
 */
void assemble_diffusion(BaseAssembly &sys);

} // namespace avafv

#endif // AVAFV_SYSTEMS_H
