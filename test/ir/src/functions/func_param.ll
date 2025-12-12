; ModuleID = 'rcompiler'
%String = type {}
%Point = type { i32, i32 }

declare dso_local void @__builtin_array_repeat_copy(i8*, i64, i64)
declare i32 @llvm.memcpy.p0i8.p0i8.i64(i8*, i8*, i64, i1)
declare dso_local void @exit(i32)
declare dso_local i32 @getInt()
declare dso_local void @getString(%String*)
declare dso_local void @printInt(i32)
declare dso_local void @printlnInt(i32)
declare dso_local void @println(i8*)
declare dso_local void @print(i8*)

define void @add(%Point* %sret.ptr, %Point* %param_a, %Point* %param_b) {
entry:
  %proj = getelementptr inbounds %Point, %Point* %param_a, i32 0, i32 0
  %t0 = load i32, i32* %proj
  %proj.1 = getelementptr inbounds %Point, %Point* %param_b, i32 0, i32 0
  %t1 = load i32, i32* %proj.1
  %t2 = add i32 %t0, %t1
  %proj.2 = getelementptr inbounds %Point, %Point* %param_a, i32 0, i32 1
  %t3 = load i32, i32* %proj.2
  %proj.3 = getelementptr inbounds %Point, %Point* %param_b, i32 0, i32 1
  %t4 = load i32, i32* %proj.3
  %t5 = add i32 %t3, %t4
  %field = getelementptr inbounds %Point, %Point* %sret.ptr, i32 0, i32 0
  store i32 %t2, i32* %field
  %field.1 = getelementptr inbounds %Point, %Point* %sret.ptr, i32 0, i32 1
  store i32 %t5, i32* %field.1
  ret void
}

define void @main() {
entry:
  %local_0 = alloca %Point
  %local_1 = alloca %Point
  %local_2 = alloca %Point
  %field = getelementptr inbounds %Point, %Point* %local_0, i32 0, i32 0
  %tmp = add i32 0, 1
  store i32 %tmp, i32* %field
  %field.1 = getelementptr inbounds %Point, %Point* %local_0, i32 0, i32 1
  %tmp.1 = add i32 0, 2
  store i32 %tmp.1, i32* %field.1
  %field.2 = getelementptr inbounds %Point, %Point* %local_1, i32 0, i32 0
  %tmp.2 = add i32 0, 3
  store i32 %tmp.2, i32* %field.2
  %field.3 = getelementptr inbounds %Point, %Point* %local_1, i32 0, i32 1
  %tmp.3 = add i32 0, 4
  store i32 %tmp.3, i32* %field.3
  %t0 = load %Point, %Point* %local_0
  %t1 = load %Point, %Point* %local_1
  call void @add(%Point* %local_2, %Point %t0, %Point %t1)
  %proj = getelementptr inbounds %Point, %Point* %local_2, i32 0, i32 0
  %t2 = load i32, i32* %proj
  call void @printlnInt(i32 %t2)
  %proj.1 = getelementptr inbounds %Point, %Point* %local_2, i32 0, i32 1
  %t3 = load i32, i32* %proj.1
  call void @printlnInt(i32 %t3)
  %tmp.4 = add i32 0, 0
  call void @exit(i32 %tmp.4)
  ret void
}
