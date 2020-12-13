use std::fs::File;
use std::io::BufReader;
use std::io::BufWriter;
use std::path::Path;

const HEIGHT: usize = 296;
const WIDTH: usize = 400;

#[repr(C)]
#[derive(Debug, Default)]
struct Pointer {
    row_from: i16,
    row_to: i16,
    row_center2: i16,
    col_from: i16,
    col_to: i16,
    col_center2: i16,
}

#[repr(C)]
#[derive(Debug, Default)]
struct Reader {
    is_ok: u8,
    candidates: u8,
    radius2: i16,
    pointer: [Pointer; 10],
}

extern "C" {
    fn digitize(image: *const u8, digitized: *mut u8, r: *mut Reader);
    fn find_pointer(digitized: *const u8, filtered: *mut u8, temp: *mut u8, r: *mut Reader);
}

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

fn mark(image: &mut Vec<u8>, r: &Reader) {
    for i in 0..r.candidates {
        let row_from = r.pointer[i as usize].row_from;
        let col_from = r.pointer[i as usize].col_from;
        let row_to = r.pointer[i as usize].row_to;
        let col_to = r.pointer[i as usize].col_to;

        for row in row_from..=row_to {
            for col in col_from..=col_to {
                let i = (row as usize * WIDTH + col as usize) * 3;
                image[i + 1] = 255;
                image[i + 2] = 0;
            }
        }

        for row in row_from-32..=row_to+32 {
            for col in col_from-32..=col_to+32 {
                let dr = row as i32 - r.pointer[i as usize].row_center2 as i32/2;
                let dc = col as i32 - r.pointer[i as usize].col_center2 as i32/2;

                if dr * dr + dc * dc <= r.radius2 as i32 {
                    let i = (row as usize * WIDTH + col as usize) * 3;
                    image[i + 1] = 255;
                    image[i + 2] = 255;
                }
            }
        }
    }
}

fn main() -> std::io::Result<()> {
    let mut digitized = vec![0u8; WIDTH * HEIGHT / 8];
    let mut temp = vec![0u8; WIDTH * HEIGHT / 8];
    let mut filtered = vec![0u8; WIDTH * HEIGHT / 8];
    let mut r: Reader = Reader::default();

    let f = File::open("../test/image.jpeg")?;
    let mut decoder = jpeg_decoder::Decoder::new(BufReader::new(f));
    let mut pixels = decoder.decode().expect("failed to decode image");

    if pixels.len() != HEIGHT * WIDTH * 3 {
        panic!("Invalid length: {}", pixels.len());
    }

    let path = Path::new("image.png");
    let file = File::create(path).unwrap();
    let ref mut w = BufWriter::new(file);
    let mut encoder = png::Encoder::new(w, WIDTH as u32, HEIGHT as u32);
    encoder.set_color(png::ColorType::RGB);
    encoder.set_depth(png::BitDepth::Eight);
    let mut writer = encoder.write_header()?;
    writer.write_image_data(&pixels)?; // Save

    // convert from rgb to bgr
    let mut i = 0;
    while i < pixels.len() {
        let r = pixels[i];
        let b = pixels[i + 2];
        pixels[i] = b;
        pixels[i + 2] = r;
        i += 3;
    }

    unsafe {
        digitize(pixels.as_ptr(), digitized.as_mut_ptr(), &mut r);
        find_pointer(
            digitized.as_ptr(),
            filtered.as_mut_ptr(),
            temp.as_mut_ptr(),
            &mut r,
        );
    }
    println!("{:?}", r);

    let path = Path::new("image_digitized.png");
    let file = File::create(path).unwrap();
    let ref mut w = BufWriter::new(file);
    let mut encoder = png::Encoder::new(w, WIDTH as u32, HEIGHT as u32);
    encoder.set_color(png::ColorType::RGB);
    encoder.set_depth(png::BitDepth::Eight);
    let mut writer = encoder.write_header()?;
    let data = bits_to_rgb888(&digitized);
    writer.write_image_data(&data)?; // Save

    let path = Path::new("image_filtered.png");
    let file = File::create(path).unwrap();
    let ref mut w = BufWriter::new(file);
    let mut encoder = png::Encoder::new(w, WIDTH as u32, HEIGHT as u32);
    encoder.set_color(png::ColorType::RGB);
    encoder.set_depth(png::BitDepth::Eight);
    let mut writer = encoder.write_header()?;
    let mut data = bits_to_rgb888(&filtered);
    mark(&mut data, &r);
    writer.write_image_data(&data)?; // Save

    Ok(())
}
