// SPDX-License-Identifier: MIT
// Copyright (c) 2026 libaoahid contributors
/* Registrations against the fake phone: how many AOA requests 54, 55 and 56 each way of opening and
 * closing Nodes sends, in which order, what a staged Node may do, and what a failure leaves. */
#include "sim/sim_support.hpp"
#include "sim/transport_support.hpp"

#include <string_view>

namespace {

constexpr std::uint8_t kRegister = 54U;
constexpr std::uint8_t kUnregister = 55U;

aoahid_spec* keyboard_spec(const std::uint8_t id) {
    aoahid_keyboard_options options;
    aoahid_keyboard_options_init(&options);
    options.usage_minimum = 0x04U;
    options.usage_maximum = 0x65U;
    options.report_id = aoahid_report_id_option{1U, id, {}};
    aoahid_spec* spec = nullptr;
    AOAHID_CHECK(aoahid_spec_create_keyboard(&options, &spec) == AOAHID_OK);
    return spec;
}

aoahid_spec* mouse_spec(const std::uint8_t id) {
    aoahid_mouse_options options;
    aoahid_mouse_options_init(&options);
    options.report_id = aoahid_report_id_option{1U, id, {}};
    aoahid_spec* spec = nullptr;
    AOAHID_CHECK(aoahid_spec_create_mouse(&options, &spec) == AOAHID_OK);
    return spec;
}

aoahid_node_placement placement(const std::uint32_t registration) {
    return aoahid_node_placement{static_cast<std::uint32_t>(sizeof(aoahid_node_placement)), 0U,
                                 registration, 0U};
}

const aoahid_commit_timeouts* waits() {
    return commit_timeouts_of(sim::kControlTimeoutMs, 20U);
}

bool field_is(const char* expected) {
    const aoahid_error_detail* detail = aoahid_last_error();
    return detail != nullptr && detail->field != nullptr &&
           std::string_view(detail->field) == expected;
}

// The requests of one kind the fake phone has seen so far.
std::size_t count(const std::uint8_t request) {
    return transport_sim::controls_for(request).size();
}

// A Device on a fake phone with no Node yet.
struct Fixture {
    sim::Session session{};
    Fixture() {
        aoahid_fake_libusb_reset();
        AOAHID_CHECK(session.open(nullptr));
    }
    ~Fixture() {
        static_cast<void>(session.close());
        aoahid_fake_libusb_reset();
    }
    aoahid_node* open(aoahid_spec* spec, const std::uint32_t registration,
                      aoahid_result* result = nullptr) {
        const aoahid_node_placement where = placement(registration);
        aoahid_node* node = nullptr;
        const aoahid_result opened =
            aoahid_node_open(session.device, spec, &where, waits(), &node);
        if (result != nullptr) {
            *result = opened;
        } else {
            AOAHID_CHECK(opened == AOAHID_OK);
        }
        return node;
    }
};

void test_one_node_is_one_register_and_one_descriptor_transfer() {
    Fixture fixture;
    aoahid_spec* spec = keyboard_spec(1U);
    aoahid_node* node = fixture.open(spec, 1U);
    AOAHID_CHECK(node != nullptr);
    AOAHID_CHECK(count(kRegister) == 1U && count(kUnregister) == 0U);
    AOAHID_CHECK(aoahid_node_id(node) != 0U);
    aoahid_spec_release(spec);
    AOAHID_CHECK(aoahid_node_close(node, waits()) == AOAHID_OK);
    AOAHID_CHECK(count(kUnregister) == 1U);
}

void test_separate_registrations_are_separate_hid_devices() {
    Fixture fixture;
    aoahid_spec* keyboard = keyboard_spec(1U);
    aoahid_spec* mouse = mouse_spec(2U);
    aoahid_node* first = fixture.open(keyboard, 1U);
    aoahid_node* second = fixture.open(mouse, 2U);
    const auto registered = transport_sim::controls_for(kRegister);
    AOAHID_CHECK(registered.size() == 2U && registered[0].value != registered[1].value);
    AOAHID_CHECK(count(kUnregister) == 0U);
    AOAHID_CHECK(aoahid_node_id(first) != aoahid_node_id(second));
    aoahid_spec_release(keyboard);
    aoahid_spec_release(mouse);
}

void test_a_shared_registration_is_replaced_on_every_open() {
    Fixture fixture;
    aoahid_spec* keyboard = keyboard_spec(1U);
    aoahid_spec* mouse = mouse_spec(2U);
    aoahid_node* first = fixture.open(keyboard, 7U);
    const std::uint16_t before = transport_sim::last_registered_hid_id();
    aoahid_node* second = fixture.open(mouse, 7U);
    // One HID device on the phone holds both: the old one went away, a new ID took its place.
    AOAHID_CHECK(count(kRegister) == 2U && count(kUnregister) == 1U);
    AOAHID_CHECK(transport_sim::controls_for(kUnregister)[0].value == before);
    AOAHID_CHECK(transport_sim::last_registered_hid_id() != before);
    // Both Nodes report to the new HID device.
    AOAHID_CHECK(aoahid_keyboard_key(first, 0x04U, 1U) == AOAHID_OK);
    AOAHID_CHECK(aoahid_node_submit_blocking(first, sim::kSendTimeoutMs) == AOAHID_OK);
    AOAHID_CHECK(aoahid_mouse_move(second, 1, 1) == AOAHID_OK);
    AOAHID_CHECK(aoahid_node_submit_blocking(second, sim::kSendTimeoutMs) == AOAHID_OK);
    aoahid_spec_release(keyboard);
    aoahid_spec_release(mouse);
}

void test_a_batch_sends_nothing_until_its_end() {
    Fixture fixture;
    aoahid_spec* keyboard = keyboard_spec(1U);
    aoahid_spec* mouse = mouse_spec(2U);
    AOAHID_CHECK(aoahid_device_batch_begin(fixture.session.device) == AOAHID_OK);
    AOAHID_CHECK(aoahid_device_batch_begin(fixture.session.device) == AOAHID_ERR_BUSY);
    aoahid_node* first = fixture.open(keyboard, 7U);
    aoahid_node* second = fixture.open(mouse, 7U);
    AOAHID_CHECK(count(kRegister) == 0U && count(kUnregister) == 0U);
    // A staged Node is not on the phone yet.
    AOAHID_CHECK(aoahid_keyboard_key(first, 0x04U, 1U) == AOAHID_OK);
    AOAHID_CHECK(aoahid_node_submit(first, sim::kSendTimeoutMs) == AOAHID_ERR_BUSY);
    AOAHID_CHECK(field_is("node.registration"));
    AOAHID_CHECK(aoahid_device_batch_end(fixture.session.device, waits()) == AOAHID_OK);
    AOAHID_CHECK(count(kRegister) == 1U && count(kUnregister) == 0U);
    AOAHID_CHECK(aoahid_node_submit_blocking(first, sim::kSendTimeoutMs) == AOAHID_OK);
    AOAHID_CHECK(aoahid_mouse_move(second, 1, 1) == AOAHID_OK);
    AOAHID_CHECK(aoahid_node_submit_blocking(second, sim::kSendTimeoutMs) == AOAHID_OK);
    AOAHID_CHECK(aoahid_device_batch_end(fixture.session.device, waits()) == AOAHID_ERR_BUSY);
    aoahid_spec_release(keyboard);
    aoahid_spec_release(mouse);
}

void test_closing_one_of_two_replaces_and_closing_the_last_unregisters() {
    Fixture fixture;
    aoahid_spec* keyboard = keyboard_spec(1U);
    aoahid_spec* mouse = mouse_spec(2U);
    aoahid_node* first = fixture.open(keyboard, 7U);
    aoahid_node* second = fixture.open(mouse, 7U);
    const std::size_t registered = count(kRegister);
    const std::size_t unregistered = count(kUnregister);
    AOAHID_CHECK(aoahid_node_close(first, waits()) == AOAHID_OK);
    AOAHID_CHECK(count(kUnregister) == unregistered + 1U && count(kRegister) == registered + 1U);
    AOAHID_CHECK(aoahid_mouse_move(second, 1, 1) == AOAHID_OK);
    AOAHID_CHECK(aoahid_node_submit_blocking(second, sim::kSendTimeoutMs) == AOAHID_OK);
    AOAHID_CHECK(aoahid_node_close(second, waits()) == AOAHID_OK);
    AOAHID_CHECK(count(kUnregister) == unregistered + 2U && count(kRegister) == registered + 1U);
    aoahid_spec_release(keyboard);
    aoahid_spec_release(mouse);
}

void test_hid_ids_are_never_reused() {
    Fixture fixture;
    aoahid_spec* keyboard = keyboard_spec(1U);
    std::uint16_t previous = 0U;
    for (int round = 0; round < 3; ++round) {
        aoahid_node* node = fixture.open(keyboard, 1U);
        const std::uint16_t current = transport_sim::last_registered_hid_id();
        AOAHID_CHECK(current != previous);
        previous = current;
        AOAHID_CHECK(aoahid_node_close(node, waits()) == AOAHID_OK);
    }
    aoahid_spec_release(keyboard);
}

void test_a_refused_join_sends_nothing() {
    Fixture fixture;
    aoahid_spec* keyboard = keyboard_spec(1U);
    aoahid_spec* mouse = mouse_spec(1U);
    aoahid_node* first = fixture.open(keyboard, 7U);
    const std::size_t registered = count(kRegister);
    const std::size_t unregistered = count(kUnregister);
    aoahid_result result = AOAHID_OK;
    aoahid_node* second = fixture.open(mouse, 7U, &result);
    AOAHID_CHECK(result == AOAHID_ERR_PARAM && second == nullptr);
    AOAHID_CHECK(field_is("node.report_id"));
    AOAHID_CHECK(count(kRegister) == registered && count(kUnregister) == unregistered);
    // The first Node is untouched.
    AOAHID_CHECK(aoahid_keyboard_key(first, 0x04U, 1U) == AOAHID_OK);
    AOAHID_CHECK(aoahid_node_submit_blocking(first, sim::kSendTimeoutMs) == AOAHID_OK);
    aoahid_spec_release(keyboard);
    aoahid_spec_release(mouse);
}

void test_a_failed_register_puts_the_old_nodes_back() {
    Fixture fixture;
    aoahid_spec* keyboard = keyboard_spec(1U);
    aoahid_spec* mouse = mouse_spec(2U);
    aoahid_node* first = fixture.open(keyboard, 7U);
    // The replacement's request 55 goes through; its request 54 is refused.
    aoahid_fake_libusb_queue_control_result(LIBUSB_SUCCESS);
    aoahid_fake_libusb_queue_control_result(LIBUSB_ERROR_PIPE);
    aoahid_result result = AOAHID_OK;
    aoahid_node* second = fixture.open(mouse, 7U, &result);
    AOAHID_CHECK(result == AOAHID_ERR_STALL && second == nullptr);
    AOAHID_CHECK(aoahid_last_error()->aoa_request == 54);
    // The first Node has a HID device again and can report.
    AOAHID_CHECK(aoahid_keyboard_key(first, 0x04U, 1U) == AOAHID_OK);
    AOAHID_CHECK(aoahid_node_submit_blocking(first, sim::kSendTimeoutMs) == AOAHID_OK);
    aoahid_spec_release(keyboard);
    aoahid_spec_release(mouse);
}

void test_device_close_unregisters_each_registration_once() {
    Fixture fixture;
    aoahid_spec* keyboard = keyboard_spec(1U);
    aoahid_spec* mouse = mouse_spec(2U);
    aoahid_spec* other = keyboard_spec(1U);
    static_cast<void>(fixture.open(keyboard, 7U));
    static_cast<void>(fixture.open(mouse, 7U));
    static_cast<void>(fixture.open(other, 8U));
    const std::size_t before = count(kUnregister);
    AOAHID_CHECK(aoahid_device_close(fixture.session.device, sim::kControlTimeoutMs,
                                     20U) == AOAHID_OK);
    fixture.session.device = nullptr;
    // Two HID devices were on the phone, so two requests, however many Nodes they held.
    AOAHID_CHECK(count(kUnregister) == before + 2U);
    aoahid_spec_release(keyboard);
    aoahid_spec_release(mouse);
    aoahid_spec_release(other);
}

void test_registration_argument_errors() {
    Fixture fixture;
    aoahid_spec* keyboard = keyboard_spec(1U);
    aoahid_result result = AOAHID_OK;
    static_cast<void>(fixture.open(keyboard, 0U, &result));
    AOAHID_CHECK(result == AOAHID_ERR_UNSET_FIELD && field_is("node.registration"));
    AOAHID_CHECK(aoahid_device_commit(fixture.session.device, nullptr) == AOAHID_ERR_PARAM);
    AOAHID_CHECK(aoahid_device_commit(fixture.session.device, waits()) == AOAHID_OK);
    AOAHID_CHECK(aoahid_device_batch_end(fixture.session.device, waits()) == AOAHID_ERR_BUSY);
    AOAHID_CHECK(count(kRegister) == 0U);
    aoahid_spec_release(keyboard);
}

} // namespace

void test_registration() {
    test_one_node_is_one_register_and_one_descriptor_transfer();
    test_separate_registrations_are_separate_hid_devices();
    test_a_shared_registration_is_replaced_on_every_open();
    test_a_batch_sends_nothing_until_its_end();
    test_closing_one_of_two_replaces_and_closing_the_last_unregisters();
    test_hid_ids_are_never_reused();
    test_a_refused_join_sends_nothing();
    test_a_failed_register_puts_the_old_nodes_back();
    test_device_close_unregisters_each_registration_once();
    test_registration_argument_errors();
}
