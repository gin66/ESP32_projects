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

#[repr(C)]
#[derive(Debug, Default)]
pub struct Entry {
    pub timestamp: u32,
    pub angle: [u8;4],
    pub row_center: [u16;4],
    pub col_center: [u16;4],
    pad: u32,
    overwritten: u32
}

extern "C" {
    pub fn digitize(image: *const u8, digitized: *mut u8, first: u8);
    pub fn find_pointer(digitized: *const u8, filtered: *mut u8, temp: *mut u8, r: *mut Reader);
    pub fn eval_pointer(digitized: *const u8, r: *mut Reader);

    pub fn psram_buffer_init(buffer: *mut u8);
    pub fn psram_buffer_add(
        timestamp: u32,
        angle0: u16,
        angle1: u16,
        angle2: u16,
        angle3: u16,
        row_center0: i16,
        row_center1: i16,
        row_center2: i16,
        row_center3: i16,
        col_center0: i16,
        col_center1: i16,
        col_center2: i16,
        col_center3: i16,
    ) -> i8;
    pub fn water_consumption() -> u16;
    pub fn have_alarm() -> u8;
    pub fn num_entries() -> u16;
    pub fn get_entry(i: u16) -> *const Entry;
}
