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

#ifndef SAMTRADER_DOMAIN_INDICATOR_H
#define SAMTRADER_DOMAIN_INDICATOR_H

#include <stdbool.h>
#include <stdint.h>
#include <time.h>

#include <samrena.h>
#include <samvector.h>

/**
 * @brief Enumeration of supported technical indicator types.
 *
 * Multi-output indicators (MACD, Bollinger, Stochastic, Pivot) are single types
 * with struct fields for each output.
 */
typedef enum {
  /* Trend Indicators (single value) */
  SAMTRADER_IND_SMA, /**< Simple Moving Average */
  SAMTRADER_IND_EMA, /**< Exponential Moving Average */
  SAMTRADER_IND_WMA, /**< Weighted Moving Average */

  /* Momentum Indicators */
  SAMTRADER_IND_RSI,        /**< Relative Strength Index (single value) */
  SAMTRADER_IND_MACD,       /**< MACD (line, signal, histogram) */
  SAMTRADER_IND_STOCHASTIC, /**< Stochastic Oscillator (k, d) */
  SAMTRADER_IND_ROC,        /**< Rate of Change (single value) */

  /* Volatility Indicators */
  SAMTRADER_IND_BOLLINGER, /**< Bollinger Bands (upper, middle, lower) */
  SAMTRADER_IND_ATR,       /**< Average True Range (single value) */
  SAMTRADER_IND_STDDEV,    /**< Standard Deviation (single value) */

  /* Volume Indicators (single value) */
  SAMTRADER_IND_OBV,  /**< On-Balance Volume */
  SAMTRADER_IND_VWAP, /**< Volume-Weighted Average Price */

  /* Support/Resistance */
  SAMTRADER_IND_PIVOT /**< Pivot Points (pivot, r1-r3, s1-s3) */
} SamtraderIndicatorType;

/*============================================================================
 * Individual Indicator Value Structs
 *============================================================================*/

/**
 * @brief Simple single-value indicator (SMA, EMA, WMA, RSI, ROC, ATR, STDDEV,
 * OBV, VWAP)
 */
typedef struct {
  double value;
} SamtraderSimpleIndValue;

/**
 * @brief MACD indicator values.
 *
 * Parameters: fast_period, slow_period, signal_period (typically 12, 26, 9)
 */
typedef struct {
  double line;      /**< MACD line (fast EMA - slow EMA) */
  double signal;    /**< Signal line (EMA of MACD line) */
  double histogram; /**< Histogram (line - signal) */
} SamtraderMacdValue;

/**
 * @brief Stochastic Oscillator values.
 *
 * Parameters: k_period, d_period (typically 14, 3)
 */
typedef struct {
  double k; /**< %K (fast stochastic) */
  double d; /**< %D (slow stochastic, SMA of %K) */
} SamtraderStochasticValue;

/**
 * @brief Bollinger Bands values.
 *
 * Parameters: period, stddev_multiplier (typically 20, 2.0)
 */
typedef struct {
  double upper;  /**< Upper band (middle + stddev * multiplier) */
  double middle; /**< Middle band (SMA) */
  double lower;  /**< Lower band (middle - stddev * multiplier) */
} SamtraderBollingerValue;

/**
 * @brief Pivot Point values (standard pivot points).
 *
 * Calculated from previous day's high, low, close.
 */
typedef struct {
  double pivot; /**< Pivot point: (H + L + C) / 3 */
  double r1;    /**< Resistance 1: (2 * pivot) - L */
  double r2;    /**< Resistance 2: pivot + (H - L) */
  double r3;    /**< Resistance 3: H + 2 * (pivot - L) */
  double s1;    /**< Support 1: (2 * pivot) - H */
  double s2;    /**< Support 2: pivot - (H - L) */
  double s3;    /**< Support 3: L - 2 * (H - pivot) */
} SamtraderPivotValue;

/*============================================================================
 * Tagged Union Indicator Value
 *============================================================================*/

/**
 * @brief A single indicator value at a specific point in time.
 *
 * Uses a tagged union to store type-specific data. Access the appropriate
 * union member based on the type field:
 *
 *   - SMA, EMA, WMA, RSI, ROC, ATR, STDDEV, OBV, VWAP -> data.simple.value
 *   - MACD -> data.macd.line, data.macd.signal, data.macd.histogram
 *   - STOCHASTIC -> data.stochastic.k, data.stochastic.d
 *   - BOLLINGER -> data.bollinger.upper, data.bollinger.middle,
 * data.bollinger.lower
 *   - PIVOT -> data.pivot.pivot, data.pivot.r1, ..., data.pivot.s3
 */
typedef struct {
  time_t date;                 /**< Unix timestamp for this value */
  bool valid;                  /**< False during warmup period */
  SamtraderIndicatorType type; /**< Indicator type (determines union member) */

  union {
    SamtraderSimpleIndValue simple;
    SamtraderMacdValue macd;
    SamtraderStochasticValue stochastic;
    SamtraderBollingerValue bollinger;
    SamtraderPivotValue pivot;
  } data;
} SamtraderIndicatorValue;

/*============================================================================
 * Indicator Series (container for time series of values)
 *============================================================================*/

/**
 * @brief Parameters for indicator calculation.
 *
 * Not all fields are used by every indicator:
 * - SMA/EMA/WMA: period
 * - RSI: period
 * - MACD: period (fast), param2 (slow), param3 (signal)
 * - Stochastic: period (k), param2 (d)
 * - Bollinger: period, param_double (stddev multiplier)
 * - ATR: period
 * - ROC: period
 * - STDDEV: period
 * - OBV, VWAP, PIVOT: no parameters
 */
typedef struct {
  int period;          /**< Primary period (most indicators) */
  int param2;          /**< Secondary period (MACD slow, Stochastic D) */
  int param3;          /**< Tertiary period (MACD signal) */
  double param_double; /**< Double param (Bollinger stddev multiplier) */
} SamtraderIndicatorParams;

/**
 * @brief A time series of indicator values.
 */
typedef struct {
  SamtraderIndicatorType type;     /**< Type of indicator */
  SamtraderIndicatorParams params; /**< Calculation parameters */
  SamrenaVector *values;           /**< Vector of SamtraderIndicatorValue */
} SamtraderIndicatorSeries;

/*============================================================================
 * API Functions
 *============================================================================*/

/**
 * @brief Get a human-readable name for an indicator type.
 */
const char *samtrader_indicator_type_name(SamtraderIndicatorType type);

/**
 * @brief Create an indicator series for simple single-value indicators.
 *
 * Use for: SMA, EMA, WMA, RSI, ROC, ATR, STDDEV, OBV, VWAP
 *
 * @param arena Memory arena for allocation
 * @param type Indicator type
 * @param period Primary period parameter
 * @param initial_capacity Initial capacity for the values vector
 * @return Pointer to the created series, or NULL on failure
 */
SamtraderIndicatorSeries *samtrader_indicator_series_create(Samrena *arena,
                                                            SamtraderIndicatorType type, int period,
                                                            uint64_t initial_capacity);

/**
 * @brief Create a MACD indicator series.
 *
 * @param arena Memory arena for allocation
 * @param fast_period Fast EMA period (typically 12)
 * @param slow_period Slow EMA period (typically 26)
 * @param signal_period Signal line period (typically 9)
 * @param initial_capacity Initial capacity for the values vector
 * @return Pointer to the created series, or NULL on failure
 */
SamtraderIndicatorSeries *samtrader_macd_series_create(Samrena *arena, int fast_period,
                                                       int slow_period, int signal_period,
                                                       uint64_t initial_capacity);

/**
 * @brief Create a Stochastic indicator series.
 *
 * @param arena Memory arena for allocation
 * @param k_period %K period (typically 14)
 * @param d_period %D period (typically 3)
 * @param initial_capacity Initial capacity for the values vector
 * @return Pointer to the created series, or NULL on failure
 */
SamtraderIndicatorSeries *samtrader_stochastic_series_create(Samrena *arena, int k_period,
                                                             int d_period,
                                                             uint64_t initial_capacity);

/**
 * @brief Create a Bollinger Bands indicator series.
 *
 * @param arena Memory arena for allocation
 * @param period Moving average period (typically 20)
 * @param stddev_multiplier Standard deviation multiplier (typically 2.0)
 * @param initial_capacity Initial capacity for the values vector
 * @return Pointer to the created series, or NULL on failure
 */
SamtraderIndicatorSeries *samtrader_bollinger_series_create(Samrena *arena, int period,
                                                            double stddev_multiplier,
                                                            uint64_t initial_capacity);

/**
 * @brief Create a Pivot Points indicator series.
 *
 * @param arena Memory arena for allocation
 * @param initial_capacity Initial capacity for the values vector
 * @return Pointer to the created series, or NULL on failure
 */
SamtraderIndicatorSeries *samtrader_pivot_series_create(Samrena *arena, uint64_t initial_capacity);

/**
 * @brief Add a simple indicator value to a series.
 *
 * Use for: SMA, EMA, WMA, RSI, ROC, ATR, STDDEV, OBV, VWAP
 *
 * @param series The indicator series
 * @param date Timestamp for the value
 * @param value The indicator value
 * @param valid Whether the value is valid (false during warmup)
 * @return Pointer to the added value, or NULL on failure
 */
SamtraderIndicatorValue *samtrader_indicator_add_simple(SamtraderIndicatorSeries *series,
                                                        time_t date, double value, bool valid);

/**
 * @brief Add a MACD value to a series.
 *
 * @param series The indicator series (must be MACD type)
 * @param date Timestamp for the value
 * @param line MACD line value
 * @param signal Signal line value
 * @param histogram Histogram value
 * @param valid Whether the value is valid (false during warmup)
 * @return Pointer to the added value, or NULL on failure
 */
SamtraderIndicatorValue *samtrader_indicator_add_macd(SamtraderIndicatorSeries *series, time_t date,
                                                      double line, double signal, double histogram,
                                                      bool valid);

/**
 * @brief Add a Stochastic value to a series.
 *
 * @param series The indicator series (must be STOCHASTIC type)
 * @param date Timestamp for the value
 * @param k %K value
 * @param d %D value
 * @param valid Whether the value is valid (false during warmup)
 * @return Pointer to the added value, or NULL on failure
 */
SamtraderIndicatorValue *samtrader_indicator_add_stochastic(SamtraderIndicatorSeries *series,
                                                            time_t date, double k, double d,
                                                            bool valid);

/**
 * @brief Add a Bollinger Bands value to a series.
 *
 * @param series The indicator series (must be BOLLINGER type)
 * @param date Timestamp for the value
 * @param upper Upper band value
 * @param middle Middle band value
 * @param lower Lower band value
 * @param valid Whether the value is valid (false during warmup)
 * @return Pointer to the added value, or NULL on failure
 */
SamtraderIndicatorValue *samtrader_indicator_add_bollinger(SamtraderIndicatorSeries *series,
                                                           time_t date, double upper, double middle,
                                                           double lower, bool valid);

/**
 * @brief Add a Pivot Points value to a series.
 *
 * @param series The indicator series (must be PIVOT type)
 * @param date Timestamp for the value
 * @param pivot Pivot point value
 * @param r1 Resistance 1 value
 * @param r2 Resistance 2 value
 * @param r3 Resistance 3 value
 * @param s1 Support 1 value
 * @param s2 Support 2 value
 * @param s3 Support 3 value
 * @param valid Whether the value is valid
 * @return Pointer to the added value, or NULL on failure
 */
SamtraderIndicatorValue *samtrader_indicator_add_pivot(SamtraderIndicatorSeries *series,
                                                       time_t date, double pivot, double r1,
                                                       double r2, double r3, double s1, double s2,
                                                       double s3, bool valid);

/**
 * @brief Get an indicator value at a specific index.
 *
 * @param series The indicator series
 * @param index Index into the series (0 = oldest)
 * @return Pointer to the value, or NULL if index out of bounds
 */
const SamtraderIndicatorValue *samtrader_indicator_series_at(const SamtraderIndicatorSeries *series,
                                                             size_t index);

/**
 * @brief Get the number of values in an indicator series.
 *
 * @param series The indicator series
 * @return Number of values, or 0 if series is NULL
 */
size_t samtrader_indicator_series_size(const SamtraderIndicatorSeries *series);

/**
 * @brief Get the latest valid value from a simple indicator series.
 *
 * @param series The indicator series
 * @param out_value Pointer to store the value (if found)
 * @return true if a valid value was found, false otherwise
 */
bool samtrader_indicator_latest_simple(const SamtraderIndicatorSeries *series, double *out_value);

/**
 * @brief Get the latest valid MACD value from a series.
 *
 * @param series The indicator series (must be MACD type)
 * @param out_value Pointer to store the MACD values (if found)
 * @return true if a valid value was found, false otherwise
 */
bool samtrader_indicator_latest_macd(const SamtraderIndicatorSeries *series,
                                     SamtraderMacdValue *out_value);

/**
 * @brief Get the latest valid Stochastic value from a series.
 *
 * @param series The indicator series (must be STOCHASTIC type)
 * @param out_value Pointer to store the Stochastic values (if found)
 * @return true if a valid value was found, false otherwise
 */
bool samtrader_indicator_latest_stochastic(const SamtraderIndicatorSeries *series,
                                           SamtraderStochasticValue *out_value);

/**
 * @brief Get the latest valid Bollinger value from a series.
 *
 * @param series The indicator series (must be BOLLINGER type)
 * @param out_value Pointer to store the Bollinger values (if found)
 * @return true if a valid value was found, false otherwise
 */
bool samtrader_indicator_latest_bollinger(const SamtraderIndicatorSeries *series,
                                          SamtraderBollingerValue *out_value);

/**
 * @brief Get the latest valid Pivot value from a series.
 *
 * @param series The indicator series (must be PIVOT type)
 * @param out_value Pointer to store the Pivot values (if found)
 * @return true if a valid value was found, false otherwise
 */
bool samtrader_indicator_latest_pivot(const SamtraderIndicatorSeries *series,
                                      SamtraderPivotValue *out_value);

/*============================================================================
 * Indicator Calculation Functions
 *============================================================================*/

/**
 * @brief Calculate an indicator series from OHLCV data.
 *
 * This is the main entry point for calculating indicators. It dispatches
 * to the appropriate calculation function based on the indicator type.
 *
 * Supported types:
 *   - SAMTRADER_IND_SMA: Simple Moving Average
 *   - SAMTRADER_IND_EMA: Exponential Moving Average
 *   - SAMTRADER_IND_WMA: Weighted Moving Average
 *   - SAMTRADER_IND_RSI: Relative Strength Index
 *   - SAMTRADER_IND_MACD: MACD (uses default 12/26/9 periods)
 *   - SAMTRADER_IND_STOCHASTIC: Stochastic (uses period for %K, default 3 for %D)
 *   - SAMTRADER_IND_BOLLINGER: Bollinger Bands (uses default 2.0 stddev)
 *   - SAMTRADER_IND_ATR: Average True Range
 *   - SAMTRADER_IND_PIVOT: Standard Pivot Points (period param ignored)
 *
 * @param arena Memory arena for allocation
 * @param type Indicator type to calculate
 * @param ohlcv Vector of SamtraderOhlcv price data
 * @param period Period parameter for the indicator
 * @return Pointer to the calculated series, or NULL on failure
 */
SamtraderIndicatorSeries *samtrader_indicator_calculate(Samrena *arena, SamtraderIndicatorType type,
                                                        SamrenaVector *ohlcv, int period);

/**
 * @brief Calculate Simple Moving Average (SMA) from OHLCV data.
 *
 * SMA(n) = (P1 + P2 + ... + Pn) / n
 *
 * The first (period - 1) values are marked as invalid (warmup period).
 * Uses the close price for calculation.
 *
 * @param arena Memory arena for allocation
 * @param ohlcv Vector of SamtraderOhlcv price data
 * @param period Number of periods for the average
 * @return Pointer to the calculated series, or NULL on failure
 */
SamtraderIndicatorSeries *samtrader_calculate_sma(Samrena *arena, SamrenaVector *ohlcv, int period);

/**
 * @brief Calculate Exponential Moving Average (EMA) from OHLCV data.
 *
 * EMA(n) = Price * k + EMA_prev * (1 - k), where k = 2 / (n + 1)
 *
 * The first EMA value is initialized to the SMA of the first n periods.
 * The first (period - 1) values are marked as invalid (warmup period).
 * Uses the close price for calculation.
 *
 * @param arena Memory arena for allocation
 * @param ohlcv Vector of SamtraderOhlcv price data
 * @param period Number of periods for the average
 * @return Pointer to the calculated series, or NULL on failure
 */
SamtraderIndicatorSeries *samtrader_calculate_ema(Samrena *arena, SamrenaVector *ohlcv, int period);

/**
 * @brief Calculate Weighted Moving Average (WMA) from OHLCV data.
 *
 * WMA(n) = (n*Pn + (n-1)*P(n-1) + ... + 1*P1) / (n*(n+1)/2)
 *
 * The most recent price has the highest weight (n), and the oldest
 * price in the window has weight 1.
 * The first (period - 1) values are marked as invalid (warmup period).
 * Uses the close price for calculation.
 *
 * @param arena Memory arena for allocation
 * @param ohlcv Vector of SamtraderOhlcv price data
 * @param period Number of periods for the average
 * @return Pointer to the calculated series, or NULL on failure
 */
SamtraderIndicatorSeries *samtrader_calculate_wma(Samrena *arena, SamrenaVector *ohlcv, int period);

/**
 * @brief Calculate Relative Strength Index (RSI) from OHLCV data.
 *
 * RSI = 100 - (100 / (1 + RS)), where RS = Avg Gain / Avg Loss
 *
 * The first average is a simple mean over the period. Subsequent values
 * use Wilder's smoothing: Avg = (prev_Avg * (period-1) + current) / period.
 * The first `period` values are marked as invalid (warmup period).
 * Uses the close price for calculation.
 *
 * @param arena Memory arena for allocation
 * @param ohlcv Vector of SamtraderOhlcv price data
 * @param period Number of periods (typically 14)
 * @return Pointer to the calculated series, or NULL on failure
 */
SamtraderIndicatorSeries *samtrader_calculate_rsi(Samrena *arena, SamrenaVector *ohlcv, int period);

/**
 * @brief Calculate Bollinger Bands from OHLCV data.
 *
 * Middle = SMA(period)
 * StdDev = sqrt(sum((close - SMA)^2) / period)
 * Upper = Middle + stddev_multiplier * StdDev
 * Lower = Middle - stddev_multiplier * StdDev
 *
 * The first (period - 1) values are marked as invalid (warmup period).
 * Uses the close price for calculation.
 *
 * @param arena Memory arena for allocation
 * @param ohlcv Vector of SamtraderOhlcv price data
 * @param period Number of periods for the moving average (typically 20)
 * @param stddev_multiplier Standard deviation multiplier (typically 2.0)
 * @return Pointer to the calculated series, or NULL on failure
 */
SamtraderIndicatorSeries *samtrader_calculate_bollinger(Samrena *arena, SamrenaVector *ohlcv,
                                                        int period, double stddev_multiplier);

/**
 * @brief Calculate MACD (Moving Average Convergence Divergence) from OHLCV data.
 *
 * MACD Line = EMA(fast_period) - EMA(slow_period)
 * Signal Line = EMA(signal_period) of MACD Line
 * Histogram = MACD Line - Signal Line
 *
 * The first (max(fast, slow) - 1 + signal - 1) values are marked as invalid
 * (warmup period). Uses the close price for calculation.
 *
 * @param arena Memory arena for allocation
 * @param ohlcv Vector of SamtraderOhlcv price data
 * @param fast_period Fast EMA period (typically 12)
 * @param slow_period Slow EMA period (typically 26)
 * @param signal_period Signal line EMA period (typically 9)
 * @return Pointer to the calculated series, or NULL on failure
 */
SamtraderIndicatorSeries *samtrader_calculate_macd(Samrena *arena, SamrenaVector *ohlcv,
                                                   int fast_period, int slow_period,
                                                   int signal_period);

/**
 * @brief Calculate Stochastic Oscillator from OHLCV data.
 *
 * %K = 100 * (close - lowest_low) / (highest_high - lowest_low)
 * %D = SMA(d_period) of %K
 *
 * Uses high and low prices for the lookback window, and close for %K.
 * The first (k_period - 1 + d_period - 1) values are marked as invalid
 * (warmup period).
 *
 * @param arena Memory arena for allocation
 * @param ohlcv Vector of SamtraderOhlcv price data
 * @param k_period %K lookback period (typically 14)
 * @param d_period %D smoothing period (typically 3)
 * @return Pointer to the calculated series, or NULL on failure
 */
SamtraderIndicatorSeries *samtrader_calculate_stochastic(Samrena *arena, SamrenaVector *ohlcv,
                                                         int k_period, int d_period);

/**
 * @brief Calculate Average True Range (ATR) from OHLCV data.
 *
 * ATR = Wilder's smoothed average of True Range
 * TR = max(high - low, |high - prev_close|, |low - prev_close|)
 *
 * The first TR value (bar 0) uses high - low since no previous close exists.
 * The first valid ATR (at index period - 1) is the simple average of the
 * first `period` true range values. Subsequent values use Wilder's smoothing:
 * ATR = (prev_ATR * (period - 1) + current_TR) / period.
 *
 * @param arena Memory arena for allocation
 * @param ohlcv Vector of SamtraderOhlcv price data
 * @param period Number of periods (typically 14)
 * @return Pointer to the calculated series, or NULL on failure
 */
SamtraderIndicatorSeries *samtrader_calculate_atr(Samrena *arena, SamrenaVector *ohlcv, int period);

/**
 * @brief Calculate Standard Pivot Points from OHLCV data.
 *
 * Pivot = (H + L + C) / 3
 * R1 = (2 * Pivot) - L,  S1 = (2 * Pivot) - H
 * R2 = Pivot + (H - L),  S2 = Pivot - (H - L)
 * R3 = H + 2*(Pivot-L),  S3 = L - 2*(H - Pivot)
 *
 * Each bar's pivot levels are calculated from the previous bar's high, low,
 * and close. The first bar is marked invalid (no previous data).
 *
 * @param arena Memory arena for allocation
 * @param ohlcv Vector of SamtraderOhlcv price data
 * @return Pointer to the calculated series, or NULL on failure
 */
SamtraderIndicatorSeries *samtrader_calculate_pivot(Samrena *arena, SamrenaVector *ohlcv);

#endif /* SAMTRADER_DOMAIN_INDICATOR_H */
