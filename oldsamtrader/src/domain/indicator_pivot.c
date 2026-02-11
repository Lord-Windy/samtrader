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

SamtraderIndicatorSeries *samtrader_calculate_pivot(Samrena *arena, SamrenaVector *ohlcv) {
  if (!arena || !ohlcv) {
    return NULL;
  }

  size_t data_size = samrena_vector_size(ohlcv);
  if (data_size == 0) {
    return NULL;
  }

  SamtraderIndicatorSeries *series = samtrader_pivot_series_create(arena, data_size);
  if (!series) {
    return NULL;
  }

  for (size_t i = 0; i < data_size; i++) {
    const SamtraderOhlcv *bar = (const SamtraderOhlcv *)samrena_vector_at_const(ohlcv, i);
    if (!bar) {
      return NULL;
    }

    if (i == 0) {
      /* First bar: no previous day's data available */
      if (!samtrader_indicator_add_pivot(series, bar->date, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0,
                                         false)) {
        return NULL;
      }
      continue;
    }

    /* Calculate pivot points from previous bar's high, low, close */
    const SamtraderOhlcv *prev = (const SamtraderOhlcv *)samrena_vector_at_const(ohlcv, i - 1);
    if (!prev) {
      return NULL;
    }

    double h = prev->high;
    double l = prev->low;
    double c = prev->close;

    double pivot = (h + l + c) / 3.0;
    double r1 = (2.0 * pivot) - l;
    double r2 = pivot + (h - l);
    double r3 = h + 2.0 * (pivot - l);
    double s1 = (2.0 * pivot) - h;
    double s2 = pivot - (h - l);
    double s3 = l - 2.0 * (h - pivot);

    if (!samtrader_indicator_add_pivot(series, bar->date, pivot, r1, r2, r3, s1, s2, s3, true)) {
      return NULL;
    }
  }

  return series;
}
