#![no_std]
#![no_main]

#[path = "opti_runtime.rs"]
mod opti_runtime;

fn pixel_index(row: i32, col: i32) -> usize {
    (row * 16 + col) as usize
}

fn clamp(value: i32, limit: i32) -> i32 {
    if value < 0 {
        0
    } else if value >= limit {
        limit - 1
    } else {
        value
    }
}

#[no_mangle]
pub extern "C" fn main() {
    let seed = opti_runtime::get_int();
    let mut src = [0i32; 256];
    let mut tmp = [0i32; 256];
    let mut dst = [0i32; 256];
    let mut r = 0i32;
    while r < 16 {
        let mut c = 0i32;
        while c < 16 {
            src[pixel_index(r, c)] = (seed.wrapping_add(r.wrapping_mul(29)).wrapping_add(c.wrapping_mul(17)).wrapping_add(r.wrapping_mul(c))) % 256;
            c += 1;
        }
        r += 1;
    }
    let mut pass = 0i32;
    while pass < 280 {
        r = 0;
        while r < 16 {
            let mut c = 0i32;
            while c < 16 {
                let left = src[pixel_index(r, clamp(c - 1, 16))];
                let mid = src[pixel_index(r, c)];
                let right = src[pixel_index(r, clamp(c + 1, 16))];
                tmp[pixel_index(r, c)] = (left.wrapping_add(mid.wrapping_mul(2)).wrapping_add(right)) / 4;
                c += 1;
            }
            r += 1;
        }
        r = 0;
        while r < 16 {
            let mut c = 0i32;
            while c < 16 {
                let up = tmp[pixel_index(clamp(r - 1, 16), c)];
                let mid = tmp[pixel_index(r, c)];
                let down = tmp[pixel_index(clamp(r + 1, 16), c)];
                dst[pixel_index(r, c)] = (up.wrapping_add(mid.wrapping_mul(2)).wrapping_add(down).wrapping_add(pass % 7)) / 4;
                c += 1;
            }
            r += 1;
        }
        r = 0;
        while r < 256 {
            src[r as usize] = dst[r as usize];
            r += 1;
        }
        pass += 1;
    }
    let mut checksum = 0i32;
    r = 0;
    while r < 256 {
        checksum = checksum.wrapping_add(dst[r as usize].wrapping_mul((r % 13) + 1));
        r += 1;
    }
    opti_runtime::println_int(checksum);
    opti_runtime::exit(0);
}

