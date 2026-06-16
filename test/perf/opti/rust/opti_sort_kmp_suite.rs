#![no_std]
#![no_main]

#[path = "opti_runtime.rs"]
mod opti_runtime;

#[no_mangle]
pub extern "C" fn main() {
    let seed = opti_runtime::get_int();
    let mut values = [0i32; 96];
    let mut i = 0i32;
    while i < 96 {
        values[i as usize] = (seed.wrapping_mul(41).wrapping_add(i.wrapping_mul(73)).wrapping_add(i.wrapping_mul(i).wrapping_mul(3))) % 10007;
        i += 1;
    }
    i = 1;
    while i < 96 {
        let key = values[i as usize];
        let mut j = i - 1;
        while j >= 0 && values[j as usize] > key {
            values[(j + 1) as usize] = values[j as usize];
            j -= 1;
        }
        values[(j + 1) as usize] = key;
        i += 1;
    }
    let mut text = [0i32; 160];
    let mut pat = [0i32; 16];
    i = 0;
    while i < 160 {
        text[i as usize] = values[(i % 96) as usize] % 13;
        i += 1;
    }
    i = 0;
    while i < 16 {
        let source_index = (i.wrapping_mul(7).wrapping_add(seed)) % 160;
        pat[i as usize] = text[source_index as usize];
        i += 1;
    }
    let mut pi = [0i32; 16];
    i = 1;
    while i < 16 {
        let mut j = pi[(i - 1) as usize];
        while j > 0 && pat[i as usize] != pat[j as usize] {
            j = pi[(j - 1) as usize];
        }
        if pat[i as usize] == pat[j as usize] {
            j += 1;
        }
        pi[i as usize] = j;
        i += 1;
    }
    let mut q = 0i32;
    let mut matches = 0i32;
    i = 0;
    while i < 160 {
        while q > 0 && text[i as usize] != pat[q as usize] {
            q = pi[(q - 1) as usize];
        }
        if text[i as usize] == pat[q as usize] {
            q += 1;
        }
        if q == 16 {
            matches += 1;
            q = pi[15];
        }
        i += 1;
    }
    opti_runtime::println_int(values[0].wrapping_add(values[95]).wrapping_add(matches.wrapping_mul(97)));
    opti_runtime::exit(0);
}

