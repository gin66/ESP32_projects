fn main() {
    cc::Build::new().file("read.c").compile("read");
    cc::Build::new()
        .file("pointer_shape.cpp")
        .compile("pointer_shape");
    cc::Build::new().file("evaluate.c").compile("evaluate");
}
