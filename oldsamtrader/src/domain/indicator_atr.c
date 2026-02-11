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

SamtraderIndicatorSeries *samtrader_calculate_atr(Samrena *arena, SamrenaVector *ohlcv,
                                                  int period) {
  if (!arena || !ohlcv || period < 1) {
    return NULL;
  }

  size_t data_size = samrena_vector_size(ohlcv);
  if (data_size == 0) {
    return NULL;
  }

  SamtraderIndicatorSeries *series =
      samtrader_indicator_series_create(arena, SAMTRADER_IND_ATR, period, data_size);
  if (!series) {
    return NULL;
  }

  double tr_sum = 0.0;
  double atr = 0.0;
  double prev_close = 0.0;

  for (size_t i = 0; i < data_size; i++) {
    const SamtraderOhlcv *bar = (const SamtraderOhlcv *)samrena_vector_at_const(ohlcv, i);
    if (!bar) {
      return NULL;
    }

    /* First bar: TR = high - low (no previous close available) */
    double tr;
    if (i == 0) {
      tr = bar->high - bar->low;
    } else {
      tr = samtrader_ohlcv_true_range(bar, prev_close);
    }
    prev_close = bar->close;

    if (i < (size_t)(period - 1)) {
      /* Warmup: accumulate true range values */
      tr_sum += tr;
      if (!samtrader_indicator_add_simple(series, bar->date, 0.0, false)) {
        return NULL;
      }
    } else if (i == (size_t)(period - 1)) {
      /* First valid ATR: simple average of first `period` true ranges */
      tr_sum += tr;
      atr = tr_sum / (double)period;
      if (!samtrader_indicator_add_simple(series, bar->date, atr, true)) {
        return NULL;
      }
    } else {
      /* Subsequent values: Wilder's smoothing */
      atr = ((atr * (double)(period - 1)) + tr) / (double)period;
      if (!samtrader_indicator_add_simple(series, bar->date, atr, true)) {
        return NULL;
      }
    }
  }

  return series;
}
