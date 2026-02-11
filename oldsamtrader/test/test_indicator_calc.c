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

#include "samtrader/domain/indicator.h"
#include "samtrader/domain/ohlcv.h"

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

/* Helper to create test OHLCV data with known close prices */
static SamrenaVector *create_test_ohlcv(Samrena *arena, const double *closes, size_t count) {
  SamrenaVector *vec = samtrader_ohlcv_vector_create(arena, count);
  if (!vec) {
    return NULL;
  }

  for (size_t i = 0; i < count; i++) {
    SamtraderOhlcv bar = {.code = "TEST",
                          .exchange = "US",
                          .date = (time_t)(1704067200 + i * 86400),
                          .open = closes[i],
                          .high = closes[i] + 1.0,
                          .low = closes[i] - 1.0,
                          .close = closes[i],
                          .volume = 1000000};
    samrena_vector_push(vec, &bar);
  }

  return vec;
}

/*============================================================================
 * SMA Tests
 *============================================================================*/

static int test_sma_basic(void) {
  printf("Testing SMA basic calculation...\n");

  Samrena *arena = samrena_create_default();
  ASSERT(arena != NULL, "Failed to create arena");

  /* Simple data: 1, 2, 3, 4, 5 with period 3 */
  double closes[] = {1.0, 2.0, 3.0, 4.0, 5.0};
  SamrenaVector *ohlcv = create_test_ohlcv(arena, closes, 5);
  ASSERT(ohlcv != NULL, "Failed to create OHLCV data");

  SamtraderIndicatorSeries *sma = samtrader_calculate_sma(arena, ohlcv, 3);
  ASSERT(sma != NULL, "Failed to calculate SMA");
  ASSERT(sma->type == SAMTRADER_IND_SMA, "Type should be SMA");
  ASSERT(sma->params.period == 3, "Period should be 3");
  ASSERT(samtrader_indicator_series_size(sma) == 5, "Should have 5 values");

  /* First two values should be invalid (warmup) */
  const SamtraderIndicatorValue *val = samtrader_indicator_series_at(sma, 0);
  ASSERT(val != NULL && val->valid == false, "Index 0 should be invalid");

  val = samtrader_indicator_series_at(sma, 1);
  ASSERT(val != NULL && val->valid == false, "Index 1 should be invalid");

  /* SMA(3) at index 2: (1+2+3)/3 = 2.0 */
  val = samtrader_indicator_series_at(sma, 2);
  ASSERT(val != NULL && val->valid == true, "Index 2 should be valid");
  ASSERT_DOUBLE_EQ(val->data.simple.value, 2.0, "SMA at index 2");

  /* SMA(3) at index 3: (2+3+4)/3 = 3.0 */
  val = samtrader_indicator_series_at(sma, 3);
  ASSERT(val != NULL && val->valid == true, "Index 3 should be valid");
  ASSERT_DOUBLE_EQ(val->data.simple.value, 3.0, "SMA at index 3");

  /* SMA(3) at index 4: (3+4+5)/3 = 4.0 */
  val = samtrader_indicator_series_at(sma, 4);
  ASSERT(val != NULL && val->valid == true, "Index 4 should be valid");
  ASSERT_DOUBLE_EQ(val->data.simple.value, 4.0, "SMA at index 4");

  samrena_destroy(arena);
  printf("  PASS\n");
  return 0;
}

static int test_sma_period_1(void) {
  printf("Testing SMA with period 1...\n");

  Samrena *arena = samrena_create_default();
  ASSERT(arena != NULL, "Failed to create arena");

  double closes[] = {10.0, 20.0, 30.0};
  SamrenaVector *ohlcv = create_test_ohlcv(arena, closes, 3);
  ASSERT(ohlcv != NULL, "Failed to create OHLCV data");

  SamtraderIndicatorSeries *sma = samtrader_calculate_sma(arena, ohlcv, 1);
  ASSERT(sma != NULL, "Failed to calculate SMA");

  /* All values should be valid and equal to the close price */
  for (size_t i = 0; i < 3; i++) {
    const SamtraderIndicatorValue *val = samtrader_indicator_series_at(sma, i);
    ASSERT(val != NULL && val->valid == true, "All values should be valid");
    ASSERT_DOUBLE_EQ(val->data.simple.value, closes[i], "SMA(1) should equal close price");
  }

  samrena_destroy(arena);
  printf("  PASS\n");
  return 0;
}

static int test_sma_invalid_params(void) {
  printf("Testing SMA with invalid parameters...\n");

  Samrena *arena = samrena_create_default();
  ASSERT(arena != NULL, "Failed to create arena");

  double closes[] = {1.0, 2.0, 3.0};
  SamrenaVector *ohlcv = create_test_ohlcv(arena, closes, 3);

  /* NULL arena */
  ASSERT(samtrader_calculate_sma(NULL, ohlcv, 3) == NULL, "NULL arena should fail");

  /* NULL ohlcv */
  ASSERT(samtrader_calculate_sma(arena, NULL, 3) == NULL, "NULL ohlcv should fail");

  /* Invalid period */
  ASSERT(samtrader_calculate_sma(arena, ohlcv, 0) == NULL, "Period 0 should fail");
  ASSERT(samtrader_calculate_sma(arena, ohlcv, -1) == NULL, "Negative period should fail");

  /* Empty vector */
  SamrenaVector *empty = samtrader_ohlcv_vector_create(arena, 10);
  ASSERT(samtrader_calculate_sma(arena, empty, 3) == NULL, "Empty vector should fail");

  samrena_destroy(arena);
  printf("  PASS\n");
  return 0;
}

static int test_sma_constant_prices(void) {
  printf("Testing SMA with constant prices...\n");

  Samrena *arena = samrena_create_default();
  ASSERT(arena != NULL, "Failed to create arena");

  double closes[] = {50.0, 50.0, 50.0, 50.0, 50.0, 50.0};
  SamrenaVector *ohlcv = create_test_ohlcv(arena, closes, 6);

  SamtraderIndicatorSeries *sma = samtrader_calculate_sma(arena, ohlcv, 3);
  ASSERT(sma != NULL, "Failed to calculate SMA");

  for (size_t i = 2; i < 6; i++) {
    const SamtraderIndicatorValue *val = samtrader_indicator_series_at(sma, i);
    ASSERT(val != NULL && val->valid == true, "Should be valid");
    ASSERT_DOUBLE_EQ(val->data.simple.value, 50.0, "SMA should be 50 for constant prices");
  }

  samrena_destroy(arena);
  printf("  PASS\n");
  return 0;
}

static int test_sma_latest_value(void) {
  printf("Testing SMA latest value retrieval...\n");

  Samrena *arena = samrena_create_default();
  ASSERT(arena != NULL, "Failed to create arena");

  double closes[] = {1.0, 2.0, 3.0, 4.0, 5.0};
  SamrenaVector *ohlcv = create_test_ohlcv(arena, closes, 5);

  SamtraderIndicatorSeries *sma = samtrader_calculate_sma(arena, ohlcv, 3);
  ASSERT(sma != NULL, "Failed to calculate SMA");

  double latest;
  ASSERT(samtrader_indicator_latest_simple(sma, &latest) == true, "Should find latest valid value");
  ASSERT_DOUBLE_EQ(latest, 4.0, "Latest SMA should be (3+4+5)/3 = 4.0");

  samrena_destroy(arena);
  printf("  PASS\n");
  return 0;
}

/*============================================================================
 * EMA Tests
 *============================================================================*/

static int test_ema_basic(void) {
  printf("Testing EMA basic calculation...\n");

  Samrena *arena = samrena_create_default();
  ASSERT(arena != NULL, "Failed to create arena");

  /* Data: 1, 2, 3, 4, 5 with period 3 */
  double closes[] = {1.0, 2.0, 3.0, 4.0, 5.0};
  SamrenaVector *ohlcv = create_test_ohlcv(arena, closes, 5);
  ASSERT(ohlcv != NULL, "Failed to create OHLCV data");

  SamtraderIndicatorSeries *ema = samtrader_calculate_ema(arena, ohlcv, 3);
  ASSERT(ema != NULL, "Failed to calculate EMA");
  ASSERT(ema->type == SAMTRADER_IND_EMA, "Type should be EMA");
  ASSERT(ema->params.period == 3, "Period should be 3");

  /* First two values should be invalid */
  const SamtraderIndicatorValue *val = samtrader_indicator_series_at(ema, 0);
  ASSERT(val != NULL && val->valid == false, "Index 0 should be invalid");

  val = samtrader_indicator_series_at(ema, 1);
  ASSERT(val != NULL && val->valid == false, "Index 1 should be invalid");

  /* EMA at index 2: initial value = SMA = (1+2+3)/3 = 2.0 */
  val = samtrader_indicator_series_at(ema, 2);
  ASSERT(val != NULL && val->valid == true, "Index 2 should be valid");
  ASSERT_DOUBLE_EQ(val->data.simple.value, 2.0, "EMA at index 2 (initial SMA)");

  /* EMA at index 3: k = 2/(3+1) = 0.5, EMA = 4*0.5 + 2.0*0.5 = 3.0 */
  val = samtrader_indicator_series_at(ema, 3);
  ASSERT(val != NULL && val->valid == true, "Index 3 should be valid");
  ASSERT_DOUBLE_EQ(val->data.simple.value, 3.0, "EMA at index 3");

  /* EMA at index 4: EMA = 5*0.5 + 3.0*0.5 = 4.0 */
  val = samtrader_indicator_series_at(ema, 4);
  ASSERT(val != NULL && val->valid == true, "Index 4 should be valid");
  ASSERT_DOUBLE_EQ(val->data.simple.value, 4.0, "EMA at index 4");

  samrena_destroy(arena);
  printf("  PASS\n");
  return 0;
}

static int test_ema_convergence(void) {
  printf("Testing EMA convergence to constant value...\n");

  Samrena *arena = samrena_create_default();
  ASSERT(arena != NULL, "Failed to create arena");

  /* Constant data should converge EMA to that value */
  double closes[] = {10.0, 10.0, 10.0, 10.0, 10.0, 10.0, 10.0, 10.0, 10.0, 10.0};
  SamrenaVector *ohlcv = create_test_ohlcv(arena, closes, 10);

  SamtraderIndicatorSeries *ema = samtrader_calculate_ema(arena, ohlcv, 3);
  ASSERT(ema != NULL, "Failed to calculate EMA");

  /* All valid values should equal 10.0 */
  for (size_t i = 2; i < 10; i++) {
    const SamtraderIndicatorValue *val = samtrader_indicator_series_at(ema, i);
    ASSERT(val != NULL && val->valid == true, "Should be valid");
    ASSERT_DOUBLE_EQ(val->data.simple.value, 10.0, "EMA should converge to constant");
  }

  samrena_destroy(arena);
  printf("  PASS\n");
  return 0;
}

static int test_ema_invalid_params(void) {
  printf("Testing EMA with invalid parameters...\n");

  Samrena *arena = samrena_create_default();
  ASSERT(arena != NULL, "Failed to create arena");

  double closes[] = {1.0, 2.0, 3.0};
  SamrenaVector *ohlcv = create_test_ohlcv(arena, closes, 3);

  ASSERT(samtrader_calculate_ema(NULL, ohlcv, 3) == NULL, "NULL arena should fail");
  ASSERT(samtrader_calculate_ema(arena, NULL, 3) == NULL, "NULL ohlcv should fail");
  ASSERT(samtrader_calculate_ema(arena, ohlcv, 0) == NULL, "Period 0 should fail");

  samrena_destroy(arena);
  printf("  PASS\n");
  return 0;
}

static int test_ema_period_1(void) {
  printf("Testing EMA with period 1...\n");

  Samrena *arena = samrena_create_default();
  ASSERT(arena != NULL, "Failed to create arena");

  double closes[] = {10.0, 20.0, 30.0};
  SamrenaVector *ohlcv = create_test_ohlcv(arena, closes, 3);

  SamtraderIndicatorSeries *ema = samtrader_calculate_ema(arena, ohlcv, 1);
  ASSERT(ema != NULL, "Failed to calculate EMA");

  /* EMA(1) with k=2/2=1.0 should equal the close price at every point */
  for (size_t i = 0; i < 3; i++) {
    const SamtraderIndicatorValue *val = samtrader_indicator_series_at(ema, i);
    ASSERT(val != NULL && val->valid == true, "All values should be valid");
    ASSERT_DOUBLE_EQ(val->data.simple.value, closes[i], "EMA(1) should equal close price");
  }

  samrena_destroy(arena);
  printf("  PASS\n");
  return 0;
}

static int test_ema_latest_value(void) {
  printf("Testing EMA latest value retrieval...\n");

  Samrena *arena = samrena_create_default();
  ASSERT(arena != NULL, "Failed to create arena");

  double closes[] = {1.0, 2.0, 3.0, 4.0, 5.0};
  SamrenaVector *ohlcv = create_test_ohlcv(arena, closes, 5);

  SamtraderIndicatorSeries *ema = samtrader_calculate_ema(arena, ohlcv, 3);
  ASSERT(ema != NULL, "Failed to calculate EMA");

  /* EMA(3) at index 4: k=0.5, EMA = 5*0.5 + 3.0*0.5 = 4.0 */
  double latest;
  ASSERT(samtrader_indicator_latest_simple(ema, &latest) == true, "Should find latest valid value");
  ASSERT_DOUBLE_EQ(latest, 4.0, "Latest EMA value");

  samrena_destroy(arena);
  printf("  PASS\n");
  return 0;
}

/*============================================================================
 * WMA Tests
 *============================================================================*/

static int test_wma_basic(void) {
  printf("Testing WMA basic calculation...\n");

  Samrena *arena = samrena_create_default();
  ASSERT(arena != NULL, "Failed to create arena");

  /* Data: 1, 2, 3, 4, 5 with period 3 */
  double closes[] = {1.0, 2.0, 3.0, 4.0, 5.0};
  SamrenaVector *ohlcv = create_test_ohlcv(arena, closes, 5);
  ASSERT(ohlcv != NULL, "Failed to create OHLCV data");

  SamtraderIndicatorSeries *wma = samtrader_calculate_wma(arena, ohlcv, 3);
  ASSERT(wma != NULL, "Failed to calculate WMA");
  ASSERT(wma->type == SAMTRADER_IND_WMA, "Type should be WMA");
  ASSERT(wma->params.period == 3, "Period should be 3");

  /* First two values should be invalid */
  const SamtraderIndicatorValue *val = samtrader_indicator_series_at(wma, 0);
  ASSERT(val != NULL && val->valid == false, "Index 0 should be invalid");

  val = samtrader_indicator_series_at(wma, 1);
  ASSERT(val != NULL && val->valid == false, "Index 1 should be invalid");

  /* WMA at index 2: (1*1 + 2*2 + 3*3) / (1+2+3) = (1+4+9)/6 = 14/6 = 2.333... */
  val = samtrader_indicator_series_at(wma, 2);
  ASSERT(val != NULL && val->valid == true, "Index 2 should be valid");
  ASSERT_DOUBLE_EQ(val->data.simple.value, 14.0 / 6.0, "WMA at index 2");

  /* WMA at index 3: (2*1 + 3*2 + 4*3) / 6 = (2+6+12)/6 = 20/6 = 3.333... */
  val = samtrader_indicator_series_at(wma, 3);
  ASSERT(val != NULL && val->valid == true, "Index 3 should be valid");
  ASSERT_DOUBLE_EQ(val->data.simple.value, 20.0 / 6.0, "WMA at index 3");

  /* WMA at index 4: (3*1 + 4*2 + 5*3) / 6 = (3+8+15)/6 = 26/6 = 4.333... */
  val = samtrader_indicator_series_at(wma, 4);
  ASSERT(val != NULL && val->valid == true, "Index 4 should be valid");
  ASSERT_DOUBLE_EQ(val->data.simple.value, 26.0 / 6.0, "WMA at index 4");

  samrena_destroy(arena);
  printf("  PASS\n");
  return 0;
}

static int test_wma_weighting(void) {
  printf("Testing WMA weighting behavior...\n");

  Samrena *arena = samrena_create_default();
  ASSERT(arena != NULL, "Failed to create arena");

  /* WMA should weight recent prices higher */
  /* With period 2: WMA = (old*1 + new*2) / 3 */
  double closes[] = {10.0, 20.0};
  SamrenaVector *ohlcv = create_test_ohlcv(arena, closes, 2);

  SamtraderIndicatorSeries *wma = samtrader_calculate_wma(arena, ohlcv, 2);
  ASSERT(wma != NULL, "Failed to calculate WMA");

  /* WMA at index 1: (10*1 + 20*2) / 3 = 50/3 = 16.666... */
  /* This is closer to 20 than SMA would be (15.0) */
  const SamtraderIndicatorValue *val = samtrader_indicator_series_at(wma, 1);
  ASSERT(val != NULL && val->valid == true, "Should be valid");
  ASSERT_DOUBLE_EQ(val->data.simple.value, 50.0 / 3.0, "WMA should weight recent higher");

  /* Verify it's greater than SMA */
  double sma = (10.0 + 20.0) / 2.0;
  ASSERT(val->data.simple.value > sma, "WMA should be > SMA when prices are rising");

  samrena_destroy(arena);
  printf("  PASS\n");
  return 0;
}

static int test_wma_invalid_params(void) {
  printf("Testing WMA with invalid parameters...\n");

  Samrena *arena = samrena_create_default();
  ASSERT(arena != NULL, "Failed to create arena");

  double closes[] = {1.0, 2.0, 3.0};
  SamrenaVector *ohlcv = create_test_ohlcv(arena, closes, 3);

  ASSERT(samtrader_calculate_wma(NULL, ohlcv, 3) == NULL, "NULL arena should fail");
  ASSERT(samtrader_calculate_wma(arena, NULL, 3) == NULL, "NULL ohlcv should fail");
  ASSERT(samtrader_calculate_wma(arena, ohlcv, 0) == NULL, "Period 0 should fail");

  samrena_destroy(arena);
  printf("  PASS\n");
  return 0;
}

static int test_wma_period_1(void) {
  printf("Testing WMA with period 1...\n");

  Samrena *arena = samrena_create_default();
  ASSERT(arena != NULL, "Failed to create arena");

  double closes[] = {10.0, 20.0, 30.0};
  SamrenaVector *ohlcv = create_test_ohlcv(arena, closes, 3);

  SamtraderIndicatorSeries *wma = samtrader_calculate_wma(arena, ohlcv, 1);
  ASSERT(wma != NULL, "Failed to calculate WMA");

  /* WMA(1) should equal the close price at every point */
  for (size_t i = 0; i < 3; i++) {
    const SamtraderIndicatorValue *val = samtrader_indicator_series_at(wma, i);
    ASSERT(val != NULL && val->valid == true, "All values should be valid");
    ASSERT_DOUBLE_EQ(val->data.simple.value, closes[i], "WMA(1) should equal close price");
  }

  samrena_destroy(arena);
  printf("  PASS\n");
  return 0;
}

static int test_wma_constant_prices(void) {
  printf("Testing WMA with constant prices...\n");

  Samrena *arena = samrena_create_default();
  ASSERT(arena != NULL, "Failed to create arena");

  double closes[] = {50.0, 50.0, 50.0, 50.0, 50.0, 50.0};
  SamrenaVector *ohlcv = create_test_ohlcv(arena, closes, 6);

  SamtraderIndicatorSeries *wma = samtrader_calculate_wma(arena, ohlcv, 3);
  ASSERT(wma != NULL, "Failed to calculate WMA");

  for (size_t i = 2; i < 6; i++) {
    const SamtraderIndicatorValue *val = samtrader_indicator_series_at(wma, i);
    ASSERT(val != NULL && val->valid == true, "Should be valid");
    ASSERT_DOUBLE_EQ(val->data.simple.value, 50.0, "WMA should be 50 for constant prices");
  }

  samrena_destroy(arena);
  printf("  PASS\n");
  return 0;
}

static int test_wma_latest_value(void) {
  printf("Testing WMA latest value retrieval...\n");

  Samrena *arena = samrena_create_default();
  ASSERT(arena != NULL, "Failed to create arena");

  double closes[] = {1.0, 2.0, 3.0, 4.0, 5.0};
  SamrenaVector *ohlcv = create_test_ohlcv(arena, closes, 5);

  SamtraderIndicatorSeries *wma = samtrader_calculate_wma(arena, ohlcv, 3);
  ASSERT(wma != NULL, "Failed to calculate WMA");

  /* WMA at index 4: (3*1 + 4*2 + 5*3) / 6 = 26/6 */
  double latest;
  ASSERT(samtrader_indicator_latest_simple(wma, &latest) == true, "Should find latest valid value");
  ASSERT_DOUBLE_EQ(latest, 26.0 / 6.0, "Latest WMA value");

  samrena_destroy(arena);
  printf("  PASS\n");
  return 0;
}

/*============================================================================
 * RSI Tests
 *============================================================================*/

static int test_rsi_basic(void) {
  printf("Testing RSI basic calculation...\n");

  Samrena *arena = samrena_create_default();
  ASSERT(arena != NULL, "Failed to create arena");

  /* Data with alternating gains and losses, period 5 */
  double closes[] = {44.0,  44.25, 44.50, 43.75, 44.50, 44.25, 43.75, 44.00,
                     43.50, 44.00, 44.50, 44.25, 44.75, 45.00, 45.50};
  SamrenaVector *ohlcv = create_test_ohlcv(arena, closes, 15);
  ASSERT(ohlcv != NULL, "Failed to create OHLCV data");

  SamtraderIndicatorSeries *rsi = samtrader_calculate_rsi(arena, ohlcv, 5);
  ASSERT(rsi != NULL, "Failed to calculate RSI");
  ASSERT(rsi->type == SAMTRADER_IND_RSI, "Type should be RSI");
  ASSERT(rsi->params.period == 5, "Period should be 5");
  ASSERT(samtrader_indicator_series_size(rsi) == 15, "Should have 15 values");

  /* First 5 values (indices 0-4) should be invalid (warmup) */
  for (size_t i = 0; i < 5; i++) {
    const SamtraderIndicatorValue *val = samtrader_indicator_series_at(rsi, i);
    ASSERT(val != NULL && val->valid == false, "Warmup values should be invalid");
  }

  /* Index 5 should be the first valid RSI value */
  const SamtraderIndicatorValue *val = samtrader_indicator_series_at(rsi, 5);
  ASSERT(val != NULL && val->valid == true, "Index 5 should be valid");

  /* All valid RSI values should be in [0, 100] */
  for (size_t i = 5; i < 15; i++) {
    val = samtrader_indicator_series_at(rsi, i);
    ASSERT(val != NULL && val->valid == true, "Should be valid");
    ASSERT(val->data.simple.value >= 0.0 && val->data.simple.value <= 100.0,
           "RSI should be between 0 and 100");
  }

  samrena_destroy(arena);
  printf("  PASS\n");
  return 0;
}

static int test_rsi_all_gains(void) {
  printf("Testing RSI with all gains (monotonically rising)...\n");

  Samrena *arena = samrena_create_default();
  ASSERT(arena != NULL, "Failed to create arena");

  /* Monotonically rising prices */
  double closes[] = {1.0, 2.0, 3.0, 4.0, 5.0, 6.0, 7.0, 8.0, 9.0, 10.0};
  SamrenaVector *ohlcv = create_test_ohlcv(arena, closes, 10);

  SamtraderIndicatorSeries *rsi = samtrader_calculate_rsi(arena, ohlcv, 5);
  ASSERT(rsi != NULL, "Failed to calculate RSI");

  /* With all gains and no losses, RSI should be 100 */
  for (size_t i = 5; i < 10; i++) {
    const SamtraderIndicatorValue *val = samtrader_indicator_series_at(rsi, i);
    ASSERT(val != NULL && val->valid == true, "Should be valid");
    ASSERT_DOUBLE_EQ(val->data.simple.value, 100.0, "RSI should be 100 with all gains");
  }

  samrena_destroy(arena);
  printf("  PASS\n");
  return 0;
}

static int test_rsi_all_losses(void) {
  printf("Testing RSI with all losses (monotonically falling)...\n");

  Samrena *arena = samrena_create_default();
  ASSERT(arena != NULL, "Failed to create arena");

  /* Monotonically falling prices */
  double closes[] = {10.0, 9.0, 8.0, 7.0, 6.0, 5.0, 4.0, 3.0, 2.0, 1.0};
  SamrenaVector *ohlcv = create_test_ohlcv(arena, closes, 10);

  SamtraderIndicatorSeries *rsi = samtrader_calculate_rsi(arena, ohlcv, 5);
  ASSERT(rsi != NULL, "Failed to calculate RSI");

  /* With all losses and no gains, RSI should be 0 */
  for (size_t i = 5; i < 10; i++) {
    const SamtraderIndicatorValue *val = samtrader_indicator_series_at(rsi, i);
    ASSERT(val != NULL && val->valid == true, "Should be valid");
    ASSERT_DOUBLE_EQ(val->data.simple.value, 0.0, "RSI should be 0 with all losses");
  }

  samrena_destroy(arena);
  printf("  PASS\n");
  return 0;
}

static int test_rsi_constant(void) {
  printf("Testing RSI with constant prices...\n");

  Samrena *arena = samrena_create_default();
  ASSERT(arena != NULL, "Failed to create arena");

  /* Constant prices - no gains or losses */
  double closes[] = {50.0, 50.0, 50.0, 50.0, 50.0, 50.0, 50.0, 50.0};
  SamrenaVector *ohlcv = create_test_ohlcv(arena, closes, 8);

  SamtraderIndicatorSeries *rsi = samtrader_calculate_rsi(arena, ohlcv, 3);
  ASSERT(rsi != NULL, "Failed to calculate RSI");

  /* With no gains and no losses, RSI should be 50 */
  for (size_t i = 3; i < 8; i++) {
    const SamtraderIndicatorValue *val = samtrader_indicator_series_at(rsi, i);
    ASSERT(val != NULL && val->valid == true, "Should be valid");
    ASSERT_DOUBLE_EQ(val->data.simple.value, 50.0, "RSI should be 50 with constant prices");
  }

  samrena_destroy(arena);
  printf("  PASS\n");
  return 0;
}

static int test_rsi_period_1(void) {
  printf("Testing RSI with period 1...\n");

  Samrena *arena = samrena_create_default();
  ASSERT(arena != NULL, "Failed to create arena");

  double closes[] = {10.0, 12.0, 11.0, 13.0};
  SamrenaVector *ohlcv = create_test_ohlcv(arena, closes, 4);

  SamtraderIndicatorSeries *rsi = samtrader_calculate_rsi(arena, ohlcv, 1);
  ASSERT(rsi != NULL, "Failed to calculate RSI");

  /* Index 0 is invalid, rest should be valid */
  const SamtraderIndicatorValue *val = samtrader_indicator_series_at(rsi, 0);
  ASSERT(val != NULL && val->valid == false, "Index 0 should be invalid");

  /* Index 1: gain of 2.0, no loss -> RSI = 100 */
  val = samtrader_indicator_series_at(rsi, 1);
  ASSERT(val != NULL && val->valid == true, "Index 1 should be valid");
  ASSERT_DOUBLE_EQ(val->data.simple.value, 100.0, "RSI should be 100 for pure gain");

  /* Index 2: loss of 1.0, smoothed avg_gain decays -> RSI < 100 */
  val = samtrader_indicator_series_at(rsi, 2);
  ASSERT(val != NULL && val->valid == true, "Index 2 should be valid");
  ASSERT_DOUBLE_EQ(val->data.simple.value, 0.0, "RSI period 1 pure loss");

  /* Index 3: gain of 2.0 -> RSI = 100 */
  val = samtrader_indicator_series_at(rsi, 3);
  ASSERT(val != NULL && val->valid == true, "Index 3 should be valid");
  ASSERT_DOUBLE_EQ(val->data.simple.value, 100.0, "RSI should be 100 for pure gain");

  samrena_destroy(arena);
  printf("  PASS\n");
  return 0;
}

static int test_rsi_invalid_params(void) {
  printf("Testing RSI with invalid parameters...\n");

  Samrena *arena = samrena_create_default();
  ASSERT(arena != NULL, "Failed to create arena");

  double closes[] = {1.0, 2.0, 3.0};
  SamrenaVector *ohlcv = create_test_ohlcv(arena, closes, 3);

  ASSERT(samtrader_calculate_rsi(NULL, ohlcv, 14) == NULL, "NULL arena should fail");
  ASSERT(samtrader_calculate_rsi(arena, NULL, 14) == NULL, "NULL ohlcv should fail");
  ASSERT(samtrader_calculate_rsi(arena, ohlcv, 0) == NULL, "Period 0 should fail");
  ASSERT(samtrader_calculate_rsi(arena, ohlcv, -1) == NULL, "Negative period should fail");

  SamrenaVector *empty = samtrader_ohlcv_vector_create(arena, 10);
  ASSERT(samtrader_calculate_rsi(arena, empty, 14) == NULL, "Empty vector should fail");

  samrena_destroy(arena);
  printf("  PASS\n");
  return 0;
}

static int test_rsi_known_values(void) {
  printf("Testing RSI with hand-calculated values...\n");

  Samrena *arena = samrena_create_default();
  ASSERT(arena != NULL, "Failed to create arena");

  /* Simple dataset for manual verification with period 3:
   * Prices: 10, 12, 11, 13, 12, 14
   * Changes:    +2, -1, +2, -1, +2
   * First 3 changes (i=1,2,3): gains={2,0,2}=4, losses={0,1,0}=1
   * Avg gain = 4/3 = 1.3333, Avg loss = 1/3 = 0.3333
   * RS = 1.3333/0.3333 = 4.0, RSI = 100 - 100/(1+4) = 80.0
   */
  double closes[] = {10.0, 12.0, 11.0, 13.0, 12.0, 14.0};
  SamrenaVector *ohlcv = create_test_ohlcv(arena, closes, 6);

  SamtraderIndicatorSeries *rsi = samtrader_calculate_rsi(arena, ohlcv, 3);
  ASSERT(rsi != NULL, "Failed to calculate RSI");

  /* First valid RSI at index 3 */
  const SamtraderIndicatorValue *val = samtrader_indicator_series_at(rsi, 3);
  ASSERT(val != NULL && val->valid == true, "Index 3 should be valid");
  ASSERT_DOUBLE_EQ(val->data.simple.value, 80.0, "RSI at index 3");

  /* Index 4: change = -1 (loss)
   * Avg gain = (1.3333 * 2 + 0) / 3 = 0.8889
   * Avg loss = (0.3333 * 2 + 1) / 3 = 0.5556
   * RS = 0.8889 / 0.5556 = 1.6, RSI = 100 - 100/2.6 = 61.5385
   */
  val = samtrader_indicator_series_at(rsi, 4);
  ASSERT(val != NULL && val->valid == true, "Index 4 should be valid");
  ASSERT_DOUBLE_EQ(val->data.simple.value, 100.0 - 100.0 / 2.6, "RSI at index 4");

  /* Index 5: change = +2 (gain)
   * Avg gain = (0.8889 * 2 + 2) / 3 = 1.2593
   * Avg loss = (0.5556 * 2 + 0) / 3 = 0.3704
   * RS = 1.2593 / 0.3704 = 3.4, RSI = 100 - 100/4.4 = 77.2727
   */
  val = samtrader_indicator_series_at(rsi, 5);
  ASSERT(val != NULL && val->valid == true, "Index 5 should be valid");
  double expected_avg_gain = ((4.0 / 3.0) * 2.0 + 0.0) / 3.0;
  double expected_avg_loss = ((1.0 / 3.0) * 2.0 + 1.0) / 3.0;
  expected_avg_gain = (expected_avg_gain * 2.0 + 2.0) / 3.0;
  expected_avg_loss = (expected_avg_loss * 2.0 + 0.0) / 3.0;
  double expected_rs = expected_avg_gain / expected_avg_loss;
  double expected_rsi = 100.0 - (100.0 / (1.0 + expected_rs));
  ASSERT_DOUBLE_EQ(val->data.simple.value, expected_rsi, "RSI at index 5");

  samrena_destroy(arena);
  printf("  PASS\n");
  return 0;
}

static int test_rsi_latest_value(void) {
  printf("Testing RSI latest value retrieval...\n");

  Samrena *arena = samrena_create_default();
  ASSERT(arena != NULL, "Failed to create arena");

  /* All gains -> RSI = 100 */
  double closes[] = {1.0, 2.0, 3.0, 4.0, 5.0, 6.0, 7.0, 8.0};
  SamrenaVector *ohlcv = create_test_ohlcv(arena, closes, 8);

  SamtraderIndicatorSeries *rsi = samtrader_calculate_rsi(arena, ohlcv, 3);
  ASSERT(rsi != NULL, "Failed to calculate RSI");

  double latest;
  ASSERT(samtrader_indicator_latest_simple(rsi, &latest) == true, "Should find latest valid value");
  ASSERT_DOUBLE_EQ(latest, 100.0, "Latest RSI should be 100 for all gains");

  samrena_destroy(arena);
  printf("  PASS\n");
  return 0;
}

/*============================================================================
 * MACD Tests
 *============================================================================*/

static int test_macd_basic(void) {
  printf("Testing MACD basic calculation...\n");

  Samrena *arena = samrena_create_default();
  ASSERT(arena != NULL, "Failed to create arena");

  /* Linearly rising data with fast=3, slow=5, signal=3
   * With perfectly linear data, the MACD line converges to a constant.
   * Fast EMA converges faster than slow, so fast > slow when prices rise. */
  double closes[] = {10.0, 11.0, 12.0, 13.0, 14.0, 15.0, 16.0, 17.0, 18.0, 19.0};
  SamrenaVector *ohlcv = create_test_ohlcv(arena, closes, 10);
  ASSERT(ohlcv != NULL, "Failed to create OHLCV data");

  SamtraderIndicatorSeries *macd = samtrader_calculate_macd(arena, ohlcv, 3, 5, 3);
  ASSERT(macd != NULL, "Failed to calculate MACD");
  ASSERT(macd->type == SAMTRADER_IND_MACD, "Type should be MACD");
  ASSERT(macd->params.period == 3, "Fast period should be 3");
  ASSERT(macd->params.param2 == 5, "Slow period should be 5");
  ASSERT(macd->params.param3 == 3, "Signal period should be 3");
  ASSERT(samtrader_indicator_series_size(macd) == 10, "Should have 10 values");

  /* First 4 values (indices 0-3) should be invalid (max(3,5)-1 = 4 for MACD line,
   * then need signal_period more for signal. Invalid until macd_line_count >= signal_period.
   * MACD line valid from i=4 (max_period-1=4). macd_line_count reaches 3 at i=6. */
  for (size_t i = 0; i < 6; i++) {
    const SamtraderIndicatorValue *val = samtrader_indicator_series_at(macd, i);
    ASSERT(val != NULL && val->valid == false, "Warmup values should be invalid");
  }

  /* First valid value at index 6 */
  const SamtraderIndicatorValue *val = samtrader_indicator_series_at(macd, 6);
  ASSERT(val != NULL && val->valid == true, "Index 6 should be first valid value");

  /* With linearly rising data, MACD line should be positive (fast > slow) */
  ASSERT(val->data.macd.line > 0.0, "MACD line should be positive for rising prices");

  /* Histogram should equal line - signal */
  ASSERT_DOUBLE_EQ(val->data.macd.histogram, val->data.macd.line - val->data.macd.signal,
                   "Histogram should be line - signal");

  samrena_destroy(arena);
  printf("  PASS\n");
  return 0;
}

static int test_macd_known_values(void) {
  printf("Testing MACD with hand-calculated values...\n");

  Samrena *arena = samrena_create_default();
  ASSERT(arena != NULL, "Failed to create arena");

  /* fast=2, slow=3, signal=2
   * fast_k = 2/3, slow_k = 2/4 = 0.5, signal_k = 2/3
   * max_period = 3, MACD line valid from i=2 */
  double closes[] = {10.0, 12.0, 11.0, 14.0, 13.0};
  SamrenaVector *ohlcv = create_test_ohlcv(arena, closes, 5);

  SamtraderIndicatorSeries *macd = samtrader_calculate_macd(arena, ohlcv, 2, 3, 2);
  ASSERT(macd != NULL, "Failed to calculate MACD");
  ASSERT(samtrader_indicator_series_size(macd) == 5, "Should have 5 values");

  /* i=0,1: invalid (warmup for slow EMA) */
  const SamtraderIndicatorValue *val = samtrader_indicator_series_at(macd, 0);
  ASSERT(val != NULL && val->valid == false, "Index 0 should be invalid");
  val = samtrader_indicator_series_at(macd, 1);
  ASSERT(val != NULL && val->valid == false, "Index 1 should be invalid");

  /* i=2: fast_ema = (10+12)/2 = 11.0, slow_ema = (10+12+11)/3 = 11.0
   * macd_line = 11.0 - 11.0 = 0.0, macd_line_count=1
   * signal_sum = 0.0, not yet valid (need signal_period=2) */
  val = samtrader_indicator_series_at(macd, 2);
  ASSERT(val != NULL && val->valid == false, "Index 2 should be invalid (signal warmup)");
  ASSERT_DOUBLE_EQ(val->data.macd.line, 0.0, "MACD line at index 2");

  /* i=3: fast_ema = 14*(2/3) + 11.0*(1/3) = 13.0
   *       slow_ema = 14*0.5 + 11.0*0.5 = 12.5
   *       macd_line = 13.0 - 12.5 = 0.5, macd_line_count=2
   *       signal_sum = 0.0 + 0.5 = 0.5, signal_ema = 0.5/2 = 0.25
   *       histogram = 0.5 - 0.25 = 0.25 -> FIRST VALID */
  val = samtrader_indicator_series_at(macd, 3);
  ASSERT(val != NULL && val->valid == true, "Index 3 should be first valid");
  ASSERT_DOUBLE_EQ(val->data.macd.line, 0.5, "MACD line at index 3");
  ASSERT_DOUBLE_EQ(val->data.macd.signal, 0.25, "MACD signal at index 3");
  ASSERT_DOUBLE_EQ(val->data.macd.histogram, 0.25, "MACD histogram at index 3");

  /* i=4: fast_ema = 13*(2/3) + 13.0*(1/3) = 13.0
   *       slow_ema = 13*0.5 + 12.5*0.5 = 12.75
   *       macd_line = 13.0 - 12.75 = 0.25, macd_line_count=3
   *       signal_ema = 0.25*(2/3) + 0.25*(1/3) = 0.25
   *       histogram = 0.25 - 0.25 = 0.0 */
  val = samtrader_indicator_series_at(macd, 4);
  ASSERT(val != NULL && val->valid == true, "Index 4 should be valid");
  ASSERT_DOUBLE_EQ(val->data.macd.line, 0.25, "MACD line at index 4");
  ASSERT_DOUBLE_EQ(val->data.macd.signal, 0.25, "MACD signal at index 4");
  ASSERT_DOUBLE_EQ(val->data.macd.histogram, 0.0, "MACD histogram at index 4");

  samrena_destroy(arena);
  printf("  PASS\n");
  return 0;
}

static int test_macd_constant_prices(void) {
  printf("Testing MACD with constant prices...\n");

  Samrena *arena = samrena_create_default();
  ASSERT(arena != NULL, "Failed to create arena");

  /* Constant prices: both EMAs converge to same value, MACD = 0 */
  double closes[] = {50.0, 50.0, 50.0, 50.0, 50.0, 50.0, 50.0, 50.0, 50.0, 50.0};
  SamrenaVector *ohlcv = create_test_ohlcv(arena, closes, 10);

  SamtraderIndicatorSeries *macd = samtrader_calculate_macd(arena, ohlcv, 3, 5, 3);
  ASSERT(macd != NULL, "Failed to calculate MACD");

  /* All valid MACD values should be (0, 0, 0) */
  for (size_t i = 0; i < 10; i++) {
    const SamtraderIndicatorValue *val = samtrader_indicator_series_at(macd, i);
    ASSERT(val != NULL, "Value should exist");
    if (val->valid) {
      ASSERT_DOUBLE_EQ(val->data.macd.line, 0.0, "MACD line should be 0 for constant prices");
      ASSERT_DOUBLE_EQ(val->data.macd.signal, 0.0, "MACD signal should be 0 for constant prices");
      ASSERT_DOUBLE_EQ(val->data.macd.histogram, 0.0,
                       "MACD histogram should be 0 for constant prices");
    }
  }

  samrena_destroy(arena);
  printf("  PASS\n");
  return 0;
}

static int test_macd_histogram_identity(void) {
  printf("Testing MACD histogram = line - signal identity...\n");

  Samrena *arena = samrena_create_default();
  ASSERT(arena != NULL, "Failed to create arena");

  double closes[] = {10.0, 12.0, 11.0, 14.0, 13.0, 16.0, 15.0, 18.0,
                     17.0, 20.0, 19.0, 22.0, 21.0, 24.0, 23.0};
  SamrenaVector *ohlcv = create_test_ohlcv(arena, closes, 15);

  SamtraderIndicatorSeries *macd = samtrader_calculate_macd(arena, ohlcv, 3, 5, 3);
  ASSERT(macd != NULL, "Failed to calculate MACD");

  for (size_t i = 0; i < 15; i++) {
    const SamtraderIndicatorValue *val = samtrader_indicator_series_at(macd, i);
    ASSERT(val != NULL, "Value should exist");
    if (val->valid) {
      ASSERT_DOUBLE_EQ(val->data.macd.histogram, val->data.macd.line - val->data.macd.signal,
                       "Histogram should be line - signal");
    }
  }

  samrena_destroy(arena);
  printf("  PASS\n");
  return 0;
}

static int test_macd_invalid_params(void) {
  printf("Testing MACD with invalid parameters...\n");

  Samrena *arena = samrena_create_default();
  ASSERT(arena != NULL, "Failed to create arena");

  double closes[] = {1.0, 2.0, 3.0};
  SamrenaVector *ohlcv = create_test_ohlcv(arena, closes, 3);

  ASSERT(samtrader_calculate_macd(NULL, ohlcv, 12, 26, 9) == NULL, "NULL arena should fail");
  ASSERT(samtrader_calculate_macd(arena, NULL, 12, 26, 9) == NULL, "NULL ohlcv should fail");
  ASSERT(samtrader_calculate_macd(arena, ohlcv, 0, 26, 9) == NULL, "Fast period 0 should fail");
  ASSERT(samtrader_calculate_macd(arena, ohlcv, 12, 0, 9) == NULL, "Slow period 0 should fail");
  ASSERT(samtrader_calculate_macd(arena, ohlcv, 12, 26, 0) == NULL, "Signal period 0 should fail");
  ASSERT(samtrader_calculate_macd(arena, ohlcv, -1, 26, 9) == NULL,
         "Negative fast period should fail");

  SamrenaVector *empty = samtrader_ohlcv_vector_create(arena, 10);
  ASSERT(samtrader_calculate_macd(arena, empty, 12, 26, 9) == NULL, "Empty vector should fail");

  samrena_destroy(arena);
  printf("  PASS\n");
  return 0;
}

static int test_macd_latest_value(void) {
  printf("Testing MACD latest value retrieval...\n");

  Samrena *arena = samrena_create_default();
  ASSERT(arena != NULL, "Failed to create arena");

  double closes[] = {50.0, 50.0, 50.0, 50.0, 50.0, 50.0, 50.0, 50.0, 50.0, 50.0};
  SamrenaVector *ohlcv = create_test_ohlcv(arena, closes, 10);

  SamtraderIndicatorSeries *macd = samtrader_calculate_macd(arena, ohlcv, 3, 5, 3);
  ASSERT(macd != NULL, "Failed to calculate MACD");

  SamtraderMacdValue latest;
  ASSERT(samtrader_indicator_latest_macd(macd, &latest) == true, "Should find latest valid value");
  ASSERT_DOUBLE_EQ(latest.line, 0.0, "Latest MACD line should be 0");
  ASSERT_DOUBLE_EQ(latest.signal, 0.0, "Latest MACD signal should be 0");
  ASSERT_DOUBLE_EQ(latest.histogram, 0.0, "Latest MACD histogram should be 0");

  samrena_destroy(arena);
  printf("  PASS\n");
  return 0;
}

/*============================================================================
 * Stochastic Oscillator Tests
 *============================================================================*/

static int test_stochastic_basic(void) {
  printf("Testing Stochastic basic calculation...\n");

  Samrena *arena = samrena_create_default();
  ASSERT(arena != NULL, "Failed to create arena");

  /* k_period=3, d_period=2
   * create_test_ohlcv: H=close+1, L=close-1 */
  double closes[] = {10.0, 12.0, 11.0, 14.0, 13.0, 16.0};
  SamrenaVector *ohlcv = create_test_ohlcv(arena, closes, 6);
  ASSERT(ohlcv != NULL, "Failed to create OHLCV data");

  SamtraderIndicatorSeries *stoch = samtrader_calculate_stochastic(arena, ohlcv, 3, 2);
  ASSERT(stoch != NULL, "Failed to calculate Stochastic");
  ASSERT(stoch->type == SAMTRADER_IND_STOCHASTIC, "Type should be STOCHASTIC");
  ASSERT(stoch->params.period == 3, "K period should be 3");
  ASSERT(stoch->params.param2 == 2, "D period should be 2");
  ASSERT(samtrader_indicator_series_size(stoch) == 6, "Should have 6 values");

  /* First 2 values should be invalid (%K warmup: k_period-1 = 2) */
  const SamtraderIndicatorValue *val = samtrader_indicator_series_at(stoch, 0);
  ASSERT(val != NULL && val->valid == false, "Index 0 should be invalid");
  val = samtrader_indicator_series_at(stoch, 1);
  ASSERT(val != NULL && val->valid == false, "Index 1 should be invalid");

  /* i=2: window [0,1,2], HH=max(11,13,12)=13, LL=min(9,11,10)=9
   * %K = 100*(11-9)/(13-9) = 50.0
   * k_count=1, d_valid=false */
  val = samtrader_indicator_series_at(stoch, 2);
  ASSERT(val != NULL && val->valid == false, "Index 2 should be invalid (%D warmup)");
  ASSERT_DOUBLE_EQ(val->data.stochastic.k, 50.0, "%%K at index 2");

  /* i=3: window [1,2,3], HH=max(13,12,15)=15, LL=min(11,10,13)=10
   * %K = 100*(14-10)/(15-10) = 80.0
   * k_count=2, d_valid=true, %D = (50+80)/2 = 65.0 */
  val = samtrader_indicator_series_at(stoch, 3);
  ASSERT(val != NULL && val->valid == true, "Index 3 should be first valid");
  ASSERT_DOUBLE_EQ(val->data.stochastic.k, 80.0, "%%K at index 3");
  ASSERT_DOUBLE_EQ(val->data.stochastic.d, 65.0, "%%D at index 3");

  /* i=4: window [2,3,4], HH=max(12,15,14)=15, LL=min(10,13,12)=10
   * %K = 100*(13-10)/(15-10) = 60.0
   * %D = (80+60)/2 = 70.0 */
  val = samtrader_indicator_series_at(stoch, 4);
  ASSERT(val != NULL && val->valid == true, "Index 4 should be valid");
  ASSERT_DOUBLE_EQ(val->data.stochastic.k, 60.0, "%%K at index 4");
  ASSERT_DOUBLE_EQ(val->data.stochastic.d, 70.0, "%%D at index 4");

  samrena_destroy(arena);
  printf("  PASS\n");
  return 0;
}

static int test_stochastic_warmup_period(void) {
  printf("Testing Stochastic warmup period...\n");

  Samrena *arena = samrena_create_default();
  ASSERT(arena != NULL, "Failed to create arena");

  double closes[] = {10.0, 12.0, 11.0, 14.0, 13.0, 16.0, 15.0, 18.0, 17.0, 20.0};
  SamrenaVector *ohlcv = create_test_ohlcv(arena, closes, 10);

  /* k_period=5, d_period=3: %K valid from i=4, %D valid from i=6 */
  SamtraderIndicatorSeries *stoch = samtrader_calculate_stochastic(arena, ohlcv, 5, 3);
  ASSERT(stoch != NULL, "Failed to calculate Stochastic");

  /* Indices 0-3: completely invalid (%K warmup) */
  for (size_t i = 0; i < 4; i++) {
    const SamtraderIndicatorValue *val = samtrader_indicator_series_at(stoch, i);
    ASSERT(val != NULL && val->valid == false, "Should be invalid during %K warmup");
  }

  /* Indices 4-5: %K valid but %D still warming up */
  for (size_t i = 4; i < 6; i++) {
    const SamtraderIndicatorValue *val = samtrader_indicator_series_at(stoch, i);
    ASSERT(val != NULL && val->valid == false, "Should be invalid during %D warmup");
  }

  /* Index 6 onwards: fully valid */
  for (size_t i = 6; i < 10; i++) {
    const SamtraderIndicatorValue *val = samtrader_indicator_series_at(stoch, i);
    ASSERT(val != NULL && val->valid == true, "Should be valid after full warmup");
  }

  samrena_destroy(arena);
  printf("  PASS\n");
  return 0;
}

static int test_stochastic_range(void) {
  printf("Testing Stochastic %%K and %%D are in [0, 100]...\n");

  Samrena *arena = samrena_create_default();
  ASSERT(arena != NULL, "Failed to create arena");

  double closes[] = {100.0, 95.0, 98.0, 90.0, 93.0, 88.0, 92.0, 85.0,
                     89.0,  87.0, 91.0, 86.0, 94.0, 83.0, 96.0};
  SamrenaVector *ohlcv = create_test_ohlcv(arena, closes, 15);

  SamtraderIndicatorSeries *stoch = samtrader_calculate_stochastic(arena, ohlcv, 5, 3);
  ASSERT(stoch != NULL, "Failed to calculate Stochastic");

  for (size_t i = 0; i < 15; i++) {
    const SamtraderIndicatorValue *val = samtrader_indicator_series_at(stoch, i);
    ASSERT(val != NULL, "Value should exist");
    if (val->valid) {
      ASSERT(val->data.stochastic.k >= 0.0 && val->data.stochastic.k <= 100.0,
             "%%K should be in [0, 100]");
      ASSERT(val->data.stochastic.d >= 0.0 && val->data.stochastic.d <= 100.0,
             "%%D should be in [0, 100]");
    }
  }

  samrena_destroy(arena);
  printf("  PASS\n");
  return 0;
}

static int test_stochastic_constant_prices(void) {
  printf("Testing Stochastic with constant prices...\n");

  Samrena *arena = samrena_create_default();
  ASSERT(arena != NULL, "Failed to create arena");

  /* Constant prices: H-L range is always 2.0 but highest_high - lowest_low
   * with same close values means the close is always at midpoint.
   * Actually, with constant close=50: H=51, L=49 for every bar.
   * So highest_high=51, lowest_low=49, range=2.
   * %K = 100*(50-49)/(51-49) = 100*1/2 = 50.0 */
  double closes[] = {50.0, 50.0, 50.0, 50.0, 50.0, 50.0, 50.0, 50.0};
  SamrenaVector *ohlcv = create_test_ohlcv(arena, closes, 8);

  SamtraderIndicatorSeries *stoch = samtrader_calculate_stochastic(arena, ohlcv, 3, 2);
  ASSERT(stoch != NULL, "Failed to calculate Stochastic");

  for (size_t i = 0; i < 8; i++) {
    const SamtraderIndicatorValue *val = samtrader_indicator_series_at(stoch, i);
    ASSERT(val != NULL, "Value should exist");
    if (val->valid) {
      ASSERT_DOUBLE_EQ(val->data.stochastic.k, 50.0, "%%K should be 50 for constant prices");
      ASSERT_DOUBLE_EQ(val->data.stochastic.d, 50.0, "%%D should be 50 for constant prices");
    }
  }

  samrena_destroy(arena);
  printf("  PASS\n");
  return 0;
}

static int test_stochastic_extremes(void) {
  printf("Testing Stochastic at extreme values...\n");

  Samrena *arena = samrena_create_default();
  ASSERT(arena != NULL, "Failed to create arena");

  /* Monotonically rising: close is always at or near the high of the window
   * With H=close+1, L=close-1:
   * i=2: window [0,1,2], HH=13, LL=9, close=12
   *       %K = 100*(12-9)/(13-9) = 75.0
   * As prices keep rising, %K trends toward high values */
  double rising[] = {10.0, 11.0, 12.0, 13.0, 14.0, 15.0};
  SamrenaVector *ohlcv = create_test_ohlcv(arena, rising, 6);

  SamtraderIndicatorSeries *stoch = samtrader_calculate_stochastic(arena, ohlcv, 3, 2);
  ASSERT(stoch != NULL, "Failed to calculate Stochastic");

  /* At index 2: HH=13, LL=9, C=12, %K = 100*3/4 = 75 */
  const SamtraderIndicatorValue *val = samtrader_indicator_series_at(stoch, 2);
  ASSERT(val != NULL, "Value should exist");
  ASSERT_DOUBLE_EQ(val->data.stochastic.k, 75.0, "%%K at index 2 for rising prices");

  /* All valid %K values should be >= 50 for rising prices */
  for (size_t i = 2; i < 6; i++) {
    val = samtrader_indicator_series_at(stoch, i);
    ASSERT(val != NULL, "Value should exist");
    ASSERT(val->data.stochastic.k >= 50.0, "%%K should be >= 50 for rising prices");
  }

  samrena_destroy(arena);
  printf("  PASS\n");
  return 0;
}

static int test_stochastic_invalid_params(void) {
  printf("Testing Stochastic with invalid parameters...\n");

  Samrena *arena = samrena_create_default();
  ASSERT(arena != NULL, "Failed to create arena");

  double closes[] = {1.0, 2.0, 3.0};
  SamrenaVector *ohlcv = create_test_ohlcv(arena, closes, 3);

  ASSERT(samtrader_calculate_stochastic(NULL, ohlcv, 14, 3) == NULL, "NULL arena should fail");
  ASSERT(samtrader_calculate_stochastic(arena, NULL, 14, 3) == NULL, "NULL ohlcv should fail");
  ASSERT(samtrader_calculate_stochastic(arena, ohlcv, 0, 3) == NULL, "K period 0 should fail");
  ASSERT(samtrader_calculate_stochastic(arena, ohlcv, 14, 0) == NULL, "D period 0 should fail");
  ASSERT(samtrader_calculate_stochastic(arena, ohlcv, -1, 3) == NULL,
         "Negative K period should fail");
  ASSERT(samtrader_calculate_stochastic(arena, ohlcv, 14, -1) == NULL,
         "Negative D period should fail");

  SamrenaVector *empty = samtrader_ohlcv_vector_create(arena, 10);
  ASSERT(samtrader_calculate_stochastic(arena, empty, 14, 3) == NULL, "Empty vector should fail");

  samrena_destroy(arena);
  printf("  PASS\n");
  return 0;
}

static int test_stochastic_latest_value(void) {
  printf("Testing Stochastic latest value retrieval...\n");

  Samrena *arena = samrena_create_default();
  ASSERT(arena != NULL, "Failed to create arena");

  double closes[] = {50.0, 50.0, 50.0, 50.0, 50.0};
  SamrenaVector *ohlcv = create_test_ohlcv(arena, closes, 5);

  SamtraderIndicatorSeries *stoch = samtrader_calculate_stochastic(arena, ohlcv, 3, 2);
  ASSERT(stoch != NULL, "Failed to calculate Stochastic");

  SamtraderStochasticValue latest;
  ASSERT(samtrader_indicator_latest_stochastic(stoch, &latest) == true,
         "Should find latest valid value");
  ASSERT_DOUBLE_EQ(latest.k, 50.0, "Latest %K should be 50");
  ASSERT_DOUBLE_EQ(latest.d, 50.0, "Latest %D should be 50");

  samrena_destroy(arena);
  printf("  PASS\n");
  return 0;
}

/*============================================================================
 * Bollinger Bands Tests
 *============================================================================*/

static int test_bollinger_basic(void) {
  printf("Testing Bollinger Bands basic calculation...\n");

  Samrena *arena = samrena_create_default();
  ASSERT(arena != NULL, "Failed to create arena");

  /* Data: 1, 2, 3, 4, 5 with period 3, stddev multiplier 2.0 */
  double closes[] = {1.0, 2.0, 3.0, 4.0, 5.0};
  SamrenaVector *ohlcv = create_test_ohlcv(arena, closes, 5);
  ASSERT(ohlcv != NULL, "Failed to create OHLCV data");

  SamtraderIndicatorSeries *bb = samtrader_calculate_bollinger(arena, ohlcv, 3, 2.0);
  ASSERT(bb != NULL, "Failed to calculate Bollinger Bands");
  ASSERT(bb->type == SAMTRADER_IND_BOLLINGER, "Type should be BOLLINGER");
  ASSERT(bb->params.period == 3, "Period should be 3");
  ASSERT(samtrader_indicator_series_size(bb) == 5, "Should have 5 values");

  /* First two values should be invalid (warmup) */
  const SamtraderIndicatorValue *val = samtrader_indicator_series_at(bb, 0);
  ASSERT(val != NULL && val->valid == false, "Index 0 should be invalid");

  val = samtrader_indicator_series_at(bb, 1);
  ASSERT(val != NULL && val->valid == false, "Index 1 should be invalid");

  /* Index 2: SMA = (1+2+3)/3 = 2.0
   * StdDev = sqrt(((1-2)^2 + (2-2)^2 + (3-2)^2) / 3) = sqrt(2/3) = 0.8165
   * Upper = 2.0 + 2.0 * 0.8165 = 3.6330
   * Lower = 2.0 - 2.0 * 0.8165 = 0.3670 */
  val = samtrader_indicator_series_at(bb, 2);
  ASSERT(val != NULL && val->valid == true, "Index 2 should be valid");
  ASSERT_DOUBLE_EQ(val->data.bollinger.middle, 2.0, "Middle at index 2");
  double stddev_2 = sqrt(2.0 / 3.0);
  ASSERT_DOUBLE_EQ(val->data.bollinger.upper, 2.0 + 2.0 * stddev_2, "Upper at index 2");
  ASSERT_DOUBLE_EQ(val->data.bollinger.lower, 2.0 - 2.0 * stddev_2, "Lower at index 2");

  /* Index 3: SMA = (2+3+4)/3 = 3.0
   * StdDev = sqrt(((2-3)^2 + (3-3)^2 + (4-3)^2) / 3) = sqrt(2/3) */
  val = samtrader_indicator_series_at(bb, 3);
  ASSERT(val != NULL && val->valid == true, "Index 3 should be valid");
  ASSERT_DOUBLE_EQ(val->data.bollinger.middle, 3.0, "Middle at index 3");
  ASSERT_DOUBLE_EQ(val->data.bollinger.upper, 3.0 + 2.0 * stddev_2, "Upper at index 3");
  ASSERT_DOUBLE_EQ(val->data.bollinger.lower, 3.0 - 2.0 * stddev_2, "Lower at index 3");

  /* Index 4: SMA = (3+4+5)/3 = 4.0
   * StdDev = sqrt(((3-4)^2 + (4-4)^2 + (5-4)^2) / 3) = sqrt(2/3) */
  val = samtrader_indicator_series_at(bb, 4);
  ASSERT(val != NULL && val->valid == true, "Index 4 should be valid");
  ASSERT_DOUBLE_EQ(val->data.bollinger.middle, 4.0, "Middle at index 4");
  ASSERT_DOUBLE_EQ(val->data.bollinger.upper, 4.0 + 2.0 * stddev_2, "Upper at index 4");
  ASSERT_DOUBLE_EQ(val->data.bollinger.lower, 4.0 - 2.0 * stddev_2, "Lower at index 4");

  samrena_destroy(arena);
  printf("  PASS\n");
  return 0;
}

static int test_bollinger_constant_prices(void) {
  printf("Testing Bollinger Bands with constant prices...\n");

  Samrena *arena = samrena_create_default();
  ASSERT(arena != NULL, "Failed to create arena");

  /* Constant prices -> stddev = 0, bands collapse to SMA */
  double closes[] = {50.0, 50.0, 50.0, 50.0, 50.0, 50.0};
  SamrenaVector *ohlcv = create_test_ohlcv(arena, closes, 6);

  SamtraderIndicatorSeries *bb = samtrader_calculate_bollinger(arena, ohlcv, 3, 2.0);
  ASSERT(bb != NULL, "Failed to calculate Bollinger Bands");

  for (size_t i = 2; i < 6; i++) {
    const SamtraderIndicatorValue *val = samtrader_indicator_series_at(bb, i);
    ASSERT(val != NULL && val->valid == true, "Should be valid");
    ASSERT_DOUBLE_EQ(val->data.bollinger.middle, 50.0, "Middle should be 50");
    ASSERT_DOUBLE_EQ(val->data.bollinger.upper, 50.0, "Upper should equal middle");
    ASSERT_DOUBLE_EQ(val->data.bollinger.lower, 50.0, "Lower should equal middle");
  }

  samrena_destroy(arena);
  printf("  PASS\n");
  return 0;
}

static int test_bollinger_band_symmetry(void) {
  printf("Testing Bollinger Bands symmetry around middle...\n");

  Samrena *arena = samrena_create_default();
  ASSERT(arena != NULL, "Failed to create arena");

  double closes[] = {10.0, 12.0, 11.0, 13.0, 12.0, 14.0, 11.0, 15.0};
  SamrenaVector *ohlcv = create_test_ohlcv(arena, closes, 8);

  SamtraderIndicatorSeries *bb = samtrader_calculate_bollinger(arena, ohlcv, 5, 2.0);
  ASSERT(bb != NULL, "Failed to calculate Bollinger Bands");

  /* Upper and lower should be equidistant from middle */
  for (size_t i = 4; i < 8; i++) {
    const SamtraderIndicatorValue *val = samtrader_indicator_series_at(bb, i);
    ASSERT(val != NULL && val->valid == true, "Should be valid");

    double upper_dist = val->data.bollinger.upper - val->data.bollinger.middle;
    double lower_dist = val->data.bollinger.middle - val->data.bollinger.lower;
    ASSERT_DOUBLE_EQ(upper_dist, lower_dist, "Bands should be symmetric");
    ASSERT(val->data.bollinger.upper >= val->data.bollinger.middle, "Upper should be >= middle");
    ASSERT(val->data.bollinger.lower <= val->data.bollinger.middle, "Lower should be <= middle");
  }

  samrena_destroy(arena);
  printf("  PASS\n");
  return 0;
}

static int test_bollinger_stddev_multiplier(void) {
  printf("Testing Bollinger Bands with different stddev multipliers...\n");

  Samrena *arena = samrena_create_default();
  ASSERT(arena != NULL, "Failed to create arena");

  double closes[] = {10.0, 12.0, 11.0, 13.0, 12.0};
  SamrenaVector *ohlcv = create_test_ohlcv(arena, closes, 5);

  SamtraderIndicatorSeries *bb1 = samtrader_calculate_bollinger(arena, ohlcv, 3, 1.0);
  SamtraderIndicatorSeries *bb2 = samtrader_calculate_bollinger(arena, ohlcv, 3, 2.0);
  SamtraderIndicatorSeries *bb3 = samtrader_calculate_bollinger(arena, ohlcv, 3, 3.0);
  ASSERT(bb1 && bb2 && bb3, "All calculations should succeed");

  /* Wider multiplier = wider bands, same middle */
  for (size_t i = 2; i < 5; i++) {
    const SamtraderIndicatorValue *v1 = samtrader_indicator_series_at(bb1, i);
    const SamtraderIndicatorValue *v2 = samtrader_indicator_series_at(bb2, i);
    const SamtraderIndicatorValue *v3 = samtrader_indicator_series_at(bb3, i);

    /* Middle should be the same for all multipliers */
    ASSERT_DOUBLE_EQ(v1->data.bollinger.middle, v2->data.bollinger.middle,
                     "Middle should be same regardless of multiplier");
    ASSERT_DOUBLE_EQ(v2->data.bollinger.middle, v3->data.bollinger.middle,
                     "Middle should be same regardless of multiplier");

    /* Wider multiplier = wider bands */
    double width1 = v1->data.bollinger.upper - v1->data.bollinger.lower;
    double width2 = v2->data.bollinger.upper - v2->data.bollinger.lower;
    double width3 = v3->data.bollinger.upper - v3->data.bollinger.lower;
    ASSERT(width2 > width1, "2x multiplier should be wider than 1x");
    ASSERT(width3 > width2, "3x multiplier should be wider than 2x");
  }

  samrena_destroy(arena);
  printf("  PASS\n");
  return 0;
}

static int test_bollinger_invalid_params(void) {
  printf("Testing Bollinger Bands with invalid parameters...\n");

  Samrena *arena = samrena_create_default();
  ASSERT(arena != NULL, "Failed to create arena");

  double closes[] = {1.0, 2.0, 3.0};
  SamrenaVector *ohlcv = create_test_ohlcv(arena, closes, 3);

  ASSERT(samtrader_calculate_bollinger(NULL, ohlcv, 20, 2.0) == NULL, "NULL arena should fail");
  ASSERT(samtrader_calculate_bollinger(arena, NULL, 20, 2.0) == NULL, "NULL ohlcv should fail");
  ASSERT(samtrader_calculate_bollinger(arena, ohlcv, 0, 2.0) == NULL, "Period 0 should fail");
  ASSERT(samtrader_calculate_bollinger(arena, ohlcv, -1, 2.0) == NULL,
         "Negative period should fail");

  SamrenaVector *empty = samtrader_ohlcv_vector_create(arena, 10);
  ASSERT(samtrader_calculate_bollinger(arena, empty, 20, 2.0) == NULL, "Empty vector should fail");

  samrena_destroy(arena);
  printf("  PASS\n");
  return 0;
}

static int test_bollinger_latest_value(void) {
  printf("Testing Bollinger Bands latest value retrieval...\n");

  Samrena *arena = samrena_create_default();
  ASSERT(arena != NULL, "Failed to create arena");

  double closes[] = {1.0, 2.0, 3.0, 4.0, 5.0};
  SamrenaVector *ohlcv = create_test_ohlcv(arena, closes, 5);

  SamtraderIndicatorSeries *bb = samtrader_calculate_bollinger(arena, ohlcv, 3, 2.0);
  ASSERT(bb != NULL, "Failed to calculate Bollinger Bands");

  SamtraderBollingerValue latest;
  ASSERT(samtrader_indicator_latest_bollinger(bb, &latest) == true,
         "Should find latest valid value");
  ASSERT_DOUBLE_EQ(latest.middle, 4.0, "Latest middle should be SMA of last 3");

  samrena_destroy(arena);
  printf("  PASS\n");
  return 0;
}

/*============================================================================
 * ATR Tests
 *============================================================================*/

static int test_atr_basic(void) {
  printf("Testing ATR basic calculation...\n");

  Samrena *arena = samrena_create_default();
  ASSERT(arena != NULL, "Failed to create arena");

  /* Data with known close prices; create_test_ohlcv sets H=close+1, L=close-1
   * Bar 0: H=11, L=9,  C=10, TR = H-L = 2.0
   * Bar 1: H=13, L=11, C=12, prev_close=10, TR = max(2, 3, 1) = 3.0
   * Bar 2: H=12, L=10, C=11, prev_close=12, TR = max(2, 0, 2) = 2.0
   * Bar 3: H=14, L=12, C=13, prev_close=11, TR = max(2, 3, 1) = 3.0
   * Bar 4: H=13, L=11, C=12, prev_close=13, TR = max(2, 0, 2) = 2.0
   */
  double closes[] = {10.0, 12.0, 11.0, 13.0, 12.0};
  SamrenaVector *ohlcv = create_test_ohlcv(arena, closes, 5);
  ASSERT(ohlcv != NULL, "Failed to create OHLCV data");

  SamtraderIndicatorSeries *atr = samtrader_calculate_atr(arena, ohlcv, 3);
  ASSERT(atr != NULL, "Failed to calculate ATR");
  ASSERT(atr->type == SAMTRADER_IND_ATR, "Type should be ATR");
  ASSERT(atr->params.period == 3, "Period should be 3");
  ASSERT(samtrader_indicator_series_size(atr) == 5, "Should have 5 values");

  /* First two values should be invalid (warmup for period 3) */
  const SamtraderIndicatorValue *val = samtrader_indicator_series_at(atr, 0);
  ASSERT(val != NULL && val->valid == false, "Index 0 should be invalid");

  val = samtrader_indicator_series_at(atr, 1);
  ASSERT(val != NULL && val->valid == false, "Index 1 should be invalid");

  /* ATR at index 2: simple avg of first 3 TRs = (2.0 + 3.0 + 2.0) / 3 */
  val = samtrader_indicator_series_at(atr, 2);
  ASSERT(val != NULL && val->valid == true, "Index 2 should be valid");
  ASSERT_DOUBLE_EQ(val->data.simple.value, 7.0 / 3.0, "ATR at index 2");

  /* ATR at index 3: Wilder's = (prev_ATR * 2 + TR) / 3 = (7/3 * 2 + 3.0) / 3 */
  val = samtrader_indicator_series_at(atr, 3);
  ASSERT(val != NULL && val->valid == true, "Index 3 should be valid");
  double expected_atr3 = ((7.0 / 3.0) * 2.0 + 3.0) / 3.0;
  ASSERT_DOUBLE_EQ(val->data.simple.value, expected_atr3, "ATR at index 3");

  /* ATR at index 4: Wilder's = (prev_ATR * 2 + TR) / 3 */
  val = samtrader_indicator_series_at(atr, 4);
  ASSERT(val != NULL && val->valid == true, "Index 4 should be valid");
  double expected_atr4 = (expected_atr3 * 2.0 + 2.0) / 3.0;
  ASSERT_DOUBLE_EQ(val->data.simple.value, expected_atr4, "ATR at index 4");

  samrena_destroy(arena);
  printf("  PASS\n");
  return 0;
}

static int test_atr_constant_prices(void) {
  printf("Testing ATR with constant prices...\n");

  Samrena *arena = samrena_create_default();
  ASSERT(arena != NULL, "Failed to create arena");

  /* Constant prices: H=close+1, L=close-1, so H-L = 2.0 always.
   * With constant close, |H-prev_close| = 1.0 and |L-prev_close| = 1.0
   * TR = max(2.0, 1.0, 1.0) = 2.0 for all bars */
  double closes[] = {50.0, 50.0, 50.0, 50.0, 50.0, 50.0, 50.0, 50.0};
  SamrenaVector *ohlcv = create_test_ohlcv(arena, closes, 8);

  SamtraderIndicatorSeries *atr = samtrader_calculate_atr(arena, ohlcv, 3);
  ASSERT(atr != NULL, "Failed to calculate ATR");

  /* All valid ATR values should be 2.0 */
  for (size_t i = 2; i < 8; i++) {
    const SamtraderIndicatorValue *val = samtrader_indicator_series_at(atr, i);
    ASSERT(val != NULL && val->valid == true, "Should be valid");
    ASSERT_DOUBLE_EQ(val->data.simple.value, 2.0, "ATR should be 2.0 for constant prices");
  }

  samrena_destroy(arena);
  printf("  PASS\n");
  return 0;
}

static int test_atr_period_1(void) {
  printf("Testing ATR with period 1...\n");

  Samrena *arena = samrena_create_default();
  ASSERT(arena != NULL, "Failed to create arena");

  double closes[] = {10.0, 12.0, 11.0};
  SamrenaVector *ohlcv = create_test_ohlcv(arena, closes, 3);

  SamtraderIndicatorSeries *atr = samtrader_calculate_atr(arena, ohlcv, 1);
  ASSERT(atr != NULL, "Failed to calculate ATR");

  /* Period 1: every value should be valid and equal to that bar's TR */
  /* Bar 0: TR = H-L = 2.0 */
  const SamtraderIndicatorValue *val = samtrader_indicator_series_at(atr, 0);
  ASSERT(val != NULL && val->valid == true, "Index 0 should be valid with period 1");
  ASSERT_DOUBLE_EQ(val->data.simple.value, 2.0, "ATR at index 0");

  /* Bar 1: H=13, L=11, prev_close=10, TR = max(2, 3, 1) = 3.0 */
  val = samtrader_indicator_series_at(atr, 1);
  ASSERT(val != NULL && val->valid == true, "Index 1 should be valid");
  ASSERT_DOUBLE_EQ(val->data.simple.value, 3.0, "ATR at index 1");

  /* Bar 2: H=12, L=10, prev_close=12, TR = max(2, 0, 2) = 2.0 */
  val = samtrader_indicator_series_at(atr, 2);
  ASSERT(val != NULL && val->valid == true, "Index 2 should be valid");
  ASSERT_DOUBLE_EQ(val->data.simple.value, 2.0, "ATR at index 2");

  samrena_destroy(arena);
  printf("  PASS\n");
  return 0;
}

static int test_atr_always_positive(void) {
  printf("Testing ATR is always positive...\n");

  Samrena *arena = samrena_create_default();
  ASSERT(arena != NULL, "Failed to create arena");

  double closes[] = {100.0, 95.0, 98.0, 90.0, 93.0, 88.0, 92.0, 85.0, 89.0, 87.0};
  SamrenaVector *ohlcv = create_test_ohlcv(arena, closes, 10);

  SamtraderIndicatorSeries *atr = samtrader_calculate_atr(arena, ohlcv, 5);
  ASSERT(atr != NULL, "Failed to calculate ATR");

  for (size_t i = 4; i < 10; i++) {
    const SamtraderIndicatorValue *val = samtrader_indicator_series_at(atr, i);
    ASSERT(val != NULL && val->valid == true, "Should be valid");
    ASSERT(val->data.simple.value > 0.0, "ATR should always be positive");
  }

  samrena_destroy(arena);
  printf("  PASS\n");
  return 0;
}

static int test_atr_invalid_params(void) {
  printf("Testing ATR with invalid parameters...\n");

  Samrena *arena = samrena_create_default();
  ASSERT(arena != NULL, "Failed to create arena");

  double closes[] = {1.0, 2.0, 3.0};
  SamrenaVector *ohlcv = create_test_ohlcv(arena, closes, 3);

  ASSERT(samtrader_calculate_atr(NULL, ohlcv, 14) == NULL, "NULL arena should fail");
  ASSERT(samtrader_calculate_atr(arena, NULL, 14) == NULL, "NULL ohlcv should fail");
  ASSERT(samtrader_calculate_atr(arena, ohlcv, 0) == NULL, "Period 0 should fail");
  ASSERT(samtrader_calculate_atr(arena, ohlcv, -1) == NULL, "Negative period should fail");

  SamrenaVector *empty = samtrader_ohlcv_vector_create(arena, 10);
  ASSERT(samtrader_calculate_atr(arena, empty, 14) == NULL, "Empty vector should fail");

  samrena_destroy(arena);
  printf("  PASS\n");
  return 0;
}

static int test_atr_latest_value(void) {
  printf("Testing ATR latest value retrieval...\n");

  Samrena *arena = samrena_create_default();
  ASSERT(arena != NULL, "Failed to create arena");

  double closes[] = {50.0, 50.0, 50.0, 50.0, 50.0};
  SamrenaVector *ohlcv = create_test_ohlcv(arena, closes, 5);

  SamtraderIndicatorSeries *atr = samtrader_calculate_atr(arena, ohlcv, 3);
  ASSERT(atr != NULL, "Failed to calculate ATR");

  double latest;
  ASSERT(samtrader_indicator_latest_simple(atr, &latest) == true, "Should find latest valid value");
  ASSERT_DOUBLE_EQ(latest, 2.0, "Latest ATR should be 2.0");

  samrena_destroy(arena);
  printf("  PASS\n");
  return 0;
}

/*============================================================================
 * Pivot Points Tests
 *============================================================================*/

static int test_pivot_basic(void) {
  printf("Testing Pivot Points basic calculation...\n");

  Samrena *arena = samrena_create_default();
  ASSERT(arena != NULL, "Failed to create arena");

  /* create_test_ohlcv sets: O=close, H=close+1, L=close-1, C=close
   * Bar 0: H=11, L=9,  C=10
   * Bar 1: H=13, L=11, C=12
   * Bar 2: H=12, L=10, C=11
   */
  double closes[] = {10.0, 12.0, 11.0, 13.0, 12.0};
  SamrenaVector *ohlcv = create_test_ohlcv(arena, closes, 5);
  ASSERT(ohlcv != NULL, "Failed to create OHLCV data");

  SamtraderIndicatorSeries *pivot = samtrader_calculate_pivot(arena, ohlcv);
  ASSERT(pivot != NULL, "Failed to calculate Pivot Points");
  ASSERT(pivot->type == SAMTRADER_IND_PIVOT, "Type should be PIVOT");
  ASSERT(samtrader_indicator_series_size(pivot) == 5, "Should have 5 values");

  /* First value should be invalid (no previous bar) */
  const SamtraderIndicatorValue *val = samtrader_indicator_series_at(pivot, 0);
  ASSERT(val != NULL && val->valid == false, "Index 0 should be invalid");

  /* Index 1: from bar 0 (H=11, L=9, C=10)
   * pivot = (11+9+10)/3 = 10.0
   * r1 = 2*10 - 9 = 11.0
   * r2 = 10 + (11-9) = 12.0
   * r3 = 11 + 2*(10-9) = 13.0
   * s1 = 2*10 - 11 = 9.0
   * s2 = 10 - (11-9) = 8.0
   * s3 = 9 - 2*(11-10) = 7.0
   */
  val = samtrader_indicator_series_at(pivot, 1);
  ASSERT(val != NULL && val->valid == true, "Index 1 should be valid");
  ASSERT_DOUBLE_EQ(val->data.pivot.pivot, 10.0, "Pivot at index 1");
  ASSERT_DOUBLE_EQ(val->data.pivot.r1, 11.0, "R1 at index 1");
  ASSERT_DOUBLE_EQ(val->data.pivot.r2, 12.0, "R2 at index 1");
  ASSERT_DOUBLE_EQ(val->data.pivot.r3, 13.0, "R3 at index 1");
  ASSERT_DOUBLE_EQ(val->data.pivot.s1, 9.0, "S1 at index 1");
  ASSERT_DOUBLE_EQ(val->data.pivot.s2, 8.0, "S2 at index 1");
  ASSERT_DOUBLE_EQ(val->data.pivot.s3, 7.0, "S3 at index 1");

  /* Index 2: from bar 1 (H=13, L=11, C=12)
   * pivot = (13+11+12)/3 = 12.0
   * r1 = 2*12 - 11 = 13.0
   * r2 = 12 + (13-11) = 14.0
   * r3 = 13 + 2*(12-11) = 15.0
   * s1 = 2*12 - 13 = 11.0
   * s2 = 12 - (13-11) = 10.0
   * s3 = 11 - 2*(13-12) = 9.0
   */
  val = samtrader_indicator_series_at(pivot, 2);
  ASSERT(val != NULL && val->valid == true, "Index 2 should be valid");
  ASSERT_DOUBLE_EQ(val->data.pivot.pivot, 12.0, "Pivot at index 2");
  ASSERT_DOUBLE_EQ(val->data.pivot.r1, 13.0, "R1 at index 2");
  ASSERT_DOUBLE_EQ(val->data.pivot.r2, 14.0, "R2 at index 2");
  ASSERT_DOUBLE_EQ(val->data.pivot.r3, 15.0, "R3 at index 2");
  ASSERT_DOUBLE_EQ(val->data.pivot.s1, 11.0, "S1 at index 2");
  ASSERT_DOUBLE_EQ(val->data.pivot.s2, 10.0, "S2 at index 2");
  ASSERT_DOUBLE_EQ(val->data.pivot.s3, 9.0, "S3 at index 2");

  samrena_destroy(arena);
  printf("  PASS\n");
  return 0;
}

static int test_pivot_level_ordering(void) {
  printf("Testing Pivot Points level ordering (S3 < S2 < S1 < P < R1 < R2 < R3)...\n");

  Samrena *arena = samrena_create_default();
  ASSERT(arena != NULL, "Failed to create arena");

  double closes[] = {10.0, 12.0, 11.0, 15.0, 9.0, 13.0, 11.0, 14.0};
  SamrenaVector *ohlcv = create_test_ohlcv(arena, closes, 8);

  SamtraderIndicatorSeries *pivot = samtrader_calculate_pivot(arena, ohlcv);
  ASSERT(pivot != NULL, "Failed to calculate Pivot Points");

  /* For all valid values, S3 < S2 < S1 < Pivot < R1 < R2 < R3 */
  for (size_t i = 1; i < 8; i++) {
    const SamtraderIndicatorValue *val = samtrader_indicator_series_at(pivot, i);
    ASSERT(val != NULL && val->valid == true, "Should be valid");
    ASSERT(val->data.pivot.s3 < val->data.pivot.s2, "S3 < S2");
    ASSERT(val->data.pivot.s2 < val->data.pivot.s1, "S2 < S1");
    ASSERT(val->data.pivot.s1 < val->data.pivot.pivot, "S1 < Pivot");
    ASSERT(val->data.pivot.pivot < val->data.pivot.r1, "Pivot < R1");
    ASSERT(val->data.pivot.r1 < val->data.pivot.r2, "R1 < R2");
    ASSERT(val->data.pivot.r2 < val->data.pivot.r3, "R2 < R3");
  }

  samrena_destroy(arena);
  printf("  PASS\n");
  return 0;
}

static int test_pivot_constant_prices(void) {
  printf("Testing Pivot Points with constant prices...\n");

  Samrena *arena = samrena_create_default();
  ASSERT(arena != NULL, "Failed to create arena");

  /* Constant prices: H=51, L=49, C=50
   * pivot = (51+49+50)/3 = 50.0
   * r1 = 2*50 - 49 = 51.0
   * r2 = 50 + (51-49) = 52.0
   * r3 = 51 + 2*(50-49) = 53.0
   * s1 = 2*50 - 51 = 49.0
   * s2 = 50 - (51-49) = 48.0
   * s3 = 49 - 2*(51-50) = 47.0
   */
  double closes[] = {50.0, 50.0, 50.0, 50.0, 50.0};
  SamrenaVector *ohlcv = create_test_ohlcv(arena, closes, 5);

  SamtraderIndicatorSeries *pivot = samtrader_calculate_pivot(arena, ohlcv);
  ASSERT(pivot != NULL, "Failed to calculate Pivot Points");

  for (size_t i = 1; i < 5; i++) {
    const SamtraderIndicatorValue *val = samtrader_indicator_series_at(pivot, i);
    ASSERT(val != NULL && val->valid == true, "Should be valid");
    ASSERT_DOUBLE_EQ(val->data.pivot.pivot, 50.0, "Pivot should be 50");
    ASSERT_DOUBLE_EQ(val->data.pivot.r1, 51.0, "R1 should be 51");
    ASSERT_DOUBLE_EQ(val->data.pivot.r2, 52.0, "R2 should be 52");
    ASSERT_DOUBLE_EQ(val->data.pivot.r3, 53.0, "R3 should be 53");
    ASSERT_DOUBLE_EQ(val->data.pivot.s1, 49.0, "S1 should be 49");
    ASSERT_DOUBLE_EQ(val->data.pivot.s2, 48.0, "S2 should be 48");
    ASSERT_DOUBLE_EQ(val->data.pivot.s3, 47.0, "S3 should be 47");
  }

  samrena_destroy(arena);
  printf("  PASS\n");
  return 0;
}

static int test_pivot_invalid_params(void) {
  printf("Testing Pivot Points with invalid parameters...\n");

  Samrena *arena = samrena_create_default();
  ASSERT(arena != NULL, "Failed to create arena");

  double closes[] = {1.0, 2.0, 3.0};
  SamrenaVector *ohlcv = create_test_ohlcv(arena, closes, 3);

  ASSERT(samtrader_calculate_pivot(NULL, ohlcv) == NULL, "NULL arena should fail");
  ASSERT(samtrader_calculate_pivot(arena, NULL) == NULL, "NULL ohlcv should fail");

  SamrenaVector *empty = samtrader_ohlcv_vector_create(arena, 10);
  ASSERT(samtrader_calculate_pivot(arena, empty) == NULL, "Empty vector should fail");

  samrena_destroy(arena);
  printf("  PASS\n");
  return 0;
}

static int test_pivot_latest_value(void) {
  printf("Testing Pivot Points latest value retrieval...\n");

  Samrena *arena = samrena_create_default();
  ASSERT(arena != NULL, "Failed to create arena");

  double closes[] = {50.0, 50.0, 50.0};
  SamrenaVector *ohlcv = create_test_ohlcv(arena, closes, 3);

  SamtraderIndicatorSeries *pivot = samtrader_calculate_pivot(arena, ohlcv);
  ASSERT(pivot != NULL, "Failed to calculate Pivot Points");

  SamtraderPivotValue latest;
  ASSERT(samtrader_indicator_latest_pivot(pivot, &latest) == true,
         "Should find latest valid value");
  ASSERT_DOUBLE_EQ(latest.pivot, 50.0, "Latest pivot should be 50");
  ASSERT_DOUBLE_EQ(latest.r1, 51.0, "Latest R1 should be 51");
  ASSERT_DOUBLE_EQ(latest.s1, 49.0, "Latest S1 should be 49");

  samrena_destroy(arena);
  printf("  PASS\n");
  return 0;
}

/*============================================================================
 * Dispatcher Tests
 *============================================================================*/

static int test_indicator_calculate_dispatcher(void) {
  printf("Testing samtrader_indicator_calculate dispatcher...\n");

  Samrena *arena = samrena_create_default();
  ASSERT(arena != NULL, "Failed to create arena");

  double closes[] = {1.0, 2.0, 3.0, 4.0, 5.0};
  SamrenaVector *ohlcv = create_test_ohlcv(arena, closes, 5);

  /* Test SMA dispatch */
  SamtraderIndicatorSeries *sma = samtrader_indicator_calculate(arena, SAMTRADER_IND_SMA, ohlcv, 3);
  ASSERT(sma != NULL, "SMA dispatch should work");
  ASSERT(sma->type == SAMTRADER_IND_SMA, "Should be SMA type");

  /* Test EMA dispatch */
  SamtraderIndicatorSeries *ema = samtrader_indicator_calculate(arena, SAMTRADER_IND_EMA, ohlcv, 3);
  ASSERT(ema != NULL, "EMA dispatch should work");
  ASSERT(ema->type == SAMTRADER_IND_EMA, "Should be EMA type");

  /* Test WMA dispatch */
  SamtraderIndicatorSeries *wma = samtrader_indicator_calculate(arena, SAMTRADER_IND_WMA, ohlcv, 3);
  ASSERT(wma != NULL, "WMA dispatch should work");
  ASSERT(wma->type == SAMTRADER_IND_WMA, "Should be WMA type");

  /* Test RSI dispatch */
  SamtraderIndicatorSeries *rsi = samtrader_indicator_calculate(arena, SAMTRADER_IND_RSI, ohlcv, 3);
  ASSERT(rsi != NULL, "RSI dispatch should work");
  ASSERT(rsi->type == SAMTRADER_IND_RSI, "Should be RSI type");

  /* Test Bollinger dispatch (uses default 2.0 stddev) */
  SamtraderIndicatorSeries *bb =
      samtrader_indicator_calculate(arena, SAMTRADER_IND_BOLLINGER, ohlcv, 3);
  ASSERT(bb != NULL, "Bollinger dispatch should work");
  ASSERT(bb->type == SAMTRADER_IND_BOLLINGER, "Should be BOLLINGER type");

  /* Test MACD dispatch (uses default 12/26/9) */
  SamtraderIndicatorSeries *macd =
      samtrader_indicator_calculate(arena, SAMTRADER_IND_MACD, ohlcv, 14);
  ASSERT(macd != NULL, "MACD dispatch should work");
  ASSERT(macd->type == SAMTRADER_IND_MACD, "Should be MACD type");

  /* Test Stochastic dispatch (uses period for %K, default 3 for %D) */
  SamtraderIndicatorSeries *stoch =
      samtrader_indicator_calculate(arena, SAMTRADER_IND_STOCHASTIC, ohlcv, 3);
  ASSERT(stoch != NULL, "Stochastic dispatch should work");
  ASSERT(stoch->type == SAMTRADER_IND_STOCHASTIC, "Should be STOCHASTIC type");

  /* Test ATR dispatch */
  SamtraderIndicatorSeries *atr = samtrader_indicator_calculate(arena, SAMTRADER_IND_ATR, ohlcv, 3);
  ASSERT(atr != NULL, "ATR dispatch should work");
  ASSERT(atr->type == SAMTRADER_IND_ATR, "Should be ATR type");

  /* Test Pivot dispatch */
  SamtraderIndicatorSeries *pvt =
      samtrader_indicator_calculate(arena, SAMTRADER_IND_PIVOT, ohlcv, 0);
  ASSERT(pvt != NULL, "Pivot dispatch should work");
  ASSERT(pvt->type == SAMTRADER_IND_PIVOT, "Should be PIVOT type");

  /* Test unsupported type */
  SamtraderIndicatorSeries *roc =
      samtrader_indicator_calculate(arena, SAMTRADER_IND_ROC, ohlcv, 14);
  ASSERT(roc == NULL, "Unsupported type should return NULL");

  samrena_destroy(arena);
  printf("  PASS\n");
  return 0;
}

/*============================================================================
 * Comparison Tests (SMA vs EMA vs WMA)
 *============================================================================*/

static int test_moving_averages_comparison(void) {
  printf("Testing moving averages relative behavior...\n");

  Samrena *arena = samrena_create_default();
  ASSERT(arena != NULL, "Failed to create arena");

  /* Rising prices - EMA and WMA should be higher than SMA */
  double rising[] = {1.0, 2.0, 3.0, 4.0, 5.0, 6.0, 7.0, 8.0, 9.0, 10.0};
  SamrenaVector *ohlcv = create_test_ohlcv(arena, rising, 10);

  SamtraderIndicatorSeries *sma = samtrader_calculate_sma(arena, ohlcv, 5);
  SamtraderIndicatorSeries *ema = samtrader_calculate_ema(arena, ohlcv, 5);
  SamtraderIndicatorSeries *wma = samtrader_calculate_wma(arena, ohlcv, 5);

  ASSERT(sma != NULL && ema != NULL && wma != NULL, "All calculations should succeed");

  /* Check last value - with rising prices, EMA and WMA should lead SMA */
  const SamtraderIndicatorValue *sma_val = samtrader_indicator_series_at(sma, 9);
  const SamtraderIndicatorValue *ema_val = samtrader_indicator_series_at(ema, 9);
  const SamtraderIndicatorValue *wma_val = samtrader_indicator_series_at(wma, 9);

  ASSERT(sma_val && ema_val && wma_val, "All values should exist");

  /* SMA(5) at last: (6+7+8+9+10)/5 = 8.0 */
  ASSERT_DOUBLE_EQ(sma_val->data.simple.value, 8.0, "SMA at last index");

  /* EMA and WMA should be >= SMA for rising prices */
  ASSERT(ema_val->data.simple.value >= sma_val->data.simple.value - 0.0001,
         "EMA should be >= SMA for rising prices");
  ASSERT(wma_val->data.simple.value >= sma_val->data.simple.value - 0.0001,
         "WMA should be >= SMA for rising prices");

  samrena_destroy(arena);
  printf("  PASS\n");
  return 0;
}

int main(void) {
  printf("=== Indicator Calculation Tests ===\n\n");

  int failures = 0;

  /* SMA tests */
  failures += test_sma_basic();
  failures += test_sma_period_1();
  failures += test_sma_invalid_params();
  failures += test_sma_constant_prices();
  failures += test_sma_latest_value();

  /* EMA tests */
  failures += test_ema_basic();
  failures += test_ema_convergence();
  failures += test_ema_invalid_params();
  failures += test_ema_period_1();
  failures += test_ema_latest_value();

  /* WMA tests */
  failures += test_wma_basic();
  failures += test_wma_weighting();
  failures += test_wma_invalid_params();
  failures += test_wma_period_1();
  failures += test_wma_constant_prices();
  failures += test_wma_latest_value();

  /* RSI tests */
  failures += test_rsi_basic();
  failures += test_rsi_all_gains();
  failures += test_rsi_all_losses();
  failures += test_rsi_constant();
  failures += test_rsi_period_1();
  failures += test_rsi_invalid_params();
  failures += test_rsi_known_values();
  failures += test_rsi_latest_value();

  /* MACD tests */
  failures += test_macd_basic();
  failures += test_macd_known_values();
  failures += test_macd_constant_prices();
  failures += test_macd_histogram_identity();
  failures += test_macd_invalid_params();
  failures += test_macd_latest_value();

  /* Stochastic tests */
  failures += test_stochastic_basic();
  failures += test_stochastic_warmup_period();
  failures += test_stochastic_range();
  failures += test_stochastic_constant_prices();
  failures += test_stochastic_extremes();
  failures += test_stochastic_invalid_params();
  failures += test_stochastic_latest_value();

  /* Bollinger Bands tests */
  failures += test_bollinger_basic();
  failures += test_bollinger_constant_prices();
  failures += test_bollinger_band_symmetry();
  failures += test_bollinger_stddev_multiplier();
  failures += test_bollinger_invalid_params();
  failures += test_bollinger_latest_value();

  /* ATR tests */
  failures += test_atr_basic();
  failures += test_atr_constant_prices();
  failures += test_atr_period_1();
  failures += test_atr_always_positive();
  failures += test_atr_invalid_params();
  failures += test_atr_latest_value();

  /* Pivot Points tests */
  failures += test_pivot_basic();
  failures += test_pivot_level_ordering();
  failures += test_pivot_constant_prices();
  failures += test_pivot_invalid_params();
  failures += test_pivot_latest_value();

  /* Dispatcher test */
  failures += test_indicator_calculate_dispatcher();

  /* Comparison test */
  failures += test_moving_averages_comparison();

  printf("\n=== Results: %d failures ===\n", failures);

  return failures > 0 ? EXIT_FAILURE : EXIT_SUCCESS;
}
