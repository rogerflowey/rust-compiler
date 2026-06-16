#![no_std]
#![no_main]

#[path = "opti_runtime.rs"]
mod opti_runtime;

fn clamp_index(value: i32, limit: i32) -> i32 {
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
    let mut a = [0i32; 128];
    let mut b = [0i32; 128];
    let mut i = 0i32;
    while i < 128 {
        a[i as usize] = (seed.wrapping_add(i.wrapping_mul(37)).wrapping_add(i.wrapping_mul(i))) % 1009;
        b[i as usize] = (seed.wrapping_mul(3).wrapping_add(i.wrapping_mul(13))) % 997;
        i += 1;
    }
    let mut pass = 0i32;
    while pass < 140 {
        i = 0;
        while i < 128 {
            let left = a[clamp_index(i - 1, 128) as usize];
            let mid = a[i as usize];
            let right = a[clamp_index(i + 1, 128) as usize];
            let blended = (left.wrapping_add(mid.wrapping_mul(2)).wrapping_add(right)) / 4;
            b[i as usize] = blended.wrapping_add((b[i as usize].wrapping_mul(3).wrapping_add(pass)) % 257);
            i += 1;
        }
        i = 0;
        while i < 128 {
            a[i as usize] = b[i as usize].wrapping_add(i.wrapping_mul(pass) % 31);
            i += 1;
        }
        pass += 1;
    }
    let mut checksum = 0i32;
    i = 0;
    while i < 128 {
        checksum = checksum.wrapping_add(a[i as usize].wrapping_mul((i % 7) + 1));
        i += 1;
    }
    opti_runtime::println_int(checksum);
    opti_runtime::exit(0);
}

