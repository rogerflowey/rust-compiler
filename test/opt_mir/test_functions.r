fn add(x: i32, y: i32) -> i32 {
    return x + y;
}

fn sub(x: i32, y: i32) -> i32 {
    return x - y;
}

fn main() {
    let a: i32 = 100;
    let b: i32 = 50;
    
    let sum = add(a, b);
    let diff = sub(a, b);
    
    exit(sub(sum, diff));
}
