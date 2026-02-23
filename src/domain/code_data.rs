//! CodeData struct and unified timeline (TRD Section 8.1/8.2).

use crate::domain::indicator::{IndicatorSeries, IndicatorType};
use crate::domain::ohlcv::OhlcvBar;
use chrono::NaiveDate;
use std::collections::{BTreeSet, HashMap};

#[derive(Debug, Clone)]
pub struct CodeData {
    pub code: String,
    pub exchange: String,
    pub ohlcv: Vec<OhlcvBar>,
    pub indicators: HashMap<IndicatorType, IndicatorSeries>,
    pub date_index: HashMap<NaiveDate, usize>,
}

impl CodeData {
    pub fn new(code: String, exchange: String, ohlcv: Vec<OhlcvBar>) -> Self {
        let date_index = ohlcv
            .iter()
            .enumerate()
            .map(|(i, bar)| (bar.date, i))
            .collect();
        Self {
            code,
            exchange,
            ohlcv,
            indicators: HashMap::new(),
            date_index,
        }
    }

    pub fn bar_count(&self) -> usize {
        self.ohlcv.len()
    }

    pub fn get_bar(&self, date: NaiveDate) -> Option<&OhlcvBar> {
        self.date_index.get(&date).map(|&i| &self.ohlcv[i])
    }

    pub fn get_bar_index(&self, date: NaiveDate) -> Option<usize> {
        self.date_index.get(&date).copied()
    }
}

pub fn build_unified_timeline(codes: &[CodeData]) -> Vec<NaiveDate> {
    let unique_dates: BTreeSet<NaiveDate> = codes
        .iter()
        .flat_map(|cd| cd.ohlcv.iter().map(|bar| bar.date))
        .collect();
    unique_dates.into_iter().collect()
}

#[cfg(test)]
mod tests {
    use super::*;

    fn make_bar(code: &str, date: &str, close: f64) -> OhlcvBar {
        OhlcvBar {
            code: code.to_string(),
            exchange: "ASX".to_string(),
            date: NaiveDate::parse_from_str(date, "%Y-%m-%d").unwrap(),
            open: close - 1.0,
            high: close + 1.0,
            low: close - 2.0,
            close,
            volume: 1000,
        }
    }

    #[test]
    fn code_data_new_builds_date_index() {
        let bars = vec![
            make_bar("BHP", "2024-01-01", 100.0),
            make_bar("BHP", "2024-01-02", 101.0),
            make_bar("BHP", "2024-01-03", 102.0),
        ];
        let cd = CodeData::new("BHP".into(), "ASX".into(), bars);

        assert_eq!(cd.date_index.len(), 3);
        assert_eq!(
            cd.date_index
                .get(&NaiveDate::from_ymd_opt(2024, 1, 1).unwrap()),
            Some(&0)
        );
        assert_eq!(
            cd.date_index
                .get(&NaiveDate::from_ymd_opt(2024, 1, 2).unwrap()),
            Some(&1)
        );
        assert_eq!(
            cd.date_index
                .get(&NaiveDate::from_ymd_opt(2024, 1, 3).unwrap()),
            Some(&2)
        );
    }

    #[test]
    fn code_data_get_bar() {
        let bars = vec![
            make_bar("BHP", "2024-01-01", 100.0),
            make_bar("BHP", "2024-01-02", 101.0),
        ];
        let cd = CodeData::new("BHP".into(), "ASX".into(), bars);

        let bar = cd.get_bar(NaiveDate::from_ymd_opt(2024, 1, 2).unwrap());
        assert!(bar.is_some());
        assert!((bar.unwrap().close - 101.0).abs() < f64::EPSILON);

        assert!(
            cd.get_bar(NaiveDate::from_ymd_opt(2024, 1, 5).unwrap())
                .is_none()
        );
    }

    #[test]
    fn unified_timeline_merges_and_sorts() {
        let bhp = CodeData::new(
            "BHP".into(),
            "ASX".into(),
            vec![
                make_bar("BHP", "2024-01-02", 100.0),
                make_bar("BHP", "2024-01-05", 101.0),
            ],
        );
        let rio = CodeData::new(
            "RIO".into(),
            "ASX".into(),
            vec![
                make_bar("RIO", "2024-01-01", 50.0),
                make_bar("RIO", "2024-01-03", 51.0),
            ],
        );

        let timeline = build_unified_timeline(&[bhp, rio]);

        assert_eq!(timeline.len(), 4);
        assert_eq!(timeline[0], NaiveDate::from_ymd_opt(2024, 1, 1).unwrap());
        assert_eq!(timeline[1], NaiveDate::from_ymd_opt(2024, 1, 2).unwrap());
        assert_eq!(timeline[2], NaiveDate::from_ymd_opt(2024, 1, 3).unwrap());
        assert_eq!(timeline[3], NaiveDate::from_ymd_opt(2024, 1, 5).unwrap());
    }

    #[test]
    fn unified_timeline_empty_codes() {
        let timeline = build_unified_timeline(&[]);
        assert!(timeline.is_empty());
    }

    #[test]
    fn unified_timeline_single_code() {
        let bhp = CodeData::new(
            "BHP".into(),
            "ASX".into(),
            vec![
                make_bar("BHP", "2024-01-03", 100.0),
                make_bar("BHP", "2024-01-01", 99.0),
            ],
        );

        let timeline = build_unified_timeline(&[bhp]);

        assert_eq!(timeline.len(), 2);
        assert_eq!(timeline[0], NaiveDate::from_ymd_opt(2024, 1, 1).unwrap());
        assert_eq!(timeline[1], NaiveDate::from_ymd_opt(2024, 1, 3).unwrap());
    }
}
