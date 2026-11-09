# SPDX-License-Identifier: MIT
# Copyright (c) 2026 libaoahid contributors
cmake_minimum_required(VERSION 3.20)
project(aoahid_consumer_smoke LANGUAGES C)

set(AOAHID_FIND_COMPONENTS "" CACHE STRING
    "Semicolon-separated installed aoahid components requested by the smoke consumer.")
if(AOAHID_FIND_COMPONENTS)
  find_package(aoahid CONFIG REQUIRED COMPONENTS ${AOAHID_FIND_COMPONENTS})
else()
  find_package(aoahid CONFIG REQUIRED)
endif()

set(AOAHID_CONSUMER_TARGET "aoahid::aoahid" CACHE STRING
    "Installed libaoahid target exercised by this smoke consumer.")
if(NOT TARGET "${AOAHID_CONSUMER_TARGET}")
  message(FATAL_ERROR
      "Requested installed target does not exist: ${AOAHID_CONSUMER_TARGET}")
endif()

# One archive carries both components, so a smoke run states which flavour it
# must have linked and which other targets the same find_package() must define.
set(AOAHID_CONSUMER_EXPECTED_TYPE "" CACHE STRING
    "When set, the TYPE the consumer target must have (SHARED_LIBRARY or STATIC_LIBRARY).")
if(AOAHID_CONSUMER_EXPECTED_TYPE)
  get_target_property(_aoahid_consumer_type "${AOAHID_CONSUMER_TARGET}" TYPE)
  if(NOT _aoahid_consumer_type STREQUAL AOAHID_CONSUMER_EXPECTED_TYPE)
    message(FATAL_ERROR
        "${AOAHID_CONSUMER_TARGET} has TYPE ${_aoahid_consumer_type}, "
        "expected ${AOAHID_CONSUMER_EXPECTED_TYPE}")
  endif()
endif()
set(AOAHID_CONSUMER_REQUIRED_TARGETS "" CACHE STRING
    "Semicolon-separated installed targets that must also exist.")
foreach(_aoahid_required_target IN LISTS AOAHID_CONSUMER_REQUIRED_TARGETS)
  if(NOT TARGET "${_aoahid_required_target}")
    message(FATAL_ERROR
        "Required installed target does not exist: ${_aoahid_required_target}")
  endif()
endforeach()

add_executable(aoahid_consumer main.c)
target_link_libraries(aoahid_consumer PRIVATE "${AOAHID_CONSUMER_TARGET}")
set_target_properties(aoahid_consumer PROPERTIES
    C_STANDARD 99
    C_STANDARD_REQUIRED YES)
