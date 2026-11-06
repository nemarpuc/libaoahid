// SPDX-License-Identifier: MIT
// Copyright (c) 2026 libaoahid contributors
/*
 * Public C ABI: Channels, the bulk byte streams of a Device.
 */

#include "api/support.hpp"

#include <algorithm>
#include <chrono>
#include <memory>
#include <new>
#include <thread>

using aoa::detail::finish_transport_result;
using aoa::detail::preflight_context;
using aoa::detail::release_channel_storage;
using aoa::detail::remaining_milliseconds;
using aoa::detail::set_error;
using aoa::detail::timeout_is_set;
using aoa::detail::wait_for_report_drain;

extern "C" {

aoahid_result AOAHID_CALL aoahid_channel_open(aoahid_device* device,
                                              const aoahid_channel_options* options,
                                              const std::uint32_t drain_timeout_ms,
                                              aoahid_channel** out_channel) try {
    AOAHID_SERIALIZED(aoa::detail::serialization_context(device), "aoahid_channel_open");
    aoa::detail::clear_error();
    if (out_channel == nullptr) {
        set_error(AOAHID_ERR_PARAM, "out_channel", "The output pointer is null.");
        return AOAHID_ERR_PARAM;
    }
    *out_channel = nullptr;
    if (!timeout_is_set(drain_timeout_ms, "drain_timeout_ms")) {
        return AOAHID_ERR_UNSET_FIELD;
    }
    if (device == nullptr || device->transport == nullptr || device->context == nullptr) {
        set_error(AOAHID_ERR_PARAM, "channel.open", "A live Device is required.");
        return AOAHID_ERR_PARAM;
    }
    if (device->closing.load(std::memory_order_acquire)) {
        set_error(AOAHID_ERR_PARAM, "device",
                  "The Device handle was consumed by close and cannot open a Channel.");
        return AOAHID_ERR_PARAM;
    }
    const aoahid_result deferred = preflight_context(device->context);
    if (deferred != AOAHID_OK)
        return deferred;
    if (options == nullptr ||
        !aoa::detail::valid_struct(options, options->struct_size,
                                   static_cast<std::uint32_t>(sizeof(*options)),
                                   "channel_options")) {
        return AOAHID_ERR_PARAM;
    }
    if (options->reserved != 0U || options->reserved8 != 0U ||
        !aoa::detail::valid_boolean(options->zero_length_termination) ||
        (options->read_mode != AOAHID_CHANNEL_READ_STREAM &&
         options->read_mode != AOAHID_CHANNEL_READ_REQUEST) ||
        options->out_transfers == 0U || options->transfer_bytes == 0U ||
        (options->read_mode == AOAHID_CHANNEL_READ_STREAM && options->in_transfers == 0U)) {
        set_error(AOAHID_ERR_UNSET_FIELD, "channel_options",
                  "Reserved fields must be zero, zero_length_termination exactly zero or one, "
                  "read_mode STREAM or REQUEST, and out_transfers, transfer_bytes and (for "
                  "STREAM) in_transfers nonzero.");
        return AOAHID_ERR_UNSET_FIELD;
    }
    aoa::transport::ChannelConfig config{};
    config.interface_class = options->interface_class;
    config.interface_subclass = options->interface_subclass;
    config.interface_protocol = options->interface_protocol;
    config.in_transfers = options->in_transfers;
    config.out_transfers = options->out_transfers;
    config.transfer_bytes = options->transfer_bytes;
    config.zero_length_termination = options->zero_length_termination == 1U;
    config.request_reads = options->read_mode == AOAHID_CHANNEL_READ_REQUEST;
    config.pump = device->context->options.event_mode == AOAHID_EVENT_CALLER_POLL
                      ? device->context->runtime
                      : nullptr;

    auto channel = std::unique_ptr<aoahid_channel>(new (std::nothrow) aoahid_channel());
    if (channel == nullptr) {
        set_error(AOAHID_ERR_INTERNAL, "channel", "Memory allocation for the Channel failed.");
        return AOAHID_ERR_INTERNAL;
    }
    // Reserve first so publishing an opened Channel cannot fail.
    device->channels.reserve(device->channels.size() + 1U);
    aoahid_result poll = AOAHID_OK;
    if (!wait_for_report_drain(
            device, std::chrono::steady_clock::now() + std::chrono::milliseconds(drain_timeout_ms),
            &poll)) {
        if (poll != AOAHID_OK) {
            return finish_transport_result(poll, "context.poll", "libusb event handling failed.");
        }
        set_error(AOAHID_ERR_TIMEOUT, "channel.open",
                  "In-flight report transfers did not complete within the close drain budget.");
        return AOAHID_ERR_TIMEOUT;
    }
    const aoahid_result opened =
        aoa::transport::Channel::open(device->transport->port(), config, &channel->transport);
    if (opened != AOAHID_OK) {
        return finish_transport_result(
            opened, "channel.open",
            opened == AOAHID_ERR_UNSUPPORTED
                ? "No interface has the requested class triple and a Bulk IN/OUT pair."
                : "The Bulk interface could not be read, claimed, or started.");
    }
    channel->device = device;
    device->channels.push_back(channel.get());
    *out_channel = channel.release();
    aoa::detail::log_event(device->context, AOAHID_LOG_INFO, "channel_open", device, nullptr);
    aoa::detail::clear_error();
    return AOAHID_OK;
}
AOAHID_C_RESULT_CATCH("channel.open")

aoahid_result AOAHID_CALL aoahid_channel_close(aoahid_channel* channel,
                                               const std::uint32_t drain_timeout_ms) try {
    AOAHID_SERIALIZED(aoa::detail::serialization_context(channel), "aoahid_channel_close");
    aoa::detail::clear_error();
    if (channel != nullptr && !timeout_is_set(drain_timeout_ms, "drain_timeout_ms")) {
        return AOAHID_ERR_UNSET_FIELD;
    }
    if (channel == nullptr || channel->device == nullptr || channel->transport == nullptr ||
        channel->device->closing.load(std::memory_order_acquire)) {
        set_error(AOAHID_ERR_PARAM, "channel.close",
                  "The Channel handle is null, detached, or owned by a closing Device.");
        return AOAHID_ERR_PARAM;
    }
    aoahid_device* device = channel->device;
    channel->transport->lose();
    const auto deadline =
        std::chrono::steady_clock::now() + std::chrono::milliseconds(drain_timeout_ms);
    while (!channel->transport->drained()) {
        const std::uint32_t remaining = remaining_milliseconds(deadline);
        if (remaining == 0U) {
            set_error(AOAHID_CLOSE_PENDING, "channel.close",
                      "Cancelled Bulk transfers did not complete within the close drain budget; "
                      "the lost Channel remains caller-owned.");
            return AOAHID_CLOSE_PENDING;
        }
        if (device->context->options.event_mode == AOAHID_EVENT_CALLER_POLL) {
            if (device->context->runtime->poll(1U) != AOAHID_OK) {
                set_error(AOAHID_CLOSE_PENDING, "channel.close",
                          "libusb event handling failed before the cancelled Bulk transfers "
                          "completed; the Channel remains caller-owned.",
                          aoa::transport::last_error().native_status);
                return AOAHID_CLOSE_PENDING;
            }
        } else {
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
    }
    // A report still in flight at the deadline does not keep the Channel open.
    aoahid_result poll = AOAHID_OK;
    static_cast<void>(wait_for_report_drain(device, deadline, &poll));
    auto& channels = device->channels;
    channels.erase(std::remove(channels.begin(), channels.end(), channel), channels.end());
    release_channel_storage(channel);
    aoa::detail::log_event(device->context, AOAHID_LOG_INFO, "channel_close", device, nullptr);
    return finish_transport_result(poll, "context.poll", "libusb event handling failed.");
}
AOAHID_C_RESULT_CATCH("channel.close")

aoahid_result AOAHID_CALL aoahid_channel_write(aoahid_channel* channel, const std::uint8_t* data,
                                               const std::size_t length, std::size_t* out_written,
                                               const std::uint32_t timeout_ms) try {
    AOAHID_SERIALIZED(aoa::detail::serialization_context_caller_poll(channel),
                      "aoahid_channel_write");
    aoa::detail::clear_error();
    if (channel == nullptr || channel->transport == nullptr) {
        if (out_written != nullptr)
            *out_written = 0U;
        set_error(AOAHID_ERR_PARAM, "channel.write", "A live Channel is required.");
        return AOAHID_ERR_PARAM;
    }
    const aoahid_result result = channel->transport->write(data, length, out_written, timeout_ms);
    if (result != AOAHID_OK) {
        return finish_transport_result(
            result, "channel.write",
            result == AOAHID_ERR_TIMEOUT ? "Every OUT transfer stayed in flight until the timeout."
                                         : "The Bulk OUT path failed or the Channel is lost.");
    }
    return AOAHID_OK;
}
AOAHID_C_RESULT_CATCH("channel.write")

aoahid_result AOAHID_CALL aoahid_channel_read(aoahid_channel* channel, std::uint8_t* buffer,
                                              const std::size_t capacity, std::size_t* out_received,
                                              const std::uint32_t timeout_ms) try {
    AOAHID_SERIALIZED(aoa::detail::serialization_context_caller_poll(channel),
                      "aoahid_channel_read");
    aoa::detail::clear_error();
    if (channel == nullptr || channel->transport == nullptr) {
        if (out_received != nullptr)
            *out_received = 0U;
        set_error(AOAHID_ERR_PARAM, "channel.read", "A live Channel is required.");
        return AOAHID_ERR_PARAM;
    }
    const aoahid_result result =
        channel->transport->read(buffer, capacity, out_received, timeout_ms);
    if (result != AOAHID_OK) {
        return finish_transport_result(result, "channel.read",
                                       result == AOAHID_ERR_TIMEOUT
                                           ? "No Bulk IN data arrived before the timeout."
                                           : "The Bulk IN path failed or the Channel is lost.");
    }
    return AOAHID_OK;
}
AOAHID_C_RESULT_CATCH("channel.read")

} // extern "C"
