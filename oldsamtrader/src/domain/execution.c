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

#include "samtrader/domain/execution.h"

#include <math.h>
#include <string.h>

#include <samvector.h>

double samtrader_execution_calc_commission(double trade_value, double flat_fee, double pct) {
  return flat_fee + (trade_value * pct / 100.0);
}

double samtrader_execution_apply_slippage(double price, double slippage_pct, bool price_increases) {
  if (price_increases) {
    return price * (1.0 + slippage_pct / 100.0);
  }
  return price * (1.0 - slippage_pct / 100.0);
}

int64_t samtrader_execution_calc_quantity(double available_capital, double price_per_share) {
  if (price_per_share <= 0.0 || available_capital <= 0.0) {
    return 0;
  }
  return (int64_t)floor(available_capital / price_per_share);
}

bool samtrader_execution_enter_long(SamtraderPortfolio *portfolio, Samrena *arena, const char *code,
                                    const char *exchange, double market_price, time_t date,
                                    double position_size_frac, double stop_loss_pct,
                                    double take_profit_pct, int max_positions,
                                    double commission_flat, double commission_pct,
                                    double slippage_pct) {
  if (!portfolio || !arena || !code || !exchange) {
    return false;
  }

  if (samtrader_portfolio_has_position(portfolio, code)) {
    return false;
  }

  if ((int)samtrader_portfolio_position_count(portfolio) >= max_positions) {
    return false;
  }

  double exec_price = samtrader_execution_apply_slippage(market_price, slippage_pct, true);
  double available_capital = portfolio->cash * position_size_frac;
  int64_t qty = samtrader_execution_calc_quantity(available_capital, exec_price);
  if (qty <= 0) {
    return false;
  }

  double trade_value = (double)qty * exec_price;
  double commission =
      samtrader_execution_calc_commission(trade_value, commission_flat, commission_pct);

  if (trade_value + commission > portfolio->cash) {
    return false;
  }

  double stop_loss = 0.0;
  if (stop_loss_pct > 0.0) {
    stop_loss = exec_price * (1.0 - stop_loss_pct / 100.0);
  }

  double take_profit = 0.0;
  if (take_profit_pct > 0.0) {
    take_profit = exec_price * (1.0 + take_profit_pct / 100.0);
  }

  SamtraderPosition *pos = samtrader_position_create(arena, code, exchange, qty, exec_price, date,
                                                     stop_loss, take_profit);
  if (!pos) {
    return false;
  }

  if (!samtrader_portfolio_add_position(portfolio, arena, pos)) {
    return false;
  }

  portfolio->cash -= (trade_value + commission);
  return true;
}

bool samtrader_execution_enter_short(SamtraderPortfolio *portfolio, Samrena *arena,
                                     const char *code, const char *exchange, double market_price,
                                     time_t date, double position_size_frac, double stop_loss_pct,
                                     double take_profit_pct, int max_positions,
                                     double commission_flat, double commission_pct,
                                     double slippage_pct) {
  if (!portfolio || !arena || !code || !exchange) {
    return false;
  }

  if (samtrader_portfolio_has_position(portfolio, code)) {
    return false;
  }

  if ((int)samtrader_portfolio_position_count(portfolio) >= max_positions) {
    return false;
  }

  double exec_price = samtrader_execution_apply_slippage(market_price, slippage_pct, false);
  double available_capital = portfolio->cash * position_size_frac;
  int64_t qty = samtrader_execution_calc_quantity(available_capital, exec_price);
  if (qty <= 0) {
    return false;
  }

  double trade_value = (double)qty * exec_price;
  double commission =
      samtrader_execution_calc_commission(trade_value, commission_flat, commission_pct);

  if (commission > portfolio->cash) {
    return false;
  }

  double stop_loss = 0.0;
  if (stop_loss_pct > 0.0) {
    stop_loss = exec_price * (1.0 + stop_loss_pct / 100.0);
  }

  double take_profit = 0.0;
  if (take_profit_pct > 0.0) {
    take_profit = exec_price * (1.0 - take_profit_pct / 100.0);
  }

  SamtraderPosition *pos = samtrader_position_create(arena, code, exchange, -qty, exec_price, date,
                                                     stop_loss, take_profit);
  if (!pos) {
    return false;
  }

  if (!samtrader_portfolio_add_position(portfolio, arena, pos)) {
    return false;
  }

  portfolio->cash += (trade_value - commission);
  return true;
}

bool samtrader_execution_exit_position(SamtraderPortfolio *portfolio, Samrena *arena,
                                       const char *code, double market_price, time_t date,
                                       double commission_flat, double commission_pct,
                                       double slippage_pct) {
  if (!portfolio || !arena || !code) {
    return false;
  }

  SamtraderPosition *pos = samtrader_portfolio_get_position(portfolio, code);
  if (!pos) {
    return false;
  }

  bool is_long = samtrader_position_is_long(pos);
  int64_t abs_qty = pos->quantity >= 0 ? pos->quantity : -pos->quantity;

  /* Long exit: sell → slippage down. Short exit: cover/buy → slippage up */
  double exec_price = samtrader_execution_apply_slippage(market_price, slippage_pct, !is_long);

  double exit_trade_value = (double)abs_qty * exec_price;
  double exit_commission =
      samtrader_execution_calc_commission(exit_trade_value, commission_flat, commission_pct);

  double entry_trade_value = (double)abs_qty * pos->entry_price;
  double entry_commission =
      samtrader_execution_calc_commission(entry_trade_value, commission_flat, commission_pct);

  /* PnL = qty * (exit_price - entry_price) - entry_commission - exit_commission */
  double pnl =
      (double)pos->quantity * (exec_price - pos->entry_price) - entry_commission - exit_commission;

  /* Update cash */
  if (is_long) {
    portfolio->cash += (exit_trade_value - exit_commission);
  } else {
    portfolio->cash -= (exit_trade_value + exit_commission);
  }

  SamtraderClosedTrade trade = {.code = pos->code,
                                .exchange = pos->exchange,
                                .quantity = pos->quantity,
                                .entry_price = pos->entry_price,
                                .exit_price = exec_price,
                                .entry_date = pos->entry_date,
                                .exit_date = date,
                                .pnl = pnl};

  if (!samtrader_portfolio_record_trade(portfolio, arena, &trade)) {
    return false;
  }

  return samtrader_portfolio_remove_position(portfolio, code);
}

typedef struct {
  const SamHashMap *price_map;
  SamrenaVector *triggered_codes;
} TriggerCheckCtx;

static void trigger_check_iterator(const char *key, void *value, void *user_data) {
  (void)key;
  TriggerCheckCtx *ctx = (TriggerCheckCtx *)user_data;
  SamtraderPosition *pos = (SamtraderPosition *)value;

  double *price = (double *)samhashmap_get(ctx->price_map, pos->code);
  if (!price) {
    return;
  }

  if (samtrader_position_should_stop_loss(pos, *price) ||
      samtrader_position_should_take_profit(pos, *price)) {
    samrena_vector_push(ctx->triggered_codes, &pos->code);
  }
}

int samtrader_execution_check_triggers(SamtraderPortfolio *portfolio, Samrena *arena,
                                       const SamHashMap *price_map, time_t date,
                                       double commission_flat, double commission_pct,
                                       double slippage_pct) {
  if (!portfolio || !arena || !price_map) {
    return -1;
  }

  SamrenaVector *triggered = samrena_vector_init(arena, sizeof(const char *), 8);
  if (!triggered) {
    return -1;
  }

  TriggerCheckCtx ctx = {.price_map = price_map, .triggered_codes = triggered};
  samhashmap_foreach(portfolio->positions, trigger_check_iterator, &ctx);

  int exit_count = 0;
  size_t num_triggered = samrena_vector_size(triggered);
  for (size_t i = 0; i < num_triggered; i++) {
    const char **code_ptr = (const char **)samrena_vector_at(triggered, i);
    if (!code_ptr) {
      continue;
    }

    double *price = (double *)samhashmap_get(price_map, *code_ptr);
    if (!price) {
      continue;
    }

    if (samtrader_execution_exit_position(portfolio, arena, *code_ptr, *price, date,
                                          commission_flat, commission_pct, slippage_pct)) {
      exit_count++;
    }
  }

  return exit_count;
}
