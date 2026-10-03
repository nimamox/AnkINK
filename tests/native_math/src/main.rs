// Compile the production card converter, without Anki/scheduler dependencies.
// This host-only fixture bridge is never packaged on the Kindle.
#[path = "../../../backend/rust/src/math.rs"]
mod math;

use serde_json::Value;
use std::io;

fn main() -> Result<(), Box<dyn std::error::Error>> {
    let mut cases: Value = serde_json::from_reader(io::stdin().lock())?;
    for test in cases.as_array_mut().ok_or("Expected a case array")? {
        let expression = test["expression"].as_str().ok_or("Missing expression")?;
        let source = match test["source"].as_str() {
            Some(source) => source.to_owned(),
            None if test["display"].as_bool() == Some(true) => {
                format!(r"<div>\[{expression}\]</div>")
            }
            None => format!(r"<div>\({expression}\)</div>"),
        };
        test["html"] = Value::String(math::render_card_html(&source));
    }
    serde_json::to_writer(io::stdout().lock(), &cases)?;
    Ok(())
}
