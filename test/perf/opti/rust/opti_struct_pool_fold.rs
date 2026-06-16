#![no_std]
#![no_main]

#[path = "opti_runtime.rs"]
mod opti_runtime;

#[derive(Copy, Clone)]
struct Cell {
    value: i32,
    next: i32,
    weight: i32,
    tag: i32,
}

fn fold_cell(cell: Cell, carry: i32) -> i32 {
    let base = cell.value.wrapping_mul(3).wrapping_add(cell.weight.wrapping_mul(5)).wrapping_add(cell.tag);
    base ^ carry.wrapping_add(cell.next.wrapping_mul(17))
}

#[no_mangle]
pub extern "C" fn main() {
    let seed = opti_runtime::get_int();
    let mut pool = [Cell { value: 0, next: 0, weight: 0, tag: 0 }; 96];
    let mut i = 0i32;
    while i < 96 {
        pool[i as usize] = Cell {
            value: seed.wrapping_add(i.wrapping_mul(19)),
            next: (i.wrapping_mul(7).wrapping_add(seed)) % 96,
            weight: (i.wrapping_mul(i).wrapping_add(11)) % 257,
            tag: i % 5,
        };
        if pool[i as usize].next < 0 {
            pool[i as usize].next += 96;
        }
        i += 1;
    }
    let mut round = 0i32;
    let mut cursor = seed % 96;
    if cursor < 0 {
        cursor += 96;
    }
    let mut acc = 0i32;
    while round < 25000 {
        let cell = pool[cursor as usize];
        acc = fold_cell(cell, acc);
        let updated = (cell.value.wrapping_add(acc / 97).wrapping_add(round)) % 4099;
        pool[cursor as usize].value = updated;
        cursor = (cell.next.wrapping_add(round).wrapping_add(cell.tag)) % 96;
        if cursor < 0 {
            cursor += 96;
        }
        round += 1;
    }
    opti_runtime::println_int(acc.wrapping_add(pool[cursor as usize].value));
    opti_runtime::exit(0);
}

