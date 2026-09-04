#pragma once

#include <cstdint>

#include "preemption_fabric/core/enums.hpp"
#include "preemption_fabric/core/identity.hpp"

namespace pf {

// A resource release obligation. The runtime governs whether the current
// execution has satisfied its release obligations; the Resource Broker owns
// arbitration of who receives the resource next. A release receipt is
// generation-bound: a stale receipt must not alter current accounting.
struct ReleaseObligation {
  ResourceClass resource = ResourceClass::kOther;
  std::uint64_t units = 0;

  bool must_release = true;    // required for PREEMPTED
  bool may_stay_warm = false;  // optional retention
  bool transfer_ownership = false;
  bool external_confirm = false;  // requires external confirmation from broker
	
  ResourceContractId contract_id;
  ResourceContractGeneration contract_generation;
  // Was released with the generation it actually released under.
  bool released = false;
  ReleaseGeneration release_generation;
};

// A completed release receipt, generation-bound. Duplicate/conflicting receipts
// are deterministic: a stale receipt never completes a current preemption.
struct ReleaseReceipt {
  ResourceClass resource = ResourceClass::kOther;
  std::uint64_t units = 0;
  ResourceContractId contract_id;
  ResourceContractGeneration contract_generation;
  ReleaseGeneration release_generation;
  WorkerBootId worker_boot_id;
  bool acknowledged = false;
};

}  // namespace pf
