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

#ifndef SAMTRADER_DOMAIN_CODE_DATA_H
#define SAMTRADER_DOMAIN_CODE_DATA_H

#include <stddef.h>
#include <time.h>

#include <samdata/samhashmap.h>
#include <samrena.h>
#include <samvector.h>

#include "samtrader/domain/strategy.h"

/* Forward declaration to avoid including full port header */
typedef struct SamtraderDataPort SamtraderDataPort;

/**
 * @brief Per-code data container for multi-code backtesting.
 *
 * Holds OHLCV data, pre-computed indicators, and metadata for a single
 * instrument in a multi-code backtest universe.
 */
typedef struct {
  const char *code;       /**< Stock symbol (arena-allocated) */
  const char *exchange;   /**< Exchange identifier (arena-allocated) */
  SamrenaVector *ohlcv;   /**< Vector of SamtraderOhlcv bars */
  SamHashMap *indicators; /**< indicator_key -> SamtraderIndicatorSeries* */
  size_t bar_count;       /**< Number of OHLCV bars */
} SamtraderCodeData;

/**
 * @brief Load OHLCV data for a single code via the data port.
 *
 * Fetches OHLCV data for the specified code and wraps it in a
 * SamtraderCodeData container. The indicators field is set to NULL
 * and must be populated separately via samtrader_code_data_compute_indicators.
 *
 * @param arena Memory arena for allocation
 * @param data_port Data source to fetch from
 * @param code Stock symbol
 * @param exchange Exchange identifier
 * @param start_date Start of date range (inclusive)
 * @param end_date End of date range (inclusive)
 * @return Pointer to the loaded code data, or NULL on error
 */
SamtraderCodeData *samtrader_load_code_data(Samrena *arena, SamtraderDataPort *data_port,
                                            const char *code, const char *exchange,
                                            time_t start_date, time_t end_date);

/**
 * @brief Pre-compute indicators for a single code from strategy rules.
 *
 * Traverses all strategy rules, collects unique indicator operands,
 * calculates each indicator series from the code's OHLCV data, and
 * stores results in code_data->indicators.
 *
 * @param arena Memory arena for allocation
 * @param code_data The code data to compute indicators for
 * @param strategy Strategy containing rules with indicator references
 * @return 0 on success, -1 on error
 */
int samtrader_code_data_compute_indicators(Samrena *arena, SamtraderCodeData *code_data,
                                           const SamtraderStrategy *strategy);

/**
 * @brief Build a sorted, deduplicated date timeline across all codes.
 *
 * Iterates all codes' OHLCV data, collects unique dates, and returns
 * a sorted vector of time_t values in ascending order.
 *
 * @param arena Memory arena for allocation
 * @param code_data Array of code data pointers
 * @param code_count Number of codes
 * @return Vector of time_t (sorted ascending, no duplicates), or NULL on error
 */
SamrenaVector *samtrader_build_date_timeline(Samrena *arena, SamtraderCodeData **code_data,
                                             size_t code_count);

/**
 * @brief Build a date-to-bar-index mapping for one code's OHLCV data.
 *
 * For each bar, maps the date string to an arena-allocated size_t* index.
 * The date key format matches that used by samtrader_build_date_timeline.
 *
 * @param arena Memory arena for allocation
 * @param ohlcv Vector of SamtraderOhlcv bars
 * @return SamHashMap mapping date string -> size_t* index, or NULL on error
 */
SamHashMap *samtrader_build_date_index(Samrena *arena, SamrenaVector *ohlcv);

#endif /* SAMTRADER_DOMAIN_CODE_DATA_H */
