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

#ifndef SAMTRADER_PORTS_CONFIG_PORT_H
#define SAMTRADER_PORTS_CONFIG_PORT_H

#include <samrena.h>

/**
 * @brief Configuration port interface.
 *
 * Defines the abstract interface for configuration sources.
 * Implementations (adapters) provide concrete configuration loading
 * from files, environment variables, or other sources.
 */
typedef struct SamtraderConfigPort SamtraderConfigPort;

/**
 * @brief Get a string configuration value.
 *
 * @param port Configuration port instance
 * @param section Configuration section name (e.g., "database")
 * @param key Configuration key name
 * @return String value or NULL if not found. The returned string is
 *         arena-allocated and should not be freed.
 */
typedef const char *(*SamtraderConfigGetStringFn)(SamtraderConfigPort *port, const char *section,
                                                  const char *key);

/**
 * @brief Get an integer configuration value.
 *
 * @param port Configuration port instance
 * @param section Configuration section name
 * @param key Configuration key name
 * @param default_value Value to return if key not found or invalid
 * @return Integer value or default_value if not found
 */
typedef int (*SamtraderConfigGetIntFn)(SamtraderConfigPort *port, const char *section,
                                       const char *key, int default_value);

/**
 * @brief Get a double configuration value.
 *
 * @param port Configuration port instance
 * @param section Configuration section name
 * @param key Configuration key name
 * @param default_value Value to return if key not found or invalid
 * @return Double value or default_value if not found
 */
typedef double (*SamtraderConfigGetDoubleFn)(SamtraderConfigPort *port, const char *section,
                                             const char *key, double default_value);

/**
 * @brief Get a boolean configuration value.
 *
 * Recognizes "true", "false", "yes", "no", "1", "0" (case-insensitive).
 *
 * @param port Configuration port instance
 * @param section Configuration section name
 * @param key Configuration key name
 * @param default_value Value to return if key not found or invalid
 * @return Boolean value or default_value if not found
 */
typedef bool (*SamtraderConfigGetBoolFn)(SamtraderConfigPort *port, const char *section,
                                         const char *key, bool default_value);

/**
 * @brief Close and release configuration port resources.
 *
 * @param port Configuration port instance to close
 */
typedef void (*SamtraderConfigCloseFn)(SamtraderConfigPort *port);

/**
 * @brief Configuration port structure.
 *
 * Contains function pointers for configuration operations and
 * adapter-specific implementation data.
 */
struct SamtraderConfigPort {
  void *impl;     /**< Adapter-specific data */
  Samrena *arena; /**< Memory arena for allocations */
  SamtraderConfigGetStringFn get_string;
  SamtraderConfigGetIntFn get_int;
  SamtraderConfigGetDoubleFn get_double;
  SamtraderConfigGetBoolFn get_bool;
  SamtraderConfigCloseFn close;
};

#endif /* SAMTRADER_PORTS_CONFIG_PORT_H */
