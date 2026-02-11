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

#define _GNU_SOURCE /* For timegm */

#include "samtrader/adapters/postgres_adapter.h"

#include <libpq-fe.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "samtrader/domain/ohlcv.h"
#include "samtrader/samtrader.h"

/**
 * @brief Internal structure holding PostgreSQL adapter state.
 */
typedef struct {
  PGconn *conn;
} PostgresAdapterImpl;

/* Forward declarations of interface functions */
static SamrenaVector *postgres_fetch_ohlcv(SamtraderDataPort *port, const char *code,
                                           const char *exchange, time_t start_date,
                                           time_t end_date);
static SamrenaVector *postgres_list_symbols(SamtraderDataPort *port, const char *exchange);
static void postgres_close(SamtraderDataPort *port);

/* Helper to convert time_t to ISO 8601 string */
static void time_to_iso8601(time_t t, char *buf, size_t buf_size) {
  struct tm *tm_info = gmtime(&t);
  strftime(buf, buf_size, "%Y-%m-%d", tm_info);
}

/* Helper to parse timestamp from PostgreSQL to time_t */
static time_t parse_pg_timestamp(const char *str) {
  struct tm tm_info = {0};
  int year, month, day;

  if (sscanf(str, "%d-%d-%d", &year, &month, &day) == 3) {
    tm_info.tm_year = year - 1900;
    tm_info.tm_mon = month - 1;
    tm_info.tm_mday = day;
    tm_info.tm_hour = 0;
    tm_info.tm_min = 0;
    tm_info.tm_sec = 0;
    return timegm(&tm_info);
  }

  return 0;
}

SamtraderDataPort *samtrader_postgres_adapter_create(Samrena *arena, const char *conninfo) {
  if (!arena || !conninfo) {
    return NULL;
  }

  /* Attempt to connect to PostgreSQL */
  PGconn *conn = PQconnectdb(conninfo);
  if (PQstatus(conn) != CONNECTION_OK) {
    PQfinish(conn);
    return NULL;
  }

  /* Allocate the adapter implementation */
  PostgresAdapterImpl *impl = SAMRENA_PUSH_TYPE_ZERO(arena, PostgresAdapterImpl);
  if (!impl) {
    PQfinish(conn);
    return NULL;
  }
  impl->conn = conn;

  /* Allocate the data port */
  SamtraderDataPort *port = SAMRENA_PUSH_TYPE_ZERO(arena, SamtraderDataPort);
  if (!port) {
    PQfinish(conn);
    return NULL;
  }

  port->impl = impl;
  port->arena = arena;
  port->fetch_ohlcv = postgres_fetch_ohlcv;
  port->list_symbols = postgres_list_symbols;
  port->close = postgres_close;

  return port;
}

static SamrenaVector *postgres_fetch_ohlcv(SamtraderDataPort *port, const char *code,
                                           const char *exchange, time_t start_date,
                                           time_t end_date) {
  if (!port || !port->impl || !code || !exchange) {
    return NULL;
  }

  PostgresAdapterImpl *impl = (PostgresAdapterImpl *)port->impl;
  Samrena *arena = port->arena;

  /* Convert dates to ISO 8601 strings */
  char start_str[32];
  char end_str[32];
  time_to_iso8601(start_date, start_str, sizeof(start_str));
  time_to_iso8601(end_date, end_str, sizeof(end_str));

  /* Build query with parameters for SQL injection prevention */
  const char *query = "SELECT code, exchange, date, open, high, low, close, volume "
                      "FROM ohlcv "
                      "WHERE code = $1 AND exchange = $2 AND date >= $3 AND date <= $4 "
                      "ORDER BY date ASC";

  const char *param_values[4] = {code, exchange, start_str, end_str};

  PGresult *result = PQexecParams(impl->conn, query, 4, NULL, param_values, NULL, NULL, 0);

  if (PQresultStatus(result) != PGRES_TUPLES_OK) {
    PQclear(result);
    return NULL;
  }

  int num_rows = PQntuples(result);
  if (num_rows == 0) {
    PQclear(result);
    /* Return empty vector, not NULL (no error, just no data) */
    return samtrader_ohlcv_vector_create(arena, 0);
  }

  /* Create vector to hold OHLCV records */
  SamrenaVector *ohlcv_vec = samtrader_ohlcv_vector_create(arena, (uint64_t)num_rows);
  if (!ohlcv_vec) {
    PQclear(result);
    return NULL;
  }

  /* Get column indices */
  int col_code = PQfnumber(result, "code");
  int col_exchange = PQfnumber(result, "exchange");
  int col_date = PQfnumber(result, "date");
  int col_open = PQfnumber(result, "open");
  int col_high = PQfnumber(result, "high");
  int col_low = PQfnumber(result, "low");
  int col_close = PQfnumber(result, "close");
  int col_volume = PQfnumber(result, "volume");

  /* Parse each row */
  for (int i = 0; i < num_rows; i++) {
    const char *row_code = PQgetvalue(result, i, col_code);
    const char *row_exchange = PQgetvalue(result, i, col_exchange);
    const char *row_date = PQgetvalue(result, i, col_date);
    const char *row_open = PQgetvalue(result, i, col_open);
    const char *row_high = PQgetvalue(result, i, col_high);
    const char *row_low = PQgetvalue(result, i, col_low);
    const char *row_close = PQgetvalue(result, i, col_close);
    const char *row_volume = PQgetvalue(result, i, col_volume);

    time_t date = parse_pg_timestamp(row_date);
    double open = strtod(row_open, NULL);
    double high = strtod(row_high, NULL);
    double low = strtod(row_low, NULL);
    double close = strtod(row_close, NULL);
    int64_t volume = strtoll(row_volume, NULL, 10);

    /* Create OHLCV record and add to vector */
    SamtraderOhlcv *ohlcv =
        samtrader_ohlcv_create(arena, row_code, row_exchange, date, open, high, low, close, volume);

    if (!ohlcv) {
      PQclear(result);
      return NULL;
    }

    /* Add the struct to the vector */
    if (!samrena_vector_push(ohlcv_vec, ohlcv)) {
      PQclear(result);
      return NULL;
    }
  }

  PQclear(result);
  return ohlcv_vec;
}

static SamrenaVector *postgres_list_symbols(SamtraderDataPort *port, const char *exchange) {
  if (!port || !port->impl) {
    return NULL;
  }

  PostgresAdapterImpl *impl = (PostgresAdapterImpl *)port->impl;
  Samrena *arena = port->arena;

  PGresult *result;

  if (exchange) {
    /* Filter by exchange */
    const char *query = "SELECT DISTINCT code FROM ohlcv WHERE exchange = $1 ORDER BY code";
    const char *param_values[1] = {exchange};
    result = PQexecParams(impl->conn, query, 1, NULL, param_values, NULL, NULL, 0);
  } else {
    /* All exchanges */
    const char *query = "SELECT DISTINCT code FROM ohlcv ORDER BY code";
    result = PQexec(impl->conn, query);
  }

  if (PQresultStatus(result) != PGRES_TUPLES_OK) {
    PQclear(result);
    return NULL;
  }

  int num_rows = PQntuples(result);

  /* Create vector to hold symbol strings (pointers) */
  SamrenaVector *symbols_vec = samrena_vector_init(arena, sizeof(const char *), (uint64_t)num_rows);
  if (!symbols_vec) {
    PQclear(result);
    return NULL;
  }

  int col_code = PQfnumber(result, "code");

  for (int i = 0; i < num_rows; i++) {
    const char *row_code = PQgetvalue(result, i, col_code);

    /* Copy string to arena */
    size_t len = strlen(row_code) + 1;
    char *code_copy = (char *)samrena_push(arena, len);
    if (!code_copy) {
      PQclear(result);
      return NULL;
    }
    memcpy(code_copy, row_code, len);

    /* Add pointer to vector */
    if (!samrena_vector_push(symbols_vec, &code_copy)) {
      PQclear(result);
      return NULL;
    }
  }

  PQclear(result);
  return symbols_vec;
}

static void postgres_close(SamtraderDataPort *port) {
  if (!port || !port->impl) {
    return;
  }

  PostgresAdapterImpl *impl = (PostgresAdapterImpl *)port->impl;
  if (impl->conn) {
    PQfinish(impl->conn);
    impl->conn = NULL;
  }
}
