use std::fs::File;
use std::io::BufReader;

use plotters::prelude::*;

use test_rs::*;

fn main() -> std::result::Result<(), Box<dyn std::error::Error>> {
    let file = File::open("all.log")?;
    let reader = BufReader::new(file);

    let results: Result = serde_json::from_reader(reader)?;

    let mut rtc_buffer = [0u8; 8192];
    unsafe {
        rtc_ram_buffer_init(rtc_buffer.as_mut_ptr());
    }
    for result in results.results.iter() {
        let r = &result.r;
        println!(
            "{} r={}  {} {} {} {}",
            result.timestamp,
            r.radius2,
            r.pointer[0].angle,
            r.pointer[1].angle,
            r.pointer[2].angle,
            r.pointer[3].angle,
        );
        unsafe {
            rtc_ram_buffer_add(
                rtc_buffer.as_mut_ptr(),
                result.timestamp as u32,
                r.pointer[0].angle,
                r.pointer[1].angle,
                r.pointer[2].angle,
                r.pointer[3].angle,
            );
        }
    }

    let mut data_0 = vec![];
    let mut data_1 = vec![];
    let mut data_2 = vec![];
    let mut data_3 = vec![];
    for result in results.results.iter() {
        let r = &result.r;
        println!(
            "{} r={}  {} {} {} {}",
            result.timestamp,
            r.radius2,
            r.pointer[0].angle,
            r.pointer[1].angle,
            r.pointer[2].angle,
            r.pointer[3].angle,
        );
        data_0.push((result.timestamp, r.pointer[0].angle));
        data_1.push((result.timestamp, r.pointer[1].angle));
        data_2.push((result.timestamp, r.pointer[2].angle));
        data_3.push((result.timestamp, r.pointer[3].angle));
    }

    let from_t = results.results[0].timestamp;
    let to_t = results.results[results.results.len() - 1].timestamp;

    let root = BitMapBackend::new("eval.png", (2500, 2000)).into_drawing_area();
    root.fill(&WHITE)?;
    let mut chart = ChartBuilder::on(&root)
        .caption("over t", ("sans-serif", 50).into_font())
        .margin(5)
        .x_label_area_size(30)
        .y_label_area_size(30)
        .build_cartesian_2d(from_t as i64..to_t as i64, 0..360)?;

    chart.configure_mesh().draw()?;

    chart
        .draw_series(LineSeries::new(
            data_0.into_iter().map(|(t, a)| (t as i64, a as i32)),
            &RED,
        ))?
        .label("0.1")
        .legend(|(x, y)| PathElement::new(vec![(x, y), (x + 20, y)], &RED));

    chart
        .draw_series(LineSeries::new(
            data_1.into_iter().map(|(t, a)| (t as i64, a as i32)),
            &BLUE,
        ))?
        .label("0.01")
        .legend(|(x, y)| PathElement::new(vec![(x, y), (x + 20, y)], &RED));

    chart
        .draw_series(LineSeries::new(
            data_3.into_iter().map(|(t, a)| (t as i64, a as i32)),
            &BLUE,
        ))?
        .label("0.001")
        .legend(|(x, y)| PathElement::new(vec![(x, y), (x + 20, y)], &RED));

    chart
        .configure_series_labels()
        .background_style(&WHITE.mix(0.8))
        .border_style(&BLACK)
        .draw()?;

    Ok(())
}
