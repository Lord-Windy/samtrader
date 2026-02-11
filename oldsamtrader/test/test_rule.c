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
#include <string.h>

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
 * Operand Construction Tests
 *============================================================================*/

static int test_operand_constant(void) {
  printf("Testing samtrader_operand_constant...\n");

  SamtraderOperand op = samtrader_operand_constant(42.5);
  ASSERT(op.type == SAMTRADER_OPERAND_CONSTANT, "Type should be CONSTANT");
  ASSERT_DOUBLE_EQ(op.constant, 42.5, "Constant value");

  op = samtrader_operand_constant(0.0);
  ASSERT_DOUBLE_EQ(op.constant, 0.0, "Zero constant");

  op = samtrader_operand_constant(-100.0);
  ASSERT_DOUBLE_EQ(op.constant, -100.0, "Negative constant");

  printf("  PASS\n");
  return 0;
}

static int test_operand_price(void) {
  printf("Testing samtrader_operand_price...\n");

  SamtraderOperand op = samtrader_operand_price(SAMTRADER_OPERAND_PRICE_CLOSE);
  ASSERT(op.type == SAMTRADER_OPERAND_PRICE_CLOSE, "Type should be PRICE_CLOSE");

  op = samtrader_operand_price(SAMTRADER_OPERAND_PRICE_OPEN);
  ASSERT(op.type == SAMTRADER_OPERAND_PRICE_OPEN, "Type should be PRICE_OPEN");

  op = samtrader_operand_price(SAMTRADER_OPERAND_PRICE_HIGH);
  ASSERT(op.type == SAMTRADER_OPERAND_PRICE_HIGH, "Type should be PRICE_HIGH");

  op = samtrader_operand_price(SAMTRADER_OPERAND_PRICE_LOW);
  ASSERT(op.type == SAMTRADER_OPERAND_PRICE_LOW, "Type should be PRICE_LOW");

  op = samtrader_operand_price(SAMTRADER_OPERAND_VOLUME);
  ASSERT(op.type == SAMTRADER_OPERAND_VOLUME, "Type should be VOLUME");

  printf("  PASS\n");
  return 0;
}

static int test_operand_indicator_simple(void) {
  printf("Testing samtrader_operand_indicator...\n");

  SamtraderOperand op = samtrader_operand_indicator(SAMTRADER_IND_SMA, 20);
  ASSERT(op.type == SAMTRADER_OPERAND_INDICATOR, "Type should be INDICATOR");
  ASSERT(op.indicator.indicator_type == SAMTRADER_IND_SMA, "Indicator type should be SMA");
  ASSERT(op.indicator.period == 20, "Period should be 20");
  ASSERT(op.indicator.param2 == 0, "param2 should be 0");
  ASSERT(op.indicator.param3 == 0, "param3 should be 0");

  op = samtrader_operand_indicator(SAMTRADER_IND_RSI, 14);
  ASSERT(op.indicator.indicator_type == SAMTRADER_IND_RSI, "Indicator type should be RSI");
  ASSERT(op.indicator.period == 14, "Period should be 14");

  printf("  PASS\n");
  return 0;
}

static int test_operand_indicator_multi(void) {
  printf("Testing samtrader_operand_indicator_multi...\n");

  SamtraderOperand op = samtrader_operand_indicator_multi(SAMTRADER_IND_MACD, 12, 26, 9);
  ASSERT(op.type == SAMTRADER_OPERAND_INDICATOR, "Type should be INDICATOR");
  ASSERT(op.indicator.indicator_type == SAMTRADER_IND_MACD, "Indicator type should be MACD");
  ASSERT(op.indicator.period == 12, "Period (fast) should be 12");
  ASSERT(op.indicator.param2 == 26, "param2 (slow) should be 26");
  ASSERT(op.indicator.param3 == 9, "param3 (signal) should be 9");

  op = samtrader_operand_indicator_multi(SAMTRADER_IND_STOCHASTIC, 14, 3, 0);
  ASSERT(op.indicator.indicator_type == SAMTRADER_IND_STOCHASTIC,
         "Indicator type should be STOCHASTIC");
  ASSERT(op.indicator.period == 14, "Period (k) should be 14");
  ASSERT(op.indicator.param2 == 3, "param2 (d) should be 3");

  printf("  PASS\n");
  return 0;
}

/*============================================================================
 * Comparison Rule Tests
 *============================================================================*/

static int test_rule_create_comparison(void) {
  printf("Testing samtrader_rule_create_comparison...\n");

  Samrena *arena = samrena_create_default();
  ASSERT(arena != NULL, "Failed to create arena");

  SamtraderOperand left = samtrader_operand_price(SAMTRADER_OPERAND_PRICE_CLOSE);
  SamtraderOperand right = samtrader_operand_indicator(SAMTRADER_IND_SMA, 20);

  /* CROSS_ABOVE */
  SamtraderRule *rule =
      samtrader_rule_create_comparison(arena, SAMTRADER_RULE_CROSS_ABOVE, left, right);
  ASSERT(rule != NULL, "Failed to create CROSS_ABOVE rule");
  ASSERT(rule->type == SAMTRADER_RULE_CROSS_ABOVE, "Type should be CROSS_ABOVE");
  ASSERT(rule->left.type == SAMTRADER_OPERAND_PRICE_CLOSE, "Left should be PRICE_CLOSE");
  ASSERT(rule->right.type == SAMTRADER_OPERAND_INDICATOR, "Right should be INDICATOR");
  ASSERT(rule->right.indicator.indicator_type == SAMTRADER_IND_SMA,
         "Right indicator should be SMA");
  ASSERT(rule->children == NULL, "Children should be NULL");
  ASSERT(rule->child == NULL, "Child should be NULL");

  /* ABOVE */
  rule = samtrader_rule_create_comparison(arena, SAMTRADER_RULE_ABOVE, left, right);
  ASSERT(rule != NULL, "Failed to create ABOVE rule");
  ASSERT(rule->type == SAMTRADER_RULE_ABOVE, "Type should be ABOVE");

  /* BELOW */
  rule = samtrader_rule_create_comparison(arena, SAMTRADER_RULE_BELOW, left, right);
  ASSERT(rule != NULL, "Failed to create BELOW rule");
  ASSERT(rule->type == SAMTRADER_RULE_BELOW, "Type should be BELOW");

  /* EQUALS */
  rule = samtrader_rule_create_comparison(arena, SAMTRADER_RULE_EQUALS, left, right);
  ASSERT(rule != NULL, "Failed to create EQUALS rule");
  ASSERT(rule->type == SAMTRADER_RULE_EQUALS, "Type should be EQUALS");

  /* CROSS_BELOW */
  rule = samtrader_rule_create_comparison(arena, SAMTRADER_RULE_CROSS_BELOW, left, right);
  ASSERT(rule != NULL, "Failed to create CROSS_BELOW rule");
  ASSERT(rule->type == SAMTRADER_RULE_CROSS_BELOW, "Type should be CROSS_BELOW");

  /* Invalid type should return NULL */
  rule = samtrader_rule_create_comparison(arena, SAMTRADER_RULE_AND, left, right);
  ASSERT(rule == NULL, "AND should not be valid for comparison");

  rule = samtrader_rule_create_comparison(arena, SAMTRADER_RULE_BETWEEN, left, right);
  ASSERT(rule == NULL, "BETWEEN should not be valid for comparison");

  samrena_destroy(arena);
  printf("  PASS\n");
  return 0;
}

static int test_rule_create_comparison_null_arena(void) {
  printf("Testing samtrader_rule_create_comparison with NULL arena...\n");

  SamtraderOperand left = samtrader_operand_constant(100.0);
  SamtraderOperand right = samtrader_operand_constant(200.0);

  SamtraderRule *rule = samtrader_rule_create_comparison(NULL, SAMTRADER_RULE_ABOVE, left, right);
  ASSERT(rule == NULL, "Should return NULL with NULL arena");

  printf("  PASS\n");
  return 0;
}

/*============================================================================
 * BETWEEN Rule Tests
 *============================================================================*/

static int test_rule_create_between(void) {
  printf("Testing samtrader_rule_create_between...\n");

  Samrena *arena = samrena_create_default();
  ASSERT(arena != NULL, "Failed to create arena");

  SamtraderOperand value = samtrader_operand_indicator(SAMTRADER_IND_RSI, 14);
  SamtraderOperand lower = samtrader_operand_constant(30.0);

  SamtraderRule *rule = samtrader_rule_create_between(arena, value, lower, 70.0);
  ASSERT(rule != NULL, "Failed to create BETWEEN rule");
  ASSERT(rule->type == SAMTRADER_RULE_BETWEEN, "Type should be BETWEEN");
  ASSERT(rule->left.type == SAMTRADER_OPERAND_INDICATOR, "Left should be INDICATOR");
  ASSERT(rule->right.type == SAMTRADER_OPERAND_CONSTANT, "Right (lower) should be CONSTANT");
  ASSERT_DOUBLE_EQ(rule->right.constant, 30.0, "Lower bound");
  ASSERT_DOUBLE_EQ(rule->threshold, 70.0, "Upper bound (threshold)");

  samrena_destroy(arena);
  printf("  PASS\n");
  return 0;
}

/*============================================================================
 * Composite Rule Tests
 *============================================================================*/

static int test_rule_create_and(void) {
  printf("Testing samtrader_rule_create_composite (AND)...\n");

  Samrena *arena = samrena_create_default();
  ASSERT(arena != NULL, "Failed to create arena");

  /* Create two child rules */
  SamtraderOperand close_op = samtrader_operand_price(SAMTRADER_OPERAND_PRICE_CLOSE);
  SamtraderOperand sma20 = samtrader_operand_indicator(SAMTRADER_IND_SMA, 20);
  SamtraderOperand rsi14 = samtrader_operand_indicator(SAMTRADER_IND_RSI, 14);
  SamtraderOperand rsi_thresh = samtrader_operand_constant(30.0);

  SamtraderRule *above_sma =
      samtrader_rule_create_comparison(arena, SAMTRADER_RULE_ABOVE, close_op, sma20);
  ASSERT(above_sma != NULL, "Failed to create ABOVE SMA rule");

  SamtraderRule *rsi_below =
      samtrader_rule_create_comparison(arena, SAMTRADER_RULE_BELOW, rsi14, rsi_thresh);
  ASSERT(rsi_below != NULL, "Failed to create RSI BELOW rule");

  SamtraderRule *children[] = {above_sma, rsi_below};
  SamtraderRule *and_rule = samtrader_rule_create_composite(arena, SAMTRADER_RULE_AND, children, 2);
  ASSERT(and_rule != NULL, "Failed to create AND rule");
  ASSERT(and_rule->type == SAMTRADER_RULE_AND, "Type should be AND");
  ASSERT(and_rule->children != NULL, "Children should not be NULL");
  ASSERT(and_rule->children[0] == above_sma, "First child should be above_sma");
  ASSERT(and_rule->children[1] == rsi_below, "Second child should be rsi_below");
  ASSERT(and_rule->children[2] == NULL, "Children array should be NULL-terminated");

  ASSERT(samtrader_rule_child_count(and_rule) == 2, "Child count should be 2");

  samrena_destroy(arena);
  printf("  PASS\n");
  return 0;
}

static int test_rule_create_or(void) {
  printf("Testing samtrader_rule_create_composite (OR)...\n");

  Samrena *arena = samrena_create_default();
  ASSERT(arena != NULL, "Failed to create arena");

  SamtraderOperand close_op = samtrader_operand_price(SAMTRADER_OPERAND_PRICE_CLOSE);
  SamtraderOperand sma50 = samtrader_operand_indicator(SAMTRADER_IND_SMA, 50);
  SamtraderOperand ema20 = samtrader_operand_indicator(SAMTRADER_IND_EMA, 20);

  SamtraderRule *r1 =
      samtrader_rule_create_comparison(arena, SAMTRADER_RULE_CROSS_ABOVE, close_op, sma50);
  SamtraderRule *r2 =
      samtrader_rule_create_comparison(arena, SAMTRADER_RULE_CROSS_ABOVE, close_op, ema20);
  ASSERT(r1 != NULL && r2 != NULL, "Failed to create child rules");

  SamtraderRule *children[] = {r1, r2};
  SamtraderRule *or_rule = samtrader_rule_create_composite(arena, SAMTRADER_RULE_OR, children, 2);
  ASSERT(or_rule != NULL, "Failed to create OR rule");
  ASSERT(or_rule->type == SAMTRADER_RULE_OR, "Type should be OR");
  ASSERT(samtrader_rule_child_count(or_rule) == 2, "Child count should be 2");

  samrena_destroy(arena);
  printf("  PASS\n");
  return 0;
}

static int test_rule_create_composite_invalid(void) {
  printf("Testing samtrader_rule_create_composite invalid inputs...\n");

  Samrena *arena = samrena_create_default();
  ASSERT(arena != NULL, "Failed to create arena");

  SamtraderOperand left = samtrader_operand_constant(1.0);
  SamtraderOperand right = samtrader_operand_constant(2.0);
  SamtraderRule *r1 = samtrader_rule_create_comparison(arena, SAMTRADER_RULE_ABOVE, left, right);

  SamtraderRule *children[] = {r1};

  /* NULL arena */
  ASSERT(samtrader_rule_create_composite(NULL, SAMTRADER_RULE_AND, children, 1) == NULL,
         "Should fail with NULL arena");

  /* NULL children */
  ASSERT(samtrader_rule_create_composite(arena, SAMTRADER_RULE_AND, NULL, 1) == NULL,
         "Should fail with NULL children");

  /* Zero count */
  ASSERT(samtrader_rule_create_composite(arena, SAMTRADER_RULE_AND, children, 0) == NULL,
         "Should fail with zero count");

  /* Invalid type */
  ASSERT(samtrader_rule_create_composite(arena, SAMTRADER_RULE_ABOVE, children, 1) == NULL,
         "Should fail with non-composite type");

  samrena_destroy(arena);
  printf("  PASS\n");
  return 0;
}

/*============================================================================
 * NOT Rule Tests
 *============================================================================*/

static int test_rule_create_not(void) {
  printf("Testing samtrader_rule_create_not...\n");

  Samrena *arena = samrena_create_default();
  ASSERT(arena != NULL, "Failed to create arena");

  SamtraderOperand close_op = samtrader_operand_price(SAMTRADER_OPERAND_PRICE_CLOSE);
  SamtraderOperand sma20 = samtrader_operand_indicator(SAMTRADER_IND_SMA, 20);

  SamtraderRule *inner =
      samtrader_rule_create_comparison(arena, SAMTRADER_RULE_ABOVE, close_op, sma20);
  ASSERT(inner != NULL, "Failed to create inner rule");

  SamtraderRule *not_rule = samtrader_rule_create_not(arena, inner);
  ASSERT(not_rule != NULL, "Failed to create NOT rule");
  ASSERT(not_rule->type == SAMTRADER_RULE_NOT, "Type should be NOT");
  ASSERT(not_rule->child == inner, "Child should be inner rule");
  ASSERT(not_rule->children == NULL, "Children should be NULL");

  /* NULL inputs */
  ASSERT(samtrader_rule_create_not(NULL, inner) == NULL, "Should fail with NULL arena");
  ASSERT(samtrader_rule_create_not(arena, NULL) == NULL, "Should fail with NULL child");

  samrena_destroy(arena);
  printf("  PASS\n");
  return 0;
}

/*============================================================================
 * Temporal Rule Tests
 *============================================================================*/

static int test_rule_create_temporal(void) {
  printf("Testing samtrader_rule_create_temporal...\n");

  Samrena *arena = samrena_create_default();
  ASSERT(arena != NULL, "Failed to create arena");

  SamtraderOperand close_op = samtrader_operand_price(SAMTRADER_OPERAND_PRICE_CLOSE);
  SamtraderOperand sma20 = samtrader_operand_indicator(SAMTRADER_IND_SMA, 20);

  SamtraderRule *inner =
      samtrader_rule_create_comparison(arena, SAMTRADER_RULE_ABOVE, close_op, sma20);
  ASSERT(inner != NULL, "Failed to create inner rule");

  /* CONSECUTIVE */
  SamtraderRule *consec =
      samtrader_rule_create_temporal(arena, SAMTRADER_RULE_CONSECUTIVE, inner, 5);
  ASSERT(consec != NULL, "Failed to create CONSECUTIVE rule");
  ASSERT(consec->type == SAMTRADER_RULE_CONSECUTIVE, "Type should be CONSECUTIVE");
  ASSERT(consec->child == inner, "Child should be inner rule");
  ASSERT(consec->lookback == 5, "Lookback should be 5");
  ASSERT(consec->children == NULL, "Children should be NULL");

  /* ANY_OF */
  SamtraderRule *any = samtrader_rule_create_temporal(arena, SAMTRADER_RULE_ANY_OF, inner, 10);
  ASSERT(any != NULL, "Failed to create ANY_OF rule");
  ASSERT(any->type == SAMTRADER_RULE_ANY_OF, "Type should be ANY_OF");
  ASSERT(any->child == inner, "Child should be inner rule");
  ASSERT(any->lookback == 10, "Lookback should be 10");

  /* Invalid inputs */
  ASSERT(samtrader_rule_create_temporal(NULL, SAMTRADER_RULE_CONSECUTIVE, inner, 5) == NULL,
         "Should fail with NULL arena");
  ASSERT(samtrader_rule_create_temporal(arena, SAMTRADER_RULE_CONSECUTIVE, NULL, 5) == NULL,
         "Should fail with NULL child");
  ASSERT(samtrader_rule_create_temporal(arena, SAMTRADER_RULE_CONSECUTIVE, inner, 0) == NULL,
         "Should fail with zero lookback");
  ASSERT(samtrader_rule_create_temporal(arena, SAMTRADER_RULE_CONSECUTIVE, inner, -1) == NULL,
         "Should fail with negative lookback");
  ASSERT(samtrader_rule_create_temporal(arena, SAMTRADER_RULE_ABOVE, inner, 5) == NULL,
         "Should fail with non-temporal type");

  samrena_destroy(arena);
  printf("  PASS\n");
  return 0;
}

/*============================================================================
 * Name/Info Function Tests
 *============================================================================*/

static int test_rule_type_name(void) {
  printf("Testing samtrader_rule_type_name...\n");

  ASSERT(strcmp(samtrader_rule_type_name(SAMTRADER_RULE_CROSS_ABOVE), "CROSS_ABOVE") == 0,
         "CROSS_ABOVE name");
  ASSERT(strcmp(samtrader_rule_type_name(SAMTRADER_RULE_CROSS_BELOW), "CROSS_BELOW") == 0,
         "CROSS_BELOW name");
  ASSERT(strcmp(samtrader_rule_type_name(SAMTRADER_RULE_ABOVE), "ABOVE") == 0, "ABOVE name");
  ASSERT(strcmp(samtrader_rule_type_name(SAMTRADER_RULE_BELOW), "BELOW") == 0, "BELOW name");
  ASSERT(strcmp(samtrader_rule_type_name(SAMTRADER_RULE_BETWEEN), "BETWEEN") == 0, "BETWEEN name");
  ASSERT(strcmp(samtrader_rule_type_name(SAMTRADER_RULE_EQUALS), "EQUALS") == 0, "EQUALS name");
  ASSERT(strcmp(samtrader_rule_type_name(SAMTRADER_RULE_AND), "AND") == 0, "AND name");
  ASSERT(strcmp(samtrader_rule_type_name(SAMTRADER_RULE_OR), "OR") == 0, "OR name");
  ASSERT(strcmp(samtrader_rule_type_name(SAMTRADER_RULE_NOT), "NOT") == 0, "NOT name");
  ASSERT(strcmp(samtrader_rule_type_name(SAMTRADER_RULE_CONSECUTIVE), "CONSECUTIVE") == 0,
         "CONSECUTIVE name");
  ASSERT(strcmp(samtrader_rule_type_name(SAMTRADER_RULE_ANY_OF), "ANY_OF") == 0, "ANY_OF name");

  printf("  PASS\n");
  return 0;
}

static int test_operand_type_name(void) {
  printf("Testing samtrader_operand_type_name...\n");

  ASSERT(strcmp(samtrader_operand_type_name(SAMTRADER_OPERAND_PRICE_OPEN), "PRICE_OPEN") == 0,
         "PRICE_OPEN name");
  ASSERT(strcmp(samtrader_operand_type_name(SAMTRADER_OPERAND_PRICE_HIGH), "PRICE_HIGH") == 0,
         "PRICE_HIGH name");
  ASSERT(strcmp(samtrader_operand_type_name(SAMTRADER_OPERAND_PRICE_LOW), "PRICE_LOW") == 0,
         "PRICE_LOW name");
  ASSERT(strcmp(samtrader_operand_type_name(SAMTRADER_OPERAND_PRICE_CLOSE), "PRICE_CLOSE") == 0,
         "PRICE_CLOSE name");
  ASSERT(strcmp(samtrader_operand_type_name(SAMTRADER_OPERAND_VOLUME), "VOLUME") == 0,
         "VOLUME name");
  ASSERT(strcmp(samtrader_operand_type_name(SAMTRADER_OPERAND_INDICATOR), "INDICATOR") == 0,
         "INDICATOR name");
  ASSERT(strcmp(samtrader_operand_type_name(SAMTRADER_OPERAND_CONSTANT), "CONSTANT") == 0,
         "CONSTANT name");

  printf("  PASS\n");
  return 0;
}

/*============================================================================
 * Complex Tree Structure Tests
 *============================================================================*/

static int test_rule_complex_tree(void) {
  printf("Testing complex rule tree construction...\n");

  Samrena *arena = samrena_create_default();
  ASSERT(arena != NULL, "Failed to create arena");

  /*
   * Build a complex rule:
   *   AND(
   *     CROSS_ABOVE(close, SMA(20)),
   *     BETWEEN(RSI(14), 30, 70),
   *     NOT(BELOW(close, EMA(50)))
   *   )
   */
  SamtraderOperand close_op = samtrader_operand_price(SAMTRADER_OPERAND_PRICE_CLOSE);
  SamtraderOperand sma20 = samtrader_operand_indicator(SAMTRADER_IND_SMA, 20);
  SamtraderOperand rsi14 = samtrader_operand_indicator(SAMTRADER_IND_RSI, 14);
  SamtraderOperand lower_bound = samtrader_operand_constant(30.0);
  SamtraderOperand ema50 = samtrader_operand_indicator(SAMTRADER_IND_EMA, 50);

  SamtraderRule *cross_rule =
      samtrader_rule_create_comparison(arena, SAMTRADER_RULE_CROSS_ABOVE, close_op, sma20);
  SamtraderRule *between_rule = samtrader_rule_create_between(arena, rsi14, lower_bound, 70.0);
  SamtraderRule *below_rule =
      samtrader_rule_create_comparison(arena, SAMTRADER_RULE_BELOW, close_op, ema50);
  SamtraderRule *not_rule = samtrader_rule_create_not(arena, below_rule);

  ASSERT(cross_rule != NULL, "Failed to create cross rule");
  ASSERT(between_rule != NULL, "Failed to create between rule");
  ASSERT(below_rule != NULL, "Failed to create below rule");
  ASSERT(not_rule != NULL, "Failed to create not rule");

  SamtraderRule *children[] = {cross_rule, between_rule, not_rule};
  SamtraderRule *and_rule = samtrader_rule_create_composite(arena, SAMTRADER_RULE_AND, children, 3);
  ASSERT(and_rule != NULL, "Failed to create AND rule");
  ASSERT(and_rule->type == SAMTRADER_RULE_AND, "Root should be AND");
  ASSERT(samtrader_rule_child_count(and_rule) == 3, "Should have 3 children");

  /* Verify tree structure */
  ASSERT(and_rule->children[0]->type == SAMTRADER_RULE_CROSS_ABOVE,
         "Child 0 should be CROSS_ABOVE");
  ASSERT(and_rule->children[1]->type == SAMTRADER_RULE_BETWEEN, "Child 1 should be BETWEEN");
  ASSERT(and_rule->children[2]->type == SAMTRADER_RULE_NOT, "Child 2 should be NOT");
  ASSERT(and_rule->children[2]->child->type == SAMTRADER_RULE_BELOW, "NOT child should be BELOW");

  samrena_destroy(arena);
  printf("  PASS\n");
  return 0;
}

static int test_rule_child_count_edge_cases(void) {
  printf("Testing samtrader_rule_child_count edge cases...\n");

  Samrena *arena = samrena_create_default();
  ASSERT(arena != NULL, "Failed to create arena");

  /* NULL rule */
  ASSERT(samtrader_rule_child_count(NULL) == 0, "NULL rule should return 0");

  /* Non-composite rule */
  SamtraderOperand left = samtrader_operand_constant(1.0);
  SamtraderOperand right = samtrader_operand_constant(2.0);
  SamtraderRule *cmp = samtrader_rule_create_comparison(arena, SAMTRADER_RULE_ABOVE, left, right);
  ASSERT(samtrader_rule_child_count(cmp) == 0, "Comparison rule should return 0");

  /* Single-child composite */
  SamtraderRule *children[] = {cmp};
  SamtraderRule *and_rule = samtrader_rule_create_composite(arena, SAMTRADER_RULE_AND, children, 1);
  ASSERT(samtrader_rule_child_count(and_rule) == 1, "Single-child AND should return 1");

  samrena_destroy(arena);
  printf("  PASS\n");
  return 0;
}

/*============================================================================
 * Main
 *============================================================================*/

int main(void) {
  printf("=== Rule Data Structures Tests ===\n\n");

  int failures = 0;

  /* Operand tests */
  failures += test_operand_constant();
  failures += test_operand_price();
  failures += test_operand_indicator_simple();
  failures += test_operand_indicator_multi();

  /* Comparison rule tests */
  failures += test_rule_create_comparison();
  failures += test_rule_create_comparison_null_arena();

  /* BETWEEN rule tests */
  failures += test_rule_create_between();

  /* Composite rule tests */
  failures += test_rule_create_and();
  failures += test_rule_create_or();
  failures += test_rule_create_composite_invalid();

  /* NOT rule tests */
  failures += test_rule_create_not();

  /* Temporal rule tests */
  failures += test_rule_create_temporal();

  /* Name/info tests */
  failures += test_rule_type_name();
  failures += test_operand_type_name();

  /* Complex tree tests */
  failures += test_rule_complex_tree();
  failures += test_rule_child_count_edge_cases();

  printf("\n=== Results: %d failures ===\n", failures);

  return failures > 0 ? EXIT_FAILURE : EXIT_SUCCESS;
}
