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

#include <samtrader/adapters/typst_report_adapter.h>

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include <samtrader/domain/portfolio.h>

/* Maximum template file size (1MB) */
#define MAX_TEMPLATE_SIZE (1024 * 1024)

/* Maximum placeholder key length */
#define MAX_KEY_LENGTH 64

/* Maximum formatted value length */
#define MAX_VALUE_LENGTH 256

/* Maximum number of distinct years in monthly returns table */
#define MAX_YEARS 50

/* Chart SVG dimensions */
#define CHART_SVG_WIDTH 600
#define CHART_SVG_HEIGHT 250
#define CHART_MARGIN_LEFT 70
#define CHART_MARGIN_RIGHT 20
#define CHART_MARGIN_TOP 15
#define CHART_MARGIN_BOTTOM 40
#define MAX_CHART_POINTS 200

/**
 * @brief Internal implementation data for Typst report adapter.
 */
typedef struct {
  const char *template_path; /**< Path to custom template, or NULL */
} TypstReportImpl;

/* Forward declarations */
static bool typst_report_write(SamtraderReportPort *port, SamtraderBacktestResult *result,
                               SamtraderStrategy *strategy, const char *output_path);
static bool typst_report_write_multi(SamtraderReportPort *port,
                                     SamtraderMultiCodeResult *multi_result,
                                     SamtraderStrategy *strategy, const char *output_path);
static void typst_report_close(SamtraderReportPort *port);
static void write_monthly_returns_table(FILE *out, SamrenaVector *equity_curve);
static void write_equity_curve_chart(FILE *out, SamrenaVector *equity_curve);
static void write_drawdown_chart(FILE *out, SamrenaVector *equity_curve);
static void write_trade_log(FILE *out, SamrenaVector *trades);
static void write_universe_summary_table(FILE *out, SamtraderCodeResult *results, size_t count);
static void write_per_code_detail_section(FILE *out, SamtraderCodeResult *cr,
                                          SamrenaVector *all_trades);
static void write_full_trade_log(FILE *out, SamrenaVector *trades);

/* ============================================================================
 * Template Placeholder Resolution
 * ============================================================================ */

/**
 * @brief Resolve a placeholder key to its value.
 *
 * @param key Placeholder key (without {{ }} delimiters)
 * @param result Backtest results
 * @param strategy Strategy definition
 * @param buf Output buffer for the value
 * @param buf_size Size of the output buffer
 * @return true if the key was recognized, false otherwise
 */
static bool resolve_placeholder(const char *key, SamtraderBacktestResult *result,
                                SamtraderStrategy *strategy, char *buf, size_t buf_size) {
  if (strcmp(key, "STRATEGY_NAME") == 0) {
    snprintf(buf, buf_size, "%s", strategy->name ? strategy->name : "Unnamed Strategy");
    return true;
  }
  if (strcmp(key, "STRATEGY_DESCRIPTION") == 0) {
    snprintf(buf, buf_size, "%s", strategy->description ? strategy->description : "");
    return true;
  }
  if (strcmp(key, "POSITION_SIZE") == 0) {
    snprintf(buf, buf_size, "%.1f", strategy->position_size * 100.0);
    return true;
  }
  if (strcmp(key, "STOP_LOSS_PCT") == 0) {
    snprintf(buf, buf_size, "%.1f", strategy->stop_loss_pct);
    return true;
  }
  if (strcmp(key, "TAKE_PROFIT_PCT") == 0) {
    snprintf(buf, buf_size, "%.1f", strategy->take_profit_pct);
    return true;
  }
  if (strcmp(key, "MAX_POSITIONS") == 0) {
    snprintf(buf, buf_size, "%d", strategy->max_positions);
    return true;
  }
  if (strcmp(key, "TOTAL_RETURN") == 0) {
    snprintf(buf, buf_size, "%.2f", result->total_return * 100.0);
    return true;
  }
  if (strcmp(key, "ANNUALIZED_RETURN") == 0) {
    snprintf(buf, buf_size, "%.2f", result->annualized_return * 100.0);
    return true;
  }
  if (strcmp(key, "SHARPE_RATIO") == 0) {
    snprintf(buf, buf_size, "%.3f", result->sharpe_ratio);
    return true;
  }
  if (strcmp(key, "SORTINO_RATIO") == 0) {
    snprintf(buf, buf_size, "%.3f", result->sortino_ratio);
    return true;
  }
  if (strcmp(key, "MAX_DRAWDOWN") == 0) {
    snprintf(buf, buf_size, "%.2f", result->max_drawdown * 100.0);
    return true;
  }
  if (strcmp(key, "MAX_DRAWDOWN_DURATION") == 0) {
    snprintf(buf, buf_size, "%.0f", result->max_drawdown_duration);
    return true;
  }
  if (strcmp(key, "WIN_RATE") == 0) {
    snprintf(buf, buf_size, "%.1f", result->win_rate * 100.0);
    return true;
  }
  if (strcmp(key, "PROFIT_FACTOR") == 0) {
    snprintf(buf, buf_size, "%.2f", result->profit_factor);
    return true;
  }
  if (strcmp(key, "TOTAL_TRADES") == 0) {
    snprintf(buf, buf_size, "%d", result->total_trades);
    return true;
  }
  if (strcmp(key, "WINNING_TRADES") == 0) {
    snprintf(buf, buf_size, "%d", result->winning_trades);
    return true;
  }
  if (strcmp(key, "LOSING_TRADES") == 0) {
    snprintf(buf, buf_size, "%d", result->losing_trades);
    return true;
  }
  if (strcmp(key, "AVERAGE_WIN") == 0) {
    snprintf(buf, buf_size, "%.2f", result->average_win);
    return true;
  }
  if (strcmp(key, "AVERAGE_LOSS") == 0) {
    snprintf(buf, buf_size, "%.2f", result->average_loss);
    return true;
  }
  if (strcmp(key, "LARGEST_WIN") == 0) {
    snprintf(buf, buf_size, "%.2f", result->largest_win);
    return true;
  }
  if (strcmp(key, "LARGEST_LOSS") == 0) {
    snprintf(buf, buf_size, "%.2f", result->largest_loss);
    return true;
  }
  if (strcmp(key, "AVG_TRADE_DURATION") == 0) {
    snprintf(buf, buf_size, "%.1f", result->average_trade_duration);
    return true;
  }
  if (strcmp(key, "GENERATED_DATE") == 0) {
    time_t now = time(NULL);
    struct tm *tm_info = localtime(&now);
    strftime(buf, buf_size, "%Y-%m-%d", tm_info);
    return true;
  }

  return false;
}

/* ============================================================================
 * Template-Based Report Generation
 * ============================================================================ */

/**
 * @brief Write a report using a custom Typst template with placeholder substitution.
 *
 * Reads the template file, replaces {{PLACEHOLDER}} markers with actual values,
 * and writes the result to the output file.
 *
 * @param template_path Path to the template file
 * @param result Backtest results
 * @param strategy Strategy definition
 * @param output_path Output file path
 * @return true on success, false on failure
 */
static bool write_template_report(const char *template_path, SamtraderBacktestResult *result,
                                  SamtraderStrategy *strategy, const char *output_path) {
  /* Read template file */
  FILE *tmpl_file = fopen(template_path, "r");
  if (tmpl_file == NULL) {
    return false;
  }

  fseek(tmpl_file, 0, SEEK_END);
  long file_size = ftell(tmpl_file);
  fseek(tmpl_file, 0, SEEK_SET);

  if (file_size <= 0 || (size_t)file_size > MAX_TEMPLATE_SIZE) {
    fclose(tmpl_file);
    return false;
  }

  char *template_buf = malloc((size_t)file_size + 1);
  if (template_buf == NULL) {
    fclose(tmpl_file);
    return false;
  }

  size_t bytes_read = fread(template_buf, 1, (size_t)file_size, tmpl_file);
  fclose(tmpl_file);
  template_buf[bytes_read] = '\0';

  /* Open output file */
  FILE *out = fopen(output_path, "w");
  if (out == NULL) {
    free(template_buf);
    return false;
  }

  /* Process template: scan for {{ }} placeholders */
  const char *pos = template_buf;
  while (*pos != '\0') {
    const char *open = strstr(pos, "{{");
    if (open == NULL) {
      /* No more placeholders, write remaining text */
      fputs(pos, out);
      break;
    }

    /* Write text before the placeholder */
    fwrite(pos, 1, (size_t)(open - pos), out);

    const char *key_start = open + 2;
    const char *close_marker = strstr(key_start, "}}");
    if (close_marker == NULL) {
      /* Unterminated placeholder, write it literally */
      fputs(open, out);
      break;
    }

    /* Extract key */
    size_t key_len = (size_t)(close_marker - key_start);
    if (key_len >= MAX_KEY_LENGTH) {
      /* Key too long, write literally */
      fwrite(open, 1, (size_t)(close_marker + 2 - open), out);
      pos = close_marker + 2;
      continue;
    }

    char key[MAX_KEY_LENGTH];
    memcpy(key, key_start, key_len);
    key[key_len] = '\0';

    /* Handle chart placeholders (multi-KB output, bypass value buffer) */
    if (strcmp(key, "MONTHLY_RETURNS") == 0) {
      write_monthly_returns_table(out, result->equity_curve);
      pos = close_marker + 2;
      continue;
    }
    if (strcmp(key, "EQUITY_CURVE_CHART") == 0) {
      write_equity_curve_chart(out, result->equity_curve);
      pos = close_marker + 2;
      continue;
    }
    if (strcmp(key, "DRAWDOWN_CHART") == 0) {
      write_drawdown_chart(out, result->equity_curve);
      pos = close_marker + 2;
      continue;
    }
    if (strcmp(key, "TRADE_LOG") == 0) {
      write_trade_log(out, result->trades);
      pos = close_marker + 2;
      continue;
    }

    /* Resolve placeholder */
    char value[MAX_VALUE_LENGTH];
    if (resolve_placeholder(key, result, strategy, value, sizeof(value))) {
      fputs(value, out);
    } else {
      /* Unknown placeholder, write it literally */
      fwrite(open, 1, (size_t)(close_marker + 2 - open), out);
    }

    pos = close_marker + 2;
  }

  free(template_buf);
  fclose(out);
  return true;
}

/* ============================================================================
 * Monthly Returns Table
 * ============================================================================ */

/**
 * @brief Write a monthly/yearly returns breakdown table.
 *
 * Computes monthly returns from the equity curve and renders them as a Typst
 * table with color-coded cells (green for positive, red for negative).
 * Rows are years, columns are Jan–Dec plus a YTD column.
 *
 * @param out Output file stream
 * @param equity_curve Vector of SamtraderEquityPoint
 */
static void write_monthly_returns_table(FILE *out, SamrenaVector *equity_curve) {
  if (equity_curve == NULL || samrena_vector_size(equity_curve) < 2) {
    return;
  }

  size_t n = samrena_vector_size(equity_curve);

  /* Find year range */
  const SamtraderEquityPoint *first_pt =
      (const SamtraderEquityPoint *)samrena_vector_at_unchecked_const(equity_curve, 0);
  const SamtraderEquityPoint *last_pt =
      (const SamtraderEquityPoint *)samrena_vector_at_unchecked_const(equity_curve, n - 1);

  struct tm *tm_info = localtime(&first_pt->date);
  int min_year = tm_info->tm_year + 1900;
  tm_info = localtime(&last_pt->date);
  int max_year = tm_info->tm_year + 1900;

  int num_years = max_year - min_year + 1;
  if (num_years <= 0 || num_years > MAX_YEARS) {
    return;
  }

  /* Track first and last equity value per month */
  double first_equity[MAX_YEARS][12];
  double last_equity[MAX_YEARS][12];
  bool has_data[MAX_YEARS][12];
  memset(has_data, 0, sizeof(has_data));

  /* Track first and last equity value per year (for YTD) */
  double year_first_equity[MAX_YEARS];
  double year_last_equity[MAX_YEARS];
  bool year_has_data[MAX_YEARS];
  memset(year_has_data, 0, sizeof(year_has_data));

  for (size_t i = 0; i < n; i++) {
    const SamtraderEquityPoint *pt =
        (const SamtraderEquityPoint *)samrena_vector_at_unchecked_const(equity_curve, i);

    tm_info = localtime(&pt->date);
    int yr = tm_info->tm_year + 1900;
    int mo = tm_info->tm_mon; /* 0-11 */
    int yr_idx = yr - min_year;

    if (yr_idx < 0 || yr_idx >= MAX_YEARS) {
      continue;
    }

    if (!has_data[yr_idx][mo]) {
      first_equity[yr_idx][mo] = pt->equity;
      has_data[yr_idx][mo] = true;
    }
    last_equity[yr_idx][mo] = pt->equity;

    if (!year_has_data[yr_idx]) {
      year_first_equity[yr_idx] = pt->equity;
      year_has_data[yr_idx] = true;
    }
    year_last_equity[yr_idx] = pt->equity;
  }

  /* Write the table */
  fprintf(out, "== Monthly Returns (%%)\n");
  fprintf(out, "\n");
  fprintf(out, "#table(\n");
  fprintf(out, "  columns: (auto, auto, auto, auto, auto, auto, auto, "
               "auto, auto, auto, auto, auto, auto, auto),\n");
  fprintf(out, "  inset: 6pt,\n");
  fprintf(out, "  fill: (x, y) => if y == 0 { luma(230) },\n");
  fprintf(out, "  [*Year*], [*Jan*], [*Feb*], [*Mar*], [*Apr*], [*May*], [*Jun*], "
               "[*Jul*], [*Aug*], [*Sep*], [*Oct*], [*Nov*], [*Dec*], [*YTD*],\n");

  for (int yr_idx = 0; yr_idx < num_years; yr_idx++) {
    int year = min_year + yr_idx;
    fprintf(out, "  [%d],", year);

    /* Monthly cells */
    for (int mo = 0; mo < 12; mo++) {
      if (!has_data[yr_idx][mo] || first_equity[yr_idx][mo] == 0.0) {
        fprintf(out, " [\\u{2014}],");
      } else {
        double ret =
            (last_equity[yr_idx][mo] - first_equity[yr_idx][mo]) / first_equity[yr_idx][mo] * 100.0;
        if (ret >= 0.0) {
          fprintf(out, " [#text(fill: rgb(\"#16a34a\"))[%.2f]],", ret);
        } else {
          fprintf(out, " [#text(fill: rgb(\"#dc2626\"))[%.2f]],", ret);
        }
      }
    }

    /* YTD cell */
    if (!year_has_data[yr_idx] || year_first_equity[yr_idx] == 0.0) {
      fprintf(out, " [\\u{2014}],\n");
    } else {
      double ytd = (year_last_equity[yr_idx] - year_first_equity[yr_idx]) /
                   year_first_equity[yr_idx] * 100.0;
      if (ytd >= 0.0) {
        fprintf(out, " [#text(fill: rgb(\"#16a34a\"))[%.2f]],\n", ytd);
      } else {
        fprintf(out, " [#text(fill: rgb(\"#dc2626\"))[%.2f]],\n", ytd);
      }
    }
  }

  fprintf(out, ")\n");
  fprintf(out, "\n");
}

/* ============================================================================
 * Chart Generation
 * ============================================================================ */

/**
 * @brief Format a dollar value for Y-axis labels.
 *
 * Values >= 1000 are shown as "$XK", values >= 1000000 as "$XM".
 */
static void format_dollar_label(double value, char *buf, size_t buf_size) {
  double abs_val = fabs(value);
  if (abs_val >= 1000000.0) {
    snprintf(buf, buf_size, "$%.1fM", value / 1000000.0);
  } else if (abs_val >= 1000.0) {
    snprintf(buf, buf_size, "$%.0fK", value / 1000.0);
  } else {
    snprintf(buf, buf_size, "$%.0f", value);
  }
}

/**
 * @brief Write an equity curve chart as inline SVG in Typst.
 */
static void write_equity_curve_chart(FILE *out, SamrenaVector *equity_curve) {
  if (equity_curve == NULL || samrena_vector_size(equity_curve) < 2) {
    return;
  }

  size_t n = samrena_vector_size(equity_curve);

  /* Find min/max equity and date range */
  const SamtraderEquityPoint *first =
      (const SamtraderEquityPoint *)samrena_vector_at_unchecked_const(equity_curve, 0);
  const SamtraderEquityPoint *last =
      (const SamtraderEquityPoint *)samrena_vector_at_unchecked_const(equity_curve, n - 1);

  double min_equity = first->equity;
  double max_equity = first->equity;
  for (size_t i = 1; i < n; i++) {
    const SamtraderEquityPoint *pt =
        (const SamtraderEquityPoint *)samrena_vector_at_unchecked_const(equity_curve, i);
    if (pt->equity < min_equity)
      min_equity = pt->equity;
    if (pt->equity > max_equity)
      max_equity = pt->equity;
  }

  /* Pad if flat */
  if (max_equity - min_equity < 1.0) {
    max_equity = min_equity + 100.0;
  }

  double date_min = (double)first->date;
  double date_max = (double)last->date;
  if (date_max - date_min < 1.0) {
    date_max = date_min + 86400.0;
  }

  /* Plot area dimensions */
  int plot_w = CHART_SVG_WIDTH - CHART_MARGIN_LEFT - CHART_MARGIN_RIGHT;
  int plot_h = CHART_SVG_HEIGHT - CHART_MARGIN_TOP - CHART_MARGIN_BOTTOM;

  /* Determine sample indices for downsampling */
  size_t num_points = n;
  if (num_points > MAX_CHART_POINTS) {
    num_points = MAX_CHART_POINTS;
  }

  fprintf(out, "== Equity Curve\n\n");
  fprintf(out, "#image.decode(\n");
  fprintf(out, "  width: 100%%,\n");
  fprintf(out, "  \"<svg xmlns='http://www.w3.org/2000/svg' ");
  fprintf(out, "viewBox='0 0 %d %d'>\n", CHART_SVG_WIDTH, CHART_SVG_HEIGHT);

  /* Background */
  fprintf(out, "<rect width='%d' height='%d' fill='white'/>\n", CHART_SVG_WIDTH, CHART_SVG_HEIGHT);

  /* Horizontal grid lines and Y-axis labels (5 lines) */
  int num_grid = 5;
  for (int i = 0; i <= num_grid; i++) {
    double frac = (double)i / (double)num_grid;
    int y = CHART_MARGIN_TOP + (int)(frac * plot_h);
    double val = max_equity - frac * (max_equity - min_equity);
    char label[32];
    format_dollar_label(val, label, sizeof(label));

    fprintf(out, "<line x1='%d' y1='%d' x2='%d' y2='%d' stroke='#e5e7eb' stroke-width='1'/>\n",
            CHART_MARGIN_LEFT, y, CHART_MARGIN_LEFT + plot_w, y);
    fprintf(out,
            "<text x='%d' y='%d' text-anchor='end' font-size='10' "
            "fill='#6b7280' font-family='sans-serif'>%s</text>\n",
            CHART_MARGIN_LEFT - 8, y + 4, label);
  }

  /* Build polyline points and polygon points */
  fprintf(out, "<polygon points='%d,%d ", CHART_MARGIN_LEFT, CHART_MARGIN_TOP + plot_h);

  for (size_t i = 0; i < num_points; i++) {
    size_t idx = (n > MAX_CHART_POINTS) ? (i * (n - 1)) / (num_points - 1) : i;
    const SamtraderEquityPoint *pt =
        (const SamtraderEquityPoint *)samrena_vector_at_unchecked_const(equity_curve, idx);

    double x_frac = ((double)pt->date - date_min) / (date_max - date_min);
    double y_frac = (pt->equity - min_equity) / (max_equity - min_equity);
    int px = CHART_MARGIN_LEFT + (int)(x_frac * plot_w);
    int py = CHART_MARGIN_TOP + plot_h - (int)(y_frac * plot_h);
    fprintf(out, "%d,%d ", px, py);
  }

  fprintf(out, "%d,%d' fill='rgba(37,99,235,0.15)' stroke='none'/>\n", CHART_MARGIN_LEFT + plot_w,
          CHART_MARGIN_TOP + plot_h);

  /* Polyline for the curve */
  fprintf(out, "<polyline points='");
  for (size_t i = 0; i < num_points; i++) {
    size_t idx = (n > MAX_CHART_POINTS) ? (i * (n - 1)) / (num_points - 1) : i;
    const SamtraderEquityPoint *pt =
        (const SamtraderEquityPoint *)samrena_vector_at_unchecked_const(equity_curve, idx);

    double x_frac = ((double)pt->date - date_min) / (date_max - date_min);
    double y_frac = (pt->equity - min_equity) / (max_equity - min_equity);
    int px = CHART_MARGIN_LEFT + (int)(x_frac * plot_w);
    int py = CHART_MARGIN_TOP + plot_h - (int)(y_frac * plot_h);
    fprintf(out, "%d,%d ", px, py);
  }
  fprintf(out, "' fill='none' stroke='#2563eb' stroke-width='1.5'/>\n");

  /* X-axis date labels */
  int num_x_labels = 5;
  for (int i = 0; i <= num_x_labels; i++) {
    double frac = (double)i / (double)num_x_labels;
    int x = CHART_MARGIN_LEFT + (int)(frac * plot_w);
    time_t t = (time_t)(date_min + frac * (date_max - date_min));
    struct tm *tm_info = localtime(&t);
    char date_label[16];
    strftime(date_label, sizeof(date_label), "%Y-%m", tm_info);

    fprintf(out,
            "<text x='%d' y='%d' text-anchor='middle' font-size='10' "
            "fill='#6b7280' font-family='sans-serif'>%s</text>\n",
            x, CHART_MARGIN_TOP + plot_h + 20, date_label);
  }

  /* Axis border lines */
  fprintf(out, "<line x1='%d' y1='%d' x2='%d' y2='%d' stroke='#d1d5db' stroke-width='1'/>\n",
          CHART_MARGIN_LEFT, CHART_MARGIN_TOP, CHART_MARGIN_LEFT, CHART_MARGIN_TOP + plot_h);
  fprintf(out, "<line x1='%d' y1='%d' x2='%d' y2='%d' stroke='#d1d5db' stroke-width='1'/>\n",
          CHART_MARGIN_LEFT, CHART_MARGIN_TOP + plot_h, CHART_MARGIN_LEFT + plot_w,
          CHART_MARGIN_TOP + plot_h);

  fprintf(out, "</svg>\",\n)\n\n");
}

/**
 * @brief Write a drawdown chart as inline SVG in Typst.
 */
static void write_drawdown_chart(FILE *out, SamrenaVector *equity_curve) {
  if (equity_curve == NULL || samrena_vector_size(equity_curve) < 2) {
    return;
  }

  size_t n = samrena_vector_size(equity_curve);

  /* Compute drawdown at each point */
  double peak = 0.0;
  double max_dd = 0.0;

  /* First pass: find max drawdown for scaling */
  for (size_t i = 0; i < n; i++) {
    const SamtraderEquityPoint *pt =
        (const SamtraderEquityPoint *)samrena_vector_at_unchecked_const(equity_curve, i);
    if (pt->equity > peak)
      peak = pt->equity;
    double dd = (peak > 0.0) ? (peak - pt->equity) / peak : 0.0;
    if (dd > max_dd)
      max_dd = dd;
  }

  /* If no drawdown, show minimal scale */
  if (max_dd < 0.001) {
    max_dd = 0.01;
  }

  const SamtraderEquityPoint *first =
      (const SamtraderEquityPoint *)samrena_vector_at_unchecked_const(equity_curve, 0);
  const SamtraderEquityPoint *last =
      (const SamtraderEquityPoint *)samrena_vector_at_unchecked_const(equity_curve, n - 1);

  double date_min = (double)first->date;
  double date_max = (double)last->date;
  if (date_max - date_min < 1.0) {
    date_max = date_min + 86400.0;
  }

  int plot_w = CHART_SVG_WIDTH - CHART_MARGIN_LEFT - CHART_MARGIN_RIGHT;
  int plot_h = CHART_SVG_HEIGHT - CHART_MARGIN_TOP - CHART_MARGIN_BOTTOM;

  size_t num_points = n;
  if (num_points > MAX_CHART_POINTS) {
    num_points = MAX_CHART_POINTS;
  }

  fprintf(out, "=== Drawdown\n\n");
  fprintf(out, "#image.decode(\n");
  fprintf(out, "  width: 100%%,\n");
  fprintf(out, "  \"<svg xmlns='http://www.w3.org/2000/svg' ");
  fprintf(out, "viewBox='0 0 %d %d'>\n", CHART_SVG_WIDTH, CHART_SVG_HEIGHT);

  /* Background */
  fprintf(out, "<rect width='%d' height='%d' fill='white'/>\n", CHART_SVG_WIDTH, CHART_SVG_HEIGHT);

  /* Horizontal grid lines and Y-axis labels */
  int num_grid = 4;
  for (int i = 0; i <= num_grid; i++) {
    double frac = (double)i / (double)num_grid;
    int y = CHART_MARGIN_TOP + (int)(frac * plot_h);
    double dd_val = frac * max_dd * 100.0;

    fprintf(out, "<line x1='%d' y1='%d' x2='%d' y2='%d' stroke='#e5e7eb' stroke-width='1'/>\n",
            CHART_MARGIN_LEFT, y, CHART_MARGIN_LEFT + plot_w, y);
    fprintf(out,
            "<text x='%d' y='%d' text-anchor='end' font-size='10' "
            "fill='#6b7280' font-family='sans-serif'>-%.1f%%</text>\n",
            CHART_MARGIN_LEFT - 8, y + 4, dd_val);
  }

  /* Build polygon for drawdown area (top edge = 0% line) */
  fprintf(out, "<polygon points='%d,%d ", CHART_MARGIN_LEFT, CHART_MARGIN_TOP);

  peak = 0.0;
  for (size_t i = 0; i < num_points; i++) {
    size_t idx = (n > MAX_CHART_POINTS) ? (i * (n - 1)) / (num_points - 1) : i;
    const SamtraderEquityPoint *pt =
        (const SamtraderEquityPoint *)samrena_vector_at_unchecked_const(equity_curve, idx);

    if (pt->equity > peak)
      peak = pt->equity;
    double dd = (peak > 0.0) ? (peak - pt->equity) / peak : 0.0;

    double x_frac = ((double)pt->date - date_min) / (date_max - date_min);
    double y_frac = dd / max_dd;
    int px = CHART_MARGIN_LEFT + (int)(x_frac * plot_w);
    int py = CHART_MARGIN_TOP + (int)(y_frac * plot_h);
    fprintf(out, "%d,%d ", px, py);
  }

  fprintf(out, "%d,%d' fill='rgba(220,38,38,0.2)' stroke='none'/>\n", CHART_MARGIN_LEFT + plot_w,
          CHART_MARGIN_TOP);

  /* Polyline for drawdown curve */
  fprintf(out, "<polyline points='");
  peak = 0.0;
  for (size_t i = 0; i < num_points; i++) {
    size_t idx = (n > MAX_CHART_POINTS) ? (i * (n - 1)) / (num_points - 1) : i;
    const SamtraderEquityPoint *pt =
        (const SamtraderEquityPoint *)samrena_vector_at_unchecked_const(equity_curve, idx);

    if (pt->equity > peak)
      peak = pt->equity;
    double dd = (peak > 0.0) ? (peak - pt->equity) / peak : 0.0;

    double x_frac = ((double)pt->date - date_min) / (date_max - date_min);
    double y_frac = dd / max_dd;
    int px = CHART_MARGIN_LEFT + (int)(x_frac * plot_w);
    int py = CHART_MARGIN_TOP + (int)(y_frac * plot_h);
    fprintf(out, "%d,%d ", px, py);
  }
  fprintf(out, "' fill='none' stroke='#dc2626' stroke-width='1.5'/>\n");

  /* X-axis date labels */
  int num_x_labels = 5;
  for (int i = 0; i <= num_x_labels; i++) {
    double frac = (double)i / (double)num_x_labels;
    int x = CHART_MARGIN_LEFT + (int)(frac * plot_w);
    time_t t = (time_t)(date_min + frac * (date_max - date_min));
    struct tm *tm_info = localtime(&t);
    char date_label[16];
    strftime(date_label, sizeof(date_label), "%Y-%m", tm_info);

    fprintf(out,
            "<text x='%d' y='%d' text-anchor='middle' font-size='10' "
            "fill='#6b7280' font-family='sans-serif'>%s</text>\n",
            x, CHART_MARGIN_TOP + plot_h + 20, date_label);
  }

  /* Axis border lines */
  fprintf(out, "<line x1='%d' y1='%d' x2='%d' y2='%d' stroke='#d1d5db' stroke-width='1'/>\n",
          CHART_MARGIN_LEFT, CHART_MARGIN_TOP, CHART_MARGIN_LEFT, CHART_MARGIN_TOP + plot_h);
  fprintf(out, "<line x1='%d' y1='%d' x2='%d' y2='%d' stroke='#d1d5db' stroke-width='1'/>\n",
          CHART_MARGIN_LEFT, CHART_MARGIN_TOP + plot_h, CHART_MARGIN_LEFT + plot_w,
          CHART_MARGIN_TOP + plot_h);

  fprintf(out, "</svg>\",\n)\n\n");
}

/* ============================================================================
 * Default Report Generation
 * ============================================================================ */

/**
 * @brief Write the Typst document preamble.
 */
static void write_preamble(FILE *out, const char *strategy_name) {
  fprintf(out, "#set document(title: \"Backtest Report: %s\")\n", strategy_name);
  fprintf(out, "#set page(paper: \"a4\", margin: 2cm)\n");
  fprintf(out, "#set text(font: \"New Computer Modern\", size: 11pt)\n");
  fprintf(out, "\n");
}

/**
 * @brief Write the report title and generation date.
 */
static void write_title(FILE *out, const char *strategy_name) {
  time_t now = time(NULL);
  struct tm *tm_info = localtime(&now);
  char date_buf[32];
  strftime(date_buf, sizeof(date_buf), "%Y-%m-%d", tm_info);

  fprintf(out, "= Backtest Report: %s\n", strategy_name);
  fprintf(out, "\n");
  fprintf(out, "_Generated on %s_\n", date_buf);
  fprintf(out, "\n");
}

/**
 * @brief Write the strategy summary section.
 */
static void write_strategy_summary(FILE *out, SamtraderStrategy *strategy) {
  const char *name = strategy->name ? strategy->name : "Unnamed Strategy";
  const char *desc = strategy->description ? strategy->description : "No description provided.";

  fprintf(out, "== Strategy Summary\n");
  fprintf(out, "\n");
  fprintf(out, "#table(\n");
  fprintf(out, "  columns: (auto, 1fr),\n");
  fprintf(out, "  stroke: none,\n");
  fprintf(out, "  inset: 6pt,\n");
  fprintf(out, "  [*Name*], [%s],\n", name);
  fprintf(out, "  [*Description*], [%s],\n", desc);
  fprintf(out, ")\n");
  fprintf(out, "\n");
}

/**
 * @brief Write the strategy parameters section.
 */
static void write_strategy_parameters(FILE *out, SamtraderStrategy *strategy) {
  fprintf(out, "== Strategy Parameters\n");
  fprintf(out, "\n");
  fprintf(out, "#table(\n");
  fprintf(out, "  columns: (1fr, 1fr),\n");
  fprintf(out, "  inset: 8pt,\n");
  fprintf(out, "  fill: (x, y) => if y == 0 { luma(230) },\n");
  fprintf(out, "  [*Parameter*], [*Value*],\n");
  fprintf(out, "  [Position Size], [%.1f%%],\n", strategy->position_size * 100.0);

  if (strategy->stop_loss_pct > 0.0) {
    fprintf(out, "  [Stop Loss], [%.1f%%],\n", strategy->stop_loss_pct);
  } else {
    fprintf(out, "  [Stop Loss], [None],\n");
  }

  if (strategy->take_profit_pct > 0.0) {
    fprintf(out, "  [Take Profit], [%.1f%%],\n", strategy->take_profit_pct);
  } else {
    fprintf(out, "  [Take Profit], [None],\n");
  }

  fprintf(out, "  [Max Positions], [%d],\n", strategy->max_positions);
  fprintf(out, "  [Long Entry], [%s],\n", strategy->entry_long ? "Defined" : "None");
  fprintf(out, "  [Long Exit], [%s],\n", strategy->exit_long ? "Defined" : "None");
  fprintf(out, "  [Short Entry], [%s],\n", strategy->entry_short ? "Defined" : "None");
  fprintf(out, "  [Short Exit], [%s],\n", strategy->exit_short ? "Defined" : "None");
  fprintf(out, ")\n");
  fprintf(out, "\n");
}

/**
 * @brief Write the performance metrics section.
 */
static void write_performance_metrics(FILE *out, SamtraderBacktestResult *result) {
  fprintf(out, "== Performance Metrics\n");
  fprintf(out, "\n");
  fprintf(out, "=== Return Metrics\n");
  fprintf(out, "\n");
  fprintf(out, "#table(\n");
  fprintf(out, "  columns: (1fr, 1fr),\n");
  fprintf(out, "  inset: 8pt,\n");
  fprintf(out, "  fill: (x, y) => if y == 0 { luma(230) },\n");
  fprintf(out, "  [*Metric*], [*Value*],\n");
  fprintf(out, "  [Total Return], [%.2f%%],\n", result->total_return * 100.0);
  fprintf(out, "  [Annualized Return], [%.2f%%],\n", result->annualized_return * 100.0);
  fprintf(out, "  [Sharpe Ratio], [%.3f],\n", result->sharpe_ratio);
  fprintf(out, "  [Sortino Ratio], [%.3f],\n", result->sortino_ratio);
  fprintf(out, ")\n");
  fprintf(out, "\n");

  fprintf(out, "=== Risk Metrics\n");
  fprintf(out, "\n");
  fprintf(out, "#table(\n");
  fprintf(out, "  columns: (1fr, 1fr),\n");
  fprintf(out, "  inset: 8pt,\n");
  fprintf(out, "  fill: (x, y) => if y == 0 { luma(230) },\n");
  fprintf(out, "  [*Metric*], [*Value*],\n");
  fprintf(out, "  [Max Drawdown], [%.2f%%],\n", result->max_drawdown * 100.0);
  fprintf(out, "  [Max Drawdown Duration], [%.0f days],\n", result->max_drawdown_duration);
  fprintf(out, "  [Profit Factor], [%.2f],\n", result->profit_factor);
  fprintf(out, ")\n");
  fprintf(out, "\n");

  fprintf(out, "=== Trade Statistics\n");
  fprintf(out, "\n");
  fprintf(out, "#table(\n");
  fprintf(out, "  columns: (1fr, 1fr),\n");
  fprintf(out, "  inset: 8pt,\n");
  fprintf(out, "  fill: (x, y) => if y == 0 { luma(230) },\n");
  fprintf(out, "  [*Metric*], [*Value*],\n");
  fprintf(out, "  [Total Trades], [%d],\n", result->total_trades);
  fprintf(out, "  [Winning Trades], [%d],\n", result->winning_trades);
  fprintf(out, "  [Losing Trades], [%d],\n", result->losing_trades);
  fprintf(out, "  [Win Rate], [%.1f%%],\n", result->win_rate * 100.0);
  fprintf(out, "  [Average Win], [\\$%.2f],\n", result->average_win);
  fprintf(out, "  [Average Loss], [\\$%.2f],\n", result->average_loss);
  fprintf(out, "  [Largest Win], [\\$%.2f],\n", result->largest_win);
  fprintf(out, "  [Largest Loss], [\\$%.2f],\n", result->largest_loss);
  fprintf(out, "  [Avg Trade Duration], [%.1f days],\n", result->average_trade_duration);
  fprintf(out, ")\n");
  fprintf(out, "\n");
}

/**
 * @brief Write the trade log section as a Typst table.
 */
static void write_trade_log(FILE *out, SamrenaVector *trades) {
  if (trades == NULL || samrena_vector_size(trades) == 0) {
    return;
  }

  size_t n = samrena_vector_size(trades);

  fprintf(out, "== Trade Log\n");
  fprintf(out, "\n");
  fprintf(out, "#table(\n");
  fprintf(out, "  columns: (auto, auto, auto, auto, auto, auto, auto, auto, auto),\n");
  fprintf(out, "  inset: 8pt,\n");
  fprintf(out, "  fill: (x, y) => if y == 0 { luma(230) },\n");
  fprintf(out, "  [*Symbol*], [*Side*], [*Qty*], [*Entry Price*], [*Exit Price*], "
               "[*Entry Date*], [*Exit Date*], [*Duration*], [*P&L*],\n");

  for (size_t i = 0; i < n; i++) {
    const SamtraderClosedTrade *trade =
        (const SamtraderClosedTrade *)samrena_vector_at_unchecked_const(trades, i);

    const char *symbol = trade->code ? trade->code : "N/A";
    const char *side = (trade->quantity > 0) ? "Long" : "Short";
    int64_t qty = (trade->quantity >= 0) ? trade->quantity : -trade->quantity;

    char entry_date[16];
    char exit_date[16];
    struct tm *tm_info;

    tm_info = localtime(&trade->entry_date);
    strftime(entry_date, sizeof(entry_date), "%Y-%m-%d", tm_info);

    tm_info = localtime(&trade->exit_date);
    strftime(exit_date, sizeof(exit_date), "%Y-%m-%d", tm_info);

    double duration_days = difftime(trade->exit_date, trade->entry_date) / 86400.0;

    if (trade->pnl >= 0.0) {
      fprintf(out,
              "  [%s], [%s], [%lld], [\\$%.2f], [\\$%.2f], [%s], [%s], [%.1f days], "
              "[#text(fill: rgb(\"#16a34a\"))[\\$%.2f]],\n",
              symbol, side, (long long)qty, trade->entry_price, trade->exit_price, entry_date,
              exit_date, duration_days, trade->pnl);
    } else {
      fprintf(out,
              "  [%s], [%s], [%lld], [\\$%.2f], [\\$%.2f], [%s], [%s], [%.1f days], "
              "[#text(fill: rgb(\"#dc2626\"))[\\$%.2f]],\n",
              symbol, side, (long long)qty, trade->entry_price, trade->exit_price, entry_date,
              exit_date, duration_days, trade->pnl);
    }
  }

  fprintf(out, ")\n");
  fprintf(out, "\n");
}

/* ============================================================================
 * Multi-Code Report Sections
 * ============================================================================ */

/**
 * @brief Write a universe summary table with per-code overview.
 */
static void write_universe_summary_table(FILE *out, SamtraderCodeResult *results, size_t count) {
  if (results == NULL || count == 0) {
    return;
  }

  fprintf(out, "== Universe Summary\n");
  fprintf(out, "\n");
  fprintf(out, "#table(\n");
  fprintf(out, "  columns: (auto, auto, auto, auto, auto, auto),\n");
  fprintf(out, "  inset: 8pt,\n");
  fprintf(out, "  fill: (x, y) => if y == 0 { luma(230) },\n");
  fprintf(out, "  [*Code*], [*Trades*], [*Win Rate*], [*Total PnL*], "
               "[*Largest Win*], [*Largest Loss*],\n");

  for (size_t i = 0; i < count; i++) {
    SamtraderCodeResult *cr = &results[i];
    const char *pnl_color = (cr->total_pnl >= 0.0) ? "#16a34a" : "#dc2626";
    fprintf(out,
            "  [%s], [%d], [%.1f%%], "
            "[#text(fill: rgb(\"%s\"))[\\$%.2f]], "
            "[\\$%.2f], [\\$%.2f],\n",
            cr->code, cr->total_trades, cr->win_rate * 100.0, pnl_color, cr->total_pnl,
            cr->largest_win, cr->largest_loss);
  }

  fprintf(out, ")\n");
  fprintf(out, "\n");
}

/**
 * @brief Write a detail section for a single code.
 */
static void write_per_code_detail_section(FILE *out, SamtraderCodeResult *cr,
                                          SamrenaVector *all_trades) {
  if (cr == NULL) {
    return;
  }

  fprintf(out, "== %s Detail\n", cr->code);
  fprintf(out, "\n");

  /* Metrics table */
  fprintf(out, "#table(\n");
  fprintf(out, "  columns: (1fr, 1fr),\n");
  fprintf(out, "  inset: 8pt,\n");
  fprintf(out, "  fill: (x, y) => if y == 0 { luma(230) },\n");
  fprintf(out, "  [*Metric*], [*Value*],\n");
  fprintf(out, "  [Total Trades], [%d],\n", cr->total_trades);
  fprintf(out, "  [Winning Trades], [%d],\n", cr->winning_trades);
  fprintf(out, "  [Losing Trades], [%d],\n", cr->losing_trades);
  fprintf(out, "  [Win Rate], [%.1f%%],\n", cr->win_rate * 100.0);
  fprintf(out, "  [Total PnL], [\\$%.2f],\n", cr->total_pnl);
  fprintf(out, "  [Largest Win], [\\$%.2f],\n", cr->largest_win);
  fprintf(out, "  [Largest Loss], [\\$%.2f],\n", cr->largest_loss);
  fprintf(out, ")\n");
  fprintf(out, "\n");

  /* Filtered trade log */
  if (all_trades == NULL || samrena_vector_size(all_trades) == 0) {
    return;
  }

  size_t n = samrena_vector_size(all_trades);
  bool has_trades = false;

  /* Check if there are any trades for this code */
  for (size_t i = 0; i < n; i++) {
    const SamtraderClosedTrade *trade =
        (const SamtraderClosedTrade *)samrena_vector_at_unchecked_const(all_trades, i);
    if (trade->code && strcmp(trade->code, cr->code) == 0) {
      has_trades = true;
      break;
    }
  }

  if (!has_trades) {
    return;
  }

  fprintf(out, "=== Trades\n");
  fprintf(out, "\n");
  fprintf(out, "#table(\n");
  fprintf(out, "  columns: (auto, auto, auto, auto, auto, auto, auto, auto),\n");
  fprintf(out, "  inset: 8pt,\n");
  fprintf(out, "  fill: (x, y) => if y == 0 { luma(230) },\n");
  fprintf(out, "  [*Side*], [*Qty*], [*Entry Price*], [*Exit Price*], "
               "[*Entry Date*], [*Exit Date*], [*Duration*], [*P&L*],\n");

  for (size_t i = 0; i < n; i++) {
    const SamtraderClosedTrade *trade =
        (const SamtraderClosedTrade *)samrena_vector_at_unchecked_const(all_trades, i);
    if (!trade->code || strcmp(trade->code, cr->code) != 0) {
      continue;
    }

    const char *side = (trade->quantity > 0) ? "Long" : "Short";
    int64_t qty = (trade->quantity >= 0) ? trade->quantity : -trade->quantity;

    char entry_date[16];
    char exit_date[16];
    struct tm *tm_info;

    tm_info = localtime(&trade->entry_date);
    strftime(entry_date, sizeof(entry_date), "%Y-%m-%d", tm_info);

    tm_info = localtime(&trade->exit_date);
    strftime(exit_date, sizeof(exit_date), "%Y-%m-%d", tm_info);

    double duration_days = difftime(trade->exit_date, trade->entry_date) / 86400.0;

    if (trade->pnl >= 0.0) {
      fprintf(out,
              "  [%s], [%lld], [\\$%.2f], [\\$%.2f], [%s], [%s], [%.1f days], "
              "[#text(fill: rgb(\"#16a34a\"))[\\$%.2f]],\n",
              side, (long long)qty, trade->entry_price, trade->exit_price, entry_date, exit_date,
              duration_days, trade->pnl);
    } else {
      fprintf(out,
              "  [%s], [%lld], [\\$%.2f], [\\$%.2f], [%s], [%s], [%.1f days], "
              "[#text(fill: rgb(\"#dc2626\"))[\\$%.2f]],\n",
              side, (long long)qty, trade->entry_price, trade->exit_price, entry_date, exit_date,
              duration_days, trade->pnl);
    }
  }

  fprintf(out, ")\n");
  fprintf(out, "\n");
}

/**
 * @brief Write the full trade log across all codes.
 */
static void write_full_trade_log(FILE *out, SamrenaVector *trades) {
  if (trades == NULL || samrena_vector_size(trades) == 0) {
    return;
  }

  size_t n = samrena_vector_size(trades);

  fprintf(out, "== Full Trade Log\n");
  fprintf(out, "\n");
  fprintf(out, "#table(\n");
  fprintf(out, "  columns: (auto, auto, auto, auto, auto, auto, auto, auto, auto),\n");
  fprintf(out, "  inset: 8pt,\n");
  fprintf(out, "  fill: (x, y) => if y == 0 { luma(230) },\n");
  fprintf(out, "  [*Symbol*], [*Side*], [*Qty*], [*Entry Price*], [*Exit Price*], "
               "[*Entry Date*], [*Exit Date*], [*Duration*], [*P&L*],\n");

  for (size_t i = 0; i < n; i++) {
    const SamtraderClosedTrade *trade =
        (const SamtraderClosedTrade *)samrena_vector_at_unchecked_const(trades, i);

    const char *symbol = trade->code ? trade->code : "N/A";
    const char *side = (trade->quantity > 0) ? "Long" : "Short";
    int64_t qty = (trade->quantity >= 0) ? trade->quantity : -trade->quantity;

    char entry_date[16];
    char exit_date[16];
    struct tm *tm_info;

    tm_info = localtime(&trade->entry_date);
    strftime(entry_date, sizeof(entry_date), "%Y-%m-%d", tm_info);

    tm_info = localtime(&trade->exit_date);
    strftime(exit_date, sizeof(exit_date), "%Y-%m-%d", tm_info);

    double duration_days = difftime(trade->exit_date, trade->entry_date) / 86400.0;

    if (trade->pnl >= 0.0) {
      fprintf(out,
              "  [%s], [%s], [%lld], [\\$%.2f], [\\$%.2f], [%s], [%s], [%.1f days], "
              "[#text(fill: rgb(\"#16a34a\"))[\\$%.2f]],\n",
              symbol, side, (long long)qty, trade->entry_price, trade->exit_price, entry_date,
              exit_date, duration_days, trade->pnl);
    } else {
      fprintf(out,
              "  [%s], [%s], [%lld], [\\$%.2f], [\\$%.2f], [%s], [%s], [%.1f days], "
              "[#text(fill: rgb(\"#dc2626\"))[\\$%.2f]],\n",
              symbol, side, (long long)qty, trade->entry_price, trade->exit_price, entry_date,
              exit_date, duration_days, trade->pnl);
    }
  }

  fprintf(out, ")\n");
  fprintf(out, "\n");
}

/**
 * @brief Write the default multi-code report (no custom template).
 */
static bool write_default_multi_report(SamtraderMultiCodeResult *multi, SamtraderStrategy *strategy,
                                       const char *output_path) {
  FILE *out = fopen(output_path, "w");
  if (out == NULL) {
    return false;
  }

  const char *name = strategy->name ? strategy->name : "Unnamed Strategy";

  write_preamble(out, name);
  write_title(out, name);
  write_strategy_summary(out, strategy);
  write_strategy_parameters(out, strategy);
  write_universe_summary_table(out, multi->code_results, multi->code_count);
  write_performance_metrics(out, &multi->aggregate);
  write_monthly_returns_table(out, multi->aggregate.equity_curve);
  write_equity_curve_chart(out, multi->aggregate.equity_curve);
  write_drawdown_chart(out, multi->aggregate.equity_curve);

  for (size_t i = 0; i < multi->code_count; i++) {
    write_per_code_detail_section(out, &multi->code_results[i], multi->aggregate.trades);
  }

  write_full_trade_log(out, multi->aggregate.trades);

  fclose(out);
  return true;
}

/**
 * @brief Write a multi-code report using a custom Typst template.
 */
static bool write_template_multi_report(const char *template_path, SamtraderMultiCodeResult *multi,
                                        SamtraderStrategy *strategy, const char *output_path) {
  /* Read template file */
  FILE *tmpl_file = fopen(template_path, "r");
  if (tmpl_file == NULL) {
    return false;
  }

  fseek(tmpl_file, 0, SEEK_END);
  long file_size = ftell(tmpl_file);
  fseek(tmpl_file, 0, SEEK_SET);

  if (file_size <= 0 || (size_t)file_size > MAX_TEMPLATE_SIZE) {
    fclose(tmpl_file);
    return false;
  }

  char *template_buf = malloc((size_t)file_size + 1);
  if (template_buf == NULL) {
    fclose(tmpl_file);
    return false;
  }

  size_t bytes_read = fread(template_buf, 1, (size_t)file_size, tmpl_file);
  fclose(tmpl_file);
  template_buf[bytes_read] = '\0';

  /* Open output file */
  FILE *out = fopen(output_path, "w");
  if (out == NULL) {
    free(template_buf);
    return false;
  }

  SamtraderBacktestResult *result = &multi->aggregate;

  /* Process template: scan for {{ }} placeholders */
  const char *pos = template_buf;
  while (*pos != '\0') {
    const char *open = strstr(pos, "{{");
    if (open == NULL) {
      fputs(pos, out);
      break;
    }

    fwrite(pos, 1, (size_t)(open - pos), out);

    const char *key_start = open + 2;
    const char *close_marker = strstr(key_start, "}}");
    if (close_marker == NULL) {
      fputs(open, out);
      break;
    }

    size_t key_len = (size_t)(close_marker - key_start);
    if (key_len >= MAX_KEY_LENGTH) {
      fwrite(open, 1, (size_t)(close_marker + 2 - open), out);
      pos = close_marker + 2;
      continue;
    }

    char key[MAX_KEY_LENGTH];
    memcpy(key, key_start, key_len);
    key[key_len] = '\0';

    /* Handle complex placeholders (multi-KB output) */
    if (strcmp(key, "MONTHLY_RETURNS") == 0) {
      write_monthly_returns_table(out, result->equity_curve);
      pos = close_marker + 2;
      continue;
    }
    if (strcmp(key, "EQUITY_CURVE_CHART") == 0) {
      write_equity_curve_chart(out, result->equity_curve);
      pos = close_marker + 2;
      continue;
    }
    if (strcmp(key, "DRAWDOWN_CHART") == 0) {
      write_drawdown_chart(out, result->equity_curve);
      pos = close_marker + 2;
      continue;
    }
    if (strcmp(key, "TRADE_LOG") == 0) {
      write_trade_log(out, result->trades);
      pos = close_marker + 2;
      continue;
    }
    if (strcmp(key, "UNIVERSE_SUMMARY") == 0) {
      write_universe_summary_table(out, multi->code_results, multi->code_count);
      pos = close_marker + 2;
      continue;
    }
    if (strcmp(key, "PER_CODE_DETAILS") == 0) {
      for (size_t i = 0; i < multi->code_count; i++) {
        write_per_code_detail_section(out, &multi->code_results[i], result->trades);
      }
      pos = close_marker + 2;
      continue;
    }
    if (strcmp(key, "FULL_TRADE_LOG") == 0) {
      write_full_trade_log(out, result->trades);
      pos = close_marker + 2;
      continue;
    }

    /* Resolve scalar placeholder */
    char value[MAX_VALUE_LENGTH];
    if (resolve_placeholder(key, result, strategy, value, sizeof(value))) {
      fputs(value, out);
    } else {
      fwrite(open, 1, (size_t)(close_marker + 2 - open), out);
    }

    pos = close_marker + 2;
  }

  free(template_buf);
  fclose(out);
  return true;
}

/**
 * @brief Write the default report (no custom template).
 */
static bool write_default_report(SamtraderBacktestResult *result, SamtraderStrategy *strategy,
                                 const char *output_path) {
  FILE *out = fopen(output_path, "w");
  if (out == NULL) {
    return false;
  }

  const char *name = strategy->name ? strategy->name : "Unnamed Strategy";

  write_preamble(out, name);
  write_title(out, name);
  write_strategy_summary(out, strategy);
  write_strategy_parameters(out, strategy);
  write_performance_metrics(out, result);
  write_monthly_returns_table(out, result->equity_curve);
  write_equity_curve_chart(out, result->equity_curve);
  write_drawdown_chart(out, result->equity_curve);
  write_trade_log(out, result->trades);

  fclose(out);
  return true;
}

/* ============================================================================
 * Port Interface Implementations
 * ============================================================================ */

static bool typst_report_write(SamtraderReportPort *port, SamtraderBacktestResult *result,
                               SamtraderStrategy *strategy, const char *output_path) {
  if (port == NULL || port->impl == NULL || result == NULL || strategy == NULL ||
      output_path == NULL) {
    return false;
  }

  TypstReportImpl *impl = (TypstReportImpl *)port->impl;

  if (impl->template_path != NULL) {
    return write_template_report(impl->template_path, result, strategy, output_path);
  }

  return write_default_report(result, strategy, output_path);
}

static bool typst_report_write_multi(SamtraderReportPort *port,
                                     SamtraderMultiCodeResult *multi_result,
                                     SamtraderStrategy *strategy, const char *output_path) {
  if (port == NULL || port->impl == NULL || multi_result == NULL || strategy == NULL ||
      output_path == NULL) {
    return false;
  }

  TypstReportImpl *impl = (TypstReportImpl *)port->impl;

  if (impl->template_path != NULL) {
    return write_template_multi_report(impl->template_path, multi_result, strategy, output_path);
  }

  return write_default_multi_report(multi_result, strategy, output_path);
}

static void typst_report_close(SamtraderReportPort *port) {
  if (port == NULL) {
    return;
  }

  /* All memory is arena-allocated, nothing to free manually */
  (void)port;
}

/* ============================================================================
 * Public API
 * ============================================================================ */

SamtraderReportPort *samtrader_typst_adapter_create(Samrena *arena, const char *template_path) {
  if (arena == NULL) {
    return NULL;
  }

  /* Allocate port structure */
  SamtraderReportPort *port = samrena_push(arena, sizeof(SamtraderReportPort));
  if (port == NULL) {
    return NULL;
  }

  /* Allocate implementation data */
  TypstReportImpl *impl = samrena_push(arena, sizeof(TypstReportImpl));
  if (impl == NULL) {
    return NULL;
  }

  /* Copy template path to arena if provided */
  if (template_path != NULL) {
    size_t path_len = strlen(template_path) + 1;
    char *path_copy = samrena_push(arena, path_len);
    if (path_copy == NULL) {
      return NULL;
    }
    memcpy(path_copy, template_path, path_len);
    impl->template_path = path_copy;
  } else {
    impl->template_path = NULL;
  }

  /* Initialize port structure */
  port->impl = impl;
  port->arena = arena;
  port->write = typst_report_write;
  port->write_multi = typst_report_write_multi;
  port->close = typst_report_close;

  return port;
}
