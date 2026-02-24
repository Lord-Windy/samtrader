//! HTML templates using Askama.

use askama::Template;
use chrono::NaiveDate;

use crate::domain::metrics::{CodeResult, Metrics};
use crate::domain::position::ClosedTrade;
use crate::domain::strategy::Strategy;

#[derive(Template)]
#[template(path = "dashboard.html")]
pub struct DashboardTemplate<'a> {
    pub recent_backtests: &'a [&'a str],
}

impl<'a> DashboardTemplate<'a> {
    pub fn fragment(&self) -> String {
        let mut html =
            String::from("<div id=\"content\"><h1>Dashboard</h1><p>Welcome to Samtrader</p>");
        if self.recent_backtests.is_empty() {
            html.push_str("<p>No recent backtests</p>");
        }
        html.push_str("</div>");
        html
    }
}

#[derive(Template)]
#[template(path = "backtest_form.html")]
pub struct BacktestFormTemplate<'a> {
    pub symbols: &'a [String],
    pub default_start: &'a str,
    pub default_end: &'a str,
}

impl<'a> BacktestFormTemplate<'a> {
    pub fn fragment(&self) -> String {
        let mut html = String::from("<div id=\"content\"><h1>New Backtest</h1>");
        html.push_str("<form hx-post=\"/backtest/run\" hx-target=\".report-container\">");
        html.push_str(
            "<label>Codes: <input name=\"codes\" placeholder=\"BHP,CBA,RIO\"></label><br>",
        );
        html.push_str(&format!(
            "<label>Start: <input type=\"date\" name=\"start_date\" value=\"{}\"></label><br>",
            self.default_start
        ));
        html.push_str(&format!(
            "<label>End: <input type=\"date\" name=\"end_date\" value=\"{}\"></label><br>",
            self.default_end
        ));
        html.push_str(
            "<label>Initial Capital: <input name=\"initial_capital\" value=\"100000\"></label><br>",
        );
        html.push_str("<label>Entry Rule: <input name=\"entry_rule\" placeholder=\"close &gt; sma(20)\"></label><br>");
        html.push_str("<label>Exit Rule: <input name=\"exit_rule\" placeholder=\"close &lt; sma(20)\"></label><br>");
        html.push_str(
            "<label>Position Size: <input name=\"position_size\" value=\"0.25\"></label><br>",
        );
        html.push_str(
            "<label>Max Positions: <input name=\"max_positions\" value=\"1\"></label><br>",
        );
        html.push_str("<button type=\"submit\">Run Backtest</button>");
        html.push_str("</form>");
        html.push_str("<div class=\"report-container\"></div>");
        html.push_str("</div>");
        html
    }
}

#[derive(Template)]
#[template(path = "report.html")]
pub struct ReportTemplate<'a> {
    pub strategy: &'a Strategy,
    pub metrics: &'a Metrics,
    pub code_results: Option<&'a [CodeResult]>,
    pub equity_svg: &'a str,
    pub drawdown_svg: &'a str,
    pub trades: &'a [ClosedTrade],
    pub skipped: &'a [SkippedCode<'a>],
    pub start_date: NaiveDate,
    pub end_date: NaiveDate,
    pub initial_capital: f64,
}

pub struct SkippedCode<'a> {
    pub code: &'a str,
    pub reason: &'a str,
}

impl<'a> ReportTemplate<'a> {
    pub fn fragment(&self) -> String {
        let mut html = String::from("<div id=\"report-content\">");

        html.push_str("<h1>Backtest Report</h1>");

        html.push_str("<h2>Strategy</h2>");
        html.push_str(&format!(
            "<p><strong>Name:</strong> {}</p>",
            self.strategy.name
        ));
        html.push_str(&format!(
            "<p><strong>Position Size:</strong> {:.0}%</p>",
            self.strategy.position_size * 100.0
        ));
        html.push_str(&format!(
            "<p><strong>Max Positions:</strong> {}</p>",
            self.strategy.max_positions
        ));

        html.push_str("<h2>Metrics</h2>");
        html.push_str("<table>");
        html.push_str(&format!(
            "<tr><td>Total Return</td><td>{:.2}%</td></tr>",
            self.metrics.total_return * 100.0
        ));
        html.push_str(&format!(
            "<tr><td>Annualized Return</td><td>{:.2}%</td></tr>",
            self.metrics.annualized_return * 100.0
        ));
        html.push_str(&format!(
            "<tr><td>Sharpe Ratio</td><td>{:.2}</td></tr>",
            self.metrics.sharpe_ratio
        ));
        html.push_str(&format!(
            "<tr><td>Sortino Ratio</td><td>{:.2}</td></tr>",
            self.metrics.sortino_ratio
        ));
        html.push_str(&format!(
            "<tr><td>Max Drawdown</td><td>-{:.1}%</td></tr>",
            self.metrics.max_drawdown * 100.0
        ));
        html.push_str(&format!(
            "<tr><td>Total Trades</td><td>{}</td></tr>",
            self.metrics.total_trades
        ));
        html.push_str(&format!(
            "<tr><td>Win Rate</td><td>{:.1}%</td></tr>",
            self.metrics.win_rate * 100.0
        ));
        html.push_str(&format!(
            "<tr><td>Profit Factor</td><td>{:.2}</td></tr>",
            self.metrics.profit_factor
        ));
        html.push_str("</table>");

        html.push_str("<h2>Equity Chart</h2>");
        html.push_str(&format!("<div class=\"chart\">{}</div>", self.equity_svg));

        html.push_str("<h2>Drawdown Chart</h2>");
        html.push_str(&format!("<div class=\"chart\">{}</div>", self.drawdown_svg));

        if !self.trades.is_empty() {
            html.push_str("<h2>Trade Log</h2>");
            html.push_str("<table>");
            html.push_str(
                "<tr><th>Code</th><th>Entry</th><th>Exit</th><th>Qty</th><th>PnL</th></tr>",
            );
            for trade in self.trades {
                html.push_str(&format!(
                    "<tr><td>{}</td><td>{}</td><td>{}</td><td>{}</td><td>{:.0}</td></tr>",
                    trade.code, trade.entry_date, trade.exit_date, trade.quantity, trade.pnl
                ));
            }
            html.push_str("</table>");
        }

        if let Some(results) = self.code_results {
            if !results.is_empty() {
                html.push_str("<h2>Per-Code Results</h2>");
                html.push_str("<table>");
                html.push_str("<tr><th>Code</th><th>Trades</th><th>Win Rate</th><th>PnL</th></tr>");
                for cr in results {
                    html.push_str(&format!(
                        "<tr><td>{}</td><td>{}</td><td>{:.1}%</td><td>{:.0}</td></tr>",
                        cr.code,
                        cr.total_trades,
                        cr.win_rate * 100.0,
                        cr.total_pnl
                    ));
                }
                html.push_str("</table>");
            }
        }

        html.push_str("</div>");
        html
    }
}

#[derive(Template)]
#[template(path = "error.html")]
pub struct ErrorTemplate<'a> {
    pub message: &'a str,
    pub status: u16,
}

impl<'a> ErrorTemplate<'a> {
    pub fn fragment(&self) -> String {
        format!(
            "<div id=\"error\" class=\"error\"><h1>Error {}</h1><p>{}</p></div>",
            self.status, self.message
        )
    }
}

#[derive(Template)]
#[template(path = "report_placeholder.html")]
pub struct ReportPlaceholderTemplate;

impl ReportPlaceholderTemplate {
    pub fn fragment(&self) -> String {
        String::from("<div id=\"content\"><p>No report loaded. Run a backtest first.</p></div>")
    }
}
