#![no_std]
#![no_main]

#[path = "opti_runtime.rs"]
mod opti_runtime;

#[no_mangle]
pub extern "C" fn main() {
    let seed = opti_runtime::get_int();
    let mut pattern = [0i32; 18];
    let mut fail = [0i32; 18];
    let mut i = 0i32;
    while i < 18 {
        pattern[i as usize] = (seed.wrapping_add(i.wrapping_mul(5)).wrapping_add(i.wrapping_mul(i))) % 6;
        i += 1;
    }
    i = 1;
    while i < 18 {
        let mut j = fail[(i - 1) as usize];
        while j > 0 && pattern[i as usize] != pattern[j as usize] {
            j = fail[(j - 1) as usize];
        }
        if pattern[i as usize] == pattern[j as usize] {
            j += 1;
        }
        fail[i as usize] = j;
        i += 1;
    }
    let mut trans = [0i32; 108];
    let mut state = 0i32;
    while state < 18 {
        let mut ch = 0i32;
        while ch < 6 {
            let mut next = state;
            while next > 0 && pattern[next as usize] != ch {
                next = fail[(next - 1) as usize];
            }
            if pattern[next as usize] == ch {
                next += 1;
            }
            trans[(state * 6 + ch) as usize] = next;
            ch += 1;
        }
        state += 1;
    }
    let mut checksum = 0i32;
    state = 0;
    i = 0;
    while i < 70000 {
        let ch = (seed.wrapping_add(i.wrapping_mul(7)).wrapping_add(checksum)) % 6;
        state = trans[(state * 6 + ch) as usize];
        if state == 18 {
            checksum = checksum.wrapping_add(i);
            state = fail[17];
        } else {
            checksum = checksum.wrapping_add(state);
        }
        i += 1;
    }
    opti_runtime::println_int(checksum);
    opti_runtime::exit(0);
}

