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

#ifndef SAMTRADER_DOMAIN_UNIVERSE_H
#define SAMTRADER_DOMAIN_UNIVERSE_H

#include <stddef.h>
#include <time.h>

#include <samrena.h>

/** @brief Minimum number of OHLCV bars required for a code to be valid. */
#define SAMTRADER_MIN_OHLCV_BARS 30

/* Forward declaration to avoid including full port header */
typedef struct SamtraderDataPort SamtraderDataPort;

/**
 * @brief A validated set of stock codes for backtesting.
 *
 * Represents the universe of instruments to be tested, parsed from a
 * comma-separated configuration string and validated against a data source.
 */
typedef struct {
  const char **codes;   /**< Arena-allocated array of code strings */
  size_t count;         /**< Number of codes in the universe */
  const char *exchange; /**< Exchange identifier (e.g., "AU", "US") */
} SamtraderUniverse;

/**
 * @brief Parse a comma-separated codes string into a universe.
 *
 * Splits the input string on commas, trims whitespace, uppercases each code,
 * and checks for duplicates. All memory is arena-allocated.
 *
 * @param arena Memory arena for allocation
 * @param codes_str Comma-separated stock codes (e.g., "CBA,BHP,WBC")
 * @param exchange Exchange identifier
 * @return Pointer to the parsed universe, or NULL on error
 */
SamtraderUniverse *samtrader_universe_parse(Samrena *arena, const char *codes_str,
                                            const char *exchange);

/**
 * @brief Validate universe codes against a data source.
 *
 * Checks each code has at least SAMTRADER_MIN_OHLCV_BARS of data in the
 * given date range. Codes with insufficient data are removed in-place.
 *
 * @param universe The universe to validate (modified in-place)
 * @param data_port Data source to check against
 * @param start_date Start of date range (inclusive)
 * @param end_date End of date range (inclusive)
 * @return Number of valid codes remaining, or -1 if all codes are invalid
 */
int samtrader_universe_validate(SamtraderUniverse *universe, SamtraderDataPort *data_port,
                                time_t start_date, time_t end_date);

#endif /* SAMTRADER_DOMAIN_UNIVERSE_H */
