use std::fs::File;
use std::io::prelude::*;
use std::io::BufWriter;
use std::path::Path;

#[repr(C)]
#[derive(Debug)]
struct Reader {
    is_ok: u8,
}

extern "C" {
    fn find_pointer(image: *const u8, digitized: *mut u8, r: *mut Reader);
}

fn rgb565_to_rgb888(image: &Vec<u8>) -> Vec<u8> {
    let mut out = vec![0u8; 3 * 320 * 240];
    let mut p = image.iter();
    let mut o = out.iter_mut();
    while let Some(rgb1) = p.next() {
        let rgb2 = p.next().unwrap();
        let r = (rgb1 >> 2) & 0x3e;
        let g = ((rgb1 << 3) & 0x38) | ((rgb2 >> 5) & 0x07);
        let b = (rgb2 << 1) & 0x3e;
        *(o.next().unwrap()) = r << 2;
        *(o.next().unwrap()) = g << 2;
        *(o.next().unwrap()) = b << 2;
    }
    out
}

fn bits_to_rgb888(image: &Vec<u8>) -> Vec<u8> {
    let mut out = vec![0u8; 3 * 320 * 240];
    let mut p = image.iter();
    let mut o = out.iter_mut();
    while let Some(mask) = p.next() {
        let mut b = 128;
        while b > 0 {
            let v = if mask & b != 0 {
                255
            }
            else {
                0
            };
            *(o.next().unwrap()) = v;
            *(o.next().unwrap()) = v;
            *(o.next().unwrap()) = v;
            b >>= 1;
        }
    }
    out
}

fn main() -> std::io::Result<()> {
    let mut image: Vec<u8> = vec![];
    let mut digitized = vec![0u8; 320 * 240 / 8];
    let mut r: Reader = Reader { is_ok: 0 };

    let mut f = File::open("../test/image")?;
    f.read_to_end(&mut image)?;

    unsafe {
        find_pointer(image.as_ptr(), digitized.as_mut_ptr(), &mut r);
    }
    println!("{:?}", r);

    let path = Path::new("image.png");
    let file = File::create(path).unwrap();
    let ref mut w = BufWriter::new(file);
    let mut encoder = png::Encoder::new(w, 320, 240);
    encoder.set_color(png::ColorType::RGB);
    encoder.set_depth(png::BitDepth::Eight);
    let mut writer = encoder.write_header()?;
    let data = rgb565_to_rgb888(&image);
    writer.write_image_data(&data)?; // Save

    let path = Path::new("image_digitized.png");
    let file = File::create(path).unwrap();
    let ref mut w = BufWriter::new(file);
    let mut encoder = png::Encoder::new(w, 320, 240);
    encoder.set_color(png::ColorType::RGB);
    encoder.set_depth(png::BitDepth::Eight);
    let mut writer = encoder.write_header()?;
    let data = bits_to_rgb888(&digitized);
    writer.write_image_data(&data)?; // Save

    Ok(())
}
