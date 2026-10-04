// SPDX-License-Identifier: MIT
// Copyright (c) 2026 libaoahid contributors
#ifndef AOAHID_H
#define AOAHID_H

/*
 * This header is the complete stable C ABI for libaoahid. It deliberately exposes
 * no libusb or C++ type, and it contains no option initializer or implicit preset.
 */

#include <stddef.h>
#include <stdint.h>

#if defined(_WIN32)
#if defined(AOAHID_BUILDING_LIBRARY)
#define AOAHID_API __declspec(dllexport)
#elif defined(AOAHID_SHARED)
#define AOAHID_API __declspec(dllimport)
#else
#define AOAHID_API
#endif
#define AOAHID_CALL __cdecl
#else
#define AOAHID_API __attribute__((visibility("default")))
#define AOAHID_CALL
#endif

#ifdef __cplusplus
extern "C" {
#endif

#define AOAHID_VERSION_MAJOR 4
#define AOAHID_VERSION_MINOR 1
#define AOAHID_VERSION_PATCH 0

typedef struct aoahid_context aoahid_context;
typedef struct aoahid_discovery aoahid_discovery;
typedef struct aoahid_device aoahid_device;
typedef struct aoahid_spec aoahid_spec;
typedef struct aoahid_node aoahid_node;
typedef struct aoahid_channel aoahid_channel;

/* Every public enum domain has an explicit 32-bit ABI representation.  The
 * anonymous enums below provide integral constants without making structure
 * layout depend on a compiler's enum-size option. */
typedef int32_t aoahid_result;
enum {
    AOAHID_OK = 0,
    AOAHID_ERR_PARAM = 1,
    AOAHID_ERR_UNSET_FIELD = 2,
    AOAHID_ERR_UNSUPPORTED = 3,
    AOAHID_ERR_NOT_AOA = 4,
    AOAHID_ERR_ACCESS = 6,
    AOAHID_ERR_BUSY = 7,
    AOAHID_ERR_NO_DEVICE = 8,
    AOAHID_ERR_STALL = 9,
    AOAHID_ERR_TIMEOUT = 10,
    AOAHID_ERR_SHORT_TRANSFER = 11,
    AOAHID_ERR_DESCRIPTOR_REJECTED = 12,
    AOAHID_ERR_IO = 13,
    AOAHID_ERR_OVERFLOW = 14,
    AOAHID_CLOSE_PENDING = 15,
    AOAHID_ERR_INTERNAL = 16
};

typedef int32_t aoahid_event_mode;
enum {
    /* The caller invokes aoahid_context_poll(). libaoahid creates no event
     * thread and takes no internal send-path lock, so the caller must serialize
     * one context. A platform libusb backend may own helper threads. */
    AOAHID_EVENT_CALLER_POLL = 1,
    /* One library event thread advances the context. Pool and error-latch access
     * is synchronized, adding contention that caller-poll mode does not have. */
    AOAHID_EVENT_INTERNAL_THREAD = 2
};

typedef int32_t aoahid_interface_claim_policy;
enum {
    /* Do not explicitly claim an interface for device-recipient EP0 requests. */
    AOAHID_INTERFACE_CLAIM_NONE = 1,
    /* Claim the exact caller-supplied interface and release only that interface. */
    AOAHID_INTERFACE_CLAIM_EXPLICIT = 2
};

typedef int32_t aoahid_log_level;
enum { AOAHID_LOG_DISABLED = 1, AOAHID_LOG_ERROR = 2, AOAHID_LOG_INFO = 3, AOAHID_LOG_TRACE = 4 };

typedef int32_t aoahid_profile_kind;
enum {
    AOAHID_PROFILE_KEYBOARD = 1,
    AOAHID_PROFILE_MOUSE = 2,
    /* One-bit allow-listed selector controls on any HUT Usage Page: Consumer,
     * System Control, Camera Control, Telephony, or a caller-chosen page. The
     * exact Application Collection and field Usage Page are caller fields. */
    AOAHID_PROFILE_TOGGLE = 3,
    /* Always the Generic Desktop / Game Pad Application Collection Usage. */
    AOAHID_PROFILE_GAMEPAD = 4,
    /* Always the fixed-slot Multi-Touch Touch Screen Application Collection
     * Usage. */
    AOAHID_PROFILE_TOUCHSCREEN = 5,
    AOAHID_PROFILE_PEN = 6,
    AOAHID_PROFILE_BATTERY = 7,
    AOAHID_PROFILE_RAW = 8,
    /* Always the Digitizers / Touch Pad Application Collection Usage. */
    AOAHID_PROFILE_TOUCHPAD = 9
};

typedef int32_t aoahid_android_status;
enum {
    AOAHID_ANDROID_PORTABLE_CANDIDATE = 1,
    AOAHID_ANDROID_CONDITIONAL = 2,
    AOAHID_ANDROID_CUSTOM_SYSTEM_ONLY = 3,
    AOAHID_ANDROID_UNSUPPORTED = 4,
    AOAHID_ANDROID_UNKNOWN = 5
};

typedef int32_t aoahid_pen_mode;
enum { AOAHID_PEN_DIRECT_SCREEN = 1, AOAHID_PEN_INDIRECT_TABLET = 2 };

typedef int32_t aoahid_axis_role;
enum {
    AOAHID_AXIS_X = 1,
    AOAHID_AXIS_Y = 2,
    AOAHID_AXIS_Z = 3,
    AOAHID_AXIS_RX = 4,
    AOAHID_AXIS_RY = 5,
    AOAHID_AXIS_RZ = 6,
    AOAHID_AXIS_SLIDER = 7,
    AOAHID_AXIS_SIMULATION_ACCELERATOR = 8,
    AOAHID_AXIS_SIMULATION_BRAKE = 9,
    AOAHID_AXIS_SIMULATION_STEERING = 10,
    AOAHID_AXIS_DIAL = 11,
    AOAHID_AXIS_WHEEL = 12,
    AOAHID_AXIS_SIMULATION_RUDDER = 13,
    AOAHID_AXIS_SIMULATION_THROTTLE = 14
};

typedef int32_t aoahid_dpad_representation;
enum { AOAHID_DPAD_NONE = 1, AOAHID_DPAD_HAT = 2, AOAHID_DPAD_BUTTONS = 3 };

/* Exact one-control report semantics selected for each Consumer, System,
 * Camera, or Telephony Usage.  These values name a descriptor encoding, not
 * an operating-system mapping.  LINEAR, DYNAMIC_VALUE, NAMED_ARRAY, and the
 * two-direction ON_OFF_PAIR are defined values so a caller can describe
 * the HUT type without lying; the press/release profile factories reject them
 * as unsupported because their value/state cannot be represented by that API. */
typedef int32_t aoahid_usage_semantic;
enum {
    AOAHID_USAGE_SELECTOR_BITMAP = 1,
    AOAHID_USAGE_ON_OFF_TOGGLE = 2,
    AOAHID_USAGE_ON_OFF_MAINTAINED = 3,
    AOAHID_USAGE_MOMENTARY = 4,
    AOAHID_USAGE_ONE_SHOT = 5,
    AOAHID_USAGE_RETRIGGER = 6,
    AOAHID_USAGE_ON_OFF_PAIR = 7,
    AOAHID_USAGE_LINEAR = 8,
    AOAHID_USAGE_DYNAMIC_VALUE = 9,
    AOAHID_USAGE_NAMED_ARRAY = 10
};

typedef void(AOAHID_CALL* aoahid_log_sink)(void* user, aoahid_log_level level, const char* message);

typedef struct aoahid_error_detail {
    int32_t code;
    const char* field;
    const char* reason;
    int32_t libusb_status;
    int32_t aoa_request;
    uint16_t hid_id;
    uint16_t report_id;
    uint32_t offset;
    uint32_t length;
} aoahid_error_detail;

typedef struct aoahid_context_options {
    uint32_t struct_size;
    uint32_t reserved;
    aoahid_event_mode event_mode;
    aoahid_log_level log_level;
    aoahid_log_sink log_sink;
    void* log_user;
} aoahid_context_options;

typedef struct aoahid_device_info {
    uint8_t bus_number;
    uint8_t device_address;
    const uint8_t* port_path;
    size_t port_path_length;
    uint16_t vendor_id;
    uint16_t product_id;
    /* The request-51 AOA protocol version aoahid_discover() read. Zero when the
     * caller fills this structure itself; aoahid_device_open() ignores it. */
    uint16_t protocol_version;
    const char* serial;
    const char* product;
} aoahid_device_info;

typedef struct aoahid_aoa_strings {
    /* AOA identification strings (request 52, string IDs 0-5), each a
     * NUL-terminated string or null. aoahid_accessory_start() requires a
     * nonempty manufacturer and model and sends only the non-null others. */
    const char* manufacturer;
    const char* model;
    const char* description;
    const char* version;
    const char* uri;
    const char* serial;
} aoahid_aoa_strings;

typedef struct aoahid_device_options {
    uint32_t struct_size;
    uint32_t reserved;
    /* Tuning fields: zero selects the documented bounded fallback. These are
     * project policies, not USB/AOA requirements or libusb recommendations. */
    /* zero -> 500 ms */
    uint32_t control_timeout_ms;
    /* zero -> 500 ms. After a touch report times out, the lifted contacts in it
     * stay occupied; close and reopen that Node. Keep this well above normal
     * EP0 latency. */
    uint32_t send_timeout_ms;
    /* zero -> 64 bytes */
    uint32_t descriptor_fragment_bytes;
    /* zero -> 8 slots */
    uint32_t transfer_pool_slots;
    /* zero -> 1024 bytes */
    uint32_t maximum_report_bytes;
    /* zero -> 1000 ms */
    uint32_t close_drain_timeout_ms;
    /* Four independent byte policies prevent a Linux descriptor ceiling from
     * being mistaken for the AOA 16-bit descriptor wire field or either
     * control-transfer buffer. None is a default or a discovered limit. */
    uint32_t aoa_descriptor_wire_policy_bytes;
    uint32_t linux_descriptor_policy_bytes;
    /* Caller-selected limits for the exact target HID parser revision. These
     * are not universal Linux, Android, HID, or library defaults. */
    uint32_t linux_hid_fields_per_report_policy;
    uint32_t linux_hid_global_stack_depth_policy;
    uint64_t linux_hid_usages_policy;
    uint64_t linux_hid_report_data_bits_policy;
    uint32_t linux_hid_report_size_bits_policy;
    uint32_t target_ep0_data_policy_bytes;
    uint32_t host_control_buffer_policy_bytes;
    aoahid_interface_claim_policy interface_claim_policy;
    int32_t interface_number;
    /* 0 or 1. 1 decodes every generated report again before request 57. */
    uint32_t validate_reports;
} aoahid_device_options;

typedef struct aoahid_accessory_options {
    uint32_t struct_size;
    uint32_t reserved;
    /* Android matches manufacturer and model against an application's
     * accessory filter; they are product values the caller chooses. */
    aoahid_aoa_strings strings;
    /* zero -> 500 ms, per request 51/52/53 */
    uint32_t control_timeout_ms;
} aoahid_accessory_options;

typedef int32_t aoahid_channel_read_mode;
enum {
    /* in_transfers IN transfers of transfer_bytes stay submitted and reads
     * return their bytes as one stream. A transfer ends only when full or on a
     * short packet, so a device that ends a packet-aligned message without a
     * zero-length packet leaves those bytes waiting for more data. */
    AOAHID_CHANNEL_READ_STREAM = 0,
    /* No read-ahead: a read with nothing buffered submits one IN transfer of
     * its capacity rounded up to wMaxPacketSize (at most transfer_bytes), as
     * AOSP adb does for a length-prefixed payload. It completes as soon as it
     * is full, with no zero-length packet needed. in_transfers is ignored. */
    AOAHID_CHANNEL_READ_REQUEST = 1
};

typedef struct aoahid_channel_options {
    uint32_t struct_size;
    uint32_t reserved;
    /* The first interface (alternate setting 0) with this exact class,
     * subclass, and protocol and one Bulk IN plus one Bulk OUT endpoint. */
    uint8_t interface_class;
    uint8_t interface_subclass;
    uint8_t interface_protocol;
    uint8_t reserved8;
    /* Tuning fields: zero selects the documented bounded fallback. These are
     * project policies, not USB, Android, or ADB requirements. */
    /* zero -> 4 IN transfers kept submitted for reading ahead */
    uint32_t in_transfers;
    /* zero -> 4 OUT transfers */
    uint32_t out_transfers;
    /* zero -> 65536 bytes per transfer; rounded up to wMaxPacketSize */
    uint32_t transfer_bytes;
    /* Exactly zero or one. One ends every write whose length is a nonzero
     * multiple of wMaxPacketSize with an explicit zero-length packet. */
    uint32_t zero_length_termination;
    /* AOAHID_CHANNEL_READ_STREAM (zero) or AOAHID_CHANNEL_READ_REQUEST. */
    aoahid_channel_read_mode read_mode;
} aoahid_channel_options;

typedef struct aoahid_node_options {
    uint32_t struct_size;
    uint32_t reserved;
    /* Zero/zero selects the no-reservation fallback. To request a reservation,
     * set has_reserved_slots to one. A Node has one in-flight report, so its
     * reservation is limited to zero or one; positive reservations must leave
     * a shared Device-pool slot. */
    uint32_t has_reserved_slots;
    uint32_t reserved_slots;
} aoahid_node_options;

typedef struct aoahid_physical_properties {
    /* Disabled state is all-zero. When enabled, the four HID 1.11 Global
     * items are emitted for this field and reset immediately after its Main
     * item so they cannot leak into the next field. */
    uint32_t enabled;
    int32_t minimum;
    int32_t maximum;
    int32_t unit_exponent;
    uint32_t unit;
} aoahid_physical_properties;

typedef struct aoahid_integer_field {
    int32_t logical_minimum;
    int32_t logical_maximum;
    uint32_t bit_width;
    aoahid_physical_properties physical;
} aoahid_integer_field;

typedef struct aoahid_report_id_option {
    uint32_t enabled;
    uint8_t value;
    uint8_t reserved8[3];
} aoahid_report_id_option;

/* The Keyboard profile is exclusively full-NKRO: every declared Usage is a
 * one-bit Variable field, so every simultaneously pressed key is reported at
 * once with no Array slot count and no ErrorRollOver overflow encoding. */
typedef struct aoahid_keyboard_options {
    uint32_t struct_size;
    uint32_t reserved;
    aoahid_report_id_option report_id;
    uint16_t usage_minimum;
    uint16_t usage_maximum;
} aoahid_keyboard_options;

typedef struct aoahid_mouse_options {
    uint32_t struct_size;
    uint32_t reserved;
    aoahid_report_id_option report_id;
    uint32_t button_count;
    aoahid_integer_field x;
    aoahid_integer_field y;
    uint32_t enable_wheel;
    aoahid_integer_field wheel;
    uint32_t enable_pan;
    aoahid_integer_field pan;
} aoahid_mouse_options;

/* The four pointer arrays below each contain allowed_usage_count entries.
 * Every pointer and every expected-event string is required.
 *
 * application_page/application_usage select the Application Collection (for
 * example HUT 0x0C/0x01 Consumer Control, 0x01/0x80 System Control, or
 * 0x0B/0x01 Telephony Phone); field_page selects the Usage Page every entry
 * of allowed_usages is read from (for example Camera Control 0x90, nested
 * inside a Consumer Control Application Collection). A caller free to declare
 * any HUT page here is exactly what lets one factory reach Consumer, System
 * Control, Camera keys, Telephony keys, or another vendor/HUT page without a
 * dedicated function per page. Camera Control (0x90) fields are additionally
 * restricted to HUT 1.7's Auto-focus (0x20) and Shutter (0x21), each as a One
 * Shot Control, because that page defines no other Usage. */
typedef struct aoahid_toggle_options {
    uint32_t struct_size;
    uint32_t reserved;
    aoahid_report_id_option report_id;
    uint16_t application_page;
    uint16_t application_usage;
    uint16_t field_page;
    uint16_t reserved16;
    const uint16_t* allowed_usages;
    size_t allowed_usage_count;
    const aoahid_usage_semantic* usage_semantics;
    const char* const* expected_linux_event_types;
    const char* const* expected_linux_codes;
} aoahid_toggle_options;

typedef struct aoahid_gamepad_axis {
    aoahid_axis_role role;
    uint16_t usage_page;
    uint16_t usage;
    aoahid_integer_field value;
    int32_t neutral_value;
    const char* expected_linux_code;
    const char* expected_android_axis;
} aoahid_gamepad_axis;

typedef struct aoahid_gamepad_options {
    uint32_t struct_size;
    uint32_t reserved;
    aoahid_report_id_option report_id;
    const aoahid_gamepad_axis* axes;
    size_t axis_count;
    uint32_t button_count;
    uint16_t button_usage_minimum;
    /* Explicitly selects no D-pad, a Hat Switch, or the four Generic Desktop
     * D-pad OOC usages. The Android portable-candidate Hat requires caller-
     * explicit logical 0..7 in four bits; fields must be zero outside HAT. */
    aoahid_dpad_representation dpad_representation;
    int32_t hat_logical_minimum;
    int32_t hat_logical_maximum;
    uint32_t hat_bit_width;
} aoahid_gamepad_options;

/* Always the fixed-slot Multi-Touch report shape (explicit Contact
 * Identifier and Contact Count fields, one packet per declared slot unless
 * enable_multi_packet_frames selects more) under the Touch Screen Application
 * Collection Usage. */
typedef struct aoahid_touchscreen_options {
    uint32_t struct_size;
    uint32_t reserved;
    aoahid_report_id_option report_id;
    uint32_t maximum_contacts;
    uint32_t contacts_per_report;
    aoahid_integer_field contact_identifier;
    aoahid_integer_field x;
    aoahid_integer_field y;
    aoahid_integer_field contact_count;
    uint32_t enable_pressure;
    aoahid_integer_field pressure;
    uint32_t enable_width;
    aoahid_integer_field width;
    uint32_t enable_height;
    aoahid_integer_field height;
    uint32_t enable_azimuth;
    /* HUT Azimuth is counter-clockwise through a full circular range. The
     * audited Linux implementation at commit 35556bed836f8dc07ac55f69c8d17dce3e7f0e25
     * consumes samples in [0, logical_maximum). That half-open range is an
     * exact-revision implementation observation, not a HID/HUT rule or a claim
     * about another target. */
    aoahid_integer_field azimuth;
    uint32_t enable_scan_time;
    aoahid_integer_field scan_time;
    uint32_t scan_time_unit_100us;
    uint32_t enable_contact_count_maximum_feature_declaration;
    uint32_t enable_multi_packet_frames;
} aoahid_touchscreen_options;

/* Same fixed-slot Multi-Touch report shape as aoahid_touchscreen_options, under the
 * Digitizers / Touch Pad Application Collection Usage instead of Touch Screen,
 * plus button_count physical click buttons. button_count may be zero for a
 * buttonless clickpad; only then does aoahid_touchpad_button stay inert. */
typedef struct aoahid_touchpad_options {
    uint32_t struct_size;
    uint32_t reserved;
    aoahid_report_id_option report_id;
    uint32_t maximum_contacts;
    uint32_t contacts_per_report;
    aoahid_integer_field contact_identifier;
    aoahid_integer_field x;
    aoahid_integer_field y;
    aoahid_integer_field contact_count;
    uint32_t enable_pressure;
    aoahid_integer_field pressure;
    uint32_t enable_width;
    aoahid_integer_field width;
    uint32_t enable_height;
    aoahid_integer_field height;
    uint32_t enable_azimuth;
    /* HUT Azimuth is counter-clockwise through a full circular range. The
     * audited Linux implementation at commit 35556bed836f8dc07ac55f69c8d17dce3e7f0e25
     * consumes samples in [0, logical_maximum). That half-open range is an
     * exact-revision implementation observation, not a HID/HUT rule or a claim
     * about another target. */
    aoahid_integer_field azimuth;
    uint32_t enable_scan_time;
    aoahid_integer_field scan_time;
    uint32_t scan_time_unit_100us;
    uint32_t enable_contact_count_maximum_feature_declaration;
    uint32_t enable_multi_packet_frames;
    uint32_t button_count;
} aoahid_touchpad_options;

typedef struct aoahid_pen_options {
    uint32_t struct_size;
    uint32_t reserved;
    aoahid_report_id_option report_id;
    aoahid_pen_mode mode;
    aoahid_integer_field x;
    aoahid_integer_field y;
    uint32_t enable_pressure;
    aoahid_integer_field pressure;
    uint32_t enable_tilt;
    aoahid_integer_field tilt_x;
    aoahid_integer_field tilt_y;
    uint32_t enable_twist_target_specific;
    aoahid_integer_field twist;
    /* Exact Digitizers-page Usage IDs for caller-selected barrel controls.
     * HUT does not define these controls as a contiguous numeric range. */
    const uint16_t* barrel_usages;
    size_t barrel_usage_count;
    /* When enabled, the wire field is HUT Invert (0D/3C), the opposite-end
     * selector. It is not the separate Eraser Usage (0D/45). */
    uint32_t enable_eraser;
    uint32_t enable_hover;
} aoahid_pen_options;

typedef struct aoahid_battery_options {
    uint32_t struct_size;
    uint32_t reserved;
    aoahid_report_id_option report_id;
    aoahid_integer_field strength;
    uint32_t enable_unknown_null_state;
} aoahid_battery_options;

typedef struct aoahid_raw_report {
    uint8_t report_id;
    uint8_t has_report_id;
    uint16_t reserved16;
    uint32_t wire_length;
} aoahid_raw_report;

typedef struct aoahid_raw_options {
    uint32_t struct_size;
    uint32_t reserved;
    const uint8_t* descriptor;
    size_t descriptor_length;
    const aoahid_raw_report* reports;
    size_t report_count;
    uint32_t acknowledges_no_android_support;
    uint32_t requires_output;
    uint32_t requires_feature_response;
} aoahid_raw_options;

typedef struct aoahid_report_capability {
    uint8_t report_id;
    uint8_t has_report_id;
    uint16_t reserved16;
    uint32_t wire_length;
} aoahid_report_capability;

typedef struct aoahid_capability_manifest {
    uint32_t struct_size;
    uint32_t reserved;
    uint32_t input_supported;
    uint32_t output_supported;
    uint32_t feature_transport_supported;
    aoahid_android_status android_status;
    aoahid_profile_kind profile_kind;
    const aoahid_report_capability* reports;
    size_t report_count;
    size_t descriptor_bytes;
} aoahid_capability_manifest;

typedef struct aoahid_touch_contact {
    uint32_t contact_id;
    int32_t x;
    int32_t y;
    int32_t pressure;
    int32_t width;
    int32_t height;
    int32_t azimuth;
} aoahid_touch_contact;

/* Optional fields for aoahid_touch(); pass null to send zero/absent for every
 * field the Spec did not enable. Passing a value for a field the Spec left
 * disabled is rejected exactly as an out-of-range value would be. */
typedef struct aoahid_touch_extra {
    int32_t pressure;
    int32_t width;
    int32_t height;
    int32_t azimuth;
} aoahid_touch_extra;

typedef struct aoahid_pen_sample {
    uint32_t in_range;
    uint32_t tip;
    /* Selects the inverted/rubber end through HUT Invert (0D/3C). Tip may be
     * one at the same time to report rubber-end contact. */
    uint32_t eraser;
    uint32_t barrel_buttons;
    int32_t x;
    int32_t y;
    int32_t pressure;
    int32_t tilt_x;
    int32_t tilt_y;
    int32_t twist;
} aoahid_pen_sample;

/* aoahid_last_error
 * Ownership: Returns a borrowed pointer to the calling thread's record; its
 * strings and the record remain valid only until that thread's next public call.
 * Blocking: Does not block, allocate, perform I/O, or alter the record.
 * Synchronization: The record is thread-local; do not transfer its pointer to
 * another thread. Internal-thread completions retain diagnostics elsewhere.
 * Returns: Always returns a nonnull pointer. An unexpected ABI-boundary failure
 * returns the same pointer after replacing the record with AOAHID_ERR_INTERNAL. */
AOAHID_API const aoahid_error_detail* AOAHID_CALL aoahid_last_error(void);

/* aoahid_result_name
 * Ownership: Returns a borrowed, process-lifetime string; the caller frees nothing.
 * Blocking: Does not block, allocate, or perform I/O.
 * Synchronization: Has no handle domain and is safe on concurrent threads; only
 * the calling thread's diagnostic record is changed.
 * Returns: A known constant's name and a clear diagnostic, or the nonnull
 * sentinel "AOAHID_RESULT_UNKNOWN" with AOAHID_ERR_PARAM in last_error. An
 * unexpected failure returns "AOAHID_ERR_INTERNAL" and records that code. */
AOAHID_API const char* AOAHID_CALL aoahid_result_name(aoahid_result result);

/* aoahid_version
 * Ownership: Returns a value and creates no owned object.
 * Blocking: Does not block, allocate, or perform I/O.
 * Synchronization: Has no handle domain and is safe on concurrent threads; only
 * the calling thread's diagnostic record is changed.
 * Returns: The packed major/minor/patch value on success. Zero is the failure
 * sentinel and is accompanied by AOAHID_ERR_INTERNAL in last_error. */
AOAHID_API uint32_t AOAHID_CALL aoahid_version(void);

/* aoahid_context_create
 * Ownership: Borrows options for the call, but retains log_sink and log_user
 * until Context destruction. On success, out_context receives one caller-owned
 * Context; a nonnull output is set to null before validation and stays null on
 * failure.
 * Blocking: Initializes libusb, allocates storage, may start one event thread,
 * and may call the log sink; it performs no device control transfer.
 * Synchronization: Creates a new independent Context domain. The log sink must
 * not re-enter that Context and must tolerate the selected event mode.
 * Returns: AOAHID_OK; AOAHID_ERR_PARAM for a null output/options pointer or ABI
 * size mismatch; AOAHID_ERR_UNSET_FIELD for an invalid required mode/log field;
 * AOAHID_ERR_UNSUPPORTED for an unsupported libusb runtime/status;
 * AOAHID_ERR_ACCESS, AOAHID_ERR_BUSY, AOAHID_ERR_NO_DEVICE, AOAHID_ERR_STALL,
 * AOAHID_ERR_TIMEOUT, AOAHID_ERR_IO, or AOAHID_ERR_OVERFLOW for mapped libusb
 * initialization failures; AOAHID_ERR_INTERNAL for allocation, thread, or
 * unexpected ABI-boundary failure. */
AOAHID_API aoahid_result AOAHID_CALL aoahid_context_create(const aoahid_context_options* options,
                                                           aoahid_context** out_context);

/* aoahid_context_poll
 * Ownership: Borrows Context and consumes no handle.
 * Blocking: Runs libusb event handling for at most the requested wait before
 * callback/reap work; timeout_ms may be zero for a nonwaiting pump.
 * Synchronization: Belongs to the Context domain. It is valid only in
 * AOAHID_EVENT_CALLER_POLL, whose caller must serialize every Context operation;
 * callbacks execute synchronously in this call.
 * Returns: AOAHID_OK; AOAHID_ERR_PARAM for a null Context; AOAHID_ERR_UNSUPPORTED
 * for the wrong event mode or a mapped backend status; AOAHID_ERR_ACCESS,
 * AOAHID_ERR_BUSY, AOAHID_ERR_NO_DEVICE, AOAHID_ERR_STALL, AOAHID_ERR_TIMEOUT,
 * AOAHID_ERR_IO, or AOAHID_ERR_OVERFLOW for event-pump/deferred transport
 * failures; AOAHID_ERR_SHORT_TRANSFER for a deferred teardown transfer;
 * AOAHID_ERR_INTERNAL for allocation-free internal/event-thread failure. */
AOAHID_API aoahid_result AOAHID_CALL aoahid_context_poll(aoahid_context* context,
                                                         uint32_t timeout_ms);

/* aoahid_context_destroy
 * Ownership: AOAHID_CLOSE_PENDING leaves Context caller-owned and retryable.
 * AOAHID_ERR_PARAM for a null handle also consumes nothing. Every other result
 * from a valid call is terminal and consumes Context and all child graphs; the
 * pointer then becomes invalid and must never be passed to any API again.
 * Blocking: Performs bounded child close work, synchronous unregister requests,
 * graveyard reaping, and an event-thread join; it does not wait indefinitely for
 * outstanding callbacks and returns AOAHID_CLOSE_PENDING instead.
 * Synchronization: Belongs to the Context domain; the caller serializes it
 * against all other calls and must not invoke it from that Context's log sink.
 * Returns: AOAHID_OK; AOAHID_ERR_PARAM for a null Context (a pointer from an
 * earlier consuming call is invalid input and is not diagnostically detectable);
 * AOAHID_CLOSE_PENDING when ownership cannot yet be released;
 * AOAHID_ERR_UNSUPPORTED, AOAHID_ERR_ACCESS, AOAHID_ERR_BUSY,
 * AOAHID_ERR_NO_DEVICE, AOAHID_ERR_STALL, AOAHID_ERR_TIMEOUT,
 * AOAHID_ERR_SHORT_TRANSFER, AOAHID_ERR_IO, or AOAHID_ERR_OVERFLOW for retained
 * close/event/transport failure after terminal cleanup; AOAHID_ERR_INTERNAL for
 * an invariant, allocation, or unexpected ABI-boundary failure. */
AOAHID_API aoahid_result AOAHID_CALL aoahid_context_destroy(aoahid_context* context);

/* aoahid_context_destroy_blocking
 * Ownership: A null call or zero-deadline call consumes nothing.
 * AOAHID_ERR_TIMEOUT with
 * last_error field "context.destroy" and the deadline-expired diagnostic leaves
 * Context caller-owned. A previously retained transport/teardown timeout can
 * return the same code with its original field after terminal cleanup and does
 * consume Context. Every other result from a valid call is also terminal. After
 * consumption the pointer is invalid and must never be passed again.
 * Blocking: Repeatedly pumps or waits for teardown until timeout_ms; synchronous
 * control transfers and thread joining performed by teardown are part of the call.
 * Synchronization: Belongs to the Context domain; the caller serializes it
 * against all other calls and must not call it from the Context event/log thread.
 * Returns: AOAHID_OK; AOAHID_ERR_PARAM for a null Context or zero deadline;
 * AOAHID_ERR_TIMEOUT either for the retaining deadline case identified above or
 * for a terminal retained timeout whose diagnostic identifies its source;
 * AOAHID_ERR_UNSUPPORTED, AOAHID_ERR_ACCESS, AOAHID_ERR_BUSY,
 * AOAHID_ERR_NO_DEVICE, AOAHID_ERR_STALL, AOAHID_ERR_SHORT_TRANSFER,
 * AOAHID_ERR_IO, or AOAHID_ERR_OVERFLOW for a terminal close/event/transport
 * failure; AOAHID_ERR_INTERNAL for an invariant, allocation, or unexpected
 * ABI-boundary failure. */
AOAHID_API aoahid_result AOAHID_CALL aoahid_context_destroy_blocking(aoahid_context* context,
                                                                     uint32_t timeout_ms);

/* aoahid_discover
 * Ownership: Borrows Context; on success, out_discovery receives a caller-owned
 * snapshot containing every returned info/string. A nonnull output is set to
 * null before validation and stays null on failure.
 * Blocking: Enumerates USB devices, sends AOA request 51 to each one, and may
 * wait control_timeout_ms for each response; it allocates the snapshot. Only
 * devices that report a nonzero version are listed, each with that version in
 * protocol_version. HID needs version 2 or later; the caller decides.
 * Synchronization: Belongs to the Context domain and must be serialized with all
 * other calls on that Context. The resulting snapshot has its own lifetime.
 * Returns: AOAHID_OK; AOAHID_ERR_PARAM for a null argument or zero timeout;
 * AOAHID_ERR_UNSUPPORTED, AOAHID_ERR_ACCESS, AOAHID_ERR_BUSY,
 * AOAHID_ERR_NO_DEVICE, AOAHID_ERR_STALL, AOAHID_ERR_TIMEOUT, AOAHID_ERR_IO, or
 * AOAHID_ERR_OVERFLOW for enumeration, event, or deferred transport failure;
 * AOAHID_ERR_SHORT_TRANSFER for a deferred teardown transfer;
 * AOAHID_ERR_INTERNAL for allocation or unexpected ABI-boundary failure. */
AOAHID_API aoahid_result AOAHID_CALL aoahid_discover(aoahid_context* context,
                                                     uint32_t control_timeout_ms,
                                                     aoahid_discovery** out_discovery);

/* aoahid_discovery_count
 * Ownership: Borrows an immutable Discovery and creates no reference.
 * Blocking: Does not block, allocate, or perform I/O.
 * Synchronization: Concurrent read-only snapshot access is allowed; the caller
 * must serialize this call against aoahid_discovery_destroy.
 * Returns: The entry count. Zero is both a valid empty count and the failure
 * sentinel; last_error is AOAHID_OK for an empty result, AOAHID_ERR_PARAM for a
 * null handle, or AOAHID_ERR_INTERNAL for an unexpected ABI-boundary failure. */
AOAHID_API size_t AOAHID_CALL aoahid_discovery_count(const aoahid_discovery* discovery);

/* aoahid_discovery_get
 * Ownership: Returns a borrowed pointer whose nested pointers remain valid only
 * until the owning Discovery is destroyed; the caller frees nothing.
 * Blocking: Does not block, allocate, or perform I/O.
 * Synchronization: Concurrent read-only snapshot access is allowed; the caller
 * must serialize this call against aoahid_discovery_destroy.
 * Returns: The requested entry, or null as the failure sentinel with
 * AOAHID_ERR_PARAM for a null handle/out-of-range index or AOAHID_ERR_INTERNAL
 * for an unexpected ABI-boundary failure in last_error. */
AOAHID_API const aoahid_device_info* AOAHID_CALL
aoahid_discovery_get(const aoahid_discovery* discovery, size_t index);

/* aoahid_discovery_destroy
 * Ownership: Consumes the Discovery and invalidates all borrowed entries and
 * nested pointers. The Discovery pointer itself becomes invalid and must never
 * be passed again. A null argument consumes nothing.
 * Blocking: Does not perform I/O or wait for a Context; deallocation may run.
 * Synchronization: The caller must serialize destruction against all snapshot
 * readers; it belongs to the Discovery lifetime domain, not a Context domain.
 * Returns: Void. Success clears last_error; a null handle records
 * AOAHID_ERR_PARAM, and an unexpected ABI-boundary failure records
 * AOAHID_ERR_INTERNAL without a returned code. */
AOAHID_API void AOAHID_CALL aoahid_discovery_destroy(aoahid_discovery* discovery);

/* aoahid_accessory_start
 * Ownership: Borrows Context, selected, and options for the call; nothing is
 * retained and no handle is returned.
 * Blocking: Sends AOA request 51, request 52 for each non-null string, and
 * request 53 (START) on EP0, then closes the device again. It returns as soon
 * as request 53 completes and never waits for re-enumeration or retries. The
 * device then disconnects and reappears in accessory mode (VID 0x18D1, PID
 * 0x2D00-0x2D05); find it with aoahid_discover() and open it with
 * aoahid_device_open(). A selection that is already in accessory mode receives
 * no request. An open Device on the same device is probed through its own
 * handle and reports AOAHID_ERR_NO_DEVICE once the device disconnects.
 * Synchronization: Belongs to the parent Context domain and must be serialized
 * with every other application call touching that Context.
 * Returns: AOAHID_OK; AOAHID_ERR_PARAM for invalid pointers/ABI fields/identity;
 * AOAHID_ERR_UNSET_FIELD for a missing or empty manufacturer/model or a
 * nonzero reserved field; AOAHID_ERR_NOT_AOA when request 51 shows no AOA
 * support; AOAHID_ERR_OVERFLOW, before any request, for a string longer than
 * 256 bytes including its NUL (AOA 1.0) or an overlong port path; AOAHID_ERR_UNSUPPORTED,
 * AOAHID_ERR_ACCESS, AOAHID_ERR_BUSY, AOAHID_ERR_NO_DEVICE, AOAHID_ERR_STALL, AOAHID_ERR_TIMEOUT,
 * AOAHID_ERR_SHORT_TRANSFER, or AOAHID_ERR_IO for USB/open/control failure
 * (after a request-53 failure the device may still have switched);
 * AOAHID_ERR_INTERNAL for allocation or unexpected ABI-boundary failure. */
AOAHID_API aoahid_result AOAHID_CALL
aoahid_accessory_start(aoahid_context* context, const aoahid_device_info* selected,
                       const aoahid_accessory_options* options);

/* aoahid_device_open
 * Ownership: Borrows Context, selected, and options for the call; it copies
 * retained values and normalizes documented zero-valued tuning fields only in
 * that copy, never in caller storage. On success, out_device receives one Device
 * owned by the caller and parent Context. A nonnull output stays null on failure.
 * Blocking: Opens the selected device in its current USB mode, optionally
 * claims the exact selected interface, and allocates a fixed pool. It sends no
 * AOA request and never waits for re-enumeration, so selected may be filled by
 * the caller instead of aoahid_discover(). A device without AOA 2.0 HID support
 * opens normally and fails at aoahid_node_open() with AOAHID_ERR_STALL.
 * Synchronization: Belongs to the parent Context domain and must be serialized
 * with every other application call touching that Context.
 * Returns: AOAHID_OK; AOAHID_ERR_PARAM for invalid pointers/ABI fields/identity;
 * AOAHID_ERR_UNSET_FIELD for a missing required product policy;
 * AOAHID_ERR_UNSUPPORTED, AOAHID_ERR_ACCESS, AOAHID_ERR_BUSY,
 * AOAHID_ERR_NO_DEVICE, AOAHID_ERR_STALL, AOAHID_ERR_TIMEOUT, or AOAHID_ERR_IO
 * for USB open/claim failure;
 * AOAHID_ERR_OVERFLOW for port/wire/buffer policy overflow;
 * AOAHID_ERR_INTERNAL for allocation or unexpected ABI-boundary failure. */
AOAHID_API aoahid_result AOAHID_CALL aoahid_device_open(aoahid_context* context,
                                                        const aoahid_device_info* selected,
                                                        const aoahid_device_options* options,
                                                        aoahid_device** out_device);

/* aoahid_device_close
 * Ownership: A null call consumes nothing. The first valid call consumes Device
 * and every child Node and Channel for every returned result, including
 * AOAHID_CLOSE_PENDING; all those pointers become invalid and must never be
 * passed again.
 * Blocking: Bounded by configured drain/control timeouts while it completes
 * frames, sends neutral/unregister requests, cancels transfers, and runs callbacks.
 * Synchronization: Belongs to the parent Context domain and must be serialized
 * against every Device, Node, and Context call; never call it from the log sink.
 * Returns: AOAHID_OK; AOAHID_ERR_PARAM for null or for a still-live Device that
 * has no valid transport/Context or has already entered closing. A pointer from
 * an earlier consuming call is invalid input and is not diagnostically detectable;
 * AOAHID_CLOSE_PENDING when the consumed graph moves to the Context graveyard;
 * AOAHID_ERR_UNSUPPORTED, AOAHID_ERR_ACCESS, AOAHID_ERR_BUSY,
 * AOAHID_ERR_NO_DEVICE, AOAHID_ERR_STALL, AOAHID_ERR_TIMEOUT,
 * AOAHID_ERR_SHORT_TRANSFER, AOAHID_ERR_IO, or AOAHID_ERR_OVERFLOW for retained
 * teardown/transport failure after ownership is consumed; AOAHID_ERR_INTERNAL
 * for an invariant, allocation, or unexpected ABI-boundary failure. */
AOAHID_API aoahid_result AOAHID_CALL aoahid_device_close(aoahid_device* device);

/* aoahid_device_latched_error
 * Ownership: Borrows a live Device and consumes the reported non-NO_DEVICE
 * latch; AOAHID_ERR_NO_DEVICE remains sticky. No handle ownership changes.
 * Blocking: Does not perform USB I/O or wait for completion.
 * Synchronization: Belongs to the parent Context domain; serialize application
 * calls. Internal-thread completion may publish the latch concurrently.
 * Returns: AOAHID_OK when no Device or Context error is pending;
 * AOAHID_ERR_PARAM for an invalid/closing Device; AOAHID_ERR_UNSUPPORTED,
 * AOAHID_ERR_ACCESS, AOAHID_ERR_BUSY, AOAHID_ERR_NO_DEVICE, AOAHID_ERR_STALL,
 * AOAHID_ERR_TIMEOUT, AOAHID_ERR_SHORT_TRANSFER, AOAHID_ERR_IO, or
 * AOAHID_ERR_OVERFLOW for the retained completion/control/event error;
 * AOAHID_ERR_INTERNAL for a retained or unexpected internal failure. */
AOAHID_API aoahid_result AOAHID_CALL aoahid_device_latched_error(aoahid_device* device);

/* aoahid_channel_open
 * Ownership: Borrows Device and options for the call. On success, out_channel
 * receives one Channel owned by the caller and Device; it shares the Device's
 * USB handle. out_channel is null on every failure. aoahid_device_close also
 * closes every Channel still open on the Device.
 * Blocking: Waits up to the Device's close drain budget for in-flight report
 * transfers (in caller-poll mode driving libusb events), selects configuration
 * 1 only when the device is unconfigured (AOA 1.0), reads the active
 * configuration descriptor, claims the selected interface, allocates a fixed
 * transfer pool, and submits the read-ahead IN transfers; it does not wait for
 * data.
 * Synchronization: Belongs to the parent Context domain and must be serialized
 * with all Device, Node, Channel open/close, and Context application calls.
 * Returns: AOAHID_OK; AOAHID_ERR_PARAM for invalid handles/options or a closing
 * Device; AOAHID_ERR_UNSET_FIELD for a nonzero reserved field or a
 * zero_length_termination other than zero or one; AOAHID_ERR_UNSUPPORTED when
 * no interface has the class triple and a Bulk IN/OUT pair, or for a mapped
 * backend status; AOAHID_ERR_ACCESS, AOAHID_ERR_BUSY, AOAHID_ERR_NO_DEVICE, or
 * AOAHID_ERR_IO for descriptor, claim, or submit failure; AOAHID_ERR_TIMEOUT
 * when report transfers are still in flight at the drain budget, or any mapped
 * result for a caller-poll event-pump failure; AOAHID_ERR_OVERFLOW for a
 * transfer size above the libusb limit; AOAHID_ERR_INTERNAL for allocation or
 * unexpected failure. */
AOAHID_API aoahid_result AOAHID_CALL aoahid_channel_open(aoahid_device* device,
                                                         const aoahid_channel_options* options,
                                                         aoahid_channel** out_channel);

/* aoahid_channel_close
 * Ownership: Only AOAHID_OK consumes Channel. AOAHID_CLOSE_PENDING leaves the
 * lost Channel caller-owned for a retry or later Device close. After AOAHID_OK
 * the Channel pointer is invalid and must never be passed again.
 * Blocking: Cancels every transfer and waits up to the Device's close drain
 * budget for their completions, then, within the same budget, for in-flight
 * report transfers before releasing the interface; in caller-poll mode this
 * call drives libusb events.
 * Synchronization: Belongs to the parent Context domain; never run it
 * concurrently with a read or write on the same Channel.
 * Returns: AOAHID_OK; AOAHID_ERR_PARAM for null; AOAHID_CLOSE_PENDING when a
 * cancelled transfer misses the close budget; AOAHID_ERR_UNSUPPORTED,
 * AOAHID_ERR_ACCESS, AOAHID_ERR_BUSY, AOAHID_ERR_NO_DEVICE, AOAHID_ERR_STALL,
 * AOAHID_ERR_TIMEOUT, AOAHID_ERR_IO, or AOAHID_ERR_OVERFLOW for a caller-poll
 * event-pump failure; AOAHID_ERR_INTERNAL for unexpected failure. */
AOAHID_API aoahid_result AOAHID_CALL aoahid_channel_close(aoahid_channel* channel);

/* aoahid_channel_write
 * Ownership: Borrows data; bytes are copied into the Channel pool before
 * return. out_written receives the number of bytes queued on every return.
 * Blocking: Waits at most timeout_ms (zero never waits) for free OUT transfers,
 * then returns once every byte is submitted; it never waits for completion and
 * never retries. In caller-poll mode a wait drives libusb events.
 * Synchronization: Internal-thread mode: one thread may write while another
 * reads, concurrently with other Context calls, but never concurrently with
 * closing this Channel, its Device, or the Context. Caller-poll mode: belongs
 * to the parent Context domain. The call takes no lock and allocates nothing.
 * Returns: AOAHID_OK when every byte was queued; AOAHID_ERR_PARAM for invalid
 * pointers; AOAHID_ERR_TIMEOUT when the pool stayed full; AOAHID_ERR_NO_DEVICE
 * once the Channel is lost; AOAHID_ERR_STALL, AOAHID_ERR_TIMEOUT,
 * AOAHID_ERR_SHORT_TRANSFER, AOAHID_ERR_IO, or AOAHID_ERR_OVERFLOW for an
 * earlier failed write completion (reported once) or an event-pump failure;
 * AOAHID_ERR_INTERNAL for unexpected failure. */
AOAHID_API aoahid_result AOAHID_CALL aoahid_channel_write(aoahid_channel* channel,
                                                          const uint8_t* data, size_t length,
                                                          size_t* out_written, uint32_t timeout_ms);

/* aoahid_channel_read
 * Ownership: Borrows buffer; out_received receives the bytes copied on every
 * return.
 * Blocking: Waits at most timeout_ms (zero never waits) for received data. One
 * call may return part of a USB transfer or stop at a transfer boundary. In
 * AOAHID_CHANNEL_READ_REQUEST mode a call with nothing buffered first submits
 * one IN transfer sized from capacity; a timeout leaves it pending, and the
 * next call keeps waiting for it, so no byte is lost. Bytes beyond a smaller
 * later capacity stay buffered for the following calls. A zero-length packet
 * carries no data and is waited past. In caller-poll mode a wait drives libusb
 * events.
 * Synchronization: Same rule as aoahid_channel_write; at most one thread reads
 * a Channel at a time. The call takes no lock and allocates nothing.
 * Returns: AOAHID_OK with at least one byte; AOAHID_ERR_PARAM for invalid
 * pointers or zero capacity; AOAHID_ERR_TIMEOUT when no data arrived;
 * AOAHID_ERR_NO_DEVICE once the Channel is lost; AOAHID_ERR_STALL,
 * AOAHID_ERR_IO, or AOAHID_ERR_OVERFLOW for a failed IN transfer, which also
 * loses the Channel, or for an event-pump failure; AOAHID_ERR_INTERNAL for
 * unexpected failure. */
AOAHID_API aoahid_result AOAHID_CALL aoahid_channel_read(aoahid_channel* channel, uint8_t* buffer,
                                                         size_t capacity, size_t* out_received,
                                                         uint32_t timeout_ms);

/* aoahid_spec_create_keyboard
 * Ownership: Borrows options for the call and copies them. On success, out_spec
 * receives one caller-owned immutable reference; it is null on every failure.
 * Blocking: Performs validation and allocation but no I/O or waiting.
 * Synchronization: Has no Context domain and may run concurrently; out_spec and
 * the calling thread's diagnostic record are exclusive to the caller.
 * Returns: AOAHID_OK; AOAHID_ERR_PARAM for invalid pointers/ABI fields/ranges;
 * AOAHID_ERR_OVERFLOW for descriptor/layout size overflow; AOAHID_ERR_INTERNAL
 * for allocation, generation inconsistency, or unexpected exception. */
AOAHID_API aoahid_result AOAHID_CALL
aoahid_spec_create_keyboard(const aoahid_keyboard_options* options, aoahid_spec** out_spec);

/* aoahid_spec_create_mouse
 * Ownership: Borrows options for the call and copies them. On success, out_spec
 * receives one caller-owned immutable reference; it is null on every failure.
 * Blocking: Performs validation and allocation but no I/O or waiting.
 * Synchronization: Has no Context domain and may run concurrently; out_spec and
 * the calling thread's diagnostic record are exclusive to the caller.
 * Returns: AOAHID_OK; AOAHID_ERR_PARAM for invalid pointers/ABI fields, flags,
 * ranges, or disabled fields; AOAHID_ERR_OVERFLOW for descriptor/layout size
 * overflow; AOAHID_ERR_INTERNAL for allocation, generation inconsistency, or an
 * unexpected exception. */
AOAHID_API aoahid_result AOAHID_CALL aoahid_spec_create_mouse(const aoahid_mouse_options* options,
                                                              aoahid_spec** out_spec);

/* aoahid_spec_create_toggle
 * Ownership: Borrows options and all parallel arrays/strings for the call and
 * copies them. On success, out_spec receives one caller-owned immutable reference;
 * it is null on failure.
 * Blocking: Performs validation and allocation but no I/O or waiting.
 * Synchronization: Has no Context domain and may run concurrently; caller-owned
 * input/output storage must not be concurrently mutated.
 * Returns: AOAHID_OK; AOAHID_ERR_PARAM for invalid pointers/ABI fields/usages,
 * including a Camera Control field Usage outside Auto-focus/Shutter;
 * AOAHID_ERR_UNSET_FIELD for a missing Application Collection, field Usage
 * Page, allow-list, semantic, or evidence array; AOAHID_ERR_UNSUPPORTED for a
 * semantic press/release cannot represent, or for a Camera Control field
 * Usage not declared One Shot;
 * AOAHID_ERR_OVERFLOW for count/descriptor/layout overflow; AOAHID_ERR_INTERNAL
 * for allocation, generation inconsistency, or unexpected exception. */
AOAHID_API aoahid_result AOAHID_CALL aoahid_spec_create_toggle(const aoahid_toggle_options* options,
                                                               aoahid_spec** out_spec);

/* aoahid_spec_create_gamepad
 * Ownership: Borrows options, axes, and evidence strings for the call and copies
 * them. On success, out_spec receives one caller-owned immutable reference; it is
 * null on failure.
 * Blocking: Performs validation and allocation but no I/O or waiting.
 * Synchronization: Has no Context domain and may run concurrently; caller-owned
 * input/output storage must not be concurrently mutated.
 * Always the Generic Desktop / Game Pad Application Collection.
 * Returns: AOAHID_OK; AOAHID_ERR_PARAM for invalid pointers/ABI fields, axis,
 * button, or D-pad declarations; AOAHID_ERR_UNSET_FIELD for missing
 * axis width/evidence; AOAHID_ERR_OVERFLOW for count/Usage/descriptor/layout
 * overflow; AOAHID_ERR_INTERNAL for allocation, generation inconsistency, or an
 * unexpected exception. */
AOAHID_API aoahid_result AOAHID_CALL
aoahid_spec_create_gamepad(const aoahid_gamepad_options* options, aoahid_spec** out_spec);

/* aoahid_spec_create_touchscreen
 * Ownership: Borrows and copies options. On success, out_spec receives one
 * caller-owned immutable reference; it is null on every failure.
 * Blocking: Performs validation and allocation but no I/O or waiting.
 * Synchronization: Has no Context domain and may run concurrently; out_spec and
 * caller-owned options must not be concurrently mutated.
 * Always the fixed-slot Multi-Touch Touch Screen Application Collection.
 * Returns: AOAHID_OK; AOAHID_ERR_PARAM for invalid pointers/ABI fields, range,
 * count, or flag; AOAHID_ERR_OVERFLOW for button, descriptor, or layout
 * overflow; AOAHID_ERR_INTERNAL for allocation, generation inconsistency, or
 * unexpected exception. */
AOAHID_API aoahid_result AOAHID_CALL
aoahid_spec_create_touchscreen(const aoahid_touchscreen_options* options, aoahid_spec** out_spec);

/* aoahid_spec_create_touchpad
 * Ownership: Borrows and copies options. On success, out_spec receives one
 * caller-owned immutable reference; it is null on every failure.
 * Blocking: Performs validation and allocation but no I/O or waiting.
 * Synchronization: Has no Context domain and may run concurrently; out_spec and
 * caller-owned options must not be concurrently mutated.
 * Always the Digitizers / Touch Pad Application Collection. button_count may
 * be zero (buttonless clickpad).
 * Returns: AOAHID_OK; AOAHID_ERR_PARAM for invalid pointers/ABI fields, range,
 * count, or flag; AOAHID_ERR_OVERFLOW for button, descriptor, or layout
 * overflow; AOAHID_ERR_INTERNAL for allocation, generation inconsistency, or
 * unexpected exception. */
AOAHID_API aoahid_result AOAHID_CALL
aoahid_spec_create_touchpad(const aoahid_touchpad_options* options, aoahid_spec** out_spec);

/* aoahid_spec_create_pen
 * Ownership: Borrows options and barrel_usages for the call and copies them. On
 * success, out_spec receives one caller-owned immutable reference; it is null on
 * every failure.
 * Blocking: Performs validation and allocation but no I/O or waiting.
 * Synchronization: Has no Context domain and may run concurrently; caller-owned
 * input/output storage must not be concurrently mutated.
 * Returns: AOAHID_OK; AOAHID_ERR_PARAM for invalid pointers/ABI fields, mode,
 * fields, flags, or barrel usages; AOAHID_ERR_OVERFLOW for descriptor/layout
 * overflow; AOAHID_ERR_INTERNAL for allocation, generation inconsistency, or an
 * unexpected exception. */
AOAHID_API aoahid_result AOAHID_CALL aoahid_spec_create_pen(const aoahid_pen_options* options,
                                                            aoahid_spec** out_spec);

/* aoahid_spec_create_battery
 * Ownership: Borrows and copies options. On success, out_spec receives one
 * caller-owned immutable reference; it is null on every failure.
 * Blocking: Performs validation and allocation but no I/O or waiting.
 * Synchronization: Has no Context domain and may run concurrently; out_spec and
 * caller-owned options must not be concurrently mutated.
 * Returns: AOAHID_OK; AOAHID_ERR_PARAM for invalid pointers/ABI fields, range,
 * flag, or null-state declaration; AOAHID_ERR_OVERFLOW for descriptor/layout
 * overflow; AOAHID_ERR_INTERNAL for allocation, generation inconsistency, or an
 * unexpected exception. */
AOAHID_API aoahid_result AOAHID_CALL
aoahid_spec_create_battery(const aoahid_battery_options* options, aoahid_spec** out_spec);

/* aoahid_spec_create_raw
 * Ownership: Borrows descriptor and report table for the call and copies them.
 * On success, out_spec receives one caller-owned immutable reference; it is null
 * on every failure.
 * Blocking: Parses/validates and allocates but performs no I/O or waiting.
 * Synchronization: Has no Context domain and may run concurrently; caller-owned
 * input/output storage must not be concurrently mutated.
 * Returns: AOAHID_OK; AOAHID_ERR_PARAM for invalid pointers/ABI fields or HID
 * encoding/report-table mismatch; AOAHID_ERR_UNSET_FIELD for a missing descriptor
 * or report table; AOAHID_ERR_UNSUPPORTED for Output/Feature dependency, missing
 * acknowledgement, long items, or unsupported raw collection/range constructs;
 * AOAHID_ERR_OVERFLOW for descriptor/report/field/bit-count overflow;
 * AOAHID_ERR_INTERNAL for allocation or unexpected exception. */
AOAHID_API aoahid_result AOAHID_CALL aoahid_spec_create_raw(const aoahid_raw_options* options,
                                                            aoahid_spec** out_spec);

/* aoahid_spec_retain
 * Ownership: Requires the caller already to own a live reference. Adds one
 * caller-owned reference on success; the caller must later release it. A null or
 * saturated-count failure adds no reference.
 * Blocking: Does not allocate, perform I/O, or wait; it uses an atomic counter.
 * Synchronization: May run concurrently while the caller already owns a live
 * reference; never race a last release with a retain lacking its own reference.
 * Returns: Void. Success clears last_error; null records AOAHID_ERR_PARAM,
 * saturated reference count records AOAHID_ERR_OVERFLOW, and an unexpected
 * ABI-boundary failure records AOAHID_ERR_INTERNAL. */
AOAHID_API void AOAHID_CALL aoahid_spec_retain(aoahid_spec* spec);

/* aoahid_spec_release
 * Ownership: Requires and consumes exactly one caller-owned live reference.
 * When it was the final reference, Spec is destroyed and the pointer becomes
 * invalid and must never be passed again. A null call consumes nothing.
 * Blocking: Performs no I/O or waiting; final deallocation may run.
 * Synchronization: Atomic reference release may be concurrent when each caller
 * owns a distinct reference; final release must be serialized against all access.
 * Returns: Void. Success clears last_error; null records AOAHID_ERR_PARAM, and an
 * unexpected ABI-boundary failure records AOAHID_ERR_INTERNAL. A missing owned
 * reference is a caller lifetime violation, not a safely detectable result. */
AOAHID_API void AOAHID_CALL aoahid_spec_release(aoahid_spec* spec);

/* aoahid_spec_descriptor
 * Ownership: Borrows Spec and returns a borrowed immutable byte span valid while
 * that Spec reference remains alive; nonnull outputs are reset on failure.
 * Blocking: Does not block, allocate, or perform I/O.
 * Synchronization: Concurrent reads of immutable Spec are allowed while every
 * reader owns a reference; serialize against final release.
 * Returns: AOAHID_OK; AOAHID_ERR_PARAM for a null Spec/bytes/length pointer;
 * AOAHID_ERR_INTERNAL for an unexpected ABI-boundary failure. */
AOAHID_API aoahid_result AOAHID_CALL aoahid_spec_descriptor(const aoahid_spec* spec,
                                                            const uint8_t** bytes, size_t* length);

/* aoahid_spec_manifest
 * Ownership: Borrows Spec, fills caller-owned manifest storage, and returns
 * borrowed report-capability storage valid while Spec remains alive.
 * Blocking: Does not block, allocate, or perform I/O.
 * Synchronization: Concurrent reads of immutable Spec are allowed while every
 * reader owns a reference; each caller supplies distinct manifest storage.
 * Returns: AOAHID_OK; AOAHID_ERR_PARAM for a null pointer, wrong manifest
 * struct_size, or nonzero reserved field; AOAHID_ERR_INTERNAL for an unexpected
 * ABI-boundary failure. */
AOAHID_API aoahid_result AOAHID_CALL aoahid_spec_manifest(const aoahid_spec* spec,
                                                          aoahid_capability_manifest* manifest);

/* aoahid_node_open
 * Ownership: Borrows Device/options, retains Spec, and on success returns one
 * Node owned by the caller and Device. out_node is null on every failure.
 * Blocking: Allocates immutable state/pool reservation and synchronously sends
 * register request 54 plus descriptor fragments in request 56.
 * Note: Android registers the HID asynchronously after the last fragment, so a
 * report sent right after this returns may STALL or be lost unseen; wait
 * briefly before the first input (see docs/API.md).
 * Synchronization: Belongs to the parent Context domain and must be serialized
 * with all Device, Node, and Context application calls.
 * Returns: AOAHID_OK; AOAHID_ERR_PARAM for invalid handles/options/reservation;
 * AOAHID_ERR_UNSET_FIELD for missing reservation intent; AOAHID_ERR_UNSUPPORTED,
 * AOAHID_ERR_ACCESS, AOAHID_ERR_BUSY, AOAHID_ERR_NO_DEVICE, AOAHID_ERR_STALL,
 * AOAHID_ERR_TIMEOUT, AOAHID_ERR_SHORT_TRANSFER, or AOAHID_ERR_IO for deferred,
 * reservation, or registration transport failure; AOAHID_ERR_OVERFLOW for HID
 * ID, reference, descriptor, parser-policy, or report-policy overflow;
 * AOAHID_ERR_INTERNAL for allocation, state initialization, or unexpected failure. */
AOAHID_API aoahid_result AOAHID_CALL aoahid_node_open(aoahid_device* device, aoahid_spec* spec,
                                                      const aoahid_node_options* options,
                                                      aoahid_node** out_node);

/* aoahid_node_close
 * Ownership: Only AOAHID_OK consumes Node and its retained Spec reference. Every
 * error, including AOAHID_CLOSE_PENDING, leaves Node live and caller-owned for a
 * retry or later Device close. After AOAHID_OK the Node pointer is invalid and
 * must never be passed again.
 * Blocking: Waits up to the configured close drain budget, finishes any touch
 * frame, sends required neutral reports, and synchronously sends request 55.
 * Synchronization: Belongs to the parent Context domain and must be serialized
 * against all Context/Device/Node calls; never call it from the log sink.
 * Returns: AOAHID_OK; AOAHID_ERR_PARAM for null or for a still-live closed or
 * detached Node. A pointer consumed by an earlier AOAHID_OK is invalid input and
 * is not diagnostically detectable;
 * AOAHID_CLOSE_PENDING when a terminal callback misses the close budget;
 * AOAHID_ERR_UNSUPPORTED, AOAHID_ERR_ACCESS, AOAHID_ERR_BUSY,
 * AOAHID_ERR_NO_DEVICE, AOAHID_ERR_STALL, AOAHID_ERR_TIMEOUT,
 * AOAHID_ERR_SHORT_TRANSFER, AOAHID_ERR_IO, or AOAHID_ERR_OVERFLOW for deferred,
 * completion, neutral, unregister, or reservation failure; AOAHID_ERR_INTERNAL
 * for serialization, invariant, allocation, or unexpected failure. */
AOAHID_API aoahid_result AOAHID_CALL aoahid_node_close(aoahid_node* node);

/* aoahid_node_hid_id
 * Ownership: Borrows a live Node and creates no reference.
 * Blocking: Does not block, allocate, or perform I/O.
 * Synchronization: Belongs to the parent Context domain and must be serialized
 * against Node/Device teardown.
 * Returns: The nonzero AOA HID ID on success. Zero is the failure sentinel with
 * AOAHID_ERR_PARAM for a null/closed Node or AOAHID_ERR_INTERNAL for an
 * unexpected ABI-boundary failure in last_error. */
AOAHID_API uint16_t AOAHID_CALL aoahid_node_hid_id(const aoahid_node* node);

/* aoahid_node_manifest
 * Ownership: Borrows Node, fills caller-owned manifest storage, and returns a
 * borrowed report array valid until Node closes or its retained Spec is released.
 * Blocking: Does not block, allocate, or perform I/O.
 * Synchronization: Belongs to the parent Context domain; serialize against Node
 * or Device teardown and use distinct manifest storage per caller.
 * Returns: AOAHID_OK; AOAHID_ERR_PARAM for an invalid Node/manifest, wrong
 * struct_size, or nonzero reserved field; AOAHID_ERR_INTERNAL for an unexpected
 * ABI-boundary failure. */
AOAHID_API aoahid_result AOAHID_CALL aoahid_node_manifest(const aoahid_node* node,
                                                          aoahid_capability_manifest* manifest);

/* aoahid_node_submit
 * Ownership: Borrows Node; the library-owned pool copies/serializes the report,
 * so no caller buffer ownership changes.
 * Blocking: Does not wait for transfer completion. It may run one zero-timeout
 * caller-poll pump after acceptance and otherwise returns after queueing.
 * Synchronization: Belongs to the parent Context domain. Callers serialize state
 * and submit calls; internal-thread completion publishes concurrently.
 * Returns: AOAHID_OK for clean state or accepted submission; AOAHID_ERR_PARAM for
 * an invalid Node/internal report argument; AOAHID_ERR_UNSUPPORTED for Raw Node
 * use or mapped backend status; AOAHID_ERR_ACCESS, AOAHID_ERR_BUSY,
 * AOAHID_ERR_NO_DEVICE, AOAHID_ERR_STALL, AOAHID_ERR_TIMEOUT,
 * AOAHID_ERR_SHORT_TRANSFER, AOAHID_ERR_IO, or AOAHID_ERR_OVERFLOW for deferred,
 * prior-completion, pool, or submit failure; AOAHID_ERR_INTERNAL for serializer,
 * validation, allocation, or unexpected failure. */
AOAHID_API aoahid_result AOAHID_CALL aoahid_node_submit(aoahid_node* node);

/* aoahid_node_submit_blocking
 * Ownership: Borrows Node and changes no handle ownership.
 * Blocking: Pumps/waits until every dirty report fragment completes or the
 * explicit deadline expires; in caller-poll mode this call drives libusb events.
 * Synchronization: Belongs to the parent Context domain and must be serialized
 * against all Context/Device/Node application calls.
 * Returns: AOAHID_OK; AOAHID_ERR_PARAM for an invalid Node/report path;
 * AOAHID_ERR_UNSET_FIELD for a zero deadline; AOAHID_ERR_UNSUPPORTED,
 * AOAHID_ERR_ACCESS, AOAHID_ERR_BUSY, AOAHID_ERR_NO_DEVICE, AOAHID_ERR_STALL,
 * AOAHID_ERR_TIMEOUT, AOAHID_ERR_SHORT_TRANSFER, AOAHID_ERR_IO, or
 * AOAHID_ERR_OVERFLOW for deferred, completion, pool, event, or submit failure;
 * AOAHID_ERR_INTERNAL for serializer, validation, allocation, or unexpected failure. */
AOAHID_API aoahid_result AOAHID_CALL aoahid_node_submit_blocking(aoahid_node* node,
                                                                 uint32_t deadline_ms);

/* aoahid_kbd
 * Ownership: Borrows a Keyboard Node; no ownership changes.
 * Blocking: Does not block, allocate after Node open, log, or perform I/O.
 * Synchronization: Belongs to the parent Context domain; serialize mutation and
 * submission calls. It is rejected while one report is in flight.
 * down must be exactly zero or one: one presses usage, matching the prior
 * key_down; zero releases it, matching the prior key_up. There is no
 * release-all call; release every usage the caller pressed before closing the
 * Node, or the target may see it as stuck.
 * Returns: AOAHID_OK; AOAHID_ERR_PARAM for an invalid profile/Usage/Node/down;
 * AOAHID_ERR_BUSY for an in-flight report or an unsent opposite edge;
 * AOAHID_ERR_NO_DEVICE for sticky device loss; AOAHID_ERR_INTERNAL for an
 * unexpected ABI-boundary failure. */
AOAHID_API aoahid_result AOAHID_CALL aoahid_kbd(aoahid_node* node, uint16_t usage, uint32_t down);

/* aoahid_mouse_move
 * Ownership: Borrows a Mouse Node; no ownership changes.
 * Blocking: Does not block, allocate, log, or perform I/O.
 * Synchronization: Belongs to the parent Context domain; serialize mutation and
 * submission calls. It is rejected while one report is in flight.
 * Returns: AOAHID_OK; AOAHID_ERR_PARAM for an invalid profile/Node;
 * AOAHID_ERR_BUSY for an in-flight report; AOAHID_ERR_NO_DEVICE for sticky
 * device loss; AOAHID_ERR_OVERFLOW if accumulated int64_t deltas overflow;
 * AOAHID_ERR_INTERNAL for an unexpected ABI-boundary failure. */
AOAHID_API aoahid_result AOAHID_CALL aoahid_mouse_move(aoahid_node* node, int32_t dx, int32_t dy);

/* aoahid_mouse_scroll
 * Ownership: Borrows a Mouse Node; no ownership changes.
 * Blocking: Does not block, allocate, log, or perform I/O.
 * Synchronization: Belongs to the parent Context domain; serialize mutation and
 * submission calls. It is rejected while one report is in flight.
 * Returns: AOAHID_OK; AOAHID_ERR_PARAM for an invalid profile/Node;
 * AOAHID_ERR_UNSUPPORTED when a requested axis is absent; AOAHID_ERR_BUSY for an
 * in-flight report; AOAHID_ERR_NO_DEVICE for sticky loss; AOAHID_ERR_OVERFLOW if
 * accumulated int64_t scroll deltas overflow; AOAHID_ERR_INTERNAL for an
 * unexpected ABI-boundary failure. */
AOAHID_API aoahid_result AOAHID_CALL aoahid_mouse_scroll(aoahid_node* node, int32_t wheel,
                                                         int32_t pan);

/* aoahid_mouse_button
 * Ownership: Borrows a Mouse Node; no ownership changes.
 * Blocking: Does not block, allocate, log, or perform I/O.
 * Synchronization: Belongs to the parent Context domain; serialize mutation and
 * submission calls. It is rejected while one report is in flight.
 * Returns: AOAHID_OK; AOAHID_ERR_PARAM for invalid Node/profile, one-based button,
 * or pressed flag; AOAHID_ERR_BUSY for an in-flight report or an unsent opposite edge;
 * AOAHID_ERR_NO_DEVICE for sticky loss; AOAHID_ERR_INTERNAL for an unexpected
 * ABI-boundary failure. */
AOAHID_API aoahid_result AOAHID_CALL aoahid_mouse_button(aoahid_node* node, uint32_t button,
                                                         uint32_t pressed);

/* aoahid_toggle
 * Ownership: Borrows a Toggle Node; no ownership changes.
 * Blocking: Does not block, allocate, log, or perform I/O.
 * Synchronization: Belongs to the parent Context domain; serialize mutation and
 * submission calls. It is rejected while one report is in flight.
 * down must be exactly zero or one: one presses usage, which must be in the
 * Spec allow-list, matching the prior press; zero releases the currently
 * pressed Usage, and usage must then be either 0 (release whichever Usage is
 * pressed, for a caller that never tracked it) or that same Usage -- a
 * different nonzero Usage is rejected instead of silently releasing the
 * wrong one.
 * Returns: AOAHID_OK; AOAHID_ERR_PARAM for invalid Node/profile/down, a
 * Usage outside the Spec allow-list, or a release Usage that is neither 0 nor
 * the one currently pressed; AOAHID_ERR_BUSY for an in-flight report or an unsent opposite edge;
 * AOAHID_ERR_NO_DEVICE for sticky loss; AOAHID_ERR_INTERNAL for an unexpected
 * ABI-boundary failure. */
AOAHID_API aoahid_result AOAHID_CALL aoahid_toggle(aoahid_node* node, uint16_t usage,
                                                   uint32_t down);

/* aoahid_gamepad_button
 * Ownership: Borrows a Gamepad Node; no ownership changes.
 * Blocking: Does not block, allocate, log, or perform I/O.
 * Synchronization: Belongs to the parent Context domain; serialize mutation and
 * submission calls. It is rejected while one report is in flight.
 * Returns: AOAHID_OK; AOAHID_ERR_PARAM for invalid Node/profile, one-based button,
 * or pressed flag; AOAHID_ERR_BUSY for an in-flight report or an unsent opposite edge;
 * AOAHID_ERR_NO_DEVICE for sticky loss; AOAHID_ERR_INTERNAL for an unexpected
 * ABI-boundary failure. */
AOAHID_API aoahid_result AOAHID_CALL aoahid_gamepad_button(aoahid_node* node, uint32_t button,
                                                           uint32_t pressed);

/* aoahid_gamepad_set_axis
 * Ownership: Borrows a Gamepad Node; no ownership changes.
 * Blocking: Does not block, allocate, log, or perform I/O.
 * Synchronization: Belongs to the parent Context domain; serialize mutation and
 * submission calls. It is rejected while one report is in flight.
 * Returns: AOAHID_OK; AOAHID_ERR_PARAM for an invalid Node/profile/index/value;
 * AOAHID_ERR_BUSY for an in-flight report; AOAHID_ERR_NO_DEVICE for sticky loss;
 * AOAHID_ERR_INTERNAL for an unexpected ABI-boundary failure. */
AOAHID_API aoahid_result AOAHID_CALL aoahid_gamepad_set_axis(aoahid_node* node, size_t axis_index,
                                                             int32_t value);

/* aoahid_dpad
 * Ownership: Borrows a Gamepad Node; no ownership changes.
 * Blocking: Does not block, allocate, log, or perform I/O.
 * Synchronization: Belongs to the parent Context domain; serialize mutation and
 * submission calls. It is rejected while one report is in flight.
 * Derives either four independent D-pad OOC fields or the canonical eight-way
 * Hat value, matching whichever aoahid_dpad_representation the Spec selected.
 * Hat mode rejects opposite pairs and no direction emits Null State.
 * Returns: AOAHID_OK; AOAHID_ERR_PARAM unless the Spec selects a D-pad and all
 * four values form a valid boolean direction; AOAHID_ERR_BUSY for an in-flight
 * report or an unsent opposite edge; AOAHID_ERR_NO_DEVICE for sticky loss;
 * AOAHID_ERR_INTERNAL for an unexpected ABI-boundary failure. */
AOAHID_API aoahid_result AOAHID_CALL aoahid_dpad(aoahid_node* node, uint32_t up, uint32_t down,
                                                 uint32_t right, uint32_t left);

/* aoahid_touch
 * Ownership: Borrows a Touchscreen or Touchpad Node and extra for the call;
 * it copies the sample and changes no ownership.
 * Blocking: Does not block, allocate, log, or perform I/O.
 * Synchronization: Belongs to the parent Context domain; serialize mutation and
 * submission calls. It is rejected during an in-flight/multi-packet frame.
 * down must be exactly zero or one. One with a contact_id not currently active
 * places it in a new slot, matching the prior touch_down; one with a
 * contact_id already active moves it, matching the prior touch_move; zero
 * lifts an active contact_id at the given final x/y, matching the prior
 * touch_up. extra may be null, which sends zero/absent for every field the
 * Spec did not enable; a non-null field the Spec left disabled is rejected.
 * Returns: AOAHID_OK; AOAHID_ERR_PARAM for invalid Node/profile/down/contact
 * values, or an inactive contact_id on lift/move; AOAHID_ERR_BUSY for an
 * in-flight frame, an unsent opposite edge, or a duplicate active ID on
 * placement; AOAHID_ERR_NO_DEVICE for sticky loss; AOAHID_ERR_OVERFLOW when no
 * declared contact slot remains; AOAHID_ERR_INTERNAL for unexpected ABI-boundary
 * failure. */
AOAHID_API aoahid_result AOAHID_CALL aoahid_touch(aoahid_node* node, uint32_t contact_id,
                                                  uint32_t down, int32_t x, int32_t y,
                                                  const aoahid_touch_extra* extra);

/* aoahid_touchpad_button
 * Ownership: Borrows exactly a Touchpad Node; no ownership changes.
 * Blocking: Does not block, allocate, log, or perform I/O.
 * Synchronization: Belongs to the parent Context domain; serialize mutation and
 * submission calls. It is rejected during an in-flight/multi-packet frame.
 * Returns: AOAHID_OK; AOAHID_ERR_PARAM for invalid Node/profile, one-based button,
 * or pressed flag; AOAHID_ERR_BUSY for an in-flight frame or an unsent opposite edge;
 * AOAHID_ERR_NO_DEVICE for sticky loss; AOAHID_ERR_INTERNAL for unexpected
 * ABI-boundary failure. */
AOAHID_API aoahid_result AOAHID_CALL aoahid_touchpad_button(aoahid_node* node, uint32_t button,
                                                            uint32_t pressed);

/* aoahid_pen_update
 * Ownership: Borrows a Pen Node and sample for the call; it copies the sample and
 * changes no ownership.
 * Blocking: Does not block, allocate, log, or perform I/O.
 * Synchronization: Belongs to the parent Context domain; serialize mutation and
 * submission calls. It is rejected while one report is in flight.
 * Returns: AOAHID_OK; AOAHID_ERR_PARAM for invalid Node/profile/sample flags,
 * buttons, or values; AOAHID_ERR_BUSY for an in-flight report or an unsent opposite edge;
 * AOAHID_ERR_NO_DEVICE for sticky loss; AOAHID_ERR_INTERNAL for unexpected
 * ABI-boundary failure. */
AOAHID_API aoahid_result AOAHID_CALL aoahid_pen_update(aoahid_node* node,
                                                       const aoahid_pen_sample* sample);

/* aoahid_pen_depart
 * Ownership: Borrows a Pen Node; no ownership changes.
 * Blocking: Does not block, allocate, log, or perform I/O.
 * Synchronization: Belongs to the parent Context domain; serialize mutation and
 * submission calls. It is rejected while one report is in flight.
 * Returns: AOAHID_OK; AOAHID_ERR_PARAM for an invalid Node/profile;
 * AOAHID_ERR_BUSY for an in-flight report or an unsent opposite edge;
 * AOAHID_ERR_NO_DEVICE for sticky loss; AOAHID_ERR_INTERNAL for an unexpected
 * ABI-boundary failure. */
AOAHID_API aoahid_result AOAHID_CALL aoahid_pen_depart(aoahid_node* node);

/* aoahid_battery_update
 * Ownership: Borrows a Battery Node; no ownership changes.
 * Blocking: Does not block, allocate, log, or perform I/O.
 * Synchronization: Belongs to the parent Context domain; serialize mutation and
 * submission calls. It is rejected while one report is in flight.
 * Returns: AOAHID_OK; AOAHID_ERR_PARAM for an invalid Node/profile, nonboolean
 * presence flag, disallowed unknown state, or out-of-range strength;
 * AOAHID_ERR_BUSY for an in-flight report; AOAHID_ERR_NO_DEVICE for sticky loss;
 * AOAHID_ERR_INTERNAL for unexpected ABI-boundary failure. */
AOAHID_API aoahid_result AOAHID_CALL aoahid_battery_update(aoahid_node* node, uint32_t has_value,
                                                           int32_t strength);

/* aoahid_raw_submit
 * Ownership: Borrows a Raw Node and report; the bytes are copied into the
 * library-owned pool before return and no ownership changes.
 * Blocking: Does not wait for completion; it validates/copies and returns after
 * the asynchronous request-57 transfer is accepted or rejected.
 * Synchronization: Belongs to the parent Context domain. Callers serialize raw
 * submissions; internal-thread completion may publish concurrently.
 * Returns: AOAHID_OK for accepted submission; AOAHID_ERR_PARAM for invalid
 * Node/profile/buffer, unaccepted ID/length, or nonzero trailing padding;
 * AOAHID_ERR_UNSUPPORTED, AOAHID_ERR_ACCESS, AOAHID_ERR_BUSY,
 * AOAHID_ERR_NO_DEVICE, AOAHID_ERR_STALL, AOAHID_ERR_TIMEOUT,
 * AOAHID_ERR_SHORT_TRANSFER, AOAHID_ERR_IO, or AOAHID_ERR_OVERFLOW for deferred,
 * prior-completion, pool, or submit failure; AOAHID_ERR_INTERNAL for allocation
 * or unexpected ABI-boundary failure. */
AOAHID_API aoahid_result AOAHID_CALL aoahid_raw_submit(aoahid_node* node, const uint8_t* report,
                                                       size_t length);

#ifdef __cplusplus
}
#endif

#endif
