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

#include "samtrader/domain/position.h"

#include <string.h>

SamtraderPosition *samtrader_position_create(Samrena *arena, const char *code, const char *exchange,
                                             int64_t quantity, double entry_price,
                                             time_t entry_date, double stop_loss,
                                             double take_profit) {
  if (!arena || !code || !exchange) {
    return NULL;
  }

  SamtraderPosition *pos = SAMRENA_PUSH_TYPE_ZERO(arena, SamtraderPosition);
  if (!pos) {
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

  pos->code = code_copy;
  pos->exchange = exchange_copy;
  pos->quantity = quantity;
  pos->entry_price = entry_price;
  pos->entry_date = entry_date;
  pos->stop_loss = stop_loss;
  pos->take_profit = take_profit;

  return pos;
}

bool samtrader_position_is_long(const SamtraderPosition *position) {
  if (!position) {
    return false;
  }
  return position->quantity > 0;
}

bool samtrader_position_is_short(const SamtraderPosition *position) {
  if (!position) {
    return false;
  }
  return position->quantity < 0;
}

double samtrader_position_market_value(const SamtraderPosition *position, double current_price) {
  if (!position) {
    return 0.0;
  }
  int64_t abs_qty = position->quantity >= 0 ? position->quantity : -position->quantity;
  return (double)abs_qty * current_price;
}

double samtrader_position_unrealized_pnl(const SamtraderPosition *position, double current_price) {
  if (!position) {
    return 0.0;
  }
  return (double)position->quantity * (current_price - position->entry_price);
}

bool samtrader_position_should_stop_loss(const SamtraderPosition *position, double current_price) {
  if (!position || position->stop_loss == 0.0) {
    return false;
  }

  if (position->quantity > 0) {
    return current_price <= position->stop_loss;
  } else {
    return current_price >= position->stop_loss;
  }
}

bool samtrader_position_should_take_profit(const SamtraderPosition *position,
                                           double current_price) {
  if (!position || position->take_profit == 0.0) {
    return false;
  }

  if (position->quantity > 0) {
    return current_price >= position->take_profit;
  } else {
    return current_price <= position->take_profit;
  }
}
