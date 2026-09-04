#pragma once

// Preemption Fabric 1.0.0 public umbrella header.
//
// Preemption Fabric is an open-source, vendor-neutral C++20 runtime for
// governing safe preemption, quiescence, checkpointability, state preservation,
// resource release, partial progress, priority inversion, and resume eligibility
// across heterogeneous accelerator infrastructure.
//
// It answers one systems question:
//
//   Can this work be interrupted safely now, what state and progress must
//   survive, which resources may be reclaimed, and under what authority and
//   conditions may execution resume?
//
// The downstream public API remains usable without the built-in TCP
// coordinator; transport is a reference deployment mechanism, not the core
// abstraction.

#include "preemption_fabric/core/enums.hpp"
#include "preemption_fabric/core/identity.hpp"

#include "preemption_fabric/domains/request.hpp"
#include "preemption_fabric/domains/safepoint.hpp"
#include "preemption_fabric/domains/progress.hpp"
#include "preemption_fabric/domains/plan.hpp"
#include "preemption_fabric/domains/preservation.hpp"
#include "preemption_fabric/domains/release.hpp"
#include "preemption_fabric/domains/resume.hpp"
#include "preemption_fabric/domains/quiescence.hpp"

#include "preemption_fabric/lifecycle/lifecycle.hpp"

#include "preemption_fabric/util/binary.hpp"
#include "preemption_fabric/util/checked.hpp"
#include "preemption_fabric/util/crc32.hpp"
#include "preemption_fabric/util/error.hpp"
