//! SVG chart rendering for reports (TRD §12.2).

use crate::domain::portfolio::EquityPoint;
use chrono::NaiveDate;

const SVG_WIDTH: f64 = 800.0;
const SVG_HEIGHT: f64 = 400.0;
const MARGIN_LEFT: f64 = 70.0;
const MARGIN_RIGHT: f64 = 30.0;
const MARGIN_TOP: f64 = 30.0;
const MARGIN_BOTTOM: f64 = 50.0;
const GRID_COLOR: &str = "#e0e0e0";
const AXIS_COLOR: &str = "#333333";
const LINE_COLOR: &str = "#2563eb";
const AREA_COLOR: &str = "rgba(37, 99, 235, 0.3)";
const TEXT_COLOR: &str = "#666666";

fn chart_width() -> f64 {
    SVG_WIDTH - MARGIN_LEFT - MARGIN_RIGHT
}

fn chart_height() -> f64 {
    SVG_HEIGHT - MARGIN_TOP - MARGIN_BOTTOM
}

pub fn generate_equity_curve_svg(equity_curve: &[EquityPoint]) -> String {
    if equity_curve.is_empty() {
        return empty_chart("Equity Curve", "No data available");
    }

    let min_equity = equity_curve
        .iter()
        .map(|p| p.equity)
        .fold(f64::INFINITY, f64::min);
    let max_equity = equity_curve
        .iter()
        .map(|p| p.equity)
        .fold(f64::NEG_INFINITY, f64::max);

    let y_range = if (max_equity - min_equity).abs() < f64::EPSILON {
        min_equity.abs().max(1.0)
    } else {
        max_equity - min_equity
    };
    let y_padding = y_range * 0.1;
    let y_min = (min_equity - y_padding).min(0.0);
    let y_max = max_equity + y_padding;

    let dates: Vec<NaiveDate> = equity_curve.iter().map(|p| p.date).collect();
    let cw = chart_width();
    let ch = chart_height();

    let mut svg = String::new();
    svg.push_str(&svg_header("Equity Curve"));
    svg.push_str(&chart_frame());

    svg.push_str(&y_axis_labels(y_min, y_max, 5, "$"));
    svg.push_str(&x_axis_labels(&dates, 6));

    let points: Vec<(f64, f64)> = equity_curve
        .iter()
        .enumerate()
        .map(|(i, p)| {
            let x = MARGIN_LEFT + (i as f64 / (equity_curve.len() - 1).max(1) as f64) * cw;
            let y = MARGIN_TOP + ch - ((p.equity - y_min) / (y_max - y_min)) * ch;
            (x, y)
        })
        .collect();

    svg.push_str(&line_path(&points, LINE_COLOR, 2.0));

    let start_y = MARGIN_TOP + ch;
    svg.push_str(&area_path(&points, start_y));

    svg.push_str(&y_axis_label("Equity ($)"));
    svg.push_str(&x_axis_label("Date"));

    svg.push_str("</svg>");
    svg
}

pub fn generate_drawdown_svg(equity_curve: &[EquityPoint]) -> String {
    if equity_curve.is_empty() {
        return empty_chart("Drawdown", "No data available");
    }

    let drawdowns: Vec<f64> = compute_drawdown_series(equity_curve);
    let max_dd = drawdowns.iter().cloned().fold(0.0_f64, f64::max);

    let y_max = (max_dd * 1.1).max(0.01);

    let dates: Vec<NaiveDate> = equity_curve.iter().map(|p| p.date).collect();
    let cw = chart_width();
    let ch = chart_height();

    let mut svg = String::new();
    svg.push_str(&svg_header("Drawdown"));
    svg.push_str(&chart_frame());

    svg.push_str(&y_axis_labels_drawdown(y_max, 5));
    svg.push_str(&x_axis_labels(&dates, 6));

    // Drawdown hangs downward from 0% at the top of the chart area
    let points: Vec<(f64, f64)> = drawdowns
        .iter()
        .enumerate()
        .map(|(i, &dd)| {
            let x = MARGIN_LEFT + (i as f64 / (drawdowns.len() - 1).max(1) as f64) * cw;
            let y = MARGIN_TOP + (dd / y_max) * ch;
            (x, y)
        })
        .collect();

    let baseline_y = MARGIN_TOP;
    svg.push_str(&drawdown_area_path(&points, baseline_y));

    svg.push_str(&y_axis_label("Drawdown (%)"));
    svg.push_str(&x_axis_label("Date"));

    svg.push_str("</svg>");
    svg
}

fn compute_drawdown_series(equity_curve: &[EquityPoint]) -> Vec<f64> {
    if equity_curve.is_empty() {
        return Vec::new();
    }

    let mut peak = equity_curve[0].equity;
    let mut drawdowns = Vec::with_capacity(equity_curve.len());

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

fn svg_header(title: &str) -> String {
    format!(
        r#"<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 {} {}" width="{}" height="{}">
<rect width="100%" height="100%" fill="white"/>
<text x="{}" y="20" font-family="system-ui, sans-serif" font-size="16" font-weight="600" fill="{}">{}</text>
"#,
        SVG_WIDTH,
        SVG_HEIGHT,
        SVG_WIDTH,
        SVG_HEIGHT,
        SVG_WIDTH / 2.0,
        AXIS_COLOR,
        title
    )
}

fn chart_frame() -> String {
    let cw = chart_width();
    let ch = chart_height();
    format!(
        r#"<rect x="{}" y="{}" width="{}" height="{}" fill="none" stroke="{}" stroke-width="1"/>
"#,
        MARGIN_LEFT, MARGIN_TOP, cw, ch, AXIS_COLOR
    )
}

fn format_number(value: f64) -> String {
    let formatted = format!("{:.0}", value.abs());
    let bytes: Vec<u8> = formatted.bytes().collect();
    let mut with_commas = String::new();
    for (i, &b) in bytes.iter().enumerate() {
        if i > 0 && (bytes.len() - i) % 3 == 0 {
            with_commas.push(',');
        }
        with_commas.push(b as char);
    }
    if value < 0.0 {
        format!("-{}", with_commas)
    } else {
        with_commas
    }
}

fn y_axis_labels(min: f64, max: f64, count: usize, prefix: &str) -> String {
    let ch = chart_height();
    let mut result = String::new();

    for i in 0..=count {
        let value = min + (max - min) * (i as f64 / count as f64);
        let y = MARGIN_TOP + ch - (i as f64 / count as f64) * ch;

        result.push_str(&format!(
            r#"<line x1="{}" y1="{}" x2="{}" y2="{}" stroke="{}" stroke-dasharray="3,3"/>
<text x="{}" y="{}" font-family="system-ui, sans-serif" font-size="11" fill="{}" text-anchor="end" dominant-baseline="middle">{}{}</text>
"#,
            MARGIN_LEFT,
            y,
            MARGIN_LEFT + chart_width(),
            y,
            GRID_COLOR,
            MARGIN_LEFT - 8.0,
            y,
            TEXT_COLOR,
            prefix,
            format_number(value),
        ));
    }

    result
}

fn y_axis_labels_drawdown(max_dd: f64, count: usize) -> String {
    let ch = chart_height();
    let mut result = String::new();

    for i in 0..=count {
        let frac = i as f64 / count as f64;
        let dd_pct = max_dd * frac * 100.0;
        let y = MARGIN_TOP + frac * ch;

        result.push_str(&format!(
            r#"<line x1="{}" y1="{}" x2="{}" y2="{}" stroke="{}" stroke-dasharray="3,3"/>
<text x="{}" y="{}" font-family="system-ui, sans-serif" font-size="11" fill="{}" text-anchor="end" dominant-baseline="middle">-{:.0}%</text>
"#,
            MARGIN_LEFT,
            y,
            MARGIN_LEFT + chart_width(),
            y,
            GRID_COLOR,
            MARGIN_LEFT - 8.0,
            y,
            TEXT_COLOR,
            dd_pct,
        ));
    }

    result
}

fn x_axis_labels(dates: &[NaiveDate], count: usize) -> String {
    if dates.is_empty() {
        return String::new();
    }

    let cw = chart_width();
    let ch = chart_height();
    let mut result = String::new();

    let step = (dates.len() - 1).max(1) as f64 / count as f64;

    for i in 0..=count {
        let idx = ((i as f64 * step).round() as usize).min(dates.len() - 1);
        let x = MARGIN_LEFT + (idx as f64 / (dates.len() - 1).max(1) as f64) * cw;
        let date_str = dates[idx].format("%Y-%m-%d").to_string();

        result.push_str(&format!(
            r#"<line x1="{}" y1="{}" x2="{}" y2="{}" stroke="{}" stroke-dasharray="3,3"/>
<text x="{}" y="{}" font-family="system-ui, sans-serif" font-size="10" fill="{}" text-anchor="middle">{}</text>
"#,
            x,
            MARGIN_TOP,
            x,
            MARGIN_TOP + ch,
            GRID_COLOR,
            x,
            MARGIN_TOP + ch + 20.0,
            TEXT_COLOR,
            date_str
        ));
    }

    result
}

fn line_path(points: &[(f64, f64)], color: &str, width: f64) -> String {
    if points.is_empty() {
        return String::new();
    }

    let mut d = format!("M {} {}", points[0].0, points[0].1);
    for point in &points[1..] {
        d.push_str(&format!(" L {} {}", point.0, point.1));
    }

    format!(
        r#"<path d="{}" fill="none" stroke="{}" stroke-width="{}" stroke-linecap="round" stroke-linejoin="round"/>
"#,
        d, color, width
    )
}

fn area_path(points: &[(f64, f64)], baseline_y: f64) -> String {
    if points.is_empty() {
        return String::new();
    }

    let mut d = format!("M {} {}", points[0].0, baseline_y);
    for point in points {
        d.push_str(&format!(" L {} {}", point.0, point.1));
    }
    d.push_str(&format!(" L {} {} Z", points.last().unwrap().0, baseline_y));

    format!(
        r#"<path d="{}" fill="{}"/>
"#,
        d, AREA_COLOR
    )
}

fn drawdown_area_path(points: &[(f64, f64)], baseline_y: f64) -> String {
    if points.is_empty() {
        return String::new();
    }

    let drawdown_color = "rgba(239, 68, 68, 0.4)";
    let drawdown_stroke = "#ef4444";

    let mut d = format!("M {} {}", points[0].0, baseline_y);
    for point in points {
        d.push_str(&format!(" L {} {}", point.0, point.1));
    }
    d.push_str(&format!(" L {} {} Z", points.last().unwrap().0, baseline_y));

    let mut result = format!(
        r#"<path d="{}" fill="{}"/>
"#,
        d, drawdown_color
    );

    let mut line_d = format!("M {} {}", points[0].0, points[0].1);
    for point in &points[1..] {
        line_d.push_str(&format!(" L {} {}", point.0, point.1));
    }
    result.push_str(&format!(
        r#"<path d="{}" fill="none" stroke="{}" stroke-width="1.5"/>
"#,
        line_d, drawdown_stroke
    ));

    result
}

fn y_axis_label(label: &str) -> String {
    format!(
        r#"<text x="15" y="{}" font-family="system-ui, sans-serif" font-size="12" fill="{}" text-anchor="middle" transform="rotate(-90, 15, {})">{}</text>
"#,
        MARGIN_TOP + chart_height() / 2.0,
        AXIS_COLOR,
        MARGIN_TOP + chart_height() / 2.0,
        label
    )
}

fn x_axis_label(label: &str) -> String {
    format!(
        r#"<text x="{}" y="{}" font-family="system-ui, sans-serif" font-size="12" fill="{}" text-anchor="middle">{}</text>
"#,
        MARGIN_LEFT + chart_width() / 2.0,
        SVG_HEIGHT - 8.0,
        AXIS_COLOR,
        label
    )
}

fn empty_chart(title: &str, message: &str) -> String {
    format!(
        r#"<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 {} {}" width="{}" height="{}">
<rect width="100%" height="100%" fill="white"/>
<text x="{}" y="20" font-family="system-ui, sans-serif" font-size="16" font-weight="600" fill="{}" text-anchor="middle">{}</text>
<text x="{}" y="{}" font-family="system-ui, sans-serif" font-size="14" fill="{}" text-anchor="middle">{}</text>
</svg>"#,
        SVG_WIDTH,
        SVG_HEIGHT,
        SVG_WIDTH,
        SVG_HEIGHT,
        SVG_WIDTH / 2.0,
        AXIS_COLOR,
        title,
        SVG_WIDTH / 2.0,
        SVG_HEIGHT / 2.0,
        TEXT_COLOR,
        message
    )
}

#[cfg(test)]
mod tests {
    use super::*;

    fn make_point(date: &str, equity: f64) -> EquityPoint {
        EquityPoint {
            date: NaiveDate::parse_from_str(date, "%Y-%m-%d").unwrap(),
            equity,
        }
    }

    #[test]
    fn test_empty_equity_curve() {
        let svg = generate_equity_curve_svg(&[]);
        assert!(svg.contains("No data available"));
        assert!(svg.contains("</svg>"));
    }

    #[test]
    fn test_single_point_equity_curve() {
        let curve = vec![make_point("2024-01-01", 100_000.0)];
        let svg = generate_equity_curve_svg(&curve);
        assert!(svg.contains("Equity Curve"));
        assert!(svg.contains("</svg>"));
    }

    #[test]
    fn test_multi_point_equity_curve() {
        let curve = vec![
            make_point("2024-01-01", 100_000.0),
            make_point("2024-01-02", 105_000.0),
            make_point("2024-01-03", 102_000.0),
            make_point("2024-01-04", 108_000.0),
        ];
        let svg = generate_equity_curve_svg(&curve);
        assert!(svg.contains("<path"));
        assert!(svg.contains("</svg>"));
    }

    #[test]
    fn test_empty_drawdown() {
        let svg = generate_drawdown_svg(&[]);
        assert!(svg.contains("No data available"));
        assert!(svg.contains("</svg>"));
    }

    #[test]
    fn test_drawdown_calculation() {
        let curve = vec![
            make_point("2024-01-01", 100_000.0),
            make_point("2024-01-02", 95_000.0),
            make_point("2024-01-03", 90_000.0),
            make_point("2024-01-04", 105_000.0),
            make_point("2024-01-05", 100_000.0),
        ];
        let dd = compute_drawdown_series(&curve);

        assert!((dd[0] - 0.0).abs() < f64::EPSILON);
        assert!((dd[1] - 0.05).abs() < 1e-10);
        assert!((dd[2] - 0.10).abs() < 1e-10);
        assert!((dd[3] - 0.0).abs() < f64::EPSILON);
        assert!((dd[4] - 0.047619).abs() < 1e-4);
    }

    #[test]
    fn test_svg_contains_axes() {
        let curve = vec![
            make_point("2024-01-01", 100_000.0),
            make_point("2024-01-02", 105_000.0),
        ];
        let svg = generate_equity_curve_svg(&curve);
        assert!(svg.contains("Equity ($)"));
        assert!(svg.contains("Date"));
    }

    #[test]
    fn test_svg_contains_gridlines() {
        let curve = vec![
            make_point("2024-01-01", 100_000.0),
            make_point("2024-01-02", 105_000.0),
        ];
        let svg = generate_equity_curve_svg(&curve);
        assert!(svg.contains("stroke-dasharray"));
    }
}
