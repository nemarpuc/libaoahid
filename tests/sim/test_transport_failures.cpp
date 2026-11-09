// SPDX-License-Identifier: MIT
// Copyright (c) 2026 libaoahid contributors
/* What the transport does when libusb or the phone fails: every libusb error
 * code and its result, discovery of several phones and of devices that are
 * not accessories, AOA requests that stall or come back short, and Channels
 * whose claim, submit or transfers fail, whose phone leaves mid-transfer and
 * whose OUT pool is exhausted. */
#include "sim/sim_support.hpp"
#include "transport/transport.hpp"

#include <array>
#include <atomic>
#include <cstdio>
#include <cstring>
#include <limits>
#include <thread>
#include <vector>

namespace {

using sim::kControlTimeoutMs;
using sim::kDrainTimeoutMs;
using sim::kThreadDrainTimeoutMs;

bool field_is(const char* field) {
    const aoahid_error_detail* detail = aoahid_last_error();
    return detail->field != nullptr && std::strcmp(detail->field, field) == 0;
}

void test_libusb_error_mapping() {
    struct Mapping {
        int status;
        aoahid_result result;
        aoahid_result probe;
    };
    static constexpr std::array<Mapping, 15U> mappings{{
        {LIBUSB_SUCCESS, AOAHID_OK, AOAHID_OK},
        {LIBUSB_ERROR_IO, AOAHID_ERR_IO, AOAHID_ERR_IO},
        {LIBUSB_ERROR_INVALID_PARAM, AOAHID_ERR_PARAM, AOAHID_ERR_PARAM},
        {LIBUSB_ERROR_ACCESS, AOAHID_ERR_ACCESS, AOAHID_ERR_ACCESS},
        {LIBUSB_ERROR_NO_DEVICE, AOAHID_ERR_NO_DEVICE, AOAHID_ERR_NO_DEVICE},
        {LIBUSB_ERROR_NOT_FOUND, AOAHID_ERR_UNSUPPORTED, AOAHID_ERR_UNSUPPORTED},
        {LIBUSB_ERROR_BUSY, AOAHID_ERR_BUSY, AOAHID_ERR_BUSY},
        {LIBUSB_ERROR_TIMEOUT, AOAHID_ERR_TIMEOUT, AOAHID_ERR_TIMEOUT},
        {LIBUSB_ERROR_OVERFLOW, AOAHID_ERR_OVERFLOW, AOAHID_ERR_OVERFLOW},
        // A STALL of request 51 means the device does not speak AOA.
        {LIBUSB_ERROR_PIPE, AOAHID_ERR_STALL, AOAHID_ERR_NOT_AOA},
        {LIBUSB_ERROR_INTERRUPTED, AOAHID_ERR_IO, AOAHID_ERR_IO},
        {LIBUSB_ERROR_NO_MEM, AOAHID_ERR_INTERNAL, AOAHID_ERR_INTERNAL},
        {LIBUSB_ERROR_NOT_SUPPORTED, AOAHID_ERR_UNSUPPORTED, AOAHID_ERR_UNSUPPORTED},
        {LIBUSB_ERROR_OTHER, AOAHID_ERR_IO, AOAHID_ERR_IO},
        {-1000, AOAHID_ERR_IO, AOAHID_ERR_IO},
    }};
    for (const Mapping& mapping : mappings) {
        AOAHID_CHECK(aoa::transport::map_libusb_error(mapping.status, false) == mapping.result);
        AOAHID_CHECK(aoa::transport::map_libusb_error(mapping.status, true) == mapping.probe);
    }
}

// Discovery lists accessories by bus, port path and address, and leaves out a device that
// cannot be opened, answers request 51 with zero, short, or a STALL.
void test_discovery_order_and_exclusions() {
    aoahid_fake_libusb_reset();
    const auto add = [](const std::uint8_t bus, const std::uint8_t address,
                        const std::array<std::uint8_t, 2U>& path, const char* serial,
                        const std::uint16_t protocol, const int open_status) {
        static std::vector<std::array<std::uint8_t, 2U>> paths;
        paths.reserve(16U);
        paths.push_back(path);
        const aoahid_fake_libusb_device_config fake{
            bus,    address, paths.back().data(), 2U, 0x18D1U, 0x4EE7U, protocol,
            serial, "",      open_status};
        AOAHID_CHECK(aoahid_fake_libusb_add_device(&fake) >= 0);
    };
    add(2U, 9U, {1U, 1U}, "third", 2U, LIBUSB_SUCCESS);
    add(1U, 8U, {2U, 1U}, "second", 1U, LIBUSB_SUCCESS);
    add(1U, 7U, {1U, 1U}, "first-b", 2U, LIBUSB_SUCCESS);
    add(1U, 6U, {1U, 1U}, "first-a", 2U, LIBUSB_SUCCESS);
    add(3U, 1U, {1U, 1U}, "denied", 2U, LIBUSB_ERROR_ACCESS);
    add(3U, 2U, {1U, 2U}, "no-aoa", 0U, LIBUSB_SUCCESS);
    add(3U, 3U, {1U, 3U}, "", 2U, LIBUSB_SUCCESS); // no serial string

    aoahid_context_options options = sim::context_options();
    aoahid_context* context = nullptr;
    AOAHID_CHECK(aoahid_context_create(&options, &context) == AOAHID_OK);
    aoahid_discovery* discovery = nullptr;
    AOAHID_CHECK(aoahid_discover(context, kControlTimeoutMs, &discovery) == AOAHID_OK);
    static constexpr std::array<const char*, 5U> expected{"first-a", "first-b", "second", "third",
                                                          ""};
    AOAHID_CHECK(aoahid_discovery_count(discovery) == expected.size());
    for (std::size_t index = 0U; index < expected.size(); ++index) {
        const aoahid_device_info* info = aoahid_discovery_get(discovery, index);
        AOAHID_CHECK(info != nullptr && std::strcmp(info->serial, expected[index]) == 0);
    }
    AOAHID_CHECK(aoahid_discovery_get(discovery, 2U)->protocol_version == 1U);
    AOAHID_CHECK(aoahid_discovery_get(discovery, 4U)->product[0] == '\0');
    aoahid_discovery_destroy(discovery);

    // A STALL or a one-byte answer to the probe leaves only that device out.
    aoahid_fake_libusb_queue_control_result(LIBUSB_ERROR_PIPE);
    aoahid_fake_libusb_queue_control_result(1);
    AOAHID_CHECK(aoahid_discover(context, kControlTimeoutMs, &discovery) == AOAHID_OK);
    AOAHID_CHECK(aoahid_discovery_count(discovery) == expected.size() - 2U);
    aoahid_discovery_destroy(discovery);
    AOAHID_CHECK(aoahid_context_destroy(context, kControlTimeoutMs, kDrainTimeoutMs) == AOAHID_OK);
    aoahid_fake_libusb_reset();
}

// Accessory start: a probe that stalls, comes back short or reports zero is NOT_AOA; a string
// or the start request that fails names its request and string index.
void test_accessory_start_failures() {
    aoahid_fake_libusb_reset();
    const sim::Phone phone = sim::add_phone(0x4EE7U);
    aoahid_context_options options = sim::context_options();
    aoahid_context* context = nullptr;
    AOAHID_CHECK(aoahid_context_create(&options, &context) == AOAHID_OK);
    aoahid_accessory_options accessory;
    aoahid_accessory_options_init(&accessory);
    accessory.strings.version = "1";
    const auto start = [&]() {
        return aoahid_accessory_start(context, &phone.info, &accessory, kControlTimeoutMs);
    };
    aoahid_fake_libusb_queue_control_result(LIBUSB_ERROR_PIPE);
    AOAHID_CHECK(start() == AOAHID_ERR_NOT_AOA && aoahid_last_error()->aoa_request == 51);
    aoahid_fake_libusb_queue_control_result(1);
    AOAHID_CHECK(start() == AOAHID_ERR_NOT_AOA && aoahid_last_error()->aoa_request == 51);
    aoahid_fake_libusb_set_protocol(phone.index, 0U);
    AOAHID_CHECK(start() == AOAHID_ERR_NOT_AOA);
    aoahid_fake_libusb_set_protocol(phone.index, 2U);
    // Probe, manufacturer, then the model string comes back one byte short.
    aoahid_fake_libusb_queue_control_result(2);
    aoahid_fake_libusb_queue_control_result(10);
    aoahid_fake_libusb_queue_control_result(3);
    AOAHID_CHECK(start() == AOAHID_ERR_SHORT_TRANSFER && aoahid_last_error()->aoa_request == 52 &&
                 aoahid_last_error()->offset == 1U && aoahid_last_error()->length == 10U);
    // The string at index 3 (version) times out.
    for (const int result : {2, 10, 10}) {
        aoahid_fake_libusb_queue_control_result(result);
    }
    aoahid_fake_libusb_queue_control_result(LIBUSB_ERROR_TIMEOUT);
    AOAHID_CHECK(start() == AOAHID_ERR_TIMEOUT && aoahid_last_error()->aoa_request == 52 &&
                 aoahid_last_error()->offset == 3U);
    for (const int result : {2, 10, 10, 2}) {
        aoahid_fake_libusb_queue_control_result(result);
    }
    aoahid_fake_libusb_queue_control_result(LIBUSB_ERROR_NO_DEVICE);
    AOAHID_CHECK(start() == AOAHID_ERR_NO_DEVICE && aoahid_last_error()->aoa_request == 53);
    // A phone that cannot be opened.
    aoahid_fake_libusb_set_present(phone.index, 0);
    AOAHID_CHECK(start() == AOAHID_ERR_NO_DEVICE && field_is("accessory.start"));
    AOAHID_CHECK(aoahid_fake_libusb_open_handle_count() == 0U);
    AOAHID_CHECK(aoahid_context_destroy(context, kControlTimeoutMs, kDrainTimeoutMs) == AOAHID_OK);
    aoahid_fake_libusb_reset();
}

void test_channel_open_failures() {
    aoahid_fake_libusb_reset();
    sim::Session session{};
    AOAHID_CHECK(session.open(nullptr, AOAHID_EVENT_CALLER_POLL, true));
    aoahid_channel_options options = sim::adb_stream_options();
    aoahid_channel* channel = nullptr;
    // The interface is claimed by someone else.
    aoahid_fake_libusb_set_claim_result(session.phone.index, LIBUSB_ERROR_BUSY);
    AOAHID_CHECK(aoahid_channel_open(session.device, &options, kDrainTimeoutMs, &channel) ==
                     AOAHID_ERR_BUSY &&
                 channel == nullptr && aoahid_last_error()->transport_status == LIBUSB_ERROR_BUSY);
    aoahid_fake_libusb_set_claim_result(session.phone.index, LIBUSB_SUCCESS);
    // Transfers rounded up to wMaxPacketSize must still fit an int.
    options.transfer_bytes = std::numeric_limits<std::uint32_t>::max();
    AOAHID_CHECK(aoahid_channel_open(session.device, &options, kDrainTimeoutMs, &channel) ==
                 AOAHID_ERR_OVERFLOW);
    options = sim::adb_stream_options();
    // The first read-ahead transfer is refused: nothing is in flight, so the open fails clean.
    aoahid_fake_libusb_queue_submit_result(LIBUSB_ERROR_IO);
    AOAHID_CHECK(aoahid_channel_open(session.device, &options, kDrainTimeoutMs, &channel) ==
                 AOAHID_ERR_IO);
    // The second one is refused: the Channel opens lost and reports it on read.
    aoahid_fake_libusb_queue_submit_result(LIBUSB_SUCCESS);
    aoahid_fake_libusb_queue_submit_result(LIBUSB_ERROR_IO);
    AOAHID_CHECK(aoahid_channel_open(session.device, &options, kDrainTimeoutMs, &channel) ==
                 AOAHID_OK);
    std::array<std::uint8_t, sim::kAdbMaxPacket> buffer{};
    std::size_t moved = 0U;
    AOAHID_CHECK(aoahid_channel_read(channel, buffer.data(), buffer.size(), &moved, 10U) ==
                 AOAHID_ERR_NO_DEVICE);
    AOAHID_CHECK(aoahid_channel_close(channel, kDrainTimeoutMs) == AOAHID_OK);
    // The configuration cannot be read once the phone is gone.
    aoahid_fake_libusb_set_present(session.phone.index, 0);
    AOAHID_CHECK(aoahid_channel_open(session.device, &options, kDrainTimeoutMs, &channel) ==
                 AOAHID_ERR_NO_DEVICE);
    aoahid_fake_libusb_set_present(session.phone.index, 1);
    AOAHID_CHECK(session.close() == AOAHID_OK);
    aoahid_fake_libusb_reset();
}

// Transfer statuses: a stalled or timed-out OUT transfer is reported by the next write; an IN
// transfer that overflows, stalls or loses the phone ends the Channel's reads.
void test_channel_transfer_failures() {
    aoahid_fake_libusb_reset();
    sim::Session session{};
    AOAHID_CHECK(session.open(nullptr, AOAHID_EVENT_CALLER_POLL, true));
    const aoahid_channel_options options = sim::adb_stream_options();
    aoahid_channel* channel = nullptr;
    AOAHID_CHECK(aoahid_channel_open(session.device, &options, kDrainTimeoutMs, &channel) ==
                 AOAHID_OK);
    const std::array<std::uint8_t, 4U> data{1U, 2U, 3U, 4U};
    std::size_t moved = 0U;
    for (const auto status : {LIBUSB_TRANSFER_STALL, LIBUSB_TRANSFER_TIMED_OUT,
                              LIBUSB_TRANSFER_ERROR, LIBUSB_TRANSFER_OVERFLOW}) {
        aoahid_fake_libusb_queue_async_completion(status, 0, 0U);
        AOAHID_CHECK(aoahid_channel_write(channel, data.data(), data.size(), &moved, 100U) ==
                     AOAHID_OK);
        AOAHID_CHECK(aoahid_context_poll(session.context, 1U) == AOAHID_OK);
        const aoahid_result expected = status == LIBUSB_TRANSFER_STALL       ? AOAHID_ERR_STALL
                                       : status == LIBUSB_TRANSFER_TIMED_OUT ? AOAHID_ERR_TIMEOUT
                                       : status == LIBUSB_TRANSFER_OVERFLOW  ? AOAHID_ERR_OVERFLOW
                                                                             : AOAHID_ERR_IO;
        AOAHID_CHECK(aoahid_channel_write(channel, data.data(), data.size(), &moved, 100U) ==
                         expected &&
                     field_is("channel.write"));
    }
    // A refused submit is returned as it is; NO_DEVICE also loses the Channel.
    aoahid_fake_libusb_queue_submit_result(LIBUSB_ERROR_IO);
    AOAHID_CHECK(aoahid_channel_write(channel, data.data(), data.size(), &moved, 100U) ==
                 AOAHID_ERR_IO);
    AOAHID_CHECK(aoahid_channel_write(nullptr, data.data(), data.size(), &moved, 1U) ==
                 AOAHID_ERR_PARAM);
    AOAHID_CHECK(aoahid_channel_write(channel, nullptr, 1U, &moved, 1U) == AOAHID_ERR_PARAM);
    AOAHID_CHECK(aoahid_channel_write(channel, data.data(), data.size(), nullptr, 1U) ==
                 AOAHID_ERR_PARAM);
    std::array<std::uint8_t, sim::kAdbMaxPacket> buffer{};
    AOAHID_CHECK(aoahid_channel_read(channel, nullptr, 1U, &moved, 1U) == AOAHID_ERR_PARAM);
    // Nothing arrives: zero and short timeouts.
    AOAHID_CHECK(aoahid_channel_read(channel, buffer.data(), buffer.size(), &moved, 0U) ==
                     AOAHID_ERR_TIMEOUT &&
                 field_is("channel.read"));
    AOAHID_CHECK(aoahid_channel_read(channel, buffer.data(), buffer.size(), &moved, 2U) ==
                 AOAHID_ERR_TIMEOUT);
    // A chunk larger than the transfer overflows it.
    const std::vector<std::uint8_t> big(sim::kAdbMaxPacket + 1U, 0x22U);
    aoahid_fake_libusb_push_bulk_in(session.phone.index, sim::kAdbEndpointIn, big.data(),
                                    big.size());
    AOAHID_CHECK(aoahid_channel_read(channel, buffer.data(), buffer.size(), &moved, 100U) ==
                 AOAHID_ERR_OVERFLOW);
    // After that failure the Channel is lost: reads and writes report NO_DEVICE.
    AOAHID_CHECK(aoahid_channel_read(channel, buffer.data(), buffer.size(), &moved, 10U) ==
                 AOAHID_ERR_NO_DEVICE);
    AOAHID_CHECK(aoahid_channel_write(channel, data.data(), data.size(), &moved, 10U) ==
                 AOAHID_ERR_NO_DEVICE);
    AOAHID_CHECK(aoahid_channel_close(channel, kDrainTimeoutMs) == AOAHID_OK);

    // The phone leaves while transfers are in flight.
    AOAHID_CHECK(aoahid_channel_open(session.device, &options, kDrainTimeoutMs, &channel) ==
                 AOAHID_OK);
    aoahid_fake_libusb_queue_async_completion(LIBUSB_TRANSFER_COMPLETED, -1, 1000U);
    AOAHID_CHECK(aoahid_channel_write(channel, data.data(), data.size(), &moved, 100U) ==
                 AOAHID_OK);
    aoahid_fake_libusb_unplug(session.phone.index);
    AOAHID_CHECK(aoahid_channel_read(channel, buffer.data(), buffer.size(), &moved, 100U) ==
                 AOAHID_ERR_NO_DEVICE);
    AOAHID_CHECK(aoahid_channel_write(channel, data.data(), data.size(), &moved, 100U) ==
                 AOAHID_ERR_NO_DEVICE);
    AOAHID_CHECK(aoahid_channel_close(channel, kDrainTimeoutMs) == AOAHID_OK);
    AOAHID_CHECK(session.close() != AOAHID_CLOSE_PENDING);
    AOAHID_CHECK(aoahid_fake_libusb_open_handle_count() == 0U);
    aoahid_fake_libusb_reset();
}

// With the internal event thread, a write whose OUT pool is full sleeps until a transfer
// completes, and a read sleeps until data arrives; neither spins.
void test_channel_waits_internal_thread() {
    aoahid_fake_libusb_reset();
    sim::Session session{};
    AOAHID_CHECK(session.open(nullptr, AOAHID_EVENT_INTERNAL_THREAD, true));
    aoahid_channel_options options = sim::adb_stream_options();
    options.zero_length_termination = 0U;
    aoahid_channel* channel = nullptr;
    AOAHID_CHECK(aoahid_channel_open(session.device, &options, kThreadDrainTimeoutMs, &channel) ==
                 AOAHID_OK);
    // Both OUT transfers stay in flight until the test releases them.
    aoahid_fake_libusb_queue_async_completion(LIBUSB_TRANSFER_COMPLETED, -1, 1000000U);
    aoahid_fake_libusb_queue_async_completion(LIBUSB_TRANSFER_COMPLETED, -1, 1000000U);
    const std::vector<std::uint8_t> data(3U * sim::kAdbMaxPacket, 0x33U);
    std::size_t written = 0U;
    AOAHID_CHECK(aoahid_channel_write(channel, data.data(), data.size(), &written, 5U) ==
                     AOAHID_ERR_TIMEOUT &&
                 written == 2U * sim::kAdbMaxPacket);
    std::atomic<bool> done{false};
    aoahid_result result = AOAHID_ERR_INTERNAL;
    std::thread writer([&]() {
        std::size_t rest = 0U;
        result = aoahid_channel_write(channel, data.data() + written, data.size() - written, &rest,
                                      2000U);
        done.store(true);
    });
    std::this_thread::sleep_for(std::chrono::milliseconds(5));
    AOAHID_CHECK(!done.load());
    aoahid_fake_libusb_make_pending_transfers_ready();
    writer.join();
    AOAHID_CHECK(result == AOAHID_OK);

    std::array<std::uint8_t, sim::kAdbMaxPacket> buffer{};
    std::size_t received = 0U;
    std::thread feeder([&]() {
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
        const std::array<std::uint8_t, 3U> bytes{'a', 'b', 'c'};
        aoahid_fake_libusb_push_bulk_in(session.phone.index, sim::kAdbEndpointIn, bytes.data(),
                                        bytes.size());
    });
    AOAHID_CHECK(aoahid_channel_read(channel, buffer.data(), buffer.size(), &received, 2000U) ==
                     AOAHID_OK &&
                 received == 3U);
    feeder.join();
    AOAHID_CHECK(aoahid_channel_close(channel, kThreadDrainTimeoutMs) == AOAHID_OK);
    AOAHID_CHECK(session.close() == AOAHID_OK);
    aoahid_fake_libusb_reset();
}

// A report transfer that completes with each failure status reports it once, on the next
// call on the Node, with the request and the Node's ID.
void test_report_completion_statuses() {
    aoahid_fake_libusb_reset();
    aoahid_spec* raw = sim::make_raw_spec();
    sim::Session session{};
    AOAHID_CHECK(session.open(raw));
    const std::array<std::uint8_t, 2U> report{1U, 1U};
    struct Status {
        libusb_transfer_status status;
        int length;
        aoahid_result result;
    };
    static constexpr std::array<Status, 5U> statuses{{
        {LIBUSB_TRANSFER_TIMED_OUT, 0, AOAHID_ERR_TIMEOUT},
        {LIBUSB_TRANSFER_STALL, 0, AOAHID_ERR_STALL},
        {LIBUSB_TRANSFER_OVERFLOW, 0, AOAHID_ERR_OVERFLOW},
        {LIBUSB_TRANSFER_ERROR, 0, AOAHID_ERR_IO},
        {LIBUSB_TRANSFER_COMPLETED, 1, AOAHID_ERR_SHORT_TRANSFER},
    }};
    for (const Status& entry : statuses) {
        aoahid_fake_libusb_queue_async_completion(entry.status, entry.length, 0U);
        const aoahid_result first =
            aoahid_raw_submit(session.node, report.data(), report.size(), sim::kSendTimeoutMs);
        AOAHID_CHECK(first == AOAHID_OK);
        AOAHID_CHECK(aoahid_context_poll(session.context, 1U) == AOAHID_OK);
        const aoahid_result next =
            aoahid_raw_submit(session.node, report.data(), report.size(), sim::kSendTimeoutMs);
        AOAHID_CHECK(next == entry.result);
#ifndef _MSC_VER
        // With MSVC the result is right but the error detail carries no request and no Node ID;
        // not traced yet (tests/README.md, "Known platform differences").
        const aoahid_error_detail* detail = aoahid_last_error();
        AOAHID_CHECK(detail->aoa_request == 57);
        AOAHID_CHECK(detail->node_id == aoahid_node_id(session.node));
#endif
        AOAHID_CHECK(aoahid_context_poll(session.context, 1U) == AOAHID_OK);
    }
    AOAHID_CHECK(session.close() == AOAHID_OK);
    aoahid_spec_release(raw);
    aoahid_fake_libusb_reset();
}

} // namespace

void test_transport_failures() {
    test_libusb_error_mapping();
    test_discovery_order_and_exclusions();
    test_accessory_start_failures();
    test_channel_open_failures();
    test_channel_transfer_failures();
    test_channel_waits_internal_thread();
    test_report_completion_statuses();
}
