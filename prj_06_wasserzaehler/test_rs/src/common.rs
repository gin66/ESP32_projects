use serde::{Deserialize, Serialize};

#[repr(C)]
#[derive(Debug, Default, Serialize, Deserialize)]
pub struct Pointer {
    pub row_from: i16,
    pub row_to: i16,
    pub row_center2: i16,
    pub col_from: i16,
    pub col_to: i16,
    pub col_center2: i16,
    pub angle: u16,
}

#[repr(C)]
#[derive(Debug, Default, Serialize, Deserialize)]
pub struct Reader {
    pub candidates: u8,
    pub radius2: i16,
    pub pointer: [Pointer; 6],
}

#[derive(Debug, Default, Serialize, Deserialize)]
pub struct TimedResult {
    pub timestamp: i64,
    pub r: Reader,
}

#[derive(Debug, Default, Serialize, Deserialize)]
pub struct Result {
    pub results: Vec<TimedResult>,
}
