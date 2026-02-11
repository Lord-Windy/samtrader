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

#include <string.h>

/*============================================================================
 * Rule Construction
 *============================================================================*/

SamtraderRule *samtrader_rule_create_comparison(Samrena *arena, SamtraderRuleType type,
                                                SamtraderOperand left, SamtraderOperand right) {
  if (!arena) {
    return NULL;
  }

  if (type != SAMTRADER_RULE_CROSS_ABOVE && type != SAMTRADER_RULE_CROSS_BELOW &&
      type != SAMTRADER_RULE_ABOVE && type != SAMTRADER_RULE_BELOW &&
      type != SAMTRADER_RULE_EQUALS) {
    return NULL;
  }

  SamtraderRule *rule = SAMRENA_PUSH_TYPE_ZERO(arena, SamtraderRule);
  if (!rule) {
    return NULL;
  }

  rule->type = type;
  rule->left = left;
  rule->right = right;

  return rule;
}

SamtraderRule *samtrader_rule_create_between(Samrena *arena, SamtraderOperand left,
                                             SamtraderOperand lower, double upper) {
  if (!arena) {
    return NULL;
  }

  SamtraderRule *rule = SAMRENA_PUSH_TYPE_ZERO(arena, SamtraderRule);
  if (!rule) {
    return NULL;
  }

  rule->type = SAMTRADER_RULE_BETWEEN;
  rule->left = left;
  rule->right = lower;
  rule->threshold = upper;

  return rule;
}

SamtraderRule *samtrader_rule_create_composite(Samrena *arena, SamtraderRuleType type,
                                               SamtraderRule **children, size_t count) {
  if (!arena || !children || count == 0) {
    return NULL;
  }

  if (type != SAMTRADER_RULE_AND && type != SAMTRADER_RULE_OR) {
    return NULL;
  }

  SamtraderRule *rule = SAMRENA_PUSH_TYPE_ZERO(arena, SamtraderRule);
  if (!rule) {
    return NULL;
  }

  /* Allocate NULL-terminated array */
  SamtraderRule **child_array = SAMRENA_PUSH_ARRAY(arena, SamtraderRule *, count + 1);
  if (!child_array) {
    return NULL;
  }

  memcpy(child_array, children, sizeof(SamtraderRule *) * count);
  child_array[count] = NULL;

  rule->type = type;
  rule->children = child_array;

  return rule;
}

SamtraderRule *samtrader_rule_create_not(Samrena *arena, SamtraderRule *child) {
  if (!arena || !child) {
    return NULL;
  }

  SamtraderRule *rule = SAMRENA_PUSH_TYPE_ZERO(arena, SamtraderRule);
  if (!rule) {
    return NULL;
  }

  rule->type = SAMTRADER_RULE_NOT;
  rule->child = child;

  return rule;
}

SamtraderRule *samtrader_rule_create_temporal(Samrena *arena, SamtraderRuleType type,
                                              SamtraderRule *child, int lookback) {
  if (!arena || !child || lookback <= 0) {
    return NULL;
  }

  if (type != SAMTRADER_RULE_CONSECUTIVE && type != SAMTRADER_RULE_ANY_OF) {
    return NULL;
  }

  SamtraderRule *rule = SAMRENA_PUSH_TYPE_ZERO(arena, SamtraderRule);
  if (!rule) {
    return NULL;
  }

  rule->type = type;
  rule->child = child;
  rule->lookback = lookback;

  return rule;
}

/*============================================================================
 * Operand Construction
 *============================================================================*/

SamtraderOperand samtrader_operand_constant(double value) {
  SamtraderOperand op = {0};
  op.type = SAMTRADER_OPERAND_CONSTANT;
  op.constant = value;
  return op;
}

SamtraderOperand samtrader_operand_price(SamtraderOperandType type) {
  SamtraderOperand op = {0};
  op.type = type;
  return op;
}

SamtraderOperand samtrader_operand_indicator(SamtraderIndicatorType indicator_type, int period) {
  SamtraderOperand op = {0};
  op.type = SAMTRADER_OPERAND_INDICATOR;
  op.indicator.indicator_type = indicator_type;
  op.indicator.period = period;
  return op;
}

SamtraderOperand samtrader_operand_indicator_multi(SamtraderIndicatorType indicator_type,
                                                   int period, int param2, int param3) {
  SamtraderOperand op = {0};
  op.type = SAMTRADER_OPERAND_INDICATOR;
  op.indicator.indicator_type = indicator_type;
  op.indicator.period = period;
  op.indicator.param2 = param2;
  op.indicator.param3 = param3;
  return op;
}

/*============================================================================
 * Rule Information
 *============================================================================*/

const char *samtrader_rule_type_name(SamtraderRuleType type) {
  switch (type) {
    case SAMTRADER_RULE_CROSS_ABOVE:
      return "CROSS_ABOVE";
    case SAMTRADER_RULE_CROSS_BELOW:
      return "CROSS_BELOW";
    case SAMTRADER_RULE_ABOVE:
      return "ABOVE";
    case SAMTRADER_RULE_BELOW:
      return "BELOW";
    case SAMTRADER_RULE_BETWEEN:
      return "BETWEEN";
    case SAMTRADER_RULE_EQUALS:
      return "EQUALS";
    case SAMTRADER_RULE_AND:
      return "AND";
    case SAMTRADER_RULE_OR:
      return "OR";
    case SAMTRADER_RULE_NOT:
      return "NOT";
    case SAMTRADER_RULE_CONSECUTIVE:
      return "CONSECUTIVE";
    case SAMTRADER_RULE_ANY_OF:
      return "ANY_OF";
  }
  return "UNKNOWN";
}

const char *samtrader_operand_type_name(SamtraderOperandType type) {
  switch (type) {
    case SAMTRADER_OPERAND_PRICE_OPEN:
      return "PRICE_OPEN";
    case SAMTRADER_OPERAND_PRICE_HIGH:
      return "PRICE_HIGH";
    case SAMTRADER_OPERAND_PRICE_LOW:
      return "PRICE_LOW";
    case SAMTRADER_OPERAND_PRICE_CLOSE:
      return "PRICE_CLOSE";
    case SAMTRADER_OPERAND_VOLUME:
      return "VOLUME";
    case SAMTRADER_OPERAND_INDICATOR:
      return "INDICATOR";
    case SAMTRADER_OPERAND_CONSTANT:
      return "CONSTANT";
  }
  return "UNKNOWN";
}

size_t samtrader_rule_child_count(const SamtraderRule *rule) {
  if (!rule || !rule->children) {
    return 0;
  }

  if (rule->type != SAMTRADER_RULE_AND && rule->type != SAMTRADER_RULE_OR) {
    return 0;
  }

  size_t count = 0;
  while (rule->children[count] != NULL) {
    count++;
  }
  return count;
}
