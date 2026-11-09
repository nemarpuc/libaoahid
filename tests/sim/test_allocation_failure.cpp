// SPDX-License-Identifier: MIT
// Copyright (c) 2026 libaoahid contributors
/* Fails the library's allocations one at a time, in order, through a whole
 * session (Context, discovery, Device, Specs, Nodes, reports, a Channel and
 * the teardown) in both event modes. Every call that meets the failure must
 * return AOAHID_ERR_INTERNAL with a matching aoahid_last_error(), never throw
 * or abort, and the session must still close completely afterwards. The fake
 * libusb's own bookkeeping is never failed (real libusb is C and reports
 * LIBUSB_ERROR_NO_MEM instead), but its nothrow allocations of contexts,
 * handles and transfers are, which is what LIBUSB_ERROR_NO_MEM looks like. */
#include "sim/sim_support.hpp"

#include <array>
#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <new>
#include <vector>

int g_failures = 0;
std::size_t g_checks = 0U;

namespace {

// The countdown is armed for one allocation; zero means disarmed.
std::atomic<long> g_countdown{0};
std::atomic<bool> g_failed{false};

bool fail_this_allocation() noexcept {
    if (g_countdown.load(std::memory_order_relaxed) == 0 ||
        aoahid_fake_libusb_backend_depth() != 0U)
        return false;
    if (g_countdown.fetch_sub(1, std::memory_order_relaxed) == 1) {
        g_failed.store(true, std::memory_order_relaxed);
        return true;
    }
    return false;
}

void* allocate(const std::size_t size) {
    if (fail_this_allocation())
        throw std::bad_alloc();
    void* storage = std::malloc(size == 0U ? 1U : size);
    if (storage == nullptr)
        throw std::bad_alloc();
    return storage;
}

void* allocate_nothrow(const std::size_t size) noexcept {
    if (fail_this_allocation())
        return nullptr;
    return std::malloc(size == 0U ? 1U : size);
}

} // namespace

void* operator new(const std::size_t size) { return allocate(size); }
void* operator new[](const std::size_t size) { return allocate(size); }
void* operator new(const std::size_t size, const std::nothrow_t&) noexcept {
    return allocate_nothrow(size);
}
void* operator new[](const std::size_t size, const std::nothrow_t&) noexcept {
    return allocate_nothrow(size);
}
void operator delete(void* storage) noexcept { std::free(storage); }
void operator delete[](void* storage) noexcept { std::free(storage); }
void operator delete(void* storage, std::size_t) noexcept { std::free(storage); }
void operator delete[](void* storage, std::size_t) noexcept { std::free(storage); }
void operator delete(void* storage, const std::nothrow_t&) noexcept { std::free(storage); }
void operator delete[](void* storage, const std::nothrow_t&) noexcept { std::free(storage); }

namespace {

using sim::kControlTimeoutMs;
using sim::kDrainTimeoutMs;
using sim::kSendTimeoutMs;
using sim::kThreadDrainTimeoutMs;

void arm(const long allocation) {
    g_failed.store(false, std::memory_order_relaxed);
    g_countdown.store(allocation, std::memory_order_relaxed);
}

void disarm() { g_countdown.store(0, std::memory_order_relaxed); }

// The test's own calls into the fake (setting up the phone, queueing bytes)
// allocate outside its locks; they are not what is under test.
class Paused final {
  public:
    Paused() noexcept : saved_(g_countdown.exchange(0, std::memory_order_relaxed)) {}
    ~Paused() { g_countdown.store(saved_, std::memory_order_relaxed); }
    Paused(const Paused&) = delete;
    Paused& operator=(const Paused&) = delete;

  private:
    long saved_;
};

// What the session made so far, so it can be closed whatever step failed.
struct Made {
    aoahid_context* context{};
    aoahid_discovery* discovery{};
    aoahid_spec* keyboard{};
    aoahid_spec* raw{};
    aoahid_spec* mouse{};
};

void close_made(Made* made) {
    if (made->discovery != nullptr)
        aoahid_discovery_destroy(made->discovery);
    if (made->context != nullptr) {
        AOAHID_CHECK(aoahid_context_destroy_blocking(made->context, kControlTimeoutMs,
                                                     kDrainTimeoutMs, kThreadDrainTimeoutMs) !=
                     AOAHID_CLOSE_PENDING);
    }
    aoahid_spec_release(made->keyboard);
    aoahid_spec_release(made->raw);
    aoahid_spec_release(made->mouse);
    *made = Made{};
}

// A step either succeeds or meets the injected failure; it may fail no other way.
bool step(const aoahid_result result, const char* name) {
    if (result == AOAHID_OK)
        return true;
    const bool expected = result == AOAHID_ERR_INTERNAL && sim::last_error_is(result) &&
                          g_failed.load(std::memory_order_relaxed);
    if (!expected) {
        std::fprintf(stderr, "%s returned %s after an allocation failure\n", name,
                     aoahid_result_name(result));
    }
    AOAHID_CHECK(expected);
    return false;
}

// Runs the session once; the armed allocation fails wherever it falls.
void run_session(const aoahid_event_mode mode, const long allocation) {
    Made made{};
    const sim::Phone phone = sim::add_phone(0x2D00U, true);
    arm(allocation);
    const aoahid_context_options context_options = sim::context_options(mode);
    const auto finish = [&made]() {
        disarm();
        close_made(&made);
    };
    if (!step(aoahid_context_create(&context_options, &made.context), "context_create"))
        return finish();
    if (!step(aoahid_discover(made.context, kControlTimeoutMs, &made.discovery), "discover"))
        return finish();
    AOAHID_CHECK(aoahid_discovery_count(made.discovery) <= 1U);
    aoahid_discovery_destroy(made.discovery);
    made.discovery = nullptr;

    aoahid_keyboard_options keyboard_options;
    aoahid_keyboard_options_init(&keyboard_options);
    if (!step(aoahid_spec_create_keyboard(&keyboard_options, &made.keyboard), "keyboard spec"))
        return finish();
    const aoahid_raw_options raw_options = sim::raw_options();
    if (!step(aoahid_spec_create_raw(&raw_options, &made.raw), "raw spec"))
        return finish();
    aoahid_mouse_options mouse_options;
    aoahid_mouse_options_init(&mouse_options);
    mouse_options.enable_battery = 1U;
    mouse_options.battery.report_id = aoahid_report_id_option{1U, 2U, {}};
    mouse_options.report_id = aoahid_report_id_option{1U, 1U, {}};
    mouse_options.battery.strength = aoahid_integer_field{0, 100, 8U, {}};
    if (!step(aoahid_spec_create_mouse(&mouse_options, &made.mouse), "mouse spec"))
        return finish();

    const aoahid_device_options device_options = sim::device_options();
    aoahid_device* device = nullptr;
    if (!step(aoahid_device_open(made.context, &phone.info, &device_options, &device),
              "device_open"))
        return finish();
    const aoahid_node_options node_options = sim::node_options();
    aoahid_node* keyboard = nullptr;
    if (!step(aoahid_node_open(device, made.keyboard, &node_options, kControlTimeoutMs, &keyboard),
              "keyboard node_open"))
        return finish();
    aoahid_node* raw = nullptr;
    if (!step(aoahid_node_open(device, made.raw, &node_options, kControlTimeoutMs, &raw),
              "raw node_open"))
        return finish();
    aoahid_node* mouse = nullptr;
    if (!step(aoahid_node_open(device, made.mouse, &node_options, kControlTimeoutMs, &mouse),
              "mouse node_open"))
        return finish();

    if (!step(aoahid_keyboard_key(keyboard, 0x04U, 1U), "keyboard_key") ||
        !step(aoahid_node_submit_blocking(keyboard, kSendTimeoutMs), "submit_blocking"))
        return finish();
    const std::array<std::uint8_t, 2U> report{1U, 1U};
    if (!step(aoahid_raw_submit(raw, report.data(), report.size(), kSendTimeoutMs), "raw_submit"))
        return finish();
    if (!step(aoahid_mouse_move(mouse, 1, 1), "mouse_move") ||
        !step(aoahid_battery_update(mouse, 50), "battery_update") ||
        !step(aoahid_node_submit_blocking(mouse, kSendTimeoutMs), "mouse submit"))
        return finish();

    aoahid_channel_options channel_options = sim::adb_stream_options();
    aoahid_channel* channel = nullptr;
    if (!step(aoahid_channel_open(device, &channel_options, kThreadDrainTimeoutMs, &channel),
              "channel_open"))
        return finish();
    const std::array<std::uint8_t, 4U> data{'O', 'K', 'A', 'Y'};
    std::size_t moved = 0U;
    if (!step(aoahid_channel_write(channel, data.data(), data.size(), &moved, 100U),
              "channel_write"))
        return finish();
    {
        const Paused paused{};
        aoahid_fake_libusb_push_bulk_in(phone.index, sim::kAdbEndpointIn, data.data(), data.size());
    }
    std::array<std::uint8_t, sim::kAdbMaxPacket> buffer{};
    if (!step(aoahid_channel_read(channel, buffer.data(), buffer.size(), &moved, 100U),
              "channel_read"))
        return finish();
    if (!step(aoahid_channel_close(channel, kThreadDrainTimeoutMs), "channel_close"))
        return finish();

    if (!step(aoahid_node_close(raw, kControlTimeoutMs, kThreadDrainTimeoutMs), "node_close"))
        return finish();
    if (!step(aoahid_device_close(device, kControlTimeoutMs, kThreadDrainTimeoutMs),
              "device_close"))
        return finish();
    const aoahid_result destroyed =
        aoahid_context_destroy(made.context, kControlTimeoutMs, kThreadDrainTimeoutMs);
    if (destroyed == AOAHID_OK || destroyed == AOAHID_ERR_INTERNAL)
        made.context = nullptr;
    static_cast<void>(step(destroyed, "context_destroy"));
    finish();
}

// Fails the 1st, 2nd, ... allocation until one run of the session meets none.
void sweep(const aoahid_event_mode mode) {
    std::size_t injected = 0U;
    for (long allocation = 1; allocation < 5000; ++allocation) {
        aoahid_fake_libusb_reset();
        run_session(mode, allocation);
        disarm();
        AOAHID_CHECK(aoahid_fake_libusb_open_handle_count() == 0U);
        AOAHID_CHECK(aoahid_fake_libusb_pending_transfer_count() == 0U);
        if (!g_failed.load(std::memory_order_relaxed)) {
            // Every allocation the session makes has now failed once.
            AOAHID_CHECK(injected > 20U);
            std::printf("%s: %zu allocation failures injected\n",
                        mode == AOAHID_EVENT_CALLER_POLL ? "caller-poll" : "internal-thread",
                        injected);
            aoahid_fake_libusb_reset();
            return;
        }
        ++injected;
    }
    std::fputs("the session never ran without meeting an allocation failure\n", stderr);
    ++g_failures;
}

} // namespace

int main() {
    sweep(AOAHID_EVENT_CALLER_POLL);
    sweep(AOAHID_EVENT_INTERNAL_THREAD);
    std::printf("%zu checks\n", g_checks);
    return g_failures == 0 ? 0 : 1;
}
