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

#ifndef SAMTRADER_DOMAIN_EXECUTION_H
#define SAMTRADER_DOMAIN_EXECUTION_H

#include <stdbool.h>
#include <stdint.h>
#include <time.h>

#include <samdata/samhashmap.h>
#include <samrena.h>

#include "samtrader/domain/portfolio.h"

/**
 * @brief Calculate commission for a trade.
 *
 * Commission = flat_fee + (trade_value * pct / 100.0)
 *
 * @param trade_value Total value of the trade
 * @param flat_fee Flat commission fee
 * @param pct Commission percentage (e.g. 0.1 means 0.1%)
 * @return Commission amount
 */
double samtrader_execution_calc_commission(double trade_value, double flat_fee, double pct);

/**
 * @brief Apply slippage to a price.
 *
 * When price_increases is true: price * (1 + slippage_pct / 100)
 * When price_increases is false: price * (1 - slippage_pct / 100)
 *
 * @param price Base price
 * @param slippage_pct Slippage percentage
 * @param price_increases true if slippage increases price (buying), false if decreases (selling)
 * @return Adjusted price after slippage
 */
double samtrader_execution_apply_slippage(double price, double slippage_pct, bool price_increases);

/**
 * @brief Calculate the number of whole shares affordable at a given price.
 *
 * @param available_capital Capital available for position sizing
 * @param price_per_share Price per share
 * @return Number of whole shares (floored), or 0 if inputs are invalid
 */
int64_t samtrader_execution_calc_quantity(double available_capital, double price_per_share);

/**
 * @brief Enter a long position.
 *
 * Applies slippage (price increases for buys), calculates quantity from
 * position_size_frac of portfolio cash, deducts cost + commission from cash,
 * and adds the position to the portfolio.
 *
 * @param portfolio Target portfolio
 * @param arena Memory arena for allocation
 * @param code Stock symbol
 * @param exchange Exchange identifier
 * @param market_price Current market price
 * @param date Trade date
 * @param position_size_frac Fraction of cash to allocate (0.0 to 1.0)
 * @param stop_loss_pct Stop loss percentage below entry (0 to disable)
 * @param take_profit_pct Take profit percentage above entry (0 to disable)
 * @param max_positions Maximum number of open positions allowed
 * @param commission_flat Flat commission fee
 * @param commission_pct Commission percentage
 * @param slippage_pct Slippage percentage
 * @return true on success, false on failure
 */
bool samtrader_execution_enter_long(SamtraderPortfolio *portfolio, Samrena *arena, const char *code,
                                    const char *exchange, double market_price, time_t date,
                                    double position_size_frac, double stop_loss_pct,
                                    double take_profit_pct, int max_positions,
                                    double commission_flat, double commission_pct,
                                    double slippage_pct);

/**
 * @brief Enter a short position.
 *
 * Applies slippage (price decreases for short sells), calculates quantity,
 * adds proceeds minus commission to cash, and adds the position to the portfolio.
 *
 * @param portfolio Target portfolio
 * @param arena Memory arena for allocation
 * @param code Stock symbol
 * @param exchange Exchange identifier
 * @param market_price Current market price
 * @param date Trade date
 * @param position_size_frac Fraction of cash to allocate (0.0 to 1.0)
 * @param stop_loss_pct Stop loss percentage above entry (0 to disable)
 * @param take_profit_pct Take profit percentage below entry (0 to disable)
 * @param max_positions Maximum number of open positions allowed
 * @param commission_flat Flat commission fee
 * @param commission_pct Commission percentage
 * @param slippage_pct Slippage percentage
 * @return true on success, false on failure
 */
bool samtrader_execution_enter_short(SamtraderPortfolio *portfolio, Samrena *arena,
                                     const char *code, const char *exchange, double market_price,
                                     time_t date, double position_size_frac, double stop_loss_pct,
                                     double take_profit_pct, int max_positions,
                                     double commission_flat, double commission_pct,
                                     double slippage_pct);

/**
 * @brief Exit an existing position.
 *
 * Determines direction from the position's quantity sign, applies appropriate
 * slippage, calculates commission, computes PnL (including round-trip commissions),
 * updates cash, records the closed trade, and removes the position.
 *
 * @param portfolio Target portfolio
 * @param arena Memory arena for allocation
 * @param code Stock symbol of the position to exit
 * @param market_price Current market price
 * @param date Trade date
 * @param commission_flat Flat commission fee
 * @param commission_pct Commission percentage
 * @param slippage_pct Slippage percentage
 * @return true on success, false if position not found or on error
 */
bool samtrader_execution_exit_position(SamtraderPortfolio *portfolio, Samrena *arena,
                                       const char *code, double market_price, time_t date,
                                       double commission_flat, double commission_pct,
                                       double slippage_pct);

/**
 * @brief Check all positions for stop loss / take profit triggers and exit triggered positions.
 *
 * Two-pass approach: first collects triggered position codes (can't modify hashmap during
 * iteration), then exits each triggered position.
 *
 * @param portfolio Target portfolio
 * @param arena Memory arena for allocation
 * @param price_map HashMap mapping stock codes to double* current prices
 * @param date Trade date
 * @param commission_flat Flat commission fee
 * @param commission_pct Commission percentage
 * @param slippage_pct Slippage percentage
 * @return Number of positions exited, or -1 on error
 */
int samtrader_execution_check_triggers(SamtraderPortfolio *portfolio, Samrena *arena,
                                       const SamHashMap *price_map, time_t date,
                                       double commission_flat, double commission_pct,
                                       double slippage_pct);

#endif /* SAMTRADER_DOMAIN_EXECUTION_H */
