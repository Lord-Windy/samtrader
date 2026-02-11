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

SamtraderIndicatorSeries *samtrader_calculate_wma(Samrena *arena, SamrenaVector *ohlcv,
                                                  int period) {
  if (!arena || !ohlcv || period < 1) {
    return NULL;
  }

  size_t data_size = samrena_vector_size(ohlcv);
  if (data_size == 0) {
    return NULL;
  }

  SamtraderIndicatorSeries *series =
      samtrader_indicator_series_create(arena, SAMTRADER_IND_WMA, period, data_size);
  if (!series) {
    return NULL;
  }

  /* Weight divisor: sum of weights 1 + 2 + ... + n = n*(n+1)/2 */
  double weight_sum = (double)period * ((double)period + 1.0) / 2.0;

  for (size_t i = 0; i < data_size; i++) {
    const SamtraderOhlcv *bar = (const SamtraderOhlcv *)samrena_vector_at_const(ohlcv, i);
    if (!bar) {
      return NULL;
    }

    bool valid = (i >= (size_t)(period - 1));
    double wma_value = 0.0;

    if (valid) {
      /* Calculate weighted sum */
      double weighted_sum = 0.0;
      for (int j = 0; j < period; j++) {
        const SamtraderOhlcv *window_bar =
            (const SamtraderOhlcv *)samrena_vector_at_const(ohlcv,
                                                            i - (size_t)(period - 1) + (size_t)j);
        if (!window_bar) {
          return NULL;
        }
        /* Weight: oldest has weight 1, newest has weight 'period' */
        double weight = (double)(j + 1);
        weighted_sum += window_bar->close * weight;
      }
      wma_value = weighted_sum / weight_sum;
    }

    if (!samtrader_indicator_add_simple(series, bar->date, wma_value, valid)) {
      return NULL;
    }
  }

  return series;
}
