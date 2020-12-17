use std::fs::File;
use std::io::BufReader;

use test_rs::*;

fn main() -> std::io::Result<()> {
    let file = File::open("all.log")?;
    let reader = BufReader::new(file);

    let results: Result = serde_json::from_reader(reader)?;

    for result in results.results {
        let r = &result.r;
        println!("{} r={}  {} {} {} {}", result.timestamp,
            r.radius2,
            r.pointer[0].angle,
            r.pointer[1].angle,
            r.pointer[2].angle,
            r.pointer[3].angle,
            );
    }
    Ok(())
}
