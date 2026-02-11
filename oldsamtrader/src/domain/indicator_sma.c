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

SamtraderIndicatorSeries *samtrader_calculate_sma(Samrena *arena, SamrenaVector *ohlcv,
                                                  int period) {
  if (!arena || !ohlcv || period < 1) {
    return NULL;
  }

  size_t data_size = samrena_vector_size(ohlcv);
  if (data_size == 0) {
    return NULL;
  }

  SamtraderIndicatorSeries *series =
      samtrader_indicator_series_create(arena, SAMTRADER_IND_SMA, period, data_size);
  if (!series) {
    return NULL;
  }

  /* Running sum for efficient calculation */
  double sum = 0.0;

  for (size_t i = 0; i < data_size; i++) {
    const SamtraderOhlcv *bar = (const SamtraderOhlcv *)samrena_vector_at_const(ohlcv, i);
    if (!bar) {
      return NULL;
    }

    sum += bar->close;

    /* Remove oldest value when window is full */
    if (i >= (size_t)period) {
      const SamtraderOhlcv *old_bar =
          (const SamtraderOhlcv *)samrena_vector_at_const(ohlcv, i - (size_t)period);
      if (!old_bar) {
        return NULL;
      }
      sum -= old_bar->close;
    }

    /* Calculate SMA value */
    bool valid = (i >= (size_t)(period - 1));
    double sma_value = valid ? (sum / (double)period) : 0.0;

    if (!samtrader_indicator_add_simple(series, bar->date, sma_value, valid)) {
      return NULL;
    }
  }

  return series;
}
