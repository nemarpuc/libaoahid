// SPDX-License-Identifier: MIT
// Copyright (c) 2026 libaoahid contributors
/* Counts the allocations aoahid_tests makes between begin() and finish(); the
 * replaced global operators live in allocation_probe.cpp. */
#pragma once

#include <cstddef>

namespace allocation_probe {

// Starts counting from zero.
void begin() noexcept;
// Stops counting and returns how many allocations were made since begin().
std::size_t finish() noexcept;

} // namespace allocation_probe
