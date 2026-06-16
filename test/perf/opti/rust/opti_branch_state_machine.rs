#![no_std]
#![no_main]

#[path = "opti_runtime.rs"]
mod opti_runtime;

#[inline(never)]
fn next_state(state: i32, input: i32) -> i32 {
    if state == 0 {
        if input < 3 { 1 } else { 4 }
    } else if state == 1 {
        if input == 5 { 3 } else { 2 }
    } else if state == 2 {
        if input > 6 { 5 } else { 0 }
    } else if state == 3 {
        if input < 4 { 2 } else { 6 }
    } else if state == 4 {
        if input == 1 { 6 } else { 3 }
    } else if state == 5 {
        if input < 5 { 4 } else { 1 }
    } else if input > 7 {
        0
    } else {
        5
    }
}

#[no_mangle]
pub extern "C" fn main() {
    let seed = opti_runtime::get_int();
    let mut i = 0i32;
    let mut state = seed % 7;
    if state < 0 {
        state += 7;
    }
    let mut score = 0i32;
    while i < 120000 {
        let input = (seed.wrapping_add(i.wrapping_mul(13)).wrapping_add(score / 17)) % 11;
        state = next_state(state, input);
        if state == 0 {
            score = score.wrapping_add(input).wrapping_add(i % 5);
        } else if state == 3 {
            score = score.wrapping_sub(input.wrapping_mul(2)).wrapping_add(9);
        } else {
            score = score.wrapping_add(state.wrapping_mul(input)).wrapping_sub(i % 3);
        }
        i += 1;
    }
    opti_runtime::println_int(score.wrapping_add(state));
    opti_runtime::exit(0);
}
