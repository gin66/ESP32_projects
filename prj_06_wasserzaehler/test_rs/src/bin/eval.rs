use std::fs::File;
use std::io::BufReader;

use plotters::prelude::*;

use test_rs::*;

fn main() -> std::result::Result<(), Box<dyn std::error::Error>> {
    let file = File::open("all.log")?;
    let reader = BufReader::new(file);

    let results: Result = serde_json::from_reader(reader)?;

    let mut err_data = vec![];
    let mut predict_data = vec![];
    let mut rtc_buffer = [0u8; 8192];
    unsafe {
        rtc_ram_buffer_init(rtc_buffer.as_mut_ptr());
    }
    for result in results.results.iter() {
        let r = &result.r;
        if false {
        println!(
            "{} r={}  {} {} {} {}",
            result.timestamp,
            r.radius2,
            r.pointer[0].angle,
            r.pointer[1].angle,
            r.pointer[2].angle,
            r.pointer[3].angle,
        );
        }
        let (ok,predict) = unsafe {
            let ok = rtc_ram_buffer_add(
                rtc_buffer.as_mut_ptr(),
                result.timestamp as u32,
                r.pointer[0].angle,
                r.pointer[1].angle,
                r.pointer[2].angle,
                r.pointer[3].angle,
            );
            let predict = water_consumption(rtc_buffer.as_mut_ptr());
            (ok, predict)
        };
        if ok != 0 {
            err_data.push((result.timestamp, r));
        }
        else {
            predict_data.push((result.timestamp, predict));
        }
    }

    let mut data_0 = vec![];
    let mut data_1 = vec![];
    let mut data_2 = vec![];
    let mut data_3 = vec![];
    for result in results.results.iter() {
        let r = &result.r;
        data_0.push((result.timestamp, r.pointer[0].angle));
        data_1.push((result.timestamp, r.pointer[1].angle));
        data_2.push((result.timestamp, r.pointer[2].angle));
        data_3.push((result.timestamp, r.pointer[3].angle));
    }

    let from_t = results.results[0].timestamp;
    let to_t = results.results[results.results.len() - 1].timestamp;

    let root_area = BitMapBackend::new("eval.png", (2500, 2000)).into_drawing_area();
    root_area.fill(&WHITE)?;

    let (upper, lower) = root_area.split_vertically(1000);

    let mut upper_chart = ChartBuilder::on(&upper)
        .caption("over t", ("sans-serif", 50).into_font())
        .margin(5)
        .x_label_area_size(30)
        .y_label_area_size(30)
        .build_cartesian_2d(from_t as i64..to_t as i64, 0..360)?;

    upper_chart.configure_mesh().draw()?;

    upper_chart
        .draw_series(
            err_data.into_iter().map(|(t, r)| Circle::new((t as i64, r.pointer[0].angle as i32), 5, RED.filled())),
        )?;

    upper_chart
        .draw_series(LineSeries::new(
            data_0.into_iter().map(|(t, a)| (t as i64, a as i32)),
            &BLACK,
        ))?
        .label("0.1")
        .legend(|(x, y)| PathElement::new(vec![(x, y), (x + 20, y)], &RED));

    if false {
    upper_chart
        .draw_series(LineSeries::new(
            data_1.into_iter().map(|(t, a)| (t as i64, a as i32)),
            &BLUE,
        ))?
        .label("0.01")
        .legend(|(x, y)| PathElement::new(vec![(x, y), (x + 20, y)], &RED));

    upper_chart
        .draw_series(LineSeries::new(
            data_2.into_iter().map(|(t, a)| (t as i64, a as i32)),
            &GREEN,
        ))?
        .label("0.001")
        .legend(|(x, y)| PathElement::new(vec![(x, y), (x + 20, y)], &RED));
    }
    upper_chart
        .configure_series_labels()
        .background_style(&WHITE.mix(0.8))
        .border_style(&BLACK)
        .draw()?;


    let y_max: i32 = predict_data.iter().map(|(_,p)| *p as i32).max().unwrap();
    let mut lower_chart = ChartBuilder::on(&lower)
        .caption("over t", ("sans-serif", 50).into_font())
        .margin(5)
        .x_label_area_size(30)
        .y_label_area_size(30)
        .build_cartesian_2d(from_t as i64..to_t as i64, 0..y_max)?;

    lower_chart.configure_mesh().draw()?;

    lower_chart
        .draw_series(
            predict_data.into_iter().map(|(t, p)| Circle::new((t as i64, p as i32), 4, BLUE.filled())),
        )?;

    lower_chart
        .configure_series_labels()
        .background_style(&WHITE.mix(0.8))
        .border_style(&BLACK)
        .draw()?;

    Ok(())
}
