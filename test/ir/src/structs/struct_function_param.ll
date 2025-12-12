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

define i32 @sum_coords(%Point* %param_p) {
entry:
  %proj = getelementptr inbounds %Point, %Point* %param_0, i32 0, i32 0
  %t0 = load i32, i32* %proj
  %proj.1 = getelementptr inbounds %Point, %Point* %param_0, i32 0, i32 1
  %t1 = load i32, i32* %proj.1
  %t2 = add i32 %t0, %t1
  ret i32 %t2
}

define void @main() {
entry:
  %local_0 = alloca %Point
  %local_1 = alloca %Point
  %field = getelementptr inbounds %Point, %Point* %local_0, i32 0, i32 0
  %tmp = add i32 0, 3
  store i32 %tmp, i32* %field
  %field.1 = getelementptr inbounds %Point, %Point* %local_0, i32 0, i32 1
  %tmp.1 = add i32 0, 7
  store i32 %tmp.1, i32* %field.1
  %sizeof.gep = getelementptr inbounds %Point, %Point* null, i32 1
  %sizeof = ptrtoint %Point* %sizeof.gep to i64
  %cpy.dest = bitcast %Point* %local_1 to i8*
  %cpy.src = bitcast %Point* %local_0 to i8*
  %tmp.2 = call i32 @llvm.memcpy.p0i8.p0i8.i64(i8* %cpy.dest, i8* %cpy.src, i64 %sizeof, i1 false)
  %t0 = call i32 @sum_coords(%Point* %local_1)
  call void @printlnInt(i32 %t0)
  %tmp.3 = add i32 0, 0
  call void @exit(i32 %tmp.3)
  ret void
}
