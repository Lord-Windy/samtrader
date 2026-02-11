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

SamtraderIndicatorSeries *samtrader_calculate_ema(Samrena *arena, SamrenaVector *ohlcv,
                                                  int period) {
  if (!arena || !ohlcv || period < 1) {
    return NULL;
  }

  size_t data_size = samrena_vector_size(ohlcv);
  if (data_size == 0) {
    return NULL;
  }

  SamtraderIndicatorSeries *series =
      samtrader_indicator_series_create(arena, SAMTRADER_IND_EMA, period, data_size);
  if (!series) {
    return NULL;
  }

  /* EMA multiplier: k = 2 / (period + 1) */
  double k = 2.0 / ((double)period + 1.0);

  /* Running sum for initial SMA calculation */
  double sum = 0.0;
  double ema_value = 0.0;

  for (size_t i = 0; i < data_size; i++) {
    const SamtraderOhlcv *bar = (const SamtraderOhlcv *)samrena_vector_at_const(ohlcv, i);
    if (!bar) {
      return NULL;
    }

    bool valid = (i >= (size_t)(period - 1));

    if (i < (size_t)(period - 1)) {
      /* Warmup period: accumulate sum for initial SMA */
      sum += bar->close;
      ema_value = 0.0;
    } else if (i == (size_t)(period - 1)) {
      /* First valid value: use SMA as initial EMA */
      sum += bar->close;
      ema_value = sum / (double)period;
    } else {
      /* Subsequent values: apply EMA formula */
      ema_value = (bar->close * k) + (ema_value * (1.0 - k));
    }

    if (!samtrader_indicator_add_simple(series, bar->date, ema_value, valid)) {
      return NULL;
    }
  }

  return series;
}
