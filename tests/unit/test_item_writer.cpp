// SPDX-License-Identifier: MIT
// Copyright (c) 2026 libaoahid contributors
/* The "item_writer" suite: HID short-item encoding, value ranges, and collection balance
 * in the generated-descriptor builder; the raw-validator tests of test_raw_validator.cpp
 * also run from here. No USB transport is involved. */
#include "hid/descriptor_builder.hpp"
#include "hid/item_writer.hpp"
#include "hid/validator.hpp"
#include "test.hpp"

#include <array>
#include <cstdint>

void test_raw_validator();

namespace {

void test_generated_collection_balance() {
    {
        aoa::hid::DescriptorBuilder builder(nullptr, 64U, nullptr);
        AOAHID_CHECK(!builder.begin_logical(0x01U, 0x01U));
        AOAHID_CHECK(!builder.good());
    }
    {
        aoa::hid::DescriptorBuilder builder(nullptr, 64U, nullptr);
        AOAHID_CHECK(!builder.end_collection());
        AOAHID_CHECK(!builder.good());
    }
    {
        aoa::hid::DescriptorBuilder builder(nullptr, 64U, nullptr);
        AOAHID_CHECK(builder.begin_application(0x01U, 0x06U));
        builder.finish();
        AOAHID_CHECK(!builder.good());
    }
    {
        aoa::hid::DescriptorBuilder builder(nullptr, 64U, nullptr);
        AOAHID_CHECK(builder.begin_application(0x01U, 0x06U));
        AOAHID_CHECK(builder.end_collection());
        builder.finish();
        AOAHID_CHECK(builder.good());
    }
    {
        aoa::hid::DescriptorBuilder builder(nullptr, 64U, nullptr);
        AOAHID_CHECK(builder.begin_application(0x01U, 0x06U));
        AOAHID_CHECK(builder.set_report_id(true, 1U));
        AOAHID_CHECK(!builder.set_report_id(false, 0U));
        AOAHID_CHECK(!builder.good());
    }
    {
        aoa::hid::DescriptorBuilder builder(nullptr, 64U, nullptr);
        AOAHID_CHECK(builder.begin_application(0x01U, 0x06U));
        AOAHID_CHECK(builder.set_report_id(true, 1U));
        AOAHID_CHECK(!builder.set_report_id(true, 2U));
        AOAHID_CHECK(!builder.good());
    }
    {
        aoa::hid::DescriptorBuilder builder(nullptr, 64U, nullptr);
        AOAHID_CHECK(builder.begin_application(0x01U, 0x06U));
        AOAHID_CHECK(!builder.constant_padding(8U));
        AOAHID_CHECK(!builder.good());
    }
    {
        std::array<std::uint8_t, 64U> descriptor{};
        aoa::hid::ReportLayout layout{};
        aoa::hid::DescriptorBuilder builder(descriptor.data(), descriptor.size(), &layout);
        AOAHID_CHECK(builder.begin_application(0x01U, 0x06U));
        AOAHID_CHECK(builder.set_report_id(true, 1U));
        AOAHID_CHECK(builder.constant_padding(8U));
        AOAHID_CHECK(builder.end_collection());
        builder.finish();
        AOAHID_CHECK(builder.good());
        AOAHID_CHECK(
            aoa::hid::validate_generated(descriptor.data(), builder.descriptor_size(), layout)
                .result == AOAHID_OK);
        descriptor[7U] = 2U;
        AOAHID_CHECK(
            aoa::hid::validate_generated(descriptor.data(), builder.descriptor_size(), layout)
                .result == AOAHID_ERR_INTERNAL);
    }
    {
        std::array<std::uint8_t, 128U> descriptor{};
        aoa::hid::ReportLayout layout{};
        aoa::hid::DescriptorBuilder builder(descriptor.data(), descriptor.size(), &layout);
        AOAHID_CHECK(builder.begin_application(0x01U, 0x06U));
        AOAHID_CHECK(builder.set_report_id(false, 0U));
        AOAHID_CHECK(builder.feature_static_value(0x01U, 0x01U, 0, 1, 1U));
        AOAHID_CHECK(builder.feature_static_value(0x01U, 0x02U, 0, 1, 32U));
        AOAHID_CHECK(builder.end_collection());
        builder.finish();
        AOAHID_CHECK(builder.good());
        // The 32-bit Feature begins at bit one and therefore spans five
        // bytes, which HID 1.11 section 8.4 forbids just as it does for Input.
        AOAHID_CHECK(
            aoa::hid::validate_generated(descriptor.data(), builder.descriptor_size(), layout)
                .result == AOAHID_ERR_INTERNAL);
    }
}

} // namespace

void test_item_writer() {
    {
        std::array<std::uint8_t, 8> bytes{};
        aoa::hid::ItemWriter writer(bytes.data(), bytes.size());
        AOAHID_CHECK(writer.logical_minimum(128));
        AOAHID_CHECK(writer.size() == 3U);
        AOAHID_CHECK(bytes[0] == 0x16U && bytes[1] == 0x80U && bytes[2] == 0x00U);
    }
    {
        std::array<std::uint8_t, 8> bytes{};
        aoa::hid::ItemWriter writer(bytes.data(), bytes.size());
        AOAHID_CHECK(writer.logical_minimum(0));
        AOAHID_CHECK(writer.logical_maximum(65535, 0));
        AOAHID_CHECK(writer.size() == 5U);
        AOAHID_CHECK(bytes[0] == 0x15U && bytes[1] == 0x00U);
        AOAHID_CHECK(bytes[2] == 0x26U && bytes[3] == 0xFFU && bytes[4] == 0xFFU);
    }
    {
        std::array<std::uint8_t, 4> bytes{};
        aoa::hid::ItemWriter writer(bytes.data(), bytes.size());
        AOAHID_CHECK(writer.unit_exponent(-4));
        AOAHID_CHECK(writer.size() == 2U);
        AOAHID_CHECK(bytes[0] == 0x55U && bytes[1] == 0x0CU);
    }
    {
        std::array<std::uint8_t, 1U> bytes{0xA5U};
        aoa::hid::ItemWriter writer(bytes.data(), bytes.size());
        AOAHID_CHECK(!writer.usage(0x100U));
        AOAHID_CHECK(writer.size() == 0U);
        AOAHID_CHECK(bytes[0] == 0xA5U);
        AOAHID_CHECK(!writer.push());
        AOAHID_CHECK(writer.size() == 0U);
        AOAHID_CHECK(bytes[0] == 0xA5U);
    }
    AOAHID_CHECK(aoa::hid::value_fits(0, 7, 4U));
    AOAHID_CHECK(!aoa::hid::value_fits(-9, 7, 4U));
    // The generated-profile API carries signed int32 extrema; raw HID item
    // signedness is tested separately through validate_raw().
    AOAHID_CHECK(!aoa::hid::value_fits(0, -1, 32U));
    test_raw_validator();
    test_generated_collection_balance();
}
