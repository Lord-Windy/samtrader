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

#ifndef SAMTRADER_PORTS_DATA_PORT_H
#define SAMTRADER_PORTS_DATA_PORT_H

#include <time.h>

#include <samrena.h>
#include <samvector.h>

/**
 * @brief Forward declaration of the data port structure.
 *
 * The SamtraderDataPort is an interface (port) for data sources in the
 * hexagonal architecture. Concrete implementations (adapters) provide the
 * actual data access logic.
 */
typedef struct SamtraderDataPort SamtraderDataPort;

/**
 * @brief Function type for fetching OHLCV data.
 *
 * Fetches OHLCV price data for a specific symbol within a date range.
 *
 * @param port The data port instance
 * @param code Stock symbol (e.g., "AAPL", "BHP")
 * @param exchange Exchange identifier (e.g., "US", "AU")
 * @param start_date Start of date range (inclusive)
 * @param end_date End of date range (inclusive)
 * @return Vector of SamtraderOhlcv pointers, or NULL on failure.
 *         The vector is arena-allocated and owned by the port's arena.
 */
typedef SamrenaVector *(*SamtraderDataFetchFn)(SamtraderDataPort *port, const char *code,
                                               const char *exchange, time_t start_date,
                                               time_t end_date);

/**
 * @brief Function type for listing available symbols.
 *
 * Lists all available stock symbols for a given exchange.
 *
 * @param port The data port instance
 * @param exchange Exchange identifier (e.g., "US", "AU"), or NULL for all
 * @return Vector of arena-allocated strings (const char *), or NULL on failure
 */
typedef SamrenaVector *(*SamtraderDataListSymbolsFn)(SamtraderDataPort *port, const char *exchange);

/**
 * @brief Function type for closing the data port.
 *
 * Releases any resources held by the adapter (e.g., database connections).
 * Memory allocated through the arena is not freed here.
 *
 * @param port The data port instance to close
 */
typedef void (*SamtraderDataCloseFn)(SamtraderDataPort *port);

/**
 * @brief Data port interface for OHLCV data sources.
 *
 * This is the port (interface) in the hexagonal architecture pattern.
 * Adapters implement this interface to provide data from various sources
 * (PostgreSQL, CSV files, APIs, etc.).
 *
 * Usage:
 * @code
 * // Create adapter (e.g., PostgreSQL)
 * SamtraderDataPort *data = samtrader_postgres_adapter_create(arena, conninfo);
 *
 * // Fetch data
 * SamrenaVector *ohlcv = data->fetch_ohlcv(data, "AAPL", "US",
 *                                           start_date, end_date);
 *
 * // List symbols
 * SamrenaVector *symbols = data->list_symbols(data, "US");
 *
 * // Clean up
 * data->close(data);
 * @endcode
 */
struct SamtraderDataPort {
  void *impl;                              /**< Adapter-specific implementation */
  Samrena *arena;                          /**< Memory arena for allocations */
  SamtraderDataFetchFn fetch_ohlcv;        /**< Fetch OHLCV data function */
  SamtraderDataListSymbolsFn list_symbols; /**< List symbols function */
  SamtraderDataCloseFn close;              /**< Close/cleanup function */
};

#endif /* SAMTRADER_PORTS_DATA_PORT_H */
