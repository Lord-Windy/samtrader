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

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <samrena.h>
#include <samvector.h>

#include <samtrader/adapters/typst_report_adapter.h>
#include <samtrader/domain/backtest.h>
#include <samtrader/domain/portfolio.h>
#include <samtrader/domain/strategy.h>
#include <samtrader/ports/report_port.h>

#define ASSERT(cond, msg)                                                                          \
  do {                                                                                             \
    if (!(cond)) {                                                                                 \
      printf("FAIL: %s\n", msg);                                                                   \
      return 1;                                                                                    \
    }                                                                                              \
  } while (0)

/* Helper: make a time_t from a day offset (day 0 = 2024-01-01) */
static time_t day_time(int day) { return (time_t)(1704067200 + day * 86400); }

/* Helper: generate temp file path */
static char *temp_path(const char *suffix) {
  static char path[256];
  snprintf(path, sizeof(path), "/tmp/test_report_%s_%d.typ", suffix, getpid());
  return path;
}

/* Helper: read entire file into malloc'd buffer, returns NULL on failure */
static char *read_file(const char *path) {
  FILE *f = fopen(path, "r");
  if (f == NULL) {
    return NULL;
  }
  fseek(f, 0, SEEK_END);
  long size = ftell(f);
  fseek(f, 0, SEEK_SET);
  if (size <= 0) {
    fclose(f);
    return NULL;
  }
  char *buf = malloc((size_t)size + 1);
  if (buf == NULL) {
    fclose(f);
    return NULL;
  }
  size_t n = fread(buf, 1, (size_t)size, f);
  buf[n] = '\0';
  fclose(f);
  return buf;
}

/* Helper: build a test strategy */
static SamtraderStrategy make_strategy(void) {
  SamtraderStrategy s;
  memset(&s, 0, sizeof(s));
  s.name = "Test SMA Crossover";
  s.description = "A simple SMA crossover strategy for testing";
  s.position_size = 0.25;
  s.stop_loss_pct = 5.0;
  s.take_profit_pct = 10.0;
  s.max_positions = 3;
  s.entry_long = NULL;
  s.exit_long = NULL;
  s.entry_short = NULL;
  s.exit_short = NULL;
  return s;
}

/* Helper: build a test backtest result with equity curve and trades */
static SamtraderBacktestResult make_result(Samrena *arena) {
  SamtraderBacktestResult r;
  memset(&r, 0, sizeof(r));

  r.total_return = 0.25;
  r.annualized_return = 0.18;
  r.sharpe_ratio = 1.234;
  r.sortino_ratio = 1.567;
  r.max_drawdown = 0.12;
  r.max_drawdown_duration = 45.0;
  r.win_rate = 0.60;
  r.profit_factor = 1.85;
  r.total_trades = 10;
  r.winning_trades = 6;
  r.losing_trades = 4;
  r.average_win = 500.0;
  r.average_loss = -300.0;
  r.largest_win = 1200.0;
  r.largest_loss = -800.0;
  r.average_trade_duration = 7.5;

  /* Build equity curve: 30 days of data */
  r.equity_curve = samrena_vector_init(arena, sizeof(SamtraderEquityPoint), 32);
  for (int i = 0; i < 30; i++) {
    SamtraderEquityPoint pt;
    pt.date = day_time(i);
    pt.equity = 10000.0 + (double)i * 100.0 - (i % 5 == 0 ? 200.0 : 0.0);
    samrena_vector_push(r.equity_curve, &pt);
  }

  /* Build trades: 3 trades (2 wins, 1 loss) */
  r.trades = samrena_vector_init(arena, sizeof(SamtraderClosedTrade), 4);

  SamtraderClosedTrade t1 = {.code = "AAPL",
                             .exchange = "NASDAQ",
                             .quantity = 100,
                             .entry_price = 150.0,
                             .exit_price = 160.0,
                             .entry_date = day_time(2),
                             .exit_date = day_time(7),
                             .pnl = 1000.0};
  samrena_vector_push(r.trades, &t1);

  SamtraderClosedTrade t2 = {.code = "MSFT",
                             .exchange = "NASDAQ",
                             .quantity = 50,
                             .entry_price = 300.0,
                             .exit_price = 290.0,
                             .entry_date = day_time(5),
                             .exit_date = day_time(12),
                             .pnl = -500.0};
  samrena_vector_push(r.trades, &t2);

  SamtraderClosedTrade t3 = {.code = "GOOG",
                             .exchange = "NASDAQ",
                             .quantity = -30,
                             .entry_price = 140.0,
                             .exit_price = 130.0,
                             .entry_date = day_time(10),
                             .exit_date = day_time(15),
                             .pnl = 300.0};
  samrena_vector_push(r.trades, &t3);

  return r;
}

/* ========== Adapter Creation Tests ========== */

static int test_create_null_arena(void) {
  printf("Testing adapter create with NULL arena...\n");
  SamtraderReportPort *port = samtrader_typst_adapter_create(NULL, NULL);
  ASSERT(port == NULL, "NULL arena should return NULL");
  printf("  PASS\n");
  return 0;
}

static int test_create_no_template(void) {
  printf("Testing adapter create without template...\n");
  Samrena *arena = samrena_create_default();
  SamtraderReportPort *port = samtrader_typst_adapter_create(arena, NULL);
  ASSERT(port != NULL, "Should create port without template");
  ASSERT(port->write != NULL, "write fn should be set");
  ASSERT(port->close != NULL, "close fn should be set");
  ASSERT(port->arena == arena, "arena should be set");
  port->close(port);
  samrena_destroy(arena);
  printf("  PASS\n");
  return 0;
}

static int test_create_with_template(void) {
  printf("Testing adapter create with template path...\n");
  Samrena *arena = samrena_create_default();
  SamtraderReportPort *port = samtrader_typst_adapter_create(arena, "/some/template.typ");
  ASSERT(port != NULL, "Should create port with template");
  ASSERT(port->write != NULL, "write fn should be set");
  port->close(port);
  samrena_destroy(arena);
  printf("  PASS\n");
  return 0;
}

/* ========== Write Validation Tests ========== */

static int test_write_null_params(void) {
  printf("Testing write with NULL params...\n");
  Samrena *arena = samrena_create_default();
  SamtraderReportPort *port = samtrader_typst_adapter_create(arena, NULL);
  SamtraderBacktestResult result = make_result(arena);
  SamtraderStrategy strategy = make_strategy();
  const char *path = temp_path("null");

  ASSERT(!port->write(NULL, &result, &strategy, path), "NULL port should fail");
  ASSERT(!port->write(port, NULL, &strategy, path), "NULL result should fail");
  ASSERT(!port->write(port, &result, NULL, path), "NULL strategy should fail");
  ASSERT(!port->write(port, &result, &strategy, NULL), "NULL path should fail");

  port->close(port);
  samrena_destroy(arena);
  printf("  PASS\n");
  return 0;
}

/* ========== Default Report Output Tests ========== */

static int test_default_report_generates_file(void) {
  printf("Testing default report generates output file...\n");
  Samrena *arena = samrena_create_default();
  SamtraderReportPort *port = samtrader_typst_adapter_create(arena, NULL);
  SamtraderBacktestResult result = make_result(arena);
  SamtraderStrategy strategy = make_strategy();
  const char *path = temp_path("default");

  bool ok = port->write(port, &result, &strategy, path);
  ASSERT(ok, "write should succeed");

  char *content = read_file(path);
  ASSERT(content != NULL, "Output file should be readable");
  ASSERT(strlen(content) > 0, "Output should not be empty");

  free(content);
  unlink(path);
  port->close(port);
  samrena_destroy(arena);
  printf("  PASS\n");
  return 0;
}

static int test_default_report_preamble(void) {
  printf("Testing default report preamble...\n");
  Samrena *arena = samrena_create_default();
  SamtraderReportPort *port = samtrader_typst_adapter_create(arena, NULL);
  SamtraderBacktestResult result = make_result(arena);
  SamtraderStrategy strategy = make_strategy();
  const char *path = temp_path("preamble");

  port->write(port, &result, &strategy, path);
  char *content = read_file(path);
  ASSERT(content != NULL, "Output file should be readable");

  ASSERT(strstr(content, "#set document(title:") != NULL, "Should have document title");
  ASSERT(strstr(content, "Test SMA Crossover") != NULL, "Should contain strategy name");
  ASSERT(strstr(content, "#set page(paper: \"a4\"") != NULL, "Should set A4 paper");
  ASSERT(strstr(content, "#set text(font:") != NULL, "Should set font");

  free(content);
  unlink(path);
  port->close(port);
  samrena_destroy(arena);
  printf("  PASS\n");
  return 0;
}

static int test_default_report_strategy_summary(void) {
  printf("Testing default report strategy summary...\n");
  Samrena *arena = samrena_create_default();
  SamtraderReportPort *port = samtrader_typst_adapter_create(arena, NULL);
  SamtraderBacktestResult result = make_result(arena);
  SamtraderStrategy strategy = make_strategy();
  const char *path = temp_path("summary");

  port->write(port, &result, &strategy, path);
  char *content = read_file(path);
  ASSERT(content != NULL, "Output should be readable");

  ASSERT(strstr(content, "== Strategy Summary") != NULL, "Should have strategy summary section");
  ASSERT(strstr(content, "Test SMA Crossover") != NULL, "Should contain strategy name");
  ASSERT(strstr(content, "A simple SMA crossover strategy") != NULL,
         "Should contain strategy description");

  free(content);
  unlink(path);
  port->close(port);
  samrena_destroy(arena);
  printf("  PASS\n");
  return 0;
}

static int test_default_report_strategy_parameters(void) {
  printf("Testing default report strategy parameters...\n");
  Samrena *arena = samrena_create_default();
  SamtraderReportPort *port = samtrader_typst_adapter_create(arena, NULL);
  SamtraderBacktestResult result = make_result(arena);
  SamtraderStrategy strategy = make_strategy();
  const char *path = temp_path("params");

  port->write(port, &result, &strategy, path);
  char *content = read_file(path);
  ASSERT(content != NULL, "Output should be readable");

  ASSERT(strstr(content, "== Strategy Parameters") != NULL, "Should have parameters section");
  ASSERT(strstr(content, "25.0%") != NULL, "Should contain position size (25.0%)");
  ASSERT(strstr(content, "5.0%") != NULL, "Should contain stop loss (5.0%)");
  ASSERT(strstr(content, "10.0%") != NULL, "Should contain take profit (10.0%)");
  ASSERT(strstr(content, "[3]") != NULL, "Should contain max positions (3)");

  free(content);
  unlink(path);
  port->close(port);
  samrena_destroy(arena);
  printf("  PASS\n");
  return 0;
}

static int test_default_report_performance_metrics(void) {
  printf("Testing default report performance metrics...\n");
  Samrena *arena = samrena_create_default();
  SamtraderReportPort *port = samtrader_typst_adapter_create(arena, NULL);
  SamtraderBacktestResult result = make_result(arena);
  SamtraderStrategy strategy = make_strategy();
  const char *path = temp_path("metrics");

  port->write(port, &result, &strategy, path);
  char *content = read_file(path);
  ASSERT(content != NULL, "Output should be readable");

  /* Return metrics */
  ASSERT(strstr(content, "== Performance Metrics") != NULL, "Should have metrics section");
  ASSERT(strstr(content, "25.00%") != NULL, "Should contain total return");
  ASSERT(strstr(content, "18.00%") != NULL, "Should contain annualized return");
  ASSERT(strstr(content, "1.234") != NULL, "Should contain sharpe ratio");
  ASSERT(strstr(content, "1.567") != NULL, "Should contain sortino ratio");

  /* Risk metrics */
  ASSERT(strstr(content, "12.00%") != NULL, "Should contain max drawdown");
  ASSERT(strstr(content, "45 days") != NULL, "Should contain max drawdown duration");
  ASSERT(strstr(content, "1.85") != NULL, "Should contain profit factor");

  /* Trade statistics */
  ASSERT(strstr(content, "[10]") != NULL, "Should contain total trades");
  ASSERT(strstr(content, "[6]") != NULL, "Should contain winning trades");
  ASSERT(strstr(content, "[4]") != NULL, "Should contain losing trades");
  ASSERT(strstr(content, "60.0%") != NULL, "Should contain win rate");
  ASSERT(strstr(content, "500.00") != NULL, "Should contain average win");
  ASSERT(strstr(content, "-300.00") != NULL, "Should contain average loss");
  ASSERT(strstr(content, "1200.00") != NULL, "Should contain largest win");
  ASSERT(strstr(content, "-800.00") != NULL, "Should contain largest loss");
  ASSERT(strstr(content, "7.5 days") != NULL, "Should contain avg trade duration");

  free(content);
  unlink(path);
  port->close(port);
  samrena_destroy(arena);
  printf("  PASS\n");
  return 0;
}

static int test_default_report_trade_log(void) {
  printf("Testing default report trade log...\n");
  Samrena *arena = samrena_create_default();
  SamtraderReportPort *port = samtrader_typst_adapter_create(arena, NULL);
  SamtraderBacktestResult result = make_result(arena);
  SamtraderStrategy strategy = make_strategy();
  const char *path = temp_path("tradelog");

  port->write(port, &result, &strategy, path);
  char *content = read_file(path);
  ASSERT(content != NULL, "Output should be readable");

  ASSERT(strstr(content, "== Trade Log") != NULL, "Should have trade log section");
  ASSERT(strstr(content, "[*Symbol*]") != NULL, "Should have symbol header");
  ASSERT(strstr(content, "[*P&L*]") != NULL, "Should have P&L header");

  /* Trade entries */
  ASSERT(strstr(content, "AAPL") != NULL, "Should contain AAPL trade");
  ASSERT(strstr(content, "MSFT") != NULL, "Should contain MSFT trade");
  ASSERT(strstr(content, "GOOG") != NULL, "Should contain GOOG trade");

  /* Side detection */
  ASSERT(strstr(content, "Long") != NULL, "Should have Long side");
  ASSERT(strstr(content, "Short") != NULL, "Should have Short side (GOOG has negative qty)");

  /* Color coding: green for positive P&L, red for negative */
  ASSERT(strstr(content, "#16a34a") != NULL, "Should have green color for winning trades");
  ASSERT(strstr(content, "#dc2626") != NULL, "Should have red color for losing trades");

  free(content);
  unlink(path);
  port->close(port);
  samrena_destroy(arena);
  printf("  PASS\n");
  return 0;
}

static int test_default_report_equity_curve(void) {
  printf("Testing default report equity curve chart...\n");
  Samrena *arena = samrena_create_default();
  SamtraderReportPort *port = samtrader_typst_adapter_create(arena, NULL);
  SamtraderBacktestResult result = make_result(arena);
  SamtraderStrategy strategy = make_strategy();
  const char *path = temp_path("equity");

  port->write(port, &result, &strategy, path);
  char *content = read_file(path);
  ASSERT(content != NULL, "Output should be readable");

  ASSERT(strstr(content, "== Equity Curve") != NULL, "Should have equity curve section");
  ASSERT(strstr(content, "#image.decode") != NULL, "Should use image.decode");
  ASSERT(strstr(content, "<svg") != NULL, "Should contain SVG");
  ASSERT(strstr(content, "<polyline") != NULL, "Should have polyline for curve");
  ASSERT(strstr(content, "<polygon") != NULL, "Should have polygon for fill");
  ASSERT(strstr(content, "viewBox='0 0 600 250'") != NULL, "Should have correct viewBox");
  ASSERT(strstr(content, "stroke='#2563eb'") != NULL, "Should have blue curve stroke");

  free(content);
  unlink(path);
  port->close(port);
  samrena_destroy(arena);
  printf("  PASS\n");
  return 0;
}

static int test_default_report_drawdown_chart(void) {
  printf("Testing default report drawdown chart...\n");
  Samrena *arena = samrena_create_default();
  SamtraderReportPort *port = samtrader_typst_adapter_create(arena, NULL);
  SamtraderBacktestResult result = make_result(arena);
  SamtraderStrategy strategy = make_strategy();
  const char *path = temp_path("drawdown");

  port->write(port, &result, &strategy, path);
  char *content = read_file(path);
  ASSERT(content != NULL, "Output should be readable");

  ASSERT(strstr(content, "=== Drawdown") != NULL, "Should have drawdown section");
  ASSERT(strstr(content, "<svg") != NULL, "Should contain SVG");
  ASSERT(strstr(content, "stroke='#dc2626'") != NULL, "Should have red drawdown stroke");
  ASSERT(strstr(content, "rgba(220,38,38,0.2)") != NULL, "Should have red fill");

  free(content);
  unlink(path);
  port->close(port);
  samrena_destroy(arena);
  printf("  PASS\n");
  return 0;
}

static int test_default_report_monthly_returns(void) {
  printf("Testing default report monthly returns table...\n");
  Samrena *arena = samrena_create_default();
  SamtraderReportPort *port = samtrader_typst_adapter_create(arena, NULL);
  SamtraderBacktestResult result = make_result(arena);
  SamtraderStrategy strategy = make_strategy();
  const char *path = temp_path("monthly");

  port->write(port, &result, &strategy, path);
  char *content = read_file(path);
  ASSERT(content != NULL, "Output should be readable");

  ASSERT(strstr(content, "== Monthly Returns") != NULL, "Should have monthly returns section");
  ASSERT(strstr(content, "[*Year*]") != NULL, "Should have year header");
  ASSERT(strstr(content, "[*Jan*]") != NULL, "Should have month headers");
  ASSERT(strstr(content, "[*YTD*]") != NULL, "Should have YTD column");
  ASSERT(strstr(content, "2024") != NULL, "Should contain year 2024");

  free(content);
  unlink(path);
  port->close(port);
  samrena_destroy(arena);
  printf("  PASS\n");
  return 0;
}

/* ========== Edge Case Tests ========== */

static int test_empty_trades(void) {
  printf("Testing report with empty trades vector...\n");
  Samrena *arena = samrena_create_default();
  SamtraderReportPort *port = samtrader_typst_adapter_create(arena, NULL);
  SamtraderBacktestResult result = make_result(arena);
  SamtraderStrategy strategy = make_strategy();
  const char *path = temp_path("notrades");

  /* Replace trades with empty vector */
  result.trades = samrena_vector_init(arena, sizeof(SamtraderClosedTrade), 4);

  bool ok = port->write(port, &result, &strategy, path);
  ASSERT(ok, "write should succeed with empty trades");

  char *content = read_file(path);
  ASSERT(content != NULL, "Output should be readable");
  /* Trade log section should be omitted when trades is empty */
  ASSERT(strstr(content, "== Trade Log") == NULL, "Should omit trade log with no trades");

  free(content);
  unlink(path);
  port->close(port);
  samrena_destroy(arena);
  printf("  PASS\n");
  return 0;
}

static int test_single_equity_point(void) {
  printf("Testing report with single equity point...\n");
  Samrena *arena = samrena_create_default();
  SamtraderReportPort *port = samtrader_typst_adapter_create(arena, NULL);
  SamtraderBacktestResult result = make_result(arena);
  SamtraderStrategy strategy = make_strategy();
  const char *path = temp_path("singleeq");

  /* Replace equity curve with single point */
  result.equity_curve = samrena_vector_init(arena, sizeof(SamtraderEquityPoint), 4);
  SamtraderEquityPoint pt = {.date = day_time(0), .equity = 10000.0};
  samrena_vector_push(result.equity_curve, &pt);

  bool ok = port->write(port, &result, &strategy, path);
  ASSERT(ok, "write should succeed with single equity point");

  char *content = read_file(path);
  ASSERT(content != NULL, "Output should be readable");
  /* Charts and monthly returns require >= 2 points, should be omitted */
  ASSERT(strstr(content, "== Equity Curve") == NULL,
         "Should omit equity curve chart with < 2 points");
  ASSERT(strstr(content, "=== Drawdown") == NULL, "Should omit drawdown chart with < 2 points");

  free(content);
  unlink(path);
  port->close(port);
  samrena_destroy(arena);
  printf("  PASS\n");
  return 0;
}

static int test_null_equity_curve(void) {
  printf("Testing report with NULL equity curve...\n");
  Samrena *arena = samrena_create_default();
  SamtraderReportPort *port = samtrader_typst_adapter_create(arena, NULL);
  SamtraderBacktestResult result = make_result(arena);
  SamtraderStrategy strategy = make_strategy();
  const char *path = temp_path("nulleq");

  result.equity_curve = NULL;

  bool ok = port->write(port, &result, &strategy, path);
  ASSERT(ok, "write should succeed with NULL equity curve");

  char *content = read_file(path);
  ASSERT(content != NULL, "Output should be readable");
  ASSERT(strstr(content, "== Equity Curve") == NULL, "Should omit equity curve with NULL");
  ASSERT(strstr(content, "=== Drawdown") == NULL, "Should omit drawdown with NULL");
  ASSERT(strstr(content, "== Monthly Returns") == NULL, "Should omit monthly returns with NULL");

  free(content);
  unlink(path);
  port->close(port);
  samrena_destroy(arena);
  printf("  PASS\n");
  return 0;
}

static int test_null_trades(void) {
  printf("Testing report with NULL trades...\n");
  Samrena *arena = samrena_create_default();
  SamtraderReportPort *port = samtrader_typst_adapter_create(arena, NULL);
  SamtraderBacktestResult result = make_result(arena);
  SamtraderStrategy strategy = make_strategy();
  const char *path = temp_path("nulltrades");

  result.trades = NULL;

  bool ok = port->write(port, &result, &strategy, path);
  ASSERT(ok, "write should succeed with NULL trades");

  char *content = read_file(path);
  ASSERT(content != NULL, "Output should be readable");
  ASSERT(strstr(content, "== Trade Log") == NULL, "Should omit trade log with NULL trades");

  free(content);
  unlink(path);
  port->close(port);
  samrena_destroy(arena);
  printf("  PASS\n");
  return 0;
}

static int test_unnamed_strategy(void) {
  printf("Testing report with unnamed strategy...\n");
  Samrena *arena = samrena_create_default();
  SamtraderReportPort *port = samtrader_typst_adapter_create(arena, NULL);
  SamtraderBacktestResult result = make_result(arena);
  SamtraderStrategy strategy = make_strategy();
  const char *path = temp_path("unnamed");

  strategy.name = NULL;
  strategy.description = NULL;

  bool ok = port->write(port, &result, &strategy, path);
  ASSERT(ok, "write should succeed with unnamed strategy");

  char *content = read_file(path);
  ASSERT(content != NULL, "Output should be readable");
  ASSERT(strstr(content, "Unnamed Strategy") != NULL, "Should use default name");

  free(content);
  unlink(path);
  port->close(port);
  samrena_destroy(arena);
  printf("  PASS\n");
  return 0;
}

static int test_no_stop_loss_take_profit(void) {
  printf("Testing report with no stop loss or take profit...\n");
  Samrena *arena = samrena_create_default();
  SamtraderReportPort *port = samtrader_typst_adapter_create(arena, NULL);
  SamtraderBacktestResult result = make_result(arena);
  SamtraderStrategy strategy = make_strategy();
  const char *path = temp_path("nostop");

  strategy.stop_loss_pct = 0.0;
  strategy.take_profit_pct = 0.0;

  bool ok = port->write(port, &result, &strategy, path);
  ASSERT(ok, "write should succeed with no stop/take profit");

  char *content = read_file(path);
  ASSERT(content != NULL, "Output should be readable");
  /* When stop_loss_pct and take_profit_pct are 0, should show "None" */
  int none_count = 0;
  const char *search = content;
  while ((search = strstr(search, "[None]")) != NULL) {
    none_count++;
    search++;
  }
  ASSERT(none_count >= 2, "Should show 'None' for disabled stop loss and take profit");

  free(content);
  unlink(path);
  port->close(port);
  samrena_destroy(arena);
  printf("  PASS\n");
  return 0;
}

static int test_flat_equity_curve(void) {
  printf("Testing report with flat equity curve...\n");
  Samrena *arena = samrena_create_default();
  SamtraderReportPort *port = samtrader_typst_adapter_create(arena, NULL);
  SamtraderBacktestResult result = make_result(arena);
  SamtraderStrategy strategy = make_strategy();
  const char *path = temp_path("flat");

  /* Replace equity with flat line */
  result.equity_curve = samrena_vector_init(arena, sizeof(SamtraderEquityPoint), 4);
  for (int i = 0; i < 5; i++) {
    SamtraderEquityPoint pt = {.date = day_time(i), .equity = 10000.0};
    samrena_vector_push(result.equity_curve, &pt);
  }

  bool ok = port->write(port, &result, &strategy, path);
  ASSERT(ok, "write should succeed with flat equity");

  char *content = read_file(path);
  ASSERT(content != NULL, "Output should be readable");
  ASSERT(strstr(content, "== Equity Curve") != NULL, "Should still render equity curve");
  ASSERT(strstr(content, "<svg") != NULL, "Should contain SVG");

  free(content);
  unlink(path);
  port->close(port);
  samrena_destroy(arena);
  printf("  PASS\n");
  return 0;
}

/* ========== Template Report Tests ========== */

static int test_template_placeholder_substitution(void) {
  printf("Testing template placeholder substitution...\n");
  Samrena *arena = samrena_create_default();

  /* Write a simple template */
  char tmpl_path[256];
  snprintf(tmpl_path, sizeof(tmpl_path), "/tmp/test_tmpl_%d.typ", getpid());

  FILE *f = fopen(tmpl_path, "w");
  ASSERT(f != NULL, "Should create template file");
  fprintf(f, "Strategy: {{STRATEGY_NAME}}\n");
  fprintf(f, "Desc: {{STRATEGY_DESCRIPTION}}\n");
  fprintf(f, "Return: {{TOTAL_RETURN}}%%\n");
  fprintf(f, "Sharpe: {{SHARPE_RATIO}}\n");
  fprintf(f, "Sortino: {{SORTINO_RATIO}}\n");
  fprintf(f, "Max DD: {{MAX_DRAWDOWN}}%%\n");
  fprintf(f, "DD Duration: {{MAX_DRAWDOWN_DURATION}} days\n");
  fprintf(f, "Win Rate: {{WIN_RATE}}%%\n");
  fprintf(f, "PF: {{PROFIT_FACTOR}}\n");
  fprintf(f, "Total: {{TOTAL_TRADES}}\n");
  fprintf(f, "Wins: {{WINNING_TRADES}}\n");
  fprintf(f, "Losses: {{LOSING_TRADES}}\n");
  fprintf(f, "Avg Win: {{AVERAGE_WIN}}\n");
  fprintf(f, "Avg Loss: {{AVERAGE_LOSS}}\n");
  fprintf(f, "Best: {{LARGEST_WIN}}\n");
  fprintf(f, "Worst: {{LARGEST_LOSS}}\n");
  fprintf(f, "Avg Dur: {{AVG_TRADE_DURATION}}\n");
  fprintf(f, "Pos Size: {{POSITION_SIZE}}%%\n");
  fprintf(f, "Stop: {{STOP_LOSS_PCT}}%%\n");
  fprintf(f, "TP: {{TAKE_PROFIT_PCT}}%%\n");
  fprintf(f, "Max Pos: {{MAX_POSITIONS}}\n");
  fprintf(f, "Ann Return: {{ANNUALIZED_RETURN}}%%\n");
  fprintf(f, "Date: {{GENERATED_DATE}}\n");
  fclose(f);

  SamtraderReportPort *port = samtrader_typst_adapter_create(arena, tmpl_path);
  SamtraderBacktestResult result = make_result(arena);
  SamtraderStrategy strategy = make_strategy();
  const char *path = temp_path("tmpl");

  bool ok = port->write(port, &result, &strategy, path);
  ASSERT(ok, "template write should succeed");

  char *content = read_file(path);
  ASSERT(content != NULL, "Output should be readable");

  /* Verify all placeholders were resolved */
  ASSERT(strstr(content, "Strategy: Test SMA Crossover") != NULL, "Name should be substituted");
  ASSERT(strstr(content, "Desc: A simple SMA crossover") != NULL, "Description should be set");
  ASSERT(strstr(content, "Return: 25.00%") != NULL, "Total return should be 25.00");
  ASSERT(strstr(content, "Sharpe: 1.234") != NULL, "Sharpe should be 1.234");
  ASSERT(strstr(content, "Sortino: 1.567") != NULL, "Sortino should be 1.567");
  ASSERT(strstr(content, "Max DD: 12.00%") != NULL, "Drawdown should be 12.00");
  ASSERT(strstr(content, "DD Duration: 45 days") != NULL, "DD duration should be 45");
  ASSERT(strstr(content, "Win Rate: 60.0%") != NULL, "Win rate should be 60.0");
  ASSERT(strstr(content, "PF: 1.85") != NULL, "Profit factor should be 1.85");
  ASSERT(strstr(content, "Total: 10") != NULL, "Total trades should be 10");
  ASSERT(strstr(content, "Wins: 6") != NULL, "Winning trades should be 6");
  ASSERT(strstr(content, "Losses: 4") != NULL, "Losing trades should be 4");
  ASSERT(strstr(content, "Avg Win: 500.00") != NULL, "Avg win should be 500.00");
  ASSERT(strstr(content, "Avg Loss: -300.00") != NULL, "Avg loss should be -300.00");
  ASSERT(strstr(content, "Best: 1200.00") != NULL, "Largest win should be 1200.00");
  ASSERT(strstr(content, "Worst: -800.00") != NULL, "Largest loss should be -800.00");
  ASSERT(strstr(content, "Avg Dur: 7.5") != NULL, "Avg duration should be 7.5");
  ASSERT(strstr(content, "Pos Size: 25.0%") != NULL, "Position size should be 25.0");
  ASSERT(strstr(content, "Stop: 5.0%") != NULL, "Stop loss should be 5.0");
  ASSERT(strstr(content, "TP: 10.0%") != NULL, "Take profit should be 10.0");
  ASSERT(strstr(content, "Max Pos: 3") != NULL, "Max positions should be 3");
  ASSERT(strstr(content, "Ann Return: 18.00%") != NULL, "Annualized return should be 18.00");
  ASSERT(strstr(content, "Date: ") != NULL, "Generated date should be present");

  /* No unresolved placeholders (except GENERATED_DATE value which varies) */
  ASSERT(strstr(content, "{{STRATEGY_NAME}}") == NULL, "No unresolved STRATEGY_NAME");
  ASSERT(strstr(content, "{{TOTAL_RETURN}}") == NULL, "No unresolved TOTAL_RETURN");

  free(content);
  unlink(path);
  unlink(tmpl_path);
  port->close(port);
  samrena_destroy(arena);
  printf("  PASS\n");
  return 0;
}

static int test_template_chart_placeholders(void) {
  printf("Testing template chart placeholder substitution...\n");
  Samrena *arena = samrena_create_default();

  char tmpl_path[256];
  snprintf(tmpl_path, sizeof(tmpl_path), "/tmp/test_tmpl_charts_%d.typ", getpid());

  FILE *f = fopen(tmpl_path, "w");
  ASSERT(f != NULL, "Should create template file");
  fprintf(f, "EQUITY:{{EQUITY_CURVE_CHART}}\n");
  fprintf(f, "DD:{{DRAWDOWN_CHART}}\n");
  fprintf(f, "LOG:{{TRADE_LOG}}\n");
  fprintf(f, "MONTHLY:{{MONTHLY_RETURNS}}\n");
  fclose(f);

  SamtraderReportPort *port = samtrader_typst_adapter_create(arena, tmpl_path);
  SamtraderBacktestResult result = make_result(arena);
  SamtraderStrategy strategy = make_strategy();
  const char *path = temp_path("charts");

  bool ok = port->write(port, &result, &strategy, path);
  ASSERT(ok, "chart template write should succeed");

  char *content = read_file(path);
  ASSERT(content != NULL, "Output should be readable");

  /* Equity curve chart should have been expanded */
  ASSERT(strstr(content, "EQUITY:") != NULL, "Should have equity prefix");
  ASSERT(strstr(content, "<svg") != NULL, "Should contain SVG from chart placeholders");
  ASSERT(strstr(content, "== Trade Log") != NULL, "Should have expanded trade log");
  ASSERT(strstr(content, "== Monthly Returns") != NULL, "Should have expanded monthly returns");

  /* Chart placeholders should not remain */
  ASSERT(strstr(content, "{{EQUITY_CURVE_CHART}}") == NULL, "Equity placeholder resolved");
  ASSERT(strstr(content, "{{DRAWDOWN_CHART}}") == NULL, "Drawdown placeholder resolved");
  ASSERT(strstr(content, "{{TRADE_LOG}}") == NULL, "Trade log placeholder resolved");
  ASSERT(strstr(content, "{{MONTHLY_RETURNS}}") == NULL, "Monthly returns placeholder resolved");

  free(content);
  unlink(path);
  unlink(tmpl_path);
  port->close(port);
  samrena_destroy(arena);
  printf("  PASS\n");
  return 0;
}

static int test_template_unknown_placeholder(void) {
  printf("Testing template with unknown placeholder...\n");
  Samrena *arena = samrena_create_default();

  char tmpl_path[256];
  snprintf(tmpl_path, sizeof(tmpl_path), "/tmp/test_tmpl_unk_%d.typ", getpid());

  FILE *f = fopen(tmpl_path, "w");
  ASSERT(f != NULL, "Should create template file");
  fprintf(f, "Known: {{STRATEGY_NAME}} Unknown: {{DOES_NOT_EXIST}}");
  fclose(f);

  SamtraderReportPort *port = samtrader_typst_adapter_create(arena, tmpl_path);
  SamtraderBacktestResult result = make_result(arena);
  SamtraderStrategy strategy = make_strategy();
  const char *path = temp_path("unknown");

  bool ok = port->write(port, &result, &strategy, path);
  ASSERT(ok, "write should succeed with unknown placeholders");

  char *content = read_file(path);
  ASSERT(content != NULL, "Output should be readable");
  ASSERT(strstr(content, "Known: Test SMA Crossover") != NULL, "Known placeholder resolved");
  ASSERT(strstr(content, "{{DOES_NOT_EXIST}}") != NULL,
         "Unknown placeholder should be written literally");

  free(content);
  unlink(path);
  unlink(tmpl_path);
  port->close(port);
  samrena_destroy(arena);
  printf("  PASS\n");
  return 0;
}

static int test_template_missing_file(void) {
  printf("Testing template with missing file...\n");
  Samrena *arena = samrena_create_default();
  SamtraderReportPort *port = samtrader_typst_adapter_create(arena, "/nonexistent/template.typ");
  SamtraderBacktestResult result = make_result(arena);
  SamtraderStrategy strategy = make_strategy();
  const char *path = temp_path("missing");

  bool ok = port->write(port, &result, &strategy, path);
  ASSERT(!ok, "write should fail with missing template");

  port->close(port);
  samrena_destroy(arena);
  printf("  PASS\n");
  return 0;
}

static int test_template_unterminated_placeholder(void) {
  printf("Testing template with unterminated placeholder...\n");
  Samrena *arena = samrena_create_default();

  char tmpl_path[256];
  snprintf(tmpl_path, sizeof(tmpl_path), "/tmp/test_tmpl_unterm_%d.typ", getpid());

  FILE *f = fopen(tmpl_path, "w");
  ASSERT(f != NULL, "Should create template file");
  fprintf(f, "Before {{STRATEGY_NAME}} middle {{UNTERMINATED end");
  fclose(f);

  SamtraderReportPort *port = samtrader_typst_adapter_create(arena, tmpl_path);
  SamtraderBacktestResult result = make_result(arena);
  SamtraderStrategy strategy = make_strategy();
  const char *path = temp_path("unterm");

  bool ok = port->write(port, &result, &strategy, path);
  ASSERT(ok, "write should succeed with unterminated placeholder");

  char *content = read_file(path);
  ASSERT(content != NULL, "Output should be readable");
  ASSERT(strstr(content, "Before Test SMA Crossover") != NULL,
         "Text before unterminated should be correct");
  ASSERT(strstr(content, "{{UNTERMINATED end") != NULL,
         "Unterminated placeholder written literally");

  free(content);
  unlink(path);
  unlink(tmpl_path);
  port->close(port);
  samrena_destroy(arena);
  printf("  PASS\n");
  return 0;
}

/* ========== Multi-Code Report Tests ========== */

/* Helper: build a multi-code result with 3 codes */
static SamtraderMultiCodeResult make_multi_result(Samrena *arena) {
  SamtraderMultiCodeResult multi;
  memset(&multi, 0, sizeof(multi));

  /* Aggregate result reuses the single-code helper */
  multi.aggregate = make_result(arena);
  multi.code_count = 3;
  multi.code_results = (SamtraderCodeResult *)samrena_push(arena, 3 * sizeof(SamtraderCodeResult));
  memset(multi.code_results, 0, 3 * sizeof(SamtraderCodeResult));

  multi.code_results[0].code = "AAPL";
  multi.code_results[0].exchange = "NASDAQ";
  multi.code_results[0].total_trades = 1;
  multi.code_results[0].winning_trades = 1;
  multi.code_results[0].losing_trades = 0;
  multi.code_results[0].total_pnl = 1000.0;
  multi.code_results[0].win_rate = 1.0;
  multi.code_results[0].largest_win = 1000.0;
  multi.code_results[0].largest_loss = 0.0;

  multi.code_results[1].code = "MSFT";
  multi.code_results[1].exchange = "NASDAQ";
  multi.code_results[1].total_trades = 1;
  multi.code_results[1].winning_trades = 0;
  multi.code_results[1].losing_trades = 1;
  multi.code_results[1].total_pnl = -500.0;
  multi.code_results[1].win_rate = 0.0;
  multi.code_results[1].largest_win = 0.0;
  multi.code_results[1].largest_loss = -500.0;

  multi.code_results[2].code = "GOOG";
  multi.code_results[2].exchange = "NASDAQ";
  multi.code_results[2].total_trades = 1;
  multi.code_results[2].winning_trades = 1;
  multi.code_results[2].losing_trades = 0;
  multi.code_results[2].total_pnl = 300.0;
  multi.code_results[2].win_rate = 1.0;
  multi.code_results[2].largest_win = 300.0;
  multi.code_results[2].largest_loss = 0.0;

  return multi;
}

static int test_multi_report_universe_summary(void) {
  printf("Testing multi-code report universe summary...\n");
  Samrena *arena = samrena_create_default();
  SamtraderReportPort *port = samtrader_typst_adapter_create(arena, NULL);
  SamtraderMultiCodeResult multi = make_multi_result(arena);
  SamtraderStrategy strategy = make_strategy();
  const char *path = temp_path("multi_univ");

  ASSERT(port->write_multi != NULL, "write_multi should be set");
  bool ok = port->write_multi(port, &multi, &strategy, path);
  ASSERT(ok, "write_multi should succeed");

  char *content = read_file(path);
  ASSERT(content != NULL, "Output should be readable");

  ASSERT(strstr(content, "== Universe Summary") != NULL, "Should have Universe Summary heading");
  ASSERT(strstr(content, "AAPL") != NULL, "Should contain AAPL");
  ASSERT(strstr(content, "MSFT") != NULL, "Should contain MSFT");
  ASSERT(strstr(content, "GOOG") != NULL, "Should contain GOOG");
  ASSERT(strstr(content, "[*Code*]") != NULL, "Should have Code column header");
  ASSERT(strstr(content, "[*Win Rate*]") != NULL, "Should have Win Rate column header");

  free(content);
  unlink(path);
  port->close(port);
  samrena_destroy(arena);
  printf("  PASS\n");
  return 0;
}

static int test_multi_report_per_code_details(void) {
  printf("Testing multi-code report per-code details...\n");
  Samrena *arena = samrena_create_default();
  SamtraderReportPort *port = samtrader_typst_adapter_create(arena, NULL);
  SamtraderMultiCodeResult multi = make_multi_result(arena);
  SamtraderStrategy strategy = make_strategy();
  const char *path = temp_path("multi_detail");

  port->write_multi(port, &multi, &strategy, path);
  char *content = read_file(path);
  ASSERT(content != NULL, "Output should be readable");

  /* Per-code detail sections */
  ASSERT(strstr(content, "== AAPL Detail") != NULL, "Should have AAPL Detail section");
  ASSERT(strstr(content, "== MSFT Detail") != NULL, "Should have MSFT Detail section");
  ASSERT(strstr(content, "== GOOG Detail") != NULL, "Should have GOOG Detail section");

  /* Each detail section should contain per-code metrics */
  ASSERT(strstr(content, "=== Trades") != NULL, "Should have filtered trade sub-section");

  free(content);
  unlink(path);
  port->close(port);
  samrena_destroy(arena);
  printf("  PASS\n");
  return 0;
}

static int test_multi_report_full_trade_log(void) {
  printf("Testing multi-code report full trade log...\n");
  Samrena *arena = samrena_create_default();
  SamtraderReportPort *port = samtrader_typst_adapter_create(arena, NULL);
  SamtraderMultiCodeResult multi = make_multi_result(arena);
  SamtraderStrategy strategy = make_strategy();
  const char *path = temp_path("multi_ftlog");

  port->write_multi(port, &multi, &strategy, path);
  char *content = read_file(path);
  ASSERT(content != NULL, "Output should be readable");

  ASSERT(strstr(content, "== Full Trade Log") != NULL, "Should have Full Trade Log heading");
  /* All 3 trades from make_result should appear */
  ASSERT(strstr(content, "AAPL") != NULL, "Full trade log should contain AAPL");
  ASSERT(strstr(content, "MSFT") != NULL, "Full trade log should contain MSFT");
  ASSERT(strstr(content, "GOOG") != NULL, "Full trade log should contain GOOG");

  free(content);
  unlink(path);
  port->close(port);
  samrena_destroy(arena);
  printf("  PASS\n");
  return 0;
}

static int test_multi_report_single_code_fallback(void) {
  printf("Testing single-code uses write not write_multi...\n");
  Samrena *arena = samrena_create_default();
  SamtraderReportPort *port = samtrader_typst_adapter_create(arena, NULL);
  SamtraderBacktestResult result = make_result(arena);
  SamtraderStrategy strategy = make_strategy();
  const char *path = temp_path("single_fb");

  /* Single-code path uses write(), not write_multi() */
  bool ok = port->write(port, &result, &strategy, path);
  ASSERT(ok, "write should succeed for single code");

  char *content = read_file(path);
  ASSERT(content != NULL, "Output should be readable");

  /* Single-code report should NOT have multi-code sections */
  ASSERT(strstr(content, "== Universe Summary") == NULL, "Single code should not have universe");
  ASSERT(strstr(content, "== Full Trade Log") == NULL, "Single code should not have full log");
  /* But should still have the regular trade log */
  ASSERT(strstr(content, "== Trade Log") != NULL, "Single code should have Trade Log");

  free(content);
  unlink(path);
  port->close(port);
  samrena_destroy(arena);
  printf("  PASS\n");
  return 0;
}

static int test_write_multi_null_params(void) {
  printf("Testing write_multi with NULL params...\n");
  Samrena *arena = samrena_create_default();
  SamtraderReportPort *port = samtrader_typst_adapter_create(arena, NULL);
  SamtraderMultiCodeResult multi = make_multi_result(arena);
  SamtraderStrategy strategy = make_strategy();
  const char *path = temp_path("multi_null");

  ASSERT(!port->write_multi(NULL, &multi, &strategy, path), "NULL port should fail");
  ASSERT(!port->write_multi(port, NULL, &strategy, path), "NULL multi_result should fail");
  ASSERT(!port->write_multi(port, &multi, NULL, path), "NULL strategy should fail");
  ASSERT(!port->write_multi(port, &multi, &strategy, NULL), "NULL path should fail");

  port->close(port);
  samrena_destroy(arena);
  printf("  PASS\n");
  return 0;
}

/* ========== Large Dataset Tests ========== */

static int test_large_equity_curve_downsampling(void) {
  printf("Testing equity curve downsampling with >200 points...\n");
  Samrena *arena = samrena_create_default();
  SamtraderReportPort *port = samtrader_typst_adapter_create(arena, NULL);
  SamtraderBacktestResult result = make_result(arena);
  SamtraderStrategy strategy = make_strategy();
  const char *path = temp_path("large");

  /* Build equity curve with 500 points */
  result.equity_curve = samrena_vector_init(arena, sizeof(SamtraderEquityPoint), 512);
  for (int i = 0; i < 500; i++) {
    SamtraderEquityPoint pt;
    pt.date = day_time(i);
    pt.equity = 10000.0 + (double)i * 20.0 + sin((double)i * 0.1) * 500.0;
    samrena_vector_push(result.equity_curve, &pt);
  }

  bool ok = port->write(port, &result, &strategy, path);
  ASSERT(ok, "write should succeed with large equity curve");

  char *content = read_file(path);
  ASSERT(content != NULL, "Output should be readable");
  ASSERT(strstr(content, "== Equity Curve") != NULL, "Should have equity curve");
  ASSERT(strstr(content, "<svg") != NULL, "Should contain SVG");

  free(content);
  unlink(path);
  port->close(port);
  samrena_destroy(arena);
  printf("  PASS\n");
  return 0;
}

/* ========== Main ========== */

int main(void) {
  int failures = 0;

  printf("=== Typst Report Adapter Tests ===\n\n");

  /* Adapter creation */
  failures += test_create_null_arena();
  failures += test_create_no_template();
  failures += test_create_with_template();

  /* Write validation */
  failures += test_write_null_params();

  /* Default report output */
  failures += test_default_report_generates_file();
  failures += test_default_report_preamble();
  failures += test_default_report_strategy_summary();
  failures += test_default_report_strategy_parameters();
  failures += test_default_report_performance_metrics();
  failures += test_default_report_trade_log();
  failures += test_default_report_equity_curve();
  failures += test_default_report_drawdown_chart();
  failures += test_default_report_monthly_returns();

  /* Edge cases */
  failures += test_empty_trades();
  failures += test_single_equity_point();
  failures += test_null_equity_curve();
  failures += test_null_trades();
  failures += test_unnamed_strategy();
  failures += test_no_stop_loss_take_profit();
  failures += test_flat_equity_curve();

  /* Template reports */
  failures += test_template_placeholder_substitution();
  failures += test_template_chart_placeholders();
  failures += test_template_unknown_placeholder();
  failures += test_template_missing_file();
  failures += test_template_unterminated_placeholder();

  /* Multi-code reports */
  failures += test_multi_report_universe_summary();
  failures += test_multi_report_per_code_details();
  failures += test_multi_report_full_trade_log();
  failures += test_multi_report_single_code_fallback();
  failures += test_write_multi_null_params();

  /* Large datasets */
  failures += test_large_equity_curve_downsampling();

  printf("\n=== Results: %d failures ===\n", failures);
  return failures > 0 ? EXIT_FAILURE : EXIT_SUCCESS;
}
