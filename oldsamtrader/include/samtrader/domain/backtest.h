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

#ifndef SAMTRADER_DOMAIN_BACKTEST_H
#define SAMTRADER_DOMAIN_BACKTEST_H

#include <stdbool.h>
#include <time.h>

#include <samrena.h>
#include <samvector.h>

/**
 * @brief Backtest configuration parameters.
 */
typedef struct {
  time_t start_date;           /**< Backtest start date */
  time_t end_date;             /**< Backtest end date */
  double initial_capital;      /**< Starting capital */
  double commission_per_trade; /**< Flat fee per trade */
  double commission_pct;       /**< Percentage of trade value */
  double slippage_pct;         /**< Price slippage simulation */
  bool allow_shorting;         /**< Whether short selling is allowed */
} SamtraderBacktestConfig;

/**
 * @brief Backtest result containing performance metrics and trade data.
 *
 * Contains all performance statistics and raw data from a backtest run.
 * The equity_curve holds SamtraderEquityPoint entries and trades holds
 * SamtraderClosedTrade entries.
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
  SamrenaVector *equity_curve;   /**< Vector of SamtraderEquityPoint */
  SamrenaVector *trades;         /**< Vector of SamtraderClosedTrade */
} SamtraderBacktestResult;

/**
 * @brief Per-code trade statistics from a multi-code backtest.
 *
 * Aggregates trade metrics for a single stock code, providing a breakdown
 * of performance alongside the portfolio-level aggregate result.
 */
typedef struct {
  const char *code;     /**< Stock symbol */
  const char *exchange; /**< Exchange identifier */
  int total_trades;     /**< Total closed trades for this code */
  int winning_trades;   /**< Trades with positive PnL */
  int losing_trades;    /**< Trades with non-positive PnL */
  double total_pnl;     /**< Sum of all trade PnL */
  double win_rate;      /**< winning_trades / total_trades */
  double largest_win;   /**< Largest single trade PnL */
  double largest_loss;  /**< Most negative single trade PnL */
} SamtraderCodeResult;

/**
 * @brief Multi-code backtest result with per-code breakdown.
 *
 * Wraps the portfolio-level aggregate result and adds an array of
 * per-code trade statistics.
 */
typedef struct {
  SamtraderBacktestResult aggregate; /**< Portfolio-level metrics */
  SamtraderCodeResult *code_results; /**< Arena-allocated per-code results */
  size_t code_count;                 /**< Number of entries in code_results */
} SamtraderMultiCodeResult;

#endif /* SAMTRADER_DOMAIN_BACKTEST_H */
