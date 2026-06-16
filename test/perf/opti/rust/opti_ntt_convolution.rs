#![no_std]
#![no_main]

#[path = "opti_runtime.rs"]
mod opti_runtime;

fn mod_norm(value: i32) -> i32 {
    let r = value % 12289;
    if r < 0 { r + 12289 } else { r }
}

#[inline(never)]
fn mod_pow(base: i32, exp: i32) -> i32 {
    let mut result = 1i32;
    let mut b = mod_norm(base);
    let mut e = exp;
    while e > 0 {
        if e % 2 == 1 {
            result = mod_norm(result.wrapping_mul(b));
        }
        b = mod_norm(b.wrapping_mul(b));
        e /= 2;
    }
    result
}

#[no_mangle]
pub extern "C" fn main() {
    let seed = opti_runtime::get_int();
    let mut a = [0i32; 64];
    let mut b = [0i32; 64];
    let mut i = 0i32;
    while i < 64 {
        a[i as usize] = mod_norm(seed.wrapping_add(i.wrapping_mul(17)).wrapping_add(i.wrapping_mul(i)));
        b[i as usize] = mod_norm(seed.wrapping_mul(3).wrapping_add(i.wrapping_mul(29)).wrapping_add(7));
        i += 1;
    }
    let root = mod_pow(11, 192);
    let mut out = [0i32; 64];
    let mut row = 0i32;
    while row < 64 {
        let mut col = 0i32;
        let mut w = 1i32;
        let step = mod_pow(root, row);
        while col < 64 {
            let product = mod_norm(a[col as usize].wrapping_mul(b[((row - col + 64) % 64) as usize]));
            out[row as usize] = mod_norm(out[row as usize].wrapping_add(product.wrapping_mul(w)));
            w = mod_norm(w.wrapping_mul(step));
            col += 1;
        }
        row += 1;
    }
    let mut checksum = 0i32;
    i = 0;
    while i < 64 {
        checksum = mod_norm(checksum.wrapping_add(out[i as usize].wrapping_mul(i + 1)));
        i += 1;
    }
    opti_runtime::println_int(checksum);
    opti_runtime::exit(0);
}

