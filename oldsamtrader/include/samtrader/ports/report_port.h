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

#ifndef SAMTRADER_PORTS_REPORT_PORT_H
#define SAMTRADER_PORTS_REPORT_PORT_H

#include <stdbool.h>

#include <samrena.h>

#include "samtrader/domain/backtest.h"
#include "samtrader/domain/strategy.h"

/**
 * @brief Forward declaration of the report port structure.
 *
 * The SamtraderReportPort is an interface (port) for report generation
 * in the hexagonal architecture. Concrete implementations (adapters)
 * provide the actual report rendering logic.
 */
typedef struct SamtraderReportPort SamtraderReportPort;

/**
 * @brief Function type for writing a backtest report.
 *
 * Generates a report from backtest results and strategy definition,
 * writing the output to the specified file path.
 *
 * @param port The report port instance
 * @param result Backtest results containing metrics and trade data
 * @param strategy Strategy definition with rules and parameters
 * @param output_path File path to write the report to
 * @return true on success, false on failure
 */
typedef bool (*SamtraderReportWriteFn)(SamtraderReportPort *port, SamtraderBacktestResult *result,
                                       SamtraderStrategy *strategy, const char *output_path);

/**
 * @brief Function type for writing a multi-code backtest report.
 *
 * Generates a report from multi-code backtest results including per-code
 * breakdowns, writing the output to the specified file path.
 *
 * @param port The report port instance
 * @param multi_result Multi-code results with aggregate and per-code data
 * @param strategy Strategy definition with rules and parameters
 * @param output_path File path to write the report to
 * @return true on success, false on failure
 */
typedef bool (*SamtraderReportWriteMultiFn)(SamtraderReportPort *port,
                                            SamtraderMultiCodeResult *multi_result,
                                            SamtraderStrategy *strategy, const char *output_path);

/**
 * @brief Function type for closing the report port.
 *
 * Releases any resources held by the adapter.
 * Memory allocated through the arena is not freed here.
 *
 * @param port The report port instance to close
 */
typedef void (*SamtraderReportCloseFn)(SamtraderReportPort *port);

/**
 * @brief Report port interface for backtest report generation.
 *
 * This is the port (interface) in the hexagonal architecture pattern.
 * Adapters implement this interface to generate reports in various
 * formats (Typst, HTML, CSV, etc.).
 *
 * Usage:
 * @code
 * // Create adapter (e.g., Typst)
 * SamtraderReportPort *report = samtrader_typst_adapter_create(arena, NULL);
 *
 * // Generate report
 * report->write(report, result, strategy, "output/report.typ");
 *
 * // Clean up
 * report->close(report);
 * @endcode
 */
struct SamtraderReportPort {
  void *impl;                              /**< Adapter-specific implementation */
  Samrena *arena;                          /**< Memory arena for allocations */
  SamtraderReportWriteFn write;            /**< Write report function */
  SamtraderReportWriteMultiFn write_multi; /**< Write multi-code report (NULL = use write) */
  SamtraderReportCloseFn close;            /**< Close/cleanup function */
};

#endif /* SAMTRADER_PORTS_REPORT_PORT_H */
