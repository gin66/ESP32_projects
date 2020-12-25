use std::fs::File;
use std::io::Read;

use plotters::prelude::*;

use test_rs::*;

fn main() -> std::result::Result<(), Box<dyn std::error::Error>> {
    let mut file = File::open("../dump")?;
    let mut data = vec![];
    file.read_to_end(&mut data)?;

    let mut err_data = vec![];
    unsafe {
        psram_buffer_init(data.as_mut_ptr());
    }

    let n = unsafe { num_entries()};
    println!("entries={}", n);

    let mut entries = vec![];
    unsafe {
        for i in 0..n {
            entries.push(get_entry(i));
        }
    }

    let from_t = unsafe { (*entries[0]).timestamp };
    let to_t = unsafe { (*entries[entries.len()-1]).timestamp };
    err_data.push((1,1));

    let root_area = BitMapBackend::new("from_dump.png", (800, 600)).into_drawing_area();
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

    unsafe {
    top_left_chart.draw_series(
        entries
            .iter()
            .map(|&e| Circle::new(((*e).timestamp as i64, (*e).angle[0] as i32), 2, BLUE.filled())),
    )? };

    top_left_chart
        .draw_series(LineSeries::new(
            err_data.into_iter().map(|(t, a)| (t as i64, a as i32)),
            &BLACK,
        ))?
        .label("0.1")
        .legend(|(x, y)| PathElement::new(vec![(x, y), (x + 20, y)], &RED));

    top_left_chart
        .configure_series_labels()
        .background_style(&WHITE.mix(0.8))
        .border_style(&BLACK)
        .draw()?;

    Ok(())
}
