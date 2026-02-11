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

#ifndef SAMTRADER_DOMAIN_METRICS_H
#define SAMTRADER_DOMAIN_METRICS_H

#include <samrena.h>
#include <samvector.h>

#include "samtrader/domain/backtest.h"

/**
 * @brief Performance metrics computed from backtest results.
 *
 * Contains return metrics, risk metrics, and trade statistics
 * computed from closed trades and equity curve data.
 */
typedef struct {
  double total_return;           /**< (final - initial) / initial */
  double annualized_return;      /**< (1 + total_return)^(252/trading_days) - 1 */
  double sharpe_ratio;           /**< mean(daily_returns) / stddev(daily_returns) * sqrt(252) */
  double sortino_ratio;          /**< mean(daily_returns) / downside_dev * sqrt(252) */
  double max_drawdown;           /**< Largest peak-to-trough decline (fraction) */
  double max_drawdown_duration;  /**< Days of longest drawdown period */
  double win_rate;               /**< winning_trades / total_trades */
  double profit_factor;          /**< sum(winning_pnl) / abs(sum(losing_pnl)) */
  int total_trades;              /**< Total number of closed trades */
  int winning_trades;            /**< Number of trades with positive PnL */
  int losing_trades;             /**< Number of trades with non-positive PnL */
  double average_win;            /**< Mean PnL of winning trades */
  double average_loss;           /**< Mean PnL of losing trades (negative) */
  double largest_win;            /**< Largest single trade PnL */
  double largest_loss;           /**< Most negative single trade PnL */
  double average_trade_duration; /**< Mean days between entry and exit */
} SamtraderMetrics;

/**
 * @brief Calculate performance metrics from backtest results.
 *
 * Computes all performance statistics from closed trade history
 * and equity curve data. All memory is allocated from the provided arena.
 *
 * @param arena Memory arena for allocation
 * @param closed_trades Vector of SamtraderClosedTrade
 * @param equity_curve Vector of SamtraderEquityPoint
 * @param risk_free_rate Annual risk-free rate (e.g. 0.05 for 5%)
 * @return Pointer to computed metrics, or NULL on failure
 */
SamtraderMetrics *samtrader_metrics_calculate(Samrena *arena, const SamrenaVector *closed_trades,
                                              const SamrenaVector *equity_curve,
                                              double risk_free_rate);

/**
 * @brief Print metrics to stdout for debugging.
 *
 * @param metrics Metrics to display
 */
void samtrader_metrics_print(const SamtraderMetrics *metrics);

/**
 * @brief Compute per-code trade statistics from closed trades.
 *
 * Iterates all closed trades once and accumulates statistics for each
 * code in the universe. Returns an arena-allocated array of code_count
 * SamtraderCodeResult structs (zero-initialized, then populated).
 *
 * @param arena Memory arena for allocation
 * @param closed_trades Vector of SamtraderClosedTrade
 * @param codes Array of code strings (from universe)
 * @param exchange Exchange identifier
 * @param code_count Number of codes
 * @return Pointer to array of SamtraderCodeResult, or NULL on failure
 */
SamtraderCodeResult *samtrader_metrics_compute_per_code(Samrena *arena,
                                                        const SamrenaVector *closed_trades,
                                                        const char **codes, const char *exchange,
                                                        size_t code_count);

#endif /* SAMTRADER_DOMAIN_METRICS_H */
