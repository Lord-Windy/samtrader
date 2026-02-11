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

#include "samtrader/domain/code_data.h"
#include "samtrader/domain/execution.h"
#include "samtrader/domain/indicator.h"
#include "samtrader/domain/metrics.h"
#include "samtrader/domain/ohlcv.h"
#include "samtrader/domain/portfolio.h"
#include "samtrader/domain/rule.h"
#include "samtrader/domain/strategy.h"
#include "samtrader/domain/universe.h"

#define ASSERT(cond, msg)                                                                          \
  do {                                                                                             \
    if (!(cond)) {                                                                                 \
      printf("FAIL: %s\n", msg);                                                                   \
      return 1;                                                                                    \
    }                                                                                              \
  } while (0)

#define ASSERT_DOUBLE_EQ(a, b, msg)                                                                \
  do {                                                                                             \
    if (fabs((a) - (b)) > 0.01) {                                                                  \
      printf("FAIL: %s (expected %f, got %f)\n", msg, (b), (a));                                   \
      return 1;                                                                                    \
    }                                                                                              \
  } while (0)

/*============================================================================
 * Helpers
 *============================================================================*/

static time_t day_time(int day) {
  return (time_t)(1704067200 + day * 86400); /* 2024-01-01 + day offset */
}

static SamrenaVector *make_ohlcv(Samrena *arena, const double *closes, size_t count) {
  SamrenaVector *vec = samrena_vector_init(arena, sizeof(SamtraderOhlcv), count);
  for (size_t i = 0; i < count; i++) {
    SamtraderOhlcv bar = {
        .code = "TEST",
        .exchange = "US",
        .date = day_time((int)i),
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

static SamHashMap *build_price_map(Samrena *arena, const SamtraderOhlcv *bar) {
  SamHashMap *price_map = samhashmap_create(4, arena);
  if (!price_map)
    return NULL;
  double *price = SAMRENA_PUSH_TYPE(arena, double);
  if (!price)
    return NULL;
  *price = bar->close;
  samhashmap_put(price_map, bar->code, price);
  return price_map;
}

/**
 * Run the core backtest loop (replicating main.c logic) with an invariant check
 * at each bar: cash + |qty| * close == total_equity().
 */
static int run_backtest_loop(Samrena *arena, SamrenaVector *ohlcv, SamtraderStrategy *strategy,
                             SamtraderPortfolio *portfolio, SamHashMap *indicators,
                             const char *code, const char *exchange, double commission_flat,
                             double commission_pct, double slippage_pct, bool allow_shorting) {
  size_t bar_count = samrena_vector_size(ohlcv);

  for (size_t i = 0; i < bar_count; i++) {
    const SamtraderOhlcv *bar = (const SamtraderOhlcv *)samrena_vector_at_const(ohlcv, i);

    SamHashMap *price_map = build_price_map(arena, bar);
    if (!price_map)
      continue;

    /* Check stop loss / take profit triggers */
    samtrader_execution_check_triggers(portfolio, arena, price_map, bar->date, commission_flat,
                                       commission_pct, slippage_pct);

    /* Evaluate exit rules for existing positions */
    if (samtrader_portfolio_has_position(portfolio, code)) {
      SamtraderPosition *pos = samtrader_portfolio_get_position(portfolio, code);
      bool should_exit = false;
      if (pos && samtrader_position_is_long(pos)) {
        should_exit = samtrader_rule_evaluate(strategy->exit_long, ohlcv, indicators, i);
      } else if (pos && samtrader_position_is_short(pos) && strategy->exit_short) {
        should_exit = samtrader_rule_evaluate(strategy->exit_short, ohlcv, indicators, i);
      }
      if (should_exit) {
        samtrader_execution_exit_position(portfolio, arena, code, bar->close, bar->date,
                                          commission_flat, commission_pct, slippage_pct);
      }
    }

    /* Evaluate entry rules */
    if (!samtrader_portfolio_has_position(portfolio, code)) {
      bool enter_long = samtrader_rule_evaluate(strategy->entry_long, ohlcv, indicators, i);
      bool enter_short = allow_shorting && strategy->entry_short
                             ? samtrader_rule_evaluate(strategy->entry_short, ohlcv, indicators, i)
                             : false;

      if (enter_long) {
        samtrader_execution_enter_long(portfolio, arena, code, exchange, bar->close, bar->date,
                                       strategy->position_size, strategy->stop_loss_pct,
                                       strategy->take_profit_pct, strategy->max_positions,
                                       commission_flat, commission_pct, slippage_pct);
      } else if (enter_short) {
        samtrader_execution_enter_short(portfolio, arena, code, exchange, bar->close, bar->date,
                                        strategy->position_size, strategy->stop_loss_pct,
                                        strategy->take_profit_pct, strategy->max_positions,
                                        commission_flat, commission_pct, slippage_pct);
      }
    }

    /* Record equity */
    double equity = samtrader_portfolio_total_equity(portfolio, price_map);
    samtrader_portfolio_record_equity(portfolio, arena, bar->date, equity);

    /* Portfolio invariant check: cash + |qty| * close == total_equity */
    double manual_equity = portfolio->cash;
    if (samtrader_portfolio_has_position(portfolio, code)) {
      SamtraderPosition *pos = samtrader_portfolio_get_position(portfolio, code);
      if (pos) {
        int64_t abs_qty = pos->quantity >= 0 ? pos->quantity : -pos->quantity;
        manual_equity += (double)abs_qty * bar->close;
      }
    }
    if (fabs(manual_equity - equity) > 0.01) {
      printf("INVARIANT FAIL at bar %zu: manual=%f, total_equity=%f\n", i, manual_equity, equity);
      return 1;
    }
  }

  return 0;
}

/*============================================================================
 * Test 1: Simple Long Backtest
 *============================================================================*/

static int test_simple_long_backtest(void) {
  printf("Testing simple long backtest...\n");
  Samrena *arena = samrena_create_default();
  ASSERT(arena != NULL, "Failed to create arena");

  /* Prices: rising then plateau then falling */
  double closes[] = {90, 95, 100, 105, 110, 115, 120, 115, 110, 105};
  size_t count = sizeof(closes) / sizeof(closes[0]);
  SamrenaVector *ohlcv = make_ohlcv(arena, closes, count);

  /* Entry: close > 95, Exit: close > 115, position_size=0.5, no stops/commission */
  SamtraderOperand close_op = samtrader_operand_price(SAMTRADER_OPERAND_PRICE_CLOSE);
  SamtraderOperand const_95 = samtrader_operand_constant(95.0);
  SamtraderOperand const_115 = samtrader_operand_constant(115.0);

  SamtraderRule *entry_rule =
      samtrader_rule_create_comparison(arena, SAMTRADER_RULE_ABOVE, close_op, const_95);
  SamtraderRule *exit_rule =
      samtrader_rule_create_comparison(arena, SAMTRADER_RULE_ABOVE, close_op, const_115);

  SamtraderStrategy strategy = {.name = "test",
                                .entry_long = entry_rule,
                                .exit_long = exit_rule,
                                .entry_short = NULL,
                                .exit_short = NULL,
                                .position_size = 0.5,
                                .stop_loss_pct = 0.0,
                                .take_profit_pct = 0.0,
                                .max_positions = 10};

  SamtraderPortfolio *portfolio = samtrader_portfolio_create(arena, 100000.0);
  ASSERT(portfolio != NULL, "Failed to create portfolio");

  SamHashMap *indicators = samhashmap_create(4, arena);

  int loop_result = run_backtest_loop(arena, ohlcv, &strategy, portfolio, indicators, "TEST", "US",
                                      0.0, 0.0, 0.0, false);
  ASSERT(loop_result == 0, "Backtest loop invariant failed");

  /* Trace through the loop:
   * Bar 0: close=90, entry rule: 90 > 95? no  → no position
   * Bar 1: close=95, entry rule: 95 > 95? no (ABOVE is strict) → no position
   * Bar 2: close=100, entry rule: 100 > 95? yes → enter long
   *   available = 100000 * 0.5 = 50000, qty = floor(50000/100) = 500
   *   cash = 100000 - 50000 = 50000
   * Bar 3: close=105, exit rule: 105 > 115? no → hold
   * Bar 4: close=110, exit rule: 110 > 115? no → hold
   * Bar 5: close=115, exit rule: 115 > 115? no → hold
   * Bar 6: close=120, exit rule: 120 > 115? yes → exit
   *   cash = 50000 + 500*120 = 110000, PnL = 500*(120-100) = 10000
   *   entry rule: 120 > 95? yes → re-enter
   *   available = 110000 * 0.5 = 55000, qty = floor(55000/120) = 458
   *   cash = 110000 - 458*120 = 110000 - 54960 = 55040
   * Bar 7: close=115, exit rule: 115 > 115? no → hold
   * Bar 8: close=110, exit rule: 110 > 115? no → hold
   * Bar 9: close=105, exit rule: 105 > 115? no → hold (still open)
   */

  /* 1 closed trade with PnL = 10000 */
  ASSERT(samrena_vector_size(portfolio->closed_trades) == 1, "Should have 1 closed trade");
  const SamtraderClosedTrade *trade =
      (const SamtraderClosedTrade *)samrena_vector_at_const(portfolio->closed_trades, 0);
  ASSERT(trade != NULL, "Trade should not be NULL");
  ASSERT(trade->quantity == 500, "First trade: 500 shares");
  ASSERT_DOUBLE_EQ(trade->entry_price, 100.0, "First trade entry at 100");
  ASSERT_DOUBLE_EQ(trade->exit_price, 120.0, "First trade exit at 120");
  ASSERT_DOUBLE_EQ(trade->pnl, 10000.0, "First trade PnL");

  /* Second position still open */
  ASSERT(samtrader_portfolio_has_position(portfolio, "TEST"), "Should have open position");
  SamtraderPosition *pos = samtrader_portfolio_get_position(portfolio, "TEST");
  ASSERT(pos != NULL, "Open position should not be NULL");
  ASSERT(pos->quantity == 458, "Second position: 458 shares");
  ASSERT_DOUBLE_EQ(pos->entry_price, 120.0, "Second position entry at 120");

  /* Equity curve has 10 points */
  ASSERT(samrena_vector_size(portfolio->equity_curve) == 10, "Equity curve: 10 points");

  samrena_destroy(arena);
  printf("  PASS\n");
  return 0;
}

/*============================================================================
 * Test 2: Stop Loss Trigger
 *============================================================================*/

static int test_stop_loss_trigger(void) {
  printf("Testing stop loss trigger...\n");
  Samrena *arena = samrena_create_default();
  ASSERT(arena != NULL, "Failed to create arena");

  double closes[] = {90, 100, 110, 105, 100, 92, 88, 85};
  size_t count = sizeof(closes) / sizeof(closes[0]);
  SamrenaVector *ohlcv = make_ohlcv(arena, closes, count);

  /* Entry: close > 95, Exit: never fires (close > 999), stop_loss=10% */
  SamtraderOperand close_op = samtrader_operand_price(SAMTRADER_OPERAND_PRICE_CLOSE);
  SamtraderOperand const_95 = samtrader_operand_constant(95.0);
  SamtraderOperand const_999 = samtrader_operand_constant(999.0);

  SamtraderRule *entry_rule =
      samtrader_rule_create_comparison(arena, SAMTRADER_RULE_ABOVE, close_op, const_95);
  SamtraderRule *exit_rule =
      samtrader_rule_create_comparison(arena, SAMTRADER_RULE_ABOVE, close_op, const_999);

  SamtraderStrategy strategy = {.name = "test_sl",
                                .entry_long = entry_rule,
                                .exit_long = exit_rule,
                                .entry_short = NULL,
                                .exit_short = NULL,
                                .position_size = 0.5,
                                .stop_loss_pct = 10.0,
                                .take_profit_pct = 0.0,
                                .max_positions = 10};

  SamtraderPortfolio *portfolio = samtrader_portfolio_create(arena, 100000.0);
  ASSERT(portfolio != NULL, "Failed to create portfolio");

  SamHashMap *indicators = samhashmap_create(4, arena);

  int loop_result = run_backtest_loop(arena, ohlcv, &strategy, portfolio, indicators, "TEST", "US",
                                      0.0, 0.0, 0.0, false);
  ASSERT(loop_result == 0, "Backtest loop invariant failed");

  /* Trace:
   * Bar 0: close=90, 90 > 95? no
   * Bar 1: close=100, 100 > 95? yes → enter long
   *   qty = floor(50000/100) = 500, cash = 50000
   *   stop_loss = 100 * (1 - 10/100) = 90.0
   * Bar 2: close=110, SL check: 110 <= 90? no. Exit rule: 110 > 999? no
   * Bar 3: close=105, SL check: 105 <= 90? no
   * Bar 4: close=100, SL check: 100 <= 90? no
   * Bar 5: close=92,  SL check: 92 <= 90? no
   * Bar 6: close=88,  SL check: 88 <= 90? YES → trigger exit at 88
   *   cash = 50000 + 500*88 = 94000
   *   PnL = 500*(88-100) = -6000
   *   entry rule: 88 > 95? no → no re-entry
   * Bar 7: close=85, no position, 85 > 95? no
   */

  ASSERT(samrena_vector_size(portfolio->closed_trades) == 1, "Should have 1 closed trade");
  const SamtraderClosedTrade *trade =
      (const SamtraderClosedTrade *)samrena_vector_at_const(portfolio->closed_trades, 0);
  ASSERT(trade != NULL, "Trade should not be NULL");
  ASSERT(trade->quantity == 500, "SL trade: 500 shares");
  ASSERT_DOUBLE_EQ(trade->entry_price, 100.0, "SL trade entry at 100");
  ASSERT_DOUBLE_EQ(trade->exit_price, 88.0, "SL trade exit at 88 (trigger price)");
  ASSERT_DOUBLE_EQ(trade->pnl, -6000.0, "SL trade PnL = -6000");

  ASSERT(!samtrader_portfolio_has_position(portfolio, "TEST"), "Position should be closed");

  samrena_destroy(arena);
  printf("  PASS\n");
  return 0;
}

/*============================================================================
 * Test 3: Multiple Trades
 *============================================================================*/

static int test_multiple_trades(void) {
  printf("Testing multiple trades...\n");
  Samrena *arena = samrena_create_default();
  ASSERT(arena != NULL, "Failed to create arena");

  double closes[] = {90, 100, 110, 120, 130, 125, 115, 110, 100, 105, 110, 120, 130, 125, 115};
  size_t count = sizeof(closes) / sizeof(closes[0]);
  SamrenaVector *ohlcv = make_ohlcv(arena, closes, count);

  /* Entry: close > 105, Exit: close > 125 */
  SamtraderOperand close_op = samtrader_operand_price(SAMTRADER_OPERAND_PRICE_CLOSE);
  SamtraderOperand const_105 = samtrader_operand_constant(105.0);
  SamtraderOperand const_125 = samtrader_operand_constant(125.0);

  SamtraderRule *entry_rule =
      samtrader_rule_create_comparison(arena, SAMTRADER_RULE_ABOVE, close_op, const_105);
  SamtraderRule *exit_rule =
      samtrader_rule_create_comparison(arena, SAMTRADER_RULE_ABOVE, close_op, const_125);

  SamtraderStrategy strategy = {.name = "test_multi",
                                .entry_long = entry_rule,
                                .exit_long = exit_rule,
                                .entry_short = NULL,
                                .exit_short = NULL,
                                .position_size = 0.5,
                                .stop_loss_pct = 0.0,
                                .take_profit_pct = 0.0,
                                .max_positions = 10};

  SamtraderPortfolio *portfolio = samtrader_portfolio_create(arena, 100000.0);
  ASSERT(portfolio != NULL, "Failed to create portfolio");

  SamHashMap *indicators = samhashmap_create(4, arena);

  int loop_result = run_backtest_loop(arena, ohlcv, &strategy, portfolio, indicators, "TEST", "US",
                                      0.0, 0.0, 0.0, false);
  ASSERT(loop_result == 0, "Backtest loop invariant failed");

  /* Trace:
   * Bar 0: close=90, 90 > 105? no
   * Bar 1: close=100, 100 > 105? no
   * Bar 2: close=110, 110 > 105? yes → enter long
   *   qty = floor(50000/110) = 454, cash = 100000 - 454*110 = 100000 - 49940 = 50060
   * Bar 3: close=120, 120 > 125? no
   * Bar 4: close=130, 130 > 125? yes → exit
   *   cash = 50060 + 454*130 = 50060 + 59020 = 109080
   *   PnL = 454*(130-110) = 9080
   *   entry: 130 > 105? yes → re-enter
   *   qty = floor(54540/130) = 419, cash = 109080 - 419*130 = 109080 - 54470 = 54610
   * Bar 5: close=125, 125 > 125? no
   * Bar 6: close=115, no
   * Bar 7: close=110, no
   * Bar 8: close=100, no
   * Bar 9: close=105, no
   * Bar 10: close=110, no
   * Bar 11: close=120, no
   * Bar 12: close=130, 130 > 125? yes → exit
   *   cash = 54610 + 419*130 = 54610 + 54470 = 109080
   *   PnL = 419*(130-130) = 0
   *   entry: 130 > 105? yes → re-enter
   *   qty = floor(54540/130) = 419, cash = 109080 - 419*130 = 54610
   * Bar 13: close=125, 125 > 125? no
   * Bar 14: close=115, no → still holding
   */

  /* 2 closed trades */
  ASSERT(samrena_vector_size(portfolio->closed_trades) == 2, "Should have 2 closed trades");

  /* First trade: entry 110, exit 130, PnL = 454 * 20 = 9080 */
  const SamtraderClosedTrade *t1 =
      (const SamtraderClosedTrade *)samrena_vector_at_const(portfolio->closed_trades, 0);
  ASSERT(t1 != NULL, "Trade 1 should not be NULL");
  ASSERT_DOUBLE_EQ(t1->entry_price, 110.0, "Trade 1 entry");
  ASSERT_DOUBLE_EQ(t1->exit_price, 130.0, "Trade 1 exit");
  ASSERT_DOUBLE_EQ(t1->pnl, 9080.0, "Trade 1 PnL");

  /* Second trade: entry 130, exit 130, PnL = 0 */
  const SamtraderClosedTrade *t2 =
      (const SamtraderClosedTrade *)samrena_vector_at_const(portfolio->closed_trades, 1);
  ASSERT(t2 != NULL, "Trade 2 should not be NULL");
  ASSERT_DOUBLE_EQ(t2->entry_price, 130.0, "Trade 2 entry");
  ASSERT_DOUBLE_EQ(t2->exit_price, 130.0, "Trade 2 exit");
  ASSERT_DOUBLE_EQ(t2->pnl, 0.0, "Trade 2 PnL");

  /* Compute metrics */
  SamtraderMetrics *metrics =
      samtrader_metrics_calculate(arena, portfolio->closed_trades, portfolio->equity_curve, 0.0);
  ASSERT(metrics != NULL, "Metrics should not be NULL");
  ASSERT(metrics->total_trades == 2, "2 total trades");
  ASSERT(metrics->winning_trades == 1, "1 winning trade (PnL > 0)");
  ASSERT(metrics->losing_trades == 1, "1 losing trade (PnL = 0 counts as loss)");
  ASSERT_DOUBLE_EQ(metrics->win_rate, 0.5, "50% win rate");

  samrena_destroy(arena);
  printf("  PASS\n");
  return 0;
}

/*============================================================================
 * Test 4: SMA Strategy (Indicator Pipeline Integration)
 *============================================================================*/

static int test_sma_strategy(void) {
  printf("Testing SMA strategy...\n");
  Samrena *arena = samrena_create_default();
  ASSERT(arena != NULL, "Failed to create arena");

  double closes[] = {100, 102, 104, 103, 101, 99, 97, 98, 100, 103};
  size_t count = sizeof(closes) / sizeof(closes[0]);
  SamrenaVector *ohlcv = make_ohlcv(arena, closes, count);

  /* Calculate SMA(3) from the OHLCV data */
  SamtraderIndicatorSeries *sma3 =
      samtrader_indicator_calculate(arena, SAMTRADER_IND_SMA, ohlcv, 3);
  ASSERT(sma3 != NULL, "SMA(3) calculation failed");
  ASSERT(samtrader_indicator_series_size(sma3) == count, "SMA series should have same length");

  /* Build indicators hashmap */
  SamHashMap *indicators = samhashmap_create(4, arena);
  SamtraderOperand sma_operand = samtrader_operand_indicator(SAMTRADER_IND_SMA, 3);
  char key[64];
  samtrader_operand_indicator_key(key, sizeof(key), &sma_operand);
  samhashmap_put(indicators, key, sma3);

  /* Entry: close > SMA(3), Exit: close < SMA(3) */
  SamtraderOperand close_op = samtrader_operand_price(SAMTRADER_OPERAND_PRICE_CLOSE);

  SamtraderRule *entry_rule =
      samtrader_rule_create_comparison(arena, SAMTRADER_RULE_ABOVE, close_op, sma_operand);
  SamtraderRule *exit_rule =
      samtrader_rule_create_comparison(arena, SAMTRADER_RULE_BELOW, close_op, sma_operand);

  SamtraderStrategy strategy = {.name = "test_sma",
                                .entry_long = entry_rule,
                                .exit_long = exit_rule,
                                .entry_short = NULL,
                                .exit_short = NULL,
                                .position_size = 0.5,
                                .stop_loss_pct = 0.0,
                                .take_profit_pct = 0.0,
                                .max_positions = 10};

  SamtraderPortfolio *portfolio = samtrader_portfolio_create(arena, 100000.0);
  ASSERT(portfolio != NULL, "Failed to create portfolio");

  int loop_result = run_backtest_loop(arena, ohlcv, &strategy, portfolio, indicators, "TEST", "US",
                                      0.0, 0.0, 0.0, false);
  ASSERT(loop_result == 0, "Backtest loop invariant failed");

  /* SMA(3) values:
   * Bar 0: invalid (warmup)
   * Bar 1: invalid (warmup)
   * Bar 2: (100+102+104)/3 = 102.0, valid. close=104 > 102? yes → enter
   *   qty = floor(50000/104) = 480, cash = 100000 - 480*104 = 100000 - 49920 = 50080
   * Bar 3: SMA=(102+104+103)/3=103.0, close=103 < 103? no (not strictly below)
   * Bar 4: SMA=(104+103+101)/3=102.67, close=101 < 102.67? yes → exit
   *   cash = 50080 + 480*101 = 50080 + 48480 = 98560
   *   PnL = 480*(101-104) = -1440
   *   entry: 101 < 102.67, so 101 > 102.67? no → no re-entry
   * Bar 5: SMA=(103+101+99)/3=101.0, close=99 < 101? below. No pos to exit.
   *   entry: 99 > 101? no
   * Bar 6: SMA=(101+99+97)/3=99.0, close=97 < 99? no position.
   *   entry: 97 > 99? no
   * Bar 7: SMA=(99+97+98)/3=98.0, close=98 > 98? no (not strictly above)
   * Bar 8: SMA=(97+98+100)/3=98.33, close=100 > 98.33? yes → enter
   *   qty = floor(49280/100) = 492, cash = 98560 - 492*100 = 98560 - 49200 = 49360
   * Bar 9: SMA=(98+100+103)/3=100.33, close=103 > 100.33 → not exit (not below).
   *   Still holding.
   */

  ASSERT(samrena_vector_size(portfolio->closed_trades) == 1, "Should have 1 closed trade");
  const SamtraderClosedTrade *trade =
      (const SamtraderClosedTrade *)samrena_vector_at_const(portfolio->closed_trades, 0);
  ASSERT(trade != NULL, "Trade should not be NULL");
  ASSERT(trade->quantity == 480, "SMA trade: 480 shares");
  ASSERT_DOUBLE_EQ(trade->entry_price, 104.0, "SMA trade entry at 104");
  ASSERT_DOUBLE_EQ(trade->exit_price, 101.0, "SMA trade exit at 101");
  ASSERT_DOUBLE_EQ(trade->pnl, -1440.0, "SMA trade PnL = -1440");

  /* Second position still open */
  ASSERT(samtrader_portfolio_has_position(portfolio, "TEST"), "Should have open position");
  SamtraderPosition *pos = samtrader_portfolio_get_position(portfolio, "TEST");
  ASSERT(pos != NULL, "Open position should not be NULL");
  ASSERT(pos->quantity == 492, "Second position: 492 shares");
  ASSERT_DOUBLE_EQ(pos->entry_price, 100.0, "Second position entry at 100");

  /* Equity curve has 10 points */
  ASSERT(samrena_vector_size(portfolio->equity_curve) == 10, "Equity curve: 10 points");

  samrena_destroy(arena);
  printf("  PASS\n");
  return 0;
}

/*============================================================================
 * Test 5: Portfolio Invariant Stress Test
 *============================================================================*/

static int test_portfolio_invariant_stress(void) {
  printf("Testing portfolio invariant under volatile prices...\n");
  Samrena *arena = samrena_create_default();
  ASSERT(arena != NULL, "Failed to create arena");

  /* 20 bars of volatile sawtooth prices:
   * closes: 100, 98, 105, 96, 110, 94, 115, 92, 120, 90, 125, 88, 130, 86, 135, 84, 140, 82,
   *         145, 80 */
  double closes[20];
  for (int i = 0; i < 20; i++) {
    double base = 100.0;
    if (i % 2 == 0) {
      closes[i] = base + 5.0 * (double)(i / 2);
    } else {
      closes[i] = base - 2.0 * (double)((i + 1) / 2);
    }
  }

  size_t count = 20;
  SamrenaVector *ohlcv = make_ohlcv(arena, closes, count);

  /* Entry: close > 50 (always true after bar 0), Exit: close > 999 (never fires) */
  SamtraderOperand close_op = samtrader_operand_price(SAMTRADER_OPERAND_PRICE_CLOSE);
  SamtraderOperand const_50 = samtrader_operand_constant(50.0);
  SamtraderOperand const_999 = samtrader_operand_constant(999.0);

  SamtraderRule *entry_rule =
      samtrader_rule_create_comparison(arena, SAMTRADER_RULE_ABOVE, close_op, const_50);
  SamtraderRule *exit_rule =
      samtrader_rule_create_comparison(arena, SAMTRADER_RULE_ABOVE, close_op, const_999);

  SamtraderStrategy strategy = {.name = "test_invariant",
                                .entry_long = entry_rule,
                                .exit_long = exit_rule,
                                .entry_short = NULL,
                                .exit_short = NULL,
                                .position_size = 0.5,
                                .stop_loss_pct = 0.0,
                                .take_profit_pct = 0.0,
                                .max_positions = 10};

  SamtraderPortfolio *portfolio = samtrader_portfolio_create(arena, 100000.0);
  ASSERT(portfolio != NULL, "Failed to create portfolio");

  SamHashMap *indicators = samhashmap_create(4, arena);

  /* The main assertion: invariant check passes at every bar */
  int loop_result = run_backtest_loop(arena, ohlcv, &strategy, portfolio, indicators, "TEST", "US",
                                      0.0, 0.0, 0.0, false);
  ASSERT(loop_result == 0, "Portfolio invariant failed during stress test");

  /* Position should be held the entire time (exit never fires) */
  ASSERT(samtrader_portfolio_has_position(portfolio, "TEST"), "Should still have position");
  ASSERT(samrena_vector_size(portfolio->closed_trades) == 0, "No closed trades");
  ASSERT(samrena_vector_size(portfolio->equity_curve) == 20, "Equity curve: 20 points");

  samrena_destroy(arena);
  printf("  PASS\n");
  return 0;
}

/*============================================================================
 * Multi-Code Helpers
 *============================================================================*/

#define DATE_KEY_BUF_SIZE 32

static SamrenaVector *make_ohlcv_for_code(Samrena *arena, const char *code, const char *exchange,
                                          const double *closes, size_t count, int day_offset) {
  SamrenaVector *vec = samrena_vector_init(arena, sizeof(SamtraderOhlcv), count);
  for (size_t i = 0; i < count; i++) {
    SamtraderOhlcv bar = {
        .code = code,
        .exchange = exchange,
        .date = day_time(day_offset + (int)i),
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

static SamtraderCodeData *make_code_data(Samrena *arena, const char *code, const char *exchange,
                                         const double *closes, size_t count, int day_offset) {
  SamtraderCodeData *cd = SAMRENA_PUSH_TYPE_ZERO(arena, SamtraderCodeData);
  if (!cd)
    return NULL;
  cd->code = code;
  cd->exchange = exchange;
  cd->ohlcv = make_ohlcv_for_code(arena, code, exchange, closes, count, day_offset);
  cd->bar_count = count;
  cd->indicators = samhashmap_create(4, arena);
  return cd;
}

/**
 * Run the multi-code backtest loop (replicating the new main.c logic).
 */
static int run_multicode_backtest_loop(Samrena *arena, SamtraderCodeData **code_data_arr,
                                       size_t code_count, SamHashMap **date_indices,
                                       SamrenaVector *timeline, SamtraderStrategy *strategy,
                                       SamtraderPortfolio *portfolio, const char *exchange,
                                       double commission_flat, double commission_pct,
                                       double slippage_pct, bool allow_shorting) {
  for (size_t t = 0; t < samrena_vector_size(timeline); t++) {
    time_t date = *(const time_t *)samrena_vector_at_const(timeline, t);

    SamHashMap *price_map = samhashmap_create(code_count * 2, arena);
    if (!price_map)
      continue;

    char date_key[DATE_KEY_BUF_SIZE];
    snprintf(date_key, sizeof(date_key), "%ld", (long)date);

    for (size_t c = 0; c < code_count; c++) {
      size_t *bar_idx = (size_t *)samhashmap_get(date_indices[c], date_key);
      if (!bar_idx)
        continue;
      const SamtraderOhlcv *bar =
          (const SamtraderOhlcv *)samrena_vector_at_const(code_data_arr[c]->ohlcv, *bar_idx);
      double *price = SAMRENA_PUSH_TYPE(arena, double);
      if (!price)
        continue;
      *price = bar->close;
      samhashmap_put(price_map, code_data_arr[c]->code, price);
    }

    samtrader_execution_check_triggers(portfolio, arena, price_map, date, commission_flat,
                                       commission_pct, slippage_pct);

    for (size_t c = 0; c < code_count; c++) {
      size_t *bar_idx = (size_t *)samhashmap_get(date_indices[c], date_key);
      if (!bar_idx)
        continue;

      const SamtraderOhlcv *bar =
          (const SamtraderOhlcv *)samrena_vector_at_const(code_data_arr[c]->ohlcv, *bar_idx);
      const char *code = code_data_arr[c]->code;

      if (samtrader_portfolio_has_position(portfolio, code)) {
        SamtraderPosition *pos = samtrader_portfolio_get_position(portfolio, code);
        bool should_exit = false;
        if (pos && samtrader_position_is_long(pos)) {
          should_exit = samtrader_rule_evaluate(strategy->exit_long, code_data_arr[c]->ohlcv,
                                                code_data_arr[c]->indicators, *bar_idx);
        } else if (pos && samtrader_position_is_short(pos) && strategy->exit_short) {
          should_exit = samtrader_rule_evaluate(strategy->exit_short, code_data_arr[c]->ohlcv,
                                                code_data_arr[c]->indicators, *bar_idx);
        }
        if (should_exit) {
          samtrader_execution_exit_position(portfolio, arena, code, bar->close, date,
                                            commission_flat, commission_pct, slippage_pct);
        }
      }

      if (!samtrader_portfolio_has_position(portfolio, code)) {
        bool enter_long = samtrader_rule_evaluate(strategy->entry_long, code_data_arr[c]->ohlcv,
                                                  code_data_arr[c]->indicators, *bar_idx);
        bool enter_short =
            allow_shorting && strategy->entry_short
                ? samtrader_rule_evaluate(strategy->entry_short, code_data_arr[c]->ohlcv,
                                          code_data_arr[c]->indicators, *bar_idx)
                : false;

        if (enter_long) {
          samtrader_execution_enter_long(portfolio, arena, code, exchange, bar->close, date,
                                         strategy->position_size, strategy->stop_loss_pct,
                                         strategy->take_profit_pct, strategy->max_positions,
                                         commission_flat, commission_pct, slippage_pct);
        } else if (enter_short) {
          samtrader_execution_enter_short(portfolio, arena, code, exchange, bar->close, date,
                                          strategy->position_size, strategy->stop_loss_pct,
                                          strategy->take_profit_pct, strategy->max_positions,
                                          commission_flat, commission_pct, slippage_pct);
        }
      }
    }

    double equity = samtrader_portfolio_total_equity(portfolio, price_map);
    samtrader_portfolio_record_equity(portfolio, arena, date, equity);
  }

  return 0;
}

/*============================================================================
 * Test 6: Two Codes Both Trigger Entry
 *============================================================================*/

static int test_multicode_both_enter(void) {
  printf("Testing multi-code: two codes both trigger entry...\n");
  Samrena *arena = samrena_create_default();
  ASSERT(arena != NULL, "Failed to create arena");

  /* Code A: prices rising from 90 to 130 */
  double closes_a[] = {90, 100, 110, 120, 130};
  /* Code B: prices rising from 85 to 125 */
  double closes_b[] = {85, 95, 105, 115, 125};
  size_t count = 5;

  SamtraderCodeData *cd_a = make_code_data(arena, "CODEA", "US", closes_a, count, 0);
  SamtraderCodeData *cd_b = make_code_data(arena, "CODEB", "US", closes_b, count, 0);
  ASSERT(cd_a != NULL && cd_b != NULL, "Failed to create code data");

  SamtraderCodeData *code_data_arr[] = {cd_a, cd_b};
  SamHashMap *date_indices[2];
  date_indices[0] = samtrader_build_date_index(arena, cd_a->ohlcv);
  date_indices[1] = samtrader_build_date_index(arena, cd_b->ohlcv);
  ASSERT(date_indices[0] && date_indices[1], "Failed to build date indices");

  SamrenaVector *timeline = samtrader_build_date_timeline(arena, code_data_arr, 2);
  ASSERT(timeline != NULL, "Failed to build timeline");
  ASSERT(samrena_vector_size(timeline) == 5, "Timeline should have 5 dates");

  /* Entry: close > 95, Exit: close > 999 (never fires), max_positions=2 */
  SamtraderOperand close_op = samtrader_operand_price(SAMTRADER_OPERAND_PRICE_CLOSE);
  SamtraderOperand const_95 = samtrader_operand_constant(95.0);
  SamtraderOperand const_999 = samtrader_operand_constant(999.0);

  SamtraderRule *entry_rule =
      samtrader_rule_create_comparison(arena, SAMTRADER_RULE_ABOVE, close_op, const_95);
  SamtraderRule *exit_rule =
      samtrader_rule_create_comparison(arena, SAMTRADER_RULE_ABOVE, close_op, const_999);

  SamtraderStrategy strategy = {.name = "test_multicode",
                                .entry_long = entry_rule,
                                .exit_long = exit_rule,
                                .entry_short = NULL,
                                .exit_short = NULL,
                                .position_size = 0.25,
                                .stop_loss_pct = 0.0,
                                .take_profit_pct = 0.0,
                                .max_positions = 2};

  SamtraderPortfolio *portfolio = samtrader_portfolio_create(arena, 100000.0);
  ASSERT(portfolio != NULL, "Failed to create portfolio");

  int loop_result = run_multicode_backtest_loop(arena, code_data_arr, 2, date_indices, timeline,
                                                &strategy, portfolio, "US", 0.0, 0.0, 0.0, false);
  ASSERT(loop_result == 0, "Multi-code backtest loop failed");

  /* Trace:
   * Bar 0 (day 0): CODEA close=90 > 95? no. CODEB close=85 > 95? no.
   * Bar 1 (day 1): CODEA close=100 > 95? yes → enter CODEA
   *   available = 100000 * 0.25 = 25000, qty = floor(25000/100) = 250
   *   cash = 100000 - 25000 = 75000
   *   CODEB close=95 > 95? no (strict above)
   * Bar 2 (day 2): CODEA close=110, hold. CODEB close=105 > 95? yes → enter CODEB
   *   available = 75000 * 0.25 = 18750, qty = floor(18750/105) = 178
   *   cash = 75000 - 178*105 = 75000 - 18690 = 56310
   * Bar 3 (day 3): Both hold.
   * Bar 4 (day 4): Both hold.
   */

  ASSERT(samtrader_portfolio_has_position(portfolio, "CODEA"), "Should have CODEA position");
  ASSERT(samtrader_portfolio_has_position(portfolio, "CODEB"), "Should have CODEB position");
  ASSERT(samtrader_portfolio_position_count(portfolio) == 2, "Should have 2 open positions");

  SamtraderPosition *pos_a = samtrader_portfolio_get_position(portfolio, "CODEA");
  ASSERT(pos_a != NULL, "CODEA position should exist");
  ASSERT(pos_a->quantity == 250, "CODEA: 250 shares");
  ASSERT_DOUBLE_EQ(pos_a->entry_price, 100.0, "CODEA entry at 100");

  SamtraderPosition *pos_b = samtrader_portfolio_get_position(portfolio, "CODEB");
  ASSERT(pos_b != NULL, "CODEB position should exist");
  ASSERT(pos_b->quantity == 178, "CODEB: 178 shares");
  ASSERT_DOUBLE_EQ(pos_b->entry_price, 105.0, "CODEB entry at 105");

  ASSERT(samrena_vector_size(portfolio->equity_curve) == 5, "Equity curve: 5 points");

  /* Final equity: cash + CODEA(250*130) + CODEB(178*125) = 56310 + 32500 + 22250 = 111060 */
  const SamtraderEquityPoint *last_eq = (const SamtraderEquityPoint *)samrena_vector_at_const(
      portfolio->equity_curve, samrena_vector_size(portfolio->equity_curve) - 1);
  ASSERT_DOUBLE_EQ(last_eq->equity, 111060.0, "Final equity");

  samrena_destroy(arena);
  printf("  PASS\n");
  return 0;
}

/*============================================================================
 * Test 7: Max Positions Respected Globally
 *============================================================================*/

static int test_multicode_max_positions(void) {
  printf("Testing multi-code: max positions respected globally...\n");
  Samrena *arena = samrena_create_default();
  ASSERT(arena != NULL, "Failed to create arena");

  /* Both codes have same prices, both trigger at bar 1 */
  double closes[] = {90, 100, 110, 120, 130};
  size_t count = 5;

  SamtraderCodeData *cd_a = make_code_data(arena, "CODEA", "US", closes, count, 0);
  SamtraderCodeData *cd_b = make_code_data(arena, "CODEB", "US", closes, count, 0);
  ASSERT(cd_a != NULL && cd_b != NULL, "Failed to create code data");

  SamtraderCodeData *code_data_arr[] = {cd_a, cd_b};
  SamHashMap *date_indices[2];
  date_indices[0] = samtrader_build_date_index(arena, cd_a->ohlcv);
  date_indices[1] = samtrader_build_date_index(arena, cd_b->ohlcv);

  SamrenaVector *timeline = samtrader_build_date_timeline(arena, code_data_arr, 2);
  ASSERT(timeline != NULL, "Failed to build timeline");

  /* Entry: close > 95, Exit: never, max_positions=1 */
  SamtraderOperand close_op = samtrader_operand_price(SAMTRADER_OPERAND_PRICE_CLOSE);
  SamtraderOperand const_95 = samtrader_operand_constant(95.0);
  SamtraderOperand const_999 = samtrader_operand_constant(999.0);

  SamtraderRule *entry_rule =
      samtrader_rule_create_comparison(arena, SAMTRADER_RULE_ABOVE, close_op, const_95);
  SamtraderRule *exit_rule =
      samtrader_rule_create_comparison(arena, SAMTRADER_RULE_ABOVE, close_op, const_999);

  SamtraderStrategy strategy = {.name = "test_max_pos",
                                .entry_long = entry_rule,
                                .exit_long = exit_rule,
                                .entry_short = NULL,
                                .exit_short = NULL,
                                .position_size = 0.25,
                                .stop_loss_pct = 0.0,
                                .take_profit_pct = 0.0,
                                .max_positions = 1};

  SamtraderPortfolio *portfolio = samtrader_portfolio_create(arena, 100000.0);
  ASSERT(portfolio != NULL, "Failed to create portfolio");

  int loop_result = run_multicode_backtest_loop(arena, code_data_arr, 2, date_indices, timeline,
                                                &strategy, portfolio, "US", 0.0, 0.0, 0.0, false);
  ASSERT(loop_result == 0, "Multi-code backtest loop failed");

  /* Only one position should be opened (max_positions=1) */
  ASSERT(samtrader_portfolio_position_count(portfolio) == 1,
         "Should have exactly 1 open position (max_positions=1)");

  /* CODEA is processed first in the inner loop, so it gets the position */
  ASSERT(samtrader_portfolio_has_position(portfolio, "CODEA"), "CODEA should have position");
  ASSERT(!samtrader_portfolio_has_position(portfolio, "CODEB"), "CODEB should NOT have position");

  samrena_destroy(arena);
  printf("  PASS\n");
  return 0;
}

/*============================================================================
 * Test 8: Disjoint Date Ranges
 *============================================================================*/

static int test_multicode_disjoint_dates(void) {
  printf("Testing multi-code: disjoint date ranges...\n");
  Samrena *arena = samrena_create_default();
  ASSERT(arena != NULL, "Failed to create arena");

  /* CODEA: days 0-4, prices rising */
  double closes_a[] = {90, 100, 110, 120, 130};
  /* CODEB: days 3-7, prices also rising */
  double closes_b[] = {85, 95, 105, 115, 125};

  SamtraderCodeData *cd_a = make_code_data(arena, "CODEA", "US", closes_a, 5, 0);
  SamtraderCodeData *cd_b = make_code_data(arena, "CODEB", "US", closes_b, 5, 3);
  ASSERT(cd_a != NULL && cd_b != NULL, "Failed to create code data");

  SamtraderCodeData *code_data_arr[] = {cd_a, cd_b};
  SamHashMap *date_indices[2];
  date_indices[0] = samtrader_build_date_index(arena, cd_a->ohlcv);
  date_indices[1] = samtrader_build_date_index(arena, cd_b->ohlcv);

  SamrenaVector *timeline = samtrader_build_date_timeline(arena, code_data_arr, 2);
  ASSERT(timeline != NULL, "Failed to build timeline");
  /* Days 0,1,2,3,4,5,6,7 = 8 unique dates (days 3,4 overlap) */
  ASSERT(samrena_vector_size(timeline) == 8, "Timeline should have 8 dates");

  /* Entry: close > 95, Exit: close > 125, max_positions=2 */
  SamtraderOperand close_op = samtrader_operand_price(SAMTRADER_OPERAND_PRICE_CLOSE);
  SamtraderOperand const_95 = samtrader_operand_constant(95.0);
  SamtraderOperand const_125 = samtrader_operand_constant(125.0);

  SamtraderRule *entry_rule =
      samtrader_rule_create_comparison(arena, SAMTRADER_RULE_ABOVE, close_op, const_95);
  SamtraderRule *exit_rule =
      samtrader_rule_create_comparison(arena, SAMTRADER_RULE_ABOVE, close_op, const_125);

  SamtraderStrategy strategy = {.name = "test_disjoint",
                                .entry_long = entry_rule,
                                .exit_long = exit_rule,
                                .entry_short = NULL,
                                .exit_short = NULL,
                                .position_size = 0.25,
                                .stop_loss_pct = 0.0,
                                .take_profit_pct = 0.0,
                                .max_positions = 2};

  SamtraderPortfolio *portfolio = samtrader_portfolio_create(arena, 100000.0);
  ASSERT(portfolio != NULL, "Failed to create portfolio");

  int loop_result = run_multicode_backtest_loop(arena, code_data_arr, 2, date_indices, timeline,
                                                &strategy, portfolio, "US", 0.0, 0.0, 0.0, false);
  ASSERT(loop_result == 0, "Multi-code backtest loop failed");

  /* Trace:
   * Day 0: CODEA close=90 > 95? no. CODEB: no data.
   * Day 1: CODEA close=100 > 95? yes → enter CODEA
   *   qty = floor(25000/100) = 250, cash = 75000
   *   CODEB: no data.
   * Day 2: CODEA close=110, hold. CODEB: no data.
   * Day 3: CODEA close=120, hold. CODEB close=85 > 95? no.
   * Day 4: CODEA close=130 > 125? yes → exit CODEA
   *   cash = 75000 + 250*130 = 107500, PnL = 250*(130-100) = 7500
   *   CODEA re-enter: 130 > 95? yes → re-enter
   *   qty = floor(26875/130) = 206, cash = 107500 - 206*130 = 107500 - 26780 = 80720
   *   CODEB close=95 > 95? no.
   * Day 5: CODEA: no data. CODEB close=105 > 95? yes → enter CODEB
   *   qty = floor(20180/105) = 192, cash = 80720 - 192*105 = 80720 - 20160 = 60560
   * Day 6: CODEA: no data. CODEB close=115, hold.
   * Day 7: CODEA: no data. CODEB close=125 > 125? no. hold.
   */

  /* 1 closed trade (CODEA first exit) */
  ASSERT(samrena_vector_size(portfolio->closed_trades) == 1, "Should have 1 closed trade");
  const SamtraderClosedTrade *trade =
      (const SamtraderClosedTrade *)samrena_vector_at_const(portfolio->closed_trades, 0);
  ASSERT_DOUBLE_EQ(trade->entry_price, 100.0, "CODEA trade entry at 100");
  ASSERT_DOUBLE_EQ(trade->exit_price, 130.0, "CODEA trade exit at 130");
  ASSERT_DOUBLE_EQ(trade->pnl, 7500.0, "CODEA trade PnL = 7500");

  /* Two open positions: re-entered CODEA and CODEB */
  ASSERT(samtrader_portfolio_has_position(portfolio, "CODEA"), "CODEA should have re-entered");
  ASSERT(samtrader_portfolio_has_position(portfolio, "CODEB"), "CODEB should have entered");

  SamtraderPosition *pos_a = samtrader_portfolio_get_position(portfolio, "CODEA");
  ASSERT(pos_a != NULL, "CODEA position should exist");
  ASSERT(pos_a->quantity == 206, "CODEA re-entry: 206 shares");
  ASSERT_DOUBLE_EQ(pos_a->entry_price, 130.0, "CODEA re-entry at 130");

  SamtraderPosition *pos_b = samtrader_portfolio_get_position(portfolio, "CODEB");
  ASSERT(pos_b != NULL, "CODEB position should exist");
  ASSERT(pos_b->quantity == 192, "CODEB: 192 shares");
  ASSERT_DOUBLE_EQ(pos_b->entry_price, 105.0, "CODEB entry at 105");

  /* Equity curve should have 8 points (one per timeline date) */
  ASSERT(samrena_vector_size(portfolio->equity_curve) == 8, "Equity curve: 8 points");

  samrena_destroy(arena);
  printf("  PASS\n");
  return 0;
}

/*============================================================================
 * Test 9: Per-Code Metrics Computation
 *============================================================================*/

static int test_multicode_per_code_metrics(void) {
  printf("Testing multi-code: per-code metrics computation...\n");
  Samrena *arena = samrena_create_default();
  ASSERT(arena != NULL, "Failed to create arena");

  /* CODEA: rises from 90 to 130, then drops. Two entries/exits expected.
   * CODEB: rises from 85, one entry/exit expected. */
  double closes_a[] = {90, 100, 110, 120, 130, 115, 100, 110, 120, 130};
  double closes_b[] = {85, 95, 105, 115, 130, 120, 110, 100, 90, 80};
  size_t count = 10;

  SamtraderCodeData *cd_a = make_code_data(arena, "CODEA", "US", closes_a, count, 0);
  SamtraderCodeData *cd_b = make_code_data(arena, "CODEB", "US", closes_b, count, 0);
  ASSERT(cd_a != NULL && cd_b != NULL, "Failed to create code data");

  SamtraderCodeData *code_data_arr[] = {cd_a, cd_b};
  SamHashMap *date_indices[2];
  date_indices[0] = samtrader_build_date_index(arena, cd_a->ohlcv);
  date_indices[1] = samtrader_build_date_index(arena, cd_b->ohlcv);
  ASSERT(date_indices[0] && date_indices[1], "Failed to build date indices");

  SamrenaVector *timeline = samtrader_build_date_timeline(arena, code_data_arr, 2);
  ASSERT(timeline != NULL, "Failed to build timeline");
  ASSERT(samrena_vector_size(timeline) == 10, "Timeline should have 10 dates");

  /* Entry: close > 95, Exit: close > 125, max_positions=2 */
  SamtraderOperand close_op = samtrader_operand_price(SAMTRADER_OPERAND_PRICE_CLOSE);
  SamtraderOperand const_95 = samtrader_operand_constant(95.0);
  SamtraderOperand const_125 = samtrader_operand_constant(125.0);

  SamtraderRule *entry_rule =
      samtrader_rule_create_comparison(arena, SAMTRADER_RULE_ABOVE, close_op, const_95);
  SamtraderRule *exit_rule =
      samtrader_rule_create_comparison(arena, SAMTRADER_RULE_ABOVE, close_op, const_125);

  SamtraderStrategy strategy = {.name = "test_per_code",
                                .entry_long = entry_rule,
                                .exit_long = exit_rule,
                                .entry_short = NULL,
                                .exit_short = NULL,
                                .position_size = 0.25,
                                .stop_loss_pct = 0.0,
                                .take_profit_pct = 0.0,
                                .max_positions = 2};

  SamtraderPortfolio *portfolio = samtrader_portfolio_create(arena, 100000.0);
  ASSERT(portfolio != NULL, "Failed to create portfolio");

  int loop_result = run_multicode_backtest_loop(arena, code_data_arr, 2, date_indices, timeline,
                                                &strategy, portfolio, "US", 0.0, 0.0, 0.0, false);
  ASSERT(loop_result == 0, "Multi-code backtest loop failed");

  /* Trace:
   * Bar 0: A=90>95? no. B=85>95? no.
   * Bar 1: A=100>95? yes → enter CODEA. qty=floor(25000/100)=250, cash=75000
   *         B=95>95? no.
   * Bar 2: A=110, hold. B=105>95? yes → enter CODEB. qty=floor(18750/105)=178, cash=56310
   * Bar 3: A=120, hold. B=115, hold.
   * Bar 4: A=130>125? yes → exit CODEA. cash=56310+250*130=88810, PnL=250*(130-100)=7500
   *         A re-enter: 130>95? yes → enter CODEA. qty=floor(22202.5/130)=170,
   * cash=88810-170*130=66710 B=130>125? yes → exit CODEB. cash=66710+178*130=89850,
   * PnL=178*(130-105)=4450 B re-enter: 130>95? yes → enter CODEB. qty=floor(22462.5/130)=172,
   * cash=89850-172*130=67490 (wait: max_positions=2, we have CODEA open)
   *
   * Let me re-trace more carefully:
   * Bar 4: date = day_time(4)
   *   price_map: CODEA=130, CODEB=130
   *   check_triggers: no SL/TP
   *   Code CODEA (c=0): has position? yes. exit_long: 130>125? yes → exit.
   *     cash = 56310 + 250*130 = 88810. PnL = 250*(130-100) = 7500. Closed trade 1.
   *     no position now. enter_long: 130>95? yes → enter.
   *     available = 88810 * 0.25 = 22202.5, qty = floor(22202.5/130) = 170
   *     cash = 88810 - 170*130 = 88810 - 22100 = 66710. positions=1(CODEA).
   *   Code CODEB (c=1): has position? yes. exit_long: 130>125? yes → exit.
   *     cash = 66710 + 178*130 = 66710 + 23140 = 89850. PnL = 178*(130-105) = 4450. Closed trade 2.
   *     no position now. enter_long: 130>95? yes → enter.
   *     positions currently = 1 (CODEA). max_positions=2. ok.
   *     available = 89850 * 0.25 = 22462.5, qty = floor(22462.5/130) = 172
   *     cash = 89850 - 172*130 = 89850 - 22360 = 67490. positions=2(CODEA,CODEB).
   *
   * Bar 5: A=115, B=120. Both hold (neither >125).
   * Bar 6: A=100, B=110. Both hold.
   * Bar 7: A=110, B=100. Both hold.
   * Bar 8: A=120, B=90. Both hold.
   * Bar 9: A=130>125? yes → exit CODEA. cash=67490+170*130=67490+22100=89590. PnL=170*(130-130)=0.
   * Closed trade 3. A re-enter: 130>95? yes. positions=1(CODEB). max=2. ok. available = 89590*0.25
   * = 22397.5, qty=floor(22397.5/130)=172 cash = 89590 - 172*130 = 89590 - 22360 = 67230. B=80>125?
   * no. hold.
   *
   * Closed trades:
   * Trade 1: CODEA, entry=100, exit=130, PnL=7500
   * Trade 2: CODEB, entry=105, exit=130, PnL=4450
   * Trade 3: CODEA, entry=130, exit=130, PnL=0
   *
   * Per-code expected:
   * CODEA: total_trades=2, winning=1, losing=1, total_pnl=7500, win_rate=0.5,
   *        largest_win=7500, largest_loss=0
   * CODEB: total_trades=1, winning=1, losing=0, total_pnl=4450, win_rate=1.0,
   *        largest_win=4450, largest_loss=0
   */

  ASSERT(samrena_vector_size(portfolio->closed_trades) == 3, "Should have 3 closed trades");

  /* Verify aggregate metrics still work */
  SamtraderMetrics *metrics =
      samtrader_metrics_calculate(arena, portfolio->closed_trades, portfolio->equity_curve, 0.0);
  ASSERT(metrics != NULL, "Metrics should not be NULL");
  ASSERT(metrics->total_trades == 3, "3 total trades aggregate");

  /* Compute per-code metrics */
  const char *codes[] = {"CODEA", "CODEB"};
  SamtraderCodeResult *code_results =
      samtrader_metrics_compute_per_code(arena, portfolio->closed_trades, codes, "US", 2);
  ASSERT(code_results != NULL, "Per-code results should not be NULL");

  /* CODEA */
  ASSERT(strcmp(code_results[0].code, "CODEA") == 0, "First result is CODEA");
  ASSERT(strcmp(code_results[0].exchange, "US") == 0, "CODEA exchange is US");
  ASSERT(code_results[0].total_trades == 2, "CODEA: 2 trades");
  ASSERT(code_results[0].winning_trades == 1, "CODEA: 1 winning trade");
  ASSERT(code_results[0].losing_trades == 1, "CODEA: 1 losing trade (PnL=0)");
  ASSERT_DOUBLE_EQ(code_results[0].total_pnl, 7500.0, "CODEA total PnL");
  ASSERT_DOUBLE_EQ(code_results[0].win_rate, 0.5, "CODEA win rate");
  ASSERT_DOUBLE_EQ(code_results[0].largest_win, 7500.0, "CODEA largest win");
  ASSERT_DOUBLE_EQ(code_results[0].largest_loss, 0.0, "CODEA largest loss");

  /* CODEB */
  ASSERT(strcmp(code_results[1].code, "CODEB") == 0, "Second result is CODEB");
  ASSERT(code_results[1].total_trades == 1, "CODEB: 1 trade");
  ASSERT(code_results[1].winning_trades == 1, "CODEB: 1 winning trade");
  ASSERT(code_results[1].losing_trades == 0, "CODEB: 0 losing trades");
  ASSERT_DOUBLE_EQ(code_results[1].total_pnl, 4450.0, "CODEB total PnL");
  ASSERT_DOUBLE_EQ(code_results[1].win_rate, 1.0, "CODEB win rate");
  ASSERT_DOUBLE_EQ(code_results[1].largest_win, 4450.0, "CODEB largest win");
  ASSERT_DOUBLE_EQ(code_results[1].largest_loss, 0.0, "CODEB largest loss");

  /* Verify aggregate total_pnl matches sum of per-code */
  double total_code_pnl = code_results[0].total_pnl + code_results[1].total_pnl;
  ASSERT_DOUBLE_EQ(total_code_pnl, 11950.0, "Sum of per-code PnL");
  ASSERT(code_results[0].total_trades + code_results[1].total_trades == metrics->total_trades,
         "Sum of per-code trades == aggregate trades");

  samrena_destroy(arena);
  printf("  PASS\n");
  return 0;
}

/*============================================================================
 * Main
 *============================================================================*/

int main(void) {
  printf("=== Backtest Integration Tests ===\n\n");

  int failures = 0;

  failures += test_simple_long_backtest();
  failures += test_stop_loss_trigger();
  failures += test_multiple_trades();
  failures += test_sma_strategy();
  failures += test_portfolio_invariant_stress();
  failures += test_multicode_both_enter();
  failures += test_multicode_max_positions();
  failures += test_multicode_disjoint_dates();
  failures += test_multicode_per_code_metrics();

  printf("\n=== Results: %d failures ===\n", failures);

  return failures > 0 ? EXIT_FAILURE : EXIT_SUCCESS;
}
