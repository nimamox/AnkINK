// ABI regression probe only; never included in application libraries.
unsafe extern "C" {
    fn c_f32(a: f32, b: f32) -> f32;
    fn c_f64(a: f64, b: f64) -> f64;
}
#[unsafe(no_mangle)]
pub extern "C" fn rust_f32(a: f32, b: f32) -> f32 {
    a / b + a * b
}
#[unsafe(no_mangle)]
pub extern "C" fn rust_f64(a: f64, b: f64) -> f64 {
    a / b + a * b
}
#[unsafe(no_mangle)]
pub unsafe extern "C" fn rust_calls_c32(a: f32, b: f32) -> f32 {
    unsafe { c_f32(a, b) }
}
#[unsafe(no_mangle)]
pub unsafe extern "C" fn rust_calls_c64(a: f64, b: f64) -> f64 {
    unsafe { c_f64(a, b) }
}
