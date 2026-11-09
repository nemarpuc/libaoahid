// SPDX-License-Identifier: MIT
// Copyright (c) 2026 libaoahid contributors
/* Provides the minimal assertion surface shared by deterministic native tests;
 * it is test-only and never enters the library ABI. */
#pragma once

#include <cstddef>
#include <cstdio>

extern int g_failures;
// Every check that ran, failed or not; a runner prints it so a split or a
// moved test can be shown to run the same checks.
extern std::size_t g_checks;

#define AOAHID_CHECK(expression)                                                                   \
    do {                                                                                           \
        ++g_checks;                                                                                \
        if (!(expression)) {                                                                       \
            std::fprintf(stderr, "%s:%d: check failed: %s\n", __FILE__, __LINE__, #expression);    \
            ++g_failures;                                                                          \
        }                                                                                          \
    } while (false)

void test_item_writer();
void test_profiles();
void test_transport();
void test_identity();
void test_api_arguments();
void test_spec_options();
void test_report_state();
void test_transport_failures();
void test_raw_descriptor();
