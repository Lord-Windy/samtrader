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

#ifndef SAMTRADER_H
#define SAMTRADER_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <time.h>

#include <samdata/samhashmap.h>
#include <samdata/samset.h>
#include <samrena.h>

/* Error codes */
typedef enum {
  SAMTRADER_ERROR_NONE = 0,
  SAMTRADER_ERROR_NULL_PARAM,
  SAMTRADER_ERROR_MEMORY,
  SAMTRADER_ERROR_DB_CONNECTION,
  SAMTRADER_ERROR_DB_QUERY,
  SAMTRADER_ERROR_CONFIG_PARSE,
  SAMTRADER_ERROR_CONFIG_MISSING,
  SAMTRADER_ERROR_RULE_PARSE,
  SAMTRADER_ERROR_RULE_INVALID,
  SAMTRADER_ERROR_NO_DATA,
  SAMTRADER_ERROR_INSUFFICIENT_DATA,
  SAMTRADER_ERROR_IO
} SamtraderError;

/* Get human-readable error string */
const char *samtrader_error_string(SamtraderError error);

/* Error callback type */
typedef void (*SamtraderErrorCallback)(SamtraderError error, const char *message, void *user_data);

/* Set global error callback */
void samtrader_set_error_callback(SamtraderErrorCallback callback, void *user_data);

#endif /* SAMTRADER_H */
