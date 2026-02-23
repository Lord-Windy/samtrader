//! Default Typst report template (TRD Section 3.9, 11.3).

pub fn get_default_template() -> &'static str {
    r##"#set page(
  paper: "a4",
  margin: (x: 2cm, y: 2cm),
)

#set text(font: "Liberation Serif", size: 11pt)

#align(center)[
  #text(size: 24pt, weight: "bold")[Backtest Report]
]

#v(1em)

== Strategy: {{STRATEGY_NAME}}

{{STRATEGY_DESCRIPTION}}

== Summary

| Metric | Value |
| ------ | ----- |
| Initial Capital | ${{INITIAL_CAPITAL}} |
| Final Cash | ${{FINAL_CASH}} |
| Total P&L | ${{TOTAL_PNL}} |
| Return | {{RETURN_PCT}}% |
| Trade Count | {{TRADE_COUNT}} |
| Win Rate | {{WIN_RATE}}% |

== Trades

{{TRADES_TABLE}}

== Equity Curve

{{EQUITY_CHART}}

== Per-Code Breakdown

{{PER_CODE_SUMMARY}}
"##
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn default_template_contains_placeholders() {
        let template = get_default_template();
        assert!(template.contains("{{STRATEGY_NAME}}"));
        assert!(template.contains("{{INITIAL_CAPITAL}}"));
        assert!(template.contains("{{FINAL_CASH}}"));
        assert!(template.contains("{{TOTAL_PNL}}"));
        assert!(template.contains("{{TRADES_TABLE}}"));
        assert!(template.contains("{{EQUITY_CHART}}"));
    }

    #[test]
    fn default_template_is_valid_typst() {
        let template = get_default_template();
        assert!(template.starts_with("#set page"));
        assert!(template.contains("#align(center)"));
    }
}
