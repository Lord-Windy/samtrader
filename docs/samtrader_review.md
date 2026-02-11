# Samtrader Code Review

A comprehensive review of `apps/samtrader/` -- what was done well, what wasn't, and suggested improvements.

---

## Overview

Samtrader is an algorithmic trading backtester written in C11, comprising ~22 source files, ~20 headers, and ~17 test suites. It implements a complete pipeline: config parsing, rule-based strategy definition, indicator calculation, portfolio management, trade execution, metrics computation, and Typst-based PDF report generation. The codebase supports both single-code and multi-code (portfolio) backtesting.

**Total scope**: ~600 lines of headers and ~200 lines of source per module on average, with ~500KB of test code across 17 test executables.

---

## What Was Done Well

### 1. Hexagonal Architecture -- Textbook Clean

The ports-and-adapters pattern is applied rigorously and consistently:

- **Three ports** (`data_port.h`, `config_port.h`, `report_port.h`) define pure function-pointer interfaces with `void *impl` for adapter state and `Samrena *arena` for allocation context.
- **Three adapters** (PostgreSQL, INI file, Typst) implement these ports cleanly.
- The **domain layer is entirely decoupled** from I/O. Not a single domain file imports an adapter header. Rule evaluation, indicator calculation, and portfolio management know nothing about PostgreSQL or file I/O.
- This makes the domain trivially testable -- tests create mock data ports and exercise the full backtest loop without a database (as `test_backtest.c` and `test_e2e.c` demonstrate).

This is the strongest architectural aspect of the codebase.

### 2. Arena Allocation -- Zero Manual `free()` Calls

The entire application uses `samrena` arena allocation consistently:

- Every allocation flows through `SAMRENA_PUSH_TYPE`, `SAMRENA_PUSH_ARRAY_ZERO`, or `samrena_push`.
- Cleanup is a single `samrena_destroy(arena)` call per command.
- No manual `free()` anywhere in the codebase.
- Even string duplication is arena-based (`arena_strdup`).

This eliminates an entire class of memory bugs (leaks, double-frees, use-after-free of individual allocations) and makes the code significantly simpler than typical C memory management.

### 3. The Rule Engine -- Well-Designed DSL

The rule system is one of the most impressive parts:

- **Clean AST representation**: `SamtraderRule` nodes form a proper tree with comparison, composite (AND/OR), negation (NOT), and temporal (CONSECUTIVE/ANY_OF) node types.
- **Recursive descent parser** (`rule_parser.c`): Clean, readable, and correct. The keyword-vs-identifier distinction is handled properly. Trailing input is checked. The parser is only ~420 lines for a non-trivial grammar.
- **Text-based strategy definitions** like `AND(CROSS_ABOVE(SMA(20), SMA(50)), ABOVE(RSI(14), 30))` are expressive and readable in INI config files.
- **Temporal rules** (`CONSECUTIVE`, `ANY_OF`) add real analytical power beyond simple threshold comparisons.

### 4. Tagged Union for Multi-Output Indicators

The `SamtraderIndicatorValue` tagged union cleanly handles the fact that MACD has 3 outputs, Bollinger has 3, Stochastic has 2, and Pivot has 7, while simple indicators have 1. The type tag ensures correct access, and the union avoids wasting memory on unused fields. This is a good use of a C pattern for variant types.

### 5. SQL Injection Prevention

The PostgreSQL adapter uses `PQexecParams` with parameter placeholders (`$1`, `$2`, etc.) throughout. No string concatenation into SQL queries. This is the correct approach.

### 6. Comprehensive Test Suite

- **17 test executables** covering every module individually plus integration and E2E tests.
- **~500KB of test code** -- the tests are actually larger than the source, which is a good ratio for a financial application.
- **Mock data ports** in tests (`test_backtest.c`, `test_e2e.c`) allow full pipeline testing without a real database.
- **Edge cases tested**: rule parser edge cases (whitespace, nesting depth), indicator warmup periods, zero-trade backtests, single-bar data.
- **Numerical accuracy**: Indicator tests verify against hand-calculated values with `ASSERT_DOUBLE_EQ` (epsilon-based comparison).

### 7. Clean CLI Design

- Subcommand pattern (`backtest`, `list-symbols`, `validate`, `info`) with `getopt_long`.
- Distinct exit codes (`EXIT_CONFIG_ERROR`, `EXIT_DB_ERROR`, `EXIT_INVALID_STRATEGY`, `EXIT_INSUFFICIENT_DATA`) for scripting integration.
- Code priority resolution: CLI `--code` > config `codes` > config `code`, with a warning when both config fields exist.

### 8. Report Generation

The Typst report adapter generates publication-quality PDF reports with:
- Inline SVG equity curves and drawdown charts (no external charting dependency).
- Monthly returns heatmap table.
- Full trade log.
- Multi-code breakdown with per-code sections.
- Custom template support with `{{PLACEHOLDER}}` substitution.

### 9. Indicator Calculation Separation

Each indicator lives in its own source file (`indicator_sma.c`, `indicator_ema.c`, etc.), making them independently reviewable and testable. The dispatcher in `indicator.c` routes to the correct calculation function. This is much better than a monolithic indicator file.

### 10. Consistent Code Style

- Every file has the Apache 2.0 license header.
- Consistent naming: `samtrader_` prefix, `SAMTRADER_` for macros/enums.
- Consistent error handling: NULL checks at function entry, NULL returns on failure.
- Clean use of forward declarations.

---

## What Wasn't Done Well

### 1. Duplicated Code Between `main.c` and `code_data.c`

`collect_from_operand()`, `collect_indicator_operands()`, and `calculate_indicator_for_operand()` are **duplicated verbatim** between `src/main.c` (lines 221-284) and `src/domain/code_data.c` (lines 50-110). The `main.c` version even has an unused `arena` parameter with a `(void)arena` cast.

This is the most clear-cut DRY violation in the codebase. The `code_data.c` module was clearly added later to support multi-code backtesting, and these functions were copied rather than extracted to a shared location.

### 2. Error Callback Implementation Homeless in `universe.c`

`samtrader_set_error_callback()` and `samtrader_error_string()` are declared in `samtrader.h` (the root header) but implemented in `universe.c` with a TODO comment:

```c
/* TODO: extract to src/error.c when other modules need this */
```

This is a layering violation. The error infrastructure is a cross-cutting concern declared at the root level, but its implementation lives inside a domain module. A developer looking for it would never think to check `universe.c`.

### 3. `typst_report_adapter.c` -- 48KB Monolith

At ~48KB and ~1000+ lines, this is by far the largest source file. It contains:
- Template placeholder resolution
- Default report Typst markup generation
- SVG chart rendering (equity curves, drawdown)
- Monthly returns table computation
- Trade log formatting
- Multi-code report sections

While the code within it is well-structured with clear internal function boundaries, the sheer size makes it unwieldy. The SVG chart generation, monthly returns computation, and Typst markup are fundamentally different concerns.

### 4. No Library Target -- Monolithic Executable + Repeated Source Lists

The `CMakeLists.txt` has no intermediate library target. Every test executable redundantly lists the source files it needs:

```cmake
add_executable(samtrader_backtest_test
    test/test_backtest.c
    src/domain/ohlcv.c
    src/domain/indicator.c
    src/domain/indicator_sma.c
    # ... 15 more source files
)
```

This means the same indicator source files are listed ~10 times across different test targets. Adding a new source file requires updating many targets. A `samtrader_core` static library would eliminate this repetition.

### 5. `qsort` on Internal Vector Data

In `code_data.c:214`:
```c
qsort(dates->data, count, sizeof(time_t), compare_time);
```

This directly accesses the `data` member of `SamrenaVector`, coupling to the internal memory layout of the vector implementation. If `samrena` ever changes its vector internals, this breaks silently.

### 6. `gmtime()` Is Not Thread-Safe

In `postgres_adapter.c:46`:
```c
struct tm *tm_info = gmtime(&t);
```

`gmtime()` returns a pointer to a static buffer that is shared across threads. While samtrader is currently single-threaded, this is a latent bug. The POSIX `gmtime_r()` alternative is used elsewhere in the codebase (`localtime_r` in `main.c:789`), so this is an inconsistency.

### 7. `main.c` Is Doing Too Much

At 915 lines, `main.c` contains:
- CLI argument parsing
- Config loading and strategy construction
- The entire single-code backtest loop (a legacy path)
- Multi-code backtest orchestration
- Console metrics printing
- Helper functions that are duplicated elsewhere

The backtest loop itself (~80 lines, `main.c:517-599`) is inline in `cmd_backtest()` rather than being a callable domain function. This means the backtest logic cannot be reused or tested independently of the CLI entry point.

### 8. Generated Artifacts Checked Into Git

`backtest_report.typ` (68KB) and `backtest_report.pdf` (1MB) are checked into the repository. These are generated output files that should be in `.gitignore`.

### 9. `my_config.ini` Contains Credentials

```ini
conninfo = postgresql://sam:password@127.0.0.1:5432/samtrader
```

While this is a local dev config with a simple password, checking database credentials into a repository is bad practice. This should be in `.gitignore` or use an environment variable reference.

### 10. WMA Has O(n * period) Complexity

In `indicator_wma.c`, the weighted sum is recalculated from scratch for every bar:

```c
for (int j = 0; j < period; j++) {
    weighted_sum += window_bar->close * weight;
}
```

For a 200-period WMA over 5000 bars, that's 1,000,000 iterations instead of ~5000 with a sliding window approach. Not a problem for current data sizes, but it's an unnecessary algorithmic inefficiency compared to SMA (which uses a running sum).

### 11. Trades with PnL == 0 Counted as Losses

In `metrics.c:55-63`:
```c
if (trade->pnl > 0.0) {
    metrics->winning_trades++;
    // ...
} else {
    metrics->losing_trades++;
    // ...
}
```

A trade with exactly zero PnL is counted as a loss. While rare, this is technically incorrect. Break-even trades should either be a separate category or excluded from win/loss counting.

### 12. No Parser Error Messages

When the rule parser fails, it returns `NULL` with no indication of what went wrong or where. For a user-facing DSL, this makes debugging strategy definitions frustrating. A parse error at `CROSS_ABOVE(SMA(20), )` just gives "failed to parse entry_long rule" with no position or expected-token information.

---

## Suggested Improvements

### Priority 1 -- Quick Wins

#### 1.1 Extract Shared Indicator Helpers

Move `collect_indicator_operands()` and `calculate_indicator_for_operand()` to a shared header/source pair (e.g., `domain/indicator_helpers.h/.c` or into `code_data.h/.c` as public functions). Remove the duplicates from `main.c`. This is a 15-minute fix.

#### 1.2 Extract Error Implementation to `src/error.c`

Move `samtrader_set_error_callback()`, `samtrader_error_string()`, and the global callback variables out of `universe.c` into a dedicated `src/error.c`. The TODO is already there.

#### 1.3 Add `.gitignore` Entries

```gitignore
apps/samtrader/backtest_report.typ
apps/samtrader/backtest_report.pdf
apps/samtrader/my_config.ini
```

#### 1.4 Fix `gmtime()` to `gmtime_r()`

In `postgres_adapter.c`:
```c
struct tm tm_info;
gmtime_r(&t, &tm_info);
strftime(buf, buf_size, "%Y-%m-%d", &tm_info);
```

#### 1.5 Handle Zero-PnL Trades

Either add a `break_even_trades` counter or change the condition to `>= 0.0` for wins (more standard in quantitative finance).

### Priority 2 -- Structural Improvements

#### 2.1 Create a `samtrader_core` Static Library

```cmake
add_library(samtrader_core STATIC
    src/domain/ohlcv.c
    src/domain/indicator.c
    # ... all domain + adapter sources except main.c
)
target_link_libraries(samtrader_core PUBLIC samrena samdata m)

ptah_add_executable(samtrader SOURCES src/main.c DEPENDENCIES samtrader_core PostgreSQL::PostgreSQL)

# Tests become:
add_executable(samtrader_backtest_test test/test_backtest.c)
target_link_libraries(samtrader_backtest_test PRIVATE samtrader_core)
```

This eliminates ~200 lines of redundant source listings in `CMakeLists.txt`.

#### 2.2 Extract Backtest Loop to Domain Function

Move the backtest loop from `main.c` into a domain function:

```c
// domain/backtest.h
SamtraderBacktestResult *samtrader_run_backtest(
    Samrena *arena,
    SamtraderCodeData **code_data, size_t code_count,
    SamHashMap **date_indices,
    SamrenaVector *timeline,
    const SamtraderStrategy *strategy,
    const SamtraderBacktestConfig *config
);
```

This makes the core logic unit-testable without CLI scaffolding and reduces `main.c` to pure orchestration.

#### 2.3 Split `typst_report_adapter.c`

Break it into focused files:
- `typst_report_adapter.c` -- Port implementation, placeholder resolution, orchestration (~200 lines)
- `typst_chart_svg.c` -- SVG equity curve and drawdown chart generation (~200 lines)
- `typst_tables.c` -- Monthly returns, trade log, universe summary (~200 lines)
- `typst_default_template.c` -- The default report Typst markup string (~300 lines)

#### 2.4 Add Parser Error Reporting

Add position and expected-token information to parse failures:

```c
typedef struct {
    const char *pos;
    Samrena *arena;
    const char *error_msg;  // Set on failure
    int error_pos;          // Character offset where error occurred
} RuleParser;
```

Surface this through `samtrader_rule_parse()` so the CLI can print: `Error at position 24: expected ')' after operand`.

### Priority 3 -- Enhancements

#### 3.1 Add a `SamrenaVector` Sort Function

Instead of `qsort(dates->data, ...)`, add `samrena_vector_sort()` to the samrena library to encapsulate the internal data access.

#### 3.2 Optimize WMA with Sliding Window

Use the Diophantine sliding window technique: maintain a running `weighted_sum` and a running `simple_sum`, then update incrementally:
```
weighted_sum += period * new_price - simple_sum
simple_sum += new_price - old_price
```

#### 3.3 Add CSV Data Adapter

A CSV adapter implementing `SamtraderDataPort` would remove the PostgreSQL dependency for simple use cases. The hexagonal architecture already supports this -- just implement the three function pointers.

#### 3.4 Add Backtest Config Validation

Currently, if `position_size` is set to 5.0 (500%) or `stop_loss` is negative, there's no validation. A `samtrader_validate_config()` function could catch these before the backtest loop runs.

#### 3.5 Add `--dry-run` Flag

Print the parsed strategy, resolved universe, and indicator list without connecting to the database or running the backtest. Useful for debugging config issues.

#### 3.6 ROC, STDDEV, OBV, VWAP Indicators Are Declared but Not Implemented

The `SamtraderIndicatorType` enum declares `SAMTRADER_IND_ROC`, `SAMTRADER_IND_STDDEV`, `SAMTRADER_IND_OBV`, and `SAMTRADER_IND_VWAP`, but no calculation functions exist for them. The dispatcher in `indicator.c` would fall through or return NULL. Either implement them or remove them from the enum to avoid confusion.

---

## Summary

| Category | Rating |
|---|---|
| Architecture | Excellent -- hexagonal pattern rigorously applied |
| Memory Management | Excellent -- arena allocation, zero manual frees |
| Domain Logic | Very Good -- clean rule engine, indicators, execution |
| Test Coverage | Excellent -- 17 test suites, ~500KB of test code |
| Security | Good -- parameterized SQL, no injection vectors |
| Code Organization | Good, with room to improve (DRY violations, large files) |
| Error Handling | Adequate -- consistent NULL checks, but weak parse errors |
| Build System | Adequate -- works, but redundant source listings |
| Repo Hygiene | Needs Work -- generated files and credentials in git |

The codebase demonstrates strong software engineering fundamentals. The hexagonal architecture, arena allocation, and rule engine design show thoughtful upfront design. The main areas for improvement are structural: extracting duplicated code, splitting oversized files, and creating a library target to simplify the build. None of the issues are correctness-critical -- the suggested improvements are about maintainability and developer experience.
