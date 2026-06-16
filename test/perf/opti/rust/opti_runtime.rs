#![allow(dead_code)]

unsafe extern "C" {
    fn putchar(c: i32) -> i32;
    fn getchar() -> i32;
    #[link_name = "__reimu_exit"]
    fn reimu_exit(code: i32) -> !;
}

#[inline(never)]
pub fn print_int(mut x: i32) {
    unsafe {
        if x == 0 {
            putchar(48);
            return;
        }
        if x < 0 {
            putchar(45);
        }
        let mut buf = [0u8; 12];
        let mut n = 0usize;
        while x != 0 {
            let mut d = x % 10;
            if d < 0 {
                d = -d;
            }
            buf[n] = d as u8 + 48;
            n += 1;
            x /= 10;
        }
        while n > 0 {
            n -= 1;
            putchar(buf[n] as i32);
        }
    }
}

#[inline(never)]
pub fn println_int(x: i32) {
    unsafe {
        print_int(x);
        putchar(10);
    }
}

#[inline(never)]
pub fn get_int() -> i32 {
    unsafe {
        let mut c = getchar();
        while c == 32 || c == 10 || c == 13 || c == 9 {
            c = getchar();
        }
        let mut sign = 1i32;
        if c == 45 {
            sign = -1;
            c = getchar();
        }
        let mut value = 0i32;
        while c >= 48 && c <= 57 {
            value = value.wrapping_mul(10).wrapping_add(c - 48);
            c = getchar();
        }
        value.wrapping_mul(sign)
    }
}

#[inline(never)]
pub fn exit(code: i32) -> ! {
    unsafe { reimu_exit(code) }
}

#[panic_handler]
fn panic(_: &core::panic::PanicInfo) -> ! {
    loop {}
}
