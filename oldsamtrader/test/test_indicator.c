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

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "samtrader/domain/indicator.h"

#define ASSERT(cond, msg)                                                                          \
  do {                                                                                             \
    if (!(cond)) {                                                                                 \
      printf("FAIL: %s\n", msg);                                                                   \
      return 1;                                                                                    \
    }                                                                                              \
  } while (0)

#define ASSERT_DOUBLE_EQ(a, b, msg)                                                                \
  do {                                                                                             \
    if (fabs((a) - (b)) > 0.0001) {                                                                \
      printf("FAIL: %s (expected %f, got %f)\n", msg, (b), (a));                                   \
      return 1;                                                                                    \
    }                                                                                              \
  } while (0)

static int test_indicator_series_create(void) {
  printf("Testing samtrader_indicator_series_create...\n");

  Samrena *arena = samrena_create_default();
  ASSERT(arena != NULL, "Failed to create arena");

  SamtraderIndicatorSeries *series =
      samtrader_indicator_series_create(arena, SAMTRADER_IND_SMA, 20, 100);

  ASSERT(series != NULL, "Failed to create indicator series");
  ASSERT(series->type == SAMTRADER_IND_SMA, "Type mismatch");
  ASSERT(series->params.period == 20, "Period mismatch");
  ASSERT(series->values != NULL, "Values vector not created");
  ASSERT(samtrader_indicator_series_size(series) == 0, "Series should be empty");

  samrena_destroy(arena);
  printf("  PASS\n");
  return 0;
}

static int test_indicator_series_add_simple(void) {
  printf("Testing samtrader_indicator_add_simple...\n");

  Samrena *arena = samrena_create_default();
  ASSERT(arena != NULL, "Failed to create arena");

  SamtraderIndicatorSeries *series =
      samtrader_indicator_series_create(arena, SAMTRADER_IND_RSI, 14, 100);

  ASSERT(series != NULL, "Failed to create indicator series");

  for (int i = 0; i < 14; i++) {
    SamtraderIndicatorValue *val =
        samtrader_indicator_add_simple(series, 1704067200 + (i * 86400), 0.0, false);
    ASSERT(val != NULL, "Failed to add warmup value");
  }

  for (int i = 14; i < 30; i++) {
    double rsi_value = 50.0 + (i - 14) * 2.0;
    SamtraderIndicatorValue *val =
        samtrader_indicator_add_simple(series, 1704067200 + (i * 86400), rsi_value, true);
    ASSERT(val != NULL, "Failed to add value");
  }

  ASSERT(samtrader_indicator_series_size(series) == 30, "Series size should be 30");

  const SamtraderIndicatorValue *val = samtrader_indicator_series_at(series, 0);
  ASSERT(val != NULL, "Failed to get first value");
  ASSERT(val->valid == false, "First value should be invalid (warmup)");

  val = samtrader_indicator_series_at(series, 14);
  ASSERT(val != NULL, "Failed to get value at index 14");
  ASSERT(val->valid == true, "Value at index 14 should be valid");
  ASSERT_DOUBLE_EQ(val->data.simple.value, 50.0, "Value at index 14");

  double latest;
  bool found = samtrader_indicator_latest_simple(series, &latest);
  ASSERT(found, "Should find latest value");
  ASSERT_DOUBLE_EQ(latest, 50.0 + 15 * 2.0, "Latest value");

  samrena_destroy(arena);
  printf("  PASS\n");
  return 0;
}

static int test_macd_series(void) {
  printf("Testing samtrader_macd_series_create...\n");

  Samrena *arena = samrena_create_default();
  ASSERT(arena != NULL, "Failed to create arena");

  SamtraderIndicatorSeries *series = samtrader_macd_series_create(arena, 12, 26, 9, 100);

  ASSERT(series != NULL, "Failed to create MACD series");
  ASSERT(series->type == SAMTRADER_IND_MACD, "Type should be MACD");
  ASSERT(series->params.period == 12, "Fast period should be 12");
  ASSERT(series->params.param2 == 26, "Slow period should be 26");
  ASSERT(series->params.param3 == 9, "Signal period should be 9");

  SamtraderIndicatorValue *val =
      samtrader_indicator_add_macd(series, 1704067200, 1.5, 1.2, 0.3, true);
  ASSERT(val != NULL, "Failed to add MACD value");
  ASSERT(val->type == SAMTRADER_IND_MACD, "Value type should be MACD");
  ASSERT_DOUBLE_EQ(val->data.macd.line, 1.5, "MACD line");
  ASSERT_DOUBLE_EQ(val->data.macd.signal, 1.2, "MACD signal");
  ASSERT_DOUBLE_EQ(val->data.macd.histogram, 0.3, "MACD histogram");

  SamtraderMacdValue latest;
  bool found = samtrader_indicator_latest_macd(series, &latest);
  ASSERT(found, "Should find latest MACD value");
  ASSERT_DOUBLE_EQ(latest.line, 1.5, "Latest MACD line");
  ASSERT_DOUBLE_EQ(latest.signal, 1.2, "Latest MACD signal");

  samrena_destroy(arena);
  printf("  PASS\n");
  return 0;
}

static int test_bollinger_series(void) {
  printf("Testing samtrader_bollinger_series_create...\n");

  Samrena *arena = samrena_create_default();
  ASSERT(arena != NULL, "Failed to create arena");

  SamtraderIndicatorSeries *series = samtrader_bollinger_series_create(arena, 20, 2.0, 100);

  ASSERT(series != NULL, "Failed to create Bollinger series");
  ASSERT(series->type == SAMTRADER_IND_BOLLINGER, "Type should be BOLLINGER");
  ASSERT(series->params.period == 20, "Period should be 20");
  ASSERT_DOUBLE_EQ(series->params.param_double, 2.0, "Stddev multiplier");

  SamtraderIndicatorValue *val =
      samtrader_indicator_add_bollinger(series, 1704067200, 160.0, 150.0, 140.0, true);
  ASSERT(val != NULL, "Failed to add Bollinger value");
  ASSERT_DOUBLE_EQ(val->data.bollinger.upper, 160.0, "Bollinger upper");
  ASSERT_DOUBLE_EQ(val->data.bollinger.middle, 150.0, "Bollinger middle");
  ASSERT_DOUBLE_EQ(val->data.bollinger.lower, 140.0, "Bollinger lower");

  SamtraderBollingerValue latest;
  bool found = samtrader_indicator_latest_bollinger(series, &latest);
  ASSERT(found, "Should find latest Bollinger value");
  ASSERT_DOUBLE_EQ(latest.upper, 160.0, "Latest Bollinger upper");

  samrena_destroy(arena);
  printf("  PASS\n");
  return 0;
}

static int test_stochastic_series(void) {
  printf("Testing samtrader_stochastic_series_create...\n");

  Samrena *arena = samrena_create_default();
  ASSERT(arena != NULL, "Failed to create arena");

  SamtraderIndicatorSeries *series = samtrader_stochastic_series_create(arena, 14, 3, 100);

  ASSERT(series != NULL, "Failed to create Stochastic series");
  ASSERT(series->type == SAMTRADER_IND_STOCHASTIC, "Type should be STOCHASTIC");
  ASSERT(series->params.period == 14, "K period should be 14");
  ASSERT(series->params.param2 == 3, "D period should be 3");

  SamtraderIndicatorValue *val =
      samtrader_indicator_add_stochastic(series, 1704067200, 75.0, 70.0, true);
  ASSERT(val != NULL, "Failed to add Stochastic value");
  ASSERT_DOUBLE_EQ(val->data.stochastic.k, 75.0, "Stochastic K");
  ASSERT_DOUBLE_EQ(val->data.stochastic.d, 70.0, "Stochastic D");

  SamtraderStochasticValue latest;
  bool found = samtrader_indicator_latest_stochastic(series, &latest);
  ASSERT(found, "Should find latest Stochastic value");
  ASSERT_DOUBLE_EQ(latest.k, 75.0, "Latest Stochastic K");

  samrena_destroy(arena);
  printf("  PASS\n");
  return 0;
}

static int test_pivot_series(void) {
  printf("Testing samtrader_pivot_series_create...\n");

  Samrena *arena = samrena_create_default();
  ASSERT(arena != NULL, "Failed to create arena");

  SamtraderIndicatorSeries *series = samtrader_pivot_series_create(arena, 100);

  ASSERT(series != NULL, "Failed to create Pivot series");
  ASSERT(series->type == SAMTRADER_IND_PIVOT, "Type should be PIVOT");

  SamtraderIndicatorValue *val =
      samtrader_indicator_add_pivot(series, 1704067200, 150.0, 155.0, 160.0, 165.0, 145.0, 140.0,
                                    135.0, true);
  ASSERT(val != NULL, "Failed to add Pivot value");
  ASSERT_DOUBLE_EQ(val->data.pivot.pivot, 150.0, "Pivot point");
  ASSERT_DOUBLE_EQ(val->data.pivot.r1, 155.0, "R1");
  ASSERT_DOUBLE_EQ(val->data.pivot.r2, 160.0, "R2");
  ASSERT_DOUBLE_EQ(val->data.pivot.r3, 165.0, "R3");
  ASSERT_DOUBLE_EQ(val->data.pivot.s1, 145.0, "S1");
  ASSERT_DOUBLE_EQ(val->data.pivot.s2, 140.0, "S2");
  ASSERT_DOUBLE_EQ(val->data.pivot.s3, 135.0, "S3");

  SamtraderPivotValue latest;
  bool found = samtrader_indicator_latest_pivot(series, &latest);
  ASSERT(found, "Should find latest Pivot value");
  ASSERT_DOUBLE_EQ(latest.pivot, 150.0, "Latest Pivot point");

  samrena_destroy(arena);
  printf("  PASS\n");
  return 0;
}

static int test_indicator_type_name(void) {
  printf("Testing samtrader_indicator_type_name...\n");

  ASSERT(strcmp(samtrader_indicator_type_name(SAMTRADER_IND_SMA), "SMA") == 0, "SMA name");
  ASSERT(strcmp(samtrader_indicator_type_name(SAMTRADER_IND_EMA), "EMA") == 0, "EMA name");
  ASSERT(strcmp(samtrader_indicator_type_name(SAMTRADER_IND_RSI), "RSI") == 0, "RSI name");
  ASSERT(strcmp(samtrader_indicator_type_name(SAMTRADER_IND_MACD), "MACD") == 0, "MACD name");
  ASSERT(strcmp(samtrader_indicator_type_name(SAMTRADER_IND_BOLLINGER), "Bollinger") == 0,
         "Bollinger name");
  ASSERT(strcmp(samtrader_indicator_type_name(SAMTRADER_IND_ATR), "ATR") == 0, "ATR name");
  ASSERT(strcmp(samtrader_indicator_type_name(SAMTRADER_IND_STOCHASTIC), "Stochastic") == 0,
         "Stochastic name");
  ASSERT(strcmp(samtrader_indicator_type_name(SAMTRADER_IND_PIVOT), "Pivot") == 0, "Pivot name");

  printf("  PASS\n");
  return 0;
}

static int test_type_mismatch_rejection(void) {
  printf("Testing type mismatch rejection...\n");

  Samrena *arena = samrena_create_default();
  ASSERT(arena != NULL, "Failed to create arena");

  SamtraderIndicatorSeries *sma_series =
      samtrader_indicator_series_create(arena, SAMTRADER_IND_SMA, 20, 100);
  ASSERT(sma_series != NULL, "Failed to create SMA series");

  SamtraderIndicatorValue *val =
      samtrader_indicator_add_macd(sma_series, 1704067200, 1.0, 0.8, 0.2, true);
  ASSERT(val == NULL, "MACD add to SMA series should fail");

  val = samtrader_indicator_add_bollinger(sma_series, 1704067200, 160.0, 150.0, 140.0, true);
  ASSERT(val == NULL, "Bollinger add to SMA series should fail");

  samrena_destroy(arena);
  printf("  PASS\n");
  return 0;
}

int main(void) {
  printf("=== Indicator Data Structures Tests ===\n\n");

  int failures = 0;

  failures += test_indicator_series_create();
  failures += test_indicator_series_add_simple();
  failures += test_macd_series();
  failures += test_bollinger_series();
  failures += test_stochastic_series();
  failures += test_pivot_series();
  failures += test_indicator_type_name();
  failures += test_type_mismatch_rejection();

  printf("\n=== Results: %d failures ===\n", failures);

  return failures > 0 ? EXIT_FAILURE : EXIT_SUCCESS;
}
