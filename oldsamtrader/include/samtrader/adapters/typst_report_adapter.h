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

#ifndef SAMTRADER_ADAPTERS_TYPST_REPORT_ADAPTER_H
#define SAMTRADER_ADAPTERS_TYPST_REPORT_ADAPTER_H

#include <samrena.h>

#include <samtrader/ports/report_port.h>

/**
 * @brief Create a Typst report adapter.
 *
 * Generates Typst markup for professional backtest reports including
 * strategy summary, parameters, and performance metrics.
 *
 * If a template_path is provided, the adapter reads the template file
 * and performs placeholder substitution. Supported placeholders:
 *   - {{STRATEGY_NAME}}, {{STRATEGY_DESCRIPTION}}
 *   - {{POSITION_SIZE}}, {{STOP_LOSS_PCT}}, {{TAKE_PROFIT_PCT}}, {{MAX_POSITIONS}}
 *   - {{TOTAL_RETURN}}, {{ANNUALIZED_RETURN}}, {{SHARPE_RATIO}}, {{SORTINO_RATIO}}
 *   - {{MAX_DRAWDOWN}}, {{MAX_DRAWDOWN_DURATION}}, {{WIN_RATE}}, {{PROFIT_FACTOR}}
 *   - {{TOTAL_TRADES}}, {{WINNING_TRADES}}, {{LOSING_TRADES}}
 *   - {{AVERAGE_WIN}}, {{AVERAGE_LOSS}}, {{LARGEST_WIN}}, {{LARGEST_LOSS}}
 *   - {{AVG_TRADE_DURATION}}, {{GENERATED_DATE}}
 *   - {{EQUITY_CURVE_CHART}}: Inline SVG equity curve (multi-KB, writes directly)
 *   - {{DRAWDOWN_CHART}}: Inline SVG drawdown visualization (multi-KB, writes directly)
 *   - {{TRADE_LOG}}: Typst table of all closed trades (multi-KB, writes directly)
 *   - {{UNIVERSE_SUMMARY}}: Multi-code summary table (write_multi only)
 *   - {{PER_CODE_DETAILS}}: Per-code detail sections (write_multi only)
 *   - {{FULL_TRADE_LOG}}: Full trade log across all codes (write_multi only)
 *
 * If template_path is NULL, a default report layout is generated.
 * For multi-code backtests, use write_multi which adds universe summary
 * and per-code breakdown sections.
 *
 * @param arena Memory arena for all allocations
 * @param template_path Optional path to a custom Typst template (NULL for default)
 * @return Report port instance, or NULL on failure
 */
SamtraderReportPort *samtrader_typst_adapter_create(Samrena *arena, const char *template_path);

#endif /* SAMTRADER_ADAPTERS_TYPST_REPORT_ADAPTER_H */
