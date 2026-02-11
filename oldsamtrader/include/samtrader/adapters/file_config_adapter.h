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

#ifndef SAMTRADER_ADAPTERS_FILE_CONFIG_ADAPTER_H
#define SAMTRADER_ADAPTERS_FILE_CONFIG_ADAPTER_H

#include <samrena.h>

#include <samtrader/ports/config_port.h>

/**
 * @brief Create a file-based configuration adapter.
 *
 * Parses an INI-style configuration file and provides access through
 * the SamtraderConfigPort interface.
 *
 * INI File Format:
 * @code
 * [section_name]
 * key = value
 * # This is a comment
 * ; This is also a comment
 *
 * [another_section]
 * key2 = another value
 * @endcode
 *
 * @param arena Memory arena for all allocations
 * @param config_path Path to the INI configuration file
 * @return Configuration port instance, or NULL on failure (file not found,
 *         parse error, memory allocation failure)
 */
SamtraderConfigPort *samtrader_file_config_adapter_create(Samrena *arena, const char *config_path);

#endif /* SAMTRADER_ADAPTERS_FILE_CONFIG_ADAPTER_H */
