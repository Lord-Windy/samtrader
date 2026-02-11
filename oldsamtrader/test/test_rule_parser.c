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

#include <math.h>
#include <stdio.h>
#include <stdlib.h>

#include "samtrader/domain/rule.h"

#define ASSERT(cond, msg)                                                                          \
  do {                                                                                             \
    if (!(cond)) {                                                                                 \
      printf("FAIL: %s\n", msg);                                                                   \
      return 1;                                                                                    \
    }                                                                                              \
  } while (0)

#define ASSERT_DOUBLE_EQ(a, b, msg)                                                                \
  do {                                                                                             \
    if (fabs((a) - (b)) > 0.0001) {                                                                \
      printf("FAIL: %s (expected %f, got %f)\n", msg, (b), (a));                                   \
      return 1;                                                                                    \
    }                                                                                              \
  } while (0)

/*============================================================================
 * Null / Invalid Input Tests
 *============================================================================*/

static int test_parse_null_inputs(void) {
  printf("Testing samtrader_rule_parse with NULL inputs...\n");

  Samrena *arena = samrena_create_default();
  ASSERT(arena != NULL, "Failed to create arena");

  ASSERT(samtrader_rule_parse(NULL, "ABOVE(close, SMA(20))") == NULL,
         "Should return NULL with NULL arena");
  ASSERT(samtrader_rule_parse(arena, NULL) == NULL, "Should return NULL with NULL text");
  ASSERT(samtrader_rule_parse(NULL, NULL) == NULL, "Should return NULL with both NULL");

  samrena_destroy(arena);
  printf("  PASS\n");
  return 0;
}

static int test_parse_empty_and_invalid(void) {
  printf("Testing samtrader_rule_parse with empty/invalid strings...\n");

  Samrena *arena = samrena_create_default();
  ASSERT(arena != NULL, "Failed to create arena");

  ASSERT(samtrader_rule_parse(arena, "") == NULL, "Should return NULL for empty string");
  ASSERT(samtrader_rule_parse(arena, "   ") == NULL, "Should return NULL for whitespace only");
  ASSERT(samtrader_rule_parse(arena, "FOOBAR(close, SMA(20))") == NULL,
         "Should return NULL for unknown keyword");
  ASSERT(samtrader_rule_parse(arena, "ABOVE(close, SMA(20)) extra") == NULL,
         "Should return NULL for trailing garbage");
  ASSERT(samtrader_rule_parse(arena, "ABOVE(close, ") == NULL,
         "Should return NULL for incomplete input");
  ASSERT(samtrader_rule_parse(arena, "ABOVE(close SMA(20))") == NULL,
         "Should return NULL for missing comma");
  ASSERT(samtrader_rule_parse(arena, "ABOVE(close, SMA(20)") == NULL,
         "Should return NULL for missing closing paren");

  samrena_destroy(arena);
  printf("  PASS\n");
  return 0;
}

/*============================================================================
 * Comparison Rule Parsing Tests
 *============================================================================*/

static int test_parse_cross_above(void) {
  printf("Testing parse CROSS_ABOVE(close, SMA(20))...\n");

  Samrena *arena = samrena_create_default();
  ASSERT(arena != NULL, "Failed to create arena");

  SamtraderRule *rule = samtrader_rule_parse(arena, "CROSS_ABOVE(close, SMA(20))");
  ASSERT(rule != NULL, "Failed to parse CROSS_ABOVE rule");
  ASSERT(rule->type == SAMTRADER_RULE_CROSS_ABOVE, "Type should be CROSS_ABOVE");
  ASSERT(rule->left.type == SAMTRADER_OPERAND_PRICE_CLOSE, "Left should be PRICE_CLOSE");
  ASSERT(rule->right.type == SAMTRADER_OPERAND_INDICATOR, "Right should be INDICATOR");
  ASSERT(rule->right.indicator.indicator_type == SAMTRADER_IND_SMA, "Right should be SMA");
  ASSERT(rule->right.indicator.period == 20, "SMA period should be 20");

  samrena_destroy(arena);
  printf("  PASS\n");
  return 0;
}

static int test_parse_cross_below(void) {
  printf("Testing parse CROSS_BELOW(SMA(20), EMA(50))...\n");

  Samrena *arena = samrena_create_default();
  ASSERT(arena != NULL, "Failed to create arena");

  SamtraderRule *rule = samtrader_rule_parse(arena, "CROSS_BELOW(SMA(20), EMA(50))");
  ASSERT(rule != NULL, "Failed to parse CROSS_BELOW rule");
  ASSERT(rule->type == SAMTRADER_RULE_CROSS_BELOW, "Type should be CROSS_BELOW");
  ASSERT(rule->left.type == SAMTRADER_OPERAND_INDICATOR, "Left should be INDICATOR");
  ASSERT(rule->left.indicator.indicator_type == SAMTRADER_IND_SMA, "Left should be SMA");
  ASSERT(rule->left.indicator.period == 20, "SMA period should be 20");
  ASSERT(rule->right.indicator.indicator_type == SAMTRADER_IND_EMA, "Right should be EMA");
  ASSERT(rule->right.indicator.period == 50, "EMA period should be 50");

  samrena_destroy(arena);
  printf("  PASS\n");
  return 0;
}

static int test_parse_above(void) {
  printf("Testing parse ABOVE(close, 100.5)...\n");

  Samrena *arena = samrena_create_default();
  ASSERT(arena != NULL, "Failed to create arena");

  SamtraderRule *rule = samtrader_rule_parse(arena, "ABOVE(close, 100.5)");
  ASSERT(rule != NULL, "Failed to parse ABOVE rule");
  ASSERT(rule->type == SAMTRADER_RULE_ABOVE, "Type should be ABOVE");
  ASSERT(rule->left.type == SAMTRADER_OPERAND_PRICE_CLOSE, "Left should be PRICE_CLOSE");
  ASSERT(rule->right.type == SAMTRADER_OPERAND_CONSTANT, "Right should be CONSTANT");
  ASSERT_DOUBLE_EQ(rule->right.constant, 100.5, "Constant should be 100.5");

  samrena_destroy(arena);
  printf("  PASS\n");
  return 0;
}

static int test_parse_below(void) {
  printf("Testing parse BELOW(volume, 1000000)...\n");

  Samrena *arena = samrena_create_default();
  ASSERT(arena != NULL, "Failed to create arena");

  SamtraderRule *rule = samtrader_rule_parse(arena, "BELOW(volume, 1000000)");
  ASSERT(rule != NULL, "Failed to parse BELOW rule");
  ASSERT(rule->type == SAMTRADER_RULE_BELOW, "Type should be BELOW");
  ASSERT(rule->left.type == SAMTRADER_OPERAND_VOLUME, "Left should be VOLUME");
  ASSERT(rule->right.type == SAMTRADER_OPERAND_CONSTANT, "Right should be CONSTANT");
  ASSERT_DOUBLE_EQ(rule->right.constant, 1000000.0, "Constant should be 1000000");

  samrena_destroy(arena);
  printf("  PASS\n");
  return 0;
}

static int test_parse_equals(void) {
  printf("Testing parse EQUALS(close, open)...\n");

  Samrena *arena = samrena_create_default();
  ASSERT(arena != NULL, "Failed to create arena");

  SamtraderRule *rule = samtrader_rule_parse(arena, "EQUALS(close, open)");
  ASSERT(rule != NULL, "Failed to parse EQUALS rule");
  ASSERT(rule->type == SAMTRADER_RULE_EQUALS, "Type should be EQUALS");
  ASSERT(rule->left.type == SAMTRADER_OPERAND_PRICE_CLOSE, "Left should be PRICE_CLOSE");
  ASSERT(rule->right.type == SAMTRADER_OPERAND_PRICE_OPEN, "Right should be PRICE_OPEN");

  samrena_destroy(arena);
  printf("  PASS\n");
  return 0;
}

/*============================================================================
 * BETWEEN Rule Parsing Tests
 *============================================================================*/

static int test_parse_between(void) {
  printf("Testing parse BETWEEN(RSI(14), 30, 70)...\n");

  Samrena *arena = samrena_create_default();
  ASSERT(arena != NULL, "Failed to create arena");

  SamtraderRule *rule = samtrader_rule_parse(arena, "BETWEEN(RSI(14), 30, 70)");
  ASSERT(rule != NULL, "Failed to parse BETWEEN rule");
  ASSERT(rule->type == SAMTRADER_RULE_BETWEEN, "Type should be BETWEEN");
  ASSERT(rule->left.type == SAMTRADER_OPERAND_INDICATOR, "Left should be INDICATOR");
  ASSERT(rule->left.indicator.indicator_type == SAMTRADER_IND_RSI, "Left should be RSI");
  ASSERT(rule->left.indicator.period == 14, "RSI period should be 14");
  ASSERT(rule->right.type == SAMTRADER_OPERAND_CONSTANT, "Right (lower) should be CONSTANT");
  ASSERT_DOUBLE_EQ(rule->right.constant, 30.0, "Lower bound should be 30");
  ASSERT_DOUBLE_EQ(rule->threshold, 70.0, "Upper bound (threshold) should be 70");

  samrena_destroy(arena);
  printf("  PASS\n");
  return 0;
}

static int test_parse_between_float_bounds(void) {
  printf("Testing parse BETWEEN with float bounds...\n");

  Samrena *arena = samrena_create_default();
  ASSERT(arena != NULL, "Failed to create arena");

  SamtraderRule *rule = samtrader_rule_parse(arena, "BETWEEN(close, 99.5, 100.5)");
  ASSERT(rule != NULL, "Failed to parse BETWEEN rule");
  ASSERT(rule->type == SAMTRADER_RULE_BETWEEN, "Type should be BETWEEN");
  ASSERT(rule->left.type == SAMTRADER_OPERAND_PRICE_CLOSE, "Left should be PRICE_CLOSE");
  ASSERT_DOUBLE_EQ(rule->right.constant, 99.5, "Lower bound should be 99.5");
  ASSERT_DOUBLE_EQ(rule->threshold, 100.5, "Upper bound should be 100.5");

  samrena_destroy(arena);
  printf("  PASS\n");
  return 0;
}

/*============================================================================
 * Composite Rule Parsing Tests
 *============================================================================*/

static int test_parse_and(void) {
  printf("Testing parse AND(ABOVE(close, SMA(20)), BELOW(close, SMA(200)))...\n");

  Samrena *arena = samrena_create_default();
  ASSERT(arena != NULL, "Failed to create arena");

  SamtraderRule *rule =
      samtrader_rule_parse(arena, "AND(ABOVE(close, SMA(20)), BELOW(close, SMA(200)))");
  ASSERT(rule != NULL, "Failed to parse AND rule");
  ASSERT(rule->type == SAMTRADER_RULE_AND, "Type should be AND");
  ASSERT(samtrader_rule_child_count(rule) == 2, "Should have 2 children");
  ASSERT(rule->children[0]->type == SAMTRADER_RULE_ABOVE, "Child 0 should be ABOVE");
  ASSERT(rule->children[1]->type == SAMTRADER_RULE_BELOW, "Child 1 should be BELOW");

  samrena_destroy(arena);
  printf("  PASS\n");
  return 0;
}

static int test_parse_or(void) {
  printf("Testing parse OR with multiple children...\n");

  Samrena *arena = samrena_create_default();
  ASSERT(arena != NULL, "Failed to create arena");

  SamtraderRule *rule = samtrader_rule_parse(
      arena,
      "OR(CROSS_ABOVE(close, SMA(20)), CROSS_ABOVE(close, EMA(20)), CROSS_ABOVE(close, SMA(50)))");
  ASSERT(rule != NULL, "Failed to parse OR rule");
  ASSERT(rule->type == SAMTRADER_RULE_OR, "Type should be OR");
  ASSERT(samtrader_rule_child_count(rule) == 3, "Should have 3 children");
  ASSERT(rule->children[0]->type == SAMTRADER_RULE_CROSS_ABOVE, "Child 0 should be CROSS_ABOVE");
  ASSERT(rule->children[1]->type == SAMTRADER_RULE_CROSS_ABOVE, "Child 1 should be CROSS_ABOVE");
  ASSERT(rule->children[2]->type == SAMTRADER_RULE_CROSS_ABOVE, "Child 2 should be CROSS_ABOVE");

  samrena_destroy(arena);
  printf("  PASS\n");
  return 0;
}

static int test_parse_not(void) {
  printf("Testing parse NOT(ABOVE(close, SMA(20)))...\n");

  Samrena *arena = samrena_create_default();
  ASSERT(arena != NULL, "Failed to create arena");

  SamtraderRule *rule = samtrader_rule_parse(arena, "NOT(ABOVE(close, SMA(20)))");
  ASSERT(rule != NULL, "Failed to parse NOT rule");
  ASSERT(rule->type == SAMTRADER_RULE_NOT, "Type should be NOT");
  ASSERT(rule->child != NULL, "Child should not be NULL");
  ASSERT(rule->child->type == SAMTRADER_RULE_ABOVE, "Child should be ABOVE");

  samrena_destroy(arena);
  printf("  PASS\n");
  return 0;
}

/*============================================================================
 * Temporal Rule Parsing Tests
 *============================================================================*/

static int test_parse_consecutive(void) {
  printf("Testing parse CONSECUTIVE(ABOVE(close, SMA(20)), 5)...\n");

  Samrena *arena = samrena_create_default();
  ASSERT(arena != NULL, "Failed to create arena");

  SamtraderRule *rule = samtrader_rule_parse(arena, "CONSECUTIVE(ABOVE(close, SMA(20)), 5)");
  ASSERT(rule != NULL, "Failed to parse CONSECUTIVE rule");
  ASSERT(rule->type == SAMTRADER_RULE_CONSECUTIVE, "Type should be CONSECUTIVE");
  ASSERT(rule->lookback == 5, "Lookback should be 5");
  ASSERT(rule->child != NULL, "Child should not be NULL");
  ASSERT(rule->child->type == SAMTRADER_RULE_ABOVE, "Child should be ABOVE");

  samrena_destroy(arena);
  printf("  PASS\n");
  return 0;
}

static int test_parse_any_of(void) {
  printf("Testing parse ANY_OF(CROSS_ABOVE(close, SMA(50)), 10)...\n");

  Samrena *arena = samrena_create_default();
  ASSERT(arena != NULL, "Failed to create arena");

  SamtraderRule *rule = samtrader_rule_parse(arena, "ANY_OF(CROSS_ABOVE(close, SMA(50)), 10)");
  ASSERT(rule != NULL, "Failed to parse ANY_OF rule");
  ASSERT(rule->type == SAMTRADER_RULE_ANY_OF, "Type should be ANY_OF");
  ASSERT(rule->lookback == 10, "Lookback should be 10");
  ASSERT(rule->child != NULL, "Child should not be NULL");
  ASSERT(rule->child->type == SAMTRADER_RULE_CROSS_ABOVE, "Child should be CROSS_ABOVE");

  samrena_destroy(arena);
  printf("  PASS\n");
  return 0;
}

/*============================================================================
 * Indicator Parsing Tests
 *============================================================================*/

static int test_parse_macd_indicator(void) {
  printf("Testing parse ABOVE(MACD(12, 26, 9), 0)...\n");

  Samrena *arena = samrena_create_default();
  ASSERT(arena != NULL, "Failed to create arena");

  SamtraderRule *rule = samtrader_rule_parse(arena, "ABOVE(MACD(12, 26, 9), 0)");
  ASSERT(rule != NULL, "Failed to parse rule with MACD indicator");
  ASSERT(rule->type == SAMTRADER_RULE_ABOVE, "Type should be ABOVE");
  ASSERT(rule->left.type == SAMTRADER_OPERAND_INDICATOR, "Left should be INDICATOR");
  ASSERT(rule->left.indicator.indicator_type == SAMTRADER_IND_MACD, "Left should be MACD");
  ASSERT(rule->left.indicator.period == 12, "MACD fast should be 12");
  ASSERT(rule->left.indicator.param2 == 26, "MACD slow should be 26");
  ASSERT(rule->left.indicator.param3 == 9, "MACD signal should be 9");
  ASSERT(rule->right.type == SAMTRADER_OPERAND_CONSTANT, "Right should be CONSTANT");
  ASSERT_DOUBLE_EQ(rule->right.constant, 0.0, "Constant should be 0");

  samrena_destroy(arena);
  printf("  PASS\n");
  return 0;
}

static int test_parse_bollinger_indicators(void) {
  printf("Testing parse Bollinger band indicators...\n");

  Samrena *arena = samrena_create_default();
  ASSERT(arena != NULL, "Failed to create arena");

  /* BOLLINGER_UPPER */
  SamtraderRule *rule = samtrader_rule_parse(arena, "ABOVE(close, BOLLINGER_UPPER(20, 2.0))");
  ASSERT(rule != NULL, "Failed to parse BOLLINGER_UPPER");
  ASSERT(rule->right.type == SAMTRADER_OPERAND_INDICATOR, "Right should be INDICATOR");
  ASSERT(rule->right.indicator.indicator_type == SAMTRADER_IND_BOLLINGER,
         "Right should be BOLLINGER");
  ASSERT(rule->right.indicator.period == 20, "Bollinger period should be 20");
  ASSERT(rule->right.indicator.param2 == 200, "Bollinger stddev*100 should be 200");
  ASSERT(rule->right.indicator.param3 == SAMTRADER_BOLLINGER_UPPER, "Should be UPPER band");

  /* BOLLINGER_MIDDLE */
  rule = samtrader_rule_parse(arena, "ABOVE(close, BOLLINGER_MIDDLE(20, 2.0))");
  ASSERT(rule != NULL, "Failed to parse BOLLINGER_MIDDLE");
  ASSERT(rule->right.indicator.param3 == SAMTRADER_BOLLINGER_MIDDLE, "Should be MIDDLE band");

  /* BOLLINGER_LOWER */
  rule = samtrader_rule_parse(arena, "BELOW(close, BOLLINGER_LOWER(20, 2.5))");
  ASSERT(rule != NULL, "Failed to parse BOLLINGER_LOWER");
  ASSERT(rule->right.indicator.param3 == SAMTRADER_BOLLINGER_LOWER, "Should be LOWER band");
  ASSERT(rule->right.indicator.param2 == 250, "Bollinger stddev*100 should be 250");

  samrena_destroy(arena);
  printf("  PASS\n");
  return 0;
}

static int test_parse_pivot_indicators(void) {
  printf("Testing parse Pivot indicators...\n");

  Samrena *arena = samrena_create_default();
  ASSERT(arena != NULL, "Failed to create arena");

  SamtraderRule *rule = samtrader_rule_parse(arena, "ABOVE(close, PIVOT)");
  ASSERT(rule != NULL, "Failed to parse PIVOT");
  ASSERT(rule->right.indicator.indicator_type == SAMTRADER_IND_PIVOT, "Should be PIVOT");
  ASSERT(rule->right.indicator.param2 == SAMTRADER_PIVOT_PIVOT, "Should be PIVOT field");

  rule = samtrader_rule_parse(arena, "ABOVE(close, PIVOT_R1)");
  ASSERT(rule != NULL, "Failed to parse PIVOT_R1");
  ASSERT(rule->right.indicator.param2 == SAMTRADER_PIVOT_R1, "Should be R1 field");

  rule = samtrader_rule_parse(arena, "ABOVE(close, PIVOT_R2)");
  ASSERT(rule != NULL, "Failed to parse PIVOT_R2");
  ASSERT(rule->right.indicator.param2 == SAMTRADER_PIVOT_R2, "Should be R2 field");

  rule = samtrader_rule_parse(arena, "ABOVE(close, PIVOT_R3)");
  ASSERT(rule != NULL, "Failed to parse PIVOT_R3");
  ASSERT(rule->right.indicator.param2 == SAMTRADER_PIVOT_R3, "Should be R3 field");

  rule = samtrader_rule_parse(arena, "BELOW(close, PIVOT_S1)");
  ASSERT(rule != NULL, "Failed to parse PIVOT_S1");
  ASSERT(rule->right.indicator.param2 == SAMTRADER_PIVOT_S1, "Should be S1 field");

  rule = samtrader_rule_parse(arena, "BELOW(close, PIVOT_S2)");
  ASSERT(rule != NULL, "Failed to parse PIVOT_S2");
  ASSERT(rule->right.indicator.param2 == SAMTRADER_PIVOT_S2, "Should be S2 field");

  rule = samtrader_rule_parse(arena, "BELOW(close, PIVOT_S3)");
  ASSERT(rule != NULL, "Failed to parse PIVOT_S3");
  ASSERT(rule->right.indicator.param2 == SAMTRADER_PIVOT_S3, "Should be S3 field");

  samrena_destroy(arena);
  printf("  PASS\n");
  return 0;
}

static int test_parse_atr_indicator(void) {
  printf("Testing parse BELOW(ATR(14), 2.5)...\n");

  Samrena *arena = samrena_create_default();
  ASSERT(arena != NULL, "Failed to create arena");

  SamtraderRule *rule = samtrader_rule_parse(arena, "BELOW(ATR(14), 2.5)");
  ASSERT(rule != NULL, "Failed to parse ATR rule");
  ASSERT(rule->left.type == SAMTRADER_OPERAND_INDICATOR, "Left should be INDICATOR");
  ASSERT(rule->left.indicator.indicator_type == SAMTRADER_IND_ATR, "Left should be ATR");
  ASSERT(rule->left.indicator.period == 14, "ATR period should be 14");

  samrena_destroy(arena);
  printf("  PASS\n");
  return 0;
}

/*============================================================================
 * Price Field Parsing Tests
 *============================================================================*/

static int test_parse_all_price_fields(void) {
  printf("Testing parse all price fields...\n");

  Samrena *arena = samrena_create_default();
  ASSERT(arena != NULL, "Failed to create arena");

  SamtraderRule *rule = samtrader_rule_parse(arena, "ABOVE(open, 100)");
  ASSERT(rule != NULL, "Failed to parse with open");
  ASSERT(rule->left.type == SAMTRADER_OPERAND_PRICE_OPEN, "Should be PRICE_OPEN");

  rule = samtrader_rule_parse(arena, "ABOVE(high, 100)");
  ASSERT(rule != NULL, "Failed to parse with high");
  ASSERT(rule->left.type == SAMTRADER_OPERAND_PRICE_HIGH, "Should be PRICE_HIGH");

  rule = samtrader_rule_parse(arena, "ABOVE(low, 100)");
  ASSERT(rule != NULL, "Failed to parse with low");
  ASSERT(rule->left.type == SAMTRADER_OPERAND_PRICE_LOW, "Should be PRICE_LOW");

  rule = samtrader_rule_parse(arena, "ABOVE(close, 100)");
  ASSERT(rule != NULL, "Failed to parse with close");
  ASSERT(rule->left.type == SAMTRADER_OPERAND_PRICE_CLOSE, "Should be PRICE_CLOSE");

  rule = samtrader_rule_parse(arena, "ABOVE(volume, 100)");
  ASSERT(rule != NULL, "Failed to parse with volume");
  ASSERT(rule->left.type == SAMTRADER_OPERAND_VOLUME, "Should be VOLUME");

  samrena_destroy(arena);
  printf("  PASS\n");
  return 0;
}

/*============================================================================
 * Complex / Nested Rule Parsing Tests
 *============================================================================*/

static int test_parse_complex_nested(void) {
  printf("Testing parse complex nested rule...\n");

  Samrena *arena = samrena_create_default();
  ASSERT(arena != NULL, "Failed to create arena");

  /* AND(CROSS_ABOVE(close, SMA(20)), BETWEEN(RSI(14), 30, 70), NOT(BELOW(close, EMA(50)))) */
  SamtraderRule *rule = samtrader_rule_parse(
      arena,
      "AND(CROSS_ABOVE(close, SMA(20)), BETWEEN(RSI(14), 30, 70), NOT(BELOW(close, EMA(50))))");
  ASSERT(rule != NULL, "Failed to parse complex nested rule");
  ASSERT(rule->type == SAMTRADER_RULE_AND, "Root should be AND");
  ASSERT(samtrader_rule_child_count(rule) == 3, "Should have 3 children");

  ASSERT(rule->children[0]->type == SAMTRADER_RULE_CROSS_ABOVE, "Child 0 should be CROSS_ABOVE");
  ASSERT(rule->children[1]->type == SAMTRADER_RULE_BETWEEN, "Child 1 should be BETWEEN");
  ASSERT(rule->children[2]->type == SAMTRADER_RULE_NOT, "Child 2 should be NOT");
  ASSERT(rule->children[2]->child->type == SAMTRADER_RULE_BELOW, "NOT child should be BELOW");

  /* Verify BETWEEN operands */
  ASSERT(rule->children[1]->left.indicator.indicator_type == SAMTRADER_IND_RSI,
         "BETWEEN left should be RSI");
  ASSERT_DOUBLE_EQ(rule->children[1]->right.constant, 30.0, "BETWEEN lower should be 30");
  ASSERT_DOUBLE_EQ(rule->children[1]->threshold, 70.0, "BETWEEN upper should be 70");

  /* Verify NOT > BELOW operands */
  ASSERT(rule->children[2]->child->right.indicator.indicator_type == SAMTRADER_IND_EMA,
         "BELOW right should be EMA");
  ASSERT(rule->children[2]->child->right.indicator.period == 50, "EMA period should be 50");

  samrena_destroy(arena);
  printf("  PASS\n");
  return 0;
}

static int test_parse_deeply_nested(void) {
  printf("Testing parse deeply nested rule...\n");

  Samrena *arena = samrena_create_default();
  ASSERT(arena != NULL, "Failed to create arena");

  /* CONSECUTIVE(AND(ABOVE(close, SMA(20)), BELOW(close, SMA(200))), 5) */
  SamtraderRule *rule =
      samtrader_rule_parse(arena,
                           "CONSECUTIVE(AND(ABOVE(close, SMA(20)), BELOW(close, SMA(200))), 5)");
  ASSERT(rule != NULL, "Failed to parse deeply nested rule");
  ASSERT(rule->type == SAMTRADER_RULE_CONSECUTIVE, "Root should be CONSECUTIVE");
  ASSERT(rule->lookback == 5, "Lookback should be 5");
  ASSERT(rule->child->type == SAMTRADER_RULE_AND, "Child should be AND");
  ASSERT(samtrader_rule_child_count(rule->child) == 2, "AND should have 2 children");
  ASSERT(rule->child->children[0]->type == SAMTRADER_RULE_ABOVE, "AND child 0 should be ABOVE");
  ASSERT(rule->child->children[1]->type == SAMTRADER_RULE_BELOW, "AND child 1 should be BELOW");

  samrena_destroy(arena);
  printf("  PASS\n");
  return 0;
}

/*============================================================================
 * Whitespace Handling Tests
 *============================================================================*/

static int test_parse_whitespace(void) {
  printf("Testing parse with extra whitespace...\n");

  Samrena *arena = samrena_create_default();
  ASSERT(arena != NULL, "Failed to create arena");

  /* Extra whitespace everywhere */
  SamtraderRule *rule = samtrader_rule_parse(arena, "  ABOVE(  close  ,  SMA( 20 )  )  ");
  ASSERT(rule != NULL, "Failed to parse rule with extra whitespace");
  ASSERT(rule->type == SAMTRADER_RULE_ABOVE, "Type should be ABOVE");
  ASSERT(rule->left.type == SAMTRADER_OPERAND_PRICE_CLOSE, "Left should be PRICE_CLOSE");
  ASSERT(rule->right.indicator.indicator_type == SAMTRADER_IND_SMA, "Right should be SMA");
  ASSERT(rule->right.indicator.period == 20, "SMA period should be 20");

  /* Newlines and tabs */
  rule = samtrader_rule_parse(arena, "AND(\n\tABOVE(close, SMA(20)),\n\tBELOW(close, EMA(50))\n)");
  ASSERT(rule != NULL, "Failed to parse rule with newlines and tabs");
  ASSERT(rule->type == SAMTRADER_RULE_AND, "Type should be AND");
  ASSERT(samtrader_rule_child_count(rule) == 2, "Should have 2 children");

  samrena_destroy(arena);
  printf("  PASS\n");
  return 0;
}

/*============================================================================
 * Integer Constant Parsing Tests
 *============================================================================*/

static int test_parse_integer_constant(void) {
  printf("Testing parse with integer constant operand...\n");

  Samrena *arena = samrena_create_default();
  ASSERT(arena != NULL, "Failed to create arena");

  SamtraderRule *rule = samtrader_rule_parse(arena, "ABOVE(close, 50)");
  ASSERT(rule != NULL, "Failed to parse rule with integer constant");
  ASSERT(rule->right.type == SAMTRADER_OPERAND_CONSTANT, "Right should be CONSTANT");
  ASSERT_DOUBLE_EQ(rule->right.constant, 50.0, "Constant should be 50");

  samrena_destroy(arena);
  printf("  PASS\n");
  return 0;
}

/*============================================================================
 * Additional Indicator Parsing Tests
 *============================================================================*/

static int test_parse_unsupported_indicators(void) {
  printf("Testing parse unsupported indicator types...\n");

  Samrena *arena = samrena_create_default();
  ASSERT(arena != NULL, "Failed to create arena");

  /* These indicator types are not yet supported by the parser */
  ASSERT(samtrader_rule_parse(arena, "ABOVE(WMA(20), 100)") == NULL, "WMA should not be parseable");
  ASSERT(samtrader_rule_parse(arena, "ABOVE(STDDEV(20), 1.5)") == NULL,
         "STDDEV should not be parseable");
  ASSERT(samtrader_rule_parse(arena, "ABOVE(ROC(14), 0)") == NULL, "ROC should not be parseable");
  ASSERT(samtrader_rule_parse(arena, "ABOVE(OBV, 1000000)") == NULL, "OBV should not be parseable");
  ASSERT(samtrader_rule_parse(arena, "ABOVE(VWAP, 100)") == NULL, "VWAP should not be parseable");
  ASSERT(samtrader_rule_parse(arena, "ABOVE(STOCHASTIC(14, 3), 80)") == NULL,
         "STOCHASTIC should not be parseable");

  samrena_destroy(arena);
  printf("  PASS\n");
  return 0;
}

/*============================================================================
 * Negative Constant Parsing Tests
 *============================================================================*/

static int test_parse_negative_constant(void) {
  printf("Testing parse with negative constant...\n");

  Samrena *arena = samrena_create_default();
  ASSERT(arena != NULL, "Failed to create arena");

  SamtraderRule *rule = samtrader_rule_parse(arena, "ABOVE(close, -50)");
  ASSERT(rule != NULL, "Failed to parse rule with negative constant");
  ASSERT(rule->type == SAMTRADER_RULE_ABOVE, "Type should be ABOVE");
  ASSERT(rule->left.type == SAMTRADER_OPERAND_PRICE_CLOSE, "Left should be PRICE_CLOSE");
  ASSERT(rule->right.type == SAMTRADER_OPERAND_CONSTANT, "Right should be CONSTANT");
  ASSERT_DOUBLE_EQ(rule->right.constant, -50.0, "Constant should be -50");

  /* Negative float */
  rule = samtrader_rule_parse(arena, "BELOW(close, -3.14)");
  ASSERT(rule != NULL, "Failed to parse rule with negative float");
  ASSERT_DOUBLE_EQ(rule->right.constant, -3.14, "Constant should be -3.14");

  samrena_destroy(arena);
  printf("  PASS\n");
  return 0;
}

/*============================================================================
 * Temporal Rule Invalid Lookback Tests
 *============================================================================*/

static int test_parse_temporal_invalid_lookback(void) {
  printf("Testing parse temporal with invalid lookback...\n");

  Samrena *arena = samrena_create_default();
  ASSERT(arena != NULL, "Failed to create arena");

  /* lookback=0 should fail (samtrader_rule_create_temporal rejects lookback <= 0) */
  ASSERT(samtrader_rule_parse(arena, "CONSECUTIVE(ABOVE(close, 50), 0)") == NULL,
         "CONSECUTIVE with lookback 0 should return NULL");

  /* lookback=-1 should fail */
  ASSERT(samtrader_rule_parse(arena, "CONSECUTIVE(ABOVE(close, 50), -1)") == NULL,
         "CONSECUTIVE with lookback -1 should return NULL");

  /* ANY_OF with lookback=0 should also fail */
  ASSERT(samtrader_rule_parse(arena, "ANY_OF(ABOVE(close, 50), 0)") == NULL,
         "ANY_OF with lookback 0 should return NULL");

  samrena_destroy(arena);
  printf("  PASS\n");
  return 0;
}

/*============================================================================
 * Nested Temporal + Composite Parsing Tests
 *============================================================================*/

static int test_parse_nested_temporal_composite(void) {
  printf("Testing parse nested temporal+composite rule...\n");

  Samrena *arena = samrena_create_default();
  ASSERT(arena != NULL, "Failed to create arena");

  /* ANY_OF(AND(ABOVE(close, SMA(20)), BELOW(RSI(14), 70)), 5) */
  SamtraderRule *rule =
      samtrader_rule_parse(arena, "ANY_OF(AND(ABOVE(close, SMA(20)), BELOW(RSI(14), 70)), 5)");
  ASSERT(rule != NULL, "Failed to parse nested temporal+composite rule");
  ASSERT(rule->type == SAMTRADER_RULE_ANY_OF, "Root should be ANY_OF");
  ASSERT(rule->lookback == 5, "Lookback should be 5");
  ASSERT(rule->child != NULL, "Child should not be NULL");
  ASSERT(rule->child->type == SAMTRADER_RULE_AND, "Child should be AND");
  ASSERT(samtrader_rule_child_count(rule->child) == 2, "AND should have 2 children");
  ASSERT(rule->child->children[0]->type == SAMTRADER_RULE_ABOVE, "AND child 0 should be ABOVE");
  ASSERT(rule->child->children[0]->right.indicator.indicator_type == SAMTRADER_IND_SMA,
         "ABOVE right should be SMA");
  ASSERT(rule->child->children[0]->right.indicator.period == 20, "SMA period should be 20");
  ASSERT(rule->child->children[1]->type == SAMTRADER_RULE_BELOW, "AND child 1 should be BELOW");
  ASSERT(rule->child->children[1]->left.indicator.indicator_type == SAMTRADER_IND_RSI,
         "BELOW left should be RSI");
  ASSERT(rule->child->children[1]->left.indicator.period == 14, "RSI period should be 14");
  ASSERT_DOUBLE_EQ(rule->child->children[1]->right.constant, 70.0, "BELOW right should be 70");

  samrena_destroy(arena);
  printf("  PASS\n");
  return 0;
}

/*============================================================================
 * Main
 *============================================================================*/

int main(void) {
  printf("=== Rule Parser Tests ===\n\n");

  int failures = 0;

  /* Null / invalid input tests */
  failures += test_parse_null_inputs();
  failures += test_parse_empty_and_invalid();

  /* Comparison rule tests */
  failures += test_parse_cross_above();
  failures += test_parse_cross_below();
  failures += test_parse_above();
  failures += test_parse_below();
  failures += test_parse_equals();

  /* BETWEEN rule tests */
  failures += test_parse_between();
  failures += test_parse_between_float_bounds();

  /* Composite rule tests */
  failures += test_parse_and();
  failures += test_parse_or();
  failures += test_parse_not();

  /* Temporal rule tests */
  failures += test_parse_consecutive();
  failures += test_parse_any_of();

  /* Indicator tests */
  failures += test_parse_macd_indicator();
  failures += test_parse_bollinger_indicators();
  failures += test_parse_pivot_indicators();
  failures += test_parse_atr_indicator();

  /* Price field tests */
  failures += test_parse_all_price_fields();

  /* Constant tests */
  failures += test_parse_integer_constant();

  /* Complex / nested tests */
  failures += test_parse_complex_nested();
  failures += test_parse_deeply_nested();

  /* Whitespace tests */
  failures += test_parse_whitespace();

  /* Additional indicator tests */
  failures += test_parse_unsupported_indicators();

  /* Negative constant tests */
  failures += test_parse_negative_constant();

  /* Temporal invalid lookback tests */
  failures += test_parse_temporal_invalid_lookback();

  /* Nested temporal + composite tests */
  failures += test_parse_nested_temporal_composite();

  printf("\n=== Results: %d failures ===\n", failures);

  return failures > 0 ? EXIT_FAILURE : EXIT_SUCCESS;
}
