#![no_std]
#![no_main]

#[path = "opti_runtime.rs"]
mod opti_runtime;

fn max_i32(a: i32, b: i32) -> i32 {
    if a > b { a } else { b }
}

#[no_mangle]
pub extern "C" fn main() {
    let seed = opti_runtime::get_int();
    let mut dp = [0i32; 96];
    let mut weight = [0i32; 48];
    let mut value = [0i32; 48];
    let mut i = 0i32;
    while i < 48 {
        weight[i as usize] = (i.wrapping_mul(7).wrapping_add(seed)) % 17 + 1;
        value[i as usize] = (i.wrapping_mul(11).wrapping_add(seed.wrapping_mul(3))) % 101 + 5;
        i += 1;
    }
    i = 0;
    while i < 48 {
        let mut cap = 95i32;
        while cap >= weight[i as usize] {
            let candidate = dp[(cap - weight[i as usize]) as usize].wrapping_add(value[i as usize]);
            dp[cap as usize] = max_i32(dp[cap as usize], candidate);
            cap -= 1;
        }
        i += 1;
    }
    let mut paths = [0i32; 100];
    let mut r = 0i32;
    while r < 10 {
        let mut c = 0i32;
        while c < 10 {
            let idx = r * 10 + c;
            let cost = ((r + 1).wrapping_mul(c + 3).wrapping_add(seed)) % 19;
            if r == 0 {
                paths[idx as usize] = cost;
            } else if c == 0 {
                paths[idx as usize] = paths[((r - 1) * 10 + c) as usize].wrapping_add(cost);
            } else {
                let up = paths[((r - 1) * 10 + c) as usize];
                let left = paths[(r * 10 + c - 1) as usize];
                paths[idx as usize] = if up < left { up.wrapping_add(cost) } else { left.wrapping_add(cost) };
            }
            c += 1;
        }
        r += 1;
    }
    opti_runtime::println_int(dp[95].wrapping_add(paths[99]));
    opti_runtime::exit(0);
}

