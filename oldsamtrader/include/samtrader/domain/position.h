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

#ifndef SAMTRADER_DOMAIN_POSITION_H
#define SAMTRADER_DOMAIN_POSITION_H

#include <stdbool.h>
#include <stdint.h>
#include <time.h>

#include <samrena.h>

/**
 * @brief Represents an open position in the portfolio.
 *
 * Quantity is signed: positive = long, negative = short.
 * Stop loss and take profit are 0 if not set.
 * All strings are arena-allocated and owned by the arena.
 */
typedef struct {
  const char *code;     /**< Stock symbol (e.g., "AAPL", "BHP") */
  const char *exchange; /**< Exchange identifier ("US", "AU") */
  int64_t quantity;     /**< Position size (positive = long, negative = short) */
  double entry_price;   /**< Average entry price */
  time_t entry_date;    /**< Date position was opened */
  double stop_loss;     /**< Stop loss price (0 if not set) */
  double take_profit;   /**< Take profit price (0 if not set) */
} SamtraderPosition;

/**
 * @brief Create a position with arena-allocated strings.
 *
 * @param arena Memory arena for allocation
 * @param code Stock symbol (will be copied to arena)
 * @param exchange Exchange identifier (will be copied to arena)
 * @param quantity Position size (positive = long, negative = short)
 * @param entry_price Average entry price
 * @param entry_date Date position was opened
 * @param stop_loss Stop loss price (0 if not set)
 * @param take_profit Take profit price (0 if not set)
 * @return Pointer to the created position, or NULL on failure
 */
SamtraderPosition *samtrader_position_create(Samrena *arena, const char *code, const char *exchange,
                                             int64_t quantity, double entry_price,
                                             time_t entry_date, double stop_loss,
                                             double take_profit);

/**
 * @brief Check if a position is long (positive quantity).
 *
 * @param position Position to check
 * @return true if quantity > 0, false otherwise
 */
bool samtrader_position_is_long(const SamtraderPosition *position);

/**
 * @brief Check if a position is short (negative quantity).
 *
 * @param position Position to check
 * @return true if quantity < 0, false otherwise
 */
bool samtrader_position_is_short(const SamtraderPosition *position);

/**
 * @brief Calculate the market value of a position at a given price.
 *
 * Market value = |quantity| * current_price
 *
 * @param position Position to evaluate
 * @param current_price Current market price
 * @return Market value, or 0.0 if position is NULL
 */
double samtrader_position_market_value(const SamtraderPosition *position, double current_price);

/**
 * @brief Calculate unrealized profit/loss at a given price.
 *
 * For long positions: quantity * (current_price - entry_price)
 * For short positions: quantity * (current_price - entry_price)
 * (negative quantity makes this positive when price falls)
 *
 * @param position Position to evaluate
 * @param current_price Current market price
 * @return Unrealized P&L, or 0.0 if position is NULL
 */
double samtrader_position_unrealized_pnl(const SamtraderPosition *position, double current_price);

/**
 * @brief Check if the stop loss has been triggered.
 *
 * For long positions: triggered when current_price <= stop_loss
 * For short positions: triggered when current_price >= stop_loss
 * Always returns false if stop_loss is 0 (not set).
 *
 * @param position Position to check
 * @param current_price Current market price
 * @return true if stop loss is triggered, false otherwise
 */
bool samtrader_position_should_stop_loss(const SamtraderPosition *position, double current_price);

/**
 * @brief Check if the take profit has been triggered.
 *
 * For long positions: triggered when current_price >= take_profit
 * For short positions: triggered when current_price <= take_profit
 * Always returns false if take_profit is 0 (not set).
 *
 * @param position Position to check
 * @param current_price Current market price
 * @return true if take profit is triggered, false otherwise
 */
bool samtrader_position_should_take_profit(const SamtraderPosition *position, double current_price);

#endif /* SAMTRADER_DOMAIN_POSITION_H */
