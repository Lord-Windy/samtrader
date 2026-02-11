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

static int test_ohlcv_create(void) {
  printf("Testing samtrader_ohlcv_create...\n");

  Samrena *arena = samrena_create_default();
  ASSERT(arena != NULL, "Failed to create arena");

  SamtraderOhlcv *ohlcv =
      samtrader_ohlcv_create(arena, "AAPL", "US", 1704067200, 150.0, 155.0, 149.0, 153.0, 1000000);

  ASSERT(ohlcv != NULL, "Failed to create OHLCV");
  ASSERT(strcmp(ohlcv->code, "AAPL") == 0, "Code mismatch");
  ASSERT(strcmp(ohlcv->exchange, "US") == 0, "Exchange mismatch");
  ASSERT(ohlcv->date == 1704067200, "Date mismatch");
  ASSERT_DOUBLE_EQ(ohlcv->open, 150.0, "Open mismatch");
  ASSERT_DOUBLE_EQ(ohlcv->high, 155.0, "High mismatch");
  ASSERT_DOUBLE_EQ(ohlcv->low, 149.0, "Low mismatch");
  ASSERT_DOUBLE_EQ(ohlcv->close, 153.0, "Close mismatch");
  ASSERT(ohlcv->volume == 1000000, "Volume mismatch");

  samrena_destroy(arena);
  printf("  PASS\n");
  return 0;
}

static int test_ohlcv_typical_price(void) {
  printf("Testing samtrader_ohlcv_typical_price...\n");

  Samrena *arena = samrena_create_default();
  ASSERT(arena != NULL, "Failed to create arena");

  SamtraderOhlcv *ohlcv =
      samtrader_ohlcv_create(arena, "AAPL", "US", 1704067200, 150.0, 155.0, 149.0, 153.0, 1000000);

  ASSERT(ohlcv != NULL, "Failed to create OHLCV");

  double typical = samtrader_ohlcv_typical_price(ohlcv);
  double expected = (155.0 + 149.0 + 153.0) / 3.0;
  ASSERT_DOUBLE_EQ(typical, expected, "Typical price calculation");

  samrena_destroy(arena);
  printf("  PASS\n");
  return 0;
}

static int test_ohlcv_true_range(void) {
  printf("Testing samtrader_ohlcv_true_range...\n");

  Samrena *arena = samrena_create_default();
  ASSERT(arena != NULL, "Failed to create arena");

  SamtraderOhlcv *ohlcv =
      samtrader_ohlcv_create(arena, "AAPL", "US", 1704067200, 150.0, 155.0, 149.0, 153.0, 1000000);

  ASSERT(ohlcv != NULL, "Failed to create OHLCV");

  double tr = samtrader_ohlcv_true_range(ohlcv, 148.0);
  ASSERT_DOUBLE_EQ(tr, 7.0, "True range with prev_close below low");

  tr = samtrader_ohlcv_true_range(ohlcv, 160.0);
  ASSERT_DOUBLE_EQ(tr, 11.0, "True range with prev_close above high");

  tr = samtrader_ohlcv_true_range(ohlcv, 152.0);
  ASSERT_DOUBLE_EQ(tr, 6.0, "True range with prev_close within range");

  samrena_destroy(arena);
  printf("  PASS\n");
  return 0;
}

static int test_ohlcv_vector(void) {
  printf("Testing samtrader_ohlcv_vector_create...\n");

  Samrena *arena = samrena_create_default();
  ASSERT(arena != NULL, "Failed to create arena");

  SamrenaVector *vec = samtrader_ohlcv_vector_create(arena, 10);
  ASSERT(vec != NULL, "Failed to create OHLCV vector");

  SamtraderOhlcv ohlcv1 = {.code = "AAPL",
                           .exchange = "US",
                           .date = 1704067200,
                           .open = 150.0,
                           .high = 155.0,
                           .low = 149.0,
                           .close = 153.0,
                           .volume = 1000000};

  SamtraderOhlcv ohlcv2 = {.code = "AAPL",
                           .exchange = "US",
                           .date = 1704153600,
                           .open = 153.0,
                           .high = 158.0,
                           .low = 152.0,
                           .close = 157.0,
                           .volume = 1200000};

  samrena_vector_push(vec, &ohlcv1);
  samrena_vector_push(vec, &ohlcv2);

  ASSERT(samrena_vector_size(vec) == 2, "Vector size should be 2");

  const SamtraderOhlcv *retrieved = (const SamtraderOhlcv *)samrena_vector_at_const(vec, 0);
  ASSERT(retrieved != NULL, "Failed to retrieve OHLCV at index 0");
  ASSERT_DOUBLE_EQ(retrieved->close, 153.0, "First OHLCV close price");

  retrieved = (const SamtraderOhlcv *)samrena_vector_at_const(vec, 1);
  ASSERT(retrieved != NULL, "Failed to retrieve OHLCV at index 1");
  ASSERT_DOUBLE_EQ(retrieved->close, 157.0, "Second OHLCV close price");

  samrena_destroy(arena);
  printf("  PASS\n");
  return 0;
}

int main(void) {
  printf("=== OHLCV Data Structures Tests ===\n\n");

  int failures = 0;

  failures += test_ohlcv_create();
  failures += test_ohlcv_typical_price();
  failures += test_ohlcv_true_range();
  failures += test_ohlcv_vector();

  printf("\n=== Results: %d failures ===\n", failures);

  return failures > 0 ? EXIT_FAILURE : EXIT_SUCCESS;
}
