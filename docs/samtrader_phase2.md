# Samtrader Phase 2: Multi-Code Backtesting TRD

## 1. Overview

### 1.1 Purpose

Phase 2 expands samtrader's backtesting engine to support multiple stock codes
in a single backtest run. Users can specify an arbitrary number of codes (all
from the same exchange) in the configuration file, and the backtest will
evaluate the strategy against each code using a shared portfolio.

### 1.2 Goals

- Allow comma-separated lists of codes in the `[backtest]` config section
- Run the same strategy independently against each code's OHLCV data
- Share a single portfolio across all codes (unified cash pool, cross-code
  position tracking)
- Respect `max_positions` as a global limit across all codes
- Produce an aggregate report with per-code breakdowns
- Maintain full backward compatibility with single-code configs

### 1.3 Non-Goals (This Phase)

- Cross-exchange backtesting (multiple exchanges in one run)
- Code-specific strategy rules (same strategy applies to all codes)
- Correlation-based position sizing or portfolio optimization
- Date-range-per-code overrides (all codes use the same date window)
- Real-time or streaming data

### 1.4 Relationship to Phase 1

Phase 1 established the hexagonal architecture, rule system, indicator engine,
and single-code backtesting pipeline. Phase 2 builds on all of these without
breaking existing interfaces. The core domain types (`SamtraderOhlcv`,
`SamtraderPosition`, `SamtraderPortfolio`, `SamtraderRule`, etc.) require no
structural changes -- the portfolio already keys positions by code and supports
multiple concurrent positions.

---

## 2. Architecture Changes

### 2.1 What Stays the Same

The hexagonal structure, ports, and adapters are unchanged:

```
Ports:     data_port.h, config_port.h, report_port.h   -- no changes
Adapters:  postgres_adapter, file_config_adapter         -- no changes
Domain:    indicator, rule, rule_parser, rule_eval,
           position, execution, metrics                  -- no changes
```

### 2.2 What Changes

```
Modified:
  src/main.c                  -- Multi-code orchestration loop
  domain/backtest.h           -- New SamtraderBacktestCodeResult, extended result
  adapters/typst_report_adapter.c -- Per-code sections in report

New:
  domain/backtest.c           -- Multi-code backtest orchestration (extracted from main.c)
  include/samtrader/domain/universe.h -- Code universe parsing & validation
  src/domain/universe.c       -- Implementation
```

### 2.3 Data Flow (Multi-Code)

```
                         ┌─────────────────────────────┐
                         │         Config File          │
                         │  codes = CBA,BHP,WBC,NAB    │
                         │  exchange = ASX              │
                         └──────────────┬──────────────┘
                                        │
                                        ▼
                         ┌─────────────────────────────┐
                         │     Universe Module          │
                         │  Parse codes → string array  │
                         │  Validate against database   │
                         └──────────────┬──────────────┘
                                        │
                    ┌───────────┬───────┴───────┬───────────┐
                    ▼           ▼               ▼           ▼
              ┌──────────┐ ┌──────────┐  ┌──────────┐ ┌──────────┐
              │ CBA OHLCV│ │ BHP OHLCV│  │ WBC OHLCV│ │ NAB OHLCV│
              │+ indicators│+ indicators│ │+ indicators│+ indicators│
              └─────┬────┘ └────┬─────┘  └────┬─────┘ └────┬─────┘
                    │           │              │            │
                    └───────────┴──────┬───────┴────────────┘
                                       │
                                       ▼
                         ┌─────────────────────────────┐
                         │    Unified Date Timeline     │
                         │  Merge all dates, iterate    │
                         │  each bar date across codes  │
                         └──────────────┬──────────────┘
                                        │
                                        ▼
                         ┌─────────────────────────────┐
                         │    Shared Portfolio          │
                         │  Cash pool, positions keyed  │
                         │  by code, global max_pos     │
                         └──────────────┬──────────────┘
                                        │
                         ┌──────────────┴──────────────┐
                         │                             │
                         ▼                             ▼
                  ┌─────────────┐              ┌─────────────┐
                  │ Aggregate   │              │  Per-Code   │
                  │ Metrics     │              │  Metrics    │
                  └──────┬──────┘              └──────┬──────┘
                         │                            │
                         └────────────┬───────────────┘
                                      ▼
                         ┌─────────────────────────────┐
                         │    Report (Typst)            │
                         │  Summary + per-code sections │
                         └─────────────────────────────┘
```

---

## 3. Configuration

### 3.1 Multi-Code Config Format

The `codes` key replaces the singular `code` key. The singular `code` key
remains supported for backward compatibility.

```ini
[database]
conninfo = postgres://user:password@localhost:5432/samtrader

[backtest]
initial_capital = 100000.0
commission_per_trade = 9.95
commission_pct = 0.1
slippage_pct = 0.05
allow_shorting = false
risk_free_rate = 0.05
start_date = 2020-01-01
end_date = 2024-12-31

; NEW: comma-separated list of codes (all from the same exchange)
codes = CBA,BHP,WBC,NAB,ANZ,RIO,FMG,CSL,WES,TLS
exchange = ASX

[strategy]
name = Multi-Code SMA Crossover
description = SMA crossover applied across top ASX stocks

entry_long = CROSS_ABOVE(SMA(20), SMA(50))
exit_long = CROSS_BELOW(SMA(20), SMA(50))

position_size = 0.10
stop_loss = 5.0
take_profit = 0.0
max_positions = 5

[report]
; Optional template
template_path = /path/to/template.typ
```

### 3.2 Config Parsing Rules

| Config Key | Behavior |
|---|---|
| `codes = CBA,BHP,WBC` | New multi-code key. Comma-separated, whitespace trimmed. |
| `code = CBA` | Legacy single-code key. Still supported. |
| Both present | `codes` takes precedence; `code` is ignored with a warning. |
| Neither present | Error: at least one code is required. |
| `--code` CLI flag | Overrides both `code` and `codes` with a single code. |

### 3.3 Parsing Specification

```
Input:   "  CBA , BHP ,WBC,  NAB  "
Output:  ["CBA", "BHP", "WBC", "NAB"]
```

- Split on commas
- Trim leading/trailing whitespace from each token
- Convert to uppercase
- Reject empty tokens (e.g., `"CBA,,BHP"` is an error)
- Reject duplicate codes (e.g., `"CBA,BHP,CBA"` is an error)
- No limit on number of codes

---

## 4. Universe Module

### 4.1 Purpose

The universe module is responsible for parsing the code list from config and
validating that each code exists in the database for the given exchange and
date range.

### 4.2 Interface

```c
/**
 * @brief A validated set of codes for backtesting.
 */
typedef struct {
    const char **codes;    /**< Array of code strings (arena-allocated) */
    size_t count;          /**< Number of codes */
    const char *exchange;  /**< Exchange (shared by all codes) */
} SamtraderUniverse;

/**
 * @brief Parse a comma-separated codes string into a universe.
 *
 * @param arena Memory arena for allocation
 * @param codes_str Comma-separated codes string (e.g., "CBA,BHP,WBC")
 * @param exchange Exchange identifier
 * @return Parsed universe, or NULL on parse error
 */
SamtraderUniverse *samtrader_universe_parse(
    Samrena *arena,
    const char *codes_str,
    const char *exchange
);

/**
 * @brief Validate that all codes in the universe have data in the database.
 *
 * Checks each code against the data port. Codes with insufficient data
 * (< MIN_OHLCV_BARS) are removed with a warning printed to stderr.
 * Returns the number of valid codes remaining.
 *
 * @param universe Universe to validate (modified in place)
 * @param data_port Data source for validation
 * @param start_date Backtest start date
 * @param end_date Backtest end date
 * @return Number of valid codes, or -1 on error
 */
int samtrader_universe_validate(
    SamtraderUniverse *universe,
    SamtraderDataPort *data_port,
    time_t start_date,
    time_t end_date
);
```

### 4.3 Validation Behavior

For each code in the universe:
1. Fetch OHLCV data for the code, exchange, and date range from the data port
2. If the fetch returns NULL or fewer than `MIN_OHLCV_BARS` (30): print
   a warning and skip the code
3. If all codes fail validation: return error (exit code 5)
4. If some codes fail: continue with the valid subset and print a summary

Example output:
```
Warning: skipping XYZ.ASX (only 12 bars, minimum 30 required)
Warning: skipping FOO.ASX (no data found)
Backtesting 8 of 10 codes on ASX
```

---

## 5. Multi-Code Backtest Engine

### 5.1 Per-Code Data Structures

Each code gets its own OHLCV data and indicator cache:

```c
/**
 * @brief Per-code data bundle for backtesting.
 *
 * Holds the OHLCV time series and pre-computed indicators for a single code.
 */
typedef struct {
    const char *code;                /**< Stock symbol */
    const char *exchange;            /**< Exchange identifier */
    SamrenaVector *ohlcv;            /**< OHLCV data for this code */
    SamHashMap *indicators;          /**< Indicator key -> SamtraderIndicatorSeries */
    size_t bar_count;                /**< Number of bars */
} SamtraderCodeData;
```

### 5.2 Date Timeline

Different codes may have different trading days (listing dates, trading halts,
delistings). The backtest engine builds a unified date timeline:

1. Collect all unique dates across all codes' OHLCV data
2. Sort dates chronologically
3. For each date in the timeline, check which codes have a bar on that date
4. Only evaluate rules for codes that have a bar on the current date

```c
/**
 * @brief Build a sorted, deduplicated date timeline from multiple OHLCV vectors.
 *
 * @param arena Memory arena for allocation
 * @param code_data Array of per-code data bundles
 * @param code_count Number of codes
 * @return Sorted vector of unique time_t values, or NULL on error
 */
SamrenaVector *samtrader_build_date_timeline(
    Samrena *arena,
    SamtraderCodeData *code_data,
    size_t code_count
);
```

Each `SamtraderCodeData` also maintains a date-to-index lookup (SamHashMap
mapping date → bar index) for O(1) access during the backtest loop:

```c
/**
 * @brief Build a date-to-bar-index lookup map for a code's OHLCV data.
 *
 * @param arena Memory arena
 * @param ohlcv OHLCV data vector
 * @return HashMap mapping time_t (as string key) -> size_t* index
 */
SamHashMap *samtrader_build_date_index(
    Samrena *arena,
    SamrenaVector *ohlcv
);
```

### 5.3 Backtest Loop (Pseudocode)

```
for each date in unified_timeline:
    build price_map from all codes that have a bar on this date

    check_triggers(portfolio, price_map)    // stop loss / take profit

    for each code in universe:
        if code has no bar on this date:
            continue

        bar_index = date_index_lookup[code][date]

        // Exit evaluation
        if portfolio has position in code:
            evaluate exit rules at bar_index using code's OHLCV + indicators
            if triggered: exit position

        // Entry evaluation (respects global max_positions)
        if portfolio does NOT have position in code:
            if portfolio position_count >= max_positions:
                continue
            evaluate entry rules at bar_index using code's OHLCV + indicators
            if triggered: enter position

    record equity point (cash + sum of all position market values)
```

### 5.4 Position Management

The existing portfolio already supports this:
- Positions are keyed by code in a `SamHashMap`
- `samtrader_portfolio_position_count()` returns the total across all codes
- `samtrader_portfolio_total_equity()` accepts a `price_map` with multiple
  code→price entries
- `max_positions` naturally limits total concurrent positions across all codes

No changes to `portfolio.h`, `position.h`, or `execution.h`.

### 5.5 Per-Code vs Aggregate Metrics

Phase 2 produces both per-code and aggregate results:

```c
/**
 * @brief Per-code backtest result.
 *
 * Tracks trades and metrics for a single code within a multi-code backtest.
 */
typedef struct {
    const char *code;           /**< Stock symbol */
    const char *exchange;       /**< Exchange identifier */
    int total_trades;           /**< Trades for this code */
    int winning_trades;         /**< Winning trades for this code */
    int losing_trades;          /**< Losing trades for this code */
    double total_pnl;           /**< Sum of PnL for this code */
    double win_rate;            /**< Win rate for this code */
    double largest_win;         /**< Largest single win */
    double largest_loss;        /**< Largest single loss */
} SamtraderCodeResult;

/**
 * @brief Extended backtest result for multi-code runs.
 *
 * Contains the aggregate SamtraderBacktestResult plus per-code breakdowns.
 */
typedef struct {
    SamtraderBacktestResult aggregate;   /**< Overall portfolio metrics */
    SamtraderCodeResult *code_results;   /**< Array of per-code results */
    size_t code_count;                   /**< Number of codes */
} SamtraderMultiCodeResult;
```

Per-code metrics are derived by filtering `closed_trades` by code after the
backtest completes. The aggregate metrics use the full portfolio equity curve
and all trades, exactly as Phase 1.

---

## 6. Report Changes

### 6.1 Report Structure

The Typst report is extended with per-code sections:

```
1. Strategy Summary (unchanged)
2. Aggregate Performance Metrics (unchanged format, now reflects multi-code)
3. Portfolio Equity Curve (unchanged -- shows total portfolio value)
4. Drawdown Chart (unchanged -- shows total portfolio drawdown)
5. NEW: Universe Summary Table
   | Code | Trades | Win Rate | Total PnL | Largest Win | Largest Loss |
6. NEW: Per-Code Detail Sections (one per code)
   - Code-specific trade count, win rate, PnL
   - Code-specific trade log
7. Full Trade Log (all codes, sorted by date)
8. Monthly/Yearly Returns (unchanged -- portfolio-level)
```

### 6.2 Report Port Interface

No changes to `report_port.h`. The `write` function already receives
`SamtraderBacktestResult` and `SamtraderStrategy`. The adapter inspects
`result->trades` to partition by code internally. The new
`SamtraderMultiCodeResult` embeds a `SamtraderBacktestResult` as its first
member, so it can be cast.

Alternatively, the report adapter receives the extended result via a new
function pointer:

```c
/**
 * @brief Extended write function for multi-code results.
 *
 * Falls back to the standard write function for single-code results.
 */
typedef bool (*SamtraderReportWriteMultiFn)(
    SamtraderReportPort *port,
    SamtraderMultiCodeResult *result,
    SamtraderStrategy *strategy,
    const char *output_path
);
```

If the report port does not implement `write_multi`, the orchestrator falls
back to the existing `write` function using only the aggregate result.

---

## 7. CLI Changes

### 7.1 Backtest Command

The `backtest` command works identically. The multi-code behavior is driven
entirely by the config file (`codes` vs `code`).

```bash
# Multi-code (driven by config)
samtrader backtest -c multi_stock.ini -o report.typ

# Single-code override (same as Phase 1)
samtrader backtest -c multi_stock.ini --code CBA -o report.typ
```

When `--code` is provided on the command line, it overrides both `code` and
`codes` from the config, running a single-code backtest.

### 7.2 New: Info for Multiple Codes

```bash
# Show info for all codes in config
samtrader info -c multi_stock.ini

# Still works for single code
samtrader info --code CBA --exchange ASX
```

### 7.3 Console Output

During a multi-code backtest, progress is printed to stderr:

```
Loading strategy: Multi-Code SMA Crossover
Validating 10 codes on ASX...
  CBA: 1247 bars [OK]
  BHP: 1247 bars [OK]
  WBC: 1247 bars [OK]
  ...
  XYZ: 12 bars [SKIP - insufficient data]
Running backtest: 9 codes, 2020-01-01 to 2024-12-31
  Processing: ████████████████████ 100% (1247 dates)

=== Aggregate Results ===
Total Return:     45.23%
Annualized:       7.82%
Sharpe Ratio:     0.95
Max Drawdown:     -18.4%
Total Trades:     87
Win Rate:         58.6%

=== Per-Code Summary ===
  CBA:  12 trades, 66.7% win rate, +$8,234
  BHP:   9 trades, 55.6% win rate, +$4,112
  WBC:  11 trades, 54.5% win rate, +$2,891
  ...

Report written to: report.typ
```

---

## 8. Error Handling

### 8.1 New Error Conditions

| Condition | Behavior | Exit Code |
|---|---|---|
| Empty `codes` value | Error: "no codes specified" | 2 (config error) |
| Duplicate code in list | Error: "duplicate code: CBA" | 2 (config error) |
| Empty token in codes | Error: "empty code in codes list" | 2 (config error) |
| All codes fail validation | Error: "no valid codes after validation" | 5 (insufficient data) |
| Some codes fail validation | Warning printed, continue with valid subset | 0 (success) |
| Database fetch fails for one code | Warning printed, skip code | 0 (success) |
| Database fetch fails for all codes | Error: "failed to fetch data for any code" | 3 (db error) |

### 8.2 Existing Error Codes (Unchanged)

| Code | Meaning |
|---|---|
| 0 | Success |
| 1 | General error |
| 2 | Configuration error |
| 3 | Database connection error |
| 4 | Invalid strategy/rule |
| 5 | Insufficient data |

---

## 9. Implementation Plan

### Phase 2.1: Universe Module
1. Create `include/samtrader/domain/universe.h` and `src/domain/universe.c`
2. Implement `samtrader_universe_parse()` -- comma-separated parsing, trimming,
   dedup
3. Implement `samtrader_universe_validate()` -- per-code data check against
   data port
4. Unit tests for parsing (edge cases: whitespace, duplicates, empty tokens)
5. Integration test for validation against mock/real data

### Phase 2.2: Multi-Code Data Loading
1. Create `SamtraderCodeData` structure
2. Implement per-code OHLCV fetching loop
3. Implement per-code indicator pre-computation
4. Implement `samtrader_build_date_timeline()` -- unified date merge
5. Implement `samtrader_build_date_index()` -- per-code date→index lookup
6. Tests for date timeline merging (overlapping, gaps, single code)

### Phase 2.3: Multi-Code Backtest Loop
1. Refactor `cmd_backtest()` in `main.c` to handle `codes` config key
2. Implement the multi-code backtest loop per Section 5.3
3. Build composite `price_map` across all codes for equity calculation
4. Ensure `max_positions` is respected globally
5. Tests: multi-code backtest with known outcomes

### Phase 2.4: Per-Code Metrics
1. Implement `SamtraderCodeResult` computation (filter trades by code)
2. Implement `SamtraderMultiCodeResult` assembly
3. Tests: per-code metric correctness

### Phase 2.5: Report & CLI
1. Extend Typst report adapter with universe summary table
2. Add per-code detail sections to report template
3. Update console output with per-code summary
4. Update `cmd_info` to support config-driven multi-code info
5. Tests: report output contains per-code sections

### Phase 2.6: Integration & Polish
1. End-to-end test: multi-code backtest → report
2. Backward compatibility test: single `code` config still works identically
3. Update example configs with multi-code examples
4. Update README with multi-code documentation

---

## 10. Backward Compatibility

### 10.1 Config Compatibility

| Scenario | Phase 1 Behavior | Phase 2 Behavior |
|---|---|---|
| `code = CBA` only | Single-code backtest | Identical single-code backtest |
| `codes = CBA` only | N/A (error) | Single-code backtest |
| `codes = CBA,BHP` | N/A (error) | Multi-code backtest |
| Both `code` and `codes` | N/A | `codes` wins, warning printed |
| `--code CBA` CLI flag | Overrides config `code` | Overrides both `code` and `codes` |

### 10.2 API Compatibility

- All existing public functions retain their signatures
- `SamtraderBacktestResult` is unchanged
- `SamtraderMultiCodeResult` is additive (new struct)
- No existing tests should break

---

## 11. Performance Considerations

### 11.1 Memory

- Each code adds one OHLCV vector + one indicator hashmap
- For 10 codes with 5 years of daily data (~1250 bars each): ~10 × 1250
  × sizeof(SamtraderOhlcv) ≈ negligible
- All allocations through samrena for bulk deallocation

### 11.2 Database

- One query per code (sequential). For 10 codes, this is 10 queries.
- Each query fetches the full date range in one batch (no pagination)
- Connection is reused across all fetches

### 11.3 Computation

- Indicators are pre-computed once per code (no redundant recalculation)
- The unified date timeline iteration is O(dates × codes) per bar
- Date-to-index lookups are O(1) via hashmap
- Total complexity: O(D × C × R) where D = dates, C = codes, R = rule
  evaluation cost

### 11.4 Scaling Expectations

| Codes | Bars/Code | Total Bars | Expected Runtime |
|---|---|---|---|
| 1 | 1250 | 1250 | Baseline (Phase 1) |
| 10 | 1250 | 12,500 | ~10× baseline |
| 50 | 1250 | 62,500 | ~50× baseline |
| 100 | 1250 | 125,000 | ~100× baseline |

Runtime scales linearly with the number of codes. No parallelism is needed at
this scale.

---

## 12. Testing Strategy

### 12.1 Unit Tests

- **Universe parsing**: valid lists, whitespace, duplicates, empty tokens,
  single code
- **Date timeline**: overlapping dates, disjoint dates, single code passthrough
- **Date index lookup**: correct mapping, missing dates
- **Per-code metrics**: correct trade filtering and aggregation

### 12.2 Integration Tests

- Multi-code backtest with mock data (known trades, verify per-code PnL)
- `max_positions` enforcement across codes
- Partial code validation (some codes skipped, others proceed)
- Backward compat: single `code` config produces identical results to Phase 1

### 12.3 End-to-End Tests

- Full pipeline: multi-code config → DB fetch → backtest → report
- Report contains universe summary table and per-code sections
- CLI output matches expected format

---

## 13. Dependencies

No new external dependencies. Same as Phase 1:
- **samrena**: Arena-based memory allocation
- **samdata**: HashMap, Set, Hash functions
- **libpq**: PostgreSQL client library

---

## 14. Future Extensions (Out of Scope)

- Cross-exchange backtesting (codes from multiple exchanges)
- Per-code strategy overrides (different rules per code)
- Sector/industry grouping and allocation limits
- Correlation-aware position sizing
- Parallel data fetching (multi-threaded DB queries)
- Code screening/filtering rules (e.g., "all codes with volume > X")
- Walk-forward optimization across the code universe
