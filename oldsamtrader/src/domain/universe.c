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

#include "samtrader/domain/universe.h"

#include <ctype.h>
#include <stdio.h>
#include <string.h>

#include "samtrader/ports/data_port.h"
#include "samtrader/samtrader.h"

/* --- Error callback infrastructure ---
 * samtrader_set_error_callback and samtrader_error_string are declared in
 * samtrader.h but implemented nowhere in the codebase.
 * TODO: extract to src/error.c when other modules need this
 */

static SamtraderErrorCallback g_error_callback = NULL;
static void *g_error_user_data = NULL;

void samtrader_set_error_callback(SamtraderErrorCallback callback, void *user_data) {
  g_error_callback = callback;
  g_error_user_data = user_data;
}

const char *samtrader_error_string(SamtraderError error) {
  switch (error) {
    case SAMTRADER_ERROR_NONE:
      return "no error";
    case SAMTRADER_ERROR_NULL_PARAM:
      return "null parameter";
    case SAMTRADER_ERROR_MEMORY:
      return "memory allocation failed";
    case SAMTRADER_ERROR_DB_CONNECTION:
      return "database connection failed";
    case SAMTRADER_ERROR_DB_QUERY:
      return "database query failed";
    case SAMTRADER_ERROR_CONFIG_PARSE:
      return "config parse error";
    case SAMTRADER_ERROR_CONFIG_MISSING:
      return "config missing";
    case SAMTRADER_ERROR_RULE_PARSE:
      return "rule parse error";
    case SAMTRADER_ERROR_RULE_INVALID:
      return "rule invalid";
    case SAMTRADER_ERROR_NO_DATA:
      return "no data";
    case SAMTRADER_ERROR_INSUFFICIENT_DATA:
      return "insufficient data";
    case SAMTRADER_ERROR_IO:
      return "I/O error";
  }
  return "unknown error";
}

static void report_error(SamtraderError error, const char *message) {
  if (g_error_callback) {
    g_error_callback(error, message, g_error_user_data);
  }
}

/* --- Arena string helpers --- */

static const char *arena_strdup(Samrena *arena, const char *src, size_t len) {
  char *copy = (char *)samrena_push(arena, len + 1);
  if (!copy) {
    return NULL;
  }
  memcpy(copy, src, len);
  copy[len] = '\0';
  return copy;
}

/* --- Universe parsing --- */

SamtraderUniverse *samtrader_universe_parse(Samrena *arena, const char *codes_str,
                                            const char *exchange) {
  if (!arena || !codes_str || !exchange) {
    report_error(SAMTRADER_ERROR_NULL_PARAM, "null parameter passed to universe_parse");
    return NULL;
  }

  /* Skip leading whitespace */
  const char *p = codes_str;
  while (isspace((unsigned char)*p)) {
    p++;
  }

  if (*p == '\0') {
    report_error(SAMTRADER_ERROR_CONFIG_PARSE, "no codes specified");
    return NULL;
  }

  /* Count commas to determine max codes */
  size_t max_codes = 1;
  for (const char *c = p; *c != '\0'; c++) {
    if (*c == ',') {
      max_codes++;
    }
  }

  const char **codes = SAMRENA_PUSH_ARRAY_ZERO(arena, const char *, max_codes);
  if (!codes) {
    report_error(SAMTRADER_ERROR_MEMORY, "failed to allocate codes array");
    return NULL;
  }

  size_t count = 0;
  const char *start = p;

  while (1) {
    /* Find end of current token (comma or end of string) */
    const char *end = start;
    while (*end != ',' && *end != '\0') {
      end++;
    }

    /* Trim leading whitespace of token */
    const char *tok_start = start;
    while (tok_start < end && isspace((unsigned char)*tok_start)) {
      tok_start++;
    }

    /* Trim trailing whitespace of token */
    const char *tok_end = end;
    while (tok_end > tok_start && isspace((unsigned char)*(tok_end - 1))) {
      tok_end--;
    }

    size_t tok_len = (size_t)(tok_end - tok_start);

    if (tok_len == 0) {
      report_error(SAMTRADER_ERROR_CONFIG_PARSE, "empty code in codes list");
      return NULL;
    }

    /* Uppercase the token into arena */
    char *code_buf = (char *)samrena_push(arena, tok_len + 1);
    if (!code_buf) {
      report_error(SAMTRADER_ERROR_MEMORY, "failed to allocate code string");
      return NULL;
    }
    for (size_t i = 0; i < tok_len; i++) {
      code_buf[i] = (char)toupper((unsigned char)tok_start[i]);
    }
    code_buf[tok_len] = '\0';

    /* Duplicate check */
    for (size_t i = 0; i < count; i++) {
      if (strcmp(codes[i], code_buf) == 0) {
        char msg[128];
        snprintf(msg, sizeof(msg), "duplicate code: %s", code_buf);
        report_error(SAMTRADER_ERROR_CONFIG_PARSE, msg);
        return NULL;
      }
    }

    codes[count++] = code_buf;

    if (*end == '\0') {
      break;
    }
    start = end + 1;
  }

  /* Copy exchange to arena */
  const char *exchange_copy = arena_strdup(arena, exchange, strlen(exchange));
  if (!exchange_copy) {
    report_error(SAMTRADER_ERROR_MEMORY, "failed to allocate exchange string");
    return NULL;
  }

  SamtraderUniverse *universe = SAMRENA_PUSH_TYPE_ZERO(arena, SamtraderUniverse);
  if (!universe) {
    report_error(SAMTRADER_ERROR_MEMORY, "failed to allocate universe");
    return NULL;
  }

  universe->codes = codes;
  universe->count = count;
  universe->exchange = exchange_copy;

  return universe;
}

/* --- Universe validation --- */

int samtrader_universe_validate(SamtraderUniverse *universe, SamtraderDataPort *data_port,
                                time_t start_date, time_t end_date) {
  if (!universe || !data_port) {
    return -1;
  }

  size_t write_idx = 0;

  for (size_t read_idx = 0; read_idx < universe->count; read_idx++) {
    SamrenaVector *result = data_port->fetch_ohlcv(data_port, universe->codes[read_idx],
                                                   universe->exchange, start_date, end_date);

    if (result && samrena_vector_size(result) >= SAMTRADER_MIN_OHLCV_BARS) {
      universe->codes[write_idx++] = universe->codes[read_idx];
    } else {
      size_t bars = result ? samrena_vector_size(result) : 0;
      fprintf(stderr, "Warning: skipping %s.%s (%zu bars, minimum %d required)\n",
              universe->codes[read_idx], universe->exchange, bars, SAMTRADER_MIN_OHLCV_BARS);
    }
  }

  universe->count = write_idx;
  return write_idx == 0 ? -1 : (int)write_idx;
}
