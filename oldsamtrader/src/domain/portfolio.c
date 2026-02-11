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

#include "samtrader/domain/portfolio.h"

#include <string.h>

SamtraderPortfolio *samtrader_portfolio_create(Samrena *arena, double initial_capital) {
  if (!arena) {
    return NULL;
  }

  SamtraderPortfolio *portfolio = SAMRENA_PUSH_TYPE_ZERO(arena, SamtraderPortfolio);
  if (!portfolio) {
    return NULL;
  }

  portfolio->positions = samhashmap_create(16, arena);
  if (!portfolio->positions) {
    return NULL;
  }

  portfolio->closed_trades = samrena_vector_init(arena, sizeof(SamtraderClosedTrade), 16);
  if (!portfolio->closed_trades) {
    return NULL;
  }

  portfolio->equity_curve = samrena_vector_init(arena, sizeof(SamtraderEquityPoint), 64);
  if (!portfolio->equity_curve) {
    return NULL;
  }

  portfolio->cash = initial_capital;
  portfolio->initial_capital = initial_capital;

  return portfolio;
}

bool samtrader_portfolio_add_position(SamtraderPortfolio *portfolio, Samrena *arena,
                                      const SamtraderPosition *position) {
  if (!portfolio || !arena || !position || !position->code) {
    return false;
  }

  SamtraderPosition *pos = SAMRENA_PUSH_TYPE_ZERO(arena, SamtraderPosition);
  if (!pos) {
    return false;
  }

  *pos = *position;

  /* Copy code string into arena */
  size_t code_len = strlen(position->code) + 1;
  char *code_copy = (char *)samrena_push(arena, code_len);
  if (!code_copy) {
    return false;
  }
  memcpy(code_copy, position->code, code_len);
  pos->code = code_copy;

  /* Copy exchange string into arena if set */
  if (position->exchange) {
    size_t exchange_len = strlen(position->exchange) + 1;
    char *exchange_copy = (char *)samrena_push(arena, exchange_len);
    if (!exchange_copy) {
      return false;
    }
    memcpy(exchange_copy, position->exchange, exchange_len);
    pos->exchange = exchange_copy;
  }

  return samhashmap_put(portfolio->positions, pos->code, pos);
}

SamtraderPosition *samtrader_portfolio_get_position(const SamtraderPortfolio *portfolio,
                                                    const char *code) {
  if (!portfolio || !code) {
    return NULL;
  }

  return (SamtraderPosition *)samhashmap_get(portfolio->positions, code);
}

bool samtrader_portfolio_has_position(const SamtraderPortfolio *portfolio, const char *code) {
  if (!portfolio || !code) {
    return false;
  }

  return samhashmap_contains(portfolio->positions, code);
}

bool samtrader_portfolio_remove_position(SamtraderPortfolio *portfolio, const char *code) {
  if (!portfolio || !code) {
    return false;
  }

  return samhashmap_remove(portfolio->positions, code);
}

size_t samtrader_portfolio_position_count(const SamtraderPortfolio *portfolio) {
  if (!portfolio) {
    return 0;
  }

  return samhashmap_size(portfolio->positions);
}

bool samtrader_portfolio_record_trade(SamtraderPortfolio *portfolio, Samrena *arena,
                                      const SamtraderClosedTrade *trade) {
  if (!portfolio || !arena || !trade) {
    return false;
  }

  SamtraderClosedTrade record = *trade;

  /* Copy code string into arena if set */
  if (trade->code) {
    size_t code_len = strlen(trade->code) + 1;
    char *code_copy = (char *)samrena_push(arena, code_len);
    if (!code_copy) {
      return false;
    }
    memcpy(code_copy, trade->code, code_len);
    record.code = code_copy;
  }

  /* Copy exchange string into arena if set */
  if (trade->exchange) {
    size_t exchange_len = strlen(trade->exchange) + 1;
    char *exchange_copy = (char *)samrena_push(arena, exchange_len);
    if (!exchange_copy) {
      return false;
    }
    memcpy(exchange_copy, trade->exchange, exchange_len);
    record.exchange = exchange_copy;
  }

  return samrena_vector_push(portfolio->closed_trades, &record) != NULL;
}

bool samtrader_portfolio_record_equity(SamtraderPortfolio *portfolio, Samrena *arena, time_t date,
                                       double equity) {
  if (!portfolio || !arena) {
    return false;
  }

  SamtraderEquityPoint point = {.date = date, .equity = equity};

  return samrena_vector_push(portfolio->equity_curve, &point) != NULL;
}

typedef struct {
  double total_value;
  const SamHashMap *price_map;
} EquityIterCtx;

static void equity_position_iterator(const char *key, void *value, void *user_data) {
  (void)key;
  EquityIterCtx *ctx = (EquityIterCtx *)user_data;
  SamtraderPosition *pos = (SamtraderPosition *)value;

  double *price = (double *)samhashmap_get(ctx->price_map, pos->code);
  if (price) {
    int64_t abs_qty = pos->quantity >= 0 ? pos->quantity : -pos->quantity;
    ctx->total_value += (double)abs_qty * (*price);
  }
}

double samtrader_portfolio_total_equity(const SamtraderPortfolio *portfolio,
                                        const SamHashMap *price_map) {
  if (!portfolio || !price_map) {
    return -1.0;
  }

  EquityIterCtx ctx = {.total_value = 0.0, .price_map = price_map};

  samhashmap_foreach(portfolio->positions, equity_position_iterator, &ctx);

  return portfolio->cash + ctx.total_value;
}
