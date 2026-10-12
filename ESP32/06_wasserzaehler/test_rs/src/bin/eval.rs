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
    let mut alarm_data = vec![];
    unsafe {
        psram_buffer_init(0 as *mut u8);
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
        let (ok, predict, alarm) = unsafe {
            let ok = psram_buffer_add(
                result.timestamp as u32,
                r.pointer[0].angle,
                r.pointer[1].angle,
                r.pointer[2].angle,
                r.pointer[3].angle,
                r.pointer[0].row_center2 >>1,
                r.pointer[1].row_center2 >>1,
                r.pointer[2].row_center2 >>1,
                r.pointer[3].row_center2 >>1,
                r.pointer[0].col_center2 >>1,
                r.pointer[1].col_center2 >>1,
                r.pointer[2].col_center2 >>1,
                r.pointer[3].col_center2 >>1,
            );
            let predict = water_consumption();
            let alarm = have_alarm();
            (ok, predict, alarm)
        };
        if ok != 0 {
            err_data.push((result.timestamp, r));
        } else {
            predict_data.push((result.timestamp, predict));
            if alarm != 0 {
                alarm_data.push((result.timestamp, alarm));
            }
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

    let root_area = BitMapBackend::new("eval.png", (800, 600)).into_drawing_area();
    root_area.fill(&WHITE)?;

    let (left, right) = root_area.split_horizontally(400);
    let (top_left, bottom_left) = left.split_vertically(300);
    let (top_right, bottom_right) = right.split_vertically(300);

    let mut top_left_chart = ChartBuilder::on(&top_left)
        .caption(
            "Rohzeiger 0.1m3 als Winkel °",
            ("sans-serif", 20).into_font(),
        )
        .margin(5)
        .x_label_area_size(30)
        .y_label_area_size(30)
        .build_cartesian_2d(from_t as i64..to_t as i64, 0..360)?;

    top_left_chart.configure_mesh().draw()?;

    top_left_chart.draw_series(
        err_data
            .into_iter()
            .map(|(t, r)| Circle::new((t as i64, r.pointer[0].angle as i32), 5, RED.filled())),
    )?;

    top_left_chart
        .draw_series(LineSeries::new(
            data_0.into_iter().map(|(t, a)| (t as i64, a as i32)),
            &BLACK,
        ))?
        .label("0.1")
        .legend(|(x, y)| PathElement::new(vec![(x, y), (x + 20, y)], &RED));

    if false {
        top_left_chart
            .draw_series(LineSeries::new(
                data_1.into_iter().map(|(t, a)| (t as i64, a as i32)),
                &BLUE,
            ))?
            .label("0.01")
            .legend(|(x, y)| PathElement::new(vec![(x, y), (x + 20, y)], &RED));

        top_left_chart
            .draw_series(LineSeries::new(
                data_2.into_iter().map(|(t, a)| (t as i64, a as i32)),
                &GREEN,
            ))?
            .label("0.001")
            .legend(|(x, y)| PathElement::new(vec![(x, y), (x + 20, y)], &RED));
    }
    top_left_chart
        .configure_series_labels()
        .background_style(&WHITE.mix(0.8))
        .border_style(&BLACK)
        .draw()?;

    let y_max: i32 = predict_data.iter().map(|(_, p)| *p as i32).max().unwrap();
    let mut bottom_left_chart = ChartBuilder::on(&bottom_left)
        .caption("kumulierter Verbrauch", ("sans-serif", 20).into_font())
        .margin(5)
        .x_label_area_size(30)
        .y_label_area_size(50)
        .build_cartesian_2d(from_t as i64..to_t as i64, 0..y_max)?;

    bottom_left_chart.configure_mesh().draw()?;

    bottom_left_chart.draw_series(
        predict_data
            .into_iter()
            .map(|(t, p)| Circle::new((t as i64, p as i32), 2, BLUE.filled())),
    )?;

    bottom_left_chart.draw_series(
        alarm_data
            .into_iter()
            .map(|(t, a)| Circle::new((t as i64, a as i32 * 500), 5, RED.filled())),
    )?;

    bottom_left_chart
        .configure_series_labels()
        .background_style(&WHITE.mix(0.8))
        .border_style(&BLACK)
        .draw()?;

    //let y_max: i32 = results.results.iter().map(|e| e.r.pointer[0].row_center2 as i32).max().unwrap();
    let y_max = 3 * 240;
    let mut top_right_chart = ChartBuilder::on(&top_right)
        .caption("Position", ("sans-serif", 20).into_font())
        .margin(5)
        .x_label_area_size(30)
        .y_label_area_size(50)
        .build_cartesian_2d(from_t as i64..to_t as i64, 0..y_max)?;

    top_right_chart.configure_mesh().draw()?;

    top_right_chart.draw_series(results.results.iter().map(|e| {
        Circle::new(
            (e.timestamp as i64, e.r.pointer[0].row_center2 as i32),
            2,
            BLUE.filled(),
        )
    }))?;

    top_right_chart.draw_series(results.results.iter().map(|e| {
        Circle::new(
            (e.timestamp as i64, e.r.pointer[3].row_center2 as i32),
            2,
            BLACK.filled(),
        )
    }))?;

    top_right_chart
        .configure_series_labels()
        .background_style(&WHITE.mix(0.8))
        .border_style(&BLACK)
        .draw()?;

    let mut bottom_right_chart = ChartBuilder::on(&bottom_right)
        .caption(
            "Winkel Zeiger 0->3 und 1->2 in °",
            ("sans-serif", 20).into_font(),
        )
        .margin(5)
        .x_label_area_size(30)
        .y_label_area_size(50)
        .build_cartesian_2d(from_t as i64..to_t as i64, -90..-70)?;

    bottom_right_chart.configure_mesh().draw()?;

    bottom_right_chart.draw_series(results.results.iter().map(|e| {
        let dy = e.r.pointer[2].row_center2 as i32 - e.r.pointer[1].row_center2 as i32;
        let dx = e.r.pointer[2].col_center2 as i32 - e.r.pointer[1].col_center2 as i32;
        let a = (dy as f64).atan2(dx as f64);
        let a = (a * 180.0 / std::f64::consts::PI).round() as i32;
        Circle::new((e.timestamp as i64, a as i32), 2, BLACK.filled())
    }))?;

    bottom_right_chart.draw_series(results.results.iter().map(|e| {
        let dy = e.r.pointer[3].row_center2 as i32 - e.r.pointer[0].row_center2 as i32;
        let dx = e.r.pointer[3].col_center2 as i32 - e.r.pointer[0].col_center2 as i32;
        let a = (dy as f64).atan2(dx as f64);
        let a = (a * 180.0 / std::f64::consts::PI).round() as i32;
        Circle::new((e.timestamp as i64, a as i32), 2, BLUE.filled())
    }))?;

    bottom_right_chart
        .configure_series_labels()
        .background_style(&WHITE.mix(0.8))
        .border_style(&BLACK)
        .draw()?;

    Ok(())
}
