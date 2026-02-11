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

#include "samtrader/domain/execution.h"

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

/* ========== Calc Helpers ========== */

static int test_commission_flat_only(void) {
  printf("Testing commission flat only...\n");
  double c = samtrader_execution_calc_commission(10000.0, 9.95, 0.0);
  ASSERT_DOUBLE_EQ(c, 9.95, "Flat commission");
  printf("  PASS\n");
  return 0;
}

static int test_commission_pct_only(void) {
  printf("Testing commission pct only...\n");
  double c = samtrader_execution_calc_commission(10000.0, 0.0, 0.1);
  ASSERT_DOUBLE_EQ(c, 10.0, "Pct commission");
  printf("  PASS\n");
  return 0;
}

static int test_commission_combined(void) {
  printf("Testing commission combined...\n");
  double c = samtrader_execution_calc_commission(10000.0, 9.95, 0.1);
  ASSERT_DOUBLE_EQ(c, 19.95, "Combined commission");
  printf("  PASS\n");
  return 0;
}

static int test_commission_zero(void) {
  printf("Testing commission zero...\n");
  double c = samtrader_execution_calc_commission(10000.0, 0.0, 0.0);
  ASSERT_DOUBLE_EQ(c, 0.0, "Zero commission");
  printf("  PASS\n");
  return 0;
}

static int test_commission_zero_trade_value(void) {
  printf("Testing commission with zero trade value...\n");
  double c = samtrader_execution_calc_commission(0.0, 9.95, 0.1);
  ASSERT_DOUBLE_EQ(c, 9.95, "Commission on zero trade value");
  printf("  PASS\n");
  return 0;
}

static int test_slippage_up(void) {
  printf("Testing slippage up...\n");
  double p = samtrader_execution_apply_slippage(100.0, 0.5, true);
  ASSERT_DOUBLE_EQ(p, 100.50, "Slippage up");
  printf("  PASS\n");
  return 0;
}

static int test_slippage_down(void) {
  printf("Testing slippage down...\n");
  double p = samtrader_execution_apply_slippage(100.0, 0.5, false);
  ASSERT_DOUBLE_EQ(p, 99.50, "Slippage down");
  printf("  PASS\n");
  return 0;
}

static int test_slippage_zero(void) {
  printf("Testing slippage zero...\n");
  double p = samtrader_execution_apply_slippage(100.0, 0.0, true);
  ASSERT_DOUBLE_EQ(p, 100.0, "Zero slippage up");
  p = samtrader_execution_apply_slippage(100.0, 0.0, false);
  ASSERT_DOUBLE_EQ(p, 100.0, "Zero slippage down");
  printf("  PASS\n");
  return 0;
}

static int test_quantity_exact(void) {
  printf("Testing quantity exact...\n");
  int64_t q = samtrader_execution_calc_quantity(10000.0, 100.0);
  ASSERT(q == 100, "Exact quantity");
  printf("  PASS\n");
  return 0;
}

static int test_quantity_fractional(void) {
  printf("Testing quantity fractional...\n");
  int64_t q = samtrader_execution_calc_quantity(10000.0, 33.33);
  ASSERT(q == 300, "Fractional quantity");
  printf("  PASS\n");
  return 0;
}

static int test_quantity_zero_price(void) {
  printf("Testing quantity zero price...\n");
  int64_t q = samtrader_execution_calc_quantity(10000.0, 0.0);
  ASSERT(q == 0, "Zero price quantity");
  printf("  PASS\n");
  return 0;
}

static int test_quantity_zero_capital(void) {
  printf("Testing quantity zero capital...\n");
  int64_t q = samtrader_execution_calc_quantity(0.0, 100.0);
  ASSERT(q == 0, "Zero capital quantity");
  printf("  PASS\n");
  return 0;
}

/* ========== Enter Long ========== */

static int test_enter_long_basic(void) {
  printf("Testing enter long basic...\n");

  Samrena *arena = samrena_create_default();
  ASSERT(arena != NULL, "Failed to create arena");

  SamtraderPortfolio *portfolio = samtrader_portfolio_create(arena, 100000.0);
  ASSERT(portfolio != NULL, "Failed to create portfolio");

  /* No slippage, no commission, 50% of cash, $100/share */
  bool result = samtrader_execution_enter_long(portfolio, arena, "AAPL", "US", 100.0, 1704067200,
                                               0.5, 0.0, 0.0, 10, 0.0, 0.0, 0.0);
  ASSERT(result, "Enter long failed");

  ASSERT(samtrader_portfolio_has_position(portfolio, "AAPL"), "Should have AAPL");
  SamtraderPosition *pos = samtrader_portfolio_get_position(portfolio, "AAPL");
  ASSERT(pos != NULL, "Position not found");
  ASSERT(pos->quantity == 500, "Expected 500 shares (50000/100)");
  ASSERT_DOUBLE_EQ(pos->entry_price, 100.0, "Entry price");
  ASSERT_DOUBLE_EQ(portfolio->cash, 50000.0, "Cash after long entry");

  samrena_destroy(arena);
  printf("  PASS\n");
  return 0;
}

static int test_enter_long_with_slippage(void) {
  printf("Testing enter long with slippage...\n");

  Samrena *arena = samrena_create_default();
  SamtraderPortfolio *portfolio = samtrader_portfolio_create(arena, 100000.0);

  /* 0.5% slippage on $100 → exec price $100.50, 50% of 100000=50000, qty=floor(50000/100.5)=497 */
  bool result = samtrader_execution_enter_long(portfolio, arena, "AAPL", "US", 100.0, 1704067200,
                                               0.5, 0.0, 0.0, 10, 0.0, 0.0, 0.5);
  ASSERT(result, "Enter long with slippage failed");

  SamtraderPosition *pos = samtrader_portfolio_get_position(portfolio, "AAPL");
  ASSERT(pos != NULL, "Position not found");
  ASSERT_DOUBLE_EQ(pos->entry_price, 100.50, "Slipped entry price");
  ASSERT(pos->quantity == 497, "Expected 497 shares");
  /* Cash = 100000 - 497*100.50 = 100000 - 49948.50 = 50051.50 */
  ASSERT_DOUBLE_EQ(portfolio->cash, 50051.50, "Cash after slipped entry");

  samrena_destroy(arena);
  printf("  PASS\n");
  return 0;
}

static int test_enter_long_with_stops(void) {
  printf("Testing enter long with stops...\n");

  Samrena *arena = samrena_create_default();
  SamtraderPortfolio *portfolio = samtrader_portfolio_create(arena, 100000.0);

  /* 5% stop loss, 10% take profit, no slippage */
  bool result = samtrader_execution_enter_long(portfolio, arena, "AAPL", "US", 100.0, 1704067200,
                                               0.5, 5.0, 10.0, 10, 0.0, 0.0, 0.0);
  ASSERT(result, "Enter long with stops failed");

  SamtraderPosition *pos = samtrader_portfolio_get_position(portfolio, "AAPL");
  ASSERT(pos != NULL, "Position not found");
  ASSERT_DOUBLE_EQ(pos->stop_loss, 95.0, "Stop loss at 5% below 100");
  ASSERT_DOUBLE_EQ(pos->take_profit, 110.0, "Take profit at 10% above 100");

  samrena_destroy(arena);
  printf("  PASS\n");
  return 0;
}

static int test_enter_long_with_commission(void) {
  printf("Testing enter long with commission...\n");

  Samrena *arena = samrena_create_default();
  SamtraderPortfolio *portfolio = samtrader_portfolio_create(arena, 100000.0);

  /* $9.95 flat + 0.1% commission, 50% of cash at $100 */
  bool result = samtrader_execution_enter_long(portfolio, arena, "AAPL", "US", 100.0, 1704067200,
                                               0.5, 0.0, 0.0, 10, 9.95, 0.1, 0.0);
  ASSERT(result, "Enter long with commission failed");

  SamtraderPosition *pos = samtrader_portfolio_get_position(portfolio, "AAPL");
  ASSERT(pos != NULL, "Position not found");
  ASSERT(pos->quantity == 500, "Expected 500 shares");
  /* Cost = 500*100 = 50000, commission = 9.95 + 50000*0.1/100 = 9.95+50 = 59.95 */
  /* Cash = 100000 - 50000 - 59.95 = 49940.05 */
  ASSERT_DOUBLE_EQ(portfolio->cash, 49940.05, "Cash after commission");

  samrena_destroy(arena);
  printf("  PASS\n");
  return 0;
}

static int test_enter_long_insufficient_funds(void) {
  printf("Testing enter long insufficient funds...\n");

  Samrena *arena = samrena_create_default();
  SamtraderPortfolio *portfolio = samtrader_portfolio_create(arena, 50.0);

  /* Only $50 cash, trying to buy shares at $100 with 100% position sizing */
  bool result = samtrader_execution_enter_long(portfolio, arena, "AAPL", "US", 100.0, 1704067200,
                                               1.0, 0.0, 0.0, 10, 0.0, 0.0, 0.0);
  /* floor(50/100) = 0 shares → should fail */
  ASSERT(!result, "Should fail with insufficient funds");
  ASSERT_DOUBLE_EQ(portfolio->cash, 50.0, "Cash unchanged");

  samrena_destroy(arena);
  printf("  PASS\n");
  return 0;
}

static int test_enter_long_max_positions(void) {
  printf("Testing enter long max positions...\n");

  Samrena *arena = samrena_create_default();
  SamtraderPortfolio *portfolio = samtrader_portfolio_create(arena, 100000.0);

  /* Fill to max_positions = 1 */
  bool result = samtrader_execution_enter_long(portfolio, arena, "AAPL", "US", 100.0, 1704067200,
                                               0.25, 0.0, 0.0, 1, 0.0, 0.0, 0.0);
  ASSERT(result, "First entry should succeed");

  /* Second entry should fail - at max */
  result = samtrader_execution_enter_long(portfolio, arena, "BHP", "AU", 50.0, 1704067200, 0.25,
                                          0.0, 0.0, 1, 0.0, 0.0, 0.0);
  ASSERT(!result, "Should fail at max positions");
  ASSERT(!samtrader_portfolio_has_position(portfolio, "BHP"), "Should not have BHP");

  samrena_destroy(arena);
  printf("  PASS\n");
  return 0;
}

static int test_enter_long_duplicate_code(void) {
  printf("Testing enter long duplicate code...\n");

  Samrena *arena = samrena_create_default();
  SamtraderPortfolio *portfolio = samtrader_portfolio_create(arena, 100000.0);

  bool result = samtrader_execution_enter_long(portfolio, arena, "AAPL", "US", 100.0, 1704067200,
                                               0.25, 0.0, 0.0, 10, 0.0, 0.0, 0.0);
  ASSERT(result, "First entry should succeed");

  double cash_after_first = portfolio->cash;

  result = samtrader_execution_enter_long(portfolio, arena, "AAPL", "US", 110.0, 1704067200, 0.25,
                                          0.0, 0.0, 10, 0.0, 0.0, 0.0);
  ASSERT(!result, "Duplicate entry should fail");
  ASSERT_DOUBLE_EQ(portfolio->cash, cash_after_first, "Cash unchanged on duplicate");

  samrena_destroy(arena);
  printf("  PASS\n");
  return 0;
}

static int test_enter_long_null_params(void) {
  printf("Testing enter long null params...\n");

  Samrena *arena = samrena_create_default();
  SamtraderPortfolio *portfolio = samtrader_portfolio_create(arena, 100000.0);

  ASSERT(!samtrader_execution_enter_long(NULL, arena, "AAPL", "US", 100.0, 0, 0.5, 0, 0, 10, 0, 0,
                                         0),
         "NULL portfolio");
  ASSERT(!samtrader_execution_enter_long(portfolio, NULL, "AAPL", "US", 100.0, 0, 0.5, 0, 0, 10, 0,
                                         0, 0),
         "NULL arena");
  ASSERT(!samtrader_execution_enter_long(portfolio, arena, NULL, "US", 100.0, 0, 0.5, 0, 0, 10, 0,
                                         0, 0),
         "NULL code");
  ASSERT(!samtrader_execution_enter_long(portfolio, arena, "AAPL", NULL, 100.0, 0, 0.5, 0, 0, 10, 0,
                                         0, 0),
         "NULL exchange");

  samrena_destroy(arena);
  printf("  PASS\n");
  return 0;
}

/* ========== Enter Short ========== */

static int test_enter_short_basic(void) {
  printf("Testing enter short basic...\n");

  Samrena *arena = samrena_create_default();
  SamtraderPortfolio *portfolio = samtrader_portfolio_create(arena, 100000.0);

  /* No slippage, no commission, 50% of cash, $100/share */
  bool result = samtrader_execution_enter_short(portfolio, arena, "AAPL", "US", 100.0, 1704067200,
                                                0.5, 0.0, 0.0, 10, 0.0, 0.0, 0.0);
  ASSERT(result, "Enter short failed");

  SamtraderPosition *pos = samtrader_portfolio_get_position(portfolio, "AAPL");
  ASSERT(pos != NULL, "Position not found");
  ASSERT(pos->quantity == -500, "Expected -500 shares");
  ASSERT_DOUBLE_EQ(pos->entry_price, 100.0, "Entry price");
  /* Cash += trade_value - commission = 50000 - 0 = 50000 */
  ASSERT_DOUBLE_EQ(portfolio->cash, 150000.0, "Cash after short entry");

  samrena_destroy(arena);
  printf("  PASS\n");
  return 0;
}

static int test_enter_short_with_slippage(void) {
  printf("Testing enter short with slippage...\n");

  Samrena *arena = samrena_create_default();
  SamtraderPortfolio *portfolio = samtrader_portfolio_create(arena, 100000.0);

  /* 0.5% slippage down on short sell → exec price $99.50 */
  bool result = samtrader_execution_enter_short(portfolio, arena, "AAPL", "US", 100.0, 1704067200,
                                                0.5, 0.0, 0.0, 10, 0.0, 0.0, 0.5);
  ASSERT(result, "Enter short with slippage failed");

  SamtraderPosition *pos = samtrader_portfolio_get_position(portfolio, "AAPL");
  ASSERT(pos != NULL, "Position not found");
  ASSERT_DOUBLE_EQ(pos->entry_price, 99.50, "Slipped short entry price");
  /* qty = floor(50000/99.50) = 502 */
  ASSERT(pos->quantity == -502, "Expected -502 shares");
  /* Cash = 100000 + 502*99.50 = 100000 + 49949.00 = 149949.00 */
  ASSERT_DOUBLE_EQ(portfolio->cash, 149949.00, "Cash after slipped short entry");

  samrena_destroy(arena);
  printf("  PASS\n");
  return 0;
}

static int test_enter_short_with_stops(void) {
  printf("Testing enter short with stops...\n");

  Samrena *arena = samrena_create_default();
  SamtraderPortfolio *portfolio = samtrader_portfolio_create(arena, 100000.0);

  /* 5% stop loss (above entry for short), 10% take profit (below entry) */
  bool result = samtrader_execution_enter_short(portfolio, arena, "AAPL", "US", 100.0, 1704067200,
                                                0.5, 5.0, 10.0, 10, 0.0, 0.0, 0.0);
  ASSERT(result, "Enter short with stops failed");

  SamtraderPosition *pos = samtrader_portfolio_get_position(portfolio, "AAPL");
  ASSERT(pos != NULL, "Position not found");
  ASSERT_DOUBLE_EQ(pos->stop_loss, 105.0, "Short stop loss at 5% above 100");
  ASSERT_DOUBLE_EQ(pos->take_profit, 90.0, "Short take profit at 10% below 100");

  samrena_destroy(arena);
  printf("  PASS\n");
  return 0;
}

/* ========== Exit Position ========== */

static int test_exit_long_profit(void) {
  printf("Testing exit long profit...\n");

  Samrena *arena = samrena_create_default();
  SamtraderPortfolio *portfolio = samtrader_portfolio_create(arena, 100000.0);

  /* Enter long at $100, 50% of cash, no slippage/commission */
  samtrader_execution_enter_long(portfolio, arena, "AAPL", "US", 100.0, 1704067200, 0.5, 0.0, 0.0,
                                 10, 0.0, 0.0, 0.0);
  /* Cash now 50000, 500 shares at $100 */

  /* Exit at $110, no slippage/commission */
  bool result =
      samtrader_execution_exit_position(portfolio, arena, "AAPL", 110.0, 1704672000, 0.0, 0.0, 0.0);
  ASSERT(result, "Exit long failed");
  ASSERT(!samtrader_portfolio_has_position(portfolio, "AAPL"), "Position should be removed");

  /* Cash = 50000 + 500*110 = 50000 + 55000 = 105000 */
  ASSERT_DOUBLE_EQ(portfolio->cash, 105000.0, "Cash after profitable exit");

  /* Check closed trade */
  ASSERT(samrena_vector_size(portfolio->closed_trades) == 1, "Should have 1 trade");
  const SamtraderClosedTrade *trade =
      (const SamtraderClosedTrade *)samrena_vector_at_const(portfolio->closed_trades, 0);
  ASSERT(trade != NULL, "Failed to get trade");
  ASSERT_DOUBLE_EQ(trade->entry_price, 100.0, "Trade entry price");
  ASSERT_DOUBLE_EQ(trade->exit_price, 110.0, "Trade exit price");
  /* PnL = 500 * (110-100) - 0 - 0 = 5000 */
  ASSERT_DOUBLE_EQ(trade->pnl, 5000.0, "Trade PnL");
  ASSERT(trade->quantity == 500, "Trade quantity");

  samrena_destroy(arena);
  printf("  PASS\n");
  return 0;
}

static int test_exit_long_loss(void) {
  printf("Testing exit long loss...\n");

  Samrena *arena = samrena_create_default();
  SamtraderPortfolio *portfolio = samtrader_portfolio_create(arena, 100000.0);

  samtrader_execution_enter_long(portfolio, arena, "AAPL", "US", 100.0, 1704067200, 0.5, 0.0, 0.0,
                                 10, 0.0, 0.0, 0.0);

  /* Exit at $90 → loss */
  bool result =
      samtrader_execution_exit_position(portfolio, arena, "AAPL", 90.0, 1704672000, 0.0, 0.0, 0.0);
  ASSERT(result, "Exit long failed");

  /* Cash = 50000 + 500*90 = 50000 + 45000 = 95000 */
  ASSERT_DOUBLE_EQ(portfolio->cash, 95000.0, "Cash after losing exit");

  const SamtraderClosedTrade *trade =
      (const SamtraderClosedTrade *)samrena_vector_at_const(portfolio->closed_trades, 0);
  /* PnL = 500 * (90-100) = -5000 */
  ASSERT_DOUBLE_EQ(trade->pnl, -5000.0, "Losing trade PnL");

  samrena_destroy(arena);
  printf("  PASS\n");
  return 0;
}

static int test_exit_short_profit(void) {
  printf("Testing exit short profit...\n");

  Samrena *arena = samrena_create_default();
  SamtraderPortfolio *portfolio = samtrader_portfolio_create(arena, 100000.0);

  samtrader_execution_enter_short(portfolio, arena, "AAPL", "US", 100.0, 1704067200, 0.5, 0.0, 0.0,
                                  10, 0.0, 0.0, 0.0);
  /* Cash = 100000 + 50000 = 150000, -500 shares at $100 */

  /* Price drops to $90 → profit for short */
  bool result =
      samtrader_execution_exit_position(portfolio, arena, "AAPL", 90.0, 1704672000, 0.0, 0.0, 0.0);
  ASSERT(result, "Exit short failed");

  /* Cash = 150000 - 500*90 = 150000 - 45000 = 105000 */
  ASSERT_DOUBLE_EQ(portfolio->cash, 105000.0, "Cash after short profit exit");

  const SamtraderClosedTrade *trade =
      (const SamtraderClosedTrade *)samrena_vector_at_const(portfolio->closed_trades, 0);
  /* PnL = -500 * (90-100) = 5000 */
  ASSERT_DOUBLE_EQ(trade->pnl, 5000.0, "Short profit PnL");
  ASSERT(trade->quantity == -500, "Short trade quantity");

  samrena_destroy(arena);
  printf("  PASS\n");
  return 0;
}

static int test_exit_short_loss(void) {
  printf("Testing exit short loss...\n");

  Samrena *arena = samrena_create_default();
  SamtraderPortfolio *portfolio = samtrader_portfolio_create(arena, 100000.0);

  samtrader_execution_enter_short(portfolio, arena, "AAPL", "US", 100.0, 1704067200, 0.5, 0.0, 0.0,
                                  10, 0.0, 0.0, 0.0);
  /* Cash = 150000, -500 shares at $100 */

  /* Price rises to $110 → loss for short */
  bool result =
      samtrader_execution_exit_position(portfolio, arena, "AAPL", 110.0, 1704672000, 0.0, 0.0, 0.0);
  ASSERT(result, "Exit short failed");

  /* Cash = 150000 - 500*110 = 150000 - 55000 = 95000 */
  ASSERT_DOUBLE_EQ(portfolio->cash, 95000.0, "Cash after short loss exit");

  const SamtraderClosedTrade *trade =
      (const SamtraderClosedTrade *)samrena_vector_at_const(portfolio->closed_trades, 0);
  /* PnL = -500 * (110-100) = -5000 */
  ASSERT_DOUBLE_EQ(trade->pnl, -5000.0, "Short loss PnL");

  samrena_destroy(arena);
  printf("  PASS\n");
  return 0;
}

static int test_exit_with_slippage(void) {
  printf("Testing exit with slippage...\n");

  Samrena *arena = samrena_create_default();
  SamtraderPortfolio *portfolio = samtrader_portfolio_create(arena, 100000.0);

  /* Enter long at $100, no slippage */
  samtrader_execution_enter_long(portfolio, arena, "AAPL", "US", 100.0, 1704067200, 0.5, 0.0, 0.0,
                                 10, 0.0, 0.0, 0.0);
  /* Cash = 50000, 500 shares */

  /* Exit at $110 with 0.5% slippage (sell → price decreases) → exec $109.45 */
  bool result =
      samtrader_execution_exit_position(portfolio, arena, "AAPL", 110.0, 1704672000, 0.0, 0.0, 0.5);
  ASSERT(result, "Exit with slippage failed");

  /* exec_price = 110 * (1 - 0.005) = 109.45 */
  /* Cash = 50000 + 500*109.45 = 50000 + 54725 = 104725 */
  ASSERT_DOUBLE_EQ(portfolio->cash, 104725.0, "Cash after slipped exit");

  const SamtraderClosedTrade *trade =
      (const SamtraderClosedTrade *)samrena_vector_at_const(portfolio->closed_trades, 0);
  ASSERT_DOUBLE_EQ(trade->exit_price, 109.45, "Slipped exit price");
  /* PnL = 500*(109.45-100) = 4725 */
  ASSERT_DOUBLE_EQ(trade->pnl, 4725.0, "PnL with exit slippage");

  samrena_destroy(arena);
  printf("  PASS\n");
  return 0;
}

static int test_exit_with_commission(void) {
  printf("Testing exit with commission...\n");

  Samrena *arena = samrena_create_default();
  SamtraderPortfolio *portfolio = samtrader_portfolio_create(arena, 100000.0);

  /* Enter long at $100, no slippage, no commission on entry for simpler math */
  samtrader_execution_enter_long(portfolio, arena, "AAPL", "US", 100.0, 1704067200, 0.5, 0.0, 0.0,
                                 10, 0.0, 0.0, 0.0);
  /* Cash = 50000, 500 shares at $100 */

  /* Exit at $110 with $9.95 + 0.1% commission */
  bool result = samtrader_execution_exit_position(portfolio, arena, "AAPL", 110.0, 1704672000, 9.95,
                                                  0.1, 0.0);
  ASSERT(result, "Exit with commission failed");

  /* exit_trade_value = 500*110 = 55000 */
  /* exit_commission = 9.95 + 55000*0.1/100 = 9.95 + 55 = 64.95 */
  /* Cash = 50000 + 55000 - 64.95 = 104935.05 */
  ASSERT_DOUBLE_EQ(portfolio->cash, 104935.05, "Cash after exit commission");

  /* entry_commission = 9.95 + 500*100*0.1/100 = 9.95 + 50 = 59.95 */
  /* PnL = 500*(110-100) - 59.95 - 64.95 = 5000 - 124.90 = 4875.10 */
  const SamtraderClosedTrade *trade =
      (const SamtraderClosedTrade *)samrena_vector_at_const(portfolio->closed_trades, 0);
  ASSERT_DOUBLE_EQ(trade->pnl, 4875.10, "PnL including round-trip commissions");

  samrena_destroy(arena);
  printf("  PASS\n");
  return 0;
}

static int test_exit_nonexistent(void) {
  printf("Testing exit nonexistent...\n");

  Samrena *arena = samrena_create_default();
  SamtraderPortfolio *portfolio = samtrader_portfolio_create(arena, 100000.0);

  bool result =
      samtrader_execution_exit_position(portfolio, arena, "AAPL", 110.0, 1704672000, 0.0, 0.0, 0.0);
  ASSERT(!result, "Exit nonexistent should fail");

  samrena_destroy(arena);
  printf("  PASS\n");
  return 0;
}

static int test_exit_null_params(void) {
  printf("Testing exit null params...\n");

  Samrena *arena = samrena_create_default();
  SamtraderPortfolio *portfolio = samtrader_portfolio_create(arena, 100000.0);

  ASSERT(!samtrader_execution_exit_position(NULL, arena, "AAPL", 100.0, 0, 0, 0, 0),
         "NULL portfolio");
  ASSERT(!samtrader_execution_exit_position(portfolio, NULL, "AAPL", 100.0, 0, 0, 0, 0),
         "NULL arena");
  ASSERT(!samtrader_execution_exit_position(portfolio, arena, NULL, 100.0, 0, 0, 0, 0),
         "NULL code");

  samrena_destroy(arena);
  printf("  PASS\n");
  return 0;
}

/* ========== Trigger Scanning ========== */

static int test_trigger_stop_loss(void) {
  printf("Testing trigger stop loss...\n");

  Samrena *arena = samrena_create_default();
  SamtraderPortfolio *portfolio = samtrader_portfolio_create(arena, 100000.0);

  /* Enter long with 5% SL */
  samtrader_execution_enter_long(portfolio, arena, "AAPL", "US", 100.0, 1704067200, 0.5, 5.0, 0.0,
                                 10, 0.0, 0.0, 0.0);
  /* SL at $95 */

  SamHashMap *price_map = samhashmap_create(16, arena);
  double aapl_price = 94.0; /* Below SL */
  samhashmap_put(price_map, "AAPL", &aapl_price);

  int exits =
      samtrader_execution_check_triggers(portfolio, arena, price_map, 1704672000, 0.0, 0.0, 0.0);
  ASSERT(exits == 1, "Should exit 1 position");
  ASSERT(!samtrader_portfolio_has_position(portfolio, "AAPL"), "AAPL should be exited");

  samrena_destroy(arena);
  printf("  PASS\n");
  return 0;
}

static int test_trigger_take_profit(void) {
  printf("Testing trigger take profit...\n");

  Samrena *arena = samrena_create_default();
  SamtraderPortfolio *portfolio = samtrader_portfolio_create(arena, 100000.0);

  /* Enter long with 10% TP */
  samtrader_execution_enter_long(portfolio, arena, "AAPL", "US", 100.0, 1704067200, 0.5, 0.0, 10.0,
                                 10, 0.0, 0.0, 0.0);
  /* TP at $110 */

  SamHashMap *price_map = samhashmap_create(16, arena);
  double aapl_price = 112.0; /* Above TP */
  samhashmap_put(price_map, "AAPL", &aapl_price);

  int exits =
      samtrader_execution_check_triggers(portfolio, arena, price_map, 1704672000, 0.0, 0.0, 0.0);
  ASSERT(exits == 1, "Should exit 1 position");
  ASSERT(!samtrader_portfolio_has_position(portfolio, "AAPL"), "AAPL should be exited");

  samrena_destroy(arena);
  printf("  PASS\n");
  return 0;
}

static int test_trigger_no_triggers(void) {
  printf("Testing no triggers...\n");

  Samrena *arena = samrena_create_default();
  SamtraderPortfolio *portfolio = samtrader_portfolio_create(arena, 100000.0);

  /* Enter long with 5% SL, 10% TP */
  samtrader_execution_enter_long(portfolio, arena, "AAPL", "US", 100.0, 1704067200, 0.5, 5.0, 10.0,
                                 10, 0.0, 0.0, 0.0);

  SamHashMap *price_map = samhashmap_create(16, arena);
  double aapl_price = 102.0; /* Between SL and TP */
  samhashmap_put(price_map, "AAPL", &aapl_price);

  int exits =
      samtrader_execution_check_triggers(portfolio, arena, price_map, 1704672000, 0.0, 0.0, 0.0);
  ASSERT(exits == 0, "Should exit 0 positions");
  ASSERT(samtrader_portfolio_has_position(portfolio, "AAPL"), "AAPL should remain");

  samrena_destroy(arena);
  printf("  PASS\n");
  return 0;
}

static int test_trigger_multiple(void) {
  printf("Testing multiple triggers...\n");

  Samrena *arena = samrena_create_default();
  SamtraderPortfolio *portfolio = samtrader_portfolio_create(arena, 100000.0);

  /* Enter two longs with SL */
  samtrader_execution_enter_long(portfolio, arena, "AAPL", "US", 100.0, 1704067200, 0.25, 5.0, 0.0,
                                 10, 0.0, 0.0, 0.0);
  samtrader_execution_enter_long(portfolio, arena, "BHP", "AU", 50.0, 1704067200, 0.25, 5.0, 0.0,
                                 10, 0.0, 0.0, 0.0);

  SamHashMap *price_map = samhashmap_create(16, arena);
  double aapl_price = 93.0; /* Below AAPL SL of $95 */
  double bhp_price = 46.0;  /* Below BHP SL of $47.50 */
  samhashmap_put(price_map, "AAPL", &aapl_price);
  samhashmap_put(price_map, "BHP", &bhp_price);

  int exits =
      samtrader_execution_check_triggers(portfolio, arena, price_map, 1704672000, 0.0, 0.0, 0.0);
  ASSERT(exits == 2, "Should exit 2 positions");
  ASSERT(!samtrader_portfolio_has_position(portfolio, "AAPL"), "AAPL should be exited");
  ASSERT(!samtrader_portfolio_has_position(portfolio, "BHP"), "BHP should be exited");

  samrena_destroy(arena);
  printf("  PASS\n");
  return 0;
}

static int test_trigger_short_stop_loss(void) {
  printf("Testing short stop loss trigger...\n");

  Samrena *arena = samrena_create_default();
  SamtraderPortfolio *portfolio = samtrader_portfolio_create(arena, 100000.0);

  /* Enter short with 5% SL → SL at $105 */
  samtrader_execution_enter_short(portfolio, arena, "AAPL", "US", 100.0, 1704067200, 0.5, 5.0, 0.0,
                                  10, 0.0, 0.0, 0.0);

  SamHashMap *price_map = samhashmap_create(16, arena);
  double aapl_price = 106.0; /* Above short SL */
  samhashmap_put(price_map, "AAPL", &aapl_price);

  int exits =
      samtrader_execution_check_triggers(portfolio, arena, price_map, 1704672000, 0.0, 0.0, 0.0);
  ASSERT(exits == 1, "Should exit 1 position");
  ASSERT(!samtrader_portfolio_has_position(portfolio, "AAPL"), "AAPL should be exited");

  samrena_destroy(arena);
  printf("  PASS\n");
  return 0;
}

static int test_trigger_no_stops_set(void) {
  printf("Testing triggers with no stops set...\n");

  Samrena *arena = samrena_create_default();
  SamtraderPortfolio *portfolio = samtrader_portfolio_create(arena, 100000.0);

  /* Enter long with no SL/TP */
  samtrader_execution_enter_long(portfolio, arena, "AAPL", "US", 100.0, 1704067200, 0.5, 0.0, 0.0,
                                 10, 0.0, 0.0, 0.0);

  SamHashMap *price_map = samhashmap_create(16, arena);
  double aapl_price = 50.0; /* Even huge drop shouldn't trigger */
  samhashmap_put(price_map, "AAPL", &aapl_price);

  int exits =
      samtrader_execution_check_triggers(portfolio, arena, price_map, 1704672000, 0.0, 0.0, 0.0);
  ASSERT(exits == 0, "Should exit 0 (no stops set)");
  ASSERT(samtrader_portfolio_has_position(portfolio, "AAPL"), "AAPL should remain");

  samrena_destroy(arena);
  printf("  PASS\n");
  return 0;
}

static int test_trigger_null_params(void) {
  printf("Testing trigger null params...\n");

  Samrena *arena = samrena_create_default();
  SamtraderPortfolio *portfolio = samtrader_portfolio_create(arena, 100000.0);
  SamHashMap *price_map = samhashmap_create(16, arena);

  ASSERT(samtrader_execution_check_triggers(NULL, arena, price_map, 0, 0, 0, 0) == -1,
         "NULL portfolio");
  ASSERT(samtrader_execution_check_triggers(portfolio, NULL, price_map, 0, 0, 0, 0) == -1,
         "NULL arena");
  ASSERT(samtrader_execution_check_triggers(portfolio, arena, NULL, 0, 0, 0, 0) == -1,
         "NULL price_map");

  samrena_destroy(arena);
  printf("  PASS\n");
  return 0;
}

/* ========== Round-Trip Tests ========== */

static int test_round_trip_long(void) {
  printf("Testing long round-trip cash check...\n");

  Samrena *arena = samrena_create_default();
  SamtraderPortfolio *portfolio = samtrader_portfolio_create(arena, 100000.0);

  /* Enter long at $100, no commission/slippage, 100% of cash */
  samtrader_execution_enter_long(portfolio, arena, "AAPL", "US", 100.0, 1704067200, 1.0, 0.0, 0.0,
                                 10, 0.0, 0.0, 0.0);
  ASSERT_DOUBLE_EQ(portfolio->cash, 0.0, "All cash invested");

  /* Exit at same price → cash should return to original */
  samtrader_execution_exit_position(portfolio, arena, "AAPL", 100.0, 1704672000, 0.0, 0.0, 0.0);
  ASSERT_DOUBLE_EQ(portfolio->cash, 100000.0, "Cash restored after break-even round-trip");

  samrena_destroy(arena);
  printf("  PASS\n");
  return 0;
}

static int test_round_trip_short(void) {
  printf("Testing short round-trip cash check...\n");

  Samrena *arena = samrena_create_default();
  SamtraderPortfolio *portfolio = samtrader_portfolio_create(arena, 100000.0);

  /* Enter short at $100, no commission/slippage, 50% of cash */
  samtrader_execution_enter_short(portfolio, arena, "AAPL", "US", 100.0, 1704067200, 0.5, 0.0, 0.0,
                                  10, 0.0, 0.0, 0.0);
  double cash_after_entry = portfolio->cash;
  /* Cash = 100000 + 50000 = 150000 */
  ASSERT_DOUBLE_EQ(cash_after_entry, 150000.0, "Cash after short entry");

  /* Exit at same price → net zero */
  samtrader_execution_exit_position(portfolio, arena, "AAPL", 100.0, 1704672000, 0.0, 0.0, 0.0);
  /* Cash = 150000 - 500*100 = 150000 - 50000 = 100000 */
  ASSERT_DOUBLE_EQ(portfolio->cash, 100000.0, "Cash restored after short break-even");

  samrena_destroy(arena);
  printf("  PASS\n");
  return 0;
}

static int test_round_trip_multiple(void) {
  printf("Testing multiple positions round-trip...\n");

  Samrena *arena = samrena_create_default();
  SamtraderPortfolio *portfolio = samtrader_portfolio_create(arena, 100000.0);

  /* Enter two positions, no commission/slippage */
  samtrader_execution_enter_long(portfolio, arena, "AAPL", "US", 100.0, 1704067200, 0.5, 0.0, 0.0,
                                 10, 0.0, 0.0, 0.0);
  samtrader_execution_enter_long(portfolio, arena, "BHP", "AU", 50.0, 1704067200, 0.5, 0.0, 0.0, 10,
                                 0.0, 0.0, 0.0);

  /* AAPL: 500 shares at $100, BHP: 500 shares at $50 */
  /* Cash = 100000 - 50000 - 25000 = 25000 */
  ASSERT_DOUBLE_EQ(portfolio->cash, 25000.0, "Cash after two entries");

  /* Exit both at entry prices → cash should be 100000 */
  samtrader_execution_exit_position(portfolio, arena, "AAPL", 100.0, 1704672000, 0.0, 0.0, 0.0);
  samtrader_execution_exit_position(portfolio, arena, "BHP", 50.0, 1704672000, 0.0, 0.0, 0.0);

  ASSERT_DOUBLE_EQ(portfolio->cash, 100000.0, "Cash restored after both exits");
  ASSERT(samrena_vector_size(portfolio->closed_trades) == 2, "Should have 2 closed trades");

  samrena_destroy(arena);
  printf("  PASS\n");
  return 0;
}

int main(void) {
  printf("=== Execution Tests ===\n\n");

  int failures = 0;

  /* Calc helpers */
  failures += test_commission_flat_only();
  failures += test_commission_pct_only();
  failures += test_commission_combined();
  failures += test_commission_zero();
  failures += test_commission_zero_trade_value();
  failures += test_slippage_up();
  failures += test_slippage_down();
  failures += test_slippage_zero();
  failures += test_quantity_exact();
  failures += test_quantity_fractional();
  failures += test_quantity_zero_price();
  failures += test_quantity_zero_capital();

  /* Enter long */
  failures += test_enter_long_basic();
  failures += test_enter_long_with_slippage();
  failures += test_enter_long_with_stops();
  failures += test_enter_long_with_commission();
  failures += test_enter_long_insufficient_funds();
  failures += test_enter_long_max_positions();
  failures += test_enter_long_duplicate_code();
  failures += test_enter_long_null_params();

  /* Enter short */
  failures += test_enter_short_basic();
  failures += test_enter_short_with_slippage();
  failures += test_enter_short_with_stops();

  /* Exit */
  failures += test_exit_long_profit();
  failures += test_exit_long_loss();
  failures += test_exit_short_profit();
  failures += test_exit_short_loss();
  failures += test_exit_with_slippage();
  failures += test_exit_with_commission();
  failures += test_exit_nonexistent();
  failures += test_exit_null_params();

  /* Triggers */
  failures += test_trigger_stop_loss();
  failures += test_trigger_take_profit();
  failures += test_trigger_no_triggers();
  failures += test_trigger_multiple();
  failures += test_trigger_short_stop_loss();
  failures += test_trigger_no_stops_set();
  failures += test_trigger_null_params();

  /* Round-trip */
  failures += test_round_trip_long();
  failures += test_round_trip_short();
  failures += test_round_trip_multiple();

  printf("\n=== Results: %d failures ===\n", failures);

  return failures > 0 ? EXIT_FAILURE : EXIT_SUCCESS;
}
