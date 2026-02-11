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

#ifndef SAMTRADER_DOMAIN_OHLCV_H
#define SAMTRADER_DOMAIN_OHLCV_H

#include <stdint.h>
#include <time.h>

#include <samrena.h>
#include <samvector.h>

/**
 * @brief OHLCV (Open, High, Low, Close, Volume) price data structure.
 *
 * Represents a single daily price bar for a financial instrument.
 * All strings are arena-allocated and owned by the arena.
 */
typedef struct {
  const char *code;     /**< Stock symbol (e.g., "AAPL", "BHP") */
  const char *exchange; /**< Exchange identifier ("US", "AU") */
  time_t date;          /**< Unix timestamp (daily resolution) */
  double open;          /**< Opening price */
  double high;          /**< Highest price during the period */
  double low;           /**< Lowest price during the period */
  double close;         /**< Closing price */
  int64_t volume;       /**< Trading volume */
} SamtraderOhlcv;

/**
 * @brief Create an OHLCV record with arena-allocated strings.
 *
 * @param arena Memory arena for allocation
 * @param code Stock symbol (will be copied to arena)
 * @param exchange Exchange identifier (will be copied to arena)
 * @param date Unix timestamp
 * @param open Opening price
 * @param high High price
 * @param low Low price
 * @param close Closing price
 * @param volume Trading volume
 * @return Pointer to the created OHLCV record, or NULL on failure
 */
SamtraderOhlcv *samtrader_ohlcv_create(Samrena *arena, const char *code, const char *exchange,
                                       time_t date, double open, double high, double low,
                                       double close, int64_t volume);

/**
 * @brief Create a vector for storing OHLCV records.
 *
 * @param arena Memory arena for allocation
 * @param initial_capacity Initial capacity of the vector
 * @return Pointer to the created vector, or NULL on failure
 */
SamrenaVector *samtrader_ohlcv_vector_create(Samrena *arena, uint64_t initial_capacity);

/**
 * @brief Get the typical price (HLC average) for an OHLCV record.
 *
 * @param ohlcv Pointer to the OHLCV record
 * @return Typical price: (high + low + close) / 3
 */
double samtrader_ohlcv_typical_price(const SamtraderOhlcv *ohlcv);

/**
 * @brief Get the true range for an OHLCV record.
 *
 * True Range = max(high - low, |high - prev_close|, |low - prev_close|)
 *
 * @param ohlcv Current OHLCV record
 * @param prev_close Previous day's closing price
 * @return True range value
 */
double samtrader_ohlcv_true_range(const SamtraderOhlcv *ohlcv, double prev_close);

#endif /* SAMTRADER_DOMAIN_OHLCV_H */
