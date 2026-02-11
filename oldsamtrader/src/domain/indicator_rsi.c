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

SamtraderIndicatorSeries *samtrader_calculate_rsi(Samrena *arena, SamrenaVector *ohlcv,
                                                  int period) {
  if (!arena || !ohlcv || period < 1) {
    return NULL;
  }

  size_t data_size = samrena_vector_size(ohlcv);
  if (data_size == 0) {
    return NULL;
  }

  SamtraderIndicatorSeries *series =
      samtrader_indicator_series_create(arena, SAMTRADER_IND_RSI, period, data_size);
  if (!series) {
    return NULL;
  }

  /* We need at least (period + 1) bars to compute the first RSI value,
   * since we need `period` price changes (starting from bar index 1). */

  double avg_gain = 0.0;
  double avg_loss = 0.0;
  double prev_close = 0.0;

  for (size_t i = 0; i < data_size; i++) {
    const SamtraderOhlcv *bar = (const SamtraderOhlcv *)samrena_vector_at_const(ohlcv, i);
    if (!bar) {
      return NULL;
    }

    if (i == 0) {
      /* First bar: no price change yet, always invalid */
      prev_close = bar->close;
      if (!samtrader_indicator_add_simple(series, bar->date, 0.0, false)) {
        return NULL;
      }
      continue;
    }

    double change = bar->close - prev_close;
    double gain = change > 0.0 ? change : 0.0;
    double loss = change < 0.0 ? -change : 0.0;
    prev_close = bar->close;

    if (i < (size_t)period) {
      /* Warmup: accumulate gains and losses for initial average */
      avg_gain += gain;
      avg_loss += loss;
      if (!samtrader_indicator_add_simple(series, bar->date, 0.0, false)) {
        return NULL;
      }
    } else if (i == (size_t)period) {
      /* First valid RSI: use simple average of gains and losses */
      avg_gain = (avg_gain + gain) / (double)period;
      avg_loss = (avg_loss + loss) / (double)period;

      double rsi;
      if (avg_loss == 0.0) {
        rsi = (avg_gain == 0.0) ? 50.0 : 100.0;
      } else {
        double rs = avg_gain / avg_loss;
        rsi = 100.0 - (100.0 / (1.0 + rs));
      }

      if (!samtrader_indicator_add_simple(series, bar->date, rsi, true)) {
        return NULL;
      }
    } else {
      /* Subsequent values: Wilder's smoothing */
      avg_gain = ((avg_gain * (double)(period - 1)) + gain) / (double)period;
      avg_loss = ((avg_loss * (double)(period - 1)) + loss) / (double)period;

      double rsi;
      if (avg_loss == 0.0) {
        rsi = (avg_gain == 0.0) ? 50.0 : 100.0;
      } else {
        double rs = avg_gain / avg_loss;
        rsi = 100.0 - (100.0 / (1.0 + rs));
      }

      if (!samtrader_indicator_add_simple(series, bar->date, rsi, true)) {
        return NULL;
      }
    }
  }

  return series;
}
