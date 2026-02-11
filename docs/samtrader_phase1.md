# Samtrader Technical Requirements Document

## 1. Overview

### 1.1 Purpose

Samtrader is an algorithmic trading backtesting application written in C. It enables users to:

- Define trading rules using text-based function composition
- Backtest strategies against 20+ years of OHLCV data from US and Australian markets
- Generate professional reports in Typst format
- Evaluate strategy performance with comprehensive metrics

### 1.2 Design Philosophy

- **Hexagonal Architecture**: Clean separation between domain logic, ports (interfaces), and adapters (implementations)
- **Function Composition**: Rules are composable functions that can be combined via text configuration
- **Arena-Based Memory**: All allocations through samrena for predictable memory management
- **Minimal Dependencies**: Only libpq as external dependency; samrena and samdata for internal data structures

### 1.3 Non-Goals (Current Phase)

- Real-time trading or market data feeds
- Live order execution
- Data ingestion/ETL pipelines
- GUI or web interface

---

## 2. Architecture

### 2.1 Hexagonal Structure

```
apps/samtrader/
├── CMakeLists.txt
├── include/
│   └── samtrader/
│       ├── samtrader.h           # Public API
│       ├── domain/               # Core business logic
│       │   ├── ohlcv.h           # Price data structures
│       │   ├── indicator.h       # Technical indicators
│       │   ├── signal.h          # Trading signals
│       │   ├── rule.h            # Rule definitions
│       │   ├── position.h        # Position tracking
│       │   ├── portfolio.h       # Portfolio state
│       │   └── backtest.h        # Backtesting engine
│       ├── ports/                # Interfaces (abstract)
│       │   ├── data_port.h       # Data source interface
│       │   ├── config_port.h     # Configuration interface
│       │   └── report_port.h     # Report output interface
│       └── adapters/             # Implementations
│           ├── postgres_adapter.h    # PostgreSQL data source
│           ├── file_config_adapter.h # File-based config
│           └── typst_report_adapter.h # Typst report output
├── src/
│   ├── domain/
│   │   ├── ohlcv.c
│   │   ├── indicator.c
│   │   ├── signal.c
│   │   ├── rule.c
│   │   ├── rule_parser.c         # Text rule parsing
│   │   ├── position.c
│   │   ├── portfolio.c
│   │   └── backtest.c
│   ├── adapters/
│   │   ├── postgres_adapter.c
│   │   ├── file_config_adapter.c
│   │   └── typst_report_adapter.c
│   └── main.c
└── test/
    ├── samtrader_test.c
    ├── test_indicators.c
    ├── test_rules.c
    ├── test_backtest.c
    └── test_rule_parser.c
```

### 2.2 Dependency Flow

```
                    ┌─────────────────┐
                    │     main.c      │
                    └────────┬────────┘
                             │
              ┌──────────────┼──────────────┐
              │              │              │
              ▼              ▼              ▼
       ┌──────────┐   ┌──────────┐   ┌──────────┐
       │ Adapters │   │  Domain  │   │  Ports   │
       └────┬─────┘   └──────────┘   └────┬─────┘
            │                              │
            └──────────────┬───────────────┘
                           │ implements
                           ▼
                    ┌──────────────┐
                    │   libpq      │
                    │  samrena     │
                    │  samdata     │
                    └──────────────┘
```

**Key Principle**: Domain code has NO knowledge of adapters. All external interactions go through ports.

---

## 3. Domain Model

### 3.1 Core Data Structures

#### 3.1.1 OHLCV (Price Data)

```c
typedef struct {
    const char *code;      // Stock symbol (e.g., "AAPL", "BHP")
    const char *exchange;  // Exchange identifier ("US", "AU")
    time_t date;           // Unix timestamp (daily resolution)
    double open;
    double high;
    double low;
    double close;
    int64_t volume;
} SamtraderOhlcv;
```

#### 3.1.2 Indicator Values

```c
typedef struct {
    time_t date;
    double value;
    bool valid;            // False during warmup period
} SamtraderIndicatorValue;

typedef struct {
    SamtraderIndicatorType type;
    int period;            // e.g., 20 for SMA(20)
    SamrenaVector *values; // Vector of SamtraderIndicatorValue
} SamtraderIndicatorSeries;
```

#### 3.1.3 Trading Signal

```c
typedef enum {
    SAMTRADER_SIGNAL_NONE = 0,
    SAMTRADER_SIGNAL_BUY,
    SAMTRADER_SIGNAL_SELL,
    SAMTRADER_SIGNAL_HOLD
} SamtraderSignalType;

typedef struct {
    time_t date;
    SamtraderSignalType type;
    double strength;       // 0.0 to 1.0, confidence level
    const char *reason;    // Human-readable explanation
} SamtraderSignal;
```

#### 3.1.4 Position

```c
typedef struct {
    const char *code;
    const char *exchange;
    int64_t quantity;      // Positive = long, negative = short
    double entry_price;
    time_t entry_date;
    double stop_loss;      // 0 if not set
    double take_profit;    // 0 if not set
} SamtraderPosition;
```

#### 3.1.5 Portfolio State

```c
typedef struct {
    double cash;
    SamHashMap *positions; // code -> SamtraderPosition
    SamrenaVector *closed_trades; // Historical trades
    double initial_capital;
} SamtraderPortfolio;
```

### 3.2 Technical Indicators

#### 3.2.1 Supported Indicators

| Category | Indicator | Parameters | Description |
|----------|-----------|------------|-------------|
| **Trend** | SMA | period | Simple Moving Average |
| | EMA | period | Exponential Moving Average |
| | WMA | period | Weighted Moving Average |
| **Momentum** | RSI | period | Relative Strength Index |
| | MACD | fast, slow, signal | Moving Average Convergence Divergence |
| | Stochastic | %K, %D periods | Stochastic Oscillator |
| | ROC | period | Rate of Change |
| **Volatility** | Bollinger | period, stddev | Bollinger Bands (upper, middle, lower) |
| | ATR | period | Average True Range |
| | Std Dev | period | Standard Deviation |
| **Volume** | OBV | - | On-Balance Volume |
| | VWAP | - | Volume-Weighted Average Price |
| **Support/Resistance** | Pivot Points | - | Classic pivot with R1-R3, S1-S3 |

#### 3.2.2 Indicator Interface

```c
typedef enum {
    SAMTRADER_IND_SMA,
    SAMTRADER_IND_EMA,
    SAMTRADER_IND_WMA,
    SAMTRADER_IND_RSI,
    SAMTRADER_IND_MACD,
    SAMTRADER_IND_STOCHASTIC,
    SAMTRADER_IND_ROC,
    SAMTRADER_IND_BOLLINGER_UPPER,
    SAMTRADER_IND_BOLLINGER_MIDDLE,
    SAMTRADER_IND_BOLLINGER_LOWER,
    SAMTRADER_IND_ATR,
    SAMTRADER_IND_STDDEV,
    SAMTRADER_IND_OBV,
    SAMTRADER_IND_VWAP,
    SAMTRADER_IND_PIVOT,
    SAMTRADER_IND_PIVOT_R1,
    SAMTRADER_IND_PIVOT_R2,
    SAMTRADER_IND_PIVOT_R3,
    SAMTRADER_IND_PIVOT_S1,
    SAMTRADER_IND_PIVOT_S2,
    SAMTRADER_IND_PIVOT_S3
} SamtraderIndicatorType;

// Calculate single indicator series from OHLCV data
SamtraderIndicatorSeries *samtrader_indicator_calculate(
    Samrena *arena,
    SamtraderIndicatorType type,
    SamrenaVector *ohlcv,  // Vector of SamtraderOhlcv
    int period
);

// Get indicator value at specific index
double samtrader_indicator_value_at(
    SamtraderIndicatorSeries *series,
    size_t index
);
```

---

## 4. Rule System & Function Composition

### 4.1 Rule Concept

Rules are composable predicates that evaluate market conditions and produce signals. They are defined via text configuration for flexibility.

### 4.2 Rule Types

#### 4.2.1 Comparison Rules

```
# Price crosses above indicator
CROSS_ABOVE(close, SMA(20))

# Price crosses below indicator
CROSS_BELOW(close, EMA(50))

# Indicator value comparison
ABOVE(RSI(14), 70)
BELOW(RSI(14), 30)
BETWEEN(RSI(14), 30, 70)
```

#### 4.2.2 Composite Rules

```
# Logical combinations
AND(ABOVE(close, SMA(200)), CROSS_ABOVE(SMA(20), SMA(50)))
OR(BELOW(RSI(14), 30), CROSS_ABOVE(close, BOLLINGER_LOWER(20, 2)))
NOT(ABOVE(RSI(14), 50))
```

#### 4.2.3 Temporal Rules

```
# Consecutive conditions
CONSECUTIVE(ABOVE(close, SMA(20)), 5)  # Above for 5 days

# Within lookback period
ANY_OF(CROSS_ABOVE(close, SMA(50)), 10)  # Crossed within last 10 days
```

### 4.3 Rule Grammar (BNF)

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
              |  "RSI(" <integer> ")"
              |  "MACD(" <integer> "," <integer> "," <integer> ")"
              |  "BOLLINGER_UPPER(" <integer> "," <number> ")"
              |  "BOLLINGER_MIDDLE(" <integer> "," <number> ")"
              |  "BOLLINGER_LOWER(" <integer> "," <number> ")"
              |  "ATR(" <integer> ")"
              |  "PIVOT" | "PIVOT_R1" | "PIVOT_R2" | "PIVOT_R3"
              |  "PIVOT_S1" | "PIVOT_S2" | "PIVOT_S3"

<number>     ::= <integer> | <float>
<integer>    ::= [0-9]+
<float>      ::= [0-9]+ "." [0-9]+
```

### 4.4 Rule Data Structures

```c
typedef enum {
    SAMTRADER_RULE_CROSS_ABOVE,
    SAMTRADER_RULE_CROSS_BELOW,
    SAMTRADER_RULE_ABOVE,
    SAMTRADER_RULE_BELOW,
    SAMTRADER_RULE_BETWEEN,
    SAMTRADER_RULE_EQUALS,
    SAMTRADER_RULE_AND,
    SAMTRADER_RULE_OR,
    SAMTRADER_RULE_NOT,
    SAMTRADER_RULE_CONSECUTIVE,
    SAMTRADER_RULE_ANY_OF
} SamtraderRuleType;

typedef enum {
    SAMTRADER_OPERAND_PRICE_OPEN,
    SAMTRADER_OPERAND_PRICE_HIGH,
    SAMTRADER_OPERAND_PRICE_LOW,
    SAMTRADER_OPERAND_PRICE_CLOSE,
    SAMTRADER_OPERAND_VOLUME,
    SAMTRADER_OPERAND_INDICATOR,
    SAMTRADER_OPERAND_CONSTANT
} SamtraderOperandType;

typedef struct SamtraderOperand {
    SamtraderOperandType type;
    union {
        double constant;
        struct {
            SamtraderIndicatorType indicator_type;
            int period;
            int param2;  // For multi-param indicators (e.g., MACD slow)
            int param3;  // For multi-param indicators (e.g., MACD signal)
        } indicator;
    };
} SamtraderOperand;

typedef struct SamtraderRule {
    SamtraderRuleType type;
    SamtraderOperand left;
    SamtraderOperand right;
    double threshold;      // For BETWEEN upper bound
    int lookback;          // For CONSECUTIVE, ANY_OF
    struct SamtraderRule **children;  // For AND, OR (NULL-terminated)
    struct SamtraderRule *child;      // For NOT
} SamtraderRule;

// Parse rule from text
SamtraderRule *samtrader_rule_parse(Samrena *arena, const char *text);

// Evaluate rule at index
bool samtrader_rule_evaluate(
    SamtraderRule *rule,
    SamrenaVector *ohlcv,
    SamHashMap *indicators,  // indicator_key -> SamtraderIndicatorSeries
    size_t index
);
```

---

## 5. Strategy Definition

### 5.1 Strategy Structure

```c
typedef struct {
    const char *name;
    const char *description;

    SamtraderRule *entry_long;    // When to buy
    SamtraderRule *exit_long;     // When to sell long position
    SamtraderRule *entry_short;   // When to short (NULL if long-only)
    SamtraderRule *exit_short;    // When to cover short

    double position_size;         // Fraction of portfolio (0.0-1.0)
    double stop_loss_pct;         // Stop loss percentage (0 = none)
    double take_profit_pct;       // Take profit percentage (0 = none)
    int max_positions;            // Maximum concurrent positions
} SamtraderStrategy;
```

### 5.2 Strategy Configuration File

```ini
[strategy]
name = Golden Cross
description = Buy when SMA(50) crosses above SMA(200)

[rules]
entry_long = CROSS_ABOVE(SMA(50), SMA(200))
exit_long = OR(CROSS_BELOW(SMA(50), SMA(200)), BELOW(RSI(14), 30))

[risk]
position_size = 0.1
stop_loss_pct = 5.0
take_profit_pct = 0
max_positions = 10

[universe]
exchanges = US,AU
# codes = AAPL,MSFT,GOOG  # Optional: specific stocks
```

---

## 6. Backtesting Engine

### 6.1 Backtest Configuration

```c
typedef struct {
    time_t start_date;
    time_t end_date;
    double initial_capital;
    double commission_per_trade;  // Flat fee
    double commission_pct;        // Percentage of trade value
    double slippage_pct;          // Price slippage simulation
    bool allow_shorting;
} SamtraderBacktestConfig;
```

### 6.2 Backtest Results

```c
typedef struct {
    double total_return;
    double annualized_return;
    double sharpe_ratio;
    double sortino_ratio;
    double max_drawdown;
    double max_drawdown_duration;  // Days
    double win_rate;
    double profit_factor;
    int total_trades;
    int winning_trades;
    int losing_trades;
    double average_win;
    double average_loss;
    double largest_win;
    double largest_loss;
    double average_trade_duration; // Days
    SamrenaVector *equity_curve;   // Daily portfolio values
    SamrenaVector *trades;         // All closed trades
} SamtraderBacktestResult;
```

### 6.3 Backtest Interface

```c
// Run backtest
SamtraderBacktestResult *samtrader_backtest_run(
    Samrena *arena,
    SamtraderStrategy *strategy,
    SamtraderBacktestConfig *config,
    SamtraderDataPort *data_port  // Interface to data source
);

// Compare multiple strategies
SamrenaVector *samtrader_backtest_compare(
    Samrena *arena,
    SamrenaVector *strategies,
    SamtraderBacktestConfig *config,
    SamtraderDataPort *data_port
);
```

---

## 7. Ports (Interfaces)

### 7.1 Data Port

```c
typedef struct SamtraderDataPort SamtraderDataPort;

typedef SamrenaVector *(*SamtraderDataFetchFn)(
    SamtraderDataPort *port,
    const char *code,
    const char *exchange,
    time_t start_date,
    time_t end_date
);

typedef SamrenaVector *(*SamtraderDataListSymbolsFn)(
    SamtraderDataPort *port,
    const char *exchange
);

typedef void (*SamtraderDataCloseFn)(SamtraderDataPort *port);

struct SamtraderDataPort {
    void *impl;  // Adapter-specific data
    Samrena *arena;
    SamtraderDataFetchFn fetch_ohlcv;
    SamtraderDataListSymbolsFn list_symbols;
    SamtraderDataCloseFn close;
};
```

### 7.2 Config Port

```c
typedef struct SamtraderConfigPort SamtraderConfigPort;

typedef const char *(*SamtraderConfigGetStringFn)(
    SamtraderConfigPort *port,
    const char *section,
    const char *key
);

typedef int (*SamtraderConfigGetIntFn)(
    SamtraderConfigPort *port,
    const char *section,
    const char *key,
    int default_value
);

typedef double (*SamtraderConfigGetDoubleFn)(
    SamtraderConfigPort *port,
    const char *section,
    const char *key,
    double default_value
);

typedef void (*SamtraderConfigCloseFn)(SamtraderConfigPort *port);

struct SamtraderConfigPort {
    void *impl;
    Samrena *arena;
    SamtraderConfigGetStringFn get_string;
    SamtraderConfigGetIntFn get_int;
    SamtraderConfigGetDoubleFn get_double;
    SamtraderConfigCloseFn close;
};
```

### 7.3 Report Port

```c
typedef struct SamtraderReportPort SamtraderReportPort;

typedef bool (*SamtraderReportWriteFn)(
    SamtraderReportPort *port,
    SamtraderBacktestResult *result,
    SamtraderStrategy *strategy,
    const char *output_path
);

typedef void (*SamtraderReportCloseFn)(SamtraderReportPort *port);

struct SamtraderReportPort {
    void *impl;
    Samrena *arena;
    SamtraderReportWriteFn write;
    SamtraderReportCloseFn close;
};
```

---

## 8. Adapters

### 8.1 PostgreSQL Data Adapter

Connects to the existing OHLCV database.

```c
// Create PostgreSQL adapter
SamtraderDataPort *samtrader_postgres_adapter_create(
    Samrena *arena,
    const char *conninfo
);

// Connection info format: "postgres://user:pass@host:port/dbname"
```

**Database Schema Reference**:
```sql
CREATE TABLE public.ohlcv (
    code character varying NOT NULL,
    exchange character varying NOT NULL,
    date timestamp with time zone NOT NULL,
    open numeric NOT NULL,
    high numeric NOT NULL,
    low numeric NOT NULL,
    close numeric NOT NULL,
    volume integer NOT NULL
);
```

### 8.2 File Config Adapter

Reads INI-style configuration files.

```c
// Create file config adapter
SamtraderConfigPort *samtrader_file_config_adapter_create(
    Samrena *arena,
    const char *config_path
);
```

**Config File Format** (INI):
```ini
[database]
conninfo = postgres://user:password@localhost:5432/samtrader

[backtest]
initial_capital = 100000.0
commission_per_trade = 9.95
commission_pct = 0.0
slippage_pct = 0.1
allow_shorting = false

[strategy]
name = My Strategy
# ... strategy configuration
```

### 8.3 Typst Report Adapter

Generates Typst markup for professional reports.

```c
// Create Typst report adapter
SamtraderReportPort *samtrader_typst_adapter_create(
    Samrena *arena,
    const char *template_path  // Optional custom template
);
```

**Report Contents**:
- Strategy summary and parameters
- Performance metrics table
- Equity curve chart (Typst-native)
- Drawdown visualization
- Trade log
- Monthly/yearly returns breakdown
- Risk metrics

---

## 9. Error Handling

### 9.1 Error Codes

```c
typedef enum {
    SAMTRADER_ERROR_NONE = 0,
    SAMTRADER_ERROR_NULL_PARAM,
    SAMTRADER_ERROR_MEMORY,
    SAMTRADER_ERROR_DB_CONNECTION,
    SAMTRADER_ERROR_DB_QUERY,
    SAMTRADER_ERROR_CONFIG_PARSE,
    SAMTRADER_ERROR_CONFIG_MISSING,
    SAMTRADER_ERROR_RULE_PARSE,
    SAMTRADER_ERROR_RULE_INVALID,
    SAMTRADER_ERROR_NO_DATA,
    SAMTRADER_ERROR_INSUFFICIENT_DATA,
    SAMTRADER_ERROR_IO
} SamtraderError;

const char *samtrader_error_string(SamtraderError error);
```

### 9.2 Error Callback

```c
typedef void (*SamtraderErrorCallback)(
    SamtraderError error,
    const char *message,
    void *user_data
);

void samtrader_set_error_callback(
    SamtraderErrorCallback callback,
    void *user_data
);
```

---

## 10. CLI Interface

### 10.1 Commands

```bash
# Run backtest with config file
samtrader backtest -c config.ini -o report.typ

# Run backtest with strategy file
samtrader backtest -c config.ini -s strategy.ini -o report.typ

# List available symbols
samtrader list-symbols --exchange US

# Validate strategy file
samtrader validate -s strategy.ini

# Show data range for symbol
samtrader info --code AAPL --exchange US
```

### 10.2 Exit Codes

| Code | Meaning |
|------|---------|
| 0 | Success |
| 1 | General error |
| 2 | Configuration error |
| 3 | Database connection error |
| 4 | Invalid strategy/rule |
| 5 | Insufficient data |

---

## 11. Implementation Phases

### Phase 1: Core Infrastructure
1. Set up project structure and CMakeLists.txt
2. Implement OHLCV data structures
3. Implement PostgreSQL data adapter
4. Implement file config adapter
5. Basic tests

### Phase 2: Indicators
1. Implement SMA, EMA, WMA
2. Implement RSI
3. Implement Bollinger Bands
4. Implement MACD, Stochastic
5. Implement ATR, Pivot Points
6. Indicator tests

### Phase 3: Rule System
1. Implement rule data structures
2. Implement rule parser (text -> AST)
3. Implement comparison rules
4. Implement composite rules (AND, OR, NOT)
5. Implement temporal rules
6. Rule evaluation tests

### Phase 4: Backtesting Engine
1. Implement portfolio tracking
2. Implement position management
3. Implement trade execution simulation
4. Implement performance metrics calculation
5. Backtest tests

### Phase 5: Reporting
1. Implement Typst report adapter
2. Implement equity curve generation
3. Implement trade log formatting
4. Implement metrics tables
5. Report output tests

### Phase 6: CLI & Integration
1. Implement CLI argument parsing
2. Integrate all components
3. End-to-end testing
4. Documentation

---

## 12. Dependencies

### 12.1 Internal (Ptah Libraries)
- **samrena**: Arena-based memory allocation
- **samdata**: HashMap, Set, Hash functions

### 12.2 External
- **libpq**: PostgreSQL client library (only external dependency)

### 12.3 Build Requirements
- C11 compiler (GCC, Clang)
- CMake 3.16+
- PostgreSQL development headers (libpq-dev)

---

## 13. Testing Strategy

### 13.1 Unit Tests
- Indicator calculations (compare against known values)
- Rule parsing (valid and invalid inputs)
- Rule evaluation (edge cases)
- Portfolio calculations

### 13.2 Integration Tests
- Database connectivity
- Full backtest pipeline
- Report generation

### 13.3 Property-Based Tests
- Indicator monotonicity/bounds
- Rule composition associativity
- Portfolio invariants (cash + positions = total)

---

## 14. Performance Considerations

### 14.1 Memory
- All allocations through samrena for bulk deallocation
- Pre-calculate indicators once, reuse during backtest
- Use SamrenaVector for time series data

### 14.2 Computation
- Vectorized indicator calculations where possible
- Lazy indicator calculation (only compute what's needed)
- Cache indicator results in HashMap by key

### 14.3 Data Access
- Batch database queries (fetch full date range at once)
- Index on (code, exchange, date) for fast lookups

---

## 15. Future Extensions (Out of Scope)

- Walk-forward optimization
- Monte Carlo simulation
- Portfolio optimization (multiple strategies)
- Live trading adapter
- Real-time data feeds
- Machine learning indicators
- Multi-timeframe analysis
