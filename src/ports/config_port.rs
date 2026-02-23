//! Configuration access port trait (TRD Section 11.2).

pub trait ConfigPort {
    fn get_string(&self, section: &str, key: &str) -> Option<String>;
    fn get_int(&self, section: &str, key: &str, default: i64) -> i64;
    fn get_double(&self, section: &str, key: &str, default: f64) -> f64;
    fn get_bool(&self, section: &str, key: &str, default: bool) -> bool;
}
