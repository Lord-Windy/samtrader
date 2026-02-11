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

#include <ctype.h>
#include <stdlib.h>
#include <string.h>

#define MAX_COMPOSITE_CHILDREN 64

/*============================================================================
 * Parser Context
 *============================================================================*/

typedef struct {
  const char *pos;
  Samrena *arena;
} RuleParser;

/*============================================================================
 * Lexer Helpers
 *============================================================================*/

static void skip_ws(RuleParser *p) {
  while (*p->pos == ' ' || *p->pos == '\t' || *p->pos == '\n' || *p->pos == '\r') {
    p->pos++;
  }
}

static bool match_char(RuleParser *p, char c) {
  skip_ws(p);
  if (*p->pos == c) {
    p->pos++;
    return true;
  }
  return false;
}

/** Match an exact string (advances past it on success). */
static bool match_str(RuleParser *p, const char *s) {
  skip_ws(p);
  size_t len = strlen(s);
  if (strncmp(p->pos, s, len) == 0) {
    p->pos += len;
    return true;
  }
  return false;
}

/** Match a keyword - ensures it's not part of a longer identifier. */
static bool match_keyword(RuleParser *p, const char *kw) {
  skip_ws(p);
  size_t len = strlen(kw);
  if (strncmp(p->pos, kw, len) == 0) {
    char next = p->pos[len];
    if (isalnum((unsigned char)next) || next == '_') {
      return false;
    }
    p->pos += len;
    return true;
  }
  return false;
}

/** Parse a number (integer or float) using strtod. */
static bool parse_number(RuleParser *p, double *out) {
  skip_ws(p);
  const char *start = p->pos;
  char *end;
  *out = strtod(start, &end);
  if (end == start) {
    return false;
  }
  p->pos = end;
  return true;
}

/*============================================================================
 * Forward Declarations
 *============================================================================*/

static SamtraderRule *parse_rule(RuleParser *p);

/*============================================================================
 * Operand Parsing
 *============================================================================*/

static bool parse_operand(RuleParser *p, SamtraderOperand *out) {
  skip_ws(p);

  /* Price fields */
  if (match_keyword(p, "close")) {
    *out = samtrader_operand_price(SAMTRADER_OPERAND_PRICE_CLOSE);
    return true;
  }
  if (match_keyword(p, "open")) {
    *out = samtrader_operand_price(SAMTRADER_OPERAND_PRICE_OPEN);
    return true;
  }
  if (match_keyword(p, "high")) {
    *out = samtrader_operand_price(SAMTRADER_OPERAND_PRICE_HIGH);
    return true;
  }
  if (match_keyword(p, "low")) {
    *out = samtrader_operand_price(SAMTRADER_OPERAND_PRICE_LOW);
    return true;
  }
  if (match_keyword(p, "volume")) {
    *out = samtrader_operand_price(SAMTRADER_OPERAND_VOLUME);
    return true;
  }

  /* Single-parameter indicators: SMA(period), EMA(period), RSI(period), ATR(period) */
  if (match_str(p, "SMA(")) {
    double val;
    if (!parse_number(p, &val) || !match_char(p, ')')) {
      return false;
    }
    *out = samtrader_operand_indicator(SAMTRADER_IND_SMA, (int)val);
    return true;
  }
  if (match_str(p, "EMA(")) {
    double val;
    if (!parse_number(p, &val) || !match_char(p, ')')) {
      return false;
    }
    *out = samtrader_operand_indicator(SAMTRADER_IND_EMA, (int)val);
    return true;
  }
  if (match_str(p, "RSI(")) {
    double val;
    if (!parse_number(p, &val) || !match_char(p, ')')) {
      return false;
    }
    *out = samtrader_operand_indicator(SAMTRADER_IND_RSI, (int)val);
    return true;
  }
  if (match_str(p, "ATR(")) {
    double val;
    if (!parse_number(p, &val) || !match_char(p, ')')) {
      return false;
    }
    *out = samtrader_operand_indicator(SAMTRADER_IND_ATR, (int)val);
    return true;
  }

  /* MACD(fast, slow, signal) */
  if (match_str(p, "MACD(")) {
    double fast, slow, signal;
    if (!parse_number(p, &fast) || !match_char(p, ',') || !parse_number(p, &slow) ||
        !match_char(p, ',') || !parse_number(p, &signal) || !match_char(p, ')')) {
      return false;
    }
    *out = samtrader_operand_indicator_multi(SAMTRADER_IND_MACD, (int)fast, (int)slow, (int)signal);
    return true;
  }

  /* Bollinger variants: BOLLINGER_UPPER/MIDDLE/LOWER(period, stddev) */
  if (match_str(p, "BOLLINGER_UPPER(")) {
    double period, stddev;
    if (!parse_number(p, &period) || !match_char(p, ',') || !parse_number(p, &stddev) ||
        !match_char(p, ')')) {
      return false;
    }
    *out = samtrader_operand_indicator_multi(SAMTRADER_IND_BOLLINGER, (int)period,
                                             (int)(stddev * 100), SAMTRADER_BOLLINGER_UPPER);
    return true;
  }
  if (match_str(p, "BOLLINGER_MIDDLE(")) {
    double period, stddev;
    if (!parse_number(p, &period) || !match_char(p, ',') || !parse_number(p, &stddev) ||
        !match_char(p, ')')) {
      return false;
    }
    *out = samtrader_operand_indicator_multi(SAMTRADER_IND_BOLLINGER, (int)period,
                                             (int)(stddev * 100), SAMTRADER_BOLLINGER_MIDDLE);
    return true;
  }
  if (match_str(p, "BOLLINGER_LOWER(")) {
    double period, stddev;
    if (!parse_number(p, &period) || !match_char(p, ',') || !parse_number(p, &stddev) ||
        !match_char(p, ')')) {
      return false;
    }
    *out = samtrader_operand_indicator_multi(SAMTRADER_IND_BOLLINGER, (int)period,
                                             (int)(stddev * 100), SAMTRADER_BOLLINGER_LOWER);
    return true;
  }

  /* Pivot variants (no parameters) - check longer names first */
  if (match_keyword(p, "PIVOT_R1")) {
    *out = samtrader_operand_indicator_multi(SAMTRADER_IND_PIVOT, 0, SAMTRADER_PIVOT_R1, 0);
    return true;
  }
  if (match_keyword(p, "PIVOT_R2")) {
    *out = samtrader_operand_indicator_multi(SAMTRADER_IND_PIVOT, 0, SAMTRADER_PIVOT_R2, 0);
    return true;
  }
  if (match_keyword(p, "PIVOT_R3")) {
    *out = samtrader_operand_indicator_multi(SAMTRADER_IND_PIVOT, 0, SAMTRADER_PIVOT_R3, 0);
    return true;
  }
  if (match_keyword(p, "PIVOT_S1")) {
    *out = samtrader_operand_indicator_multi(SAMTRADER_IND_PIVOT, 0, SAMTRADER_PIVOT_S1, 0);
    return true;
  }
  if (match_keyword(p, "PIVOT_S2")) {
    *out = samtrader_operand_indicator_multi(SAMTRADER_IND_PIVOT, 0, SAMTRADER_PIVOT_S2, 0);
    return true;
  }
  if (match_keyword(p, "PIVOT_S3")) {
    *out = samtrader_operand_indicator_multi(SAMTRADER_IND_PIVOT, 0, SAMTRADER_PIVOT_S3, 0);
    return true;
  }
  if (match_keyword(p, "PIVOT")) {
    *out = samtrader_operand_indicator_multi(SAMTRADER_IND_PIVOT, 0, SAMTRADER_PIVOT_PIVOT, 0);
    return true;
  }

  /* Numeric constant */
  double val;
  if (parse_number(p, &val)) {
    *out = samtrader_operand_constant(val);
    return true;
  }

  return false;
}

/*============================================================================
 * Rule Parsing (Recursive Descent)
 *============================================================================*/

/** Parse a comparison rule (opening paren already consumed). */
static SamtraderRule *parse_comparison(RuleParser *p, SamtraderRuleType type) {
  SamtraderOperand left, right;
  if (!parse_operand(p, &left)) {
    return NULL;
  }
  if (!match_char(p, ',')) {
    return NULL;
  }
  if (!parse_operand(p, &right)) {
    return NULL;
  }
  if (!match_char(p, ')')) {
    return NULL;
  }
  return samtrader_rule_create_comparison(p->arena, type, left, right);
}

/** Parse a BETWEEN rule (opening paren already consumed). */
static SamtraderRule *parse_between(RuleParser *p) {
  SamtraderOperand operand;
  if (!parse_operand(p, &operand)) {
    return NULL;
  }
  if (!match_char(p, ',')) {
    return NULL;
  }
  double lower;
  if (!parse_number(p, &lower)) {
    return NULL;
  }
  if (!match_char(p, ',')) {
    return NULL;
  }
  double upper;
  if (!parse_number(p, &upper)) {
    return NULL;
  }
  if (!match_char(p, ')')) {
    return NULL;
  }
  return samtrader_rule_create_between(p->arena, operand, samtrader_operand_constant(lower), upper);
}

/** Parse a composite rule - AND/OR (opening paren already consumed). */
static SamtraderRule *parse_composite(RuleParser *p, SamtraderRuleType type) {
  SamtraderRule *children[MAX_COMPOSITE_CHILDREN];
  size_t count = 0;

  children[0] = parse_rule(p);
  if (!children[0]) {
    return NULL;
  }
  count = 1;

  while (match_char(p, ',')) {
    if (count >= MAX_COMPOSITE_CHILDREN) {
      return NULL;
    }
    children[count] = parse_rule(p);
    if (!children[count]) {
      return NULL;
    }
    count++;
  }

  if (!match_char(p, ')')) {
    return NULL;
  }
  return samtrader_rule_create_composite(p->arena, type, children, count);
}

/** Parse a NOT rule (opening paren already consumed). */
static SamtraderRule *parse_not(RuleParser *p) {
  SamtraderRule *child = parse_rule(p);
  if (!child) {
    return NULL;
  }
  if (!match_char(p, ')')) {
    return NULL;
  }
  return samtrader_rule_create_not(p->arena, child);
}

/** Parse a temporal rule - CONSECUTIVE/ANY_OF (opening paren already consumed). */
static SamtraderRule *parse_temporal(RuleParser *p, SamtraderRuleType type) {
  SamtraderRule *child = parse_rule(p);
  if (!child) {
    return NULL;
  }
  if (!match_char(p, ',')) {
    return NULL;
  }
  double lookback;
  if (!parse_number(p, &lookback)) {
    return NULL;
  }
  if (!match_char(p, ')')) {
    return NULL;
  }
  return samtrader_rule_create_temporal(p->arena, type, child, (int)lookback);
}

/** Parse any rule by matching the leading keyword. */
static SamtraderRule *parse_rule(RuleParser *p) {
  skip_ws(p);

  /* Comparison rules */
  if (match_str(p, "CROSS_ABOVE(")) {
    return parse_comparison(p, SAMTRADER_RULE_CROSS_ABOVE);
  }
  if (match_str(p, "CROSS_BELOW(")) {
    return parse_comparison(p, SAMTRADER_RULE_CROSS_BELOW);
  }
  if (match_str(p, "ABOVE(")) {
    return parse_comparison(p, SAMTRADER_RULE_ABOVE);
  }
  if (match_str(p, "BELOW(")) {
    return parse_comparison(p, SAMTRADER_RULE_BELOW);
  }
  if (match_str(p, "BETWEEN(")) {
    return parse_between(p);
  }
  if (match_str(p, "EQUALS(")) {
    return parse_comparison(p, SAMTRADER_RULE_EQUALS);
  }

  /* Composite rules */
  if (match_str(p, "AND(")) {
    return parse_composite(p, SAMTRADER_RULE_AND);
  }
  if (match_str(p, "OR(")) {
    return parse_composite(p, SAMTRADER_RULE_OR);
  }
  if (match_str(p, "NOT(")) {
    return parse_not(p);
  }

  /* Temporal rules */
  if (match_str(p, "CONSECUTIVE(")) {
    return parse_temporal(p, SAMTRADER_RULE_CONSECUTIVE);
  }
  if (match_str(p, "ANY_OF(")) {
    return parse_temporal(p, SAMTRADER_RULE_ANY_OF);
  }

  return NULL;
}

/*============================================================================
 * Public API
 *============================================================================*/

SamtraderRule *samtrader_rule_parse(Samrena *arena, const char *text) {
  if (!arena || !text) {
    return NULL;
  }

  RuleParser parser = {.pos = text, .arena = arena};
  SamtraderRule *rule = parse_rule(&parser);
  if (!rule) {
    return NULL;
  }

  /* Verify entire input was consumed (ignoring trailing whitespace) */
  skip_ws(&parser);
  if (*parser.pos != '\0') {
    return NULL;
  }

  return rule;
}
