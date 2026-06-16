#![no_std]
#![no_main]

#[path = "opti_runtime.rs"]
mod opti_runtime;

fn hash_key(key: i32) -> i32 {
    let h = key.wrapping_mul(1103).wrapping_add((key / 17).wrapping_mul(97)).wrapping_add(53);
    let r = h % 257;
    if r < 0 { r + 257 } else { r }
}

#[no_mangle]
pub extern "C" fn main() {
    let seed = opti_runtime::get_int();
    let mut keys = [0i32; 257];
    let mut vals = [0i32; 257];
    let mut used = [0i32; 257];
    let mut i = 0i32;
    while i < 180 {
        let key = seed.wrapping_mul(1000).wrapping_add(i.wrapping_mul(37)).wrapping_add(i % 13);
        let mut slot = hash_key(key);
        while used[slot as usize] != 0 {
            slot = (slot + 1) % 257;
        }
        used[slot as usize] = 1;
        keys[slot as usize] = key;
        vals[slot as usize] = i.wrapping_mul(i).wrapping_add(seed);
        i += 1;
    }
    let mut q = 0i32;
    let mut checksum = 0i32;
    while q < 60000 {
        let wanted_id = (q.wrapping_mul(19).wrapping_add(seed)) % 180;
        let key = seed.wrapping_mul(1000).wrapping_add(wanted_id.wrapping_mul(37)).wrapping_add(wanted_id % 13);
        let mut slot = hash_key(key);
        while used[slot as usize] != 0 && keys[slot as usize] != key {
            slot = (slot + 1) % 257;
        }
        if used[slot as usize] != 0 {
            checksum = checksum.wrapping_add(vals[slot as usize]).wrapping_add(slot);
        } else {
            checksum = checksum.wrapping_sub(q);
        }
        q += 1;
    }
    opti_runtime::println_int(checksum);
    opti_runtime::exit(0);
}

