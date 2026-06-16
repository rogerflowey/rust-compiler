#![no_std]
#![no_main]

#[path = "opti_runtime.rs"]
mod opti_runtime;

fn norm(value: i32) -> i32 {
    let r = value % 1000003;
    if r < 0 { r + 1000003 } else { r }
}

#[no_mangle]
pub extern "C" fn main() {
    let seed = opti_runtime::get_int();
    let mut bytes = [0i32; 256];
    let mut prefix = [0i32; 257];
    let mut i = 0i32;
    while i < 256 {
        bytes[i as usize] = (seed.wrapping_add(i.wrapping_mul(31)).wrapping_add(i.wrapping_mul(i).wrapping_mul(5))) % 251;
        prefix[(i + 1) as usize] = norm(prefix[i as usize].wrapping_mul(257).wrapping_add(bytes[i as usize]).wrapping_add(1));
        i += 1;
    }
    let mut block_hash = [0i32; 64];
    let mut block = 0i32;
    while block < 64 {
        let start = block * 4;
        let mut h = 0i32;
        let mut j = 0i32;
        while j < 4 {
            h = norm(h.wrapping_mul(263).wrapping_add(bytes[(start + j) as usize]).wrapping_add(block));
            j += 1;
        }
        block_hash[block as usize] = h;
        block += 1;
    }
    let mut round = 0i32;
    let mut acc = 0i32;
    while round < 50000 {
        let block_id = (round.wrapping_mul(13).wrapping_add(seed)) % 64;
        let span = 1 + (round % 8);
        let block_end = block_id + span;
        let mut mixed = block_hash[block_id as usize];
        if block_end < 64 {
            mixed = mixed.wrapping_add(block_hash[block_end as usize]);
        } else {
            mixed = mixed.wrapping_add(block_hash[(block_end - 64) as usize]);
        }
        acc = norm(acc.wrapping_mul(911).wrapping_add(mixed).wrapping_add(prefix[(round % 256) as usize]));
        round += 1;
    }
    opti_runtime::println_int(acc);
    opti_runtime::exit(0);
}

