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

#include "samtrader/domain/metrics.h"
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
    if (fabs((a) - (b)) > 0.01) {                                                                  \
      printf("FAIL: %s (expected %f, got %f)\n", msg, (b), (a));                                   \
      return 1;                                                                                    \
    }                                                                                              \
  } while (0)

#define ASSERT_DOUBLE_NEAR(a, b, tol, msg)                                                         \
  do {                                                                                             \
    if (fabs((a) - (b)) > (tol)) {                                                                 \
      printf("FAIL: %s (expected %f, got %f, tol %f)\n", msg, (b), (a), (tol));                    \
      return 1;                                                                                    \
    }                                                                                              \
  } while (0)

/* Helper: make a time_t from a day offset (day 0 = some epoch base) */
static time_t day_time(int day) {
  return (time_t)(1704067200 + day * 86400); /* 2024-01-01 + day offset */
}

/* ========== NULL/Empty Input Tests ========== */

static int test_null_arena(void) {
  printf("Testing NULL arena...\n");
  SamtraderMetrics *m = samtrader_metrics_calculate(NULL, NULL, NULL, 0.0);
  ASSERT(m == NULL, "NULL arena should return NULL");
  printf("  PASS\n");
  return 0;
}

static int test_null_vectors(void) {
  printf("Testing NULL vectors...\n");
  Samrena *arena = samrena_create_default();
  SamtraderMetrics *m = samtrader_metrics_calculate(arena, NULL, NULL, 0.0);
  ASSERT(m != NULL, "Should return zeroed metrics");
  ASSERT(m->total_trades == 0, "No trades");
  ASSERT_DOUBLE_EQ(m->total_return, 0.0, "Zero return");
  ASSERT_DOUBLE_EQ(m->sharpe_ratio, 0.0, "Zero sharpe");
  ASSERT_DOUBLE_EQ(m->max_drawdown, 0.0, "Zero drawdown");
  samrena_destroy(arena);
  printf("  PASS\n");
  return 0;
}

static int test_empty_vectors(void) {
  printf("Testing empty vectors...\n");
  Samrena *arena = samrena_create_default();
  SamrenaVector *trades = samrena_vector_init(arena, sizeof(SamtraderClosedTrade), 4);
  SamrenaVector *equity = samrena_vector_init(arena, sizeof(SamtraderEquityPoint), 4);

  SamtraderMetrics *m = samtrader_metrics_calculate(arena, trades, equity, 0.0);
  ASSERT(m != NULL, "Should return zeroed metrics");
  ASSERT(m->total_trades == 0, "No trades");
  ASSERT_DOUBLE_EQ(m->win_rate, 0.0, "Zero win rate");
  ASSERT_DOUBLE_EQ(m->profit_factor, 0.0, "Zero profit factor");

  samrena_destroy(arena);
  printf("  PASS\n");
  return 0;
}

/* ========== Single Trade Tests ========== */

static int test_single_winning_trade(void) {
  printf("Testing single winning trade...\n");
  Samrena *arena = samrena_create_default();
  SamrenaVector *trades = samrena_vector_init(arena, sizeof(SamtraderClosedTrade), 4);
  SamrenaVector *equity = samrena_vector_init(arena, sizeof(SamtraderEquityPoint), 4);

  SamtraderClosedTrade trade = {.code = "AAPL",
                                .exchange = "US",
                                .quantity = 100,
                                .entry_price = 100.0,
                                .exit_price = 110.0,
                                .entry_date = day_time(0),
                                .exit_date = day_time(5),
                                .pnl = 1000.0};
  samrena_vector_push(trades, &trade);

  SamtraderMetrics *m = samtrader_metrics_calculate(arena, trades, equity, 0.0);
  ASSERT(m != NULL, "Metrics not NULL");
  ASSERT(m->total_trades == 1, "1 trade");
  ASSERT(m->winning_trades == 1, "1 win");
  ASSERT(m->losing_trades == 0, "0 losses");
  ASSERT_DOUBLE_EQ(m->win_rate, 1.0, "100% win rate");
  ASSERT_DOUBLE_EQ(m->average_win, 1000.0, "Average win");
  ASSERT_DOUBLE_EQ(m->largest_win, 1000.0, "Largest win");
  ASSERT_DOUBLE_EQ(m->largest_loss, 0.0, "No losses");
  ASSERT_DOUBLE_EQ(m->average_trade_duration, 5.0, "5 day duration");
  /* profit_factor with no losses → INFINITY */
  ASSERT(isinf(m->profit_factor), "Infinite profit factor");

  samrena_destroy(arena);
  printf("  PASS\n");
  return 0;
}

static int test_single_losing_trade(void) {
  printf("Testing single losing trade...\n");
  Samrena *arena = samrena_create_default();
  SamrenaVector *trades = samrena_vector_init(arena, sizeof(SamtraderClosedTrade), 4);
  SamrenaVector *equity = samrena_vector_init(arena, sizeof(SamtraderEquityPoint), 4);

  SamtraderClosedTrade trade = {.code = "AAPL",
                                .exchange = "US",
                                .quantity = 100,
                                .entry_price = 100.0,
                                .exit_price = 90.0,
                                .entry_date = day_time(0),
                                .exit_date = day_time(3),
                                .pnl = -1000.0};
  samrena_vector_push(trades, &trade);

  SamtraderMetrics *m = samtrader_metrics_calculate(arena, trades, equity, 0.0);
  ASSERT(m->total_trades == 1, "1 trade");
  ASSERT(m->winning_trades == 0, "0 wins");
  ASSERT(m->losing_trades == 1, "1 loss");
  ASSERT_DOUBLE_EQ(m->win_rate, 0.0, "0% win rate");
  ASSERT_DOUBLE_EQ(m->average_loss, -1000.0, "Average loss");
  ASSERT_DOUBLE_EQ(m->largest_loss, -1000.0, "Largest loss");
  ASSERT_DOUBLE_EQ(m->profit_factor, 0.0, "Zero profit factor");
  ASSERT_DOUBLE_EQ(m->average_trade_duration, 3.0, "3 day duration");

  samrena_destroy(arena);
  printf("  PASS\n");
  return 0;
}

/* ========== Mixed Trades Tests ========== */

static int test_mixed_trades(void) {
  printf("Testing mixed trades...\n");
  Samrena *arena = samrena_create_default();
  SamrenaVector *trades = samrena_vector_init(arena, sizeof(SamtraderClosedTrade), 8);
  SamrenaVector *equity = samrena_vector_init(arena, sizeof(SamtraderEquityPoint), 4);

  /* 3 winners: +500, +300, +200 = +1000 total */
  SamtraderClosedTrade t1 = {.code = "A",
                             .entry_date = day_time(0),
                             .exit_date = day_time(10),
                             .pnl = 500.0};
  SamtraderClosedTrade t2 = {.code = "B",
                             .entry_date = day_time(0),
                             .exit_date = day_time(5),
                             .pnl = 300.0};
  SamtraderClosedTrade t3 = {.code = "C",
                             .entry_date = day_time(0),
                             .exit_date = day_time(20),
                             .pnl = 200.0};
  /* 2 losers: -400, -100 = -500 total */
  SamtraderClosedTrade t4 = {.code = "D",
                             .entry_date = day_time(0),
                             .exit_date = day_time(15),
                             .pnl = -400.0};
  SamtraderClosedTrade t5 = {.code = "E",
                             .entry_date = day_time(0),
                             .exit_date = day_time(10),
                             .pnl = -100.0};

  samrena_vector_push(trades, &t1);
  samrena_vector_push(trades, &t2);
  samrena_vector_push(trades, &t3);
  samrena_vector_push(trades, &t4);
  samrena_vector_push(trades, &t5);

  SamtraderMetrics *m = samtrader_metrics_calculate(arena, trades, equity, 0.0);
  ASSERT(m->total_trades == 5, "5 trades");
  ASSERT(m->winning_trades == 3, "3 wins");
  ASSERT(m->losing_trades == 2, "2 losses");
  ASSERT_DOUBLE_EQ(m->win_rate, 0.60, "60% win rate");

  /* Average win = 1000/3 = 333.33 */
  ASSERT_DOUBLE_NEAR(m->average_win, 333.33, 0.01, "Average win");
  /* Average loss = -500/2 = -250 */
  ASSERT_DOUBLE_EQ(m->average_loss, -250.0, "Average loss");
  ASSERT_DOUBLE_EQ(m->largest_win, 500.0, "Largest win");
  ASSERT_DOUBLE_EQ(m->largest_loss, -400.0, "Largest loss");
  /* Profit factor = 1000/500 = 2.0 */
  ASSERT_DOUBLE_EQ(m->profit_factor, 2.0, "Profit factor");
  /* Average duration = (10+5+20+15+10)/5 = 12.0 */
  ASSERT_DOUBLE_EQ(m->average_trade_duration, 12.0, "Average duration");

  samrena_destroy(arena);
  printf("  PASS\n");
  return 0;
}

/* ========== Return Calculation Tests ========== */

static int test_total_return(void) {
  printf("Testing total return...\n");
  Samrena *arena = samrena_create_default();
  SamrenaVector *trades = samrena_vector_init(arena, sizeof(SamtraderClosedTrade), 4);
  SamrenaVector *equity = samrena_vector_init(arena, sizeof(SamtraderEquityPoint), 8);

  /* Start at 10000, end at 12000 → 20% return */
  SamtraderEquityPoint p0 = {.date = day_time(0), .equity = 10000.0};
  SamtraderEquityPoint p1 = {.date = day_time(1), .equity = 10500.0};
  SamtraderEquityPoint p2 = {.date = day_time(2), .equity = 11000.0};
  SamtraderEquityPoint p3 = {.date = day_time(3), .equity = 11500.0};
  SamtraderEquityPoint p4 = {.date = day_time(4), .equity = 12000.0};
  samrena_vector_push(equity, &p0);
  samrena_vector_push(equity, &p1);
  samrena_vector_push(equity, &p2);
  samrena_vector_push(equity, &p3);
  samrena_vector_push(equity, &p4);

  SamtraderMetrics *m = samtrader_metrics_calculate(arena, trades, equity, 0.0);
  ASSERT_DOUBLE_EQ(m->total_return, 0.20, "20% total return");

  /* 4 trading days, annualized: (1.20)^(252/4) - 1 */
  double expected_ann = pow(1.20, 252.0 / 4.0) - 1.0;
  ASSERT_DOUBLE_NEAR(m->annualized_return, expected_ann, 0.01, "Annualized return");

  samrena_destroy(arena);
  printf("  PASS\n");
  return 0;
}

static int test_negative_return(void) {
  printf("Testing negative return...\n");
  Samrena *arena = samrena_create_default();
  SamrenaVector *trades = samrena_vector_init(arena, sizeof(SamtraderClosedTrade), 4);
  SamrenaVector *equity = samrena_vector_init(arena, sizeof(SamtraderEquityPoint), 4);

  SamtraderEquityPoint p0 = {.date = day_time(0), .equity = 10000.0};
  SamtraderEquityPoint p1 = {.date = day_time(1), .equity = 8000.0};
  samrena_vector_push(equity, &p0);
  samrena_vector_push(equity, &p1);

  SamtraderMetrics *m = samtrader_metrics_calculate(arena, trades, equity, 0.0);
  ASSERT_DOUBLE_EQ(m->total_return, -0.20, "-20% total return");

  samrena_destroy(arena);
  printf("  PASS\n");
  return 0;
}

static int test_single_equity_point(void) {
  printf("Testing single equity point...\n");
  Samrena *arena = samrena_create_default();
  SamrenaVector *trades = samrena_vector_init(arena, sizeof(SamtraderClosedTrade), 4);
  SamrenaVector *equity = samrena_vector_init(arena, sizeof(SamtraderEquityPoint), 4);

  SamtraderEquityPoint p0 = {.date = day_time(0), .equity = 10000.0};
  samrena_vector_push(equity, &p0);

  SamtraderMetrics *m = samtrader_metrics_calculate(arena, trades, equity, 0.0);
  ASSERT_DOUBLE_EQ(m->total_return, 0.0, "Zero return single point");
  ASSERT_DOUBLE_EQ(m->sharpe_ratio, 0.0, "Zero sharpe single point");
  ASSERT_DOUBLE_EQ(m->max_drawdown, 0.0, "Zero drawdown single point");

  samrena_destroy(arena);
  printf("  PASS\n");
  return 0;
}

/* ========== Sharpe/Sortino Tests ========== */

static int test_sharpe_ratio(void) {
  printf("Testing Sharpe ratio...\n");
  Samrena *arena = samrena_create_default();
  SamrenaVector *trades = samrena_vector_init(arena, sizeof(SamtraderClosedTrade), 4);
  SamrenaVector *equity = samrena_vector_init(arena, sizeof(SamtraderEquityPoint), 8);

  /* Constant 1% daily returns: 100, 101, 102.01, 103.0301, 104.060401 */
  double eq = 100.0;
  for (int i = 0; i < 5; i++) {
    SamtraderEquityPoint pt = {.date = day_time(i), .equity = eq};
    samrena_vector_push(equity, &pt);
    eq *= 1.01;
  }

  SamtraderMetrics *m = samtrader_metrics_calculate(arena, trades, equity, 0.0);

  /* All daily returns are ~0.01, stddev ~0, so sharpe approaches infinity.
   * But due to floating point, there's tiny variation. We just verify it's very large. */
  /* With zero risk-free rate and constant returns, sharpe = mean/stddev * sqrt(252)
   * mean ≈ 0.01, stddev ≈ tiny → very large sharpe */
  ASSERT(m->sharpe_ratio > 100.0, "Sharpe should be very high for constant returns");

  samrena_destroy(arena);
  printf("  PASS\n");
  return 0;
}

static int test_sharpe_with_risk_free(void) {
  printf("Testing Sharpe with risk-free rate...\n");
  Samrena *arena = samrena_create_default();
  SamrenaVector *trades = samrena_vector_init(arena, sizeof(SamtraderClosedTrade), 4);
  SamrenaVector *equity = samrena_vector_init(arena, sizeof(SamtraderEquityPoint), 16);

  /* Known daily returns: +2%, -1%, +3%, -2%, +1% */
  double equities[] = {10000.0, 10200.0, 10098.0, 10400.94, 10192.92, 10294.85};
  for (int i = 0; i < 6; i++) {
    SamtraderEquityPoint pt = {.date = day_time(i), .equity = equities[i]};
    samrena_vector_push(equity, &pt);
  }

  /* Hand-compute:
   * daily_returns = [0.02, -0.01, 0.03, -0.02, 0.01]
   * mean = 0.006
   * risk_free_daily = 0.05/252 ≈ 0.000198
   * excess mean = 0.006 - 0.000198 = 0.005802
   * variance = sum((r-mean)^2)/5
   *   = ((0.014)^2 + (-0.016)^2 + (0.024)^2 + (-0.026)^2 + (0.004)^2)/5
   *   = (0.000196 + 0.000256 + 0.000576 + 0.000676 + 0.000016)/5
   *   = 0.001720/5 = 0.000344
   * stddev = sqrt(0.000344) ≈ 0.018547
   * sharpe = 0.005802 / 0.018547 * sqrt(252) ≈ 4.968
   */
  SamtraderMetrics *m = samtrader_metrics_calculate(arena, trades, equity, 0.05);
  ASSERT_DOUBLE_NEAR(m->sharpe_ratio, 4.97, 0.1, "Sharpe with risk-free rate");

  samrena_destroy(arena);
  printf("  PASS\n");
  return 0;
}

static int test_sortino_ratio(void) {
  printf("Testing Sortino ratio...\n");
  Samrena *arena = samrena_create_default();
  SamrenaVector *trades = samrena_vector_init(arena, sizeof(SamtraderClosedTrade), 4);
  SamrenaVector *equity = samrena_vector_init(arena, sizeof(SamtraderEquityPoint), 16);

  /* Same daily returns: +2%, -1%, +3%, -2%, +1% */
  double equities[] = {10000.0, 10200.0, 10098.0, 10400.94, 10192.92, 10294.85};
  for (int i = 0; i < 6; i++) {
    SamtraderEquityPoint pt = {.date = day_time(i), .equity = equities[i]};
    samrena_vector_push(equity, &pt);
  }

  /* Hand-compute (risk_free = 0):
   * daily returns: [0.02, -0.01, 0.03, -0.02, 0.01]
   * mean = 0.006, risk_free_daily = 0
   * downside: returns below 0 = [-0.01, -0.02]
   * downside_sq = (0.01^2 + 0.02^2)/5 = (0.0001 + 0.0004)/5 = 0.0001
   * downside_dev = sqrt(0.0001) = 0.01
   * sortino = 0.006/0.01 * sqrt(252) ≈ 9.524
   */
  SamtraderMetrics *m = samtrader_metrics_calculate(arena, trades, equity, 0.0);
  ASSERT_DOUBLE_NEAR(m->sortino_ratio, 9.52, 0.1, "Sortino ratio");

  samrena_destroy(arena);
  printf("  PASS\n");
  return 0;
}

/* ========== Max Drawdown Tests ========== */

static int test_max_drawdown(void) {
  printf("Testing max drawdown...\n");
  Samrena *arena = samrena_create_default();
  SamrenaVector *trades = samrena_vector_init(arena, sizeof(SamtraderClosedTrade), 4);
  SamrenaVector *equity = samrena_vector_init(arena, sizeof(SamtraderEquityPoint), 16);

  /* Equity: 100, 120, 108, 90, 110, 130 */
  /* Peak at 120, trough at 90 → dd = (120-90)/120 = 25% */
  double equities[] = {100.0, 120.0, 108.0, 90.0, 110.0, 130.0};
  for (int i = 0; i < 6; i++) {
    SamtraderEquityPoint pt = {.date = day_time(i), .equity = equities[i]};
    samrena_vector_push(equity, &pt);
  }

  SamtraderMetrics *m = samtrader_metrics_calculate(arena, trades, equity, 0.0);
  ASSERT_DOUBLE_EQ(m->max_drawdown, 0.25, "25% max drawdown");

  samrena_destroy(arena);
  printf("  PASS\n");
  return 0;
}

static int test_max_drawdown_duration(void) {
  printf("Testing max drawdown duration...\n");
  Samrena *arena = samrena_create_default();
  SamrenaVector *trades = samrena_vector_init(arena, sizeof(SamtraderClosedTrade), 4);
  SamrenaVector *equity = samrena_vector_init(arena, sizeof(SamtraderEquityPoint), 16);

  /* Equity: 100, 120, 108, 90, 110, 130 */
  /* Peak at index 1 (120), new peak at index 5 (130) → duration = 5-1 = 4 days */
  double equities[] = {100.0, 120.0, 108.0, 90.0, 110.0, 130.0};
  for (int i = 0; i < 6; i++) {
    SamtraderEquityPoint pt = {.date = day_time(i), .equity = equities[i]};
    samrena_vector_push(equity, &pt);
  }

  SamtraderMetrics *m = samtrader_metrics_calculate(arena, trades, equity, 0.0);
  ASSERT_DOUBLE_EQ(m->max_drawdown_duration, 4.0, "4 day drawdown duration");

  samrena_destroy(arena);
  printf("  PASS\n");
  return 0;
}

static int test_no_drawdown(void) {
  printf("Testing no drawdown (monotonic increase)...\n");
  Samrena *arena = samrena_create_default();
  SamrenaVector *trades = samrena_vector_init(arena, sizeof(SamtraderClosedTrade), 4);
  SamrenaVector *equity = samrena_vector_init(arena, sizeof(SamtraderEquityPoint), 8);

  for (int i = 0; i < 5; i++) {
    SamtraderEquityPoint pt = {.date = day_time(i), .equity = 100.0 + i * 10.0};
    samrena_vector_push(equity, &pt);
  }

  SamtraderMetrics *m = samtrader_metrics_calculate(arena, trades, equity, 0.0);
  ASSERT_DOUBLE_EQ(m->max_drawdown, 0.0, "No drawdown");
  ASSERT_DOUBLE_EQ(m->max_drawdown_duration, 0.0, "No drawdown duration");

  samrena_destroy(arena);
  printf("  PASS\n");
  return 0;
}

static int test_drawdown_never_recovers(void) {
  printf("Testing drawdown that never recovers...\n");
  Samrena *arena = samrena_create_default();
  SamrenaVector *trades = samrena_vector_init(arena, sizeof(SamtraderClosedTrade), 4);
  SamrenaVector *equity = samrena_vector_init(arena, sizeof(SamtraderEquityPoint), 8);

  /* 100, 80, 70, 60 — never recovers from initial peak */
  double equities[] = {100.0, 80.0, 70.0, 60.0};
  for (int i = 0; i < 4; i++) {
    SamtraderEquityPoint pt = {.date = day_time(i), .equity = equities[i]};
    samrena_vector_push(equity, &pt);
  }

  SamtraderMetrics *m = samtrader_metrics_calculate(arena, trades, equity, 0.0);
  /* dd = (100-60)/100 = 40% */
  ASSERT_DOUBLE_EQ(m->max_drawdown, 0.40, "40% drawdown");
  /* Duration from index 0 to end (3) = 3 days */
  ASSERT_DOUBLE_EQ(m->max_drawdown_duration, 3.0, "3 day unrecovered drawdown");

  samrena_destroy(arena);
  printf("  PASS\n");
  return 0;
}

/* ========== Edge Case Tests ========== */

static int test_all_winning_trades(void) {
  printf("Testing all winning trades...\n");
  Samrena *arena = samrena_create_default();
  SamrenaVector *trades = samrena_vector_init(arena, sizeof(SamtraderClosedTrade), 4);
  SamrenaVector *equity = samrena_vector_init(arena, sizeof(SamtraderEquityPoint), 4);

  SamtraderClosedTrade t1 = {.code = "A",
                             .entry_date = day_time(0),
                             .exit_date = day_time(1),
                             .pnl = 100.0};
  SamtraderClosedTrade t2 = {.code = "B",
                             .entry_date = day_time(0),
                             .exit_date = day_time(2),
                             .pnl = 200.0};
  samrena_vector_push(trades, &t1);
  samrena_vector_push(trades, &t2);

  SamtraderMetrics *m = samtrader_metrics_calculate(arena, trades, equity, 0.0);
  ASSERT_DOUBLE_EQ(m->win_rate, 1.0, "100% win rate");
  ASSERT(isinf(m->profit_factor), "Infinite profit factor");
  ASSERT_DOUBLE_EQ(m->average_loss, 0.0, "No losses");

  samrena_destroy(arena);
  printf("  PASS\n");
  return 0;
}

static int test_all_losing_trades(void) {
  printf("Testing all losing trades...\n");
  Samrena *arena = samrena_create_default();
  SamrenaVector *trades = samrena_vector_init(arena, sizeof(SamtraderClosedTrade), 4);
  SamrenaVector *equity = samrena_vector_init(arena, sizeof(SamtraderEquityPoint), 4);

  SamtraderClosedTrade t1 = {.code = "A",
                             .entry_date = day_time(0),
                             .exit_date = day_time(1),
                             .pnl = -100.0};
  SamtraderClosedTrade t2 = {.code = "B",
                             .entry_date = day_time(0),
                             .exit_date = day_time(2),
                             .pnl = -200.0};
  samrena_vector_push(trades, &t1);
  samrena_vector_push(trades, &t2);

  SamtraderMetrics *m = samtrader_metrics_calculate(arena, trades, equity, 0.0);
  ASSERT_DOUBLE_EQ(m->win_rate, 0.0, "0% win rate");
  ASSERT_DOUBLE_EQ(m->profit_factor, 0.0, "Zero profit factor");
  ASSERT_DOUBLE_EQ(m->average_win, 0.0, "No wins");

  samrena_destroy(arena);
  printf("  PASS\n");
  return 0;
}

static int test_zero_pnl_trade(void) {
  printf("Testing zero PnL trade...\n");
  Samrena *arena = samrena_create_default();
  SamrenaVector *trades = samrena_vector_init(arena, sizeof(SamtraderClosedTrade), 4);
  SamrenaVector *equity = samrena_vector_init(arena, sizeof(SamtraderEquityPoint), 4);

  /* Zero PnL is counted as a loss (pnl <= 0) */
  SamtraderClosedTrade t1 = {.code = "A",
                             .entry_date = day_time(0),
                             .exit_date = day_time(1),
                             .pnl = 0.0};
  samrena_vector_push(trades, &t1);

  SamtraderMetrics *m = samtrader_metrics_calculate(arena, trades, equity, 0.0);
  ASSERT(m->total_trades == 1, "1 trade");
  ASSERT(m->winning_trades == 0, "0 wins");
  ASSERT(m->losing_trades == 1, "1 loss (zero PnL)");
  ASSERT_DOUBLE_EQ(m->win_rate, 0.0, "0% win rate");

  samrena_destroy(arena);
  printf("  PASS\n");
  return 0;
}

static int test_flat_equity_curve(void) {
  printf("Testing flat equity curve...\n");
  Samrena *arena = samrena_create_default();
  SamrenaVector *trades = samrena_vector_init(arena, sizeof(SamtraderClosedTrade), 4);
  SamrenaVector *equity = samrena_vector_init(arena, sizeof(SamtraderEquityPoint), 8);

  for (int i = 0; i < 5; i++) {
    SamtraderEquityPoint pt = {.date = day_time(i), .equity = 10000.0};
    samrena_vector_push(equity, &pt);
  }

  SamtraderMetrics *m = samtrader_metrics_calculate(arena, trades, equity, 0.0);
  ASSERT_DOUBLE_EQ(m->total_return, 0.0, "Zero return");
  ASSERT_DOUBLE_EQ(m->annualized_return, 0.0, "Zero annualized return");
  ASSERT_DOUBLE_EQ(m->sharpe_ratio, 0.0, "Zero sharpe (zero stddev)");
  ASSERT_DOUBLE_EQ(m->max_drawdown, 0.0, "No drawdown");

  samrena_destroy(arena);
  printf("  PASS\n");
  return 0;
}

static int test_print_null(void) {
  printf("Testing print with NULL...\n");
  samtrader_metrics_print(NULL);
  printf("  PASS\n");
  return 0;
}

int main(void) {
  printf("=== Metrics Tests ===\n\n");

  int failures = 0;

  /* NULL/empty inputs */
  failures += test_null_arena();
  failures += test_null_vectors();
  failures += test_empty_vectors();

  /* Single trades */
  failures += test_single_winning_trade();
  failures += test_single_losing_trade();

  /* Mixed trades */
  failures += test_mixed_trades();

  /* Return calculation */
  failures += test_total_return();
  failures += test_negative_return();
  failures += test_single_equity_point();

  /* Sharpe/Sortino */
  failures += test_sharpe_ratio();
  failures += test_sharpe_with_risk_free();
  failures += test_sortino_ratio();

  /* Max drawdown */
  failures += test_max_drawdown();
  failures += test_max_drawdown_duration();
  failures += test_no_drawdown();
  failures += test_drawdown_never_recovers();

  /* Edge cases */
  failures += test_all_winning_trades();
  failures += test_all_losing_trades();
  failures += test_zero_pnl_trade();
  failures += test_flat_equity_curve();
  failures += test_print_null();

  printf("\n=== Results: %d failures ===\n", failures);

  return failures > 0 ? EXIT_FAILURE : EXIT_SUCCESS;
}
