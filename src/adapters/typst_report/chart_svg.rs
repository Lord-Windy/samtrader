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

    let width = 500.0_f64;
    let height = 200.0_f64;
    let padding = 40.0_f64;

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

    let x_label_start = &equity_curve.first().unwrap().date.to_string();
    let x_label_end = &equity_curve.last().unwrap().date.to_string();
    let y_label_min = format!("{:.0}", min_equity);
    let y_label_max = format!("{:.0}", max_equity);

    let py2 = height - padding;
    let px2 = width - padding;
    let yl_x = padding - 4.0;
    let yl_top = padding + 4.0;
    let yl_bot = height - padding + 4.0;
    let xl_y = height - padding + 16.0;

    let mut svg = String::new();
    svg.push_str(&format!(
        "<svg xmlns=\"http://www.w3.org/2000/svg\" viewBox=\"0 0 {} {}\" width=\"{}\" height=\"{}\">",
        width, height, width, height
    ));
    svg.push_str(&format!(
        "<rect width=\"{}\" height=\"{}\" fill=\"white\"/>",
        width, height
    ));
    svg.push_str(&format!(
        "<line x1=\"{}\" y1=\"{}\" x2=\"{}\" y2=\"{}\" stroke=\"#888\" stroke-width=\"1\"/>",
        padding, padding, padding, py2
    ));
    svg.push_str(&format!(
        "<line x1=\"{}\" y1=\"{}\" x2=\"{}\" y2=\"{}\" stroke=\"#888\" stroke-width=\"1\"/>",
        padding, py2, px2, py2
    ));
    svg.push_str(&format!(
        "<text x=\"{}\" y=\"{}\" font-size=\"10\" text-anchor=\"end\" fill=\"#666\">{}</text>",
        yl_x, yl_top, y_label_max
    ));
    svg.push_str(&format!(
        "<text x=\"{}\" y=\"{}\" font-size=\"10\" text-anchor=\"end\" fill=\"#666\">{}</text>",
        yl_x, yl_bot, y_label_min
    ));
    svg.push_str(&format!(
        "<text x=\"{}\" y=\"{}\" font-size=\"10\" text-anchor=\"start\" fill=\"#666\">{}</text>",
        padding, xl_y, x_label_start
    ));
    svg.push_str(&format!(
        "<text x=\"{}\" y=\"{}\" font-size=\"10\" text-anchor=\"end\" fill=\"#666\">{}</text>",
        px2, xl_y, x_label_end
    ));
    svg.push_str(&format!(
        "<polyline points=\"{}\" fill=\"none\" stroke=\"#2563eb\" stroke-width=\"2\" stroke-linejoin=\"round\"/>",
        polyline_points
    ));
    svg.push_str("</svg>");

    let escaped_svg = svg.replace('\\', "\\\\").replace('"', "\\\"");
    format!(
        "#figure(\n  image.decode(\"{}\"),\n  caption: [Equity Curve]\n)\n",
        escaped_svg
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
    fn format_generates_svg() {
        let curve = vec![
            sample_equity_point("2024-01-01", 100_000.0),
            sample_equity_point("2024-01-15", 105_000.0),
            sample_equity_point("2024-01-31", 110_000.0),
        ];
        let result = format_equity_chart(&curve);

        assert!(result.contains("image.decode"));
        assert!(result.contains("<svg"));
        assert!(result.contains("<polyline"));
    }

    #[test]
    fn chart_includes_axis_labels() {
        let curve = vec![
            sample_equity_point("2024-01-01", 100_000.0),
            sample_equity_point("2024-01-31", 110_000.0),
        ];
        let result = format_equity_chart(&curve);

        assert!(result.contains("100000"));
        assert!(result.contains("110000"));
        assert!(result.contains("2024-01-01"));
        assert!(result.contains("2024-01-31"));
    }
}
