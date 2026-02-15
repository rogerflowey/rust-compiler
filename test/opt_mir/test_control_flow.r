fn main() {
    let a: i32 = 10;
    let b: i32 = 20;
    
    let mut result: i32 = 0;
    
    if (a < b) {
        result = b - a;
    } else {
        result = a - b;
    }
    
    let mut i: i32 = 0;
    while (i < 10) {
        result = result + 1;
        i = i + 1;
    }
    
    exit(result);
}
