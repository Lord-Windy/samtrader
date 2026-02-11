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

#include "samtrader/domain/position.h"

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

static int test_position_create_long(void) {
  printf("Testing samtrader_position_create (long)...\n");

  Samrena *arena = samrena_create_default();
  ASSERT(arena != NULL, "Failed to create arena");

  SamtraderPosition *pos =
      samtrader_position_create(arena, "AAPL", "US", 100, 150.0, 1704067200, 140.0, 170.0);
  ASSERT(pos != NULL, "Failed to create position");
  ASSERT(strcmp(pos->code, "AAPL") == 0, "Code mismatch");
  ASSERT(strcmp(pos->exchange, "US") == 0, "Exchange mismatch");
  ASSERT(pos->quantity == 100, "Quantity mismatch");
  ASSERT_DOUBLE_EQ(pos->entry_price, 150.0, "Entry price mismatch");
  ASSERT(pos->entry_date == 1704067200, "Entry date mismatch");
  ASSERT_DOUBLE_EQ(pos->stop_loss, 140.0, "Stop loss mismatch");
  ASSERT_DOUBLE_EQ(pos->take_profit, 170.0, "Take profit mismatch");

  samrena_destroy(arena);
  printf("  PASS\n");
  return 0;
}

static int test_position_create_short(void) {
  printf("Testing samtrader_position_create (short)...\n");

  Samrena *arena = samrena_create_default();
  ASSERT(arena != NULL, "Failed to create arena");

  SamtraderPosition *pos =
      samtrader_position_create(arena, "BHP", "AU", -50, 45.0, 1704067200, 50.0, 40.0);
  ASSERT(pos != NULL, "Failed to create position");
  ASSERT(strcmp(pos->code, "BHP") == 0, "Code mismatch");
  ASSERT(strcmp(pos->exchange, "AU") == 0, "Exchange mismatch");
  ASSERT(pos->quantity == -50, "Quantity mismatch");
  ASSERT_DOUBLE_EQ(pos->entry_price, 45.0, "Entry price mismatch");
  ASSERT_DOUBLE_EQ(pos->stop_loss, 50.0, "Stop loss mismatch");
  ASSERT_DOUBLE_EQ(pos->take_profit, 40.0, "Take profit mismatch");

  samrena_destroy(arena);
  printf("  PASS\n");
  return 0;
}

static int test_position_create_no_stops(void) {
  printf("Testing samtrader_position_create (no stops)...\n");

  Samrena *arena = samrena_create_default();
  ASSERT(arena != NULL, "Failed to create arena");

  SamtraderPosition *pos =
      samtrader_position_create(arena, "MSFT", "US", 200, 380.0, 1704067200, 0, 0);
  ASSERT(pos != NULL, "Failed to create position");
  ASSERT_DOUBLE_EQ(pos->stop_loss, 0.0, "Stop loss should be 0");
  ASSERT_DOUBLE_EQ(pos->take_profit, 0.0, "Take profit should be 0");

  samrena_destroy(arena);
  printf("  PASS\n");
  return 0;
}

static int test_position_is_long_short(void) {
  printf("Testing samtrader_position_is_long/is_short...\n");

  Samrena *arena = samrena_create_default();
  ASSERT(arena != NULL, "Failed to create arena");

  SamtraderPosition *long_pos = samtrader_position_create(arena, "AAPL", "US", 100, 150.0, 0, 0, 0);
  ASSERT(long_pos != NULL, "Failed to create long position");
  ASSERT(samtrader_position_is_long(long_pos), "Should be long");
  ASSERT(!samtrader_position_is_short(long_pos), "Should not be short");

  SamtraderPosition *short_pos = samtrader_position_create(arena, "BHP", "AU", -50, 45.0, 0, 0, 0);
  ASSERT(short_pos != NULL, "Failed to create short position");
  ASSERT(!samtrader_position_is_long(short_pos), "Should not be long");
  ASSERT(samtrader_position_is_short(short_pos), "Should be short");

  /* Zero quantity is neither long nor short */
  SamtraderPosition *zero_pos = samtrader_position_create(arena, "GOOG", "US", 0, 100.0, 0, 0, 0);
  ASSERT(zero_pos != NULL, "Failed to create zero position");
  ASSERT(!samtrader_position_is_long(zero_pos), "Zero should not be long");
  ASSERT(!samtrader_position_is_short(zero_pos), "Zero should not be short");

  samrena_destroy(arena);
  printf("  PASS\n");
  return 0;
}

static int test_position_market_value(void) {
  printf("Testing samtrader_position_market_value...\n");

  Samrena *arena = samrena_create_default();
  ASSERT(arena != NULL, "Failed to create arena");

  /* Long position: 100 shares at $160 = $16,000 */
  SamtraderPosition *long_pos = samtrader_position_create(arena, "AAPL", "US", 100, 150.0, 0, 0, 0);
  ASSERT_DOUBLE_EQ(samtrader_position_market_value(long_pos, 160.0), 16000.0, "Long market value");

  /* Short position: |-50| shares at $48 = $2,400 */
  SamtraderPosition *short_pos = samtrader_position_create(arena, "BHP", "AU", -50, 45.0, 0, 0, 0);
  ASSERT_DOUBLE_EQ(samtrader_position_market_value(short_pos, 48.0), 2400.0, "Short market value");

  samrena_destroy(arena);
  printf("  PASS\n");
  return 0;
}

static int test_position_unrealized_pnl(void) {
  printf("Testing samtrader_position_unrealized_pnl...\n");

  Samrena *arena = samrena_create_default();
  ASSERT(arena != NULL, "Failed to create arena");

  /* Long position profit: 100 * (160 - 150) = 1000 */
  SamtraderPosition *long_pos = samtrader_position_create(arena, "AAPL", "US", 100, 150.0, 0, 0, 0);
  ASSERT_DOUBLE_EQ(samtrader_position_unrealized_pnl(long_pos, 160.0), 1000.0, "Long profit");

  /* Long position loss: 100 * (140 - 150) = -1000 */
  ASSERT_DOUBLE_EQ(samtrader_position_unrealized_pnl(long_pos, 140.0), -1000.0, "Long loss");

  /* Short position profit: -50 * (42 - 45) = 150 */
  SamtraderPosition *short_pos = samtrader_position_create(arena, "BHP", "AU", -50, 45.0, 0, 0, 0);
  ASSERT_DOUBLE_EQ(samtrader_position_unrealized_pnl(short_pos, 42.0), 150.0, "Short profit");

  /* Short position loss: -50 * (48 - 45) = -150 */
  ASSERT_DOUBLE_EQ(samtrader_position_unrealized_pnl(short_pos, 48.0), -150.0, "Short loss");

  /* Breakeven: 100 * (150 - 150) = 0 */
  ASSERT_DOUBLE_EQ(samtrader_position_unrealized_pnl(long_pos, 150.0), 0.0, "Breakeven");

  samrena_destroy(arena);
  printf("  PASS\n");
  return 0;
}

static int test_position_stop_loss(void) {
  printf("Testing samtrader_position_should_stop_loss...\n");

  Samrena *arena = samrena_create_default();
  ASSERT(arena != NULL, "Failed to create arena");

  /* Long position: stop loss at 140 */
  SamtraderPosition *long_pos =
      samtrader_position_create(arena, "AAPL", "US", 100, 150.0, 0, 140.0, 0);

  ASSERT(!samtrader_position_should_stop_loss(long_pos, 150.0), "Above stop: not triggered");
  ASSERT(!samtrader_position_should_stop_loss(long_pos, 141.0), "Just above stop: not triggered");
  ASSERT(samtrader_position_should_stop_loss(long_pos, 140.0), "At stop: triggered");
  ASSERT(samtrader_position_should_stop_loss(long_pos, 130.0), "Below stop: triggered");

  /* Short position: stop loss at 50 */
  SamtraderPosition *short_pos =
      samtrader_position_create(arena, "BHP", "AU", -50, 45.0, 0, 50.0, 0);

  ASSERT(!samtrader_position_should_stop_loss(short_pos, 45.0), "Below stop: not triggered");
  ASSERT(!samtrader_position_should_stop_loss(short_pos, 49.0), "Just below stop: not triggered");
  ASSERT(samtrader_position_should_stop_loss(short_pos, 50.0), "At stop: triggered");
  ASSERT(samtrader_position_should_stop_loss(short_pos, 55.0), "Above stop: triggered");

  /* No stop loss set */
  SamtraderPosition *no_stop = samtrader_position_create(arena, "MSFT", "US", 100, 380.0, 0, 0, 0);
  ASSERT(!samtrader_position_should_stop_loss(no_stop, 1.0), "No stop set: never triggered");

  samrena_destroy(arena);
  printf("  PASS\n");
  return 0;
}

static int test_position_take_profit(void) {
  printf("Testing samtrader_position_should_take_profit...\n");

  Samrena *arena = samrena_create_default();
  ASSERT(arena != NULL, "Failed to create arena");

  /* Long position: take profit at 170 */
  SamtraderPosition *long_pos =
      samtrader_position_create(arena, "AAPL", "US", 100, 150.0, 0, 0, 170.0);

  ASSERT(!samtrader_position_should_take_profit(long_pos, 150.0), "Below target: not triggered");
  ASSERT(!samtrader_position_should_take_profit(long_pos, 169.0),
         "Just below target: not triggered");
  ASSERT(samtrader_position_should_take_profit(long_pos, 170.0), "At target: triggered");
  ASSERT(samtrader_position_should_take_profit(long_pos, 180.0), "Above target: triggered");

  /* Short position: take profit at 40 */
  SamtraderPosition *short_pos =
      samtrader_position_create(arena, "BHP", "AU", -50, 45.0, 0, 0, 40.0);

  ASSERT(!samtrader_position_should_take_profit(short_pos, 45.0), "Above target: not triggered");
  ASSERT(!samtrader_position_should_take_profit(short_pos, 41.0),
         "Just above target: not triggered");
  ASSERT(samtrader_position_should_take_profit(short_pos, 40.0), "At target: triggered");
  ASSERT(samtrader_position_should_take_profit(short_pos, 35.0), "Below target: triggered");

  /* No take profit set */
  SamtraderPosition *no_tp = samtrader_position_create(arena, "MSFT", "US", 100, 380.0, 0, 0, 0);
  ASSERT(!samtrader_position_should_take_profit(no_tp, 99999.0), "No TP set: never triggered");

  samrena_destroy(arena);
  printf("  PASS\n");
  return 0;
}

static int test_position_null_params(void) {
  printf("Testing position NULL parameter handling...\n");

  Samrena *arena = samrena_create_default();
  ASSERT(arena != NULL, "Failed to create arena");

  /* Create with NULL params */
  ASSERT(samtrader_position_create(NULL, "AAPL", "US", 100, 150.0, 0, 0, 0) == NULL,
         "Create with NULL arena");
  ASSERT(samtrader_position_create(arena, NULL, "US", 100, 150.0, 0, 0, 0) == NULL,
         "Create with NULL code");
  ASSERT(samtrader_position_create(arena, "AAPL", NULL, 100, 150.0, 0, 0, 0) == NULL,
         "Create with NULL exchange");

  /* Functions with NULL position */
  ASSERT(!samtrader_position_is_long(NULL), "is_long NULL");
  ASSERT(!samtrader_position_is_short(NULL), "is_short NULL");
  ASSERT_DOUBLE_EQ(samtrader_position_market_value(NULL, 150.0), 0.0, "market_value NULL");
  ASSERT_DOUBLE_EQ(samtrader_position_unrealized_pnl(NULL, 150.0), 0.0, "unrealized_pnl NULL");
  ASSERT(!samtrader_position_should_stop_loss(NULL, 150.0), "stop_loss NULL");
  ASSERT(!samtrader_position_should_take_profit(NULL, 150.0), "take_profit NULL");

  samrena_destroy(arena);
  printf("  PASS\n");
  return 0;
}

static int test_position_string_independence(void) {
  printf("Testing position string independence from source...\n");

  Samrena *arena = samrena_create_default();
  ASSERT(arena != NULL, "Failed to create arena");

  char code_buf[16] = "AAPL";
  char exchange_buf[16] = "US";

  SamtraderPosition *pos =
      samtrader_position_create(arena, code_buf, exchange_buf, 100, 150.0, 0, 0, 0);
  ASSERT(pos != NULL, "Failed to create position");

  /* Modify source buffers */
  strcpy(code_buf, "XXXX");
  strcpy(exchange_buf, "ZZ");

  /* Position strings should be independent copies */
  ASSERT(strcmp(pos->code, "AAPL") == 0, "Code should be independent copy");
  ASSERT(strcmp(pos->exchange, "US") == 0, "Exchange should be independent copy");

  samrena_destroy(arena);
  printf("  PASS\n");
  return 0;
}

int main(void) {
  printf("=== Position Tests ===\n\n");

  int failures = 0;

  failures += test_position_create_long();
  failures += test_position_create_short();
  failures += test_position_create_no_stops();
  failures += test_position_is_long_short();
  failures += test_position_market_value();
  failures += test_position_unrealized_pnl();
  failures += test_position_stop_loss();
  failures += test_position_take_profit();
  failures += test_position_null_params();
  failures += test_position_string_independence();

  printf("\n=== Results: %d failures ===\n", failures);

  return failures > 0 ? EXIT_FAILURE : EXIT_SUCCESS;
}
