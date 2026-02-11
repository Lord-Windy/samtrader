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

#include "samtrader/domain/ohlcv.h"

#include <math.h>
#include <string.h>

SamtraderOhlcv *samtrader_ohlcv_create(Samrena *arena, const char *code, const char *exchange,
                                       time_t date, double open, double high, double low,
                                       double close, int64_t volume) {
  if (!arena || !code || !exchange) {
    return NULL;
  }

  SamtraderOhlcv *ohlcv = SAMRENA_PUSH_TYPE_ZERO(arena, SamtraderOhlcv);
  if (!ohlcv) {
    return NULL;
  }

  size_t code_len = strlen(code) + 1;
  char *code_copy = (char *)samrena_push(arena, code_len);
  if (!code_copy) {
    return NULL;
  }
  memcpy(code_copy, code, code_len);

  size_t exchange_len = strlen(exchange) + 1;
  char *exchange_copy = (char *)samrena_push(arena, exchange_len);
  if (!exchange_copy) {
    return NULL;
  }
  memcpy(exchange_copy, exchange, exchange_len);

  ohlcv->code = code_copy;
  ohlcv->exchange = exchange_copy;
  ohlcv->date = date;
  ohlcv->open = open;
  ohlcv->high = high;
  ohlcv->low = low;
  ohlcv->close = close;
  ohlcv->volume = volume;

  return ohlcv;
}

SamrenaVector *samtrader_ohlcv_vector_create(Samrena *arena, uint64_t initial_capacity) {
  if (!arena) {
    return NULL;
  }

  return samrena_vector_init(arena, sizeof(SamtraderOhlcv), initial_capacity);
}

double samtrader_ohlcv_typical_price(const SamtraderOhlcv *ohlcv) {
  if (!ohlcv) {
    return 0.0;
  }

  return (ohlcv->high + ohlcv->low + ohlcv->close) / 3.0;
}

double samtrader_ohlcv_true_range(const SamtraderOhlcv *ohlcv, double prev_close) {
  if (!ohlcv) {
    return 0.0;
  }

  double high_low = ohlcv->high - ohlcv->low;
  double high_prev = fabs(ohlcv->high - prev_close);
  double low_prev = fabs(ohlcv->low - prev_close);

  double max_val = high_low;
  if (high_prev > max_val) {
    max_val = high_prev;
  }
  if (low_prev > max_val) {
    max_val = low_prev;
  }

  return max_val;
}
