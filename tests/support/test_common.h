/**
 * @file tests/support/test_common.h
 * @brief Shared scaffolding for cmocka-based kernel unit tests.
 *
 * cmocka requires this exact include order: <stdarg.h>, <stddef.h>,
 * <setjmp.h>, <stdint.h>, then <cmocka.h>. Including this header first in
 * every test file keeps that contract in one place.
 */

#ifndef ALCOR2_TEST_COMMON_H
#define ALCOR2_TEST_COMMON_H

/* clang-format off */
#include <stdarg.h>
#include <stddef.h>
#include <setjmp.h>
#include <stdint.h>
#include <cmocka.h>
/* clang-format on */

#endif /* ALCOR2_TEST_COMMON_H */
