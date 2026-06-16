#![no_std]
#![no_main]

#[path = "opti_runtime.rs"]
mod opti_runtime;

fn lowbit(x: i32) -> i32 {
    x & (0i32 - x)
}

#[inline(never)]
fn prefix_sum(tree: [i32; 129], index: i32) -> i32 {
    let mut i = index;
    let mut result = 0i32;
    while i > 0 {
        result = result.wrapping_add(tree[i as usize]);
        i -= lowbit(i);
    }
    result
}

#[no_mangle]
pub extern "C" fn main() {
    let seed = opti_runtime::get_int();
    let mut tree = [0i32; 129];
    let mut raw = [0i32; 128];
    let mut i = 1i32;
    while i <= 128 {
        let value = (seed.wrapping_add(i.wrapping_mul(23)).wrapping_add(i.wrapping_mul(i))) % 1000;
        raw[(i - 1) as usize] = value;
        let mut j = i;
        while j <= 128 {
            tree[j as usize] = tree[j as usize].wrapping_add(value);
            j += lowbit(j);
        }
        i += 1;
    }
    let mut op = 0i32;
    let mut checksum = 0i32;
    while op < 24000 {
        let pos = ((op.wrapping_mul(17).wrapping_add(seed)) % 128) + 1;
        let delta = ((op.wrapping_add(seed)) % 31) - 15;
        raw[(pos - 1) as usize] = raw[(pos - 1) as usize].wrapping_add(delta);
        let mut j = pos;
        while j <= 128 {
            tree[j as usize] = tree[j as usize].wrapping_add(delta);
            j += lowbit(j);
        }
        let left = ((op.wrapping_mul(5).wrapping_add(7)) % 128) + 1;
        let right = ((left + op % 37) % 128) + 1;
        if left <= right {
            checksum = checksum.wrapping_add(prefix_sum(tree, right).wrapping_sub(prefix_sum(tree, left - 1)));
        } else {
            checksum = checksum.wrapping_add(prefix_sum(tree, left).wrapping_sub(prefix_sum(tree, right - 1)));
        }
        op += 1;
    }
    opti_runtime::println_int(checksum);
    opti_runtime::exit(0);
}
