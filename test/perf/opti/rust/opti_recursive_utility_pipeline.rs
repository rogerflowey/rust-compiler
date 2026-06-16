#![no_std]
#![no_main]

#[path = "opti_runtime.rs"]
mod opti_runtime;

fn combine(a: i32, b: i32, salt: i32) -> i32 {
    let mixed = a.wrapping_mul(31).wrapping_add(b.wrapping_mul(17)).wrapping_add(salt);
    mixed ^ (mixed / 256)
}

#[inline(never)]
fn utility_depth(n: i32, salt: i32) -> i32 {
    if n <= 1 {
        salt.wrapping_add(n.wrapping_mul(3))
    } else {
        let left = utility_depth(n - 1, salt.wrapping_add(n));
        let right = utility_depth(n - 2, salt.wrapping_add(n / 2));
        combine(left, right, salt.wrapping_add(n))
    }
}

#[no_mangle]
pub extern "C" fn main() {
    let seed = opti_runtime::get_int();
    let mut data = [0i32; 48];
    let mut i = 0i32;
    while i < 48 {
        data[i as usize] = seed.wrapping_add(i.wrapping_mul(7)).wrapping_add(utility_depth(5 + (i % 4), seed % 23));
        i += 1;
    }
    let mut round = 0i32;
    let mut acc = 0i32;
    while round < 220 {
        let pos = (round.wrapping_mul(11).wrapping_add(seed)) % 48;
        let mut idx = pos;
        if idx < 0 {
            idx += 48;
        }
        let value = data[idx as usize];
        let extra = utility_depth(6 + (round % 3), value % 19);
        data[idx as usize] = combine(value, extra, round);
        acc = acc.wrapping_add(data[idx as usize] / 3).wrapping_add(extra);
        round += 1;
    }
    opti_runtime::println_int(acc);
    opti_runtime::exit(0);
}

