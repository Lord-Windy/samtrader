//! Data access port trait (TRD Section 11.1).

use crate::domain::error::SamtraderError;
use crate::domain::ohlcv::OhlcvBar;
use chrono::NaiveDate;

pub trait DataPort {
    fn fetch_ohlcv(
        &self,
        code: &str,
        exchange: &str,
        start_date: NaiveDate,
        end_date: NaiveDate,
    ) -> Result<Vec<OhlcvBar>, SamtraderError>;

    fn list_symbols(&self, exchange: &str) -> Result<Vec<String>, SamtraderError>;

    fn get_data_range(
        &self,
        code: &str,
        exchange: &str,
    ) -> Result<Option<(NaiveDate, NaiveDate, usize)>, SamtraderError>;
}
