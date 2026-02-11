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

#ifndef SAMTRADER_DOMAIN_STRATEGY_H
#define SAMTRADER_DOMAIN_STRATEGY_H

#include "samtrader/domain/rule.h"

/**
 * @brief Trading strategy definition.
 *
 * Defines entry/exit rules, position sizing, and risk parameters
 * for a trading strategy. All string fields are arena-allocated.
 */
typedef struct {
  const char *name;        /**< Strategy name */
  const char *description; /**< Strategy description */

  SamtraderRule *entry_long;  /**< When to buy */
  SamtraderRule *exit_long;   /**< When to sell long position */
  SamtraderRule *entry_short; /**< When to short (NULL if long-only) */
  SamtraderRule *exit_short;  /**< When to cover short */

  double position_size;   /**< Fraction of portfolio (0.0-1.0) */
  double stop_loss_pct;   /**< Stop loss percentage (0 = none) */
  double take_profit_pct; /**< Take profit percentage (0 = none) */
  int max_positions;      /**< Maximum concurrent positions */
} SamtraderStrategy;

#endif /* SAMTRADER_DOMAIN_STRATEGY_H */
