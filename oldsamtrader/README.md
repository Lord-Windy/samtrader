# samtrader

An algorithmic trading backtester written in C. Define trading strategies using composable text-based rules, backtest them against historical OHLCV data from a PostgreSQL database, and generate performance reports in Typst format.

## Building

samtrader is part of the ptah monorepo. From the repository root:

```bash
mkdir build && cd build
cmake ..
make samtrader
```

### Dependencies

- **libpq** (PostgreSQL client library) - required for database access
- **samrena**, **samdata** - internal ptah libraries (built automatically)

## CLI Usage

```
samtrader <command> [options]
```

### Commands

#### `backtest` — Run a backtest

```bash
samtrader backtest -c config.ini [-s strategy.ini] [-o report.typ] [--code X] [--exchange Y]
```

| Option | Description |
|--------|-------------|
| `-c, --config <path>` | Config file path (**required**) |
| `-s, --strategy <path>` | Separate strategy file (otherwise reads strategy from config) |
| `-o, --output <path>` | Output report path (default: `backtest_report.typ`) |
| `--code <symbol>` | Symbol code (overrides config `[backtest] code`) |
| `--exchange <name>` | Exchange name (overrides config `[backtest] exchange`) |

#### `list-symbols` — List available symbols on an exchange

```bash
samtrader list-symbols --exchange <EXCHANGE> [-c config.ini]
```

Database connection is read from config file, or falls back to the `SAMTRADER_DB` environment variable.

#### `validate` — Validate a strategy file

```bash
samtrader validate -s <strategy.ini>
```

Parses the strategy file and prints all fields. No database connection required.

#### `info` — Show data range for a symbol

```bash
samtrader info --code <CODE> --exchange <EXCHANGE> [-c config.ini]
```

Prints the date range, total bars, and first/last close price. Database connection is read from config file, or falls back to `SAMTRADER_DB`.

### Exit Codes

| Code | Meaning |
|------|---------|
| 0 | Success |
| 1 | General error (unknown command, invalid arguments) |
| 2 | Configuration error (missing config, invalid values) |
| 3 | Database error (connection failure, query failure) |
| 4 | Invalid strategy (parse error, missing required rules) |
| 5 | Insufficient data (fewer than 30 OHLCV bars) |

### Environment Variables

| Variable | Description |
|----------|-------------|
| `SAMTRADER_DB` | PostgreSQL connection string fallback for `list-symbols` and `info` commands when no `-c` config is provided |

## Configuration File Format

Configuration files use INI format.

### `[database]` section

| Key | Type | Default | Description |
|-----|------|---------|-------------|
| `conninfo` | string | *(required)* | PostgreSQL connection string |

### `[backtest]` section

| Key | Type | Default | Description |
|-----|------|---------|-------------|
| `initial_capital` | double | 100000.0 | Starting portfolio value |
| `commission_per_trade` | double | 0.0 | Flat commission per trade |
| `commission_pct` | double | 0.0 | Commission as percentage of trade value |
| `slippage_pct` | double | 0.0 | Simulated slippage percentage |
| `allow_shorting` | bool | false | Enable short selling |
| `risk_free_rate` | double | 0.05 | Risk-free rate for Sharpe/Sortino ratios |
| `start_date` | string | *(required)* | Backtest start date (YYYY-MM-DD) |
| `end_date` | string | *(required)* | Backtest end date (YYYY-MM-DD) |
| `code` | string | *(required if codes not set)* | Symbol code (e.g., `CBA`) |
| `codes` | string | *(optional)* | Comma-separated codes for multi-code backtest |
| `exchange` | string | *(required)* | Exchange name (e.g., `ASX`) |

Boolean values accept: `true`/`false`, `yes`/`no`, `1`/`0` (case-insensitive).

### `[strategy]` section

| Key | Type | Default | Description |
|-----|------|---------|-------------|
| `name` | string | `"Unnamed Strategy"` | Strategy name |
| `description` | string | `""` | Strategy description |
| `entry_long` | rule | *(required)* | Rule expression for long entry |
| `exit_long` | rule | *(required)* | Rule expression for long exit |
| `entry_short` | rule | *(optional)* | Rule expression for short entry |
| `exit_short` | rule | *(optional)* | Rule expression for short exit |
| `position_size` | double | 0.25 | Fraction of portfolio per position (0.0–1.0) |
| `stop_loss` | double | 0.0 | Stop loss percentage (0 = disabled) |
| `take_profit` | double | 0.0 | Take profit percentage (0 = disabled) |
| `max_positions` | int | 1 | Maximum concurrent positions |

The strategy section can live in the main config file or in a separate file loaded with `-s`.

### `[report]` section

| Key | Type | Default | Description |
|-----|------|---------|-------------|
| `template_path` | string | *(optional)* | Custom Typst template path |

## Multi-Code Backtesting

Run the same strategy across multiple instruments in a single portfolio.

### Configuration

Use the `codes` field instead of `code` in the `[backtest]` section:

```ini
[backtest]
codes = CBA, BHP, WBC, NAB
exchange = ASX
```

### Code Resolution Priority

1. `--code` CLI flag (overrides everything, single code only)
2. `codes` config field (comma-separated, multi-code)
3. `code` config field (single code, legacy)

If both `codes` and `code` are present in the config, `codes` takes precedence (with a warning).

### Behavior

- Each code in the universe is validated against the database for sufficient data (minimum 30 bars).
- The same strategy rules are evaluated independently per code using each code's own OHLCV and indicator data.
- `max_positions` is enforced globally across all codes — set it to the number of codes for one position per instrument.
- Position sizing uses the portfolio-level `position_size` fraction.

### Report Output

Multi-code backtests generate an extended report containing:

- **Universe Summary** — table with per-code trade counts, win rates, and PnL
- **Per-code detail** sections — individual trade statistics for each instrument
- **Full trade log** — all trades across all codes

### Console Output

A per-code breakdown table is printed to stdout:

```
=== Per-Code Breakdown ===
Code       Trades   Wins Losses    TotalPnL WinRate%  LargestWin LargestLoss
CBA            12      7      5     4523.50   58.33%     2150.00    -1200.00
BHP             8      5      3     3210.00   62.50%     1800.00     -950.00
```

## Rule Grammar

Rules are composable text expressions. All rule and indicator names are **case-sensitive**.

### Comparison Rules

Compare two operands:

| Rule | Syntax | Description |
|------|--------|-------------|
| `CROSS_ABOVE` | `CROSS_ABOVE(left, right)` | Left crosses above right (requires 2+ bars) |
| `CROSS_BELOW` | `CROSS_BELOW(left, right)` | Left crosses below right (requires 2+ bars) |
| `ABOVE` | `ABOVE(left, right)` | Left > right |
| `BELOW` | `BELOW(left, right)` | Left < right |
| `EQUALS` | `EQUALS(left, right)` | Left == right (within tolerance) |
| `BETWEEN` | `BETWEEN(operand, lower, upper)` | lower <= operand <= upper |

### Composite Rules

Combine child rules:

| Rule | Syntax | Description |
|------|--------|-------------|
| `AND` | `AND(rule1, rule2, ...)` | All children must be true |
| `OR` | `OR(rule1, rule2, ...)` | At least one child must be true |
| `NOT` | `NOT(rule)` | Negation of child rule |

### Temporal Rules

Add time constraints:

| Rule | Syntax | Description |
|------|--------|-------------|
| `CONSECUTIVE` | `CONSECUTIVE(rule, N)` | Rule must be true for N consecutive bars |
| `ANY_OF` | `ANY_OF(rule, N)` | Rule must be true at least once in last N bars |

### Operands

**Price fields** (lowercase keywords):

| Operand | Description |
|---------|-------------|
| `close` | Close price |
| `open` | Open price |
| `high` | High price |
| `low` | Low price |
| `volume` | Trading volume |

**Numeric constants**: Any integer or floating-point number (e.g., `50`, `100.5`, `-3.14`).

### Examples

```ini
# SMA crossover: buy when short SMA crosses above long SMA
entry_long = CROSS_ABOVE(SMA(20), SMA(50))

# Exit when SMA crosses back
exit_long = CROSS_BELOW(SMA(20), SMA(50))

# RSI oversold with trend filter
entry_long = AND(BELOW(RSI(14), 30), ABOVE(close, SMA(200)))

# Close above upper Bollinger band
entry_long = ABOVE(close, BOLLINGER_UPPER(20, 2.0))

# RSI in neutral zone
entry_long = BETWEEN(RSI(14), 40, 60)

# Consecutive days above SMA
entry_long = CONSECUTIVE(ABOVE(close, SMA(50)), 5)

# Any crossover in last 10 bars
entry_long = ANY_OF(CROSS_ABOVE(SMA(20), SMA(50)), 10)

# Composite with negation
entry_long = AND(CROSS_ABOVE(SMA(10), SMA(30)), NOT(ABOVE(RSI(14), 70)))
```

## Supported Indicators

### Single-Parameter Indicators

Syntax: `INDICATOR(period)`

| Indicator | Description | Typical Period |
|-----------|-------------|----------------|
| `SMA(n)` | Simple Moving Average | 20, 50, 200 |
| `EMA(n)` | Exponential Moving Average | 12, 26, 50 |
| `RSI(n)` | Relative Strength Index (0–100) | 14 |
| `ATR(n)` | Average True Range | 14 |

### Multi-Parameter Indicators

| Indicator | Syntax | Parameters |
|-----------|--------|------------|
| `MACD(fast, slow, signal)` | `MACD(12, 26, 9)` | fast EMA period, slow EMA period, signal line period |
| `BOLLINGER_UPPER(period, stddev)` | `BOLLINGER_UPPER(20, 2.0)` | MA period, standard deviation multiplier |
| `BOLLINGER_MIDDLE(period, stddev)` | `BOLLINGER_MIDDLE(20, 2.0)` | MA period, standard deviation multiplier |
| `BOLLINGER_LOWER(period, stddev)` | `BOLLINGER_LOWER(20, 2.0)` | MA period, standard deviation multiplier |

### Pivot Indicators

No parameters — calculated from previous bar's high, low, close.

| Indicator | Description |
|-----------|-------------|
| `PIVOT` | Pivot point: (H + L + C) / 3 |
| `PIVOT_R1` | Resistance 1: (2 × Pivot) - L |
| `PIVOT_R2` | Resistance 2: Pivot + (H - L) |
| `PIVOT_R3` | Resistance 3: H + 2 × (Pivot - L) |
| `PIVOT_S1` | Support 1: (2 × Pivot) - H |
| `PIVOT_S2` | Support 2: Pivot - (H - L) |
| `PIVOT_S3` | Support 3: L - 2 × (H - Pivot) |

## Example Strategies

See the `examples/` directory for complete strategy files:

- **`config.ini`** — Full configuration file with all options
- **`multi_code.ini`** — Multi-code portfolio backtest (4 ASX stocks)
- **`sma_crossover.ini`** — Simple SMA crossover (beginner)
- **`rsi_mean_reversion.ini`** — RSI mean reversion
- **`golden_cross_filtered.ini`** — Golden cross with RSI filter (composite rules)
- **`bollinger_bands.ini`** — Bollinger Bands breakout
- **`momentum.ini`** — Temporal rules example (CONSECUTIVE/ANY_OF)
