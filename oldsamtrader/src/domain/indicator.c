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

#include "samtrader/domain/indicator.h"

const char *samtrader_indicator_type_name(SamtraderIndicatorType type) {
  switch (type) {
    case SAMTRADER_IND_SMA:
      return "SMA";
    case SAMTRADER_IND_EMA:
      return "EMA";
    case SAMTRADER_IND_WMA:
      return "WMA";
    case SAMTRADER_IND_RSI:
      return "RSI";
    case SAMTRADER_IND_MACD:
      return "MACD";
    case SAMTRADER_IND_STOCHASTIC:
      return "Stochastic";
    case SAMTRADER_IND_ROC:
      return "ROC";
    case SAMTRADER_IND_BOLLINGER:
      return "Bollinger";
    case SAMTRADER_IND_ATR:
      return "ATR";
    case SAMTRADER_IND_STDDEV:
      return "StdDev";
    case SAMTRADER_IND_OBV:
      return "OBV";
    case SAMTRADER_IND_VWAP:
      return "VWAP";
    case SAMTRADER_IND_PIVOT:
      return "Pivot";
    default:
      return "Unknown";
  }
}

/* Internal helper to create a series with full parameters */
static SamtraderIndicatorSeries *indicator_series_create_internal(Samrena *arena,
                                                                  SamtraderIndicatorType type,
                                                                  int period, int param2,
                                                                  int param3, double param_double,
                                                                  uint64_t initial_capacity) {
  if (!arena) {
    return NULL;
  }

  SamtraderIndicatorSeries *series = SAMRENA_PUSH_TYPE_ZERO(arena, SamtraderIndicatorSeries);
  if (!series) {
    return NULL;
  }

  series->values = samrena_vector_init(arena, sizeof(SamtraderIndicatorValue), initial_capacity);
  if (!series->values) {
    return NULL;
  }

  series->type = type;
  series->params.period = period;
  series->params.param2 = param2;
  series->params.param3 = param3;
  series->params.param_double = param_double;

  return series;
}

SamtraderIndicatorSeries *samtrader_indicator_series_create(Samrena *arena,
                                                            SamtraderIndicatorType type, int period,
                                                            uint64_t initial_capacity) {
  return indicator_series_create_internal(arena, type, period, 0, 0, 0.0, initial_capacity);
}

SamtraderIndicatorSeries *samtrader_macd_series_create(Samrena *arena, int fast_period,
                                                       int slow_period, int signal_period,
                                                       uint64_t initial_capacity) {
  return indicator_series_create_internal(arena, SAMTRADER_IND_MACD, fast_period, slow_period,
                                          signal_period, 0.0, initial_capacity);
}

SamtraderIndicatorSeries *samtrader_stochastic_series_create(Samrena *arena, int k_period,
                                                             int d_period,
                                                             uint64_t initial_capacity) {
  return indicator_series_create_internal(arena, SAMTRADER_IND_STOCHASTIC, k_period, d_period, 0,
                                          0.0, initial_capacity);
}

SamtraderIndicatorSeries *samtrader_bollinger_series_create(Samrena *arena, int period,
                                                            double stddev_multiplier,
                                                            uint64_t initial_capacity) {
  return indicator_series_create_internal(arena, SAMTRADER_IND_BOLLINGER, period, 0, 0,
                                          stddev_multiplier, initial_capacity);
}

SamtraderIndicatorSeries *samtrader_pivot_series_create(Samrena *arena, uint64_t initial_capacity) {
  return indicator_series_create_internal(arena, SAMTRADER_IND_PIVOT, 0, 0, 0, 0.0,
                                          initial_capacity);
}

SamtraderIndicatorValue *samtrader_indicator_add_simple(SamtraderIndicatorSeries *series,
                                                        time_t date, double value, bool valid) {
  if (!series || !series->values) {
    return NULL;
  }

  SamtraderIndicatorValue val = {
      .date = date,
      .valid = valid,
      .type = series->type,
      .data.simple.value = value,
  };

  return (SamtraderIndicatorValue *)samrena_vector_push(series->values, &val);
}

SamtraderIndicatorValue *samtrader_indicator_add_macd(SamtraderIndicatorSeries *series, time_t date,
                                                      double line, double signal, double histogram,
                                                      bool valid) {
  if (!series || !series->values || series->type != SAMTRADER_IND_MACD) {
    return NULL;
  }

  SamtraderIndicatorValue val = {
      .date = date,
      .valid = valid,
      .type = SAMTRADER_IND_MACD,
      .data.macd.line = line,
      .data.macd.signal = signal,
      .data.macd.histogram = histogram,
  };

  return (SamtraderIndicatorValue *)samrena_vector_push(series->values, &val);
}

SamtraderIndicatorValue *samtrader_indicator_add_stochastic(SamtraderIndicatorSeries *series,
                                                            time_t date, double k, double d,
                                                            bool valid) {
  if (!series || !series->values || series->type != SAMTRADER_IND_STOCHASTIC) {
    return NULL;
  }

  SamtraderIndicatorValue val = {
      .date = date,
      .valid = valid,
      .type = SAMTRADER_IND_STOCHASTIC,
      .data.stochastic.k = k,
      .data.stochastic.d = d,
  };

  return (SamtraderIndicatorValue *)samrena_vector_push(series->values, &val);
}

SamtraderIndicatorValue *samtrader_indicator_add_bollinger(SamtraderIndicatorSeries *series,
                                                           time_t date, double upper, double middle,
                                                           double lower, bool valid) {
  if (!series || !series->values || series->type != SAMTRADER_IND_BOLLINGER) {
    return NULL;
  }

  SamtraderIndicatorValue val = {
      .date = date,
      .valid = valid,
      .type = SAMTRADER_IND_BOLLINGER,
      .data.bollinger.upper = upper,
      .data.bollinger.middle = middle,
      .data.bollinger.lower = lower,
  };

  return (SamtraderIndicatorValue *)samrena_vector_push(series->values, &val);
}

SamtraderIndicatorValue *samtrader_indicator_add_pivot(SamtraderIndicatorSeries *series,
                                                       time_t date, double pivot, double r1,
                                                       double r2, double r3, double s1, double s2,
                                                       double s3, bool valid) {
  if (!series || !series->values || series->type != SAMTRADER_IND_PIVOT) {
    return NULL;
  }

  SamtraderIndicatorValue val = {
      .date = date,
      .valid = valid,
      .type = SAMTRADER_IND_PIVOT,
      .data.pivot.pivot = pivot,
      .data.pivot.r1 = r1,
      .data.pivot.r2 = r2,
      .data.pivot.r3 = r3,
      .data.pivot.s1 = s1,
      .data.pivot.s2 = s2,
      .data.pivot.s3 = s3,
  };

  return (SamtraderIndicatorValue *)samrena_vector_push(series->values, &val);
}

const SamtraderIndicatorValue *samtrader_indicator_series_at(const SamtraderIndicatorSeries *series,
                                                             size_t index) {
  if (!series || !series->values) {
    return NULL;
  }

  return (const SamtraderIndicatorValue *)samrena_vector_at_const(series->values, index);
}

size_t samtrader_indicator_series_size(const SamtraderIndicatorSeries *series) {
  if (!series || !series->values) {
    return 0;
  }

  return samrena_vector_size(series->values);
}

bool samtrader_indicator_latest_simple(const SamtraderIndicatorSeries *series, double *out_value) {
  if (!series || !series->values || !out_value) {
    return false;
  }

  size_t size = samrena_vector_size(series->values);
  if (size == 0) {
    return false;
  }

  for (size_t i = size; i > 0; i--) {
    const SamtraderIndicatorValue *val =
        (const SamtraderIndicatorValue *)samrena_vector_at_const(series->values, i - 1);
    if (val && val->valid) {
      *out_value = val->data.simple.value;
      return true;
    }
  }

  return false;
}

bool samtrader_indicator_latest_macd(const SamtraderIndicatorSeries *series,
                                     SamtraderMacdValue *out_value) {
  if (!series || !series->values || !out_value || series->type != SAMTRADER_IND_MACD) {
    return false;
  }

  size_t size = samrena_vector_size(series->values);
  if (size == 0) {
    return false;
  }

  for (size_t i = size; i > 0; i--) {
    const SamtraderIndicatorValue *val =
        (const SamtraderIndicatorValue *)samrena_vector_at_const(series->values, i - 1);
    if (val && val->valid) {
      *out_value = val->data.macd;
      return true;
    }
  }

  return false;
}

bool samtrader_indicator_latest_stochastic(const SamtraderIndicatorSeries *series,
                                           SamtraderStochasticValue *out_value) {
  if (!series || !series->values || !out_value || series->type != SAMTRADER_IND_STOCHASTIC) {
    return false;
  }

  size_t size = samrena_vector_size(series->values);
  if (size == 0) {
    return false;
  }

  for (size_t i = size; i > 0; i--) {
    const SamtraderIndicatorValue *val =
        (const SamtraderIndicatorValue *)samrena_vector_at_const(series->values, i - 1);
    if (val && val->valid) {
      *out_value = val->data.stochastic;
      return true;
    }
  }

  return false;
}

bool samtrader_indicator_latest_bollinger(const SamtraderIndicatorSeries *series,
                                          SamtraderBollingerValue *out_value) {
  if (!series || !series->values || !out_value || series->type != SAMTRADER_IND_BOLLINGER) {
    return false;
  }

  size_t size = samrena_vector_size(series->values);
  if (size == 0) {
    return false;
  }

  for (size_t i = size; i > 0; i--) {
    const SamtraderIndicatorValue *val =
        (const SamtraderIndicatorValue *)samrena_vector_at_const(series->values, i - 1);
    if (val && val->valid) {
      *out_value = val->data.bollinger;
      return true;
    }
  }

  return false;
}

bool samtrader_indicator_latest_pivot(const SamtraderIndicatorSeries *series,
                                      SamtraderPivotValue *out_value) {
  if (!series || !series->values || !out_value || series->type != SAMTRADER_IND_PIVOT) {
    return false;
  }

  size_t size = samrena_vector_size(series->values);
  if (size == 0) {
    return false;
  }

  for (size_t i = size; i > 0; i--) {
    const SamtraderIndicatorValue *val =
        (const SamtraderIndicatorValue *)samrena_vector_at_const(series->values, i - 1);
    if (val && val->valid) {
      *out_value = val->data.pivot;
      return true;
    }
  }

  return false;
}

/*============================================================================
 * Indicator Calculation Dispatcher
 *============================================================================*/

SamtraderIndicatorSeries *samtrader_indicator_calculate(Samrena *arena, SamtraderIndicatorType type,
                                                        SamrenaVector *ohlcv, int period) {
  if (!arena || !ohlcv) {
    return NULL;
  }

  switch (type) {
    case SAMTRADER_IND_SMA:
      return samtrader_calculate_sma(arena, ohlcv, period);
    case SAMTRADER_IND_EMA:
      return samtrader_calculate_ema(arena, ohlcv, period);
    case SAMTRADER_IND_WMA:
      return samtrader_calculate_wma(arena, ohlcv, period);
    case SAMTRADER_IND_RSI:
      return samtrader_calculate_rsi(arena, ohlcv, period);
    case SAMTRADER_IND_MACD:
      return samtrader_calculate_macd(arena, ohlcv, 12, 26, 9);
    case SAMTRADER_IND_STOCHASTIC:
      return samtrader_calculate_stochastic(arena, ohlcv, period, 3);
    case SAMTRADER_IND_BOLLINGER:
      return samtrader_calculate_bollinger(arena, ohlcv, period, 2.0);
    case SAMTRADER_IND_ATR:
      return samtrader_calculate_atr(arena, ohlcv, period);
    case SAMTRADER_IND_PIVOT:
      return samtrader_calculate_pivot(arena, ohlcv);
    default:
      /* Unsupported indicator type */
      return NULL;
  }
}
