#![no_std]
#![no_main]

#[path = "opti_runtime.rs"]
mod opti_runtime;

fn positive_mod(value: i32, modulus: i32) -> i32 {
    let r = value % modulus;
    if r < 0 { r + modulus } else { r }
}

#[inline(never)]
fn mix_word(value: i32, round: i32) -> i32 {
    let a = value ^ (value >> 13);
    let b = a.wrapping_mul(1103515245).wrapping_add(round.wrapping_mul(97)).wrapping_add(12345);
    let c = b ^ (b / 65536);
    c.wrapping_add(round.wrapping_mul(round).wrapping_add(31))
}

#[no_mangle]
pub extern "C" fn main() {
    let seed = opti_runtime::get_int();
    let mut buckets = [0i32; 64];
    let mut i = 0i32;
    let mut state = seed.wrapping_add(17);
    let mut acc = 0i32;
    while i < 18000 {
        state = mix_word(state.wrapping_add(i), i);
        let slot = positive_mod(state.wrapping_add(i.wrapping_mul(3)), 64) as usize;
        let old = buckets[slot];
        let updated = old.wrapping_add(positive_mod(state, 997)).wrapping_add(i / 2);
        buckets[slot] = updated;
        acc ^= updated.wrapping_add(state / 97);
        i += 1;
    }
    let mut j = 0usize;
    let mut total = acc;
    while j < 64 {
        total = total.wrapping_add(buckets[j].wrapping_mul(j as i32 + 3));
        j += 1;
    }
    opti_runtime::println_int(total);
    opti_runtime::exit(0);
}

