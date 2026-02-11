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

#include <samdata/samhashmap.h>
#include <samrena.h>
#include <samvector.h>

#include "samtrader/domain/code_data.h"
#include "samtrader/domain/indicator.h"
#include "samtrader/domain/ohlcv.h"
#include "samtrader/domain/rule.h"
#include "samtrader/domain/strategy.h"
#include "samtrader/ports/data_port.h"

#define ASSERT(cond, msg)                                                                          \
  do {                                                                                             \
    if (!(cond)) {                                                                                 \
      printf("FAIL: %s\n", msg);                                                                   \
      return 1;                                                                                    \
    }                                                                                              \
  } while (0)

/* Base epoch for test dates: 2024-01-01 00:00:00 UTC */
#define BASE_DATE 1704067200
#define DAY_SECONDS 86400

/* =========================== Mock Data Port =========================== */

typedef struct {
  const char **codes;
  size_t *bar_counts;
  time_t *start_dates; /* per-code start date offset (in days from BASE_DATE) */
  size_t num_codes;
  Samrena *arena;
} MockDataPortImpl;

static SamrenaVector *mock_fetch_ohlcv(SamtraderDataPort *port, const char *code,
                                       const char *exchange, time_t start_date, time_t end_date) {
  (void)start_date;
  (void)end_date;
  MockDataPortImpl *impl = (MockDataPortImpl *)port->impl;

  for (size_t i = 0; i < impl->num_codes; i++) {
    if (strcmp(impl->codes[i], code) == 0) {
      if (impl->bar_counts[i] == 0)
        return NULL;
      SamrenaVector *vec = samtrader_ohlcv_vector_create(impl->arena, impl->bar_counts[i]);
      if (!vec)
        return NULL;
      time_t start = BASE_DATE;
      if (impl->start_dates)
        start = BASE_DATE + impl->start_dates[i] * DAY_SECONDS;
      for (size_t j = 0; j < impl->bar_counts[i]; j++) {
        SamtraderOhlcv bar = {.code = code,
                              .exchange = exchange,
                              .date = start + (time_t)(j * DAY_SECONDS),
                              .open = 100.0 + (double)j,
                              .high = 105.0 + (double)j,
                              .low = 95.0 + (double)j,
                              .close = 102.0 + (double)j,
                              .volume = 10000 + (int64_t)j * 100};
        samrena_vector_push(vec, &bar);
      }
      return vec;
    }
  }
  return NULL;
}

static void mock_close(SamtraderDataPort *port) { (void)port; }

static SamtraderDataPort *create_mock_port(Samrena *arena, const char **codes, size_t *bar_counts,
                                           time_t *start_dates, size_t num_codes) {
  SamtraderDataPort *port = SAMRENA_PUSH_TYPE_ZERO(arena, SamtraderDataPort);
  MockDataPortImpl *impl = SAMRENA_PUSH_TYPE_ZERO(arena, MockDataPortImpl);
  impl->codes = codes;
  impl->bar_counts = bar_counts;
  impl->start_dates = start_dates;
  impl->num_codes = num_codes;
  impl->arena = arena;
  port->impl = impl;
  port->arena = arena;
  port->fetch_ohlcv = mock_fetch_ohlcv;
  port->close = mock_close;
  return port;
}

/* =========================== Date Timeline Tests =========================== */

static int test_timeline_single_code(void) {
  printf("Testing date timeline single code...\n");

  Samrena *arena = samrena_create_default();
  ASSERT(arena != NULL, "Failed to create arena");

  const char *codes[] = {"CBA"};
  size_t bars[] = {5};
  SamtraderDataPort *port = create_mock_port(arena, codes, bars, NULL, 1);

  SamtraderCodeData *cd = samtrader_load_code_data(arena, port, "CBA", "AU", 0, 0);
  ASSERT(cd != NULL, "Failed to load code data");

  SamtraderCodeData *cds[] = {cd};
  SamrenaVector *timeline = samtrader_build_date_timeline(arena, cds, 1);
  ASSERT(timeline != NULL, "Timeline should not be NULL");
  ASSERT(samrena_vector_size(timeline) == 5, "Expected 5 dates in timeline");

  /* Verify sorted ascending */
  for (size_t i = 1; i < samrena_vector_size(timeline); i++) {
    time_t prev = *(const time_t *)samrena_vector_at_const(timeline, i - 1);
    time_t curr = *(const time_t *)samrena_vector_at_const(timeline, i);
    ASSERT(prev < curr, "Timeline should be sorted ascending");
  }

  samrena_destroy(arena);
  printf("  PASS\n");
  return 0;
}

static int test_timeline_overlapping_dates(void) {
  printf("Testing date timeline overlapping dates...\n");

  Samrena *arena = samrena_create_default();
  ASSERT(arena != NULL, "Failed to create arena");

  /* CBA: days 0-4, BHP: days 2-6 (overlap on days 2,3,4) */
  const char *codes[] = {"CBA", "BHP"};
  size_t bars[] = {5, 5};
  time_t starts[] = {0, 2};
  SamtraderDataPort *port = create_mock_port(arena, codes, bars, starts, 2);

  SamtraderCodeData *cd1 = samtrader_load_code_data(arena, port, "CBA", "AU", 0, 0);
  SamtraderCodeData *cd2 = samtrader_load_code_data(arena, port, "BHP", "AU", 0, 0);
  ASSERT(cd1 != NULL && cd2 != NULL, "Failed to load code data");

  SamtraderCodeData *cds[] = {cd1, cd2};
  SamrenaVector *timeline = samtrader_build_date_timeline(arena, cds, 2);
  ASSERT(timeline != NULL, "Timeline should not be NULL");
  /* Union of {0,1,2,3,4} and {2,3,4,5,6} = {0,1,2,3,4,5,6} = 7 dates */
  ASSERT(samrena_vector_size(timeline) == 7, "Expected 7 unique dates");

  /* Verify sorted */
  for (size_t i = 1; i < samrena_vector_size(timeline); i++) {
    time_t prev = *(const time_t *)samrena_vector_at_const(timeline, i - 1);
    time_t curr = *(const time_t *)samrena_vector_at_const(timeline, i);
    ASSERT(prev < curr, "Timeline should be sorted ascending with no duplicates");
  }

  samrena_destroy(arena);
  printf("  PASS\n");
  return 0;
}

static int test_timeline_disjoint_dates(void) {
  printf("Testing date timeline disjoint dates...\n");

  Samrena *arena = samrena_create_default();
  ASSERT(arena != NULL, "Failed to create arena");

  /* CBA: days 0-2, BHP: days 10-12 (no overlap) */
  const char *codes[] = {"CBA", "BHP"};
  size_t bars[] = {3, 3};
  time_t starts[] = {0, 10};
  SamtraderDataPort *port = create_mock_port(arena, codes, bars, starts, 2);

  SamtraderCodeData *cd1 = samtrader_load_code_data(arena, port, "CBA", "AU", 0, 0);
  SamtraderCodeData *cd2 = samtrader_load_code_data(arena, port, "BHP", "AU", 0, 0);
  ASSERT(cd1 != NULL && cd2 != NULL, "Failed to load code data");

  SamtraderCodeData *cds[] = {cd1, cd2};
  SamrenaVector *timeline = samtrader_build_date_timeline(arena, cds, 2);
  ASSERT(timeline != NULL, "Timeline should not be NULL");
  ASSERT(samrena_vector_size(timeline) == 6, "Expected 6 dates (3 + 3, no overlap)");

  /* Verify sorted */
  for (size_t i = 1; i < samrena_vector_size(timeline); i++) {
    time_t prev = *(const time_t *)samrena_vector_at_const(timeline, i - 1);
    time_t curr = *(const time_t *)samrena_vector_at_const(timeline, i);
    ASSERT(prev < curr, "Timeline should be sorted ascending");
  }

  samrena_destroy(arena);
  printf("  PASS\n");
  return 0;
}

static int test_timeline_identical_dates(void) {
  printf("Testing date timeline identical dates...\n");

  Samrena *arena = samrena_create_default();
  ASSERT(arena != NULL, "Failed to create arena");

  /* Both codes have same dates (days 0-4) */
  const char *codes[] = {"CBA", "BHP"};
  size_t bars[] = {5, 5};
  time_t starts[] = {0, 0};
  SamtraderDataPort *port = create_mock_port(arena, codes, bars, starts, 2);

  SamtraderCodeData *cd1 = samtrader_load_code_data(arena, port, "CBA", "AU", 0, 0);
  SamtraderCodeData *cd2 = samtrader_load_code_data(arena, port, "BHP", "AU", 0, 0);
  ASSERT(cd1 != NULL && cd2 != NULL, "Failed to load code data");

  SamtraderCodeData *cds[] = {cd1, cd2};
  SamrenaVector *timeline = samtrader_build_date_timeline(arena, cds, 2);
  ASSERT(timeline != NULL, "Timeline should not be NULL");
  ASSERT(samrena_vector_size(timeline) == 5, "Expected 5 dates (no duplicates)");

  samrena_destroy(arena);
  printf("  PASS\n");
  return 0;
}

static int test_timeline_gaps(void) {
  printf("Testing date timeline with gaps (Mon-Fri vs Mon/Wed/Fri)...\n");

  Samrena *arena = samrena_create_default();
  ASSERT(arena != NULL, "Failed to create arena");

  /* CBA: days 0,1,2,3,4 (Mon-Fri), BHP: days 0,2,4 (Mon/Wed/Fri) */
  const char *codes[] = {"CBA", "BHP"};
  size_t bars[] = {5, 3};
  time_t starts_cba[] = {0, 0}; /* CBA starts day 0 */

  /* For BHP we need custom dates: 0,2,4 -- can't use the linear mock easily.
     Instead we'll build code_data manually for BHP. */
  SamtraderDataPort *port = create_mock_port(arena, codes, bars, starts_cba, 2);

  SamtraderCodeData *cd1 = samtrader_load_code_data(arena, port, "CBA", "AU", 0, 0);
  ASSERT(cd1 != NULL, "Failed to load CBA");

  /* Build BHP manually with days 0,2,4 */
  SamtraderCodeData *cd2 = SAMRENA_PUSH_TYPE_ZERO(arena, SamtraderCodeData);
  cd2->code = "BHP";
  cd2->exchange = "AU";
  cd2->ohlcv = samtrader_ohlcv_vector_create(arena, 3);
  time_t bhp_days[] = {0, 2, 4};
  for (int i = 0; i < 3; i++) {
    SamtraderOhlcv bar = {.code = "BHP",
                          .exchange = "AU",
                          .date = BASE_DATE + bhp_days[i] * DAY_SECONDS,
                          .open = 50.0,
                          .high = 55.0,
                          .low = 45.0,
                          .close = 52.0,
                          .volume = 5000};
    samrena_vector_push(cd2->ohlcv, &bar);
  }
  cd2->bar_count = 3;

  SamtraderCodeData *cds[] = {cd1, cd2};
  SamrenaVector *timeline = samtrader_build_date_timeline(arena, cds, 2);
  ASSERT(timeline != NULL, "Timeline should not be NULL");
  /* Union of {0,1,2,3,4} and {0,2,4} = {0,1,2,3,4} = 5 dates */
  ASSERT(samrena_vector_size(timeline) == 5, "Expected 5 dates (union)");

  samrena_destroy(arena);
  printf("  PASS\n");
  return 0;
}

static int test_timeline_null_params(void) {
  printf("Testing date timeline null parameters...\n");

  Samrena *arena = samrena_create_default();
  ASSERT(arena != NULL, "Failed to create arena");

  ASSERT(samtrader_build_date_timeline(NULL, NULL, 0) == NULL, "NULL arena should return NULL");
  ASSERT(samtrader_build_date_timeline(arena, NULL, 0) == NULL,
         "NULL code_data should return NULL");
  ASSERT(samtrader_build_date_timeline(arena, NULL, 1) == NULL,
         "NULL code_data with count should return NULL");

  SamtraderCodeData *cds[] = {NULL};
  ASSERT(samtrader_build_date_timeline(arena, cds, 0) == NULL, "Zero count should return NULL");

  samrena_destroy(arena);
  printf("  PASS\n");
  return 0;
}

static int test_timeline_one_empty_code(void) {
  printf("Testing date timeline one code with data + one empty...\n");

  Samrena *arena = samrena_create_default();
  ASSERT(arena != NULL, "Failed to create arena");

  const char *codes[] = {"CBA"};
  size_t bars[] = {5};
  SamtraderDataPort *port = create_mock_port(arena, codes, bars, NULL, 1);

  SamtraderCodeData *cd1 = samtrader_load_code_data(arena, port, "CBA", "AU", 0, 0);
  ASSERT(cd1 != NULL, "Failed to load CBA");

  /* Create empty code data for BHP */
  SamtraderCodeData *cd2 = SAMRENA_PUSH_TYPE_ZERO(arena, SamtraderCodeData);
  cd2->code = "BHP";
  cd2->exchange = "AU";
  cd2->ohlcv = samtrader_ohlcv_vector_create(arena, 1);
  cd2->bar_count = 0;

  SamtraderCodeData *cds[] = {cd1, cd2};
  SamrenaVector *timeline = samtrader_build_date_timeline(arena, cds, 2);
  ASSERT(timeline != NULL, "Timeline should not be NULL");
  ASSERT(samrena_vector_size(timeline) == 5, "Expected 5 dates from CBA only");

  samrena_destroy(arena);
  printf("  PASS\n");
  return 0;
}

/* =========================== Date Index Tests =========================== */

static int test_date_index_basic(void) {
  printf("Testing date index basic...\n");

  Samrena *arena = samrena_create_default();
  ASSERT(arena != NULL, "Failed to create arena");

  SamrenaVector *ohlcv = samtrader_ohlcv_vector_create(arena, 5);
  for (size_t i = 0; i < 5; i++) {
    SamtraderOhlcv bar = {.code = "CBA",
                          .exchange = "AU",
                          .date = BASE_DATE + (time_t)(i * DAY_SECONDS),
                          .open = 100.0,
                          .high = 105.0,
                          .low = 95.0,
                          .close = 102.0,
                          .volume = 10000};
    samrena_vector_push(ohlcv, &bar);
  }

  SamHashMap *idx = samtrader_build_date_index(arena, ohlcv);
  ASSERT(idx != NULL, "Date index should not be NULL");

  /* Check each date maps to the correct index */
  for (size_t i = 0; i < 5; i++) {
    char key[32];
    snprintf(key, sizeof(key), "%ld", (long)(BASE_DATE + (time_t)(i * DAY_SECONDS)));
    size_t *val = (size_t *)samhashmap_get(idx, key);
    ASSERT(val != NULL, "Date key should be found");
    ASSERT(*val == i, "Index should match bar position");
  }

  samrena_destroy(arena);
  printf("  PASS\n");
  return 0;
}

static int test_date_index_missing_date(void) {
  printf("Testing date index missing date lookup...\n");

  Samrena *arena = samrena_create_default();
  ASSERT(arena != NULL, "Failed to create arena");

  SamrenaVector *ohlcv = samtrader_ohlcv_vector_create(arena, 3);
  for (size_t i = 0; i < 3; i++) {
    SamtraderOhlcv bar = {.code = "CBA",
                          .exchange = "AU",
                          .date = BASE_DATE + (time_t)(i * DAY_SECONDS),
                          .open = 100.0,
                          .high = 105.0,
                          .low = 95.0,
                          .close = 102.0,
                          .volume = 10000};
    samrena_vector_push(ohlcv, &bar);
  }

  SamHashMap *idx = samtrader_build_date_index(arena, ohlcv);
  ASSERT(idx != NULL, "Date index should not be NULL");

  /* Look up a date that doesn't exist */
  char key[32];
  snprintf(key, sizeof(key), "%ld", (long)(BASE_DATE + 100 * DAY_SECONDS));
  size_t *val = (size_t *)samhashmap_get(idx, key);
  ASSERT(val == NULL, "Missing date should return NULL");

  samrena_destroy(arena);
  printf("  PASS\n");
  return 0;
}

static int test_date_index_null_params(void) {
  printf("Testing date index null parameters...\n");

  Samrena *arena = samrena_create_default();
  ASSERT(arena != NULL, "Failed to create arena");

  ASSERT(samtrader_build_date_index(NULL, NULL) == NULL, "NULL arena should return NULL");
  ASSERT(samtrader_build_date_index(arena, NULL) == NULL, "NULL ohlcv should return NULL");

  samrena_destroy(arena);
  printf("  PASS\n");
  return 0;
}

/* =========================== Code Data Loading Tests =========================== */

static int test_load_code_data_basic(void) {
  printf("Testing load code data basic...\n");

  Samrena *arena = samrena_create_default();
  ASSERT(arena != NULL, "Failed to create arena");

  const char *codes[] = {"CBA"};
  size_t bars[] = {50};
  SamtraderDataPort *port = create_mock_port(arena, codes, bars, NULL, 1);

  SamtraderCodeData *cd = samtrader_load_code_data(arena, port, "CBA", "AU", 0, 0);
  ASSERT(cd != NULL, "Code data should not be NULL");
  ASSERT(strcmp(cd->code, "CBA") == 0, "Code should be CBA");
  ASSERT(strcmp(cd->exchange, "AU") == 0, "Exchange should be AU");
  ASSERT(cd->ohlcv != NULL, "OHLCV should not be NULL");
  ASSERT(cd->bar_count == 50, "Bar count should be 50");
  ASSERT(cd->indicators == NULL, "Indicators should be NULL before computation");

  samrena_destroy(arena);
  printf("  PASS\n");
  return 0;
}

static int test_load_code_data_unknown(void) {
  printf("Testing load code data unknown code...\n");

  Samrena *arena = samrena_create_default();
  ASSERT(arena != NULL, "Failed to create arena");

  const char *codes[] = {"CBA"};
  size_t bars[] = {50};
  SamtraderDataPort *port = create_mock_port(arena, codes, bars, NULL, 1);

  SamtraderCodeData *cd = samtrader_load_code_data(arena, port, "UNKNOWN", "AU", 0, 0);
  ASSERT(cd == NULL, "Unknown code should return NULL");

  samrena_destroy(arena);
  printf("  PASS\n");
  return 0;
}

static int test_load_code_data_null_params(void) {
  printf("Testing load code data null parameters...\n");

  Samrena *arena = samrena_create_default();
  ASSERT(arena != NULL, "Failed to create arena");

  const char *codes[] = {"CBA"};
  size_t bars[] = {50};
  SamtraderDataPort *port = create_mock_port(arena, codes, bars, NULL, 1);

  ASSERT(samtrader_load_code_data(NULL, port, "CBA", "AU", 0, 0) == NULL,
         "NULL arena should return NULL");
  ASSERT(samtrader_load_code_data(arena, NULL, "CBA", "AU", 0, 0) == NULL,
         "NULL port should return NULL");
  ASSERT(samtrader_load_code_data(arena, port, NULL, "AU", 0, 0) == NULL,
         "NULL code should return NULL");
  ASSERT(samtrader_load_code_data(arena, port, "CBA", NULL, 0, 0) == NULL,
         "NULL exchange should return NULL");

  samrena_destroy(arena);
  printf("  PASS\n");
  return 0;
}

/* =========================== Indicator Pre-computation Tests =========================== */

static int test_compute_indicators(void) {
  printf("Testing indicator pre-computation (SMA cross strategy)...\n");

  Samrena *arena = samrena_create_default();
  ASSERT(arena != NULL, "Failed to create arena");

  /* Build 50-bar synthetic data */
  const char *codes[] = {"CBA"};
  size_t bars[] = {50};
  SamtraderDataPort *port = create_mock_port(arena, codes, bars, NULL, 1);

  SamtraderCodeData *cd = samtrader_load_code_data(arena, port, "CBA", "AU", 0, 0);
  ASSERT(cd != NULL, "Failed to load code data");

  /* Build SMA(5) cross_above SMA(10) strategy */
  SamtraderOperand sma5 = samtrader_operand_indicator(SAMTRADER_IND_SMA, 5);
  SamtraderOperand sma10 = samtrader_operand_indicator(SAMTRADER_IND_SMA, 10);

  SamtraderRule *entry =
      samtrader_rule_create_comparison(arena, SAMTRADER_RULE_CROSS_ABOVE, sma5, sma10);
  SamtraderRule *exit_rule =
      samtrader_rule_create_comparison(arena, SAMTRADER_RULE_CROSS_BELOW, sma5, sma10);
  ASSERT(entry != NULL && exit_rule != NULL, "Failed to create rules");

  SamtraderStrategy strategy = {.name = "SMA Cross",
                                .description = "Test",
                                .entry_long = entry,
                                .exit_long = exit_rule,
                                .entry_short = NULL,
                                .exit_short = NULL,
                                .position_size = 0.25,
                                .stop_loss_pct = 0.0,
                                .take_profit_pct = 0.0,
                                .max_positions = 1};

  int rc = samtrader_code_data_compute_indicators(arena, cd, &strategy);
  ASSERT(rc == 0, "Indicator computation should succeed");
  ASSERT(cd->indicators != NULL, "Indicators map should not be NULL");

  /* Verify SMA_5 and SMA_10 keys exist */
  SamtraderIndicatorSeries *sma5_series =
      (SamtraderIndicatorSeries *)samhashmap_get(cd->indicators, "SMA_5");
  ASSERT(sma5_series != NULL, "SMA_5 should be in indicators map");
  ASSERT(samtrader_indicator_series_size(sma5_series) == 50, "SMA_5 should have 50 values");

  SamtraderIndicatorSeries *sma10_series =
      (SamtraderIndicatorSeries *)samhashmap_get(cd->indicators, "SMA_10");
  ASSERT(sma10_series != NULL, "SMA_10 should be in indicators map");
  ASSERT(samtrader_indicator_series_size(sma10_series) == 50, "SMA_10 should have 50 values");

  samrena_destroy(arena);
  printf("  PASS\n");
  return 0;
}

/* =========================== Main =========================== */

int main(void) {
  printf("=== Code Data Tests ===\n\n");

  int failures = 0;

  /* Date timeline tests */
  failures += test_timeline_single_code();
  failures += test_timeline_overlapping_dates();
  failures += test_timeline_disjoint_dates();
  failures += test_timeline_identical_dates();
  failures += test_timeline_gaps();
  failures += test_timeline_null_params();
  failures += test_timeline_one_empty_code();

  /* Date index tests */
  failures += test_date_index_basic();
  failures += test_date_index_missing_date();
  failures += test_date_index_null_params();

  /* Code data loading tests */
  failures += test_load_code_data_basic();
  failures += test_load_code_data_unknown();
  failures += test_load_code_data_null_params();

  /* Indicator pre-computation test */
  failures += test_compute_indicators();

  printf("\n=== Results: %d failures ===\n", failures);

  return failures > 0 ? EXIT_FAILURE : EXIT_SUCCESS;
}
