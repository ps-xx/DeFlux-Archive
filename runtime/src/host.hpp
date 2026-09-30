#pragma once

#include "deflux/runtime/abi.h"

namespace deflux::runtime {

DefluxRuntimeResult query_abi(DefluxRuntimeAbiInfo* out_info) noexcept;
DefluxRuntimeResult initialize(const DefluxRuntimeInitContext* context) noexcept;
DefluxRuntimeResult shutdown() noexcept;
DefluxRuntimeResult get_status(DefluxRuntimeStatus* out_status) noexcept;

} // namespace deflux::runtime
