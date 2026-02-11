# Samtrader Rust Implementation TRD

A Rust-specific technical requirements document for reimplementing samtrader as
a standalone project. Consolidates Phase 1 (core backtesting), Phase 2
(multi-code/universe support), and all code review findings.

---

## 1. Overview

### 1.1 Purpose

Samtrader is an algorithmic trading backtester. It enables users to:

- Define trading rules using text-based function composition
- Backtest strategies against 20+ years of daily OHLCV data from US and
  Australian markets
- Run strategies across multiple instruments with a shared portfolio
- Generate professional PDF reports via Typst markup
- Evaluate strategy performance with comprehensive metrics

### 1.2 Design Philosophy

- **Hexagonal Architecture**: Clean separation between domain logic, ports
  (traits), and adapters (implementations). Domain code has zero knowledge
  of I/O mechanisms.
- **Library Core as First-Class Target**: The domain logic, ports, and adapters
  are packaged as a library (`[lib]` target). The CLI is a thin `[[bin]]`
  consumer of this library.
  *(Review fix #4: no library target in original)*
- **Ownership-Driven Safety**: Rust ownership and borrowing replace arena
  allocation. No explicit `free()` or `close()` — `Drop` handles cleanup,
  `Result<T, E>` replaces null-return error handling, and the type system
  prevents null pointer dereferences at compile time.
- **Function Composition**: Rules are composable predicates defined as text in
  configuration files. No code changes needed to express new strategies.
- **Minimal Dependencies**: Only the default data adapter (PostgreSQL) requires
  an external library, and it is behind a Cargo feature flag (`postgres`). A
  CSV adapter is available for standalone use with zero non-Rust dependencies.

### 1.3 Non-Goals

- Real-time trading or market data feeds
- Live order execution
- Data ingestion/ETL pipelines
- GUI or web interface
- Cross-exchange backtesting in a single run
- Code-specific strategy rules (same strategy applies to all codes)
- Correlation-based position sizing or portfolio optimisation
- Multi-timeframe analysis

---

## 2. Architecture

### 2.1 Project Structure

```
samtrader/
├── Cargo.toml                    # Single crate: lib + bin targets
├── src/
│   ├── lib.rs                    # Library root — re-exports public API
│   ├── main.rs                   # CLI binary (thin consumer of lib)
│   ├── domain/
│   │   ├── mod.rs
│   │   ├── ohlcv.rs              # Price data structures
│   │   ├── position.rs           # Open position tracking
│   │   ├── portfolio.rs          # Portfolio state management
│   │   ├── execution.rs          # Trade entry/exit logic
│   │   ├── indicator/            # One file per indicator (sma.rs, ema.rs, etc.)
│   │   │   └── mod.rs            # IndicatorValue enum, IndicatorSeries, dispatch
│   │   ├── indicator_helpers.rs  # Shared indicator pre-computation logic
│   │   ├── rule.rs               # Rule AST structures
│   │   ├── rule_parser.rs        # Text → AST parsing
│   │   ├── rule_eval.rs          # Rule evaluation against bars
│   │   ├── backtest.rs           # Backtest loop as a callable domain function
│   │   ├── metrics.rs            # Performance metrics computation
│   │   ├── strategy.rs           # Strategy definition
│   │   ├── universe.rs           # Multi-code parsing & validation
│   │   ├── code_data.rs          # Per-code data container
│   │   └── error.rs              # thiserror-derived error types
│   ├── ports/                    # Trait definitions (abstract interfaces)
│   │   ├── mod.rs
│   │   ├── data_port.rs          # Data source trait
│   │   ├── config_port.rs        # Configuration trait
│   │   └── report_port.rs        # Report output trait
│   └── adapters/
│       ├── mod.rs
│       ├── postgres_adapter.rs   # PostgreSQL data source (feature-gated)
│       ├── csv_adapter.rs        # CSV file data source
│       ├── file_config_adapter.rs # INI file-based config
│       └── typst_report/         # Typst report output (split into submodules)
│           ├── mod.rs            # Port implementation, orchestration
│           ├── chart_svg.rs      # SVG equity curve and drawdown charts
│           ├── tables.rs         # Monthly returns, trade log, universe summary
│           └── default_template.rs # Built-in Typst report markup
└── tests/                        # Integration tests
```

**Decision: Single crate** with `[lib]` + `[[bin]]` targets in one `Cargo.toml`.
A workspace is a mechanical refactor later if needed — premature splitting adds
coordination overhead with no benefit at this scale.

### 2.2 Dependency Flow

```
                    ┌─────────────────┐
                    │   CLI (main.rs) │
                    └────────┬────────┘
                             │ depends on
                             ▼
                    ┌─────────────────┐
                    │  Library Core   │
                    │ (domain + ports │
                    │  + adapters)    │
                    └────────┬────────┘
                             │ implements
                             ▼
                    ┌─────────────────┐
                    │  Cargo crates   │
                    │ (chrono, clap,  │
                    │  postgres, ...) │
                    └─────────────────┘
```

**Key Principle**: Domain code has NO knowledge of adapters. All external
interactions go through port traits. Adapters depend on ports; ports depend
on nothing outside the domain.

#### 2.2.1 Cargo.toml Dependencies

| Crate | Purpose | Notes |
|---|---|---|
| `chrono` 0.4 | `NaiveDate` for daily-resolution dates | `default-features = false, features = ["std"]` |
| `clap` 4 | CLI parsing via `#[derive(Parser)]` | `features = ["derive"]` |
| `configparser` 3 | INI file parsing | Lightweight, no proc macros |
| `csv` 1 | CSV data adapter | Standard Rust CSV crate |
| `thiserror` 2 | Derive `Error` + `Display` for error types | Zero runtime cost |
| `postgres` 0.19 | PostgreSQL adapter | **Optional**, feature-gated |
| **Dev:** `approx` 0.5 | `assert_relative_eq!` for f64 tests | |
| **Dev:** `proptest` 1 | Property-based testing | |
| **Dev:** `tempfile` 3 | Temp files for integration tests | |

Feature flags in `Cargo.toml`:
```toml
[features]
default = ["postgres"]
postgres = ["dep:postgres"]
```

### 2.3 Key Design Decisions vs. Original

| Decision | Motivation | Review Finding |
|---|---|---|
| Library core as first-class target | Eliminates redundant source listings in build; enables reuse | #4 |
| Backtest loop as domain function | Testable without CLI scaffolding; reduces main to orchestration | #7 |
| Split typst_report_adapter into submodules | 48KB monolith → focused, reviewable modules | #3 |
| Shared indicator helpers module | Eliminates DRY violation between main and code_data | #1 |
| Dedicated error module | Error infrastructure belongs at root level, not hidden in universe | #2 |
| Parser error reporting | Users need position + expected-token info on parse failures | #12 |
| Break-even trade handling | Zero-PnL trades should not count as losses | #11 |
| Config validation | Catch invalid position_size, negative stop_loss, etc. before running | Review Priority 3 |
| All indicators implemented | ROC, STDDEV, OBV, VWAP must have calculation functions | #3.6 |
| CSV data adapter | Removes hard PostgreSQL dependency for simple use cases | Review Priority 3 |

### 2.4 Rust Idioms Reference

The original C implementation uses arena allocation, function pointer structs,
tagged unions, and custom data structure libraries. This table maps each
C pattern to its idiomatic Rust replacement:

| C Pattern | Rust Replacement |
|---|---|
| `Samrena *arena` everywhere | Owned data (`Vec<T>`, `String`, `Box<T>`) — RAII via `Drop` |
| `SamrenaVector *` | `Vec<T>` |
| `SamHashMap *` | `HashMap<K, V>` (from `std::collections`) |
| Function pointer structs (ports) | `trait` definitions |
| `void *impl` | Concrete adapter struct fields |
| `typedef enum` + flat struct (tagged union) | `enum Rule { Variant { fields }, ... }` — Rust enum variants |
| C tagged union | Rust enum with per-variant data |
| NULL return for errors | `Result<T, SamtraderError>` |
| `if (!ptr) return NULL` chains | `?` operator (early return on `Err`) |
| `goto cleanup` | RAII / `Drop` / `?` — no manual cleanup needed |
| Custom `ASSERT` macros | `assert!()`, `assert_eq!()`, `#[test]` |
| `time_t` | `chrono::NaiveDate` (daily resolution, no timezone ambiguity) |
| `const char *` | `&str` (borrowed params) or `String` (owned struct fields) |
| `snprintf(buf, ...)` | `format!()` or `impl Display` |
| `qsort(ptr, len, ...)` | `Vec::sort_by()` / `sort_unstable_by()` — safe, no UB |
| `gmtime_r` / `gmtime` | `chrono::NaiveDate` — inherently thread-safe, no global state |

---

## 3. Domain Model

All structures in this section are concrete Rust type definitions. Structs
derive `Debug, Clone` at minimum. Fields use `String` for owned text,
`NaiveDate` for dates, `f64` for prices/amounts, `i64` for quantities,
and `usize` for counts.

### 3.1 OHLCV (Price Bar)

```rust
#[derive(Debug, Clone)]
pub struct OhlcvBar {
    pub code: String,       // Stock symbol (e.g., "AAPL", "BHP")
    pub exchange: String,   // Exchange identifier (e.g., "US", "ASX")
    pub date: NaiveDate,    // Daily resolution
    pub open: f64,
    pub high: f64,
    pub low: f64,
    pub close: f64,
    pub volume: i64,
}

impl OhlcvBar {
    /// (high + low + close) / 3
    pub fn typical_price(&self) -> f64 {
        (self.high + self.low + self.close) / 3.0
    }

    /// max(high - low, |high - prev_close|, |low - prev_close|)
    pub fn true_range(&self, prev_close: f64) -> f64 {
        let hl = self.high - self.low;
        let hc = (self.high - prev_close).abs();
        let lc = (self.low - prev_close).abs();
        hl.max(hc).max(lc)
    }
}
```

### 3.2 Position

```rust
#[derive(Debug, Clone)]
pub struct Position {
    pub code: String,
    pub exchange: String,
    pub quantity: i64,        // Positive = long, negative = short
    pub entry_price: f64,
    pub entry_date: NaiveDate,
    pub stop_loss: f64,       // Trigger price; 0.0 = disabled
    pub take_profit: f64,     // Trigger price; 0.0 = disabled
}

impl Position {
    pub fn is_long(&self) -> bool { self.quantity > 0 }
    pub fn is_short(&self) -> bool { self.quantity < 0 }
    pub fn market_value(&self, price: f64) -> f64 { self.quantity.unsigned_abs() as f64 * price }
    pub fn unrealized_pnl(&self, price: f64) -> f64 { self.quantity as f64 * (price - self.entry_price) }

    pub fn should_stop_loss(&self, price: f64) -> bool {
        if self.stop_loss == 0.0 { return false; }
        if self.is_long() { price <= self.stop_loss } else { price >= self.stop_loss }
    }

    pub fn should_take_profit(&self, price: f64) -> bool {
        if self.take_profit == 0.0 { return false; }
        if self.is_long() { price >= self.take_profit } else { price <= self.take_profit }
    }
}
```

### 3.3 Closed Trade

```rust
#[derive(Debug, Clone)]
pub struct ClosedTrade {
    pub code: String,
    pub exchange: String,
    pub quantity: i64,        // Positive = long, negative = short
    pub entry_price: f64,
    pub exit_price: f64,
    pub entry_date: NaiveDate,
    pub exit_date: NaiveDate,
    pub pnl: f64,             // Realised profit/loss (after commissions)
}
```

### 3.4 Equity Point

```rust
#[derive(Debug, Clone)]
pub struct EquityPoint {
    pub date: NaiveDate,
    pub equity: f64,          // Total portfolio value at this date
}
```

### 3.5 Portfolio

```rust
use std::collections::HashMap;

#[derive(Debug, Clone)]
pub struct Portfolio {
    pub cash: f64,
    pub initial_capital: f64,
    pub positions: HashMap<String, Position>,   // Keyed by code
    pub closed_trades: Vec<ClosedTrade>,
    pub equity_curve: Vec<EquityPoint>,
}

impl Portfolio {
    pub fn add_position(&mut self, position: Position) {
        self.positions.insert(position.code.clone(), position);
    }

    pub fn get_position(&self, code: &str) -> Option<&Position> {
        self.positions.get(code)
    }

    pub fn has_position(&self, code: &str) -> bool {
        self.positions.contains_key(code)
    }

    /// Returns the removed position, or None if not found.
    pub fn remove_position(&mut self, code: &str) -> Option<Position> {
        self.positions.remove(code)
    }

    pub fn position_count(&self) -> usize {
        self.positions.len()
    }

    pub fn record_trade(&mut self, trade: ClosedTrade) {
        self.closed_trades.push(trade);
    }

    pub fn record_equity(&mut self, date: NaiveDate, equity: f64) {
        self.equity_curve.push(EquityPoint { date, equity });
    }

    pub fn total_equity(&self, price_map: &HashMap<String, f64>) -> f64 {
        let position_value: f64 = self.positions.values()
            .filter_map(|pos| price_map.get(&pos.code).map(|&price| pos.market_value(price)))
            .sum();
        self.cash + position_value
    }
}
```

### 3.6 Strategy

```rust
#[derive(Debug, Clone)]
pub struct Strategy {
    pub name: String,
    pub description: String,
    pub entry_long: Rule,              // Required
    pub exit_long: Rule,               // Required
    pub entry_short: Option<Rule>,     // None if long-only
    pub exit_short: Option<Rule>,      // None if long-only
    pub position_size: f64,            // Fraction of portfolio (0.0–1.0)
    pub stop_loss_pct: f64,            // Percentage below/above entry (0.0 = none)
    pub take_profit_pct: f64,          // Percentage above/below entry (0.0 = none)
    pub max_positions: usize,          // Maximum concurrent open positions
}
```

### 3.7 Backtest Configuration

```rust
#[derive(Debug, Clone)]
pub struct BacktestConfig {
    pub start_date: NaiveDate,
    pub end_date: NaiveDate,
    pub initial_capital: f64,
    pub commission_per_trade: f64,     // Flat fee per trade
    pub commission_pct: f64,           // Percentage of trade value
    pub slippage_pct: f64,             // Price slippage simulation
    pub allow_shorting: bool,
    pub risk_free_rate: f64,           // Annual rate (e.g. 0.05 for 5%)
}
```

---

## 4. Indicator System

### 4.1 Indicator Types

All indicators produce a time series aligned 1:1 with the input OHLCV bars.
Each point has a `valid` flag that is `false` during the warmup period.

| # | Category | Indicator | Parameters | Outputs | Warmup |
|---|---|---|---|---|---|
| 1 | Trend | SMA | period | value | period - 1 |
| 2 | Trend | EMA | period | value | period - 1 |
| 3 | Trend | WMA | period | value | period - 1 |
| 4 | Momentum | RSI | period | value | period |
| 5 | Momentum | MACD | fast, slow, signal | line, signal, histogram | max(fast,slow) - 1 + signal - 1 |
| 6 | Momentum | Stochastic | k_period, d_period | %K, %D | k_period - 1 + d_period - 1 |
| 7 | Momentum | ROC | period | value | period |
| 8 | Volatility | Bollinger Bands | period, stddev_mult | upper, middle, lower | period - 1 |
| 9 | Volatility | ATR | period | value | period - 1 |
| 10 | Volatility | STDDEV | period | value | period - 1 |
| 11 | Volume | OBV | (none) | value | 0 |
| 12 | Volume | VWAP | (none) | value | 0 |
| 13 | Support/Resistance | Pivot Points | (none) | pivot, R1-R3, S1-S3 | 1 |

**Note**: All 13 indicator types MUST have complete calculation functions.
ROC, STDDEV, OBV, and VWAP were declared but unimplemented in the original —
they are required in the reimplementation. *(Review fix #3.6)*

### 4.2 Indicator Value Types

Indicators use a Rust enum to handle varying output shapes:

```rust
#[derive(Debug, Clone)]
pub struct IndicatorPoint {
    pub date: NaiveDate,
    pub valid: bool,
    pub value: IndicatorValue,
}

#[derive(Debug, Clone)]
pub enum IndicatorValue {
    Simple(f64),
    Macd { line: f64, signal: f64, histogram: f64 },
    Stochastic { k: f64, d: f64 },
    Bollinger { upper: f64, middle: f64, lower: f64 },
    Pivot { pivot: f64, r1: f64, r2: f64, r3: f64, s1: f64, s2: f64, s3: f64 },
}
```

Single-value indicators (SMA, EMA, WMA, RSI, ROC, ATR, STDDEV, OBV, VWAP)
use `IndicatorValue::Simple`. Multi-output indicators use their respective
variants.

### 4.3 Indicator Type & Series

The `IndicatorType` enum encodes indicator identity + parameters. It derives
`Hash + Eq` and serves directly as a `HashMap` key — **replacing string key
generation** for lookups. String representations are only needed for
human-readable output (via `impl Display`).

```rust
#[derive(Debug, Clone, PartialEq, Eq, Hash)]
pub enum IndicatorType {
    Sma(usize),
    Ema(usize),
    Wma(usize),
    Rsi(usize),
    Roc(usize),
    Atr(usize),
    Stddev(usize),
    Obv,
    Vwap,
    Macd { fast: usize, slow: usize, signal: usize },
    Stochastic { k_period: usize, d_period: usize },
    Bollinger { period: usize, stddev_mult_x100: u32 },
    Pivot,
}

#[derive(Debug, Clone)]
pub struct IndicatorSeries {
    pub indicator_type: IndicatorType,
    pub values: Vec<IndicatorPoint>,
}
```

### 4.4 Calculation Specifications

All indicators use the **close** price unless noted.

#### 4.4.1 SMA — Simple Moving Average

```
SMA(n) = (C[i] + C[i-1] + ... + C[i-n+1]) / n

Warmup: first (n-1) bars are invalid.
Optimisation: use a running sum, subtract outgoing bar, add incoming bar.
```

#### 4.4.2 EMA — Exponential Moving Average

```
k = 2 / (n + 1)
EMA[n-1] = SMA(n)                      # Seed with first SMA
EMA[i]   = C[i] * k + EMA[i-1] * (1-k)   for i >= n

Warmup: first (n-1) bars are invalid.
```

#### 4.4.3 WMA — Weighted Moving Average

```
WMA(n) = (n*C[i] + (n-1)*C[i-1] + ... + 1*C[i-n+1]) / (n*(n+1)/2)

Warmup: first (n-1) bars are invalid.
Implementation note: A sliding-window approach (Diophantine technique) gives
O(bars) instead of O(bars * period). Maintain running weighted_sum and
simple_sum, update incrementally:
    weighted_sum += n * new_price - simple_sum
    simple_sum   += new_price - old_price
```
*(Review fix #10: O(n*period) → O(n))*

#### 4.4.4 RSI — Relative Strength Index

```
For each bar i > 0:
    gain[i] = max(C[i] - C[i-1], 0)
    loss[i] = max(C[i-1] - C[i], 0)

First average (simple mean):
    avg_gain = mean(gain[1..n])
    avg_loss = mean(loss[1..n])

Subsequent (Wilder's smoothing):
    avg_gain = (prev_avg_gain * (n-1) + gain[i]) / n
    avg_loss = (prev_avg_loss * (n-1) + loss[i]) / n

RSI = 100 - (100 / (1 + avg_gain / avg_loss))

If avg_loss == 0: RSI = 100
Warmup: first n bars are invalid.
```

#### 4.4.5 MACD — Moving Average Convergence Divergence

```
MACD Line    = EMA(fast) - EMA(slow)
Signal Line  = EMA(signal_period) of MACD Line
Histogram    = MACD Line - Signal Line

Default parameters: fast=12, slow=26, signal=9
Warmup: max(fast, slow) - 1 + signal - 1 bars.
```

#### 4.4.6 Stochastic Oscillator

```
For each bar i >= k_period - 1:
    lowest_low   = min(low[i-k+1 .. i])
    highest_high = max(high[i-k+1 .. i])
    %K[i] = 100 * (close[i] - lowest_low) / (highest_high - lowest_low)

%D = SMA(d_period) of %K

If highest_high == lowest_low: %K = 50 (convention)
Default parameters: k=14, d=3
Warmup: k_period - 1 + d_period - 1 bars.
Uses high and low prices for the lookback window, close for %K.
```

#### 4.4.7 ROC — Rate of Change

```
ROC(n)[i] = ((C[i] - C[i-n]) / C[i-n]) * 100

If C[i-n] == 0: ROC = 0
Warmup: first n bars are invalid.
```

#### 4.4.8 Bollinger Bands

```
Middle = SMA(period)
StdDev = sqrt(sum((C[i-j] - Middle)^2 for j in 0..period-1) / period)
Upper  = Middle + stddev_multiplier * StdDev
Lower  = Middle - stddev_multiplier * StdDev

Default: period=20, stddev_multiplier=2.0
Warmup: first (period-1) bars are invalid.
Note: uses population standard deviation (divides by N, not N-1).
```

#### 4.4.9 ATR — Average True Range

```
TR[0] = high[0] - low[0]
TR[i] = max(high[i] - low[i], |high[i] - close[i-1]|, |low[i] - close[i-1]|)

ATR[n-1] = mean(TR[0..n-1])                     # Simple average seed
ATR[i]   = (ATR[i-1] * (n-1) + TR[i]) / n       # Wilder's smoothing

Warmup: first (n-1) bars are invalid.
```

#### 4.4.10 STDDEV — Standard Deviation

```
STDDEV(n)[i] = sqrt(sum((C[i-j] - SMA(n)[i])^2 for j in 0..n-1) / n)

Population stddev (divides by N).
Warmup: first (n-1) bars are invalid.
```

#### 4.4.11 OBV — On-Balance Volume

```
OBV[0] = volume[0]
If close[i] > close[i-1]:  OBV[i] = OBV[i-1] + volume[i]
If close[i] < close[i-1]:  OBV[i] = OBV[i-1] - volume[i]
If close[i] == close[i-1]: OBV[i] = OBV[i-1]

No warmup; all bars are valid.
```

#### 4.4.12 VWAP — Volume-Weighted Average Price

```
VWAP[i] = cumulative_sum(typical_price[j] * volume[j], j=0..i)
         / cumulative_sum(volume[j], j=0..i)

where typical_price = (high + low + close) / 3

If cumulative volume == 0: VWAP = 0
No warmup; all bars are valid.
Note: This is a session-cumulative VWAP (resets daily in intraday contexts,
but for daily bars it accumulates from the start of the data).
```

#### 4.4.13 Pivot Points

```
Calculated from the PREVIOUS bar's high (H), low (L), close (C):

Pivot = (H + L + C) / 3
R1 = (2 * Pivot) - L          S1 = (2 * Pivot) - H
R2 = Pivot + (H - L)          S2 = Pivot - (H - L)
R3 = H + 2 * (Pivot - L)      S3 = L - 2 * (H - Pivot)

First bar is invalid (no previous data).
```

### 4.5 Indicator Caching

Indicators are pre-computed once per code before the backtest loop begins.
Indicator series are stored in a `HashMap<IndicatorType, IndicatorSeries>`.
Since `IndicatorType` derives `Hash + Eq`, it serves directly as the map key
— no string key generation is needed for lookups.

String key generation (via `impl Display for IndicatorType`) is only used for
human-readable output (logs, reports, debug formatting):

```
Display format examples:
    IndicatorType::Sma(20)                            → "SMA(20)"
    IndicatorType::Macd { fast: 12, slow: 26, .. }    → "MACD(12,26,9)"
    IndicatorType::Bollinger { period: 20, .. }        → "BOLLINGER(20,2.0)"
    IndicatorType::Pivot                               → "PIVOT"
```

The key generation is consistent between rule parsing and indicator
pre-computation (shared via the `indicator_helpers` module).
*(Review fix #1: eliminates DRY violation)*

---

## 5. Rule System

### 5.1 Concept

Rules are composable predicates that evaluate market conditions against OHLCV
data and pre-computed indicator values. They are defined as text in
configuration files and parsed into an AST for evaluation.

### 5.2 Rule Types

#### 5.2.1 Comparison Rules

| Rule | Semantics |
|---|---|
| `CROSS_ABOVE(left, right)` | `left[i] > right[i]` AND `left[i-1] <= right[i-1]` |
| `CROSS_BELOW(left, right)` | `left[i] < right[i]` AND `left[i-1] >= right[i-1]` |
| `ABOVE(left, right)` | `left[i] > right[i]` |
| `BELOW(left, right)` | `left[i] < right[i]` |
| `BETWEEN(operand, lower, upper)` | `lower <= operand[i] <= upper` |
| `EQUALS(left, right)` | `|left[i] - right[i]| < epsilon` |

`CROSS_ABOVE` and `CROSS_BELOW` require `index >= 1` (need previous bar).
Returns `false` at index 0.

#### 5.2.2 Composite Rules

| Rule | Semantics |
|---|---|
| `AND(rule1, rule2, ...)` | All children must evaluate to true. Variadic (2+). |
| `OR(rule1, rule2, ...)` | At least one child must evaluate to true. Variadic (2+). |
| `NOT(rule)` | Negation of child rule. |

#### 5.2.3 Temporal Rules

| Rule | Semantics |
|---|---|
| `CONSECUTIVE(rule, N)` | Child must be true for N consecutive bars ending at current bar. |
| `ANY_OF(rule, N)` | Child must be true at least once in the last N bars. |

### 5.3 Grammar (BNF)

```bnf
<rule>       ::= <comparison> | <composite> | <temporal>

<comparison> ::= "CROSS_ABOVE(" <operand> "," <operand> ")"
              |  "CROSS_BELOW(" <operand> "," <operand> ")"
              |  "ABOVE(" <operand> "," <operand> ")"
              |  "BELOW(" <operand> "," <operand> ")"
              |  "BETWEEN(" <operand> "," <number> "," <number> ")"
              |  "EQUALS(" <operand> "," <operand> ")"

<composite>  ::= "AND(" <rule> "," <rule> {"," <rule>} ")"
              |  "OR(" <rule> "," <rule> {"," <rule>} ")"
              |  "NOT(" <rule> ")"

<temporal>   ::= "CONSECUTIVE(" <rule> "," <integer> ")"
              |  "ANY_OF(" <rule> "," <integer> ")"

<operand>    ::= <price_field> | <indicator> | <number>

<price_field> ::= "open" | "high" | "low" | "close" | "volume"

<indicator>  ::= "SMA(" <integer> ")"
              |  "EMA(" <integer> ")"
              |  "WMA(" <integer> ")"
              |  "RSI(" <integer> ")"
              |  "ROC(" <integer> ")"
              |  "ATR(" <integer> ")"
              |  "STDDEV(" <integer> ")"
              |  "OBV"
              |  "VWAP"
              |  "MACD(" <integer> "," <integer> "," <integer> ")"
              |  "STOCHASTIC(" <integer> "," <integer> ")"
              |  "BOLLINGER_UPPER(" <integer> "," <number> ")"
              |  "BOLLINGER_MIDDLE(" <integer> "," <number> ")"
              |  "BOLLINGER_LOWER(" <integer> "," <number> ")"
              |  "PIVOT" | "PIVOT_R1" | "PIVOT_R2" | "PIVOT_R3"
              |  "PIVOT_S1" | "PIVOT_S2" | "PIVOT_S3"

<number>     ::= <integer> | <float>
<integer>    ::= [0-9]+
<float>      ::= [0-9]+ "." [0-9]+
```

Whitespace is allowed between tokens. Parsing is case-sensitive for keywords.

### 5.4 AST Structure

```rust
#[derive(Debug, Clone)]
pub enum Operand {
    Open,
    High,
    Low,
    Close,
    Volume,
    Constant(f64),
    Indicator(IndicatorRef),
}

#[derive(Debug, Clone)]
pub struct IndicatorRef {
    pub indicator_type: IndicatorType,
    pub field: IndicatorField,
}

#[derive(Debug, Clone)]
pub enum IndicatorField {
    /// For single-value indicators (SMA, EMA, WMA, RSI, ROC, ATR, STDDEV, OBV, VWAP)
    Value,
    /// MACD fields
    MacdLine,
    MacdSignal,
    MacdHistogram,
    /// Stochastic fields
    StochasticK,
    StochasticD,
    /// Bollinger Bands fields
    BollingerUpper,
    BollingerMiddle,
    BollingerLower,
    /// Pivot Point fields
    Pivot,
    R1, R2, R3,
    S1, S2, S3,
}

#[derive(Debug, Clone)]
pub enum Rule {
    CrossAbove { left: Operand, right: Operand },
    CrossBelow { left: Operand, right: Operand },
    Above { left: Operand, right: Operand },
    Below { left: Operand, right: Operand },
    Between { operand: Operand, lower: f64, upper: f64 },
    Equals { left: Operand, right: Operand },
    And(Vec<Rule>),
    Or(Vec<Rule>),
    Not(Box<Rule>),
    Consecutive { rule: Box<Rule>, count: usize },
    AnyOf { rule: Box<Rule>, count: usize },
}
```

Note: `Box<Rule>` for recursive variants (`Not`, `Consecutive`, `AnyOf`),
`Vec<Rule>` for variadic composites (`And`, `Or`).

### 5.5 Parser Requirements

The parser is a recursive descent parser. It MUST provide meaningful error
messages on failure, including: *(Review fix #12)*

1. **Character offset** where the error was detected
2. **What was expected** (e.g., "expected ')' after operand")
3. **What was found** (the actual token or character)

Return type: `Result<Rule, ParseError>`

```rust
#[derive(Debug, Clone, thiserror::Error)]
#[error("parse error at position {position}: {message}")]
pub struct ParseError {
    pub message: String,
    pub position: usize,
}

impl ParseError {
    /// Format with a caret pointing to the error position in the input.
    pub fn display_with_context(&self, input: &str) -> String {
        format!(
            "Error at position {}: {}\n  {}\n  {}^",
            self.position, self.message, input, " ".repeat(self.position)
        )
    }
}
```

Example error output:
```
Error at position 24: expected ')' after operand, found ','
  CROSS_ABOVE(SMA(20), , SMA(50))
                        ^
```

The parser MUST:
- Reject trailing input after a valid rule (e.g., `ABOVE(close, 50) garbage`)
- Handle arbitrary whitespace between tokens
- Support nested rules to arbitrary depth

### 5.6 Rule Evaluation

```rust
pub fn evaluate(
    rule: &Rule,
    ohlcv: &[OhlcvBar],
    indicators: &HashMap<IndicatorType, IndicatorSeries>,
    bar_index: usize,
) -> bool
```

The evaluator:
1. Resolves operands to scalar values from the OHLCV bar or indicator series
   at the given index
2. Evaluates the rule predicate
3. For composite rules, evaluates children (short-circuit: AND stops at first
   false, OR stops at first true)
4. For temporal rules, evaluates the child at prior indices within the
   lookback window

---

## 6. Strategy Definition & Configuration

### 6.1 Configuration Format (INI)

The configuration uses INI format with the following sections:

```ini
[database]
conninfo = host=localhost dbname=marketdata user=trader password=secret

[backtest]
initial_capital = 100000.0
commission_per_trade = 0.0
commission_pct = 0.0
slippage_pct = 0.0
allow_shorting = false
risk_free_rate = 0.05
start_date = 2020-01-01
end_date = 2024-12-31
code = CBA                         ; Single-code (legacy, still supported)
; codes = CBA,BHP,WBC,NAB          ; Multi-code (comma-separated)
exchange = ASX

[strategy]
name = SMA Crossover
description = Simple moving average crossover strategy
entry_long = CROSS_ABOVE(SMA(20), SMA(50))
exit_long = CROSS_BELOW(SMA(20), SMA(50))
; entry_short = ...                 ; Optional (requires allow_shorting = true)
; exit_short = ...                  ; Optional
position_size = 0.25               ; Fraction of portfolio per position
stop_loss = 0.0                    ; Percentage (0 = disabled)
take_profit = 0.0                  ; Percentage (0 = disabled)
max_positions = 1

[report]
; template_path = /path/to/custom_template.typ  ; Optional
```

### 6.2 Configuration Validation *(Review fix: config validation)*

The following validations MUST be applied before running a backtest:

| Field | Validation | Error |
|---|---|---|
| `conninfo` | Non-empty string | "database connection string is required" |
| `start_date` | Valid YYYY-MM-DD, before `end_date` | "invalid start_date" |
| `end_date` | Valid YYYY-MM-DD, after `start_date` | "invalid end_date" |
| `initial_capital` | > 0 | "initial_capital must be positive" |
| `commission_per_trade` | >= 0 | "commission_per_trade must be non-negative" |
| `commission_pct` | >= 0 | "commission_pct must be non-negative" |
| `slippage_pct` | >= 0 | "slippage_pct must be non-negative" |
| `risk_free_rate` | >= 0 and < 1 | "risk_free_rate must be between 0 and 1" |
| `position_size` | > 0 and <= 1.0 | "position_size must be between 0 and 1" |
| `stop_loss` | >= 0 | "stop_loss must be non-negative" |
| `take_profit` | >= 0 | "take_profit must be non-negative" |
| `max_positions` | >= 1 | "max_positions must be at least 1" |
| `entry_long` | Non-empty, valid rule | "entry_long rule is required" |
| `exit_long` | Non-empty, valid rule | "exit_long rule is required" |
| `code` or `codes` | At least one code specified | "no codes specified" |
| `exchange` | Non-empty | "exchange is required" |

Boolean fields (`allow_shorting`) accept: `true`, `false`, `yes`, `no`, `1`,
`0` (case-insensitive).

### 6.3 Code Resolution Priority

| Priority | Source | Behavior |
|---|---|---|
| 1 (highest) | CLI `--code` flag | Single-code backtest, overrides all config |
| 2 | Config `codes` key | Multi-code backtest (comma-separated) |
| 3 | Config `code` key | Single-code backtest (legacy) |

If both `code` and `codes` are present in config, `codes` takes precedence
and a warning is printed. If neither is present and no `--code` flag is
given, it is a configuration error.

---

## 7. Universe Module

### 7.1 Purpose

The universe module parses the code list from configuration and validates that
each code has sufficient data for backtesting.

### 7.2 Data Structure

```rust
#[derive(Debug, Clone)]
pub struct Universe {
    pub codes: Vec<String>,    // Validated code symbols
    pub exchange: String,      // Shared exchange identifier
}

impl Universe {
    pub fn count(&self) -> usize { self.codes.len() }
}
```

### 7.3 Parsing Specification

```
Input:   "  CBA , BHP ,WBC,  NAB  "
Output:  ["CBA", "BHP", "WBC", "NAB"]
```

Rules:
- Split on commas
- Trim leading/trailing whitespace from each token
- Convert to uppercase
- Reject empty tokens (e.g., `"CBA,,BHP"` → error)
- Reject duplicate codes (e.g., `"CBA,BHP,CBA"` → error)
- No limit on number of codes

### 7.4 Validation

For each code in the universe:
1. Fetch OHLCV data for the code, exchange, and date range from the data port
2. If the fetch returns an empty `Vec` or fewer than `MIN_OHLCV_BARS` (30)
   bars: print a warning to stderr and remove the code from the universe
3. If **all** codes fail validation: return error (exit code 5)
4. If **some** codes fail: continue with the valid subset and print a summary

```
Warning: skipping XYZ.ASX (only 12 bars, minimum 30 required)
Warning: skipping FOO.ASX (no data found)
Backtesting 8 of 10 codes on ASX
```

---

## 8. Backtest Engine

### 8.1 Library Function Signature *(Review fix #7)*

The backtest loop is a **domain function**, not inline in the CLI. This makes
it testable without CLI scaffolding and reusable by library consumers.

```rust
pub fn run_backtest(
    code_data: &[CodeData],
    timeline: &[NaiveDate],
    strategy: &Strategy,
    config: &BacktestConfig,
) -> BacktestResult
```

Where `CodeData` is:

```rust
#[derive(Debug, Clone)]
pub struct CodeData {
    pub code: String,
    pub exchange: String,
    pub ohlcv: Vec<OhlcvBar>,
    pub indicators: HashMap<IndicatorType, IndicatorSeries>,
    pub date_index: HashMap<NaiveDate, usize>,  // Date → bar-index lookup
}

impl CodeData {
    pub fn bar_count(&self) -> usize { self.ohlcv.len() }
}
```

Note: `date_index` is folded into `CodeData` (each code owns its own
date-to-bar-index mapping). `NaiveDate` is used directly as a `HashMap` key
(it implements `Hash + Eq`) — no string conversion needed.

### 8.2 Date Timeline

Different codes may have different trading days. The engine builds a unified
timeline:

1. Collect all unique dates across all codes' OHLCV data
2. Sort chronologically
3. For each date, only evaluate codes that have a bar on that date

Each `CodeData` maintains a `date_index: HashMap<NaiveDate, usize>` for O(1)
lookup during the backtest loop.

### 8.3 Backtest Loop (Pseudocode)

There is a single unified loop for both single-code and multi-code backtests.
A single-code backtest is simply a multi-code backtest with one code.

```
portfolio = Portfolio(initial_capital)

for each date in timeline:
    # Build price map from all codes with data on this date
    price_map = {}
    for each code_data in code_data_list:
        if date in code_data.date_index:
            bar = code_data.ohlcv[code_data.date_index[date]]
            price_map[code_data.code] = bar.close

    # Check stop-loss / take-profit triggers
    check_triggers(portfolio, price_map, date, config)

    # Per-code rule evaluation
    for each code_data in code_data_list:
        if date not in code_data.date_index:
            continue

        bar_index = code_data.date_index[date]

        # Exit evaluation
        if portfolio.has_position(code_data.code):
            if evaluate(strategy.exit_long, code_data.ohlcv,
                        code_data.indicators, bar_index):
                exit_position(portfolio, code_data.code, bar.close,
                              date, config)

        # Entry evaluation (respect global max_positions)
        if not portfolio.has_position(code_data.code):
            if portfolio.position_count() >= strategy.max_positions:
                continue
            if evaluate(strategy.entry_long, code_data.ohlcv,
                        code_data.indicators, bar_index):
                enter_long(portfolio, code_data.code, code_data.exchange,
                           bar.close, date, strategy, config)

    # Record equity snapshot
    equity = portfolio.total_equity(price_map)
    portfolio.record_equity(date, equity)

return compute_result(portfolio, config)
```

**Key properties**:
- `max_positions` is enforced **globally** across all codes
- Stop-loss / take-profit triggers are checked **before** rule evaluation
  on each date
- Exit rules are evaluated **before** entry rules for each code
- A single-code backtest follows the exact same path (universe of 1)

---

## 9. Position Management & Execution

### 9.1 Entering a Position

**Long entry**:
1. Apply slippage: `execution_price = market_price * (1 + slippage_pct / 100)`
2. Calculate available capital: `cash * position_size_fraction`
3. Calculate quantity: `floor(available_capital / execution_price)` (whole shares only)
4. If quantity == 0: no trade (insufficient capital)
5. Calculate cost: `quantity * execution_price`
6. Calculate commission: `flat_fee + (cost * commission_pct / 100)`
7. Deduct `cost + commission` from portfolio cash
8. Calculate stop-loss price (if enabled):
   `entry_price * (1 - stop_loss_pct / 100)`
9. Calculate take-profit price (if enabled):
   `entry_price * (1 + take_profit_pct / 100)`
10. Add position to portfolio

**Short entry** (when `allow_shorting = true`):
1. Apply slippage: `execution_price = market_price * (1 - slippage_pct / 100)`
2. Steps 2-7 as above (with negative quantity)
3. Stop-loss price: `entry_price * (1 + stop_loss_pct / 100)` (above entry)
4. Take-profit price: `entry_price * (1 - take_profit_pct / 100)` (below entry)
5. Add position to portfolio

### 9.2 Exiting a Position

1. Determine direction from position quantity sign
2. Apply slippage:
   - Long exit (sell): `execution_price = market_price * (1 - slippage_pct / 100)`
   - Short exit (buy to cover): `execution_price = market_price * (1 + slippage_pct / 100)`
3. Calculate exit value: `|quantity| * execution_price`
4. Calculate commission: `flat_fee + (exit_value * commission_pct / 100)`
5. Calculate PnL: `quantity * (exit_price - entry_price) - entry_commission - exit_commission`
6. Add `exit_value - commission` to portfolio cash
7. Record closed trade
8. Remove position from portfolio

### 9.3 Stop-Loss / Take-Profit Trigger Checking

On each date, before rule evaluation:
1. For each open position, check against the current price from price_map
2. Two-pass approach: first collect triggered codes (cannot modify position map
   during iteration), then exit each triggered position
3. Returns the number of positions exited

### 9.4 Commission Calculation

```
commission = flat_fee + (trade_value * commission_pct / 100.0)
```

Both entry and exit commissions are included in the PnL calculation (round-trip
commissions).

---

## 10. Metrics

### 10.1 Portfolio-Level Metrics *(the `Metrics` struct)*

```rust
#[derive(Debug, Clone)]
pub struct Metrics {
    // Return metrics
    pub total_return: f64,             // (final - initial) / initial
    pub annualized_return: f64,        // (1 + total_return)^(252/trading_days) - 1

    // Risk metrics
    pub sharpe_ratio: f64,             // (mean_daily - rf_daily) / stddev_daily * sqrt(252)
    pub sortino_ratio: f64,            // (mean_daily - rf_daily) / downside_dev * sqrt(252)
    pub max_drawdown: f64,             // Largest peak-to-trough decline (fraction)
    pub max_drawdown_duration: f64,    // Days of longest drawdown period

    // Trade statistics
    pub total_trades: usize,
    pub winning_trades: usize,         // PnL > 0
    pub losing_trades: usize,          // PnL < 0
    pub break_even_trades: usize,      // PnL == 0 (review fix #11)
    pub win_rate: f64,                 // winning_trades / total_trades
    pub profit_factor: f64,            // sum(wins) / |sum(losses)|
    pub average_win: f64,              // mean PnL of winning trades
    pub average_loss: f64,             // mean PnL of losing trades (negative)
    pub largest_win: f64,
    pub largest_loss: f64,             // Most negative
    pub average_trade_duration: f64,   // Mean days between entry and exit
}
```

### 10.2 Metric Formulas

#### Total Return
```
total_return = (final_equity - initial_equity) / initial_equity
```

#### Annualised Return
```
annualized_return = (1 + total_return)^(252 / trading_days) - 1
```
Where `trading_days = len(equity_curve) - 1`. Uses 252 trading days/year.

#### Daily Returns
```
daily_return[i] = (equity[i+1] - equity[i]) / equity[i]
```

#### Sharpe Ratio
```
rf_daily = risk_free_rate / 252
sharpe = (mean(daily_returns) - rf_daily) / stddev(daily_returns) * sqrt(252)
```
Uses population standard deviation.

#### Sortino Ratio
```
downside_returns = [max(0, rf_daily - r) for r in daily_returns]
downside_dev = sqrt(mean(r^2 for r in daily_returns where r < rf_daily))
sortino = (mean(daily_returns) - rf_daily) / downside_dev * sqrt(252)
```

#### Maximum Drawdown
```
For each point i:
    peak = max(equity[0..i])
    drawdown[i] = (peak - equity[i]) / peak
max_drawdown = max(drawdown)
```

#### Maximum Drawdown Duration
```
Number of trading days from a peak to recovery (next new peak).
If never recovered, count to end of data.
```

#### Win Rate
```
win_rate = winning_trades / total_trades
```
Where winning = PnL > 0, losing = PnL < 0, break-even = PnL == 0.
*(Review fix #11: break-even trades are neither wins nor losses)*

#### Profit Factor
```
profit_factor = sum(pnl for pnl > 0) / |sum(pnl for pnl < 0)|
If no losses: INFINITY (when wins > 0), else 0.
```

### 10.3 Per-Code Metrics

For multi-code backtests, per-code metrics are computed by filtering closed
trades by code:

```rust
#[derive(Debug, Clone)]
pub struct CodeResult {
    pub code: String,
    pub exchange: String,
    pub total_trades: usize,
    pub winning_trades: usize,
    pub losing_trades: usize,
    pub total_pnl: f64,
    pub win_rate: f64,
    pub largest_win: f64,
    pub largest_loss: f64,
}
```

---

## 11. Ports & Adapters

### 11.1 Data Port (Trait)

```rust
pub trait DataPort {
    fn fetch_ohlcv(
        &self,
        code: &str,
        exchange: &str,
        start_date: NaiveDate,
        end_date: NaiveDate,
    ) -> Result<Vec<OhlcvBar>, SamtraderError>;

    fn list_symbols(&self, exchange: &str) -> Result<Vec<String>, SamtraderError>;
}
```

No `close()` method — Rust `Drop` handles cleanup automatically.
No arena parameters — methods return owned data.

#### PostgreSQL Adapter (Default, Feature-Gated)

- Uses parameterised queries (`$1`, `$2`, etc.) — never string concatenation
  into SQL *(preserves SQL injection prevention)*
- Feature-gated: only compiled when `postgres` feature is enabled
  (`#[cfg(feature = "postgres")]`)
- Connection string format: `host=... dbname=... user=... password=...`

Database schema:
```sql
CREATE TABLE public.ohlcv (
    code     character varying NOT NULL,
    exchange character varying NOT NULL,
    date     timestamp with time zone NOT NULL,
    open     numeric NOT NULL,
    high     numeric NOT NULL,
    low      numeric NOT NULL,
    close    numeric NOT NULL,
    volume   integer NOT NULL
);
```

#### CSV Adapter *(New — Review fix Priority 3)*

- Implements the same `DataPort` trait
- Reads OHLCV data from CSV files
- File naming convention: `{code}_{exchange}.csv` or configurable
- Column format: `date,open,high,low,close,volume`
- Removes the hard PostgreSQL dependency for simple use cases

### 11.2 Config Port (Trait)

```rust
pub trait ConfigPort {
    fn get_string(&self, section: &str, key: &str) -> Option<String>;
    fn get_int(&self, section: &str, key: &str, default: i64) -> i64;
    fn get_double(&self, section: &str, key: &str, default: f64) -> f64;
    fn get_bool(&self, section: &str, key: &str, default: bool) -> bool;
}
```

No `close()` method — Rust `Drop` handles cleanup automatically.

#### File Config Adapter

Reads INI-format configuration files. See Section 6 for the full format
specification.

### 11.3 Report Port (Trait)

```rust
pub trait ReportPort {
    fn write(
        &self,
        result: &BacktestResult,
        strategy: &Strategy,
        output_path: &str,
    ) -> Result<(), SamtraderError>;

    /// Default implementation: falls back to `write` using only the aggregate result.
    fn write_multi(
        &self,
        result: &MultiCodeResult,
        strategy: &Strategy,
        output_path: &str,
    ) -> Result<(), SamtraderError> {
        self.write(&result.aggregate, strategy, output_path)
    }
}
```

No `close()` method — Rust `Drop` handles cleanup automatically.

#### Typst Report Adapter (Default)

Split into focused submodules: *(Review fix #3)*

| Submodule | Responsibility |
|---|---|
| `mod.rs` | Port implementation, placeholder resolution, orchestration |
| `chart_svg.rs` | Inline SVG equity curve and drawdown charts |
| `tables.rs` | Monthly returns heatmap, trade log, universe summary table |
| `default_template.rs` | Built-in Typst report markup with `{{PLACEHOLDER}}` substitution |

Custom templates are supported via `template_path` in `[report]` config.

---

## 12. Report Specification

### 12.1 Report Structure

```
1. Strategy Summary
   - Name, description
   - Entry/exit rules (as text)
   - Position sizing parameters
   - Date range, initial capital

2. Aggregate Performance Metrics
   - All metrics from Section 10.1 in a formatted table

3. Portfolio Equity Curve
   - Inline SVG line chart of equity over time

4. Drawdown Chart
   - Inline SVG area chart of drawdown percentage over time

5. Universe Summary Table (multi-code only)
   | Code | Trades | Win Rate | Total PnL | Largest Win | Largest Loss |

6. Per-Code Detail Sections (multi-code only, one per code)
   - Code-specific trade count, win rate, PnL
   - Code-specific trade log

7. Full Trade Log (all codes, sorted by date)
   | # | Code | Entry Date | Exit Date | Qty | Entry Price | Exit Price | PnL |

8. Monthly/Yearly Returns Table
   - Portfolio-level monthly returns in a heatmap grid
```

### 12.2 Chart Specifications

- Charts are rendered as inline SVG (no external dependencies)
- Equity curve: line chart, x-axis = dates, y-axis = portfolio value
- Drawdown chart: area chart (filled below zero line), y-axis = drawdown %
- Charts should include axis labels and gridlines

---

## 13. CLI Interface

### 13.1 Commands

```bash
# Run backtest
samtrader backtest -c config.ini [-s strategy.ini] [-o report.typ]
                   [--code CODE] [--exchange EXCHANGE]

# Run backtest in dry-run mode (no DB connection, no execution)
samtrader backtest -c config.ini --dry-run

# List available symbols
samtrader list-symbols --exchange US [-c config.ini]

# Validate strategy file
samtrader validate -s strategy.ini

# Show data range for symbol(s)
samtrader info --code CODE --exchange EXCHANGE [-c config.ini]
samtrader info -c config.ini   # Show info for all codes in config
```

### 13.2 Clap Derive Structure

```rust
use clap::{Parser, Subcommand};
use std::process::ExitCode;

#[derive(Parser)]
#[command(name = "samtrader", about = "Algorithmic trading backtester")]
pub struct Cli {
    #[command(subcommand)]
    pub command: Command,
}

#[derive(Subcommand)]
pub enum Command {
    /// Run a backtest
    Backtest {
        #[arg(short, long)]
        config: String,
        #[arg(short, long)]
        strategy: Option<String>,
        #[arg(short, long)]
        output: Option<String>,
        #[arg(long)]
        code: Option<String>,
        #[arg(long)]
        exchange: Option<String>,
        #[arg(long)]
        dry_run: bool,
    },
    /// List available symbols on an exchange
    ListSymbols {
        #[arg(long)]
        exchange: String,
        #[arg(short, long)]
        config: Option<String>,
    },
    /// Validate a strategy configuration
    Validate {
        #[arg(short, long)]
        strategy: String,
    },
    /// Show data range for symbol(s)
    Info {
        #[arg(long)]
        code: Option<String>,
        #[arg(long)]
        exchange: Option<String>,
        #[arg(short, long)]
        config: Option<String>,
    },
}

impl From<&SamtraderError> for ExitCode {
    fn from(err: &SamtraderError) -> ExitCode {
        let code = match err {
            SamtraderError::Config { .. } => 2,
            SamtraderError::Database { .. } => 3,
            SamtraderError::RuleParse(_) => 4,
            SamtraderError::InsufficientData { .. } => 5,
            _ => 1,
        };
        ExitCode::from(code)
    }
}
```

### 13.3 The `--dry-run` Flag *(Review fix Priority 3)*

When `--dry-run` is specified on the `backtest` command:

1. Parse and validate the configuration file
2. Parse and display the strategy rules (as parsed AST, pretty-printed)
3. Resolve the universe (list of codes)
4. List all indicators that would be computed
5. **Do NOT** connect to the database or run the backtest
6. Exit with code 0 if everything is valid

This enables debugging configuration and strategy issues without requiring a
database connection.

### 13.4 Exit Codes

| Code | Meaning |
|---|---|
| 0 | Success |
| 1 | General error (unknown command, invalid arguments) |
| 2 | Configuration error (missing config, invalid values) |
| 3 | Database error (connection failure, query failure) |
| 4 | Invalid strategy (parse error, missing rules) |
| 5 | Insufficient data (< 30 bars for all codes) |

### 13.5 Console Output (Multi-Code Backtest)

```
Loading strategy: Multi-Code SMA Crossover
Validating 10 codes on ASX...
  CBA: 1247 bars [OK]
  BHP: 1247 bars [OK]
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
  ...

Report written to: report.typ
```

All progress and summary output goes to **stderr**. Only machine-readable
output (if any) goes to stdout.

---

## 14. Error Handling

### 14.1 Error Types

```rust
use thiserror::Error;

#[derive(Debug, Error)]
pub enum SamtraderError {
    #[error("database connection failed: {reason}")]
    Database { reason: String },

    #[error("database query failed: {reason}")]
    DatabaseQuery { reason: String },

    #[error("config parse error in {file}: {reason}")]
    ConfigParse { file: String, reason: String },

    #[error("missing config key [{section}] {key}")]
    ConfigMissing { section: String, key: String },

    #[error("invalid config value [{section}] {key}: {reason}")]
    ConfigInvalid { section: String, key: String, reason: String },

    #[error(transparent)]
    RuleParse(#[from] ParseError),

    #[error("invalid rule: {reason}")]
    RuleInvalid { reason: String },

    #[error("no data found for {code}.{exchange}")]
    NoData { code: String, exchange: String },

    #[error("insufficient data for {code}.{exchange}: {bars} bars (minimum {minimum})")]
    InsufficientData { code: String, exchange: String, bars: usize, minimum: usize },

    #[error("I/O error: {0}")]
    Io(#[from] std::io::Error),
}
```

Removed from the C version:
- `NONE` → use `Result::Ok`
- `MEMORY` → Rust aborts on OOM; not an application-level concern
- `NULL_PARAM` → the type system prevents null arguments at compile time

All variants carry structured context fields (not just strings). The
`#[from]` attribute on `ParseError` and `std::io::Error` enables automatic
conversion via the `?` operator.

Error callback concept (from the C version) is removed entirely — errors
propagate via `Result<T, SamtraderError>` and `?`.

### 14.2 Error Reporting

Each error variant has a human-readable `Display` implementation generated by
`thiserror`. The error infrastructure is a **dedicated module** (`error.rs`),
not buried in another domain module. *(Review fix #2)*

### 14.3 Parser Error Messages *(Review fix #12)*

Rule parse errors MUST include:
1. Character position of the error
2. Description of what was expected
3. Description of what was found

The `ParseError` struct (Section 5.5) provides `display_with_context()` for
caret-style output. The CLI formats this for the user.

### 14.4 Multi-Code Error Behaviour

| Condition | Behaviour | Exit Code |
|---|---|---|
| Empty `codes` value | Error: "no codes specified" | 2 |
| Duplicate code in list | Error: "duplicate code: CBA" | 2 |
| Empty token in codes | Error: "empty code in codes list" | 2 |
| All codes fail validation | Error: "no valid codes after validation" | 5 |
| Some codes fail validation | Warning, continue with valid subset | 0 |
| DB fetch fails for one code | Warning, skip code | 0 |
| DB fetch fails for all codes | Error: "failed to fetch data for any code" | 3 |

---

## 15. Testing Strategy

### 15.1 Unit Tests

Unit tests live in `#[cfg(test)] mod tests` blocks within each source file.
Use `assert_relative_eq!` from the `approx` crate for f64 comparisons.

| Module | Test Focus |
|---|---|
| Indicators | Verify against hand-calculated values with `assert_relative_eq!` |
| Rule parser | Valid inputs, malformed inputs, whitespace handling, nesting depth, error messages |
| Rule evaluation | Edge cases (index 0 for crossover, warmup periods, all rule types) |
| Portfolio | Add/remove positions, equity calculation, trade recording |
| Position | Long/short detection, stop-loss/take-profit triggers, PnL calculation |
| Execution | Commission calculation, slippage application, quantity sizing |
| Metrics | Known portfolios with expected metric values, break-even trade handling |
| Universe | Parsing: whitespace, duplicates, empty tokens, single code |
| Config | INI parsing, type coercion, default values, validation rules |

### 15.2 Integration Tests

Integration tests live in the `tests/` directory. Mock adapters are plain
structs that implement the port traits — no mocking framework needed.

- Full backtest pipeline with mock data port (no database)
- Multi-code backtest with known trades — verify per-code PnL
- `max_positions` enforcement across codes
- Partial universe validation (some codes skipped, others proceed)
- Backward compatibility: single `code` config produces identical results
- Report generation: output contains expected sections

### 15.3 End-to-End Tests

- Full pipeline: config → DB fetch → backtest → report
- Multi-code: universe summary table and per-code sections in report
- CLI: exit codes, `--dry-run`, `--code` override, `validate` command

### 15.4 Property-Based Tests

Use `proptest!` macro from the `proptest` crate:

```rust
use proptest::prelude::*;

proptest! {
    #[test]
    fn rsi_is_bounded(values in prop::collection::vec(1.0..1000.0f64, 20..100)) {
        let bars = make_bars_from_closes(&values);
        let series = compute_rsi(&bars, 14);
        for point in &series.values {
            if point.valid {
                if let IndicatorValue::Simple(v) = point.value {
                    prop_assert!(v >= 0.0 && v <= 100.0);
                }
            }
        }
    }
}
```

Property invariants to test:
- Portfolio invariant: `cash + sum(position_market_values) == total_equity`
- Indicator bounds: RSI ∈ [0, 100], Stochastic %K ∈ [0, 100]
- Drawdown ∈ [0, 1]
- Win rate ∈ [0, 1]
- `winning_trades + losing_trades + break_even_trades == total_trades`
- Monotonicity: OBV changes by exactly `±volume[i]` or 0 per bar

---

## 16. Implementation Phases

Suggested build order for the Rust reimplementation:

### Phase 1: Core Infrastructure
1. `Cargo.toml` with `[lib]` + `[[bin]]` targets, module hierarchy, `lib.rs`
   re-exports
2. OHLCV data structures (`domain/ohlcv.rs`)
3. Error types with `thiserror` derives (`domain/error.rs`)
4. Config port trait + file config adapter (INI parsing via `configparser`)
5. Config validation
6. Basic unit tests
7. CI: `cargo build`, `cargo test`, `cargo clippy`, `cargo fmt --check`

### Phase 2: Indicators
1. Indicator value types (`IndicatorValue` enum, `IndicatorPoint`,
   `IndicatorType`, `IndicatorSeries`)
2. SMA, EMA, WMA (trend indicators)
3. RSI (momentum)
4. MACD, Stochastic (multi-output momentum)
5. Bollinger Bands, ATR, STDDEV (volatility)
6. OBV, VWAP (volume)
7. Pivot Points (support/resistance)
8. ROC (momentum)
9. Indicator caching with `HashMap<IndicatorType, IndicatorSeries>`
10. Indicator helpers (shared pre-computation logic)
11. Comprehensive indicator tests

### Phase 3: Rule System
1. Rule AST data structures (`Rule`, `Operand`, `IndicatorRef` enums)
2. Operand types and construction
3. Rule parser with `ParseError` error reporting
4. Comparison rule evaluation
5. Composite rule evaluation (AND, OR, NOT)
6. Temporal rule evaluation (CONSECUTIVE, ANY_OF)
7. Parser and evaluation tests

### Phase 4: Position & Portfolio Management
1. Position data structures and `impl` methods
2. Portfolio state management with `HashMap<String, Position>`
3. Trade execution (entry/exit)
4. Commission and slippage calculation
5. Stop-loss / take-profit trigger checking
6. Portfolio and execution tests

### Phase 5: Backtest Engine
1. Universe module (parsing + validation)
2. `CodeData` container with `date_index: HashMap<NaiveDate, usize>`
3. Date timeline builder
4. Backtest loop as library function (`run_backtest`)
5. Metrics computation (including break-even handling)
6. Per-code metrics
7. Backtest integration tests

### Phase 6: Data Adapters
1. `DataPort` trait definition
2. PostgreSQL adapter (feature-gated, parameterised queries)
3. CSV adapter
4. Data adapter tests

### Phase 7: Reporting
1. `ReportPort` trait with default `write_multi` implementation
2. Typst adapter: orchestration + placeholder resolution
3. SVG chart generation (equity curve, drawdown)
4. Monthly returns table
5. Trade log formatting
6. Multi-code sections (universe summary, per-code details)
7. Custom template support
8. Report tests

### Phase 8: CLI & Integration
1. `clap` derive structs (`Cli`, `Command` enum)
2. `backtest` command (orchestrate library calls)
3. `validate` command
4. `list-symbols` command
5. `info` command
6. `--dry-run` flag
7. `ExitCode` conversion from `SamtraderError`
8. End-to-end tests

---

## 17. Review Findings Traceability

Every review finding from the original code review is addressed in this TRD:

| # | Finding | Resolution | TRD Section |
|---|---|---|---|
| 1 | DRY violation: duplicated indicator helpers | Shared `indicator_helpers` module | 2.1, 4.5 |
| 2 | Error callback homeless in universe.c | Dedicated `error.rs` module with `thiserror` | 2.1, 14.1 |
| 3 | typst_report_adapter.c 48KB monolith | Split into 4 submodules | 2.1, 11.3 |
| 4 | No library target | `[lib]` + `[[bin]]` targets in `Cargo.toml` | 2.1, 2.3 |
| 5 | qsort on internal vector data | N/A — `Vec::sort_by()` is safe and idiomatic | N/A |
| 6 | gmtime() not thread-safe | N/A — `chrono::NaiveDate` is inherently thread-safe | N/A |
| 7 | main.c doing too much | Backtest loop extracted to domain function | 8.1 |
| 8 | Generated artifacts in git | .gitignore (implementation detail) | N/A (impl) |
| 9 | Credentials in git | .gitignore + env var support (implementation detail) | N/A (impl) |
| 10 | WMA O(n*period) complexity | Sliding window specification | 4.4.3 |
| 11 | Zero-PnL trades counted as losses | `break_even_trades` counter, excluded from win/loss | 10.1, 10.2 |
| 12 | No parser error messages | `ParseError` with position + context, `thiserror` derive | 5.5, 14.3 |
| P3.4 | No config validation | Validation table with rules | 6.2 |
| P3.5 | No --dry-run flag | `--dry-run` specification | 13.3 |
| P3.6 | ROC/STDDEV/OBV/VWAP unimplemented | All 13 indicators required with calculation specs | 4.1, 4.4.7–4.4.12 |
| P3.3 | No CSV data adapter | CSV adapter implementing `DataPort` trait | 11.1 |

---

## 18. Future Extensions (Out of Scope)

- Walk-forward optimisation
- Monte Carlo simulation
- Portfolio optimisation (multiple strategies)
- Live trading adapter
- Real-time data feeds
- Machine learning indicators
- Cross-exchange backtesting
- Per-code strategy overrides
- Sector/industry grouping
- Correlation-aware position sizing
- Parallel data fetching
- Code screening/filtering rules
