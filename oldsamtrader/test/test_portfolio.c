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

#include "samtrader/domain/portfolio.h"

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

static int test_portfolio_create(void) {
  printf("Testing samtrader_portfolio_create...\n");

  Samrena *arena = samrena_create_default();
  ASSERT(arena != NULL, "Failed to create arena");

  SamtraderPortfolio *portfolio = samtrader_portfolio_create(arena, 100000.0);
  ASSERT(portfolio != NULL, "Failed to create portfolio");
  ASSERT_DOUBLE_EQ(portfolio->cash, 100000.0, "Initial cash");
  ASSERT_DOUBLE_EQ(portfolio->initial_capital, 100000.0, "Initial capital");
  ASSERT(samtrader_portfolio_position_count(portfolio) == 0, "Should have 0 positions");
  ASSERT(samrena_vector_size(portfolio->closed_trades) == 0, "Should have 0 closed trades");
  ASSERT(samrena_vector_size(portfolio->equity_curve) == 0, "Should have 0 equity points");

  samrena_destroy(arena);
  printf("  PASS\n");
  return 0;
}

static int test_portfolio_add_position(void) {
  printf("Testing samtrader_portfolio_add_position...\n");

  Samrena *arena = samrena_create_default();
  ASSERT(arena != NULL, "Failed to create arena");

  SamtraderPortfolio *portfolio = samtrader_portfolio_create(arena, 100000.0);
  ASSERT(portfolio != NULL, "Failed to create portfolio");

  SamtraderPosition pos = {.code = "AAPL",
                           .exchange = "US",
                           .quantity = 100,
                           .entry_price = 150.0,
                           .entry_date = 1704067200,
                           .stop_loss = 140.0,
                           .take_profit = 170.0};

  bool result = samtrader_portfolio_add_position(portfolio, arena, &pos);
  ASSERT(result, "Failed to add position");
  ASSERT(samtrader_portfolio_position_count(portfolio) == 1, "Should have 1 position");

  SamtraderPosition *retrieved = samtrader_portfolio_get_position(portfolio, "AAPL");
  ASSERT(retrieved != NULL, "Failed to get position");
  ASSERT(strcmp(retrieved->code, "AAPL") == 0, "Code mismatch");
  ASSERT(strcmp(retrieved->exchange, "US") == 0, "Exchange mismatch");
  ASSERT(retrieved->quantity == 100, "Quantity mismatch");
  ASSERT_DOUBLE_EQ(retrieved->entry_price, 150.0, "Entry price mismatch");
  ASSERT(retrieved->entry_date == 1704067200, "Entry date mismatch");
  ASSERT_DOUBLE_EQ(retrieved->stop_loss, 140.0, "Stop loss mismatch");
  ASSERT_DOUBLE_EQ(retrieved->take_profit, 170.0, "Take profit mismatch");

  /* Add a second position */
  SamtraderPosition pos2 = {.code = "BHP",
                            .exchange = "AU",
                            .quantity = -50,
                            .entry_price = 45.0,
                            .entry_date = 1704067200,
                            .stop_loss = 0,
                            .take_profit = 0};

  result = samtrader_portfolio_add_position(portfolio, arena, &pos2);
  ASSERT(result, "Failed to add second position");
  ASSERT(samtrader_portfolio_position_count(portfolio) == 2, "Should have 2 positions");

  SamtraderPosition *retrieved2 = samtrader_portfolio_get_position(portfolio, "BHP");
  ASSERT(retrieved2 != NULL, "Failed to get second position");
  ASSERT(retrieved2->quantity == -50, "Short quantity mismatch");

  samrena_destroy(arena);
  printf("  PASS\n");
  return 0;
}

static int test_portfolio_remove_position(void) {
  printf("Testing samtrader_portfolio_remove_position...\n");

  Samrena *arena = samrena_create_default();
  ASSERT(arena != NULL, "Failed to create arena");

  SamtraderPortfolio *portfolio = samtrader_portfolio_create(arena, 100000.0);
  ASSERT(portfolio != NULL, "Failed to create portfolio");

  SamtraderPosition pos = {
      .code = "AAPL", .exchange = "US", .quantity = 100, .entry_price = 150.0, .entry_date = 0};

  samtrader_portfolio_add_position(portfolio, arena, &pos);
  ASSERT(samtrader_portfolio_position_count(portfolio) == 1, "Should have 1 position");

  bool removed = samtrader_portfolio_remove_position(portfolio, "AAPL");
  ASSERT(removed, "Remove should return true");
  ASSERT(samtrader_portfolio_position_count(portfolio) == 0, "Should have 0 positions");

  SamtraderPosition *gone = samtrader_portfolio_get_position(portfolio, "AAPL");
  ASSERT(gone == NULL, "Position should be gone");

  /* Removing non-existent position returns false */
  bool removed2 = samtrader_portfolio_remove_position(portfolio, "MSFT");
  ASSERT(!removed2, "Remove non-existent should return false");

  samrena_destroy(arena);
  printf("  PASS\n");
  return 0;
}

static int test_portfolio_has_position(void) {
  printf("Testing samtrader_portfolio_has_position...\n");

  Samrena *arena = samrena_create_default();
  ASSERT(arena != NULL, "Failed to create arena");

  SamtraderPortfolio *portfolio = samtrader_portfolio_create(arena, 100000.0);
  ASSERT(portfolio != NULL, "Failed to create portfolio");

  ASSERT(!samtrader_portfolio_has_position(portfolio, "AAPL"), "Should not have AAPL");

  SamtraderPosition pos = {
      .code = "AAPL", .exchange = "US", .quantity = 100, .entry_price = 150.0, .entry_date = 0};

  samtrader_portfolio_add_position(portfolio, arena, &pos);
  ASSERT(samtrader_portfolio_has_position(portfolio, "AAPL"), "Should have AAPL");
  ASSERT(!samtrader_portfolio_has_position(portfolio, "MSFT"), "Should not have MSFT");

  samrena_destroy(arena);
  printf("  PASS\n");
  return 0;
}

static int test_portfolio_position_count(void) {
  printf("Testing samtrader_portfolio_position_count...\n");

  Samrena *arena = samrena_create_default();
  ASSERT(arena != NULL, "Failed to create arena");

  SamtraderPortfolio *portfolio = samtrader_portfolio_create(arena, 100000.0);
  ASSERT(portfolio != NULL, "Failed to create portfolio");

  ASSERT(samtrader_portfolio_position_count(portfolio) == 0, "Should start at 0");

  SamtraderPosition pos1 = {
      .code = "AAPL", .exchange = "US", .quantity = 100, .entry_price = 150.0, .entry_date = 0};
  SamtraderPosition pos2 = {
      .code = "BHP", .exchange = "AU", .quantity = 50, .entry_price = 45.0, .entry_date = 0};
  SamtraderPosition pos3 = {
      .code = "MSFT", .exchange = "US", .quantity = 75, .entry_price = 380.0, .entry_date = 0};

  samtrader_portfolio_add_position(portfolio, arena, &pos1);
  ASSERT(samtrader_portfolio_position_count(portfolio) == 1, "Should be 1");

  samtrader_portfolio_add_position(portfolio, arena, &pos2);
  ASSERT(samtrader_portfolio_position_count(portfolio) == 2, "Should be 2");

  samtrader_portfolio_add_position(portfolio, arena, &pos3);
  ASSERT(samtrader_portfolio_position_count(portfolio) == 3, "Should be 3");

  samtrader_portfolio_remove_position(portfolio, "BHP");
  ASSERT(samtrader_portfolio_position_count(portfolio) == 2, "Should be 2 after remove");

  samrena_destroy(arena);
  printf("  PASS\n");
  return 0;
}

static int test_portfolio_record_trade(void) {
  printf("Testing samtrader_portfolio_record_trade...\n");

  Samrena *arena = samrena_create_default();
  ASSERT(arena != NULL, "Failed to create arena");

  SamtraderPortfolio *portfolio = samtrader_portfolio_create(arena, 100000.0);
  ASSERT(portfolio != NULL, "Failed to create portfolio");

  SamtraderClosedTrade trade1 = {.code = "AAPL",
                                 .exchange = "US",
                                 .quantity = 100,
                                 .entry_price = 150.0,
                                 .exit_price = 160.0,
                                 .entry_date = 1704067200,
                                 .exit_date = 1704672000,
                                 .pnl = 1000.0};

  bool result = samtrader_portfolio_record_trade(portfolio, arena, &trade1);
  ASSERT(result, "Failed to record trade");
  ASSERT(samrena_vector_size(portfolio->closed_trades) == 1, "Should have 1 trade");

  const SamtraderClosedTrade *retrieved =
      (const SamtraderClosedTrade *)samrena_vector_at_const(portfolio->closed_trades, 0);
  ASSERT(retrieved != NULL, "Failed to retrieve trade");
  ASSERT(strcmp(retrieved->code, "AAPL") == 0, "Trade code mismatch");
  ASSERT(strcmp(retrieved->exchange, "US") == 0, "Trade exchange mismatch");
  ASSERT(retrieved->quantity == 100, "Trade quantity mismatch");
  ASSERT_DOUBLE_EQ(retrieved->entry_price, 150.0, "Trade entry price mismatch");
  ASSERT_DOUBLE_EQ(retrieved->exit_price, 160.0, "Trade exit price mismatch");
  ASSERT_DOUBLE_EQ(retrieved->pnl, 1000.0, "Trade PnL mismatch");

  /* Record a second trade (losing short) */
  SamtraderClosedTrade trade2 = {.code = "BHP",
                                 .exchange = "AU",
                                 .quantity = -50,
                                 .entry_price = 45.0,
                                 .exit_price = 48.0,
                                 .entry_date = 1704067200,
                                 .exit_date = 1704672000,
                                 .pnl = -150.0};

  result = samtrader_portfolio_record_trade(portfolio, arena, &trade2);
  ASSERT(result, "Failed to record second trade");
  ASSERT(samrena_vector_size(portfolio->closed_trades) == 2, "Should have 2 trades");

  samrena_destroy(arena);
  printf("  PASS\n");
  return 0;
}

static int test_portfolio_record_equity(void) {
  printf("Testing samtrader_portfolio_record_equity...\n");

  Samrena *arena = samrena_create_default();
  ASSERT(arena != NULL, "Failed to create arena");

  SamtraderPortfolio *portfolio = samtrader_portfolio_create(arena, 100000.0);
  ASSERT(portfolio != NULL, "Failed to create portfolio");

  bool result = samtrader_portfolio_record_equity(portfolio, arena, 1704067200, 100000.0);
  ASSERT(result, "Failed to record equity point");

  result = samtrader_portfolio_record_equity(portfolio, arena, 1704153600, 101500.0);
  ASSERT(result, "Failed to record second equity point");

  result = samtrader_portfolio_record_equity(portfolio, arena, 1704240000, 99800.0);
  ASSERT(result, "Failed to record third equity point");

  ASSERT(samrena_vector_size(portfolio->equity_curve) == 3, "Should have 3 equity points");

  const SamtraderEquityPoint *p0 =
      (const SamtraderEquityPoint *)samrena_vector_at_const(portfolio->equity_curve, 0);
  ASSERT(p0 != NULL, "Failed to get equity point 0");
  ASSERT(p0->date == 1704067200, "Equity point 0 date mismatch");
  ASSERT_DOUBLE_EQ(p0->equity, 100000.0, "Equity point 0 value mismatch");

  const SamtraderEquityPoint *p1 =
      (const SamtraderEquityPoint *)samrena_vector_at_const(portfolio->equity_curve, 1);
  ASSERT(p1 != NULL, "Failed to get equity point 1");
  ASSERT_DOUBLE_EQ(p1->equity, 101500.0, "Equity point 1 value mismatch");

  const SamtraderEquityPoint *p2 =
      (const SamtraderEquityPoint *)samrena_vector_at_const(portfolio->equity_curve, 2);
  ASSERT(p2 != NULL, "Failed to get equity point 2");
  ASSERT_DOUBLE_EQ(p2->equity, 99800.0, "Equity point 2 value mismatch");

  samrena_destroy(arena);
  printf("  PASS\n");
  return 0;
}

static int test_portfolio_total_equity(void) {
  printf("Testing samtrader_portfolio_total_equity...\n");

  Samrena *arena = samrena_create_default();
  ASSERT(arena != NULL, "Failed to create arena");

  SamtraderPortfolio *portfolio = samtrader_portfolio_create(arena, 50000.0);
  ASSERT(portfolio != NULL, "Failed to create portfolio");

  /* Add two positions */
  SamtraderPosition pos1 = {
      .code = "AAPL", .exchange = "US", .quantity = 100, .entry_price = 150.0, .entry_date = 0};
  SamtraderPosition pos2 = {
      .code = "BHP", .exchange = "AU", .quantity = 200, .entry_price = 45.0, .entry_date = 0};

  samtrader_portfolio_add_position(portfolio, arena, &pos1);
  samtrader_portfolio_add_position(portfolio, arena, &pos2);

  /* Create price map with current prices */
  SamHashMap *price_map = samhashmap_create(16, arena);
  ASSERT(price_map != NULL, "Failed to create price map");

  double aapl_price = 160.0;
  double bhp_price = 50.0;
  samhashmap_put(price_map, "AAPL", &aapl_price);
  samhashmap_put(price_map, "BHP", &bhp_price);

  /* Total equity = cash + AAPL market value + BHP market value */
  /* = 50000 + (100 * 160) + (200 * 50) = 50000 + 16000 + 10000 = 76000 */
  double equity = samtrader_portfolio_total_equity(portfolio, price_map);
  ASSERT_DOUBLE_EQ(equity, 76000.0, "Total equity calculation");

  /* Test with no positions (cash only) */
  SamtraderPortfolio *cash_only = samtrader_portfolio_create(arena, 25000.0);
  ASSERT(cash_only != NULL, "Failed to create cash-only portfolio");

  double cash_equity = samtrader_portfolio_total_equity(cash_only, price_map);
  ASSERT_DOUBLE_EQ(cash_equity, 25000.0, "Cash-only equity");

  samrena_destroy(arena);
  printf("  PASS\n");
  return 0;
}

static int test_portfolio_null_params(void) {
  printf("Testing portfolio NULL parameter handling...\n");

  Samrena *arena = samrena_create_default();
  ASSERT(arena != NULL, "Failed to create arena");

  /* Create with NULL arena */
  ASSERT(samtrader_portfolio_create(NULL, 100000.0) == NULL, "Create with NULL arena");

  SamtraderPortfolio *portfolio = samtrader_portfolio_create(arena, 100000.0);
  ASSERT(portfolio != NULL, "Failed to create portfolio");

  SamtraderPosition pos = {
      .code = "AAPL", .exchange = "US", .quantity = 100, .entry_price = 150.0, .entry_date = 0};

  /* Add position with NULL params */
  ASSERT(!samtrader_portfolio_add_position(NULL, arena, &pos), "Add with NULL portfolio");
  ASSERT(!samtrader_portfolio_add_position(portfolio, NULL, &pos), "Add with NULL arena");
  ASSERT(!samtrader_portfolio_add_position(portfolio, arena, NULL), "Add with NULL position");

  /* Get/has/remove with NULL params */
  ASSERT(samtrader_portfolio_get_position(NULL, "AAPL") == NULL, "Get with NULL portfolio");
  ASSERT(samtrader_portfolio_get_position(portfolio, NULL) == NULL, "Get with NULL code");
  ASSERT(!samtrader_portfolio_has_position(NULL, "AAPL"), "Has with NULL portfolio");
  ASSERT(!samtrader_portfolio_has_position(portfolio, NULL), "Has with NULL code");
  ASSERT(!samtrader_portfolio_remove_position(NULL, "AAPL"), "Remove with NULL portfolio");
  ASSERT(!samtrader_portfolio_remove_position(portfolio, NULL), "Remove with NULL code");

  /* Position count with NULL */
  ASSERT(samtrader_portfolio_position_count(NULL) == 0, "Count with NULL portfolio");

  /* Record trade with NULL params */
  SamtraderClosedTrade trade = {0};
  ASSERT(!samtrader_portfolio_record_trade(NULL, arena, &trade), "Record trade NULL portfolio");
  ASSERT(!samtrader_portfolio_record_trade(portfolio, NULL, &trade), "Record trade NULL arena");
  ASSERT(!samtrader_portfolio_record_trade(portfolio, arena, NULL), "Record trade NULL trade");

  /* Record equity with NULL params */
  ASSERT(!samtrader_portfolio_record_equity(NULL, arena, 0, 100000.0), "Record eq NULL portfolio");
  ASSERT(!samtrader_portfolio_record_equity(portfolio, NULL, 0, 100000.0), "Record eq NULL arena");

  /* Total equity with NULL params */
  SamHashMap *price_map = samhashmap_create(16, arena);
  ASSERT_DOUBLE_EQ(samtrader_portfolio_total_equity(NULL, price_map), -1.0,
                   "Equity NULL portfolio");
  ASSERT_DOUBLE_EQ(samtrader_portfolio_total_equity(portfolio, NULL), -1.0,
                   "Equity NULL price map");

  samrena_destroy(arena);
  printf("  PASS\n");
  return 0;
}

int main(void) {
  printf("=== Portfolio Tests ===\n\n");

  int failures = 0;

  failures += test_portfolio_create();
  failures += test_portfolio_add_position();
  failures += test_portfolio_remove_position();
  failures += test_portfolio_has_position();
  failures += test_portfolio_position_count();
  failures += test_portfolio_record_trade();
  failures += test_portfolio_record_equity();
  failures += test_portfolio_total_equity();
  failures += test_portfolio_null_params();

  printf("\n=== Results: %d failures ===\n", failures);

  return failures > 0 ? EXIT_FAILURE : EXIT_SUCCESS;
}
