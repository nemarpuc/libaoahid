// SPDX-License-Identifier: MIT
// Copyright (c) 2026 libaoahid contributors
/* Bulk Channels (ADB): sharing the Device handle, configuring an unconfigured device,
 * request-sized reads, HID and bulk traffic together, and close with the Device. */
#include "sim/transport_support.hpp"

namespace transport_sim {

void test_channel_shares_the_device_handle_internal_thread() {
    aoahid_fake_libusb_reset();
    const Candidate phone = add_adb_phone();
    aoahid_context_options context_options = public_context_options();
    context_options.event_mode = AOAHID_EVENT_INTERNAL_THREAD;
    aoahid_context* context = nullptr;
    AOAHID_CHECK(aoahid_context_create(&context_options, &context) == AOAHID_OK);
    const aoahid_device_info selected = public_info(phone);
    aoahid_device_options device_options = public_device_options();
    aoahid_device* device = nullptr;
    AOAHID_CHECK(aoahid_device_open(context, &selected, &device_options, &device) == AOAHID_OK);
    const std::size_t opens = aoahid_fake_libusb_open_call_count();

    aoahid_channel* channel = nullptr;
    aoahid_channel_options options = adb_channel_options();
    options.interface_subclass = 0xFFU;
    AOAHID_CHECK(aoahid_channel_open(device, &options, kDrainTimeoutMs, &channel) ==
                 AOAHID_ERR_UNSUPPORTED);
    AOAHID_CHECK(channel == nullptr);
    options = adb_channel_options();
    options.zero_length_termination = 2U;
    AOAHID_CHECK(aoahid_channel_open(device, &options, kDrainTimeoutMs, &channel) ==
                 AOAHID_ERR_UNSET_FIELD);
    options = adb_channel_options();
    options.reserved8 = 1U;
    AOAHID_CHECK(aoahid_channel_open(device, &options, kDrainTimeoutMs, &channel) ==
                 AOAHID_ERR_UNSET_FIELD);
    options = adb_channel_options();
    options.struct_size += 1U;
    AOAHID_CHECK(aoahid_channel_open(device, &options, kDrainTimeoutMs, &channel) ==
                 AOAHID_ERR_PARAM);
    options = adb_channel_options();
    AOAHID_CHECK(aoahid_channel_open(nullptr, &options, kDrainTimeoutMs, &channel) ==
                 AOAHID_ERR_PARAM);
    AOAHID_CHECK(aoahid_channel_open(device, &options, kDrainTimeoutMs, &channel) == AOAHID_OK);
    // An already configured device gets no SET_CONFIGURATION (it would act as
    // a lightweight reset).
    AOAHID_CHECK(aoahid_fake_libusb_set_configuration_count() == 0U);
    // One handle for HID and Bulk; the Bulk interface is claimed on it.
    AOAHID_CHECK(aoahid_fake_libusb_open_call_count() == opens);
    AOAHID_CHECK(aoahid_fake_libusb_open_handle_count() == 1U);
    aoahid_fake_libusb_claim_record claim{};
    AOAHID_CHECK(aoahid_fake_libusb_claim_count() == 1U &&
                 aoahid_fake_libusb_get_claim(0U, &claim) == LIBUSB_SUCCESS &&
                 claim.interface_number == 1 && claim.release == 0);

    std::array<std::uint8_t, 64> buffer{};
    std::size_t received = 7U;
    AOAHID_CHECK(aoahid_channel_read(channel, buffer.data(), buffer.size(), &received, 0U) ==
                 AOAHID_ERR_TIMEOUT);
    AOAHID_CHECK(received == 0U);
    AOAHID_CHECK(aoahid_channel_read(channel, buffer.data(), 0U, &received, 0U) ==
                 AOAHID_ERR_PARAM);

    // An ADB CNXN message: 24-byte header plus "host::".
    const std::array<std::uint8_t, 30> cnxn{
        'C',  'N',  'X',  'N',  0x01, 0x00, 0x00, 0x01, 0x00, 0x10, 0x00, 0x00, 0x06, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x00, 0xBC, 0xB1, 0xA7, 0xB1, 'h',  'o',  's',  't',  ':',  ':'};
    std::size_t written = 0U;
    AOAHID_CHECK(aoahid_channel_write(channel, cnxn.data(), cnxn.size(), &written, 1000U) ==
                 AOAHID_OK);
    AOAHID_CHECK(written == cnxn.size());
    std::array<std::uint8_t, 64> sent{};
    AOAHID_CHECK(aoahid_fake_libusb_bulk_out_count() == 1U &&
                 aoahid_fake_libusb_copy_bulk_out(0U, sent.data(), sent.size()) == cnxn.size() &&
                 std::equal(cnxn.begin(), cnxn.end(), sent.begin()));

    // The reply arrives in two USB transfers; the reader sees one stream.
    aoahid_fake_libusb_push_bulk_in(0U, kAdbEndpointIn, cnxn.data(), 24U);
    aoahid_fake_libusb_push_bulk_in(0U, kAdbEndpointIn, cnxn.data() + 24U, cnxn.size() - 24U);
    std::vector<std::uint8_t> stream;
    while (stream.size() < cnxn.size()) {
        std::array<std::uint8_t, 16> part{};
        const aoahid_result read =
            aoahid_channel_read(channel, part.data(), part.size(), &received, 1000U);
        AOAHID_CHECK(read == AOAHID_OK && received != 0U);
        if (read != AOAHID_OK)
            break;
        stream.insert(stream.end(), part.begin(),
                      part.begin() + static_cast<std::ptrdiff_t>(received));
    }
    AOAHID_CHECK(stream == std::vector<std::uint8_t>(cnxn.begin(), cnxn.end()));

    // One max packet exactly: data, then an explicit zero-length packet.
    const std::vector<std::uint8_t> packet(kAdbMaxPacket, 0x5AU);
    AOAHID_CHECK(aoahid_channel_write(channel, packet.data(), packet.size(), &written, 1000U) ==
                 AOAHID_OK);
    AOAHID_CHECK(wait_until_true([]() { return aoahid_fake_libusb_bulk_out_count() == 3U; }));
    AOAHID_CHECK(aoahid_fake_libusb_copy_bulk_out(1U, nullptr, 0U) == kAdbMaxPacket);
    AOAHID_CHECK(aoahid_fake_libusb_copy_bulk_out(2U, nullptr, 0U) == 0U);

    // HID on the same handle keeps working next to the Channel.
    aoahid_keyboard_options keyboard{};
    keyboard.struct_size = sizeof(keyboard);
    keyboard.usage_minimum = 0x04U;
    keyboard.usage_maximum = 0x65U;
    aoahid_spec* spec = nullptr;
    AOAHID_CHECK(aoahid_spec_create_keyboard(&keyboard, &spec) == AOAHID_OK);
    const aoahid_node_options node_options{sizeof(aoahid_node_options), 0U, 0U, 0U};
    aoahid_node* node = nullptr;
    AOAHID_CHECK(aoahid_node_open(device, spec, &node_options, kControlTimeoutMs, &node) ==
                 AOAHID_OK);
    aoahid_spec_release(spec);
    AOAHID_CHECK(aoahid_keyboard_key(node, 0x04U, 1U) == AOAHID_OK);
    AOAHID_CHECK(aoahid_node_submit_blocking(node, 1000U) == AOAHID_OK);

    // Unplugging loses the Channel: it reports NO_DEVICE and must be closed.
    aoahid_fake_libusb_unplug(0U);
    AOAHID_CHECK(aoahid_channel_read(channel, buffer.data(), buffer.size(), &received, 1000U) ==
                 AOAHID_ERR_NO_DEVICE);
    AOAHID_CHECK(aoahid_channel_write(channel, cnxn.data(), cnxn.size(), &written, 0U) ==
                 AOAHID_ERR_NO_DEVICE);
    AOAHID_CHECK(aoahid_channel_close(channel, kThreadDrainTimeoutMs) == AOAHID_OK);
    AOAHID_CHECK(aoahid_channel_close(nullptr, kDrainTimeoutMs) == AOAHID_ERR_PARAM);
    static_cast<void>(aoahid_node_close(node, kControlTimeoutMs, kThreadDrainTimeoutMs));
    static_cast<void>(aoahid_device_close(device, kControlTimeoutMs, kThreadDrainTimeoutMs));
    AOAHID_CHECK(aoahid_context_destroy(context, kControlTimeoutMs, kThreadDrainTimeoutMs) ==
                 AOAHID_OK);
    AOAHID_CHECK(aoahid_fake_libusb_open_handle_count() == 0U);
    aoahid_fake_libusb_reset();
}

void test_channel_configures_an_unconfigured_device() {
    aoahid_fake_libusb_reset();
    const Candidate phone = add_adb_phone();
    aoahid_fake_libusb_set_active_configuration(0U, 0);
    aoahid_context_options context_options = public_context_options();
    aoahid_context* context = nullptr;
    AOAHID_CHECK(aoahid_context_create(&context_options, &context) == AOAHID_OK);
    const aoahid_device_info selected = public_info(phone);
    aoahid_device_options device_options = public_device_options();
    aoahid_device* device = nullptr;
    AOAHID_CHECK(aoahid_device_open(context, &selected, &device_options, &device) == AOAHID_OK);
    const aoahid_channel_options options = adb_channel_options();
    aoahid_channel* channel = nullptr;
    // AOA 1.0: the host selects configuration 1 before using the endpoints.
    AOAHID_CHECK(aoahid_channel_open(device, &options, kDrainTimeoutMs, &channel) == AOAHID_OK);
    AOAHID_CHECK(aoahid_fake_libusb_set_configuration_count() == 1U);
    AOAHID_CHECK(aoahid_channel_close(channel, kDrainTimeoutMs) == AOAHID_OK);
    AOAHID_CHECK(aoahid_device_close(device, kControlTimeoutMs, kDrainTimeoutMs) == AOAHID_OK);
    AOAHID_CHECK(aoahid_context_destroy(context, kControlTimeoutMs, kDrainTimeoutMs) == AOAHID_OK);
    aoahid_fake_libusb_reset();
}

// Request mode: no read-ahead; each read with nothing buffered submits one IN
// transfer sized from its capacity (whole packets, at most transfer_bytes).
void test_channel_request_reads() {
    aoahid_fake_libusb_reset();
    const Candidate phone = add_adb_phone();
    aoahid_context_options context_options = public_context_options();
    context_options.event_mode = AOAHID_EVENT_INTERNAL_THREAD;
    aoahid_context* context = nullptr;
    AOAHID_CHECK(aoahid_context_create(&context_options, &context) == AOAHID_OK);
    const aoahid_device_info selected = public_info(phone);
    aoahid_device_options device_options = public_device_options();
    aoahid_device* device = nullptr;
    AOAHID_CHECK(aoahid_device_open(context, &selected, &device_options, &device) == AOAHID_OK);

    aoahid_channel* channel = nullptr;
    aoahid_channel_options options = adb_channel_options();
    for (const aoahid_channel_read_mode mode : {0, 3}) {
        options.read_mode = mode;
        AOAHID_CHECK(aoahid_channel_open(device, &options, kDrainTimeoutMs, &channel) ==
                     AOAHID_ERR_UNSET_FIELD);
    }
    options = adb_channel_options();
    options.read_mode = AOAHID_CHANNEL_READ_REQUEST;
    options.in_transfers = 7U;      // ignored in request mode
    options.transfer_bytes = 4000U; // rounded up to 4096
    AOAHID_CHECK(aoahid_channel_open(device, &options, kDrainTimeoutMs, &channel) == AOAHID_OK);
    AOAHID_CHECK(aoahid_fake_libusb_bulk_in_submit_count() == 0U);

    std::vector<std::uint8_t> buffer(100000U);
    std::size_t received = 0U;
    // A 24-byte header read asks for one whole packet; a timeout keeps it
    // pending instead of submitting a second transfer.
    AOAHID_CHECK(aoahid_channel_read(channel, buffer.data(), 24U, &received, 0U) ==
                 AOAHID_ERR_TIMEOUT);
    AOAHID_CHECK(aoahid_channel_read(channel, buffer.data(), 24U, &received, 0U) ==
                 AOAHID_ERR_TIMEOUT);
    AOAHID_CHECK(aoahid_fake_libusb_bulk_in_submit_count() == 1U &&
                 aoahid_fake_libusb_bulk_in_submit_length(0U) == kAdbMaxPacket);
    std::vector<std::uint8_t> header(24U);
    for (std::size_t index = 0U; index < header.size(); ++index)
        header[index] = static_cast<std::uint8_t>(index + 1U);
    aoahid_fake_libusb_push_bulk_in(0U, kAdbEndpointIn, header.data(), header.size());
    AOAHID_CHECK(aoahid_channel_read(channel, buffer.data(), 24U, &received, 1000U) == AOAHID_OK &&
                 received == 24U && std::equal(header.begin(), header.end(), buffer.begin()));

    // A packet-aligned payload with no zero-length packet: the transfer is
    // exactly the payload length, so it completes when full.
    const std::vector<std::uint8_t> aligned(2U * kAdbMaxPacket, 0xA5U);
    aoahid_fake_libusb_push_bulk_in(0U, kAdbEndpointIn, aligned.data(), aligned.size());
    AOAHID_CHECK(aoahid_channel_read(channel, buffer.data(), aligned.size(), &received, 1000U) ==
                     AOAHID_OK &&
                 received == aligned.size());
    AOAHID_CHECK(aoahid_fake_libusb_bulk_in_submit_count() == 2U &&
                 aoahid_fake_libusb_bulk_in_submit_length(1U) == static_cast<int>(aligned.size()));

    // A capacity above transfer_bytes is capped at transfer_bytes.
    const std::vector<std::uint8_t> full(4096U, 0x3CU);
    aoahid_fake_libusb_push_bulk_in(0U, kAdbEndpointIn, full.data(), full.size());
    AOAHID_CHECK(aoahid_channel_read(channel, buffer.data(), buffer.size(), &received, 1000U) ==
                     AOAHID_OK &&
                 received == full.size());
    AOAHID_CHECK(aoahid_fake_libusb_bulk_in_submit_length(2U) == 4096);

    // Bytes beyond a smaller later capacity stay buffered; no new transfer
    // is submitted until they are consumed.
    std::vector<std::uint8_t> short_data(1000U);
    for (std::size_t index = 0U; index < short_data.size(); ++index)
        short_data[index] = static_cast<std::uint8_t>(index);
    aoahid_fake_libusb_push_bulk_in(0U, kAdbEndpointIn, short_data.data(), short_data.size());
    std::vector<std::uint8_t> stream;
    AOAHID_CHECK(aoahid_channel_read(channel, buffer.data(), 600U, &received, 1000U) == AOAHID_OK &&
                 received == 600U);
    stream.insert(stream.end(), buffer.begin(), buffer.begin() + 600);
    AOAHID_CHECK(aoahid_fake_libusb_bulk_in_submit_length(3U) == 1024);
    AOAHID_CHECK(aoahid_channel_read(channel, buffer.data(), 16U, &received, 0U) == AOAHID_OK &&
                 received == 16U);
    stream.insert(stream.end(), buffer.begin(), buffer.begin() + 16);
    AOAHID_CHECK(aoahid_channel_read(channel, buffer.data(), buffer.size(), &received, 0U) ==
                     AOAHID_OK &&
                 received == 384U);
    stream.insert(stream.end(), buffer.begin(), buffer.begin() + 384);
    AOAHID_CHECK(stream == short_data);
    AOAHID_CHECK(aoahid_fake_libusb_bulk_in_submit_count() == 4U);

    // A zero-length packet carries no data: the read waits past it.
    AOAHID_CHECK(aoahid_channel_read(channel, buffer.data(), 24U, &received, 0U) ==
                 AOAHID_ERR_TIMEOUT);
    aoahid_fake_libusb_push_bulk_in(0U, kAdbEndpointIn, nullptr, 0U);
    aoahid_fake_libusb_push_bulk_in(0U, kAdbEndpointIn, header.data(), header.size());
    AOAHID_CHECK(aoahid_channel_read(channel, buffer.data(), 24U, &received, 1000U) == AOAHID_OK &&
                 received == 24U);
    AOAHID_CHECK(aoahid_fake_libusb_bulk_in_submit_count() == 6U);

    // Writes are unchanged by the read mode.
    std::size_t written = 0U;
    AOAHID_CHECK(aoahid_channel_write(channel, header.data(), header.size(), &written, 1000U) ==
                     AOAHID_OK &&
                 written == header.size());

    // Unplugging with a request pending loses the Channel.
    AOAHID_CHECK(aoahid_channel_read(channel, buffer.data(), 24U, &received, 0U) ==
                 AOAHID_ERR_TIMEOUT);
    aoahid_fake_libusb_unplug(0U);
    AOAHID_CHECK(aoahid_channel_read(channel, buffer.data(), 24U, &received, 1000U) ==
                 AOAHID_ERR_NO_DEVICE);
    AOAHID_CHECK(aoahid_channel_close(channel, kThreadDrainTimeoutMs) == AOAHID_OK);
    static_cast<void>(aoahid_device_close(device, kControlTimeoutMs, kThreadDrainTimeoutMs));
    AOAHID_CHECK(aoahid_context_destroy(context, kControlTimeoutMs, kThreadDrainTimeoutMs) ==
                 AOAHID_OK);
    AOAHID_CHECK(aoahid_fake_libusb_open_handle_count() == 0U);
    aoahid_fake_libusb_reset();
}

void test_hid_adb_and_accessory_bulk_run_together() {
    aoahid_fake_libusb_reset();
    // AOA 1.0: 0x2D01 has the accessory interface first and ADB second.
    const Candidate phone = add_candidate(9U, {2U, 3U}, 0x18D1U, 0x2D01U);
    const aoahid_fake_libusb_bulk_interface accessory{0U, 0xFFU, 0xFFU, 0x00U, 0x81U, 0x01U, 512U};
    const aoahid_fake_libusb_bulk_interface adb{1U, 0xFFU, 0x42U, 0x01U, 0x82U, 0x02U, 512U};
    aoahid_fake_libusb_add_bulk_interface(0U, &accessory);
    aoahid_fake_libusb_add_bulk_interface(0U, &adb);
    aoahid_context_options context_options = public_context_options();
    context_options.event_mode = AOAHID_EVENT_INTERNAL_THREAD;
    aoahid_context* context = nullptr;
    AOAHID_CHECK(aoahid_context_create(&context_options, &context) == AOAHID_OK);
    const aoahid_device_info selected = public_info(phone);
    aoahid_device_options device_options = public_device_options();
    aoahid_device* device = nullptr;
    AOAHID_CHECK(aoahid_device_open(context, &selected, &device_options, &device) == AOAHID_OK);

    aoahid_keyboard_options keyboard{};
    keyboard.struct_size = sizeof(keyboard);
    keyboard.usage_minimum = 0x04U;
    keyboard.usage_maximum = 0x65U;
    aoahid_spec* spec = nullptr;
    AOAHID_CHECK(aoahid_spec_create_keyboard(&keyboard, &spec) == AOAHID_OK);
    const aoahid_node_options node_options{sizeof(aoahid_node_options), 0U, 0U, 0U};
    aoahid_node* node = nullptr;
    AOAHID_CHECK(aoahid_node_open(device, spec, &node_options, kControlTimeoutMs, &node) ==
                 AOAHID_OK);
    aoahid_spec_release(spec);

    aoahid_channel_options accessory_options = adb_channel_options();
    accessory_options.interface_subclass = 0xFFU;
    accessory_options.interface_protocol = 0x00U;
    aoahid_channel* app = nullptr;
    aoahid_channel* shell = nullptr;
    AOAHID_CHECK(aoahid_channel_open(device, &accessory_options, kDrainTimeoutMs, &app) ==
                 AOAHID_OK);
    const aoahid_channel_options adb_options = adb_channel_options();
    AOAHID_CHECK(aoahid_channel_open(device, &adb_options, kDrainTimeoutMs, &shell) == AOAHID_OK);
    // One handle; interfaces 0 and 1 claimed on it.
    AOAHID_CHECK(aoahid_fake_libusb_open_handle_count() == 1U);
    AOAHID_CHECK(aoahid_fake_libusb_claim_count() == 2U);

    // Each Channel reads on its own thread while HID reports go out.
    const std::array<std::uint8_t, 5> app_data{'A', 'P', 'P', '!', '\n'};
    const std::array<std::uint8_t, 4> adb_data{'O', 'K', 'A', 'Y'};
    std::vector<std::uint8_t> app_stream;
    std::vector<std::uint8_t> adb_stream;
    const auto reader = [](aoahid_channel* channel, std::size_t expected,
                           std::vector<std::uint8_t>* out) {
        while (out->size() < expected) {
            std::array<std::uint8_t, 16> part{};
            std::size_t received = 0U;
            if (aoahid_channel_read(channel, part.data(), part.size(), &received, 2000U) !=
                AOAHID_OK)
                return;
            out->insert(out->end(), part.begin(),
                        part.begin() + static_cast<std::ptrdiff_t>(received));
        }
    };
    std::thread app_reader(reader, app, app_data.size(), &app_stream);
    std::thread adb_reader(reader, shell, adb_data.size(), &adb_stream);
    aoahid_fake_libusb_push_bulk_in(0U, 0x81U, app_data.data(), app_data.size());
    aoahid_fake_libusb_push_bulk_in(0U, 0x82U, adb_data.data(), adb_data.size());
    std::size_t written = 0U;
    AOAHID_CHECK(aoahid_channel_write(app, app_data.data(), app_data.size(), &written, 1000U) ==
                 AOAHID_OK);
    AOAHID_CHECK(aoahid_channel_write(shell, adb_data.data(), adb_data.size(), &written, 1000U) ==
                 AOAHID_OK);
    for (std::uint16_t usage = 0x04U; usage < 0x0CU; ++usage) {
        AOAHID_CHECK(aoahid_keyboard_key(node, usage, 1U) == AOAHID_OK);
        AOAHID_CHECK(aoahid_node_submit_blocking(node, 1000U) == AOAHID_OK);
        AOAHID_CHECK(aoahid_keyboard_key(node, usage, 0U) == AOAHID_OK);
        AOAHID_CHECK(aoahid_node_submit_blocking(node, 1000U) == AOAHID_OK);
    }
    app_reader.join();
    adb_reader.join();
    // Data stays on its own interface: nothing crosses between Channels.
    AOAHID_CHECK(app_stream == std::vector<std::uint8_t>(app_data.begin(), app_data.end()));
    AOAHID_CHECK(adb_stream == std::vector<std::uint8_t>(adb_data.begin(), adb_data.end()));
    AOAHID_CHECK(controls_for(57U).size() == 16U);
    AOAHID_CHECK(aoahid_fake_libusb_bulk_out_count() == 2U);

    AOAHID_CHECK(aoahid_channel_close(app, kThreadDrainTimeoutMs) == AOAHID_OK);
    AOAHID_CHECK(aoahid_node_close(node, kControlTimeoutMs, kThreadDrainTimeoutMs) == AOAHID_OK);
    // The Device close also consumes the still-open ADB Channel.
    AOAHID_CHECK(aoahid_device_close(device, kControlTimeoutMs, kThreadDrainTimeoutMs) ==
                 AOAHID_OK);
    AOAHID_CHECK(aoahid_fake_libusb_open_handle_count() == 0U);
    AOAHID_CHECK(aoahid_context_destroy(context, kControlTimeoutMs, kThreadDrainTimeoutMs) ==
                 AOAHID_OK);
    aoahid_fake_libusb_reset();
}

void test_channel_caller_poll_and_device_close() {
    aoahid_fake_libusb_reset();
    const Candidate phone = add_adb_phone();
    aoahid_context_options context_options = public_context_options();
    aoahid_context* context = nullptr;
    AOAHID_CHECK(aoahid_context_create(&context_options, &context) == AOAHID_OK);
    const aoahid_device_info selected = public_info(phone);
    aoahid_device_options device_options = public_device_options();
    aoahid_device* device = nullptr;
    AOAHID_CHECK(aoahid_device_open(context, &selected, &device_options, &device) == AOAHID_OK);
    const aoahid_channel_options options = adb_channel_options();
    aoahid_channel* channel = nullptr;
    AOAHID_CHECK(aoahid_channel_open(device, &options, kDrainTimeoutMs, &channel) == AOAHID_OK);

    // A waiting read drives libusb events itself in caller-poll mode.
    const std::array<std::uint8_t, 4> okay{'O', 'K', 'A', 'Y'};
    aoahid_fake_libusb_push_bulk_in(0U, kAdbEndpointIn, okay.data(), okay.size());
    std::array<std::uint8_t, 8> buffer{};
    std::size_t received = 0U;
    AOAHID_CHECK(aoahid_channel_read(channel, buffer.data(), buffer.size(), &received, 100U) ==
                 AOAHID_OK);
    AOAHID_CHECK(received == okay.size() && std::equal(okay.begin(), okay.end(), buffer.begin()));

    // Without a pump, a zero timeout never waits: two OUT transfers fill the
    // pool and the third chunk reports TIMEOUT with the queued byte count.
    const std::vector<std::uint8_t> data(3U * kAdbMaxPacket + 1U, 0x11U);
    std::size_t written = 0U;
    AOAHID_CHECK(aoahid_channel_write(channel, data.data(), data.size(), &written, 0U) ==
                 AOAHID_ERR_TIMEOUT);
    AOAHID_CHECK(written == 2U * kAdbMaxPacket);
    // With a timeout the same call pumps completions and finishes.
    AOAHID_CHECK(aoahid_channel_write(channel, data.data() + written, data.size() - written,
                                      &written, 100U) == AOAHID_OK);
    AOAHID_CHECK(written == kAdbMaxPacket + 1U);

    // Closing the Device consumes the still-open Channel and its handle.
    AOAHID_CHECK(aoahid_device_close(device, kControlTimeoutMs, kDrainTimeoutMs) == AOAHID_OK);
    AOAHID_CHECK(aoahid_fake_libusb_open_handle_count() == 0U);
    aoahid_fake_libusb_claim_record claim{};
    AOAHID_CHECK(aoahid_fake_libusb_claim_count() == 2U &&
                 aoahid_fake_libusb_get_claim(1U, &claim) == LIBUSB_SUCCESS &&
                 claim.interface_number == 1 && claim.release == 1);
    AOAHID_CHECK(aoahid_context_destroy(context, kControlTimeoutMs, kDrainTimeoutMs) == AOAHID_OK);
    aoahid_fake_libusb_reset();
}

} // namespace transport_sim
