use std::fs::File;
use std::io::BufReader;
use std::io::BufWriter;
use std::path::Path;

const HEIGHT: usize = 296;
const WIDTH: usize = 400;

#[repr(C)]
#[derive(Debug)]
struct Reader {
    is_ok: u8,
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
    let mut digitized = vec![0u8; WIDTH * HEIGHT / 8];
    let mut temp = vec![0u8; WIDTH * HEIGHT / 8];
    let mut filtered = vec![0u8; WIDTH * HEIGHT / 8];
    let mut r: Reader = Reader { is_ok: 0 };

    let f = File::open("../test/image.jpeg")?;
    let mut decoder = jpeg_decoder::Decoder::new(BufReader::new(f));
    let pixels = decoder.decode().expect("failed to decode image");

    if pixels.len() != HEIGHT*WIDTH*3 {
        panic!("Invalid length: {}", pixels.len());
    }

    // convert from rgb to bgr
    let mut i = 0;
    while i < pixels.len() {
        let r = pixels[i];
        let b = pixels[i+2];
        pixels[i]= b;
        pixels[i+2] = r;
        i += 3;
    }

    unsafe {
        digitize(pixels.as_ptr(), digitized.as_mut_ptr(), &mut r);
        find_pointer(digitized.as_ptr(), filtered.as_mut_ptr(), temp.as_mut_ptr(), &mut r);
    }
    println!("{:?}", r);

    let path = Path::new("image.png");
    let file = File::create(path).unwrap();
    let ref mut w = BufWriter::new(file);
    let mut encoder = png::Encoder::new(w, WIDTH as u32, HEIGHT as u32);
    encoder.set_color(png::ColorType::RGB);
    encoder.set_depth(png::BitDepth::Eight);
    let mut writer = encoder.write_header()?;
    writer.write_image_data(&pixels)?; // Save

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
    let data = bits_to_rgb888(&filtered);
    writer.write_image_data(&data)?; // Save

    Ok(())
}
