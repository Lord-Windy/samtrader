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

SamtraderIndicatorSeries *samtrader_calculate_stochastic(Samrena *arena, SamrenaVector *ohlcv,
                                                         int k_period, int d_period) {
  if (!arena || !ohlcv || k_period < 1 || d_period < 1) {
    return NULL;
  }

  size_t data_size = samrena_vector_size(ohlcv);
  if (data_size == 0) {
    return NULL;
  }

  SamtraderIndicatorSeries *series =
      samtrader_stochastic_series_create(arena, k_period, d_period, data_size);
  if (!series) {
    return NULL;
  }

  /* Circular buffer for %D SMA calculation */
  double *k_buffer = SAMRENA_PUSH_ARRAY_ZERO(arena, double, (uint64_t)d_period);
  if (!k_buffer) {
    return NULL;
  }

  int k_count = 0;
  double k_sum = 0.0;

  for (size_t i = 0; i < data_size; i++) {
    const SamtraderOhlcv *bar = (const SamtraderOhlcv *)samrena_vector_at_const(ohlcv, i);
    if (!bar) {
      return NULL;
    }

    bool k_valid = (i >= (size_t)(k_period - 1));

    if (!k_valid) {
      if (!samtrader_indicator_add_stochastic(series, bar->date, 0.0, 0.0, false)) {
        return NULL;
      }
      continue;
    }

    /* Find highest high and lowest low in the lookback window */
    size_t window_start = i - (size_t)(k_period - 1);
    const SamtraderOhlcv *first_bar =
        (const SamtraderOhlcv *)samrena_vector_at_const(ohlcv, window_start);
    if (!first_bar) {
      return NULL;
    }

    double highest_high = first_bar->high;
    double lowest_low = first_bar->low;

    for (size_t j = window_start + 1; j <= i; j++) {
      const SamtraderOhlcv *w_bar = (const SamtraderOhlcv *)samrena_vector_at_const(ohlcv, j);
      if (!w_bar) {
        return NULL;
      }
      if (w_bar->high > highest_high) {
        highest_high = w_bar->high;
      }
      if (w_bar->low < lowest_low) {
        lowest_low = w_bar->low;
      }
    }

    /* %K = 100 * (close - lowest_low) / (highest_high - lowest_low) */
    double k_value;
    double range = highest_high - lowest_low;
    if (range == 0.0) {
      k_value = 50.0;
    } else {
      k_value = 100.0 * (bar->close - lowest_low) / range;
    }

    /* Update %D running SMA via circular buffer */
    int buf_idx = k_count % d_period;
    if (k_count >= d_period) {
      k_sum -= k_buffer[buf_idx];
    }
    k_buffer[buf_idx] = k_value;
    k_sum += k_value;
    k_count++;

    bool d_valid = (k_count >= d_period);
    double d_value = d_valid ? (k_sum / (double)d_period) : 0.0;

    if (!samtrader_indicator_add_stochastic(series, bar->date, k_value, d_value, d_valid)) {
      return NULL;
    }
  }

  return series;
}
