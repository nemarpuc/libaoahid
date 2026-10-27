// SPDX-License-Identifier: MIT
// Copyright (c) 2026 libaoahid contributors
/* Checks that the C++ header forwards to the C ABI under namespace aoahid. */
#include "aoahid.hpp"

#include <cstdint>

// Every wait is an argument of the call and has no default; these are the tests' own choices.
inline constexpr std::uint32_t kControlTimeoutMs = 20U;
inline constexpr std::uint32_t kSendTimeoutMs = 20U;
inline constexpr std::uint32_t kDrainTimeoutMs = 20U;

int main() {
    if (aoahid::version() != aoahid_version() || aoahid::result_name(AOAHID_OK) == nullptr ||
        aoahid::keyboard_key(nullptr, 0U, 0U) != AOAHID_ERR_PARAM ||
        aoahid::mouse_move(nullptr, 0, 0) != AOAHID_ERR_PARAM ||
        aoahid::node_submit(nullptr, kSendTimeoutMs) != AOAHID_ERR_PARAM ||
        aoahid::context_create(nullptr, nullptr) != AOAHID_ERR_PARAM ||
        aoahid::last_error() != aoahid_last_error()) {
        return 1;
    }
    return 0;
}
