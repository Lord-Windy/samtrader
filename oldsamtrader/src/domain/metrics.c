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

#include "samtrader/domain/metrics.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

#include "samtrader/domain/portfolio.h"

SamtraderMetrics *samtrader_metrics_calculate(Samrena *arena, const SamrenaVector *closed_trades,
                                              const SamrenaVector *equity_curve,
                                              double risk_free_rate) {
  if (!arena) {
    return NULL;
  }

  SamtraderMetrics *metrics = SAMRENA_PUSH_TYPE_ZERO(arena, SamtraderMetrics);
  if (!metrics) {
    return NULL;
  }

  /* ===== Trade Statistics ===== */
  size_t num_trades = closed_trades ? samrena_vector_size(closed_trades) : 0;
  metrics->total_trades = (int)num_trades;

  if (num_trades > 0) {
    double sum_wins = 0.0;
    double sum_losses = 0.0;
    double total_duration = 0.0;
    metrics->largest_win = 0.0;
    metrics->largest_loss = 0.0;

    for (size_t i = 0; i < num_trades; i++) {
      const SamtraderClosedTrade *trade =
          (const SamtraderClosedTrade *)samrena_vector_at_const(closed_trades, i);

      double duration = difftime(trade->exit_date, trade->entry_date) / 86400.0;
      total_duration += duration;

      if (trade->pnl > 0.0) {
        metrics->winning_trades++;
        sum_wins += trade->pnl;
        if (trade->pnl > metrics->largest_win) {
          metrics->largest_win = trade->pnl;
        }
      } else {
        metrics->losing_trades++;
        sum_losses += trade->pnl;
        if (trade->pnl < metrics->largest_loss) {
          metrics->largest_loss = trade->pnl;
        }
      }
    }

    metrics->win_rate = (double)metrics->winning_trades / (double)metrics->total_trades;
    metrics->average_trade_duration = total_duration / (double)num_trades;

    if (metrics->winning_trades > 0) {
      metrics->average_win = sum_wins / (double)metrics->winning_trades;
    }
    if (metrics->losing_trades > 0) {
      metrics->average_loss = sum_losses / (double)metrics->losing_trades;
    }

    if (sum_losses < 0.0) {
      metrics->profit_factor = sum_wins / (-sum_losses);
    } else {
      /* All winning or zero-loss trades */
      metrics->profit_factor = (sum_wins > 0.0) ? INFINITY : 0.0;
    }
  }

  /* ===== Return & Risk Metrics from Equity Curve ===== */
  size_t num_points = equity_curve ? samrena_vector_size(equity_curve) : 0;

  if (num_points >= 2) {
    const SamtraderEquityPoint *first =
        (const SamtraderEquityPoint *)samrena_vector_at_const(equity_curve, 0);
    const SamtraderEquityPoint *last =
        (const SamtraderEquityPoint *)samrena_vector_at_const(equity_curve, num_points - 1);

    double initial_equity = first->equity;
    double final_equity = last->equity;

    if (initial_equity > 0.0) {
      metrics->total_return = (final_equity - initial_equity) / initial_equity;
    }

    size_t trading_days = num_points - 1;
    if (trading_days > 0 && metrics->total_return > -1.0) {
      metrics->annualized_return = pow(1.0 + metrics->total_return, 252.0 / trading_days) - 1.0;
    }

    /* Compute daily returns */
    double *daily_returns = (double *)samrena_push(arena, trading_days * sizeof(double));
    if (!daily_returns) {
      return NULL;
    }

    double sum_returns = 0.0;
    for (size_t i = 0; i < trading_days; i++) {
      const SamtraderEquityPoint *prev =
          (const SamtraderEquityPoint *)samrena_vector_at_const(equity_curve, i);
      const SamtraderEquityPoint *curr =
          (const SamtraderEquityPoint *)samrena_vector_at_const(equity_curve, i + 1);
      if (prev->equity > 0.0) {
        daily_returns[i] = (curr->equity - prev->equity) / prev->equity;
      } else {
        daily_returns[i] = 0.0;
      }
      sum_returns += daily_returns[i];
    }

    double mean_return = sum_returns / (double)trading_days;
    double risk_free_daily = risk_free_rate / 252.0;

    /* Stddev of daily returns */
    double sum_sq = 0.0;
    double sum_downside_sq = 0.0;
    for (size_t i = 0; i < trading_days; i++) {
      double diff = daily_returns[i] - mean_return;
      sum_sq += diff * diff;

      double excess = daily_returns[i] - risk_free_daily;
      if (excess < 0.0) {
        sum_downside_sq += excess * excess;
      }
    }

    double stddev = sqrt(sum_sq / (double)trading_days);
    double downside_dev = sqrt(sum_downside_sq / (double)trading_days);

    if (stddev > 0.0) {
      metrics->sharpe_ratio = (mean_return - risk_free_daily) / stddev * sqrt(252.0);
    }
    if (downside_dev > 0.0) {
      metrics->sortino_ratio = (mean_return - risk_free_daily) / downside_dev * sqrt(252.0);
    }

    /* Max drawdown and max drawdown duration */
    double peak = first->equity;
    double max_dd = 0.0;
    size_t dd_start = 0;
    size_t max_dd_dur = 0;
    bool in_drawdown = false;

    for (size_t i = 1; i < num_points; i++) {
      const SamtraderEquityPoint *pt =
          (const SamtraderEquityPoint *)samrena_vector_at_const(equity_curve, i);

      if (pt->equity >= peak) {
        /* New peak - record duration of the drawdown that just ended */
        if (in_drawdown) {
          size_t dur = i - dd_start;
          if (dur > max_dd_dur) {
            max_dd_dur = dur;
          }
          in_drawdown = false;
        }
        peak = pt->equity;
        dd_start = i;
      } else if (!in_drawdown) {
        in_drawdown = true;
        dd_start = i - 1; /* Drawdown started at the peak */
      }

      if (peak > 0.0) {
        double dd = (peak - pt->equity) / peak;
        if (dd > max_dd) {
          max_dd = dd;
        }
      }
    }
    /* Check the final drawdown period (if we never recovered) */
    if (in_drawdown) {
      size_t final_dur = (num_points - 1) - dd_start;
      if (final_dur > max_dd_dur) {
        max_dd_dur = final_dur;
      }
    }

    metrics->max_drawdown = max_dd;
    metrics->max_drawdown_duration = (double)max_dd_dur;
  }

  return metrics;
}

SamtraderCodeResult *samtrader_metrics_compute_per_code(Samrena *arena,
                                                        const SamrenaVector *closed_trades,
                                                        const char **codes, const char *exchange,
                                                        size_t code_count) {
  if (!arena || !codes || code_count == 0) {
    return NULL;
  }

  SamtraderCodeResult *results = SAMRENA_PUSH_ARRAY_ZERO(arena, SamtraderCodeResult, code_count);
  if (!results) {
    return NULL;
  }

  for (size_t i = 0; i < code_count; i++) {
    results[i].code = codes[i];
    results[i].exchange = exchange;
  }

  size_t num_trades = closed_trades ? samrena_vector_size(closed_trades) : 0;

  for (size_t t = 0; t < num_trades; t++) {
    const SamtraderClosedTrade *trade =
        (const SamtraderClosedTrade *)samrena_vector_at_const(closed_trades, t);

    /* Find matching code index */
    size_t ci = code_count; /* sentinel = not found */
    for (size_t i = 0; i < code_count; i++) {
      if (strcmp(trade->code, codes[i]) == 0) {
        ci = i;
        break;
      }
    }
    if (ci == code_count) {
      continue; /* trade for unknown code, skip */
    }

    SamtraderCodeResult *r = &results[ci];
    r->total_trades++;
    r->total_pnl += trade->pnl;

    if (trade->pnl > 0.0) {
      r->winning_trades++;
      if (trade->pnl > r->largest_win) {
        r->largest_win = trade->pnl;
      }
    } else {
      r->losing_trades++;
      if (trade->pnl < r->largest_loss) {
        r->largest_loss = trade->pnl;
      }
    }
  }

  /* Compute win rates */
  for (size_t i = 0; i < code_count; i++) {
    if (results[i].total_trades > 0) {
      results[i].win_rate = (double)results[i].winning_trades / (double)results[i].total_trades;
    }
  }

  return results;
}

void samtrader_metrics_print(const SamtraderMetrics *metrics) {
  if (!metrics) {
    printf("Metrics: NULL\n");
    return;
  }

  printf("=== Performance Metrics ===\n");
  printf("Total Return:       %8.2f%%\n", metrics->total_return * 100.0);
  printf("Annualized Return:  %8.2f%%\n", metrics->annualized_return * 100.0);
  printf("Sharpe Ratio:       %8.4f\n", metrics->sharpe_ratio);
  printf("Sortino Ratio:      %8.4f\n", metrics->sortino_ratio);
  printf("Max Drawdown:       %8.2f%%\n", metrics->max_drawdown * 100.0);
  printf("Max DD Duration:    %8.0f days\n", metrics->max_drawdown_duration);
  printf("\n--- Trade Statistics ---\n");
  printf("Total Trades:       %8d\n", metrics->total_trades);
  printf("Winning Trades:     %8d\n", metrics->winning_trades);
  printf("Losing Trades:      %8d\n", metrics->losing_trades);
  printf("Win Rate:           %8.2f%%\n", metrics->win_rate * 100.0);
  printf("Profit Factor:      %8.4f\n", metrics->profit_factor);
  printf("Average Win:        %8.2f\n", metrics->average_win);
  printf("Average Loss:       %8.2f\n", metrics->average_loss);
  printf("Largest Win:        %8.2f\n", metrics->largest_win);
  printf("Largest Loss:       %8.2f\n", metrics->largest_loss);
  printf("Avg Trade Duration: %8.2f days\n", metrics->average_trade_duration);
}
