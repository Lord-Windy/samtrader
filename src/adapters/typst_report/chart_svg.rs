//! SVG chart rendering for reports (TRD Section 3.9).
//!
//! Generates inline SVG strings for equity curve and drawdown charts,
//! suitable for embedding in Typst via `#image.decode(...)`.

use crate::domain::portfolio::EquityPoint;

const CHART_WIDTH: f64 = 600.0;
const CHART_HEIGHT: f64 = 300.0;
const MARGIN_LEFT: f64 = 60.0;
const MARGIN_RIGHT: f64 = 20.0;
const MARGIN_TOP: f64 = 30.0;
const MARGIN_BOTTOM: f64 = 40.0;

fn fmt_currency(value: f64) -> String {
    if value >= 0.0 {
        format!("${:.2}", value)
    } else {
        format!("-${:.2}", value.abs())
    }
}

/// Generate an inline SVG line chart of portfolio equity over time.
///
/// Returns an empty string if the curve is empty.
pub fn generate_equity_svg(equity_curve: &[EquityPoint]) -> String {
    if equity_curve.is_empty() {
        return String::new();
    }

    let min_equity = equity_curve
        .iter()
        .map(|p| p.equity)
        .fold(f64::INFINITY, f64::min);
    let max_equity = equity_curve
        .iter()
        .map(|p| p.equity)
        .fold(f64::NEG_INFINITY, f64::max);
    let range = (max_equity - min_equity).max(1.0);

    let plot_width = CHART_WIDTH - MARGIN_LEFT - MARGIN_RIGHT;
    let plot_height = CHART_HEIGHT - MARGIN_TOP - MARGIN_BOTTOM;

    let x_scale = |i: usize| -> f64 {
        MARGIN_LEFT + (i as f64 / (equity_curve.len() - 1).max(1) as f64) * plot_width
    };
    let y_scale =
        |v: f64| -> f64 { MARGIN_TOP + plot_height - ((v - min_equity) / range) * plot_height };

    let mut path_data = String::new();
    for (i, point) in equity_curve.iter().enumerate() {
        let x = x_scale(i);
        let y = y_scale(point.equity);
        if i == 0 {
            path_data.push_str(&format!("M {x:.1} {y:.1}"));
        } else {
            path_data.push_str(&format!(" L {x:.1} {y:.1}"));
        }
    }

    let start_date = equity_curve.first().unwrap().date;
    let end_date = equity_curve.last().unwrap().date;
    let mid_date = if equity_curve.len() > 1 {
        equity_curve[equity_curve.len() / 2].date
    } else {
        start_date
    };

    let mut svg = String::new();
    svg.push_str(&format!(
        r##"<svg width="{CHART_WIDTH}" height="{CHART_HEIGHT}" viewBox="0 0 {CHART_WIDTH} {CHART_HEIGHT}" xmlns="http://www.w3.org/2000/svg">"##,
    ));
    svg.push_str("\n  <rect width=\"100%\" height=\"100%\" fill=\"white\"/>\n");
    // Title
    svg.push_str(&format!(
        "  <text x=\"{CHART_WIDTH}\" y=\"15\" text-anchor=\"end\" font-size=\"12\" fill=\"#666\">Equity ($)</text>\n",
    ));
    // Axes
    svg.push_str(&format!(
        "  <line x1=\"{MARGIN_LEFT}\" y1=\"{MARGIN_TOP}\" x2=\"{MARGIN_LEFT}\" y2=\"{}\" stroke=\"#ccc\" stroke-width=\"1\"/>\n",
        CHART_HEIGHT - MARGIN_BOTTOM
    ));
    svg.push_str(&format!(
        "  <line x1=\"{MARGIN_LEFT}\" y1=\"{}\" x2=\"{}\" y2=\"{}\" stroke=\"#ccc\" stroke-width=\"1\"/>\n",
        CHART_HEIGHT - MARGIN_BOTTOM,
        CHART_WIDTH - MARGIN_RIGHT,
        CHART_HEIGHT - MARGIN_BOTTOM
    ));
    // Y-axis labels
    svg.push_str(&format!(
        "  <text x=\"{}\" y=\"{}\" text-anchor=\"end\" font-size=\"10\" fill=\"#666\">{}</text>\n",
        MARGIN_LEFT - 5.0,
        MARGIN_TOP + 5.0,
        fmt_currency(max_equity)
    ));
    svg.push_str(&format!(
        "  <text x=\"{}\" y=\"{}\" text-anchor=\"end\" font-size=\"10\" fill=\"#666\">{}</text>\n",
        MARGIN_LEFT - 5.0,
        MARGIN_TOP + plot_height / 2.0,
        fmt_currency((max_equity + min_equity) / 2.0)
    ));
    svg.push_str(&format!(
        "  <text x=\"{}\" y=\"{}\" text-anchor=\"end\" font-size=\"10\" fill=\"#666\">{}</text>\n",
        MARGIN_LEFT - 5.0,
        CHART_HEIGHT - MARGIN_BOTTOM - 5.0,
        fmt_currency(min_equity)
    ));
    // X-axis labels
    svg.push_str(&format!(
        "  <text x=\"{MARGIN_LEFT}\" y=\"{CHART_HEIGHT}\" text-anchor=\"middle\" font-size=\"10\" fill=\"#666\">{start_date}</text>\n",
    ));
    svg.push_str(&format!(
        "  <text x=\"{}\" y=\"{CHART_HEIGHT}\" text-anchor=\"middle\" font-size=\"10\" fill=\"#666\">{mid_date}</text>\n",
        MARGIN_LEFT + plot_width / 2.0,
    ));
    svg.push_str(&format!(
        "  <text x=\"{}\" y=\"{CHART_HEIGHT}\" text-anchor=\"middle\" font-size=\"10\" fill=\"#666\">{end_date}</text>\n",
        CHART_WIDTH - MARGIN_RIGHT,
    ));
    // Line
    svg.push_str(&format!(
        "  <path d=\"{path_data}\" fill=\"none\" stroke=\"#2563eb\" stroke-width=\"2\"/>\n",
    ));
    svg.push_str("</svg>");
    svg
}

/// Generate an inline SVG area chart of drawdown percentage over time.
///
/// Returns an empty string if the curve has fewer than 2 points.
pub fn generate_drawdown_svg(equity_curve: &[EquityPoint]) -> String {
    if equity_curve.len() < 2 {
        return String::new();
    }

    let drawdowns = compute_drawdown_series(equity_curve);
    let max_dd = drawdowns.iter().cloned().fold(0.0, f64::max).max(0.01);

    let plot_width = CHART_WIDTH - MARGIN_LEFT - MARGIN_RIGHT;
    let plot_height = CHART_HEIGHT - MARGIN_TOP - MARGIN_BOTTOM;

    let x_scale = |i: usize| -> f64 {
        MARGIN_LEFT + (i as f64 / (drawdowns.len() - 1).max(1) as f64) * plot_width
    };
    let y_scale = |dd: f64| -> f64 { MARGIN_TOP + (dd / max_dd) * plot_height };

    let mut path_data = format!("M {:.1} {:.1}", x_scale(0), y_scale(0.0));
    for (i, &dd) in drawdowns.iter().enumerate() {
        if i > 0 {
            path_data.push_str(&format!(" L {:.1} {:.1}", x_scale(i), y_scale(dd)));
        }
    }
    path_data.push_str(&format!(
        " L {:.1} {:.1} L {:.1} {:.1} Z",
        x_scale(drawdowns.len() - 1),
        y_scale(0.0),
        x_scale(0),
        y_scale(0.0)
    ));

    let start_date = equity_curve.first().unwrap().date;
    let end_date = equity_curve.last().unwrap().date;
    let mid_date = if equity_curve.len() > 1 {
        equity_curve[equity_curve.len() / 2].date
    } else {
        start_date
    };

    let mut svg = String::new();
    svg.push_str(&format!(
        r##"<svg width="{CHART_WIDTH}" height="{CHART_HEIGHT}" viewBox="0 0 {CHART_WIDTH} {CHART_HEIGHT}" xmlns="http://www.w3.org/2000/svg">"##,
    ));
    svg.push_str("\n  <rect width=\"100%\" height=\"100%\" fill=\"white\"/>\n");
    svg.push_str(&format!(
        "  <text x=\"{CHART_WIDTH}\" y=\"15\" text-anchor=\"end\" font-size=\"12\" fill=\"#666\">Drawdown (%)</text>\n",
    ));
    // Axes
    svg.push_str(&format!(
        "  <line x1=\"{MARGIN_LEFT}\" y1=\"{MARGIN_TOP}\" x2=\"{MARGIN_LEFT}\" y2=\"{}\" stroke=\"#ccc\" stroke-width=\"1\"/>\n",
        CHART_HEIGHT - MARGIN_BOTTOM
    ));
    svg.push_str(&format!(
        "  <line x1=\"{MARGIN_LEFT}\" y1=\"{}\" x2=\"{}\" y2=\"{}\" stroke=\"#ccc\" stroke-width=\"1\"/>\n",
        CHART_HEIGHT - MARGIN_BOTTOM,
        CHART_WIDTH - MARGIN_RIGHT,
        CHART_HEIGHT - MARGIN_BOTTOM
    ));
    // Y-axis labels
    svg.push_str(&format!(
        "  <text x=\"{}\" y=\"{}\" text-anchor=\"end\" font-size=\"10\" fill=\"#666\">0%</text>\n",
        MARGIN_LEFT - 5.0,
        MARGIN_TOP + 5.0
    ));
    svg.push_str(&format!(
        "  <text x=\"{}\" y=\"{}\" text-anchor=\"end\" font-size=\"10\" fill=\"#666\">-{:.1}%</text>\n",
        MARGIN_LEFT - 5.0,
        MARGIN_TOP + plot_height / 2.0,
        max_dd * 50.0
    ));
    svg.push_str(&format!(
        "  <text x=\"{}\" y=\"{}\" text-anchor=\"end\" font-size=\"10\" fill=\"#666\">-{:.1}%</text>\n",
        MARGIN_LEFT - 5.0,
        CHART_HEIGHT - MARGIN_BOTTOM - 5.0,
        max_dd * 100.0
    ));
    // X-axis labels
    svg.push_str(&format!(
        "  <text x=\"{MARGIN_LEFT}\" y=\"{CHART_HEIGHT}\" text-anchor=\"middle\" font-size=\"10\" fill=\"#666\">{start_date}</text>\n",
    ));
    svg.push_str(&format!(
        "  <text x=\"{}\" y=\"{CHART_HEIGHT}\" text-anchor=\"middle\" font-size=\"10\" fill=\"#666\">{mid_date}</text>\n",
        MARGIN_LEFT + plot_width / 2.0,
    ));
    svg.push_str(&format!(
        "  <text x=\"{}\" y=\"{CHART_HEIGHT}\" text-anchor=\"middle\" font-size=\"10\" fill=\"#666\">{end_date}</text>\n",
        CHART_WIDTH - MARGIN_RIGHT,
    ));
    // Area fill
    svg.push_str(&format!(
        "  <path d=\"{path_data}\" fill=\"rgba(239,68,68,0.3)\" stroke=\"#dc2626\" stroke-width=\"1\"/>\n",
    ));
    svg.push_str("</svg>");
    svg
}

pub fn compute_drawdown_series(equity_curve: &[EquityPoint]) -> Vec<f64> {
    let mut drawdowns = Vec::with_capacity(equity_curve.len());
    let mut peak = equity_curve[0].equity;

    for point in equity_curve {
        if point.equity > peak {
            peak = point.equity;
        }
        let dd = if peak > 0.0 {
            (peak - point.equity) / peak
        } else {
            0.0
        };
        drawdowns.push(dd);
    }

    drawdowns
}

#[cfg(test)]
mod tests {
    use super::*;
    use chrono::NaiveDate;

    #[test]
    fn equity_svg_empty_curve() {
        let curve: Vec<EquityPoint> = vec![];
        assert!(generate_equity_svg(&curve).is_empty());
    }

    #[test]
    fn equity_svg_single_point() {
        let curve = vec![EquityPoint {
            date: NaiveDate::from_ymd_opt(2024, 1, 1).unwrap(),
            equity: 100_000.0,
        }];
        let svg = generate_equity_svg(&curve);
        assert!(svg.contains("<svg"));
        assert!(svg.contains("100000"));
    }

    #[test]
    fn equity_svg_multiple_points() {
        let curve = vec![
            EquityPoint {
                date: NaiveDate::from_ymd_opt(2024, 1, 1).unwrap(),
                equity: 100_000.0,
            },
            EquityPoint {
                date: NaiveDate::from_ymd_opt(2024, 1, 2).unwrap(),
                equity: 105_000.0,
            },
            EquityPoint {
                date: NaiveDate::from_ymd_opt(2024, 1, 3).unwrap(),
                equity: 102_000.0,
            },
        ];
        let svg = generate_equity_svg(&curve);
        assert!(svg.contains("<path"));
        assert!(svg.contains("stroke=\"#2563eb\""));
    }

    #[test]
    fn drawdown_svg_empty_curve() {
        let curve: Vec<EquityPoint> = vec![];
        assert!(generate_drawdown_svg(&curve).is_empty());
    }

    #[test]
    fn drawdown_svg_single_point() {
        let curve = vec![EquityPoint {
            date: NaiveDate::from_ymd_opt(2024, 1, 1).unwrap(),
            equity: 100_000.0,
        }];
        assert!(generate_drawdown_svg(&curve).is_empty());
    }

    #[test]
    fn drawdown_svg_multiple_points() {
        let curve = vec![
            EquityPoint {
                date: NaiveDate::from_ymd_opt(2024, 1, 1).unwrap(),
                equity: 100_000.0,
            },
            EquityPoint {
                date: NaiveDate::from_ymd_opt(2024, 1, 2).unwrap(),
                equity: 95_000.0,
            },
            EquityPoint {
                date: NaiveDate::from_ymd_opt(2024, 1, 3).unwrap(),
                equity: 98_000.0,
            },
        ];
        let svg = generate_drawdown_svg(&curve);
        assert!(svg.contains("<path"));
        assert!(svg.contains("fill=\"rgba(239,68,68,0.3)\""));
    }

    #[test]
    fn drawdown_series_zero_drawdown() {
        let curve = vec![
            EquityPoint {
                date: NaiveDate::from_ymd_opt(2024, 1, 1).unwrap(),
                equity: 100_000.0,
            },
            EquityPoint {
                date: NaiveDate::from_ymd_opt(2024, 1, 2).unwrap(),
                equity: 110_000.0,
            },
            EquityPoint {
                date: NaiveDate::from_ymd_opt(2024, 1, 3).unwrap(),
                equity: 120_000.0,
            },
        ];
        let dd = compute_drawdown_series(&curve);
        assert!(dd.iter().all(|&d| d == 0.0));
    }

    #[test]
    fn drawdown_series_with_drawdown() {
        let curve = vec![
            EquityPoint {
                date: NaiveDate::from_ymd_opt(2024, 1, 1).unwrap(),
                equity: 100_000.0,
            },
            EquityPoint {
                date: NaiveDate::from_ymd_opt(2024, 1, 2).unwrap(),
                equity: 90_000.0,
            },
            EquityPoint {
                date: NaiveDate::from_ymd_opt(2024, 1, 3).unwrap(),
                equity: 95_000.0,
            },
        ];
        let dd = compute_drawdown_series(&curve);
        assert!((dd[0] - 0.0).abs() < 1e-9);
        assert!((dd[1] - 0.10).abs() < 1e-9);
        assert!((dd[2] - 0.05).abs() < 1e-9);
    }
}
