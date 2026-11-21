// SPDX-License-Identifier: AGPL-3.0-or-later
// Producer CLI boundary. Kernel environment wiring is private and removed by the Context split.
#pragma once

#include <unistd.h>

#include <cerrno>
#include <cstring>
#include <iostream>
#include <map>
#include <set>
#include <string>
#include <vector>

#include "construct/components/production_policy.hpp"
#include "construct/model/alignment_options.hpp"
#include "construct/model/memory_plan.hpp"
#include "construct/output/quarantine.hpp"
#include "exact/arithmetic/arith.hpp"
#include "exact/arithmetic/maximal_order.hpp"
#include "sic/support/cli.hpp"
#include "support/resources.hpp"

namespace sic::construct {

class ProducerCommandLine {
  sic::support::KernelSettings kernel_options_;
  bool resume_ = false;
  void derive_storage();

  static void validate_output(const std::string& out, bool resume);

 public:
  sic::construct::ProductionPolicy policy;
  unsigned exponent = 0;

  static sic::support::cli::Parser parser();

  bool log_admissions = false;
  AlignmentOptions alignment;

  ProducerCommandLine(int argc, char** argv, sic::construct::ProductionPolicy chosen_policy = {});

  void install_kernel_options(char** environment);
};

}  // namespace sic::construct
