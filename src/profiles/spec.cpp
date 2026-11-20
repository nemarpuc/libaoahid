// SPDX-License-Identifier: MIT
// Copyright (c) 2026 libaoahid contributors
/*
 * What every Spec has, whatever made it: its reference count, its descriptor and its manifest.
 * The factories are in spec_<profile>.cpp. No USB I/O, and no device-specific state.
 */

#include "profiles/spec_support.hpp"

using namespace aoa::spec_build;

namespace aoa::detail {

bool retain_spec(aoahid_spec* spec) noexcept {
    if (spec == nullptr) {
        return false;
    }
    std::uint32_t references = spec->references.load(std::memory_order_relaxed);
    while (references != std::numeric_limits<std::uint32_t>::max()) {
        if (spec->references.compare_exchange_weak(references, references + 1U,
                                                   std::memory_order_relaxed,
                                                   std::memory_order_relaxed)) {
            return true;
        }
    }
    return false;
}

bool release_spec(aoahid_spec* spec) noexcept {
    if (spec == nullptr) {
        return false;
    }
    std::uint32_t references = spec->references.load(std::memory_order_acquire);
    while (references != 0U) {
        if (spec->references.compare_exchange_weak(references, references - 1U,
                                                   std::memory_order_acq_rel,
                                                   std::memory_order_acquire)) {
            if (references == 1U) {
                delete spec;
            }
            return true;
        }
    }
    return false;
}

} // namespace aoa::detail

extern "C" {

static void aoahid_spec_retain_impl(aoahid_spec* spec) {
    aoa::detail::clear_error();
    if (!aoa::detail::retain_spec(spec)) {
        set_error(spec == nullptr ? AOAHID_ERR_PARAM : AOAHID_ERR_OVERFLOW, "spec.retain",
                  spec == nullptr ? "The spec handle is null."
                                  : "The spec reference count cannot be incremented further.");
    }
}

static void aoahid_spec_release_impl(aoahid_spec* spec) {
    aoa::detail::clear_error();
    if (!aoa::detail::release_spec(spec)) {
        set_error(AOAHID_ERR_PARAM, "spec.release",
                  "The spec handle is null or has no owned reference to release.");
    }
}

static aoahid_result aoahid_spec_descriptor_impl(const aoahid_spec* spec,
                                                 const std::uint8_t** bytes, std::size_t* length) {
    aoa::detail::clear_error();
    if (bytes != nullptr) {
        *bytes = nullptr;
    }
    if (length != nullptr) {
        *length = 0U;
    }
    if (spec == nullptr || bytes == nullptr || length == nullptr) {
        set_error(AOAHID_ERR_PARAM, "spec", "A required pointer is null.");
        return AOAHID_ERR_PARAM;
    }
    *bytes = spec->descriptor.data();
    *length = spec->descriptor.size();
    aoa::detail::clear_error();
    return AOAHID_OK;
}

static aoahid_result aoahid_spec_manifest_impl(const aoahid_spec* spec,
                                               aoahid_capability_manifest* manifest) {
    aoa::detail::clear_error();
    if (spec == nullptr || manifest == nullptr || manifest->struct_size != sizeof(*manifest) ||
        manifest->reserved != 0U) {
        set_error(AOAHID_ERR_PARAM, "manifest",
                  "The manifest pointer, struct_size, or reserved field is invalid.");
        return AOAHID_ERR_PARAM;
    }
    manifest->input_supported = 1U;
    manifest->output_supported = 0U;
    manifest->feature_transport_supported = 0U;
    manifest->android_status = spec->android_status;
    manifest->profile_kind = spec->kind;
    manifest->reports = spec->report_capabilities.data();
    manifest->report_count = spec->report_capabilities.size();
    manifest->descriptor_length = spec->descriptor.size();
    aoa::detail::clear_error();
    return AOAHID_OK;
}

void AOAHID_CALL aoahid_spec_retain(aoahid_spec* spec) {
    abi_void("spec.retain", [&] { aoahid_spec_retain_impl(spec); });
}

void AOAHID_CALL aoahid_spec_release(aoahid_spec* spec) {
    abi_void("spec.release", [&] { aoahid_spec_release_impl(spec); });
}

aoahid_result AOAHID_CALL aoahid_spec_descriptor(const aoahid_spec* spec,
                                                 const std::uint8_t** bytes, std::size_t* length) {
    return abi_result("spec.descriptor",
                      [&] { return aoahid_spec_descriptor_impl(spec, bytes, length); });
}

aoahid_result AOAHID_CALL aoahid_spec_manifest(const aoahid_spec* spec,
                                               aoahid_capability_manifest* manifest) {
    return abi_result("spec.manifest", [&] { return aoahid_spec_manifest_impl(spec, manifest); });
}

} // extern "C"
