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

#define _POSIX_C_SOURCE 200809L

#include <getopt.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include <samdata/samhashmap.h>
#include <samrena.h>
#include <samvector.h>

#include <samtrader/adapters/file_config_adapter.h>
#include <samtrader/adapters/postgres_adapter.h>
#include <samtrader/adapters/typst_report_adapter.h>
#include <samtrader/domain/backtest.h>
#include <samtrader/domain/code_data.h>
#include <samtrader/domain/execution.h>
#include <samtrader/domain/indicator.h>
#include <samtrader/domain/metrics.h>
#include <samtrader/domain/ohlcv.h>
#include <samtrader/domain/portfolio.h>
#include <samtrader/domain/position.h>
#include <samtrader/domain/rule.h>
#include <samtrader/domain/strategy.h>
#include <samtrader/domain/universe.h>
#include <samtrader/ports/config_port.h>
#include <samtrader/ports/data_port.h>
#include <samtrader/ports/report_port.h>
#include <samtrader/samtrader.h>

/* TRD Section 10.2 exit codes */
#define EXIT_GENERAL_ERROR 1
#define EXIT_CONFIG_ERROR 2
#define EXIT_DB_ERROR 3
#define EXIT_INVALID_STRATEGY 4
#define EXIT_INSUFFICIENT_DATA 5

#define INDICATOR_KEY_BUF_SIZE 64
#define DATE_KEY_BUF_SIZE 32
#define MIN_OHLCV_BARS 30

typedef struct {
  const char *config_path;   /* -c / --config */
  const char *strategy_path; /* -s / --strategy */
  const char *output_path;   /* -o / --output */
  const char *exchange;      /* --exchange */
  const char *code;          /* --code */
} CliArgs;

typedef enum { CMD_BACKTEST, CMD_LIST_SYMBOLS, CMD_VALIDATE, CMD_INFO, CMD_HELP } Command;

static void print_usage(const char *prog) {
  fprintf(stderr,
          "Usage: %s <command> [options]\n"
          "\n"
          "samtrader - Algorithmic Trading Backtester\n"
          "\n"
          "Commands:\n"
          "  backtest       Run a backtest\n"
          "  list-symbols   List available symbols\n"
          "  validate       Validate a strategy file\n"
          "  info           Show data range for a symbol (or all codes in config)\n"
          "\n"
          "Options:\n"
          "  -c, --config <path>     Config file path (required for backtest)\n"
          "  -s, --strategy <path>   Strategy file path\n"
          "  -o, --output <path>     Output report path\n"
          "      --exchange <name>   Exchange name\n"
          "      --code <symbol>     Symbol code\n"
          "  -h, --help              Show this help message\n",
          prog);
}

static int parse_command(const char *arg) {
  if (strcmp(arg, "backtest") == 0)
    return CMD_BACKTEST;
  if (strcmp(arg, "list-symbols") == 0)
    return CMD_LIST_SYMBOLS;
  if (strcmp(arg, "validate") == 0)
    return CMD_VALIDATE;
  if (strcmp(arg, "info") == 0)
    return CMD_INFO;
  if (strcmp(arg, "--help") == 0 || strcmp(arg, "-h") == 0)
    return CMD_HELP;
  return -1;
}

static int parse_args(int argc, char *argv[], Command *cmd, CliArgs *args) {
  static const struct option long_options[] = {{"config", required_argument, NULL, 'c'},
                                               {"strategy", required_argument, NULL, 's'},
                                               {"output", required_argument, NULL, 'o'},
                                               {"exchange", required_argument, NULL, 'E'},
                                               {"code", required_argument, NULL, 'C'},
                                               {"help", no_argument, NULL, 'h'},
                                               {NULL, 0, NULL, 0}};

  memset(args, 0, sizeof(*args));

  if (argc < 2) {
    print_usage(argv[0]);
    return EXIT_GENERAL_ERROR;
  }

  int parsed = parse_command(argv[1]);
  if (parsed < 0) {
    fprintf(stderr, "Error: unknown command '%s'\n\n", argv[1]);
    print_usage(argv[0]);
    return EXIT_GENERAL_ERROR;
  }
  *cmd = (Command)parsed;

  if (*cmd == CMD_HELP) {
    print_usage(argv[0]);
    return -1; /* signal: help printed, exit 0 */
  }

  /* Reset getopt and parse flags from argv[1] onward.
     We shift optind to 2 so getopt skips argv[0] and the subcommand. */
  optind = 2;
  int opt;
  while ((opt = getopt_long(argc, argv, "c:s:o:h", long_options, NULL)) != -1) {
    switch (opt) {
      case 'c':
        args->config_path = optarg;
        break;
      case 's':
        args->strategy_path = optarg;
        break;
      case 'o':
        args->output_path = optarg;
        break;
      case 'E':
        args->exchange = optarg;
        break;
      case 'C':
        args->code = optarg;
        break;
      case 'h':
        print_usage(argv[0]);
        return -1;
      default:
        print_usage(argv[0]);
        return EXIT_GENERAL_ERROR;
    }
  }

  return 0;
}

static int validate_args(Command cmd, const CliArgs *args) {
  switch (cmd) {
    case CMD_BACKTEST:
      if (!args->config_path) {
        fprintf(stderr, "Error: backtest requires -c/--config\n");
        return EXIT_CONFIG_ERROR;
      }
      break;
    case CMD_LIST_SYMBOLS:
      if (!args->exchange) {
        fprintf(stderr, "Error: list-symbols requires --exchange\n");
        return EXIT_GENERAL_ERROR;
      }
      break;
    case CMD_VALIDATE:
      if (!args->strategy_path) {
        fprintf(stderr, "Error: validate requires -s/--strategy\n");
        return EXIT_INVALID_STRATEGY;
      }
      break;
    case CMD_INFO:
      if (!args->code && !args->config_path) {
        fprintf(stderr, "Error: info requires --code or -c/--config\n");
        return EXIT_GENERAL_ERROR;
      }
      if (args->code && !args->exchange && !args->config_path) {
        fprintf(stderr, "Error: info requires --exchange (or -c/--config)\n");
        return EXIT_GENERAL_ERROR;
      }
      break;
    case CMD_HELP:
      break;
  }
  return 0;
}

/*============================================================================
 * Helper Functions
 *============================================================================*/

static time_t parse_date(const char *date_str) {
  if (!date_str)
    return (time_t)-1;
  int year, month, day;
  if (sscanf(date_str, "%d-%d-%d", &year, &month, &day) != 3)
    return (time_t)-1;
  struct tm tm_val = {0};
  tm_val.tm_year = year - 1900;
  tm_val.tm_mon = month - 1;
  tm_val.tm_mday = day;
  tm_val.tm_isdst = -1;
  return mktime(&tm_val);
}

static void collect_from_operand(const SamtraderOperand *op, SamHashMap *seen_keys,
                                 SamrenaVector *operands, Samrena *arena) {
  if (op->type != SAMTRADER_OPERAND_INDICATOR)
    return;
  char key_buf[INDICATOR_KEY_BUF_SIZE];
  if (samtrader_operand_indicator_key(key_buf, sizeof(key_buf), op) < 0)
    return;
  if (samhashmap_contains(seen_keys, key_buf))
    return;
  samhashmap_put(seen_keys, key_buf, (void *)1);
  samrena_vector_push(operands, op);
  (void)arena;
}

static void collect_indicator_operands(const SamtraderRule *rule, SamHashMap *seen_keys,
                                       SamrenaVector *operands, Samrena *arena) {
  if (!rule)
    return;
  switch (rule->type) {
    case SAMTRADER_RULE_CROSS_ABOVE:
    case SAMTRADER_RULE_CROSS_BELOW:
    case SAMTRADER_RULE_ABOVE:
    case SAMTRADER_RULE_BELOW:
    case SAMTRADER_RULE_BETWEEN:
    case SAMTRADER_RULE_EQUALS:
      collect_from_operand(&rule->left, seen_keys, operands, arena);
      collect_from_operand(&rule->right, seen_keys, operands, arena);
      break;
    case SAMTRADER_RULE_AND:
    case SAMTRADER_RULE_OR:
      if (rule->children) {
        for (size_t i = 0; rule->children[i] != NULL; i++)
          collect_indicator_operands(rule->children[i], seen_keys, operands, arena);
      }
      break;
    case SAMTRADER_RULE_NOT:
      collect_indicator_operands(rule->child, seen_keys, operands, arena);
      break;
    case SAMTRADER_RULE_CONSECUTIVE:
    case SAMTRADER_RULE_ANY_OF:
      collect_indicator_operands(rule->child, seen_keys, operands, arena);
      break;
  }
}

static SamtraderIndicatorSeries *
calculate_indicator_for_operand(Samrena *arena, const SamtraderOperand *op, SamrenaVector *ohlcv) {
  switch (op->indicator.indicator_type) {
    case SAMTRADER_IND_MACD:
      return samtrader_calculate_macd(arena, ohlcv, op->indicator.period, op->indicator.param2,
                                      op->indicator.param3);
    case SAMTRADER_IND_BOLLINGER:
      return samtrader_calculate_bollinger(arena, ohlcv, op->indicator.period,
                                           op->indicator.param2 / 100.0);
    case SAMTRADER_IND_STOCHASTIC:
      return samtrader_calculate_stochastic(arena, ohlcv, op->indicator.period,
                                            op->indicator.param2);
    case SAMTRADER_IND_PIVOT:
      return samtrader_calculate_pivot(arena, ohlcv);
    default:
      return samtrader_indicator_calculate(arena, op->indicator.indicator_type, ohlcv,
                                           op->indicator.period);
  }
}

static SamHashMap *build_price_map(Samrena *arena, const SamtraderOhlcv *bar) {
  SamHashMap *price_map = samhashmap_create(4, arena);
  if (!price_map)
    return NULL;
  double *price = SAMRENA_PUSH_TYPE(arena, double);
  if (!price)
    return NULL;
  *price = bar->close;
  samhashmap_put(price_map, bar->code, price);
  return price_map;
}

static int load_strategy_from_config(SamtraderConfigPort *config, Samrena *arena,
                                     SamtraderStrategy *strategy) {
  memset(strategy, 0, sizeof(*strategy));

  strategy->name = config->get_string(config, "strategy", "name");
  if (!strategy->name)
    strategy->name = "Unnamed Strategy";

  strategy->description = config->get_string(config, "strategy", "description");
  if (!strategy->description)
    strategy->description = "";

  const char *entry_long_str = config->get_string(config, "strategy", "entry_long");
  if (!entry_long_str || entry_long_str[0] == '\0') {
    fprintf(stderr, "Error: strategy requires entry_long rule\n");
    return EXIT_INVALID_STRATEGY;
  }
  strategy->entry_long = samtrader_rule_parse(arena, entry_long_str);
  if (!strategy->entry_long) {
    fprintf(stderr, "Error: failed to parse entry_long rule: %s\n", entry_long_str);
    return EXIT_INVALID_STRATEGY;
  }

  const char *exit_long_str = config->get_string(config, "strategy", "exit_long");
  if (!exit_long_str || exit_long_str[0] == '\0') {
    fprintf(stderr, "Error: strategy requires exit_long rule\n");
    return EXIT_INVALID_STRATEGY;
  }
  strategy->exit_long = samtrader_rule_parse(arena, exit_long_str);
  if (!strategy->exit_long) {
    fprintf(stderr, "Error: failed to parse exit_long rule: %s\n", exit_long_str);
    return EXIT_INVALID_STRATEGY;
  }

  const char *entry_short_str = config->get_string(config, "strategy", "entry_short");
  if (entry_short_str && entry_short_str[0] != '\0')
    strategy->entry_short = samtrader_rule_parse(arena, entry_short_str);

  const char *exit_short_str = config->get_string(config, "strategy", "exit_short");
  if (exit_short_str && exit_short_str[0] != '\0')
    strategy->exit_short = samtrader_rule_parse(arena, exit_short_str);

  strategy->position_size = config->get_double(config, "strategy", "position_size", 0.25);
  strategy->stop_loss_pct = config->get_double(config, "strategy", "stop_loss", 0.0);
  strategy->take_profit_pct = config->get_double(config, "strategy", "take_profit", 0.0);
  strategy->max_positions = config->get_int(config, "strategy", "max_positions", 1);

  return 0;
}

static int load_strategy_from_file(const char *strategy_path, Samrena *arena,
                                   SamtraderStrategy *strategy) {
  SamtraderConfigPort *config = samtrader_file_config_adapter_create(arena, strategy_path);
  if (!config) {
    fprintf(stderr, "Error: failed to load strategy file: %s\n", strategy_path);
    return EXIT_INVALID_STRATEGY;
  }
  int rc = load_strategy_from_config(config, arena, strategy);
  config->close(config);
  return rc;
}

/*============================================================================
 * Command Implementations
 *============================================================================*/

static int cmd_backtest(const CliArgs *args) {
  int rc = EXIT_SUCCESS;
  Samrena *arena = samrena_create_default();
  if (!arena) {
    fprintf(stderr, "Error: failed to create memory arena\n");
    return EXIT_GENERAL_ERROR;
  }

  SamtraderConfigPort *config = NULL;
  SamtraderDataPort *data = NULL;
  SamtraderReportPort *report = NULL;

  /* Load config */
  config = samtrader_file_config_adapter_create(arena, args->config_path);
  if (!config) {
    fprintf(stderr, "Error: failed to load config: %s\n", args->config_path);
    rc = EXIT_CONFIG_ERROR;
    goto cleanup;
  }

  /* Read backtest parameters */
  const char *conninfo = config->get_string(config, "database", "conninfo");
  if (!conninfo) {
    fprintf(stderr, "Error: missing [database] conninfo in config\n");
    rc = EXIT_CONFIG_ERROR;
    goto cleanup;
  }

  /* Resolve code(s) - CLI --code overrides config; 'codes' wins over 'code' */
  const char *codes_str = config->get_string(config, "backtest", "codes");
  const char *code_single =
      args->code ? args->code : config->get_string(config, "backtest", "code");
  const char *exchange =
      args->exchange ? args->exchange : config->get_string(config, "backtest", "exchange");
  if (!exchange) {
    fprintf(stderr, "Error: backtest requires exchange\n");
    rc = EXIT_CONFIG_ERROR;
    goto cleanup;
  }

  const char *effective_codes;
  if (args->code) {
    effective_codes = args->code; /* CLI flag overrides everything */
  } else if (codes_str) {
    if (code_single)
      fprintf(stderr, "Warning: both 'codes' and 'code' in config; using 'codes'\n");
    effective_codes = codes_str;
  } else if (code_single) {
    effective_codes = code_single; /* Legacy single code */
  } else {
    fprintf(stderr, "Error: backtest requires code or codes\n");
    rc = EXIT_CONFIG_ERROR;
    goto cleanup;
  }

  SamtraderUniverse *universe = samtrader_universe_parse(arena, effective_codes, exchange);
  if (!universe) {
    fprintf(stderr, "Error: failed to parse codes\n");
    rc = EXIT_CONFIG_ERROR;
    goto cleanup;
  }

  const char *start_str = config->get_string(config, "backtest", "start_date");
  const char *end_str = config->get_string(config, "backtest", "end_date");
  time_t start_date = parse_date(start_str);
  time_t end_date = parse_date(end_str);
  if (start_date == (time_t)-1 || end_date == (time_t)-1) {
    fprintf(stderr, "Error: invalid start_date or end_date (expected YYYY-MM-DD)\n");
    rc = EXIT_CONFIG_ERROR;
    goto cleanup;
  }

  double initial_capital = config->get_double(config, "backtest", "initial_capital", 100000.0);
  double commission_flat = config->get_double(config, "backtest", "commission_per_trade", 0.0);
  double commission_pct = config->get_double(config, "backtest", "commission_pct", 0.0);
  double slippage_pct = config->get_double(config, "backtest", "slippage_pct", 0.0);
  bool allow_shorting = config->get_bool(config, "backtest", "allow_shorting", false);
  double risk_free_rate = config->get_double(config, "backtest", "risk_free_rate", 0.05);

  /* Load strategy */
  SamtraderStrategy strategy;
  if (args->strategy_path) {
    rc = load_strategy_from_file(args->strategy_path, arena, &strategy);
  } else {
    rc = load_strategy_from_config(config, arena, &strategy);
  }
  if (rc != 0)
    goto cleanup;

  /* Connect to database */
  data = samtrader_postgres_adapter_create(arena, conninfo);
  if (!data) {
    fprintf(stderr, "Error: failed to connect to database\n");
    rc = EXIT_DB_ERROR;
    goto cleanup;
  }

  /* Validate universe against data source */
  int valid_count = samtrader_universe_validate(universe, data, start_date, end_date);
  if (valid_count < 0) {
    fprintf(stderr, "Error: no valid codes in universe\n");
    rc = EXIT_INSUFFICIENT_DATA;
    goto cleanup;
  }

  /* Load per-code data, compute indicators, build date indices */
  printf("Loading universe (%zu codes)...\n", universe->count);

  SamtraderCodeData **code_data_arr =
      SAMRENA_PUSH_ARRAY_ZERO(arena, SamtraderCodeData *, universe->count);
  SamHashMap **date_indices = SAMRENA_PUSH_ARRAY_ZERO(arena, SamHashMap *, universe->count);

  for (size_t c = 0; c < universe->count; c++) {
    code_data_arr[c] =
        samtrader_load_code_data(arena, data, universe->codes[c], exchange, start_date, end_date);
    if (!code_data_arr[c]) {
      fprintf(stderr, "Error: failed to load data for %s\n", universe->codes[c]);
      rc = EXIT_DB_ERROR;
      goto cleanup;
    }
    printf("  Validated %s: %zu bars\n", universe->codes[c],
           samrena_vector_size(code_data_arr[c]->ohlcv));
    if (samtrader_code_data_compute_indicators(arena, code_data_arr[c], &strategy) < 0) {
      fprintf(stderr, "Error: failed to compute indicators for %s\n", universe->codes[c]);
      rc = EXIT_GENERAL_ERROR;
      goto cleanup;
    }
    date_indices[c] = samtrader_build_date_index(arena, code_data_arr[c]->ohlcv);
    if (!date_indices[c]) {
      fprintf(stderr, "Error: failed to build date index for %s\n", universe->codes[c]);
      rc = EXIT_GENERAL_ERROR;
      goto cleanup;
    }
  }

  /* Build unified date timeline */
  SamrenaVector *timeline = samtrader_build_date_timeline(arena, code_data_arr, universe->count);
  if (!timeline || samrena_vector_size(timeline) == 0) {
    fprintf(stderr, "Error: empty date timeline\n");
    rc = EXIT_INSUFFICIENT_DATA;
    goto cleanup;
  }
  printf("Timeline: %zu trading days\n", samrena_vector_size(timeline));

  /* Create portfolio */
  SamtraderPortfolio *portfolio = samtrader_portfolio_create(arena, initial_capital);
  if (!portfolio) {
    fprintf(stderr, "Error: failed to create portfolio\n");
    rc = EXIT_GENERAL_ERROR;
    goto cleanup;
  }

  /* Main backtest loop - iterate unified timeline */
  for (size_t t = 0; t < samrena_vector_size(timeline); t++) {
    time_t date = *(const time_t *)samrena_vector_at_const(timeline, t);

    /* Build composite price_map from all codes with bars on this date */
    SamHashMap *price_map = samhashmap_create(universe->count * 2, arena);
    if (!price_map)
      continue;

    char date_key[DATE_KEY_BUF_SIZE];
    snprintf(date_key, sizeof(date_key), "%ld", (long)date);

    for (size_t c = 0; c < universe->count; c++) {
      size_t *bar_idx = (size_t *)samhashmap_get(date_indices[c], date_key);
      if (!bar_idx)
        continue;
      const SamtraderOhlcv *bar =
          (const SamtraderOhlcv *)samrena_vector_at_const(code_data_arr[c]->ohlcv, *bar_idx);
      double *price = SAMRENA_PUSH_TYPE(arena, double);
      if (!price)
        continue;
      *price = bar->close;
      samhashmap_put(price_map, code_data_arr[c]->code, price);
    }

    /* Check stop loss / take profit triggers across all positions */
    samtrader_execution_check_triggers(portfolio, arena, price_map, date, commission_flat,
                                       commission_pct, slippage_pct);

    /* For each code with data on this date */
    for (size_t c = 0; c < universe->count; c++) {
      size_t *bar_idx = (size_t *)samhashmap_get(date_indices[c], date_key);
      if (!bar_idx)
        continue;

      const SamtraderOhlcv *bar =
          (const SamtraderOhlcv *)samrena_vector_at_const(code_data_arr[c]->ohlcv, *bar_idx);
      const char *code = code_data_arr[c]->code;

      /* Evaluate exit rules for existing positions */
      if (samtrader_portfolio_has_position(portfolio, code)) {
        SamtraderPosition *pos = samtrader_portfolio_get_position(portfolio, code);
        bool should_exit = false;
        if (pos && samtrader_position_is_long(pos)) {
          should_exit = samtrader_rule_evaluate(strategy.exit_long, code_data_arr[c]->ohlcv,
                                                code_data_arr[c]->indicators, *bar_idx);
        } else if (pos && samtrader_position_is_short(pos) && strategy.exit_short) {
          should_exit = samtrader_rule_evaluate(strategy.exit_short, code_data_arr[c]->ohlcv,
                                                code_data_arr[c]->indicators, *bar_idx);
        }
        if (should_exit) {
          samtrader_execution_exit_position(portfolio, arena, code, bar->close, date,
                                            commission_flat, commission_pct, slippage_pct);
        }
      }

      /* Evaluate entry rules (max_positions enforced globally) */
      if (!samtrader_portfolio_has_position(portfolio, code)) {
        bool enter_long = samtrader_rule_evaluate(strategy.entry_long, code_data_arr[c]->ohlcv,
                                                  code_data_arr[c]->indicators, *bar_idx);
        bool enter_short =
            allow_shorting && strategy.entry_short
                ? samtrader_rule_evaluate(strategy.entry_short, code_data_arr[c]->ohlcv,
                                          code_data_arr[c]->indicators, *bar_idx)
                : false;

        if (enter_long) {
          samtrader_execution_enter_long(portfolio, arena, code, exchange, bar->close, date,
                                         strategy.position_size, strategy.stop_loss_pct,
                                         strategy.take_profit_pct, strategy.max_positions,
                                         commission_flat, commission_pct, slippage_pct);
        } else if (enter_short) {
          samtrader_execution_enter_short(portfolio, arena, code, exchange, bar->close, date,
                                          strategy.position_size, strategy.stop_loss_pct,
                                          strategy.take_profit_pct, strategy.max_positions,
                                          commission_flat, commission_pct, slippage_pct);
        }
      }
    }

    /* Record equity (cash + all position market values) */
    double equity = samtrader_portfolio_total_equity(portfolio, price_map);
    samtrader_portfolio_record_equity(portfolio, arena, date, equity);
  }

  /* Calculate metrics */
  SamtraderMetrics *metrics = samtrader_metrics_calculate(arena, portfolio->closed_trades,
                                                          portfolio->equity_curve, risk_free_rate);
  if (!metrics) {
    fprintf(stderr, "Error: failed to calculate metrics\n");
    rc = EXIT_GENERAL_ERROR;
    goto cleanup;
  }

  /* Build backtest result */
  SamtraderBacktestResult *result = SAMRENA_PUSH_TYPE_ZERO(arena, SamtraderBacktestResult);
  if (!result) {
    fprintf(stderr, "Error: failed to allocate backtest result\n");
    rc = EXIT_GENERAL_ERROR;
    goto cleanup;
  }
  result->total_return = metrics->total_return;
  result->annualized_return = metrics->annualized_return;
  result->sharpe_ratio = metrics->sharpe_ratio;
  result->sortino_ratio = metrics->sortino_ratio;
  result->max_drawdown = metrics->max_drawdown;
  result->max_drawdown_duration = metrics->max_drawdown_duration;
  result->win_rate = metrics->win_rate;
  result->profit_factor = metrics->profit_factor;
  result->total_trades = metrics->total_trades;
  result->winning_trades = metrics->winning_trades;
  result->losing_trades = metrics->losing_trades;
  result->average_win = metrics->average_win;
  result->average_loss = metrics->average_loss;
  result->largest_win = metrics->largest_win;
  result->largest_loss = metrics->largest_loss;
  result->average_trade_duration = metrics->average_trade_duration;
  result->equity_curve = portfolio->equity_curve;
  result->trades = portfolio->closed_trades;

  /* Compute per-code metrics (before report generation) */
  SamtraderCodeResult *code_results = NULL;
  if (universe->count > 1) {
    code_results = samtrader_metrics_compute_per_code(arena, portfolio->closed_trades,
                                                      universe->codes, exchange, universe->count);
  }

  /* Generate report */
  const char *output_path = args->output_path ? args->output_path : "backtest_report.typ";
  const char *template_path = config->get_string(config, "report", "template_path");
  report = samtrader_typst_adapter_create(arena, template_path);
  if (report) {
    if (report->write_multi && universe->count > 1 && code_results) {
      SamtraderMultiCodeResult multi = {.aggregate = *result,
                                        .code_results = code_results,
                                        .code_count = universe->count};
      report->write_multi(report, &multi, &strategy, output_path);
    } else {
      report->write(report, result, &strategy, output_path);
    }
    printf("Report written to: %s\n", output_path);
  }

  /* Print metrics summary */
  samtrader_metrics_print(metrics);

  /* Print per-code metrics to console */
  if (code_results && universe->count > 1) {
    printf("\n=== Per-Code Breakdown ===\n");
    printf("%-10s %6s %6s %6s %10s %8s %10s %10s\n", "Code", "Trades", "Wins", "Losses",
           "Total PnL", "Win %", "Best", "Worst");
    printf("%-10s %6s %6s %6s %10s %8s %10s %10s\n", "----------", "------", "------", "------",
           "----------", "--------", "----------", "----------");
    for (size_t i = 0; i < universe->count; i++) {
      SamtraderCodeResult *cr = &code_results[i];
      printf("%-10s %6d %6d %6d %10.2f %7.2f%% %10.2f %10.2f\n", cr->code, cr->total_trades,
             cr->winning_trades, cr->losing_trades, cr->total_pnl, cr->win_rate * 100.0,
             cr->largest_win, cr->largest_loss);
    }
  }

cleanup:
  if (report)
    report->close(report);
  if (data)
    data->close(data);
  if (config)
    config->close(config);
  samrena_destroy(arena);
  return rc;
}

static int cmd_list_symbols(const CliArgs *args) {
  int rc = EXIT_SUCCESS;
  Samrena *arena = samrena_create_default();
  if (!arena) {
    fprintf(stderr, "Error: failed to create memory arena\n");
    return EXIT_GENERAL_ERROR;
  }

  SamtraderDataPort *data = NULL;
  const char *conninfo = NULL;

  if (args->config_path) {
    SamtraderConfigPort *config = samtrader_file_config_adapter_create(arena, args->config_path);
    if (config) {
      conninfo = config->get_string(config, "database", "conninfo");
      config->close(config);
    }
  }
  if (!conninfo)
    conninfo = getenv("SAMTRADER_DB");
  if (!conninfo) {
    fprintf(stderr, "Error: no database connection (use -c config or SAMTRADER_DB env)\n");
    rc = EXIT_DB_ERROR;
    goto cleanup;
  }

  data = samtrader_postgres_adapter_create(arena, conninfo);
  if (!data) {
    fprintf(stderr, "Error: failed to connect to database\n");
    rc = EXIT_DB_ERROR;
    goto cleanup;
  }

  SamrenaVector *symbols = data->list_symbols(data, args->exchange);
  if (!symbols) {
    fprintf(stderr, "Error: failed to list symbols\n");
    rc = EXIT_DB_ERROR;
    goto cleanup;
  }

  size_t count = samrena_vector_size(symbols);
  printf("Symbols on %s (%zu):\n", args->exchange, count);
  for (size_t i = 0; i < count; i++) {
    const char *const *sym = (const char *const *)samrena_vector_at_const(symbols, i);
    if (sym)
      printf("  %s\n", *sym);
  }

cleanup:
  if (data)
    data->close(data);
  samrena_destroy(arena);
  return rc;
}

static int cmd_validate(const CliArgs *args) {
  int rc = EXIT_SUCCESS;
  Samrena *arena = samrena_create_default();
  if (!arena) {
    fprintf(stderr, "Error: failed to create memory arena\n");
    return EXIT_GENERAL_ERROR;
  }

  SamtraderStrategy strategy;
  rc = load_strategy_from_file(args->strategy_path, arena, &strategy);
  if (rc != 0) {
    samrena_destroy(arena);
    return rc;
  }

  printf("Strategy: %s\n", strategy.name);
  printf("Description: %s\n", strategy.description);
  printf("Entry Long: parsed successfully\n");
  printf("Exit Long: parsed successfully\n");
  printf("Entry Short: %s\n", strategy.entry_short ? "parsed successfully" : "not defined");
  printf("Exit Short: %s\n", strategy.exit_short ? "parsed successfully" : "not defined");
  printf("Position Size: %.2f\n", strategy.position_size);
  printf("Stop Loss: %.2f%%\n", strategy.stop_loss_pct);
  printf("Take Profit: %.2f%%\n", strategy.take_profit_pct);
  printf("Max Positions: %d\n", strategy.max_positions);
  printf("\nStrategy is valid.\n");

  samrena_destroy(arena);
  return rc;
}

static int print_code_info(SamtraderDataPort *data, const char *code, const char *exchange) {
  time_t epoch_start = 0;
  time_t epoch_end = (time_t)4102444800; /* 2100-01-01 */
  SamrenaVector *ohlcv = data->fetch_ohlcv(data, code, exchange, epoch_start, epoch_end);
  if (!ohlcv || samrena_vector_size(ohlcv) == 0) {
    fprintf(stderr, "Error: no data found for %s.%s\n", code, exchange);
    return EXIT_INSUFFICIENT_DATA;
  }

  size_t count = samrena_vector_size(ohlcv);
  const SamtraderOhlcv *first = (const SamtraderOhlcv *)samrena_vector_at_const(ohlcv, 0);
  const SamtraderOhlcv *last = (const SamtraderOhlcv *)samrena_vector_at_const(ohlcv, count - 1);

  char first_date_str[32], last_date_str[32];
  struct tm first_tm, last_tm;
  localtime_r(&first->date, &first_tm);
  localtime_r(&last->date, &last_tm);
  strftime(first_date_str, sizeof(first_date_str), "%Y-%m-%d", &first_tm);
  strftime(last_date_str, sizeof(last_date_str), "%Y-%m-%d", &last_tm);

  printf("Symbol: %s.%s\n", code, exchange);
  printf("Date Range: %s to %s\n", first_date_str, last_date_str);
  printf("Total Bars: %zu\n", count);
  printf("First Close: %.2f\n", first->close);
  printf("Last Close: %.2f\n", last->close);

  return EXIT_SUCCESS;
}

static int cmd_info(const CliArgs *args) {
  int rc = EXIT_SUCCESS;
  Samrena *arena = samrena_create_default();
  if (!arena) {
    fprintf(stderr, "Error: failed to create memory arena\n");
    return EXIT_GENERAL_ERROR;
  }

  SamtraderDataPort *data = NULL;
  SamtraderConfigPort *config = NULL;
  const char *conninfo = NULL;
  const char *exchange = args->exchange;

  if (args->config_path) {
    config = samtrader_file_config_adapter_create(arena, args->config_path);
    if (config) {
      conninfo = config->get_string(config, "database", "conninfo");
      if (!exchange)
        exchange = config->get_string(config, "backtest", "exchange");
    }
  }
  if (!conninfo)
    conninfo = getenv("SAMTRADER_DB");
  if (!conninfo) {
    fprintf(stderr, "Error: no database connection (use -c config or SAMTRADER_DB env)\n");
    rc = EXIT_DB_ERROR;
    goto cleanup;
  }

  data = samtrader_postgres_adapter_create(arena, conninfo);
  if (!data) {
    fprintf(stderr, "Error: failed to connect to database\n");
    rc = EXIT_DB_ERROR;
    goto cleanup;
  }

  if (args->code) {
    /* Single-code info */
    if (!exchange) {
      fprintf(stderr, "Error: info requires --exchange\n");
      rc = EXIT_GENERAL_ERROR;
      goto cleanup;
    }
    rc = print_code_info(data, args->code, exchange);
  } else if (args->config_path && config) {
    /* Multi-code info from config */
    if (!exchange) {
      fprintf(stderr, "Error: no exchange in config\n");
      rc = EXIT_CONFIG_ERROR;
      goto cleanup;
    }
    const char *codes_str = config->get_string(config, "backtest", "codes");
    if (!codes_str)
      codes_str = config->get_string(config, "backtest", "code");
    if (!codes_str) {
      fprintf(stderr, "Error: no codes in config\n");
      rc = EXIT_CONFIG_ERROR;
      goto cleanup;
    }
    SamtraderUniverse *universe = samtrader_universe_parse(arena, codes_str, exchange);
    if (!universe) {
      fprintf(stderr, "Error: failed to parse codes\n");
      rc = EXIT_CONFIG_ERROR;
      goto cleanup;
    }
    for (size_t i = 0; i < universe->count; i++) {
      if (i > 0)
        printf("\n");
      int code_rc = print_code_info(data, universe->codes[i], exchange);
      if (code_rc != EXIT_SUCCESS)
        rc = code_rc;
    }
  }

cleanup:
  if (data)
    data->close(data);
  if (config)
    config->close(config);
  samrena_destroy(arena);
  return rc;
}

int main(int argc, char *argv[]) {
  Command cmd;
  CliArgs args;

  int rc = parse_args(argc, argv, &cmd, &args);
  if (rc == -1)
    return EXIT_SUCCESS; /* --help */
  if (rc != 0)
    return rc;

  rc = validate_args(cmd, &args);
  if (rc != 0)
    return rc;

  switch (cmd) {
    case CMD_BACKTEST:
      return cmd_backtest(&args);
    case CMD_LIST_SYMBOLS:
      return cmd_list_symbols(&args);
    case CMD_VALIDATE:
      return cmd_validate(&args);
    case CMD_INFO:
      return cmd_info(&args);
    case CMD_HELP:
      return EXIT_SUCCESS;
  }

  return EXIT_GENERAL_ERROR;
}
