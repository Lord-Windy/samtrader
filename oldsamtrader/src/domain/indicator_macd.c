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

#include "samtrader/domain/indicator.h"
#include "samtrader/domain/ohlcv.h"

SamtraderIndicatorSeries *samtrader_calculate_macd(Samrena *arena, SamrenaVector *ohlcv,
                                                   int fast_period, int slow_period,
                                                   int signal_period) {
  if (!arena || !ohlcv || fast_period < 1 || slow_period < 1 || signal_period < 1) {
    return NULL;
  }

  size_t data_size = samrena_vector_size(ohlcv);
  if (data_size == 0) {
    return NULL;
  }

  SamtraderIndicatorSeries *series =
      samtrader_macd_series_create(arena, fast_period, slow_period, signal_period, data_size);
  if (!series) {
    return NULL;
  }

  /* EMA multipliers */
  double fast_k = 2.0 / ((double)fast_period + 1.0);
  double slow_k = 2.0 / ((double)slow_period + 1.0);
  double signal_k = 2.0 / ((double)signal_period + 1.0);

  /* Running sums for initial SMA seed of each EMA */
  double fast_sum = 0.0;
  double slow_sum = 0.0;
  double fast_ema = 0.0;
  double slow_ema = 0.0;

  /* Signal line state */
  double signal_sum = 0.0;
  double signal_ema = 0.0;
  int macd_line_count = 0;

  for (size_t i = 0; i < data_size; i++) {
    const SamtraderOhlcv *bar = (const SamtraderOhlcv *)samrena_vector_at_const(ohlcv, i);
    if (!bar) {
      return NULL;
    }

    /* Update fast EMA */
    if (i < (size_t)(fast_period - 1)) {
      fast_sum += bar->close;
    } else if (i == (size_t)(fast_period - 1)) {
      fast_sum += bar->close;
      fast_ema = fast_sum / (double)fast_period;
    } else {
      fast_ema = (bar->close * fast_k) + (fast_ema * (1.0 - fast_k));
    }

    /* Update slow EMA */
    if (i < (size_t)(slow_period - 1)) {
      slow_sum += bar->close;
    } else if (i == (size_t)(slow_period - 1)) {
      slow_sum += bar->close;
      slow_ema = slow_sum / (double)slow_period;
    } else {
      slow_ema = (bar->close * slow_k) + (slow_ema * (1.0 - slow_k));
    }

    /* MACD line requires both EMAs to be valid */
    int max_period = fast_period > slow_period ? fast_period : slow_period;
    bool macd_line_valid = (i >= (size_t)(max_period - 1));

    if (!macd_line_valid) {
      if (!samtrader_indicator_add_macd(series, bar->date, 0.0, 0.0, 0.0, false)) {
        return NULL;
      }
      continue;
    }

    double macd_line = fast_ema - slow_ema;
    macd_line_count++;

    /* Update signal EMA (EMA of MACD line values) */
    if (macd_line_count < signal_period) {
      /* Accumulating sum for initial signal SMA */
      signal_sum += macd_line;
      if (!samtrader_indicator_add_macd(series, bar->date, macd_line, 0.0, 0.0, false)) {
        return NULL;
      }
    } else if (macd_line_count == signal_period) {
      /* First valid signal: use SMA of first signal_period MACD line values */
      signal_sum += macd_line;
      signal_ema = signal_sum / (double)signal_period;
      double histogram = macd_line - signal_ema;
      if (!samtrader_indicator_add_macd(series, bar->date, macd_line, signal_ema, histogram,
                                        true)) {
        return NULL;
      }
    } else {
      /* Subsequent values: apply EMA formula to signal line */
      signal_ema = (macd_line * signal_k) + (signal_ema * (1.0 - signal_k));
      double histogram = macd_line - signal_ema;
      if (!samtrader_indicator_add_macd(series, bar->date, macd_line, signal_ema, histogram,
                                        true)) {
        return NULL;
      }
    }
  }

  return series;
}
