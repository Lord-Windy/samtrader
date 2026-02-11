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
#include "samtrader/domain/ohlcv.h"
#include "samtrader/domain/rule.h"

#define ASSERT(cond, msg)                                                                          \
  do {                                                                                             \
    if (!(cond)) {                                                                                 \
      printf("FAIL: %s\n", msg);                                                                   \
      return 1;                                                                                    \
    }                                                                                              \
  } while (0)

/*============================================================================
 * Test Helpers
 *============================================================================*/

/** Create test OHLCV data with given close prices. Open=close-1, high=close+1, low=close-2. */
static SamrenaVector *make_ohlcv(Samrena *arena, const double *closes, size_t count) {
  SamrenaVector *vec = samrena_vector_init(arena, sizeof(SamtraderOhlcv), count);
  for (size_t i = 0; i < count; i++) {
    SamtraderOhlcv bar = {
        .code = "TEST",
        .exchange = "US",
        .date = (time_t)(1000000 + i * 86400),
        .open = closes[i] - 1.0,
        .high = closes[i] + 1.0,
        .low = closes[i] - 2.0,
        .close = closes[i],
        .volume = (int64_t)(1000 * (i + 1)),
    };
    samrena_vector_push(vec, &bar);
  }
  return vec;
}

/** Create a simple indicator series with given values (all valid). */
static SamtraderIndicatorSeries *make_simple_series(Samrena *arena, SamtraderIndicatorType type,
                                                    int period, const double *values,
                                                    size_t count) {
  SamtraderIndicatorSeries *series = samtrader_indicator_series_create(arena, type, period, count);
  for (size_t i = 0; i < count; i++) {
    samtrader_indicator_add_simple(series, (time_t)(1000000 + i * 86400), values[i], true);
  }
  return series;
}

/** Create a Bollinger series with given upper/middle/lower values. */
static SamtraderIndicatorSeries *make_bollinger_series(Samrena *arena, int period, double stddev,
                                                       const double *upper, const double *middle,
                                                       const double *lower, size_t count) {
  SamtraderIndicatorSeries *series =
      samtrader_bollinger_series_create(arena, period, stddev, count);
  for (size_t i = 0; i < count; i++) {
    samtrader_indicator_add_bollinger(series, (time_t)(1000000 + i * 86400), upper[i], middle[i],
                                      lower[i], true);
  }
  return series;
}

/** Create a Pivot series. */
static SamtraderIndicatorSeries *make_pivot_series(Samrena *arena, const double *pivots,
                                                   const double *r1, size_t count) {
  SamtraderIndicatorSeries *series = samtrader_pivot_series_create(arena, count);
  for (size_t i = 0; i < count; i++) {
    samtrader_indicator_add_pivot(series, (time_t)(1000000 + i * 86400), pivots[i], r1[i],
                                  r1[i] + 5.0, r1[i] + 10.0, pivots[i] - 5.0, pivots[i] - 10.0,
                                  pivots[i] - 15.0, true);
  }
  return series;
}

/** Create a MACD series. */
static SamtraderIndicatorSeries *make_macd_series(Samrena *arena, int fast, int slow, int signal,
                                                  const double *lines, size_t count) {
  SamtraderIndicatorSeries *series = samtrader_macd_series_create(arena, fast, slow, signal, count);
  for (size_t i = 0; i < count; i++) {
    samtrader_indicator_add_macd(series, (time_t)(1000000 + i * 86400), lines[i], lines[i] * 0.8,
                                 lines[i] * 0.2, true);
  }
  return series;
}

/** Put an indicator series into the hashmap using the standard key. */
static void put_indicator(SamHashMap *map, const SamtraderOperand *op,
                          SamtraderIndicatorSeries *series) {
  char key[64];
  samtrader_operand_indicator_key(key, sizeof(key), op);
  samhashmap_put(map, key, series);
}

/*============================================================================
 * Indicator Key Tests
 *============================================================================*/

static int test_indicator_key_simple(void) {
  printf("Testing indicator key generation (simple)...\n");

  char buf[64];

  SamtraderOperand op = samtrader_operand_indicator(SAMTRADER_IND_SMA, 20);
  ASSERT(samtrader_operand_indicator_key(buf, sizeof(buf), &op) > 0, "SMA key should succeed");
  ASSERT(strcmp(buf, "SMA_20") == 0, "SMA key format");

  op = samtrader_operand_indicator(SAMTRADER_IND_EMA, 50);
  samtrader_operand_indicator_key(buf, sizeof(buf), &op);
  ASSERT(strcmp(buf, "EMA_50") == 0, "EMA key format");

  op = samtrader_operand_indicator(SAMTRADER_IND_RSI, 14);
  samtrader_operand_indicator_key(buf, sizeof(buf), &op);
  ASSERT(strcmp(buf, "RSI_14") == 0, "RSI key format");

  op = samtrader_operand_indicator(SAMTRADER_IND_ATR, 14);
  samtrader_operand_indicator_key(buf, sizeof(buf), &op);
  ASSERT(strcmp(buf, "ATR_14") == 0, "ATR key format");

  printf("  PASS\n");
  return 0;
}

static int test_indicator_key_multi(void) {
  printf("Testing indicator key generation (multi-param)...\n");

  char buf[64];

  SamtraderOperand op = samtrader_operand_indicator_multi(SAMTRADER_IND_MACD, 12, 26, 9);
  samtrader_operand_indicator_key(buf, sizeof(buf), &op);
  ASSERT(strcmp(buf, "MACD_12_26_9") == 0, "MACD key format");

  op = samtrader_operand_indicator_multi(SAMTRADER_IND_BOLLINGER, 20, 200,
                                         SAMTRADER_BOLLINGER_UPPER);
  samtrader_operand_indicator_key(buf, sizeof(buf), &op);
  ASSERT(strcmp(buf, "BOLLINGER_20_200") == 0, "Bollinger key format");

  /* Bollinger middle and lower should produce the same series key */
  op = samtrader_operand_indicator_multi(SAMTRADER_IND_BOLLINGER, 20, 200,
                                         SAMTRADER_BOLLINGER_LOWER);
  samtrader_operand_indicator_key(buf, sizeof(buf), &op);
  ASSERT(strcmp(buf, "BOLLINGER_20_200") == 0, "Bollinger lower same key");

  op = samtrader_operand_indicator_multi(SAMTRADER_IND_PIVOT, 0, SAMTRADER_PIVOT_R1, 0);
  samtrader_operand_indicator_key(buf, sizeof(buf), &op);
  ASSERT(strcmp(buf, "PIVOT") == 0, "Pivot key format");

  printf("  PASS\n");
  return 0;
}

static int test_indicator_key_invalid(void) {
  printf("Testing indicator key generation (invalid)...\n");

  char buf[64];

  /* Non-indicator operand */
  SamtraderOperand op = samtrader_operand_constant(42.0);
  ASSERT(samtrader_operand_indicator_key(buf, sizeof(buf), &op) < 0,
         "Constant operand should fail");

  /* NULL buffer */
  op = samtrader_operand_indicator(SAMTRADER_IND_SMA, 20);
  ASSERT(samtrader_operand_indicator_key(NULL, 64, &op) < 0, "NULL buffer should fail");

  /* Zero buffer size */
  ASSERT(samtrader_operand_indicator_key(buf, 0, &op) < 0, "Zero buf_size should fail");

  /* NULL operand */
  ASSERT(samtrader_operand_indicator_key(buf, sizeof(buf), NULL) < 0, "NULL operand should fail");

  printf("  PASS\n");
  return 0;
}

/*============================================================================
 * Null / Invalid Input Tests
 *============================================================================*/

static int test_evaluate_null_rule(void) {
  printf("Testing evaluate with NULL rule...\n");
  Samrena *arena = samrena_create_default();
  double closes[] = {100.0};
  SamrenaVector *ohlcv = make_ohlcv(arena, closes, 1);

  ASSERT(!samtrader_rule_evaluate(NULL, ohlcv, NULL, 0), "NULL rule should return false");

  samrena_destroy(arena);
  printf("  PASS\n");
  return 0;
}

static int test_evaluate_null_ohlcv(void) {
  printf("Testing evaluate with NULL ohlcv...\n");
  Samrena *arena = samrena_create_default();
  SamtraderRule *rule =
      samtrader_rule_create_comparison(arena, SAMTRADER_RULE_ABOVE, samtrader_operand_constant(1.0),
                                       samtrader_operand_constant(0.0));

  ASSERT(!samtrader_rule_evaluate(rule, NULL, NULL, 0), "NULL ohlcv should return false");

  samrena_destroy(arena);
  printf("  PASS\n");
  return 0;
}

/*============================================================================
 * ABOVE Rule Tests
 *============================================================================*/

static int test_above_constants(void) {
  printf("Testing ABOVE with constants...\n");
  Samrena *arena = samrena_create_default();
  double closes[] = {100.0};
  SamrenaVector *ohlcv = make_ohlcv(arena, closes, 1);

  SamtraderRule *rule = samtrader_rule_create_comparison(arena, SAMTRADER_RULE_ABOVE,
                                                         samtrader_operand_constant(10.0),
                                                         samtrader_operand_constant(5.0));
  ASSERT(samtrader_rule_evaluate(rule, ohlcv, NULL, 0), "10 > 5 should be true");

  rule =
      samtrader_rule_create_comparison(arena, SAMTRADER_RULE_ABOVE, samtrader_operand_constant(5.0),
                                       samtrader_operand_constant(10.0));
  ASSERT(!samtrader_rule_evaluate(rule, ohlcv, NULL, 0), "5 > 10 should be false");

  rule =
      samtrader_rule_create_comparison(arena, SAMTRADER_RULE_ABOVE, samtrader_operand_constant(5.0),
                                       samtrader_operand_constant(5.0));
  ASSERT(!samtrader_rule_evaluate(rule, ohlcv, NULL, 0), "5 > 5 should be false (not strict)");

  samrena_destroy(arena);
  printf("  PASS\n");
  return 0;
}

static int test_above_price_vs_constant(void) {
  printf("Testing ABOVE with price vs constant...\n");
  Samrena *arena = samrena_create_default();
  double closes[] = {100.0, 50.0, 75.0};
  SamrenaVector *ohlcv = make_ohlcv(arena, closes, 3);

  SamtraderRule *rule =
      samtrader_rule_create_comparison(arena, SAMTRADER_RULE_ABOVE,
                                       samtrader_operand_price(SAMTRADER_OPERAND_PRICE_CLOSE),
                                       samtrader_operand_constant(60.0));

  ASSERT(samtrader_rule_evaluate(rule, ohlcv, NULL, 0), "close=100 > 60");
  ASSERT(!samtrader_rule_evaluate(rule, ohlcv, NULL, 1), "close=50 not > 60");
  ASSERT(samtrader_rule_evaluate(rule, ohlcv, NULL, 2), "close=75 > 60");

  samrena_destroy(arena);
  printf("  PASS\n");
  return 0;
}

static int test_above_price_fields(void) {
  printf("Testing ABOVE with different price fields...\n");
  Samrena *arena = samrena_create_default();
  /* close=100, open=99, high=101, low=98 */
  double closes[] = {100.0};
  SamrenaVector *ohlcv = make_ohlcv(arena, closes, 1);

  /* high > close */
  SamtraderRule *rule =
      samtrader_rule_create_comparison(arena, SAMTRADER_RULE_ABOVE,
                                       samtrader_operand_price(SAMTRADER_OPERAND_PRICE_HIGH),
                                       samtrader_operand_price(SAMTRADER_OPERAND_PRICE_CLOSE));
  ASSERT(samtrader_rule_evaluate(rule, ohlcv, NULL, 0), "high(101) > close(100)");

  /* low > close */
  rule = samtrader_rule_create_comparison(arena, SAMTRADER_RULE_ABOVE,
                                          samtrader_operand_price(SAMTRADER_OPERAND_PRICE_LOW),
                                          samtrader_operand_price(SAMTRADER_OPERAND_PRICE_CLOSE));
  ASSERT(!samtrader_rule_evaluate(rule, ohlcv, NULL, 0), "low(98) not > close(100)");

  samrena_destroy(arena);
  printf("  PASS\n");
  return 0;
}

static int test_above_with_indicator(void) {
  printf("Testing ABOVE with indicator operand...\n");
  Samrena *arena = samrena_create_default();
  double closes[] = {100.0, 110.0, 90.0};
  SamrenaVector *ohlcv = make_ohlcv(arena, closes, 3);

  double sma_vals[] = {95.0, 105.0, 100.0};
  SamtraderOperand sma_op = samtrader_operand_indicator(SAMTRADER_IND_SMA, 20);
  SamtraderIndicatorSeries *sma = make_simple_series(arena, SAMTRADER_IND_SMA, 20, sma_vals, 3);

  SamHashMap *indicators = samhashmap_create(16, arena);
  put_indicator(indicators, &sma_op, sma);

  SamtraderRule *rule =
      samtrader_rule_create_comparison(arena, SAMTRADER_RULE_ABOVE,
                                       samtrader_operand_price(SAMTRADER_OPERAND_PRICE_CLOSE),
                                       sma_op);

  /* close=100 > SMA=95 -> true */
  ASSERT(samtrader_rule_evaluate(rule, ohlcv, indicators, 0), "100 > 95");
  /* close=110 > SMA=105 -> true */
  ASSERT(samtrader_rule_evaluate(rule, ohlcv, indicators, 1), "110 > 105");
  /* close=90 > SMA=100 -> false */
  ASSERT(!samtrader_rule_evaluate(rule, ohlcv, indicators, 2), "90 not > 100");

  samrena_destroy(arena);
  printf("  PASS\n");
  return 0;
}

static int test_above_volume(void) {
  printf("Testing ABOVE with volume...\n");
  Samrena *arena = samrena_create_default();
  double closes[] = {100.0, 200.0};
  SamrenaVector *ohlcv = make_ohlcv(arena, closes, 2);
  /* volumes: 1000, 2000 */

  SamtraderRule *rule =
      samtrader_rule_create_comparison(arena, SAMTRADER_RULE_ABOVE,
                                       samtrader_operand_price(SAMTRADER_OPERAND_VOLUME),
                                       samtrader_operand_constant(1500.0));

  ASSERT(!samtrader_rule_evaluate(rule, ohlcv, NULL, 0), "volume=1000 not > 1500");
  ASSERT(samtrader_rule_evaluate(rule, ohlcv, NULL, 1), "volume=2000 > 1500");

  samrena_destroy(arena);
  printf("  PASS\n");
  return 0;
}

/*============================================================================
 * BELOW Rule Tests
 *============================================================================*/

static int test_below_basic(void) {
  printf("Testing BELOW basic...\n");
  Samrena *arena = samrena_create_default();
  double closes[] = {50.0, 100.0};
  SamrenaVector *ohlcv = make_ohlcv(arena, closes, 2);

  SamtraderRule *rule =
      samtrader_rule_create_comparison(arena, SAMTRADER_RULE_BELOW,
                                       samtrader_operand_price(SAMTRADER_OPERAND_PRICE_CLOSE),
                                       samtrader_operand_constant(75.0));

  ASSERT(samtrader_rule_evaluate(rule, ohlcv, NULL, 0), "50 < 75");
  ASSERT(!samtrader_rule_evaluate(rule, ohlcv, NULL, 1), "100 not < 75");

  samrena_destroy(arena);
  printf("  PASS\n");
  return 0;
}

static int test_below_equal_values(void) {
  printf("Testing BELOW with equal values...\n");
  Samrena *arena = samrena_create_default();
  double closes[] = {75.0};
  SamrenaVector *ohlcv = make_ohlcv(arena, closes, 1);

  SamtraderRule *rule =
      samtrader_rule_create_comparison(arena, SAMTRADER_RULE_BELOW,
                                       samtrader_operand_price(SAMTRADER_OPERAND_PRICE_CLOSE),
                                       samtrader_operand_constant(75.0));

  ASSERT(!samtrader_rule_evaluate(rule, ohlcv, NULL, 0), "75 not < 75 (strict)");

  samrena_destroy(arena);
  printf("  PASS\n");
  return 0;
}

/*============================================================================
 * EQUALS Rule Tests
 *============================================================================*/

static int test_equals_exact(void) {
  printf("Testing EQUALS with exact match...\n");
  Samrena *arena = samrena_create_default();
  double closes[] = {100.0};
  SamrenaVector *ohlcv = make_ohlcv(arena, closes, 1);

  SamtraderRule *rule =
      samtrader_rule_create_comparison(arena, SAMTRADER_RULE_EQUALS,
                                       samtrader_operand_price(SAMTRADER_OPERAND_PRICE_CLOSE),
                                       samtrader_operand_constant(100.0));

  ASSERT(samtrader_rule_evaluate(rule, ohlcv, NULL, 0), "100 == 100");

  samrena_destroy(arena);
  printf("  PASS\n");
  return 0;
}

static int test_equals_within_tolerance(void) {
  printf("Testing EQUALS within tolerance...\n");
  Samrena *arena = samrena_create_default();
  double closes[] = {100.0};
  SamrenaVector *ohlcv = make_ohlcv(arena, closes, 1);

  /* Values very close together (within 1e-9 tolerance) */
  SamtraderRule *rule = samtrader_rule_create_comparison(arena, SAMTRADER_RULE_EQUALS,
                                                         samtrader_operand_constant(1.0000000001),
                                                         samtrader_operand_constant(1.0000000002));
  ASSERT(samtrader_rule_evaluate(rule, ohlcv, NULL, 0), "Within tolerance");

  /* Values further apart */
  rule = samtrader_rule_create_comparison(arena, SAMTRADER_RULE_EQUALS,
                                          samtrader_operand_constant(1.0),
                                          samtrader_operand_constant(1.01));
  ASSERT(!samtrader_rule_evaluate(rule, ohlcv, NULL, 0), "Outside tolerance");

  samrena_destroy(arena);
  printf("  PASS\n");
  return 0;
}

/*============================================================================
 * BETWEEN Rule Tests
 *============================================================================*/

static int test_between_in_range(void) {
  printf("Testing BETWEEN in range...\n");
  Samrena *arena = samrena_create_default();
  double closes[] = {100.0};
  SamrenaVector *ohlcv = make_ohlcv(arena, closes, 1);

  double rsi_vals[] = {45.0};
  SamtraderOperand rsi_op = samtrader_operand_indicator(SAMTRADER_IND_RSI, 14);
  SamtraderIndicatorSeries *rsi = make_simple_series(arena, SAMTRADER_IND_RSI, 14, rsi_vals, 1);

  SamHashMap *indicators = samhashmap_create(16, arena);
  put_indicator(indicators, &rsi_op, rsi);

  /* BETWEEN(RSI(14), 30, 70) -> RSI=45 in [30, 70] */
  SamtraderRule *rule =
      samtrader_rule_create_between(arena, rsi_op, samtrader_operand_constant(30.0), 70.0);

  ASSERT(samtrader_rule_evaluate(rule, ohlcv, indicators, 0), "RSI=45 in [30,70]");

  samrena_destroy(arena);
  printf("  PASS\n");
  return 0;
}

static int test_between_out_of_range(void) {
  printf("Testing BETWEEN out of range...\n");
  Samrena *arena = samrena_create_default();
  double closes[] = {100.0};
  SamrenaVector *ohlcv = make_ohlcv(arena, closes, 1);

  double rsi_vals[] = {80.0};
  SamtraderOperand rsi_op = samtrader_operand_indicator(SAMTRADER_IND_RSI, 14);
  SamtraderIndicatorSeries *rsi = make_simple_series(arena, SAMTRADER_IND_RSI, 14, rsi_vals, 1);

  SamHashMap *indicators = samhashmap_create(16, arena);
  put_indicator(indicators, &rsi_op, rsi);

  /* BETWEEN(RSI(14), 30, 70) -> RSI=80 not in [30, 70] */
  SamtraderRule *rule =
      samtrader_rule_create_between(arena, rsi_op, samtrader_operand_constant(30.0), 70.0);

  ASSERT(!samtrader_rule_evaluate(rule, ohlcv, indicators, 0), "RSI=80 not in [30,70]");

  samrena_destroy(arena);
  printf("  PASS\n");
  return 0;
}

static int test_between_at_boundaries(void) {
  printf("Testing BETWEEN at boundaries...\n");
  Samrena *arena = samrena_create_default();
  double closes[] = {100.0, 100.0, 100.0};
  SamrenaVector *ohlcv = make_ohlcv(arena, closes, 3);

  double rsi_vals[] = {30.0, 70.0, 29.99};
  SamtraderOperand rsi_op = samtrader_operand_indicator(SAMTRADER_IND_RSI, 14);
  SamtraderIndicatorSeries *rsi = make_simple_series(arena, SAMTRADER_IND_RSI, 14, rsi_vals, 3);

  SamHashMap *indicators = samhashmap_create(16, arena);
  put_indicator(indicators, &rsi_op, rsi);

  SamtraderRule *rule =
      samtrader_rule_create_between(arena, rsi_op, samtrader_operand_constant(30.0), 70.0);

  /* At lower bound: 30 >= 30 && 30 <= 70 -> true */
  ASSERT(samtrader_rule_evaluate(rule, ohlcv, indicators, 0), "RSI=30 at lower bound");
  /* At upper bound: 70 >= 30 && 70 <= 70 -> true */
  ASSERT(samtrader_rule_evaluate(rule, ohlcv, indicators, 1), "RSI=70 at upper bound");
  /* Just below: 29.99 < 30 -> false */
  ASSERT(!samtrader_rule_evaluate(rule, ohlcv, indicators, 2), "RSI=29.99 below lower bound");

  samrena_destroy(arena);
  printf("  PASS\n");
  return 0;
}

static int test_between_price_constant(void) {
  printf("Testing BETWEEN with price operand and constants...\n");
  Samrena *arena = samrena_create_default();
  double closes[] = {50.0, 100.0, 150.0};
  SamrenaVector *ohlcv = make_ohlcv(arena, closes, 3);

  /* BETWEEN(close, 75, 125) */
  SamtraderRule *rule =
      samtrader_rule_create_between(arena, samtrader_operand_price(SAMTRADER_OPERAND_PRICE_CLOSE),
                                    samtrader_operand_constant(75.0), 125.0);

  ASSERT(!samtrader_rule_evaluate(rule, ohlcv, NULL, 0), "50 not in [75,125]");
  ASSERT(samtrader_rule_evaluate(rule, ohlcv, NULL, 1), "100 in [75,125]");
  ASSERT(!samtrader_rule_evaluate(rule, ohlcv, NULL, 2), "150 not in [75,125]");

  samrena_destroy(arena);
  printf("  PASS\n");
  return 0;
}

/*============================================================================
 * CROSS_ABOVE Rule Tests
 *============================================================================*/

static int test_cross_above_basic(void) {
  printf("Testing CROSS_ABOVE basic...\n");
  Samrena *arena = samrena_create_default();
  /* close: 90, 100, 110, 105 */
  double closes[] = {90.0, 100.0, 110.0, 105.0};
  SamrenaVector *ohlcv = make_ohlcv(arena, closes, 4);

  /* SMA stays flat at 100 */
  double sma_vals[] = {100.0, 100.0, 100.0, 100.0};
  SamtraderOperand sma_op = samtrader_operand_indicator(SAMTRADER_IND_SMA, 20);
  SamtraderIndicatorSeries *sma = make_simple_series(arena, SAMTRADER_IND_SMA, 20, sma_vals, 4);

  SamHashMap *indicators = samhashmap_create(16, arena);
  put_indicator(indicators, &sma_op, sma);

  SamtraderRule *rule =
      samtrader_rule_create_comparison(arena, SAMTRADER_RULE_CROSS_ABOVE,
                                       samtrader_operand_price(SAMTRADER_OPERAND_PRICE_CLOSE),
                                       sma_op);

  /* Index 0: no previous bar -> false */
  ASSERT(!samtrader_rule_evaluate(rule, ohlcv, indicators, 0), "No previous bar");
  /* Index 1: prev close=90 <= SMA=100, curr close=100 not > SMA=100 -> false */
  ASSERT(!samtrader_rule_evaluate(rule, ohlcv, indicators, 1), "Equal not above");
  /* Index 2: prev close=100 <= SMA=100, curr close=110 > SMA=100 -> true (cross!) */
  ASSERT(samtrader_rule_evaluate(rule, ohlcv, indicators, 2), "Crossed above");
  /* Index 3: prev close=110 > SMA=100, already above -> false */
  ASSERT(!samtrader_rule_evaluate(rule, ohlcv, indicators, 3), "Already above, no cross");

  samrena_destroy(arena);
  printf("  PASS\n");
  return 0;
}

static int test_cross_above_index_zero(void) {
  printf("Testing CROSS_ABOVE at index 0...\n");
  Samrena *arena = samrena_create_default();
  double closes[] = {110.0};
  SamrenaVector *ohlcv = make_ohlcv(arena, closes, 1);

  SamtraderRule *rule =
      samtrader_rule_create_comparison(arena, SAMTRADER_RULE_CROSS_ABOVE,
                                       samtrader_operand_price(SAMTRADER_OPERAND_PRICE_CLOSE),
                                       samtrader_operand_constant(100.0));

  ASSERT(!samtrader_rule_evaluate(rule, ohlcv, NULL, 0), "Index 0 should be false");

  samrena_destroy(arena);
  printf("  PASS\n");
  return 0;
}

static int test_cross_above_with_constants(void) {
  printf("Testing CROSS_ABOVE close vs constant...\n");
  Samrena *arena = samrena_create_default();
  /* close crosses above 100: prev=95, curr=105 */
  double closes[] = {95.0, 105.0, 110.0};
  SamrenaVector *ohlcv = make_ohlcv(arena, closes, 3);

  SamtraderRule *rule =
      samtrader_rule_create_comparison(arena, SAMTRADER_RULE_CROSS_ABOVE,
                                       samtrader_operand_price(SAMTRADER_OPERAND_PRICE_CLOSE),
                                       samtrader_operand_constant(100.0));

  /* Index 1: prev=95 <= 100, curr=105 > 100 -> true */
  ASSERT(samtrader_rule_evaluate(rule, ohlcv, NULL, 1), "Cross above constant");
  /* Index 2: prev=105 > 100, already above -> false */
  ASSERT(!samtrader_rule_evaluate(rule, ohlcv, NULL, 2), "Already above");

  samrena_destroy(arena);
  printf("  PASS\n");
  return 0;
}

/*============================================================================
 * CROSS_BELOW Rule Tests
 *============================================================================*/

static int test_cross_below_basic(void) {
  printf("Testing CROSS_BELOW basic...\n");
  Samrena *arena = samrena_create_default();
  /* close: 110, 100, 90, 95 */
  double closes[] = {110.0, 100.0, 90.0, 95.0};
  SamrenaVector *ohlcv = make_ohlcv(arena, closes, 4);

  /* SMA stays flat at 100 */
  double sma_vals[] = {100.0, 100.0, 100.0, 100.0};
  SamtraderOperand sma_op = samtrader_operand_indicator(SAMTRADER_IND_SMA, 20);
  SamtraderIndicatorSeries *sma = make_simple_series(arena, SAMTRADER_IND_SMA, 20, sma_vals, 4);

  SamHashMap *indicators = samhashmap_create(16, arena);
  put_indicator(indicators, &sma_op, sma);

  SamtraderRule *rule =
      samtrader_rule_create_comparison(arena, SAMTRADER_RULE_CROSS_BELOW,
                                       samtrader_operand_price(SAMTRADER_OPERAND_PRICE_CLOSE),
                                       sma_op);

  /* Index 0: no previous bar -> false */
  ASSERT(!samtrader_rule_evaluate(rule, ohlcv, indicators, 0), "No previous bar");
  /* Index 1: prev=110 >= SMA=100, curr=100 not < SMA=100 -> false */
  ASSERT(!samtrader_rule_evaluate(rule, ohlcv, indicators, 1), "Equal not below");
  /* Index 2: prev=100 >= SMA=100, curr=90 < SMA=100 -> true (cross!) */
  ASSERT(samtrader_rule_evaluate(rule, ohlcv, indicators, 2), "Crossed below");
  /* Index 3: prev=90 < SMA=100, already below -> false */
  ASSERT(!samtrader_rule_evaluate(rule, ohlcv, indicators, 3), "Already below, no cross");

  samrena_destroy(arena);
  printf("  PASS\n");
  return 0;
}

static int test_cross_below_index_zero(void) {
  printf("Testing CROSS_BELOW at index 0...\n");
  Samrena *arena = samrena_create_default();
  double closes[] = {90.0};
  SamrenaVector *ohlcv = make_ohlcv(arena, closes, 1);

  SamtraderRule *rule =
      samtrader_rule_create_comparison(arena, SAMTRADER_RULE_CROSS_BELOW,
                                       samtrader_operand_price(SAMTRADER_OPERAND_PRICE_CLOSE),
                                       samtrader_operand_constant(100.0));

  ASSERT(!samtrader_rule_evaluate(rule, ohlcv, NULL, 0), "Index 0 should be false");

  samrena_destroy(arena);
  printf("  PASS\n");
  return 0;
}

/*============================================================================
 * Indicator Operand Tests (Bollinger, Pivot, MACD)
 *============================================================================*/

static int test_above_bollinger_upper(void) {
  printf("Testing ABOVE with Bollinger upper...\n");
  Samrena *arena = samrena_create_default();
  double closes[] = {120.0, 90.0};
  SamrenaVector *ohlcv = make_ohlcv(arena, closes, 2);

  double upper[] = {110.0, 110.0};
  double middle[] = {100.0, 100.0};
  double lower[] = {90.0, 90.0};

  SamtraderOperand bb_op = samtrader_operand_indicator_multi(SAMTRADER_IND_BOLLINGER, 20, 200,
                                                             SAMTRADER_BOLLINGER_UPPER);
  SamtraderIndicatorSeries *bb = make_bollinger_series(arena, 20, 2.0, upper, middle, lower, 2);

  SamHashMap *indicators = samhashmap_create(16, arena);
  put_indicator(indicators, &bb_op, bb);

  SamtraderRule *rule =
      samtrader_rule_create_comparison(arena, SAMTRADER_RULE_ABOVE,
                                       samtrader_operand_price(SAMTRADER_OPERAND_PRICE_CLOSE),
                                       bb_op);

  ASSERT(samtrader_rule_evaluate(rule, ohlcv, indicators, 0), "120 > upper 110");
  ASSERT(!samtrader_rule_evaluate(rule, ohlcv, indicators, 1), "90 not > upper 110");

  samrena_destroy(arena);
  printf("  PASS\n");
  return 0;
}

static int test_below_bollinger_lower(void) {
  printf("Testing BELOW with Bollinger lower...\n");
  Samrena *arena = samrena_create_default();
  double closes[] = {85.0, 95.0};
  SamrenaVector *ohlcv = make_ohlcv(arena, closes, 2);

  double upper[] = {110.0, 110.0};
  double middle[] = {100.0, 100.0};
  double lower[] = {90.0, 90.0};

  SamtraderOperand bb_lower_op = samtrader_operand_indicator_multi(SAMTRADER_IND_BOLLINGER, 20, 200,
                                                                   SAMTRADER_BOLLINGER_LOWER);
  SamtraderIndicatorSeries *bb = make_bollinger_series(arena, 20, 2.0, upper, middle, lower, 2);

  SamHashMap *indicators = samhashmap_create(16, arena);
  put_indicator(indicators, &bb_lower_op, bb);

  SamtraderRule *rule =
      samtrader_rule_create_comparison(arena, SAMTRADER_RULE_BELOW,
                                       samtrader_operand_price(SAMTRADER_OPERAND_PRICE_CLOSE),
                                       bb_lower_op);

  ASSERT(samtrader_rule_evaluate(rule, ohlcv, indicators, 0), "85 < lower 90");
  ASSERT(!samtrader_rule_evaluate(rule, ohlcv, indicators, 1), "95 not < lower 90");

  samrena_destroy(arena);
  printf("  PASS\n");
  return 0;
}

static int test_above_pivot_r1(void) {
  printf("Testing ABOVE with Pivot R1...\n");
  Samrena *arena = samrena_create_default();
  double closes[] = {115.0, 95.0};
  SamrenaVector *ohlcv = make_ohlcv(arena, closes, 2);

  double pivots[] = {100.0, 100.0};
  double r1[] = {110.0, 110.0};

  SamtraderOperand pivot_op =
      samtrader_operand_indicator_multi(SAMTRADER_IND_PIVOT, 0, SAMTRADER_PIVOT_R1, 0);
  SamtraderIndicatorSeries *piv = make_pivot_series(arena, pivots, r1, 2);

  SamHashMap *indicators = samhashmap_create(16, arena);
  put_indicator(indicators, &pivot_op, piv);

  SamtraderRule *rule =
      samtrader_rule_create_comparison(arena, SAMTRADER_RULE_ABOVE,
                                       samtrader_operand_price(SAMTRADER_OPERAND_PRICE_CLOSE),
                                       pivot_op);

  ASSERT(samtrader_rule_evaluate(rule, ohlcv, indicators, 0), "115 > R1=110");
  ASSERT(!samtrader_rule_evaluate(rule, ohlcv, indicators, 1), "95 not > R1=110");

  samrena_destroy(arena);
  printf("  PASS\n");
  return 0;
}

static int test_above_macd(void) {
  printf("Testing ABOVE with MACD...\n");
  Samrena *arena = samrena_create_default();
  double closes[] = {100.0, 100.0};
  SamrenaVector *ohlcv = make_ohlcv(arena, closes, 2);

  double macd_lines[] = {5.0, -3.0};
  SamtraderOperand macd_op = samtrader_operand_indicator_multi(SAMTRADER_IND_MACD, 12, 26, 9);
  SamtraderIndicatorSeries *macd = make_macd_series(arena, 12, 26, 9, macd_lines, 2);

  SamHashMap *indicators = samhashmap_create(16, arena);
  put_indicator(indicators, &macd_op, macd);

  /* ABOVE(MACD, 0) -> MACD line > 0 */
  SamtraderRule *rule = samtrader_rule_create_comparison(arena, SAMTRADER_RULE_ABOVE, macd_op,
                                                         samtrader_operand_constant(0.0));

  ASSERT(samtrader_rule_evaluate(rule, ohlcv, indicators, 0), "MACD=5 > 0");
  ASSERT(!samtrader_rule_evaluate(rule, ohlcv, indicators, 1), "MACD=-3 not > 0");

  samrena_destroy(arena);
  printf("  PASS\n");
  return 0;
}

/*============================================================================
 * Missing Indicator / Invalid Index Tests
 *============================================================================*/

static int test_missing_indicator(void) {
  printf("Testing evaluation with missing indicator...\n");
  Samrena *arena = samrena_create_default();
  double closes[] = {100.0};
  SamrenaVector *ohlcv = make_ohlcv(arena, closes, 1);

  SamHashMap *indicators = samhashmap_create(16, arena);

  /* Reference SMA(20) but don't put it in the hashmap */
  SamtraderOperand sma_op = samtrader_operand_indicator(SAMTRADER_IND_SMA, 20);
  SamtraderRule *rule =
      samtrader_rule_create_comparison(arena, SAMTRADER_RULE_ABOVE,
                                       samtrader_operand_price(SAMTRADER_OPERAND_PRICE_CLOSE),
                                       sma_op);

  ASSERT(!samtrader_rule_evaluate(rule, ohlcv, indicators, 0),
         "Missing indicator should return false");

  samrena_destroy(arena);
  printf("  PASS\n");
  return 0;
}

static int test_null_indicators_map(void) {
  printf("Testing evaluation with NULL indicators map...\n");
  Samrena *arena = samrena_create_default();
  double closes[] = {100.0};
  SamrenaVector *ohlcv = make_ohlcv(arena, closes, 1);

  SamtraderOperand sma_op = samtrader_operand_indicator(SAMTRADER_IND_SMA, 20);
  SamtraderRule *rule =
      samtrader_rule_create_comparison(arena, SAMTRADER_RULE_ABOVE,
                                       samtrader_operand_price(SAMTRADER_OPERAND_PRICE_CLOSE),
                                       sma_op);

  ASSERT(!samtrader_rule_evaluate(rule, ohlcv, NULL, 0), "NULL indicators map should return false");

  samrena_destroy(arena);
  printf("  PASS\n");
  return 0;
}

static int test_out_of_bounds_index(void) {
  printf("Testing evaluation with out-of-bounds index...\n");
  Samrena *arena = samrena_create_default();
  double closes[] = {100.0};
  SamrenaVector *ohlcv = make_ohlcv(arena, closes, 1);

  SamtraderRule *rule =
      samtrader_rule_create_comparison(arena, SAMTRADER_RULE_ABOVE,
                                       samtrader_operand_price(SAMTRADER_OPERAND_PRICE_CLOSE),
                                       samtrader_operand_constant(50.0));

  /* Index 10 is out of bounds for a 1-element vector */
  ASSERT(!samtrader_rule_evaluate(rule, ohlcv, NULL, 10),
         "Out-of-bounds index should return false");

  samrena_destroy(arena);
  printf("  PASS\n");
  return 0;
}

/*============================================================================
 * Cross with Two Indicators
 *============================================================================*/

static int test_cross_above_two_indicators(void) {
  printf("Testing CROSS_ABOVE with two indicator operands...\n");
  Samrena *arena = samrena_create_default();
  double closes[] = {100.0, 100.0, 100.0, 100.0};
  SamrenaVector *ohlcv = make_ohlcv(arena, closes, 4);

  /* Fast SMA crosses above slow SMA */
  double sma20[] = {95.0, 98.0, 103.0, 105.0};
  double sma50[] = {100.0, 100.0, 100.0, 100.0};

  SamtraderOperand sma20_op = samtrader_operand_indicator(SAMTRADER_IND_SMA, 20);
  SamtraderOperand sma50_op = samtrader_operand_indicator(SAMTRADER_IND_SMA, 50);

  SamtraderIndicatorSeries *sma20_series =
      make_simple_series(arena, SAMTRADER_IND_SMA, 20, sma20, 4);
  SamtraderIndicatorSeries *sma50_series =
      make_simple_series(arena, SAMTRADER_IND_SMA, 50, sma50, 4);

  SamHashMap *indicators = samhashmap_create(16, arena);
  put_indicator(indicators, &sma20_op, sma20_series);
  put_indicator(indicators, &sma50_op, sma50_series);

  SamtraderRule *rule =
      samtrader_rule_create_comparison(arena, SAMTRADER_RULE_CROSS_ABOVE, sma20_op, sma50_op);

  /* bar 0: no prev -> false */
  ASSERT(!samtrader_rule_evaluate(rule, ohlcv, indicators, 0), "No previous");
  /* bar 1: prev=95 <= 100, curr=98 not > 100 -> false */
  ASSERT(!samtrader_rule_evaluate(rule, ohlcv, indicators, 1), "Not yet crossed");
  /* bar 2: prev=98 <= 100, curr=103 > 100 -> true (cross!) */
  ASSERT(samtrader_rule_evaluate(rule, ohlcv, indicators, 2), "Golden cross");
  /* bar 3: prev=103 > 100, already above -> false */
  ASSERT(!samtrader_rule_evaluate(rule, ohlcv, indicators, 3), "Already above");

  samrena_destroy(arena);
  printf("  PASS\n");
  return 0;
}

/*============================================================================
 * CONSECUTIVE Rule Tests
 *============================================================================*/

static int test_consecutive_all_true(void) {
  printf("Testing CONSECUTIVE all true...\n");
  Samrena *arena = samrena_create_default();
  /* All closes above 50 */
  double closes[] = {60.0, 70.0, 80.0, 90.0, 100.0};
  SamrenaVector *ohlcv = make_ohlcv(arena, closes, 5);

  /* CONSECUTIVE(ABOVE(close, 50), 3) */
  SamtraderRule *child =
      samtrader_rule_create_comparison(arena, SAMTRADER_RULE_ABOVE,
                                       samtrader_operand_price(SAMTRADER_OPERAND_PRICE_CLOSE),
                                       samtrader_operand_constant(50.0));
  SamtraderRule *rule = samtrader_rule_create_temporal(arena, SAMTRADER_RULE_CONSECUTIVE, child, 3);

  /* Index 0,1: not enough lookback (need 3 bars) -> false */
  ASSERT(!samtrader_rule_evaluate(rule, ohlcv, NULL, 0), "Not enough lookback at index 0");
  ASSERT(!samtrader_rule_evaluate(rule, ohlcv, NULL, 1), "Not enough lookback at index 1");
  /* Index 2: bars [0,1,2] = [60,70,80] all > 50 -> true */
  ASSERT(samtrader_rule_evaluate(rule, ohlcv, NULL, 2), "3 consecutive above 50");
  /* Index 4: bars [2,3,4] = [80,90,100] all > 50 -> true */
  ASSERT(samtrader_rule_evaluate(rule, ohlcv, NULL, 4), "3 consecutive above 50 at end");

  samrena_destroy(arena);
  printf("  PASS\n");
  return 0;
}

static int test_consecutive_broken_streak(void) {
  printf("Testing CONSECUTIVE with broken streak...\n");
  Samrena *arena = samrena_create_default();
  /* close dips below 50 at index 2 */
  double closes[] = {60.0, 70.0, 40.0, 80.0, 90.0};
  SamrenaVector *ohlcv = make_ohlcv(arena, closes, 5);

  SamtraderRule *child =
      samtrader_rule_create_comparison(arena, SAMTRADER_RULE_ABOVE,
                                       samtrader_operand_price(SAMTRADER_OPERAND_PRICE_CLOSE),
                                       samtrader_operand_constant(50.0));
  SamtraderRule *rule = samtrader_rule_create_temporal(arena, SAMTRADER_RULE_CONSECUTIVE, child, 3);

  /* Index 2: bars [0,1,2] = [60,70,40], bar 2 fails -> false */
  ASSERT(!samtrader_rule_evaluate(rule, ohlcv, NULL, 2), "Streak broken at bar 2");
  /* Index 3: bars [1,2,3] = [70,40,80], bar 2 fails -> false */
  ASSERT(!samtrader_rule_evaluate(rule, ohlcv, NULL, 3), "Streak broken includes bar 2");
  /* Index 4: bars [2,3,4] = [40,80,90], bar 2 fails -> false */
  ASSERT(!samtrader_rule_evaluate(rule, ohlcv, NULL, 4), "Streak broken includes bar 2");

  samrena_destroy(arena);
  printf("  PASS\n");
  return 0;
}

static int test_consecutive_lookback_one(void) {
  printf("Testing CONSECUTIVE with lookback=1...\n");
  Samrena *arena = samrena_create_default();
  double closes[] = {60.0, 40.0};
  SamrenaVector *ohlcv = make_ohlcv(arena, closes, 2);

  SamtraderRule *child =
      samtrader_rule_create_comparison(arena, SAMTRADER_RULE_ABOVE,
                                       samtrader_operand_price(SAMTRADER_OPERAND_PRICE_CLOSE),
                                       samtrader_operand_constant(50.0));
  SamtraderRule *rule = samtrader_rule_create_temporal(arena, SAMTRADER_RULE_CONSECUTIVE, child, 1);

  /* lookback=1: just checks current bar */
  ASSERT(samtrader_rule_evaluate(rule, ohlcv, NULL, 0), "60 > 50 at index 0");
  ASSERT(!samtrader_rule_evaluate(rule, ohlcv, NULL, 1), "40 not > 50 at index 1");

  samrena_destroy(arena);
  printf("  PASS\n");
  return 0;
}

static int test_consecutive_with_indicator(void) {
  printf("Testing CONSECUTIVE with indicator...\n");
  Samrena *arena = samrena_create_default();
  double closes[] = {100.0, 110.0, 120.0, 90.0, 130.0};
  SamrenaVector *ohlcv = make_ohlcv(arena, closes, 5);

  double sma_vals[] = {95.0, 105.0, 110.0, 100.0, 120.0};
  SamtraderOperand sma_op = samtrader_operand_indicator(SAMTRADER_IND_SMA, 20);
  SamtraderIndicatorSeries *sma = make_simple_series(arena, SAMTRADER_IND_SMA, 20, sma_vals, 5);

  SamHashMap *indicators = samhashmap_create(16, arena);
  put_indicator(indicators, &sma_op, sma);

  /* CONSECUTIVE(ABOVE(close, SMA(20)), 3) */
  SamtraderRule *child =
      samtrader_rule_create_comparison(arena, SAMTRADER_RULE_ABOVE,
                                       samtrader_operand_price(SAMTRADER_OPERAND_PRICE_CLOSE),
                                       sma_op);
  SamtraderRule *rule = samtrader_rule_create_temporal(arena, SAMTRADER_RULE_CONSECUTIVE, child, 3);

  /* Bars [0,1,2]: close=[100,110,120] vs SMA=[95,105,110] -> all above -> true */
  ASSERT(samtrader_rule_evaluate(rule, ohlcv, indicators, 2), "3 consecutive close > SMA");
  /* Bars [1,2,3]: close=[110,120,90] vs SMA=[105,110,100] -> bar 3: 90 < 100 -> false */
  ASSERT(!samtrader_rule_evaluate(rule, ohlcv, indicators, 3), "Bar 3 breaks consecutive");

  samrena_destroy(arena);
  printf("  PASS\n");
  return 0;
}

static int test_consecutive_null_child(void) {
  printf("Testing CONSECUTIVE with NULL child...\n");
  Samrena *arena = samrena_create_default();
  double closes[] = {100.0, 100.0, 100.0};
  SamrenaVector *ohlcv = make_ohlcv(arena, closes, 3);

  /* Create temporal rule that should fail due to NULL child */
  SamtraderRule *rule = samtrader_rule_create_temporal(arena, SAMTRADER_RULE_CONSECUTIVE, NULL, 3);

  ASSERT(rule == NULL || !samtrader_rule_evaluate(rule, ohlcv, NULL, 2),
         "NULL child should return false");

  samrena_destroy(arena);
  printf("  PASS\n");
  return 0;
}

/*============================================================================
 * ANY_OF Rule Tests
 *============================================================================*/

static int test_any_of_found(void) {
  printf("Testing ANY_OF found in window...\n");
  Samrena *arena = samrena_create_default();
  /* Only bar 1 has close > 100 */
  double closes[] = {90.0, 110.0, 80.0, 70.0, 60.0};
  SamrenaVector *ohlcv = make_ohlcv(arena, closes, 5);

  /* ANY_OF(ABOVE(close, 100), 3) */
  SamtraderRule *child =
      samtrader_rule_create_comparison(arena, SAMTRADER_RULE_ABOVE,
                                       samtrader_operand_price(SAMTRADER_OPERAND_PRICE_CLOSE),
                                       samtrader_operand_constant(100.0));
  SamtraderRule *rule = samtrader_rule_create_temporal(arena, SAMTRADER_RULE_ANY_OF, child, 3);

  /* Index 2: window [0,1,2], bar 1 has 110 > 100 -> true */
  ASSERT(samtrader_rule_evaluate(rule, ohlcv, NULL, 2), "Found in window [0,1,2]");
  /* Index 3: window [1,2,3], bar 1 has 110 > 100 -> true */
  ASSERT(samtrader_rule_evaluate(rule, ohlcv, NULL, 3), "Found in window [1,2,3]");
  /* Index 4: window [2,3,4] = [80,70,60], none > 100 -> false */
  ASSERT(!samtrader_rule_evaluate(rule, ohlcv, NULL, 4), "Not found in window [2,3,4]");

  samrena_destroy(arena);
  printf("  PASS\n");
  return 0;
}

static int test_any_of_not_found(void) {
  printf("Testing ANY_OF not found...\n");
  Samrena *arena = samrena_create_default();
  /* No closes above 100 */
  double closes[] = {50.0, 60.0, 70.0, 80.0, 90.0};
  SamrenaVector *ohlcv = make_ohlcv(arena, closes, 5);

  SamtraderRule *child =
      samtrader_rule_create_comparison(arena, SAMTRADER_RULE_ABOVE,
                                       samtrader_operand_price(SAMTRADER_OPERAND_PRICE_CLOSE),
                                       samtrader_operand_constant(100.0));
  SamtraderRule *rule = samtrader_rule_create_temporal(arena, SAMTRADER_RULE_ANY_OF, child, 3);

  ASSERT(!samtrader_rule_evaluate(rule, ohlcv, NULL, 2), "None > 100 in [50,60,70]");
  ASSERT(!samtrader_rule_evaluate(rule, ohlcv, NULL, 4), "None > 100 in [70,80,90]");

  samrena_destroy(arena);
  printf("  PASS\n");
  return 0;
}

static int test_any_of_insufficient_lookback(void) {
  printf("Testing ANY_OF insufficient lookback...\n");
  Samrena *arena = samrena_create_default();
  double closes[] = {110.0, 120.0};
  SamrenaVector *ohlcv = make_ohlcv(arena, closes, 2);

  SamtraderRule *child =
      samtrader_rule_create_comparison(arena, SAMTRADER_RULE_ABOVE,
                                       samtrader_operand_price(SAMTRADER_OPERAND_PRICE_CLOSE),
                                       samtrader_operand_constant(100.0));
  SamtraderRule *rule = samtrader_rule_create_temporal(arena, SAMTRADER_RULE_ANY_OF, child, 5);

  /* Need 5 bars but only have 2 -> false at any index */
  ASSERT(!samtrader_rule_evaluate(rule, ohlcv, NULL, 0), "Not enough bars at index 0");
  ASSERT(!samtrader_rule_evaluate(rule, ohlcv, NULL, 1), "Not enough bars at index 1");

  samrena_destroy(arena);
  printf("  PASS\n");
  return 0;
}

static int test_any_of_lookback_one(void) {
  printf("Testing ANY_OF with lookback=1...\n");
  Samrena *arena = samrena_create_default();
  double closes[] = {110.0, 40.0};
  SamrenaVector *ohlcv = make_ohlcv(arena, closes, 2);

  SamtraderRule *child =
      samtrader_rule_create_comparison(arena, SAMTRADER_RULE_ABOVE,
                                       samtrader_operand_price(SAMTRADER_OPERAND_PRICE_CLOSE),
                                       samtrader_operand_constant(100.0));
  SamtraderRule *rule = samtrader_rule_create_temporal(arena, SAMTRADER_RULE_ANY_OF, child, 1);

  /* lookback=1: just checks current bar */
  ASSERT(samtrader_rule_evaluate(rule, ohlcv, NULL, 0), "110 > 100 at index 0");
  ASSERT(!samtrader_rule_evaluate(rule, ohlcv, NULL, 1), "40 not > 100 at index 1");

  samrena_destroy(arena);
  printf("  PASS\n");
  return 0;
}

static int test_any_of_cross_above(void) {
  printf("Testing ANY_OF with CROSS_ABOVE child...\n");
  Samrena *arena = samrena_create_default();
  /* Cross above happens at index 2 (prev=95, curr=105 vs threshold 100) */
  double closes[] = {90.0, 95.0, 105.0, 108.0, 112.0, 115.0};
  SamrenaVector *ohlcv = make_ohlcv(arena, closes, 6);

  /* ANY_OF(CROSS_ABOVE(close, 100), 3) */
  SamtraderRule *child =
      samtrader_rule_create_comparison(arena, SAMTRADER_RULE_CROSS_ABOVE,
                                       samtrader_operand_price(SAMTRADER_OPERAND_PRICE_CLOSE),
                                       samtrader_operand_constant(100.0));
  SamtraderRule *rule = samtrader_rule_create_temporal(arena, SAMTRADER_RULE_ANY_OF, child, 3);

  /* Index 2: window [0,1,2], cross at bar 2 -> true */
  ASSERT(samtrader_rule_evaluate(rule, ohlcv, NULL, 2), "Cross at bar 2 in window");
  /* Index 4: window [2,3,4], cross at bar 2 -> true */
  ASSERT(samtrader_rule_evaluate(rule, ohlcv, NULL, 4), "Cross at bar 2 still in window");
  /* Index 5: window [3,4,5] = [108,112,115], no cross -> false */
  ASSERT(!samtrader_rule_evaluate(rule, ohlcv, NULL, 5), "Cross at bar 2 outside window");

  samrena_destroy(arena);
  printf("  PASS\n");
  return 0;
}

static int test_any_of_null_child(void) {
  printf("Testing ANY_OF with NULL child...\n");
  Samrena *arena = samrena_create_default();
  double closes[] = {100.0, 100.0, 100.0};
  SamrenaVector *ohlcv = make_ohlcv(arena, closes, 3);

  SamtraderRule *rule = samtrader_rule_create_temporal(arena, SAMTRADER_RULE_ANY_OF, NULL, 3);

  ASSERT(rule == NULL || !samtrader_rule_evaluate(rule, ohlcv, NULL, 2),
         "NULL child should return false");

  samrena_destroy(arena);
  printf("  PASS\n");
  return 0;
}

/*============================================================================
 * AND Rule Evaluation Tests
 *============================================================================*/

static int test_and_both_true(void) {
  printf("Testing AND both children true...\n");
  Samrena *arena = samrena_create_default();
  double closes[] = {100.0};
  SamrenaVector *ohlcv = make_ohlcv(arena, closes, 1);

  /* AND(ABOVE(close, 50), BELOW(close, 200)) with close=100 -> true */
  SamtraderRule *above =
      samtrader_rule_create_comparison(arena, SAMTRADER_RULE_ABOVE,
                                       samtrader_operand_price(SAMTRADER_OPERAND_PRICE_CLOSE),
                                       samtrader_operand_constant(50.0));
  SamtraderRule *below =
      samtrader_rule_create_comparison(arena, SAMTRADER_RULE_BELOW,
                                       samtrader_operand_price(SAMTRADER_OPERAND_PRICE_CLOSE),
                                       samtrader_operand_constant(200.0));
  SamtraderRule *children[] = {above, below};
  SamtraderRule *rule = samtrader_rule_create_composite(arena, SAMTRADER_RULE_AND, children, 2);

  ASSERT(samtrader_rule_evaluate(rule, ohlcv, NULL, 0), "AND(100>50, 100<200) should be true");

  samrena_destroy(arena);
  printf("  PASS\n");
  return 0;
}

static int test_and_one_false(void) {
  printf("Testing AND one child false...\n");
  Samrena *arena = samrena_create_default();
  double closes[] = {100.0};
  SamrenaVector *ohlcv = make_ohlcv(arena, closes, 1);

  /* AND(ABOVE(close, 50), ABOVE(close, 200)) with close=100 -> false */
  SamtraderRule *above1 =
      samtrader_rule_create_comparison(arena, SAMTRADER_RULE_ABOVE,
                                       samtrader_operand_price(SAMTRADER_OPERAND_PRICE_CLOSE),
                                       samtrader_operand_constant(50.0));
  SamtraderRule *above2 =
      samtrader_rule_create_comparison(arena, SAMTRADER_RULE_ABOVE,
                                       samtrader_operand_price(SAMTRADER_OPERAND_PRICE_CLOSE),
                                       samtrader_operand_constant(200.0));
  SamtraderRule *children[] = {above1, above2};
  SamtraderRule *rule = samtrader_rule_create_composite(arena, SAMTRADER_RULE_AND, children, 2);

  ASSERT(!samtrader_rule_evaluate(rule, ohlcv, NULL, 0), "AND(100>50, 100>200) should be false");

  samrena_destroy(arena);
  printf("  PASS\n");
  return 0;
}

static int test_and_null_children(void) {
  printf("Testing AND with NULL children...\n");
  Samrena *arena = samrena_create_default();
  double closes[] = {100.0};
  SamrenaVector *ohlcv = make_ohlcv(arena, closes, 1);

  SamtraderRule *rule = samtrader_rule_create_composite(arena, SAMTRADER_RULE_AND, NULL, 0);

  ASSERT(rule == NULL || !samtrader_rule_evaluate(rule, ohlcv, NULL, 0),
         "AND with NULL children should return false");

  samrena_destroy(arena);
  printf("  PASS\n");
  return 0;
}

/*============================================================================
 * OR Rule Evaluation Tests
 *============================================================================*/

static int test_or_one_true(void) {
  printf("Testing OR one child true...\n");
  Samrena *arena = samrena_create_default();
  double closes[] = {100.0};
  SamrenaVector *ohlcv = make_ohlcv(arena, closes, 1);

  /* OR(ABOVE(close, 200), ABOVE(close, 50)) with close=100 -> true */
  SamtraderRule *above1 =
      samtrader_rule_create_comparison(arena, SAMTRADER_RULE_ABOVE,
                                       samtrader_operand_price(SAMTRADER_OPERAND_PRICE_CLOSE),
                                       samtrader_operand_constant(200.0));
  SamtraderRule *above2 =
      samtrader_rule_create_comparison(arena, SAMTRADER_RULE_ABOVE,
                                       samtrader_operand_price(SAMTRADER_OPERAND_PRICE_CLOSE),
                                       samtrader_operand_constant(50.0));
  SamtraderRule *children[] = {above1, above2};
  SamtraderRule *rule = samtrader_rule_create_composite(arena, SAMTRADER_RULE_OR, children, 2);

  ASSERT(samtrader_rule_evaluate(rule, ohlcv, NULL, 0), "OR(100>200, 100>50) should be true");

  samrena_destroy(arena);
  printf("  PASS\n");
  return 0;
}

static int test_or_none_true(void) {
  printf("Testing OR none true...\n");
  Samrena *arena = samrena_create_default();
  double closes[] = {100.0};
  SamrenaVector *ohlcv = make_ohlcv(arena, closes, 1);

  /* OR(ABOVE(close, 200), ABOVE(close, 300)) with close=100 -> false */
  SamtraderRule *above1 =
      samtrader_rule_create_comparison(arena, SAMTRADER_RULE_ABOVE,
                                       samtrader_operand_price(SAMTRADER_OPERAND_PRICE_CLOSE),
                                       samtrader_operand_constant(200.0));
  SamtraderRule *above2 =
      samtrader_rule_create_comparison(arena, SAMTRADER_RULE_ABOVE,
                                       samtrader_operand_price(SAMTRADER_OPERAND_PRICE_CLOSE),
                                       samtrader_operand_constant(300.0));
  SamtraderRule *children[] = {above1, above2};
  SamtraderRule *rule = samtrader_rule_create_composite(arena, SAMTRADER_RULE_OR, children, 2);

  ASSERT(!samtrader_rule_evaluate(rule, ohlcv, NULL, 0), "OR(100>200, 100>300) should be false");

  samrena_destroy(arena);
  printf("  PASS\n");
  return 0;
}

static int test_or_null_children(void) {
  printf("Testing OR with NULL children...\n");
  Samrena *arena = samrena_create_default();
  double closes[] = {100.0};
  SamrenaVector *ohlcv = make_ohlcv(arena, closes, 1);

  SamtraderRule *rule = samtrader_rule_create_composite(arena, SAMTRADER_RULE_OR, NULL, 0);

  ASSERT(rule == NULL || !samtrader_rule_evaluate(rule, ohlcv, NULL, 0),
         "OR with NULL children should return false");

  samrena_destroy(arena);
  printf("  PASS\n");
  return 0;
}

/*============================================================================
 * NOT Rule Evaluation Tests
 *============================================================================*/

static int test_not_true_to_false(void) {
  printf("Testing NOT true->false...\n");
  Samrena *arena = samrena_create_default();
  double closes[] = {100.0};
  SamrenaVector *ohlcv = make_ohlcv(arena, closes, 1);

  /* NOT(ABOVE(close, 50)) with close=100 -> inner true, NOT -> false */
  SamtraderRule *above =
      samtrader_rule_create_comparison(arena, SAMTRADER_RULE_ABOVE,
                                       samtrader_operand_price(SAMTRADER_OPERAND_PRICE_CLOSE),
                                       samtrader_operand_constant(50.0));
  SamtraderRule *rule = samtrader_rule_create_not(arena, above);

  ASSERT(!samtrader_rule_evaluate(rule, ohlcv, NULL, 0), "NOT(100>50) should be false");

  samrena_destroy(arena);
  printf("  PASS\n");
  return 0;
}

static int test_not_false_to_true(void) {
  printf("Testing NOT false->true...\n");
  Samrena *arena = samrena_create_default();
  double closes[] = {100.0};
  SamrenaVector *ohlcv = make_ohlcv(arena, closes, 1);

  /* NOT(ABOVE(close, 200)) with close=100 -> inner false, NOT -> true */
  SamtraderRule *above =
      samtrader_rule_create_comparison(arena, SAMTRADER_RULE_ABOVE,
                                       samtrader_operand_price(SAMTRADER_OPERAND_PRICE_CLOSE),
                                       samtrader_operand_constant(200.0));
  SamtraderRule *rule = samtrader_rule_create_not(arena, above);

  ASSERT(samtrader_rule_evaluate(rule, ohlcv, NULL, 0), "NOT(100>200) should be true");

  samrena_destroy(arena);
  printf("  PASS\n");
  return 0;
}

static int test_not_null_child(void) {
  printf("Testing NOT with NULL child...\n");
  Samrena *arena = samrena_create_default();
  double closes[] = {100.0};
  SamrenaVector *ohlcv = make_ohlcv(arena, closes, 1);

  SamtraderRule *rule = samtrader_rule_create_not(arena, NULL);

  ASSERT(rule == NULL || !samtrader_rule_evaluate(rule, ohlcv, NULL, 0),
         "NOT with NULL child should return false");

  samrena_destroy(arena);
  printf("  PASS\n");
  return 0;
}

/*============================================================================
 * Nested Composite Evaluation Tests
 *============================================================================*/

static int test_nested_and_or(void) {
  printf("Testing nested AND(OR(...), ...)...\n");
  Samrena *arena = samrena_create_default();
  double closes[] = {100.0};
  SamrenaVector *ohlcv = make_ohlcv(arena, closes, 1);

  /* AND(OR(ABOVE(close,200), ABOVE(close,50)), BELOW(close,150)) with close=100
   * OR: 100>200=false, 100>50=true -> true
   * AND: true && 100<150=true -> true */
  SamtraderRule *above200 =
      samtrader_rule_create_comparison(arena, SAMTRADER_RULE_ABOVE,
                                       samtrader_operand_price(SAMTRADER_OPERAND_PRICE_CLOSE),
                                       samtrader_operand_constant(200.0));
  SamtraderRule *above50 =
      samtrader_rule_create_comparison(arena, SAMTRADER_RULE_ABOVE,
                                       samtrader_operand_price(SAMTRADER_OPERAND_PRICE_CLOSE),
                                       samtrader_operand_constant(50.0));
  SamtraderRule *or_children[] = {above200, above50};
  SamtraderRule *or_rule =
      samtrader_rule_create_composite(arena, SAMTRADER_RULE_OR, or_children, 2);

  SamtraderRule *below150 =
      samtrader_rule_create_comparison(arena, SAMTRADER_RULE_BELOW,
                                       samtrader_operand_price(SAMTRADER_OPERAND_PRICE_CLOSE),
                                       samtrader_operand_constant(150.0));

  SamtraderRule *and_children[] = {or_rule, below150};
  SamtraderRule *rule = samtrader_rule_create_composite(arena, SAMTRADER_RULE_AND, and_children, 2);

  ASSERT(samtrader_rule_evaluate(rule, ohlcv, NULL, 0), "AND(OR(false,true), true) should be true");

  samrena_destroy(arena);
  printf("  PASS\n");
  return 0;
}

static int test_nested_or_and(void) {
  printf("Testing nested OR(AND(...), ...)...\n");
  Samrena *arena = samrena_create_default();
  double closes[] = {100.0};
  SamrenaVector *ohlcv = make_ohlcv(arena, closes, 1);

  /* OR(AND(ABOVE(close,200), BELOW(close,50)), ABOVE(close,80)) with close=100
   * AND: 100>200=false, 100<50=false -> false
   * OR: false || 100>80=true -> true */
  SamtraderRule *above200 =
      samtrader_rule_create_comparison(arena, SAMTRADER_RULE_ABOVE,
                                       samtrader_operand_price(SAMTRADER_OPERAND_PRICE_CLOSE),
                                       samtrader_operand_constant(200.0));
  SamtraderRule *below50 =
      samtrader_rule_create_comparison(arena, SAMTRADER_RULE_BELOW,
                                       samtrader_operand_price(SAMTRADER_OPERAND_PRICE_CLOSE),
                                       samtrader_operand_constant(50.0));
  SamtraderRule *and_children[] = {above200, below50};
  SamtraderRule *and_rule =
      samtrader_rule_create_composite(arena, SAMTRADER_RULE_AND, and_children, 2);

  SamtraderRule *above80 =
      samtrader_rule_create_comparison(arena, SAMTRADER_RULE_ABOVE,
                                       samtrader_operand_price(SAMTRADER_OPERAND_PRICE_CLOSE),
                                       samtrader_operand_constant(80.0));

  SamtraderRule *or_children[] = {and_rule, above80};
  SamtraderRule *rule = samtrader_rule_create_composite(arena, SAMTRADER_RULE_OR, or_children, 2);

  ASSERT(samtrader_rule_evaluate(rule, ohlcv, NULL, 0),
         "OR(AND(false,false), true) should be true");

  samrena_destroy(arena);
  printf("  PASS\n");
  return 0;
}

static int test_not_composite(void) {
  printf("Testing NOT(AND(...))...\n");
  Samrena *arena = samrena_create_default();
  double closes[] = {100.0};
  SamrenaVector *ohlcv = make_ohlcv(arena, closes, 1);

  /* NOT(AND(ABOVE(close,50), BELOW(close,200))) with close=100
   * AND: 100>50=true, 100<200=true -> true
   * NOT: !true -> false */
  SamtraderRule *above50 =
      samtrader_rule_create_comparison(arena, SAMTRADER_RULE_ABOVE,
                                       samtrader_operand_price(SAMTRADER_OPERAND_PRICE_CLOSE),
                                       samtrader_operand_constant(50.0));
  SamtraderRule *below200 =
      samtrader_rule_create_comparison(arena, SAMTRADER_RULE_BELOW,
                                       samtrader_operand_price(SAMTRADER_OPERAND_PRICE_CLOSE),
                                       samtrader_operand_constant(200.0));
  SamtraderRule *and_children[] = {above50, below200};
  SamtraderRule *and_rule =
      samtrader_rule_create_composite(arena, SAMTRADER_RULE_AND, and_children, 2);

  SamtraderRule *rule = samtrader_rule_create_not(arena, and_rule);

  ASSERT(!samtrader_rule_evaluate(rule, ohlcv, NULL, 0), "NOT(AND(true,true)) should be false");

  samrena_destroy(arena);
  printf("  PASS\n");
  return 0;
}

/*============================================================================
 * Temporal + Composite Combination Tests
 *============================================================================*/

static int test_consecutive_and_child(void) {
  printf("Testing CONSECUTIVE(AND(...), 3)...\n");
  Samrena *arena = samrena_create_default();
  /* All closes: >50 and <200 for all bars */
  double closes[] = {100.0, 110.0, 120.0, 130.0};
  SamrenaVector *ohlcv = make_ohlcv(arena, closes, 4);

  /* CONSECUTIVE(AND(ABOVE(close,50), BELOW(close,200)), 3) */
  SamtraderRule *above50 =
      samtrader_rule_create_comparison(arena, SAMTRADER_RULE_ABOVE,
                                       samtrader_operand_price(SAMTRADER_OPERAND_PRICE_CLOSE),
                                       samtrader_operand_constant(50.0));
  SamtraderRule *below200 =
      samtrader_rule_create_comparison(arena, SAMTRADER_RULE_BELOW,
                                       samtrader_operand_price(SAMTRADER_OPERAND_PRICE_CLOSE),
                                       samtrader_operand_constant(200.0));
  SamtraderRule *and_children[] = {above50, below200};
  SamtraderRule *and_rule =
      samtrader_rule_create_composite(arena, SAMTRADER_RULE_AND, and_children, 2);

  SamtraderRule *rule =
      samtrader_rule_create_temporal(arena, SAMTRADER_RULE_CONSECUTIVE, and_rule, 3);

  /* Index 2: bars [0,1,2] all satisfy AND -> true */
  ASSERT(samtrader_rule_evaluate(rule, ohlcv, NULL, 2), "3 consecutive bars satisfying AND");
  /* Index 3: bars [1,2,3] all satisfy AND -> true */
  ASSERT(samtrader_rule_evaluate(rule, ohlcv, NULL, 3), "3 consecutive bars satisfying AND at end");

  samrena_destroy(arena);
  printf("  PASS\n");
  return 0;
}

static int test_any_of_or_child(void) {
  printf("Testing ANY_OF(OR(...), 3)...\n");
  Samrena *arena = samrena_create_default();
  /* Only bar 1 has close > 200 */
  double closes[] = {50.0, 250.0, 30.0, 20.0, 10.0};
  SamrenaVector *ohlcv = make_ohlcv(arena, closes, 5);

  /* ANY_OF(OR(ABOVE(close,200), ABOVE(close,300)), 3) */
  SamtraderRule *above200 =
      samtrader_rule_create_comparison(arena, SAMTRADER_RULE_ABOVE,
                                       samtrader_operand_price(SAMTRADER_OPERAND_PRICE_CLOSE),
                                       samtrader_operand_constant(200.0));
  SamtraderRule *above300 =
      samtrader_rule_create_comparison(arena, SAMTRADER_RULE_ABOVE,
                                       samtrader_operand_price(SAMTRADER_OPERAND_PRICE_CLOSE),
                                       samtrader_operand_constant(300.0));
  SamtraderRule *or_children[] = {above200, above300};
  SamtraderRule *or_rule =
      samtrader_rule_create_composite(arena, SAMTRADER_RULE_OR, or_children, 2);

  SamtraderRule *rule = samtrader_rule_create_temporal(arena, SAMTRADER_RULE_ANY_OF, or_rule, 3);

  /* Index 2: window [0,1,2], bar 1 has 250>200 -> true */
  ASSERT(samtrader_rule_evaluate(rule, ohlcv, NULL, 2), "Found OR match in window [0,1,2]");
  /* Index 3: window [1,2,3], bar 1 has 250>200 -> true */
  ASSERT(samtrader_rule_evaluate(rule, ohlcv, NULL, 3), "Found OR match in window [1,2,3]");
  /* Index 4: window [2,3,4] = [30,20,10], none match -> false */
  ASSERT(!samtrader_rule_evaluate(rule, ohlcv, NULL, 4), "No OR match in window [2,3,4]");

  samrena_destroy(arena);
  printf("  PASS\n");
  return 0;
}

static int test_and_temporal_children(void) {
  printf("Testing AND(CONSECUTIVE(...), ANY_OF(...))...\n");
  Samrena *arena = samrena_create_default();
  /* close: 100, 110, 120, 95, 130 */
  double closes[] = {100.0, 110.0, 120.0, 95.0, 130.0};
  SamrenaVector *ohlcv = make_ohlcv(arena, closes, 5);

  /* AND(CONSECUTIVE(ABOVE(close,50), 3), ANY_OF(ABOVE(close,90), 3)) */
  SamtraderRule *above50 =
      samtrader_rule_create_comparison(arena, SAMTRADER_RULE_ABOVE,
                                       samtrader_operand_price(SAMTRADER_OPERAND_PRICE_CLOSE),
                                       samtrader_operand_constant(50.0));
  SamtraderRule *consec =
      samtrader_rule_create_temporal(arena, SAMTRADER_RULE_CONSECUTIVE, above50, 3);

  SamtraderRule *above90 =
      samtrader_rule_create_comparison(arena, SAMTRADER_RULE_ABOVE,
                                       samtrader_operand_price(SAMTRADER_OPERAND_PRICE_CLOSE),
                                       samtrader_operand_constant(90.0));
  SamtraderRule *any_of = samtrader_rule_create_temporal(arena, SAMTRADER_RULE_ANY_OF, above90, 3);

  SamtraderRule *and_children[] = {consec, any_of};
  SamtraderRule *rule = samtrader_rule_create_composite(arena, SAMTRADER_RULE_AND, and_children, 2);

  /* Index 2: CONSECUTIVE bars [0,1,2]=[100,110,120] all>50 -> true
   *          ANY_OF bars [0,1,2]=[100,110,120] any>90 -> true
   *          AND -> true */
  ASSERT(samtrader_rule_evaluate(rule, ohlcv, NULL, 2),
         "AND(CONSECUTIVE, ANY_OF) both true at index 2");

  /* Index 3: CONSECUTIVE bars [1,2,3]=[110,120,95] all>50 -> true
   *          ANY_OF bars [1,2,3]=[110,120,95] any>90 -> true (110,120)
   *          AND -> true */
  ASSERT(samtrader_rule_evaluate(rule, ohlcv, NULL, 3),
         "AND(CONSECUTIVE, ANY_OF) both true at index 3");

  samrena_destroy(arena);
  printf("  PASS\n");
  return 0;
}

static int test_deep_nesting(void) {
  printf("Testing deep nesting NOT(AND(ABOVE, CONSECUTIVE))...\n");
  Samrena *arena = samrena_create_default();
  double closes[] = {100.0, 110.0, 120.0};
  SamrenaVector *ohlcv = make_ohlcv(arena, closes, 3);

  /* NOT(AND(ABOVE(close,50), CONSECUTIVE(BELOW(close,200), 2)))
   * At index 2:
   *   ABOVE(close=120, 50) -> true
   *   CONSECUTIVE(BELOW(close,200), 2): bars [1,2]=[110,120] both <200 -> true
   *   AND -> true
   *   NOT -> false */
  SamtraderRule *above50 =
      samtrader_rule_create_comparison(arena, SAMTRADER_RULE_ABOVE,
                                       samtrader_operand_price(SAMTRADER_OPERAND_PRICE_CLOSE),
                                       samtrader_operand_constant(50.0));
  SamtraderRule *below200 =
      samtrader_rule_create_comparison(arena, SAMTRADER_RULE_BELOW,
                                       samtrader_operand_price(SAMTRADER_OPERAND_PRICE_CLOSE),
                                       samtrader_operand_constant(200.0));
  SamtraderRule *consec =
      samtrader_rule_create_temporal(arena, SAMTRADER_RULE_CONSECUTIVE, below200, 2);

  SamtraderRule *and_children[] = {above50, consec};
  SamtraderRule *and_rule =
      samtrader_rule_create_composite(arena, SAMTRADER_RULE_AND, and_children, 2);

  SamtraderRule *rule = samtrader_rule_create_not(arena, and_rule);

  ASSERT(!samtrader_rule_evaluate(rule, ohlcv, NULL, 2),
         "NOT(AND(true, CONSECUTIVE(true,2))) should be false");

  samrena_destroy(arena);
  printf("  PASS\n");
  return 0;
}

/*============================================================================
 * Main
 *============================================================================*/

int main(void) {
  int failures = 0;

  printf("=== Rule Evaluation Tests ===\n\n");

  /* Indicator key tests */
  failures += test_indicator_key_simple();
  failures += test_indicator_key_multi();
  failures += test_indicator_key_invalid();

  /* Null / invalid input tests */
  failures += test_evaluate_null_rule();
  failures += test_evaluate_null_ohlcv();

  /* ABOVE tests */
  failures += test_above_constants();
  failures += test_above_price_vs_constant();
  failures += test_above_price_fields();
  failures += test_above_with_indicator();
  failures += test_above_volume();

  /* BELOW tests */
  failures += test_below_basic();
  failures += test_below_equal_values();

  /* EQUALS tests */
  failures += test_equals_exact();
  failures += test_equals_within_tolerance();

  /* BETWEEN tests */
  failures += test_between_in_range();
  failures += test_between_out_of_range();
  failures += test_between_at_boundaries();
  failures += test_between_price_constant();

  /* CROSS_ABOVE tests */
  failures += test_cross_above_basic();
  failures += test_cross_above_index_zero();
  failures += test_cross_above_with_constants();

  /* CROSS_BELOW tests */
  failures += test_cross_below_basic();
  failures += test_cross_below_index_zero();

  /* Multi-type indicator tests */
  failures += test_above_bollinger_upper();
  failures += test_below_bollinger_lower();
  failures += test_above_pivot_r1();
  failures += test_above_macd();

  /* Error cases */
  failures += test_missing_indicator();
  failures += test_null_indicators_map();
  failures += test_out_of_bounds_index();

  /* Two-indicator cross tests */
  failures += test_cross_above_two_indicators();

  /* CONSECUTIVE tests */
  failures += test_consecutive_all_true();
  failures += test_consecutive_broken_streak();
  failures += test_consecutive_lookback_one();
  failures += test_consecutive_with_indicator();
  failures += test_consecutive_null_child();

  /* ANY_OF tests */
  failures += test_any_of_found();
  failures += test_any_of_not_found();
  failures += test_any_of_insufficient_lookback();
  failures += test_any_of_lookback_one();
  failures += test_any_of_cross_above();
  failures += test_any_of_null_child();

  /* AND evaluation tests */
  failures += test_and_both_true();
  failures += test_and_one_false();
  failures += test_and_null_children();

  /* OR evaluation tests */
  failures += test_or_one_true();
  failures += test_or_none_true();
  failures += test_or_null_children();

  /* NOT evaluation tests */
  failures += test_not_true_to_false();
  failures += test_not_false_to_true();
  failures += test_not_null_child();

  /* Nested composite evaluation tests */
  failures += test_nested_and_or();
  failures += test_nested_or_and();
  failures += test_not_composite();

  /* Temporal + composite combination tests */
  failures += test_consecutive_and_child();
  failures += test_any_of_or_child();
  failures += test_and_temporal_children();
  failures += test_deep_nesting();

  printf("\n=== Results: %d failures ===\n", failures);

  return failures > 0 ? EXIT_FAILURE : EXIT_SUCCESS;
}
