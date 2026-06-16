#![no_std]
#![no_main]

#[path = "opti_runtime.rs"]
mod opti_runtime;

fn edge_index(row: i32, col: i32) -> usize {
    (row * 12 + col) as usize
}

#[no_mangle]
pub extern "C" fn main() {
    let seed = opti_runtime::get_int();
    let mut graph = [0i32; 144];
    let mut r = 0i32;
    while r < 12 {
        let mut c = 0i32;
        while c < 12 {
            if r == c {
                graph[edge_index(r, c)] = 0;
            } else {
                graph[edge_index(r, c)] = ((r + 3).wrapping_mul(c + 5).wrapping_add(seed)) % 97 + 1;
            }
            c += 1;
        }
        r += 1;
    }
    let mut k = 0i32;
    while k < 12 {
        r = 0;
        while r < 12 {
            let mut c = 0i32;
            while c < 12 {
                let through = graph[edge_index(r, k)].wrapping_add(graph[edge_index(k, c)]);
                if through < graph[edge_index(r, c)] {
                    graph[edge_index(r, c)] = through;
                }
                c += 1;
            }
            r += 1;
        }
        k += 1;
    }
    let mut checksum = 0i32;
    r = 0;
    while r < 12 {
        let target = (r.wrapping_mul(7).wrapping_add(seed)) % 12;
        checksum = checksum.wrapping_add(graph[edge_index(r, target)].wrapping_mul(r + 1));
        r += 1;
    }
    opti_runtime::println_int(checksum);
    opti_runtime::exit(0);
}

