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

#ifndef SAMTRADER_ADAPTERS_POSTGRES_ADAPTER_H
#define SAMTRADER_ADAPTERS_POSTGRES_ADAPTER_H

#include <samrena.h>

#include "samtrader/ports/data_port.h"

/**
 * @brief Create a PostgreSQL data adapter.
 *
 * Creates a data port implementation that connects to a PostgreSQL database
 * and fetches OHLCV data from the `ohlcv` table.
 *
 * Expected database schema:
 * @code{.sql}
 * CREATE TABLE public.ohlcv (
 *     code character varying NOT NULL,
 *     exchange character varying NOT NULL,
 *     date timestamp with time zone NOT NULL,
 *     open numeric NOT NULL,
 *     high numeric NOT NULL,
 *     low numeric NOT NULL,
 *     close numeric NOT NULL,
 *     volume integer NOT NULL
 * );
 * @endcode
 *
 * @param arena Memory arena for allocations. The adapter and all data returned
 *              by its methods are allocated from this arena.
 * @param conninfo PostgreSQL connection string.
 *                 Format: "postgres://user:pass@host:port/dbname"
 *                 or libpq keyword/value format: "host=localhost dbname=samtrader"
 * @return Pointer to the created data port, or NULL on failure.
 *         On failure, use samtrader_error_string() to get the error message.
 *
 * @note The connection is established immediately. The adapter must be closed
 *       with port->close(port) when done to release the database connection.
 *
 * Usage:
 * @code
 * SamtraderDataPort *data = samtrader_postgres_adapter_create(arena,
 *     "postgres://user:pass@localhost:5432/samtrader");
 * if (!data) {
 *     // Handle connection error
 * }
 *
 * // Fetch OHLCV data
 * time_t start = ..., end = ...;
 * SamrenaVector *ohlcv = data->fetch_ohlcv(data, "AAPL", "US", start, end);
 *
 * // Clean up
 * data->close(data);
 * @endcode
 */
SamtraderDataPort *samtrader_postgres_adapter_create(Samrena *arena, const char *conninfo);

#endif /* SAMTRADER_ADAPTERS_POSTGRES_ADAPTER_H */
