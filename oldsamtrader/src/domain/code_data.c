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

#include "samtrader/domain/code_data.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "samtrader/domain/indicator.h"
#include "samtrader/domain/ohlcv.h"
#include "samtrader/domain/rule.h"
#include "samtrader/ports/data_port.h"

#define INDICATOR_KEY_BUF_SIZE 64
#define DATE_KEY_BUF_SIZE 32

/* --- Arena string helper --- */

static const char *arena_strdup(Samrena *arena, const char *src) {
  size_t len = strlen(src);
  char *copy = (char *)samrena_push(arena, len + 1);
  if (!copy)
    return NULL;
  memcpy(copy, src, len + 1);
  return copy;
}

/* --- Date key helper --- */

static void date_to_key(char *buf, size_t buf_size, time_t date) {
  snprintf(buf, buf_size, "%ld", (long)date);
}

/* --- Indicator collection (reused from main.c logic) --- */

static void collect_from_operand(const SamtraderOperand *op, SamHashMap *seen_keys,
                                 SamrenaVector *operands) {
  if (op->type != SAMTRADER_OPERAND_INDICATOR)
    return;
  char key_buf[INDICATOR_KEY_BUF_SIZE];
  if (samtrader_operand_indicator_key(key_buf, sizeof(key_buf), op) < 0)
    return;
  if (samhashmap_contains(seen_keys, key_buf))
    return;
  samhashmap_put(seen_keys, key_buf, (void *)1);
  samrena_vector_push(operands, op);
}

static void collect_indicator_operands(const SamtraderRule *rule, SamHashMap *seen_keys,
                                       SamrenaVector *operands) {
  if (!rule)
    return;
  switch (rule->type) {
    case SAMTRADER_RULE_CROSS_ABOVE:
    case SAMTRADER_RULE_CROSS_BELOW:
    case SAMTRADER_RULE_ABOVE:
    case SAMTRADER_RULE_BELOW:
    case SAMTRADER_RULE_BETWEEN:
    case SAMTRADER_RULE_EQUALS:
      collect_from_operand(&rule->left, seen_keys, operands);
      collect_from_operand(&rule->right, seen_keys, operands);
      break;
    case SAMTRADER_RULE_AND:
    case SAMTRADER_RULE_OR:
      if (rule->children) {
        for (size_t i = 0; rule->children[i] != NULL; i++)
          collect_indicator_operands(rule->children[i], seen_keys, operands);
      }
      break;
    case SAMTRADER_RULE_NOT:
    case SAMTRADER_RULE_CONSECUTIVE:
    case SAMTRADER_RULE_ANY_OF:
      collect_indicator_operands(rule->child, seen_keys, operands);
      break;
  }
}

static SamtraderIndicatorSeries *
calculate_indicator_for_operand(Samrena *arena, const SamtraderOperand *op, SamrenaVector *ohlcv) {
  switch (op->indicator.indicator_type) {
    case SAMTRADER_IND_MACD:
      return samtrader_calculate_macd(arena, ohlcv, op->indicator.period, op->indicator.param2,
                                      op->indicator.param3);
    case SAMTRADER_IND_BOLLINGER:
      return samtrader_calculate_bollinger(arena, ohlcv, op->indicator.period,
                                           op->indicator.param2 / 100.0);
    case SAMTRADER_IND_STOCHASTIC:
      return samtrader_calculate_stochastic(arena, ohlcv, op->indicator.period,
                                            op->indicator.param2);
    case SAMTRADER_IND_PIVOT:
      return samtrader_calculate_pivot(arena, ohlcv);
    default:
      return samtrader_indicator_calculate(arena, op->indicator.indicator_type, ohlcv,
                                           op->indicator.period);
  }
}

/* --- qsort comparator for time_t --- */

static int compare_time(const void *a, const void *b) {
  time_t ta = *(const time_t *)a;
  time_t tb = *(const time_t *)b;
  if (ta < tb)
    return -1;
  if (ta > tb)
    return 1;
  return 0;
}

/* --- Public API --- */

SamtraderCodeData *samtrader_load_code_data(Samrena *arena, SamtraderDataPort *data_port,
                                            const char *code, const char *exchange,
                                            time_t start_date, time_t end_date) {
  if (!arena || !data_port || !code || !exchange)
    return NULL;

  SamrenaVector *ohlcv = data_port->fetch_ohlcv(data_port, code, exchange, start_date, end_date);
  if (!ohlcv)
    return NULL;

  SamtraderCodeData *cd = SAMRENA_PUSH_TYPE_ZERO(arena, SamtraderCodeData);
  if (!cd)
    return NULL;

  cd->code = arena_strdup(arena, code);
  cd->exchange = arena_strdup(arena, exchange);
  if (!cd->code || !cd->exchange)
    return NULL;

  cd->ohlcv = ohlcv;
  cd->bar_count = samrena_vector_size(ohlcv);
  cd->indicators = NULL;

  return cd;
}

int samtrader_code_data_compute_indicators(Samrena *arena, SamtraderCodeData *code_data,
                                           const SamtraderStrategy *strategy) {
  if (!arena || !code_data || !strategy)
    return -1;

  SamHashMap *seen_keys = samhashmap_create(32, arena);
  SamrenaVector *operands = samrena_vector_init(arena, sizeof(SamtraderOperand), 16);
  if (!seen_keys || !operands)
    return -1;

  collect_indicator_operands(strategy->entry_long, seen_keys, operands);
  collect_indicator_operands(strategy->exit_long, seen_keys, operands);
  if (strategy->entry_short)
    collect_indicator_operands(strategy->entry_short, seen_keys, operands);
  if (strategy->exit_short)
    collect_indicator_operands(strategy->exit_short, seen_keys, operands);

  SamHashMap *indicators = samhashmap_create(32, arena);
  if (!indicators)
    return -1;

  for (size_t i = 0; i < samrena_vector_size(operands); i++) {
    const SamtraderOperand *op = (const SamtraderOperand *)samrena_vector_at_const(operands, i);
    SamtraderIndicatorSeries *series = calculate_indicator_for_operand(arena, op, code_data->ohlcv);
    if (!series)
      return -1;
    char key_buf[INDICATOR_KEY_BUF_SIZE];
    samtrader_operand_indicator_key(key_buf, sizeof(key_buf), op);
    samhashmap_put(indicators, key_buf, series);
  }

  code_data->indicators = indicators;
  return 0;
}

SamrenaVector *samtrader_build_date_timeline(Samrena *arena, SamtraderCodeData **code_data,
                                             size_t code_count) {
  if (!arena || !code_data || code_count == 0)
    return NULL;

  SamHashMap *seen = samhashmap_create(256, arena);
  SamrenaVector *dates = samrena_vector_init(arena, sizeof(time_t), 256);
  if (!seen || !dates)
    return NULL;

  for (size_t c = 0; c < code_count; c++) {
    if (!code_data[c] || !code_data[c]->ohlcv)
      continue;
    for (size_t i = 0; i < code_data[c]->bar_count; i++) {
      const SamtraderOhlcv *bar =
          (const SamtraderOhlcv *)samrena_vector_at_const(code_data[c]->ohlcv, i);
      char key[DATE_KEY_BUF_SIZE];
      date_to_key(key, sizeof(key), bar->date);
      if (!samhashmap_contains(seen, key)) {
        samhashmap_put(seen, key, (void *)1);
        samrena_vector_push(dates, &bar->date);
      }
    }
  }

  size_t count = samrena_vector_size(dates);
  if (count > 1) {
    qsort(dates->data, count, sizeof(time_t), compare_time);
  }

  return dates;
}

SamHashMap *samtrader_build_date_index(Samrena *arena, SamrenaVector *ohlcv) {
  if (!arena || !ohlcv)
    return NULL;

  size_t count = samrena_vector_size(ohlcv);
  SamHashMap *index = samhashmap_create(count > 0 ? count * 2 : 4, arena);
  if (!index)
    return NULL;

  for (size_t i = 0; i < count; i++) {
    const SamtraderOhlcv *bar = (const SamtraderOhlcv *)samrena_vector_at_const(ohlcv, i);
    char key[DATE_KEY_BUF_SIZE];
    date_to_key(key, sizeof(key), bar->date);

    size_t *idx = SAMRENA_PUSH_TYPE(arena, size_t);
    if (!idx)
      return NULL;
    *idx = i;
    samhashmap_put(index, key, idx);
  }

  return index;
}
