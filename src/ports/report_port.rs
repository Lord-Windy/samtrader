//! Report generation port trait (TRD Section 2.2, 11.3).

use crate::domain::backtest::{BacktestResult, MultiCodeResult};
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

    /// Default implementation: falls back to `write` using only the aggregate result.
    fn write_multi(
        &self,
        result: &MultiCodeResult,
        strategy: &Strategy,
        output_path: &str,
    ) -> Result<(), SamtraderError> {
        self.write(&result.aggregate, strategy, output_path)
    }
}
