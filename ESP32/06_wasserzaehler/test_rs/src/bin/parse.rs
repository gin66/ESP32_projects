use std::fs::File;
use std::io::BufReader;
use std::io::BufWriter;
use std::io::Write;
use std::path::Path;

const HEIGHT: usize = 480;
const WIDTH: usize = 640;

use test_rs::*;

fn bits_to_rgb888(image: &Vec<u8>) -> Vec<u8> {
    let mut out = vec![0u8; 3 * WIDTH * HEIGHT];
    let mut p = image.iter();
    let mut o = out.iter_mut();
    while let Some(mask) = p.next() {
        let mut b = 128;
        while b > 0 {
            let v = if mask & b != 0 { 255 } else { 0 };
            *(o.next().unwrap()) = v;
            *(o.next().unwrap()) = 0;
            *(o.next().unwrap()) = 0;
            b >>= 1;
        }
    }
    out
}

fn mark(image: &mut Vec<u8>, r: &Reader, ps: &PointerShape) {
    const FRAME: i16 = 50;
    for i in 0..r.candidates {
        let row_from = r.pointer[i as usize].row_from;
        let col_from = r.pointer[i as usize].col_from;
        let row_to = r.pointer[i as usize].row_to;
        let col_to = r.pointer[i as usize].col_to;
        let row_center = r.pointer[i as usize].row_center2 / 2;
        let col_center = r.pointer[i as usize].col_center2 / 2;

        if row_center < FRAME
            || row_from < FRAME
            || row_to < FRAME
            || col_center < FRAME
            || col_from < FRAME
            || col_to < FRAME
        {
            println!("INVALID DATA {:?} {}", r.pointer[i as usize], FRAME);
            return;
        }

        if row_center >= HEIGHT as i16
            || row_from >= HEIGHT as i16
            || row_to >= HEIGHT as i16
            || col_center >= WIDTH as i16
            || col_from >= WIDTH as i16
            || col_to >= WIDTH as i16
        {
            println!("INVALID DATA");
            return;
        }

        for row in row_from..=row_to {
            for col in col_from..=col_to {
                let ind = (row as usize * WIDTH + col as usize) * 3;
                image[ind + 1] = 255;
                image[ind + 2] = 0;
            }
        }

        for row in row_from - FRAME..=(row_to + FRAME).min(HEIGHT as i16 - 1) {
            for col in col_from - FRAME..=(col_to + FRAME).min(WIDTH as i16 - 1) {
                let dr = row as i32 - row_center as i32;
                let dc = col as i32 - col_center as i32;

                if dr * dr + dc * dc <= r.radius2 as i32 {
                    let ind = (row as usize * WIDTH + col as usize) * 3;
                    image[ind + 1] = 255;
                    image[ind + 2] = 255;
                }
            }
        }

        let ind = (row_center as usize * WIDTH + col_center as usize) * 3;
        image[ind] = 0;
        image[ind + 1] = 0;
        image[ind + 2] = 0;

        // draw angle
        let angle = r.pointer[i as usize].angle as f64 / 180.0 * 3.1415;
        for radius in 0..20 {
            let dr = (radius as f64 * f64::sin(angle)).floor() as i16;
            let dc = (radius as f64 * f64::cos(angle)).floor() as i16;
            let row = row_center + dr;
            let col = col_center + dc;
            let ind = (row as usize * WIDTH + col as usize) * 3;
            image[ind + 1] = 0;
            image[ind + 2] = 255;
        }

        // draw pointer shape
        let shape = &ps.shapes[r.pointer[i as usize].angle as usize / 18];
        for (y, x_min, x_max) in shape.iter().step_by(shape.len() - 1) {
            for x in *x_min..=*x_max {
                let row = row_center + y;

                let col = col_center + x;
                if row < 0 || col < 0 {
                    continue;
                }
                if row >= HEIGHT as i16 || col >= WIDTH as i16 {
                    continue;
                }
                let ind = (row as usize * WIDTH + col as usize) * 3;
                image[ind + 0] = 127;
                image[ind + 1] = 127;
                image[ind + 2] = 127;
            }
        }
        for (y, x_min, x_max) in shape.iter() {
            let row = row_center + y;

            let col = col_center + x_min;
            if row < 0 || col < 0 {
                continue;
            }
            if row >= HEIGHT as i16 || col >= WIDTH as i16 {
                continue;
            }
            let ind = (row as usize * WIDTH + col as usize) * 3;
            image[ind + 0] = 127;
            image[ind + 1] = 127;
            image[ind + 2] = 127;

            let col = col_center + x_max;
            if row < 0 || col < 0 {
                continue;
            }
            if row >= HEIGHT as i16 || col >= WIDTH as i16 {
                continue;
            }
            let ind = (row as usize * WIDTH + col as usize) * 3;
            image[ind + 0] = 127;
            image[ind + 1] = 127;
            image[ind + 2] = 127;
        }
    }
}

struct PointerShape {
    shapes: Vec<Vec<(i16, i16, i16)>>,
}

impl PointerShape {
    const ANGLES: usize = 20;
    const CENTER: usize = 60;
    fn make_pointer() -> std::io::Result<PointerShape> {
        let mut shapes = vec![];
        for angle_i in 0..PointerShape::ANGLES {
            let mut x_min = vec![10000; 2 * PointerShape::CENTER + 1];
            let mut x_max = vec![-10000; 2 * PointerShape::CENTER + 1];

            let angle = angle_i as f64 / PointerShape::ANGLES as f64 * 2.0 * std::f64::consts::PI;
            for radius_i in -210..260 {
                let radius = radius_i as f64 / 10.0;

                let half_width = if radius_i < 0 {
                    16.0
                } else {
                    16.0 - radius / 2.0
                };

                for w_i in -300..=300 {
                    let w = w_i as f64 * half_width / 350.0;

                    let r_y = (radius * angle.sin()).floor() as i16;
                    let r_x = (radius * angle.cos()).floor() as i16;

                    let y_1 = r_y - (w as f64 * f64::cos(angle)).floor() as i16;
                    let x_1 = r_x + (w as f64 * f64::sin(angle)).floor() as i16;

                    let y_2 = r_y + (w as f64 * f64::cos(angle)).floor() as i16;
                    let x_2 = r_x - (w as f64 * f64::sin(angle)).floor() as i16;

                    let yi_1 = (y_1 + PointerShape::CENTER as i16) as usize;
                    let yi_2 = (y_2 + PointerShape::CENTER as i16) as usize;

                    x_min[yi_1] = i16::min(x_min[yi_1], x_1);
                    x_max[yi_1] = i16::max(x_max[yi_1], x_1);

                    x_min[yi_2] = i16::min(x_min[yi_2], x_2);
                    x_max[yi_2] = i16::max(x_max[yi_2], x_2);
                }
            }

            let mut shape = vec![];
            for i in 0..(2 * PointerShape::CENTER + 1) {
                if x_min[i] <= x_max[i] {
                    shape.push((
                        i as i16 - PointerShape::CENTER as i16 - 1,
                        x_min[i],
                        x_max[i],
                    ));
                }
            }
            shapes.push(shape);
        }
        Ok(PointerShape { shapes })
    }
    fn print_shapes(&self) {
        for (angle, shape) in self.shapes.iter().enumerate() {
            for (y, x_min, x_max) in shape.iter() {
                println!("{}: {}, {} {}", angle, y, x_min, x_max);
            }
        }
    }
    fn generate_c(&self) -> std::io::Result<()> {
        let path = Path::new("../src/pointer_shape.h");
        let file = File::create(path).unwrap();
        let mut w = BufWriter::new(file);
        writeln!(w, "{}", r#"#include <stdint.h>"#)?;
        writeln!(w)?;
        writeln!(w, "#define ANGLES {}", PointerShape::ANGLES)?;
        writeln!(w)?;
        writeln!(w, "{}", r#"struct shape_s {"#)?;
        writeln!(w, "   int16_t y_min; ")?;
        writeln!(w, "   int16_t y_max; ")?;
        writeln!(w, "   int16_t x_min[{}]; ", 2 * PointerShape::CENTER + 1)?;
        writeln!(w, "   int16_t x_max[{}]; ", 2 * PointerShape::CENTER + 1)?;
        writeln!(w, "{}", r#"};"#)?;
        writeln!(w)?;
        writeln!(
            w,
            "extern const struct shape_s shapes[{}];",
            PointerShape::ANGLES
        )?;

        let path = Path::new("../src/pointer_shape.cpp");
        let file = File::create(path).unwrap();
        let mut w = BufWriter::new(file);
        writeln!(w, "{}", r#"#include "pointer_shape.h""#)?;
        writeln!(w)?;
        writeln!(
            w,
            "const struct shape_s shapes[{}] = ",
            PointerShape::ANGLES
        )?;
        writeln!(w, "{}", r#"{"#)?;
        for shape in self.shapes.iter() {
            writeln!(w, "{}", r#"   {"#)?;
            writeln!(w, "      .y_min = {},", shape[0].0)?;
            writeln!(w, "      .y_max = {},", shape[shape.len() - 1].0)?;
            writeln!(w, "{}", r#"      .x_min = {"#)?;
            for (_, x_min, _) in shape.iter() {
                writeln!(w, "         {},", x_min)?;
            }
            for _ in shape.len()..80 {
                writeln!(w, "         0,")?;
            }
            writeln!(w, "{}", r#"      },"#)?;
            writeln!(w, "{}", r#"      .x_max = {"#)?;
            for (_, _, x_max) in shape.iter() {
                writeln!(w, "         {},", x_max)?;
            }
            for _ in shape.len()..80 {
                writeln!(w, "         0,")?;
            }
            writeln!(w, "{}", r#"      },"#)?;
            writeln!(w, "{}", r#"   },"#)?;
        }
        writeln!(w, "{}", r#"};"#)?;

        Ok(())
    }
}

fn main() -> std::io::Result<()> {
    let mut results = vec![];
    for fname in std::env::args().skip(1) {
        println!("{}", fname);

        let timestamp = fname.split("@").into_iter().collect::<Vec<_>>()[1];
        let timestamp = timestamp.split(".").into_iter().collect::<Vec<_>>()[0];
        let timestamp =
            chrono::NaiveDateTime::parse_from_str(timestamp, "%d-%m-%Y_%H-%M-%S").unwrap();
        println!("{}", timestamp);

        let pointer_shapes = PointerShape::make_pointer()?;
        if false {
            pointer_shapes.print_shapes();
        }
        pointer_shapes.generate_c()?;

        let mut digitized = vec![0u8; WIDTH * HEIGHT / 8];
        let mut temp = vec![0u8; WIDTH * HEIGHT / 8];
        let mut filtered = vec![0u8; WIDTH * HEIGHT / 8];
        let mut r: Reader = Reader::default();

        let f = File::open(&fname)?;
        let mut decoder = jpeg_decoder::Decoder::new(BufReader::new(f));
        let mut pixels = decoder.decode().expect("failed to decode image");

        if pixels.len() != HEIGHT * WIDTH * 3 {
            panic!("Invalid length: {}", pixels.len());
        }

        if false {
            let path = Path::new(&fname);
            let file = File::create(path.with_extension("original.png")).unwrap();
            let ref mut w = BufWriter::new(file);
            let mut encoder = png::Encoder::new(w, WIDTH as u32, HEIGHT as u32);
            encoder.set_color(png::ColorType::RGB);
            encoder.set_depth(png::BitDepth::Eight);
            let mut writer = encoder.write_header()?;
            let mut px = pixels.clone();
            let mut i = 0;
            while i < px.len() {
                let r = px[i] as i32;
                let g = px[i + 1] as i32;
                let b = px[i + 2] as i32;
                let v = if 255 * r * r < 224 * (g * g + b * b) {
                    0
                } else {
                    255
                };
                px[i] = v;
                px[i + 1] = v;
                px[i + 2] = v;
                i += 3;
            }
            writer.write_image_data(&px)?; // Save
        }

        // convert from rgb to bgr
        let mut i = 0;
        while i < pixels.len() {
            let r = pixels[i];
            let b = pixels[i + 2];
            pixels[i] = b;
            pixels[i + 2] = r;
            i += 3;
        }
        println!("BEFORE");
        unsafe {
            digitize(pixels.as_ptr(), digitized.as_mut_ptr(), 1);
            println!("INBETWEEN");
            find_pointer(
                digitized.as_ptr(),
                filtered.as_mut_ptr(),
                temp.as_mut_ptr(),
                &mut r,
            );
            digitize(pixels.as_ptr(), digitized.as_mut_ptr(), 0);
            eval_pointer(digitized.as_ptr(), &mut r);
        }
        println!("{:?}", r);
        println!("AFTER");

        let path = Path::new(&fname);
        let file = File::create(path.with_extension("digitized.png")).unwrap();
        let ref mut w = BufWriter::new(file);
        let mut encoder = png::Encoder::new(w, WIDTH as u32, HEIGHT as u32);
        encoder.set_color(png::ColorType::RGB);
        encoder.set_depth(png::BitDepth::Eight);
        let mut writer = encoder.write_header()?;
        let mut data = bits_to_rgb888(&digitized);
        mark(&mut data, &r, &pointer_shapes);
        writer.write_image_data(&data)?; // Save

        let path = Path::new(&fname);
        let file = File::create(path.with_extension("filtered.png")).unwrap();
        let ref mut w = BufWriter::new(file);
        let mut encoder = png::Encoder::new(w, WIDTH as u32, HEIGHT as u32);
        encoder.set_color(png::ColorType::RGB);
        encoder.set_depth(png::BitDepth::Eight);
        let mut writer = encoder.write_header()?;
        let mut data = bits_to_rgb888(&filtered);
        mark(&mut data, &r, &pointer_shapes);
        writer.write_image_data(&data)?; // Save

        if r.candidates == 4 {
            results.push((timestamp.and_utc().timestamp(), r));
        }
    }

    results.sort_by_key(|(t, _)| *t);
    let results = results
        .into_iter()
        .map(|(timestamp, r)| TimedResult { timestamp, r })
        .collect::<Vec<_>>();

    let path = Path::new("all.log");
    let log_file = File::create(path)?;
    let ref mut log_w = BufWriter::new(log_file);
    let j = serde_json::json!(Result { results });
    writeln!(log_w, "{}", serde_json::to_string_pretty(&j).unwrap())?;
    Ok(())
}
