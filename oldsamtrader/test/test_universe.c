/*
 * Copyright 2025 Samuel "Lord-Windy" Brown
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "samtrader/domain/ohlcv.h"
#include "samtrader/domain/universe.h"
#include "samtrader/ports/data_port.h"
#include "samtrader/samtrader.h"

#define ASSERT(cond, msg)                                                                          \
  do {                                                                                             \
    if (!(cond)) {                                                                                 \
      printf("FAIL: %s\n", msg);                                                                   \
      return 1;                                                                                    \
    }                                                                                              \
  } while (0)

/* --- Error capture for testing --- */

static SamtraderError last_error = SAMTRADER_ERROR_NONE;
static char last_error_msg[256] = {0};

static void test_error_callback(SamtraderError error, const char *message, void *user_data) {
  (void)user_data;
  last_error = error;
  strncpy(last_error_msg, message, sizeof(last_error_msg) - 1);
  last_error_msg[sizeof(last_error_msg) - 1] = '\0';
}

static void reset_error(void) {
  last_error = SAMTRADER_ERROR_NONE;
  last_error_msg[0] = '\0';
}

/* --- Mock data port --- */

typedef struct {
  const char **codes;
  size_t *bar_counts;
  size_t num_codes;
  Samrena *arena;
} MockDataPortImpl;

static SamrenaVector *mock_fetch_ohlcv(SamtraderDataPort *port, const char *code,
                                       const char *exchange, time_t start_date, time_t end_date) {
  (void)exchange;
  (void)start_date;
  (void)end_date;

  MockDataPortImpl *impl = (MockDataPortImpl *)port->impl;

  for (size_t i = 0; i < impl->num_codes; i++) {
    if (strcmp(impl->codes[i], code) == 0) {
      if (impl->bar_counts[i] == 0) {
        return NULL;
      }
      SamrenaVector *vec = samtrader_ohlcv_vector_create(impl->arena, impl->bar_counts[i]);
      if (!vec) {
        return NULL;
      }
      for (size_t j = 0; j < impl->bar_counts[i]; j++) {
        SamtraderOhlcv bar = {.code = code,
                              .exchange = exchange,
                              .date = (time_t)(1704067200 + j * 86400),
                              .open = 100.0,
                              .high = 105.0,
                              .low = 95.0,
                              .close = 102.0,
                              .volume = 10000};
        samrena_vector_push(vec, &bar);
      }
      return vec;
    }
  }

  return NULL;
}

static void mock_close(SamtraderDataPort *port) { (void)port; }

static SamtraderDataPort *create_mock_port(Samrena *arena, const char **codes, size_t *bar_counts,
                                           size_t num_codes) {
  SamtraderDataPort *port = SAMRENA_PUSH_TYPE_ZERO(arena, SamtraderDataPort);
  MockDataPortImpl *impl = SAMRENA_PUSH_TYPE_ZERO(arena, MockDataPortImpl);
  impl->codes = codes;
  impl->bar_counts = bar_counts;
  impl->num_codes = num_codes;
  impl->arena = arena;
  port->impl = impl;
  port->arena = arena;
  port->fetch_ohlcv = mock_fetch_ohlcv;
  port->close = mock_close;
  return port;
}

/* =========================== Parsing Tests =========================== */

static int test_parse_basic(void) {
  printf("Testing universe parse basic...\n");

  Samrena *arena = samrena_create_default();
  ASSERT(arena != NULL, "Failed to create arena");

  SamtraderUniverse *u = samtrader_universe_parse(arena, "CBA,BHP,WBC", "AU");
  ASSERT(u != NULL, "Parse returned NULL");
  ASSERT(u->count == 3, "Expected 3 codes");
  ASSERT(strcmp(u->codes[0], "CBA") == 0, "First code should be CBA");
  ASSERT(strcmp(u->codes[1], "BHP") == 0, "Second code should be BHP");
  ASSERT(strcmp(u->codes[2], "WBC") == 0, "Third code should be WBC");
  ASSERT(strcmp(u->exchange, "AU") == 0, "Exchange should be AU");

  samrena_destroy(arena);
  printf("  PASS\n");
  return 0;
}

static int test_parse_whitespace(void) {
  printf("Testing universe parse whitespace trimming...\n");

  Samrena *arena = samrena_create_default();
  ASSERT(arena != NULL, "Failed to create arena");

  SamtraderUniverse *u = samtrader_universe_parse(arena, "  CBA , BHP ,WBC,  NAB  ", "AU");
  ASSERT(u != NULL, "Parse returned NULL");
  ASSERT(u->count == 4, "Expected 4 codes");
  ASSERT(strcmp(u->codes[0], "CBA") == 0, "First code should be CBA");
  ASSERT(strcmp(u->codes[1], "BHP") == 0, "Second code should be BHP");
  ASSERT(strcmp(u->codes[2], "WBC") == 0, "Third code should be WBC");
  ASSERT(strcmp(u->codes[3], "NAB") == 0, "Fourth code should be NAB");

  samrena_destroy(arena);
  printf("  PASS\n");
  return 0;
}

static int test_parse_uppercase(void) {
  printf("Testing universe parse uppercasing...\n");

  Samrena *arena = samrena_create_default();
  ASSERT(arena != NULL, "Failed to create arena");

  SamtraderUniverse *u = samtrader_universe_parse(arena, "cba,bhp", "AU");
  ASSERT(u != NULL, "Parse returned NULL");
  ASSERT(u->count == 2, "Expected 2 codes");
  ASSERT(strcmp(u->codes[0], "CBA") == 0, "First code should be CBA");
  ASSERT(strcmp(u->codes[1], "BHP") == 0, "Second code should be BHP");

  samrena_destroy(arena);
  printf("  PASS\n");
  return 0;
}

static int test_parse_single(void) {
  printf("Testing universe parse single code...\n");

  Samrena *arena = samrena_create_default();
  ASSERT(arena != NULL, "Failed to create arena");

  SamtraderUniverse *u = samtrader_universe_parse(arena, "CBA", "AU");
  ASSERT(u != NULL, "Parse returned NULL");
  ASSERT(u->count == 1, "Expected 1 code");
  ASSERT(strcmp(u->codes[0], "CBA") == 0, "Code should be CBA");

  samrena_destroy(arena);
  printf("  PASS\n");
  return 0;
}

static int test_parse_empty_string(void) {
  printf("Testing universe parse empty string...\n");
  reset_error();

  Samrena *arena = samrena_create_default();
  ASSERT(arena != NULL, "Failed to create arena");

  SamtraderUniverse *u = samtrader_universe_parse(arena, "", "AU");
  ASSERT(u == NULL, "Should return NULL for empty string");
  ASSERT(strstr(last_error_msg, "no codes specified") != NULL, "Error should mention no codes");

  samrena_destroy(arena);
  printf("  PASS\n");
  return 0;
}

static int test_parse_empty_token(void) {
  printf("Testing universe parse empty token...\n");
  reset_error();

  Samrena *arena = samrena_create_default();
  ASSERT(arena != NULL, "Failed to create arena");

  SamtraderUniverse *u = samtrader_universe_parse(arena, "CBA,,BHP", "AU");
  ASSERT(u == NULL, "Should return NULL for empty token");
  ASSERT(strstr(last_error_msg, "empty code") != NULL, "Error should mention empty code");

  samrena_destroy(arena);
  printf("  PASS\n");
  return 0;
}

static int test_parse_duplicate(void) {
  printf("Testing universe parse duplicate detection...\n");
  reset_error();

  Samrena *arena = samrena_create_default();
  ASSERT(arena != NULL, "Failed to create arena");

  SamtraderUniverse *u = samtrader_universe_parse(arena, "CBA,BHP,CBA", "AU");
  ASSERT(u == NULL, "Should return NULL for duplicate");
  ASSERT(strstr(last_error_msg, "duplicate code: CBA") != NULL,
         "Error should mention duplicate CBA");

  samrena_destroy(arena);
  printf("  PASS\n");
  return 0;
}

static int test_parse_null_params(void) {
  printf("Testing universe parse null parameters...\n");
  reset_error();

  Samrena *arena = samrena_create_default();
  ASSERT(arena != NULL, "Failed to create arena");

  ASSERT(samtrader_universe_parse(NULL, "CBA", "AU") == NULL, "NULL arena should fail");
  ASSERT(last_error == SAMTRADER_ERROR_NULL_PARAM, "Should be NULL_PARAM error");

  reset_error();
  ASSERT(samtrader_universe_parse(arena, NULL, "AU") == NULL, "NULL codes_str should fail");
  ASSERT(last_error == SAMTRADER_ERROR_NULL_PARAM, "Should be NULL_PARAM error");

  reset_error();
  ASSERT(samtrader_universe_parse(arena, "CBA", NULL) == NULL, "NULL exchange should fail");
  ASSERT(last_error == SAMTRADER_ERROR_NULL_PARAM, "Should be NULL_PARAM error");

  samrena_destroy(arena);
  printf("  PASS\n");
  return 0;
}

static int test_parse_whitespace_only_token(void) {
  printf("Testing universe parse whitespace-only token...\n");
  reset_error();

  Samrena *arena = samrena_create_default();
  ASSERT(arena != NULL, "Failed to create arena");

  SamtraderUniverse *u = samtrader_universe_parse(arena, "CBA, ,BHP", "AU");
  ASSERT(u == NULL, "Should return NULL for whitespace-only token");
  ASSERT(strstr(last_error_msg, "empty code") != NULL, "Error should mention empty code");

  samrena_destroy(arena);
  printf("  PASS\n");
  return 0;
}

/* =========================== Validation Tests =========================== */

static int test_validate_all_valid(void) {
  printf("Testing universe validate all valid...\n");

  Samrena *arena = samrena_create_default();
  ASSERT(arena != NULL, "Failed to create arena");

  SamtraderUniverse *u = samtrader_universe_parse(arena, "CBA,BHP,WBC", "AU");
  ASSERT(u != NULL, "Parse failed");

  const char *mock_codes[] = {"CBA", "BHP", "WBC"};
  size_t mock_bars[] = {50, 50, 50};
  SamtraderDataPort *port = create_mock_port(arena, mock_codes, mock_bars, 3);

  int result = samtrader_universe_validate(u, port, 1704067200, 1709337600);
  ASSERT(result == 3, "All 3 codes should be valid");
  ASSERT(u->count == 3, "Universe count should be 3");

  samrena_destroy(arena);
  printf("  PASS\n");
  return 0;
}

static int test_validate_some_insufficient(void) {
  printf("Testing universe validate some insufficient...\n");

  Samrena *arena = samrena_create_default();
  ASSERT(arena != NULL, "Failed to create arena");

  SamtraderUniverse *u = samtrader_universe_parse(arena, "CBA,BHP,WBC", "AU");
  ASSERT(u != NULL, "Parse failed");

  const char *mock_codes[] = {"CBA", "BHP", "WBC"};
  size_t mock_bars[] = {50, 10, 40};
  SamtraderDataPort *port = create_mock_port(arena, mock_codes, mock_bars, 3);

  int result = samtrader_universe_validate(u, port, 1704067200, 1709337600);
  ASSERT(result == 2, "Should have 2 valid codes");
  ASSERT(u->count == 2, "Universe count should be 2");
  ASSERT(strcmp(u->codes[0], "CBA") == 0, "First remaining code should be CBA");
  ASSERT(strcmp(u->codes[1], "WBC") == 0, "Second remaining code should be WBC");

  samrena_destroy(arena);
  printf("  PASS\n");
  return 0;
}

static int test_validate_all_insufficient(void) {
  printf("Testing universe validate all insufficient...\n");

  Samrena *arena = samrena_create_default();
  ASSERT(arena != NULL, "Failed to create arena");

  SamtraderUniverse *u = samtrader_universe_parse(arena, "CBA,BHP,WBC", "AU");
  ASSERT(u != NULL, "Parse failed");

  const char *mock_codes[] = {"CBA", "BHP", "WBC"};
  size_t mock_bars[] = {5, 5, 5};
  SamtraderDataPort *port = create_mock_port(arena, mock_codes, mock_bars, 3);

  int result = samtrader_universe_validate(u, port, 1704067200, 1709337600);
  ASSERT(result == -1, "All insufficient should return -1");
  ASSERT(u->count == 0, "Universe count should be 0");

  samrena_destroy(arena);
  printf("  PASS\n");
  return 0;
}

static int test_validate_null_fetch(void) {
  printf("Testing universe validate with NULL fetch result...\n");

  Samrena *arena = samrena_create_default();
  ASSERT(arena != NULL, "Failed to create arena");

  SamtraderUniverse *u = samtrader_universe_parse(arena, "CBA,BHP", "AU");
  ASSERT(u != NULL, "Parse failed");

  /* BHP returns 0 bars (NULL vector) since it's not in mock codes */
  const char *mock_codes[] = {"CBA"};
  size_t mock_bars[] = {50};
  SamtraderDataPort *port = create_mock_port(arena, mock_codes, mock_bars, 1);

  int result = samtrader_universe_validate(u, port, 1704067200, 1709337600);
  ASSERT(result == 1, "Should have 1 valid code");
  ASSERT(u->count == 1, "Universe count should be 1");
  ASSERT(strcmp(u->codes[0], "CBA") == 0, "Remaining code should be CBA");

  samrena_destroy(arena);
  printf("  PASS\n");
  return 0;
}

static int test_validate_null_params(void) {
  printf("Testing universe validate null parameters...\n");

  Samrena *arena = samrena_create_default();
  ASSERT(arena != NULL, "Failed to create arena");

  SamtraderUniverse *u = samtrader_universe_parse(arena, "CBA", "AU");
  ASSERT(u != NULL, "Parse failed");

  const char *mock_codes[] = {"CBA"};
  size_t mock_bars[] = {50};
  SamtraderDataPort *port = create_mock_port(arena, mock_codes, mock_bars, 1);

  ASSERT(samtrader_universe_validate(NULL, port, 0, 0) == -1, "NULL universe should return -1");
  ASSERT(samtrader_universe_validate(u, NULL, 0, 0) == -1, "NULL port should return -1");

  samrena_destroy(arena);
  printf("  PASS\n");
  return 0;
}

/* =========================== Main =========================== */

int main(void) {
  printf("=== Universe Parsing & Validation Tests ===\n\n");

  samtrader_set_error_callback(test_error_callback, NULL);

  int failures = 0;

  /* Parsing tests */
  failures += test_parse_basic();
  failures += test_parse_whitespace();
  failures += test_parse_uppercase();
  failures += test_parse_single();
  failures += test_parse_empty_string();
  failures += test_parse_empty_token();
  failures += test_parse_duplicate();
  failures += test_parse_null_params();
  failures += test_parse_whitespace_only_token();

  /* Validation tests */
  failures += test_validate_all_valid();
  failures += test_validate_some_insufficient();
  failures += test_validate_all_insufficient();
  failures += test_validate_null_fetch();
  failures += test_validate_null_params();

  printf("\n=== Results: %d failures ===\n", failures);

  return failures > 0 ? EXIT_FAILURE : EXIT_SUCCESS;
}
