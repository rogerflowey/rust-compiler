#![no_std]
#![no_main]

#[path = "opti_runtime.rs"]
mod opti_runtime;

fn map_hash(key: i32) -> i32 {
    let r = (key.wrapping_mul(4099).wrapping_add(key / 11)) % 127;
    if r < 0 { r + 127 } else { r }
}

#[no_mangle]
pub extern "C" fn main() {
    let seed = opti_runtime::get_int();
    let mut key = [0i32; 96];
    let mut left = [0i32; 96];
    let mut right = [0i32; 96];
    let mut count = 0i32;
    while count < 96 {
        key[count as usize] = (seed.wrapping_mul(97).wrapping_add(count.wrapping_mul(53)).wrapping_add(count.wrapping_mul(count))) % 1009;
        left[count as usize] = -1;
        right[count as usize] = -1;
        if count > 0 {
            let mut cur = 0i32;
            let mut placed = 0i32;
            while placed == 0 {
                if key[count as usize] < key[cur as usize] {
                    if left[cur as usize] < 0 {
                        left[cur as usize] = count;
                        placed = 1;
                    } else {
                        cur = left[cur as usize];
                    }
                } else if right[cur as usize] < 0 {
                    right[cur as usize] = count;
                    placed = 1;
                } else {
                    cur = right[cur as usize];
                }
            }
        }
        count += 1;
    }
    let mut table_key = [0i32; 127];
    let mut table_val = [0i32; 127];
    let mut used = [0i32; 127];
    let mut i = 0i32;
    while i < 96 {
        let mut slot = map_hash(key[i as usize]);
        while used[slot as usize] != 0 {
            slot = (slot + 1) % 127;
        }
        used[slot as usize] = 1;
        table_key[slot as usize] = key[i as usize];
        table_val[slot as usize] = left[i as usize].wrapping_add(right[i as usize]).wrapping_add(i);
        i += 1;
    }
    let mut query = 0i32;
    let mut checksum = 0i32;
    while query < 50000 {
        let wanted = key[(query % 96) as usize];
        let mut cur = 0i32;
        while cur >= 0 && key[cur as usize] != wanted {
            if wanted < key[cur as usize] {
                cur = left[cur as usize];
            } else {
                cur = right[cur as usize];
            }
        }
        let mut slot = map_hash(wanted);
        while used[slot as usize] != 0 && table_key[slot as usize] != wanted {
            slot = (slot + 1) % 127;
        }
        checksum = checksum.wrapping_add(cur).wrapping_add(table_val[slot as usize]);
        query += 1;
    }
    opti_runtime::println_int(checksum);
    opti_runtime::exit(0);
}
