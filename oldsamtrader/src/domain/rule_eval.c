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

#include "samtrader/domain/rule.h"

#include "samtrader/domain/ohlcv.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

#define EQUALS_TOLERANCE 1e-9

/*============================================================================
 * Indicator Key Generation
 *============================================================================*/

int samtrader_operand_indicator_key(char *buf, size_t buf_size, const SamtraderOperand *operand) {
  if (!buf || buf_size == 0 || !operand || operand->type != SAMTRADER_OPERAND_INDICATOR) {
    if (buf && buf_size > 0) {
      buf[0] = '\0';
    }
    return -1;
  }

  switch (operand->indicator.indicator_type) {
    case SAMTRADER_IND_SMA:
      return snprintf(buf, buf_size, "SMA_%d", operand->indicator.period);
    case SAMTRADER_IND_EMA:
      return snprintf(buf, buf_size, "EMA_%d", operand->indicator.period);
    case SAMTRADER_IND_WMA:
      return snprintf(buf, buf_size, "WMA_%d", operand->indicator.period);
    case SAMTRADER_IND_RSI:
      return snprintf(buf, buf_size, "RSI_%d", operand->indicator.period);
    case SAMTRADER_IND_ROC:
      return snprintf(buf, buf_size, "ROC_%d", operand->indicator.period);
    case SAMTRADER_IND_ATR:
      return snprintf(buf, buf_size, "ATR_%d", operand->indicator.period);
    case SAMTRADER_IND_STDDEV:
      return snprintf(buf, buf_size, "STDDEV_%d", operand->indicator.period);
    case SAMTRADER_IND_OBV:
      return snprintf(buf, buf_size, "OBV");
    case SAMTRADER_IND_VWAP:
      return snprintf(buf, buf_size, "VWAP");
    case SAMTRADER_IND_MACD:
      return snprintf(buf, buf_size, "MACD_%d_%d_%d", operand->indicator.period,
                      operand->indicator.param2, operand->indicator.param3);
    case SAMTRADER_IND_STOCHASTIC:
      return snprintf(buf, buf_size, "STOCHASTIC_%d_%d", operand->indicator.period,
                      operand->indicator.param2);
    case SAMTRADER_IND_BOLLINGER:
      return snprintf(buf, buf_size, "BOLLINGER_%d_%d", operand->indicator.period,
                      operand->indicator.param2);
    case SAMTRADER_IND_PIVOT:
      return snprintf(buf, buf_size, "PIVOT");
  }

  buf[0] = '\0';
  return -1;
}

/*============================================================================
 * Operand Resolution
 *============================================================================*/

static bool resolve_indicator(const SamtraderOperand *op, const SamHashMap *indicators,
                              size_t index, double *out) {
  char key[64];
  if (samtrader_operand_indicator_key(key, sizeof(key), op) < 0) {
    return false;
  }

  SamtraderIndicatorSeries *series = (SamtraderIndicatorSeries *)samhashmap_get(indicators, key);
  if (!series) {
    return false;
  }

  const SamtraderIndicatorValue *val = samtrader_indicator_series_at(series, index);
  if (!val || !val->valid) {
    return false;
  }

  switch (op->indicator.indicator_type) {
    case SAMTRADER_IND_BOLLINGER:
      switch (op->indicator.param3) {
        case SAMTRADER_BOLLINGER_UPPER:
          *out = val->data.bollinger.upper;
          return true;
        case SAMTRADER_BOLLINGER_MIDDLE:
          *out = val->data.bollinger.middle;
          return true;
        case SAMTRADER_BOLLINGER_LOWER:
          *out = val->data.bollinger.lower;
          return true;
        default:
          return false;
      }
    case SAMTRADER_IND_MACD:
      *out = val->data.macd.line;
      return true;
    case SAMTRADER_IND_STOCHASTIC:
      *out = val->data.stochastic.k;
      return true;
    case SAMTRADER_IND_PIVOT:
      switch (op->indicator.param2) {
        case SAMTRADER_PIVOT_PIVOT:
          *out = val->data.pivot.pivot;
          return true;
        case SAMTRADER_PIVOT_R1:
          *out = val->data.pivot.r1;
          return true;
        case SAMTRADER_PIVOT_R2:
          *out = val->data.pivot.r2;
          return true;
        case SAMTRADER_PIVOT_R3:
          *out = val->data.pivot.r3;
          return true;
        case SAMTRADER_PIVOT_S1:
          *out = val->data.pivot.s1;
          return true;
        case SAMTRADER_PIVOT_S2:
          *out = val->data.pivot.s2;
          return true;
        case SAMTRADER_PIVOT_S3:
          *out = val->data.pivot.s3;
          return true;
        default:
          return false;
      }
    default:
      *out = val->data.simple.value;
      return true;
  }
}

static bool resolve_operand(const SamtraderOperand *op, const SamrenaVector *ohlcv,
                            const SamHashMap *indicators, size_t index, double *out) {
  switch (op->type) {
    case SAMTRADER_OPERAND_CONSTANT:
      *out = op->constant;
      return true;

    case SAMTRADER_OPERAND_PRICE_OPEN:
    case SAMTRADER_OPERAND_PRICE_HIGH:
    case SAMTRADER_OPERAND_PRICE_LOW:
    case SAMTRADER_OPERAND_PRICE_CLOSE:
    case SAMTRADER_OPERAND_VOLUME: {
      const SamtraderOhlcv *bar = (const SamtraderOhlcv *)samrena_vector_at_const(ohlcv, index);
      if (!bar) {
        return false;
      }
      switch (op->type) {
        case SAMTRADER_OPERAND_PRICE_OPEN:
          *out = bar->open;
          return true;
        case SAMTRADER_OPERAND_PRICE_HIGH:
          *out = bar->high;
          return true;
        case SAMTRADER_OPERAND_PRICE_LOW:
          *out = bar->low;
          return true;
        case SAMTRADER_OPERAND_PRICE_CLOSE:
          *out = bar->close;
          return true;
        case SAMTRADER_OPERAND_VOLUME:
          *out = (double)bar->volume;
          return true;
        default:
          return false;
      }
    }

    case SAMTRADER_OPERAND_INDICATOR:
      if (!indicators) {
        return false;
      }
      return resolve_indicator(op, indicators, index, out);
  }

  return false;
}

/*============================================================================
 * Rule Evaluation
 *============================================================================*/

bool samtrader_rule_evaluate(const SamtraderRule *rule, const SamrenaVector *ohlcv,
                             const SamHashMap *indicators, size_t index) {
  if (!rule || !ohlcv) {
    return false;
  }

  switch (rule->type) {
    case SAMTRADER_RULE_ABOVE: {
      double left, right;
      if (!resolve_operand(&rule->left, ohlcv, indicators, index, &left) ||
          !resolve_operand(&rule->right, ohlcv, indicators, index, &right)) {
        return false;
      }
      return left > right;
    }

    case SAMTRADER_RULE_BELOW: {
      double left, right;
      if (!resolve_operand(&rule->left, ohlcv, indicators, index, &left) ||
          !resolve_operand(&rule->right, ohlcv, indicators, index, &right)) {
        return false;
      }
      return left < right;
    }

    case SAMTRADER_RULE_EQUALS: {
      double left, right;
      if (!resolve_operand(&rule->left, ohlcv, indicators, index, &left) ||
          !resolve_operand(&rule->right, ohlcv, indicators, index, &right)) {
        return false;
      }
      return fabs(left - right) <= EQUALS_TOLERANCE;
    }

    case SAMTRADER_RULE_BETWEEN: {
      double left, lower;
      if (!resolve_operand(&rule->left, ohlcv, indicators, index, &left) ||
          !resolve_operand(&rule->right, ohlcv, indicators, index, &lower)) {
        return false;
      }
      return left >= lower && left <= rule->threshold;
    }

    case SAMTRADER_RULE_CROSS_ABOVE: {
      if (index == 0) {
        return false;
      }
      double curr_left, curr_right, prev_left, prev_right;
      if (!resolve_operand(&rule->left, ohlcv, indicators, index, &curr_left) ||
          !resolve_operand(&rule->right, ohlcv, indicators, index, &curr_right) ||
          !resolve_operand(&rule->left, ohlcv, indicators, index - 1, &prev_left) ||
          !resolve_operand(&rule->right, ohlcv, indicators, index - 1, &prev_right)) {
        return false;
      }
      return prev_left <= prev_right && curr_left > curr_right;
    }

    case SAMTRADER_RULE_CROSS_BELOW: {
      if (index == 0) {
        return false;
      }
      double curr_left, curr_right, prev_left, prev_right;
      if (!resolve_operand(&rule->left, ohlcv, indicators, index, &curr_left) ||
          !resolve_operand(&rule->right, ohlcv, indicators, index, &curr_right) ||
          !resolve_operand(&rule->left, ohlcv, indicators, index - 1, &prev_left) ||
          !resolve_operand(&rule->right, ohlcv, indicators, index - 1, &prev_right)) {
        return false;
      }
      return prev_left >= prev_right && curr_left < curr_right;
    }

    case SAMTRADER_RULE_AND: {
      if (!rule->children)
        return false;
      for (size_t i = 0; rule->children[i] != NULL; i++) {
        if (!samtrader_rule_evaluate(rule->children[i], ohlcv, indicators, index)) {
          return false;
        }
      }
      return true;
    }

    case SAMTRADER_RULE_OR: {
      if (!rule->children)
        return false;
      for (size_t i = 0; rule->children[i] != NULL; i++) {
        if (samtrader_rule_evaluate(rule->children[i], ohlcv, indicators, index)) {
          return true;
        }
      }
      return false;
    }

    case SAMTRADER_RULE_NOT: {
      if (!rule->child)
        return false;
      return !samtrader_rule_evaluate(rule->child, ohlcv, indicators, index);
    }

    case SAMTRADER_RULE_CONSECUTIVE: {
      if (!rule->child || rule->lookback <= 0)
        return false;
      if (index < (size_t)(rule->lookback - 1))
        return false;
      size_t start = index - (size_t)(rule->lookback - 1);
      for (size_t i = start; i <= index; i++) {
        if (!samtrader_rule_evaluate(rule->child, ohlcv, indicators, i)) {
          return false;
        }
      }
      return true;
    }

    case SAMTRADER_RULE_ANY_OF: {
      if (!rule->child || rule->lookback <= 0)
        return false;
      if (index < (size_t)(rule->lookback - 1))
        return false;
      size_t start = index - (size_t)(rule->lookback - 1);
      for (size_t i = start; i <= index; i++) {
        if (samtrader_rule_evaluate(rule->child, ohlcv, indicators, i)) {
          return true;
        }
      }
      return false;
    }
  }

  return false;
}
