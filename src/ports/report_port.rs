//! Report generation port trait (TRD Section 2.2, 11.3).

use crate::domain::backtest::BacktestResult;
use crate::domain::error::SamtraderError;
use crate::domain::strategy::Strategy;

/// Port for writing backtest reports.
pub trait ReportPort {
    fn write(
        &self,
        result: &BacktestResult,
        strategy: &Strategy,
        output_path: &str,
    ) -> Result<(), SamtraderError>;
}
