struct Point { x: i32, y: i32 }

fn add(p_1: Point, p_2: Point) -> Point {
    return Point { x: p_1.x + p_2.x, y: p_1.y + p_2.y };
}

fn main() {
    let p_1 = Point { x: 10, y: 20 };
    let p_2 = Point { x: 30, y: 40 };
    let val = add(p_1, p_2);
    exit(0);
}
