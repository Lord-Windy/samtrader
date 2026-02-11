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

#ifndef SAMTRADER_DOMAIN_PORTFOLIO_H
#define SAMTRADER_DOMAIN_PORTFOLIO_H

#include <stdbool.h>
#include <stdint.h>
#include <time.h>

#include <samdata/samhashmap.h>
#include <samrena.h>
#include <samvector.h>

#include "samtrader/domain/position.h"

/**
 * @brief A closed (realized) trade record.
 *
 * Created when a position is fully or partially closed.
 * All strings are arena-allocated and owned by the arena.
 */
typedef struct {
  const char *code;     /**< Stock symbol */
  const char *exchange; /**< Exchange identifier */
  int64_t quantity;     /**< Trade quantity (positive = long, negative = short) */
  double entry_price;   /**< Entry price */
  double exit_price;    /**< Exit price */
  time_t entry_date;    /**< Date position was opened */
  time_t exit_date;     /**< Date position was closed */
  double pnl;           /**< Realized profit/loss */
} SamtraderClosedTrade;

/**
 * @brief A single point on the equity curve.
 *
 * Records portfolio equity at a given point in time.
 */
typedef struct {
  time_t date;   /**< Timestamp of the equity snapshot */
  double equity; /**< Total portfolio equity at this time */
} SamtraderEquityPoint;

/**
 * @brief Portfolio state during a backtest.
 *
 * Holds cash balance, open positions, closed trade history,
 * and the equity curve. All data is arena-allocated.
 */
typedef struct {
  double cash;                  /**< Available cash balance */
  double initial_capital;       /**< Starting capital */
  SamHashMap *positions;        /**< Open positions keyed by code (SamtraderPosition*) */
  SamrenaVector *closed_trades; /**< Vector of SamtraderClosedTrade */
  SamrenaVector *equity_curve;  /**< Vector of SamtraderEquityPoint */
} SamtraderPortfolio;

/**
 * @brief Create a new portfolio with initial capital.
 *
 * @param arena Memory arena for allocation
 * @param initial_capital Starting cash balance
 * @return Pointer to the created portfolio, or NULL on failure
 */
SamtraderPortfolio *samtrader_portfolio_create(Samrena *arena, double initial_capital);

/**
 * @brief Add a position to the portfolio.
 *
 * The position is keyed by its code field. If a position with the same
 * code already exists, it will be replaced.
 *
 * @param portfolio Target portfolio
 * @param arena Memory arena for string allocation
 * @param position Position to add (will be copied into the hashmap)
 * @return true on success, false on failure
 */
bool samtrader_portfolio_add_position(SamtraderPortfolio *portfolio, Samrena *arena,
                                      const SamtraderPosition *position);

/**
 * @brief Get a position by stock code.
 *
 * @param portfolio Portfolio to search
 * @param code Stock symbol to look up
 * @return Pointer to the position, or NULL if not found
 */
SamtraderPosition *samtrader_portfolio_get_position(const SamtraderPortfolio *portfolio,
                                                    const char *code);

/**
 * @brief Check if a position exists in the portfolio.
 *
 * @param portfolio Portfolio to search
 * @param code Stock symbol to look up
 * @return true if the position exists, false otherwise
 */
bool samtrader_portfolio_has_position(const SamtraderPortfolio *portfolio, const char *code);

/**
 * @brief Remove a position from the portfolio.
 *
 * @param portfolio Portfolio to modify
 * @param code Stock symbol to remove
 * @return true if the position was removed, false if not found
 */
bool samtrader_portfolio_remove_position(SamtraderPortfolio *portfolio, const char *code);

/**
 * @brief Get the number of open positions.
 *
 * @param portfolio Portfolio to query
 * @return Number of open positions, or 0 if portfolio is NULL
 */
size_t samtrader_portfolio_position_count(const SamtraderPortfolio *portfolio);

/**
 * @brief Record a closed trade in the portfolio history.
 *
 * @param portfolio Target portfolio
 * @param arena Memory arena for string allocation
 * @param trade Closed trade to record (will be copied)
 * @return true on success, false on failure
 */
bool samtrader_portfolio_record_trade(SamtraderPortfolio *portfolio, Samrena *arena,
                                      const SamtraderClosedTrade *trade);

/**
 * @brief Record an equity point on the equity curve.
 *
 * @param portfolio Target portfolio
 * @param arena Memory arena for allocation
 * @param date Timestamp of the equity snapshot
 * @param equity Total portfolio equity at this time
 * @return true on success, false on failure
 */
bool samtrader_portfolio_record_equity(SamtraderPortfolio *portfolio, Samrena *arena, time_t date,
                                       double equity);

/**
 * @brief Calculate total portfolio equity (cash + market value of positions).
 *
 * @param portfolio Portfolio to evaluate
 * @param price_map HashMap mapping stock codes to double* current prices
 * @return Total equity, or -1.0 on error
 */
double samtrader_portfolio_total_equity(const SamtraderPortfolio *portfolio,
                                        const SamHashMap *price_map);

#endif /* SAMTRADER_DOMAIN_PORTFOLIO_H */
