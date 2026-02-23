//! SVG chart rendering for reports (TRD Section 3.9, 11.3).

use crate::domain::portfolio::EquityPoint;

pub fn format_equity_chart(equity_curve: &[EquityPoint]) -> String {
    if equity_curve.is_empty() {
        return "No equity data available.".to_string();
    }

    let min_equity = equity_curve
        .iter()
        .map(|p| p.equity)
        .fold(f64::INFINITY, f64::min);
    let max_equity = equity_curve
        .iter()
        .map(|p| p.equity)
        .fold(f64::NEG_INFINITY, f64::max);

    let width = 500.0;
    let height = 200.0;
    let padding = 40.0;

    let plot_width = width - 2.0 * padding;
    let plot_height = height - 2.0 * padding;

    let range = max_equity - min_equity;
    let scale_y = if range > 0.0 {
        plot_height / range
    } else {
        1.0
    };
    let scale_x = if equity_curve.len() > 1 {
        plot_width / (equity_curve.len() - 1) as f64
    } else {
        0.0
    };

    let points: Vec<String> = equity_curve
        .iter()
        .enumerate()
        .map(|(i, point)| {
            let x = padding + i as f64 * scale_x;
            let y = height - padding - (point.equity - min_equity) * scale_y;
            format!("{:.1},{:.1}", x, y)
        })
        .collect();

    let polyline_points = points.join(" ");

    format!(
        r#"#figure(
  box(
    width: {:.0}pt,
    height: {:.0}pt,
    fill: white,
    {{
      move(dx: {:.0}pt, dy: {:.0}pt, line(length: {:.0}pt, start: (0, 0), end: (0, {:.0}pt)))
      move(dx: {:.0}pt, dy: {:.0}pt, line(length: {:.0}pt, start: (0, 0), end: ({:.0}pt, 0)))
      move(dx: {:.0}pt, dy: {:.0}pt, path(
        fill: none,
        stroke: blue + 1pt,
        ({})
      ))
    }}
  ),
  caption: [Equity Curve]
)
"#,
        width,
        height,
        padding,
        padding,
        plot_height,
        plot_height,
        padding,
        height - padding,
        plot_width,
        plot_width,
        padding,
        padding,
        polyline_points
    )
}

#[cfg(test)]
mod tests {
    use super::*;
    use chrono::NaiveDate;

    fn sample_equity_point(date: &str, equity: f64) -> EquityPoint {
        EquityPoint {
            date: NaiveDate::parse_from_str(date, "%Y-%m-%d").unwrap(),
            equity,
        }
    }

    #[test]
    fn format_empty_equity_curve() {
        let result = format_equity_chart(&[]);
        assert_eq!(result, "No equity data available.");
    }

    #[test]
    fn format_single_point() {
        let curve = vec![sample_equity_point("2024-01-01", 100_000.0)];
        let result = format_equity_chart(&curve);

        assert!(result.contains("#figure"));
        assert!(result.contains("Equity Curve"));
    }

    #[test]
    fn format_multiple_points() {
        let curve = vec![
            sample_equity_point("2024-01-01", 100_000.0),
            sample_equity_point("2024-01-15", 105_000.0),
            sample_equity_point("2024-01-31", 110_000.0),
        ];
        let result = format_equity_chart(&curve);

        assert!(result.contains("#figure"));
        assert!(result.contains("path"));
    }

    #[test]
    fn chart_has_dimensions() {
        let curve = vec![sample_equity_point("2024-01-01", 100_000.0)];
        let result = format_equity_chart(&curve);

        assert!(result.contains("width: 500pt"));
        assert!(result.contains("height: 200pt"));
    }
}
