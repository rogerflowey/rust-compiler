; ModuleID = 'rcompiler'
%String = type {}
%Edge = type { i32, i32, i32, i32, i32 }
%Graph = type { i32, [2000 x %Edge], i32, [100 x [100 x i32]], [100 x i32], [100 x i32], [100 x i1], [100 x [100 x i32]], [100 x i32], [100 x i32], [100 x i1], [100 x i32], i32, i32, i32, [100 x i32], [100 x i32], i32 }

declare dso_local void @__builtin_array_repeat_copy(i8*, i64, i64)
declare i32 @llvm.memcpy.p0i8.p0i8.i64(i8*, i8*, i64, i1)
declare dso_local void @exit(i32)
declare dso_local i32 @getInt()
declare dso_local void @getString(%String*)
declare dso_local void @printInt(i32)
declare dso_local void @printlnInt(i32)
declare dso_local void @println(i8*)
declare dso_local void @print(i8*)

define i32 @pm_rand_update(i32 %param_x) {
entry:
  %local_0 = alloca i32
  %local_1 = alloca i32
  %local_2 = alloca i32
  %local_3 = alloca i32
  %local_4 = alloca i32
  %local_5 = alloca i32
  %local_6 = alloca i32
  store i32 %param_x, i32* %local_0
  %tmp = add i32 0, 16807
  store i32 %tmp, i32* %local_1
  %tmp.1 = add i32 0, 2147483647
  store i32 %tmp.1, i32* %local_2
  %tmp.2 = add i32 0, 127773
  store i32 %tmp.2, i32* %local_3
  %tmp.3 = add i32 0, 2836
  store i32 %tmp.3, i32* %local_4
  %t0 = load i32, i32* %local_0
  %t1 = load i32, i32* %local_3
  %t2 = sdiv i32 %t0, %t1
  store i32 %t2, i32* %local_5
  %t3 = load i32, i32* %local_1
  %t4 = load i32, i32* %local_0
  %t5 = load i32, i32* %local_5
  %t6 = load i32, i32* %local_3
  %t7 = mul i32 %t5, %t6
  %t8 = sub i32 %t4, %t7
  %t9 = mul i32 %t3, %t8
  %t10 = load i32, i32* %local_4
  %t11 = load i32, i32* %local_5
  %t12 = mul i32 %t10, %t11
  %t13 = sub i32 %t9, %t12
  store i32 %t13, i32* %local_6
  %t14 = load i32, i32* %local_6
  %tmp.4 = add i32 0, 0
  %t15 = icmp sle i32 %t14, %tmp.4
  br i1 %t15, label %bb1, label %bb2
bb1:
  %t16 = load i32, i32* %local_6
  %t17 = load i32, i32* %local_2
  %t18 = add i32 %t16, %t17
  store i32 %t18, i32* %local_6
  br label %bb2
bb2:
  %t19 = load i32, i32* %local_6
  ret i32 %t19
}

define void @Edge_new(%Edge* %sret.ptr, i32 %param_from, i32 %param_to, i32 %param_weight, i32 %param_capacity) {
entry:
  %local_0 = alloca i32
  %local_1 = alloca i32
  %local_2 = alloca i32
  %local_3 = alloca i32
  store i32 %param_from, i32* %local_0
  store i32 %param_to, i32* %local_1
  store i32 %param_weight, i32* %local_2
  store i32 %param_capacity, i32* %local_3
  %t1 = load i32, i32* %local_0
  %t2 = load i32, i32* %local_1
  %t3 = load i32, i32* %local_2
  %t4 = load i32, i32* %local_3
  %field = getelementptr inbounds %Edge, %Edge* %t0, i32 0, i32 0
  store i32 %t1, i32* %field
  %field.1 = getelementptr inbounds %Edge, %Edge* %t0, i32 0, i32 1
  store i32 %t2, i32* %field.1
  %field.2 = getelementptr inbounds %Edge, %Edge* %t0, i32 0, i32 2
  store i32 %t3, i32* %field.2
  %field.3 = getelementptr inbounds %Edge, %Edge* %t0, i32 0, i32 3
  store i32 %t4, i32* %field.3
  %field.4 = getelementptr inbounds %Edge, %Edge* %t0, i32 0, i32 4
  %tmp = add i32 0, 0
  store i32 %tmp, i32* %field.4
  ret void
}

define void @Graph_new(%Graph* %sret.ptr, i32 %param_vertices) {
entry:
  %local_0 = alloca i32
  %local_1 = alloca %Graph
  %local_2 = alloca i32
  %local_3 = alloca i32
  store i32 %param_vertices, i32* %local_0
  %t1 = load i32, i32* %local_0
  %tmp = add i32 0, 0
  %proj = getelementptr inbounds %Graph, %Graph* %local_1, i32 0, i32 1, i32 %tmp
  %tmp.1 = add i32 0, 0
  %tmp.2 = add i32 0, 0
  %tmp.3 = add i32 0, 0
  %tmp.4 = add i32 0, 0
  call void @Edge_new(%Edge* %proj, i32 %tmp.1, i32 %tmp.2, i32 %tmp.3, i32 %tmp.4)
  %proj.1 = getelementptr inbounds %Graph, %Graph* %local_1, i32 0, i32 1
  %repeat.ptr = bitcast [2000 x %Edge]* %proj.1 to i8*
  %sizeof.gep = getelementptr inbounds %Edge, %Edge* null, i32 1
  %sizeof = ptrtoint %Edge* %sizeof.gep to i64
  call void @__builtin_array_repeat_copy(i8* %repeat.ptr, i64 %sizeof, i64 2000)
  %tmp.5 = add i32 0, 0
  %proj.2 = getelementptr inbounds %Graph, %Graph* %local_1, i32 0, i32 3, i32 %tmp.5
  %elem0 = getelementptr inbounds [100 x i32], [100 x i32]* %proj.2, i32 0, i32 0
  %tmp.6 = add i32 0, 0
  store i32 %tmp.6, i32* %elem0
  %sizeof.gep.1 = getelementptr inbounds i32, i32* null, i32 1
  %sizeof.1 = ptrtoint i32* %sizeof.gep.1 to i64
  %repeat.ptr.1 = bitcast i32* %elem0 to i8*
  call void @__builtin_array_repeat_copy(i8* %repeat.ptr.1, i64 %sizeof.1, i64 100)
  %proj.3 = getelementptr inbounds %Graph, %Graph* %local_1, i32 0, i32 3
  %repeat.ptr.2 = bitcast [100 x [100 x i32]]* %proj.3 to i8*
  %sizeof.gep.2 = getelementptr inbounds [100 x i32], [100 x i32]* null, i32 1
  %sizeof.2 = ptrtoint [100 x i32]* %sizeof.gep.2 to i64
  call void @__builtin_array_repeat_copy(i8* %repeat.ptr.2, i64 %sizeof.2, i64 100)
  %proj.4 = getelementptr inbounds %Graph, %Graph* %local_1, i32 0, i32 4
  %elem0.1 = getelementptr inbounds [100 x i32], [100 x i32]* %proj.4, i32 0, i32 0
  %tmp.7 = add i32 0, 0
  store i32 %tmp.7, i32* %elem0.1
  %sizeof.gep.3 = getelementptr inbounds i32, i32* null, i32 1
  %sizeof.3 = ptrtoint i32* %sizeof.gep.3 to i64
  %repeat.ptr.3 = bitcast i32* %elem0.1 to i8*
  call void @__builtin_array_repeat_copy(i8* %repeat.ptr.3, i64 %sizeof.3, i64 100)
  %proj.5 = getelementptr inbounds %Graph, %Graph* %local_1, i32 0, i32 5
  %elem0.2 = getelementptr inbounds [100 x i32], [100 x i32]* %proj.5, i32 0, i32 0
  %tmp.8 = add i32 0, 2147483647
  store i32 %tmp.8, i32* %elem0.2
  %sizeof.gep.4 = getelementptr inbounds i32, i32* null, i32 1
  %sizeof.4 = ptrtoint i32* %sizeof.gep.4 to i64
  %repeat.ptr.4 = bitcast i32* %elem0.2 to i8*
  call void @__builtin_array_repeat_copy(i8* %repeat.ptr.4, i64 %sizeof.4, i64 100)
  %proj.6 = getelementptr inbounds %Graph, %Graph* %local_1, i32 0, i32 6
  %elem0.3 = getelementptr inbounds [100 x i1], [100 x i1]* %proj.6, i32 0, i32 0
  %tmp.9 = add i1 0, 0
  store i1 %tmp.9, i1* %elem0.3
  %sizeof.gep.5 = getelementptr inbounds i1, i1* null, i32 1
  %sizeof.5 = ptrtoint i1* %sizeof.gep.5 to i64
  %repeat.ptr.5 = bitcast i1* %elem0.3 to i8*
  call void @__builtin_array_repeat_copy(i8* %repeat.ptr.5, i64 %sizeof.5, i64 100)
  %tmp.10 = add i32 0, 0
  %proj.7 = getelementptr inbounds %Graph, %Graph* %local_1, i32 0, i32 7, i32 %tmp.10
  %elem0.4 = getelementptr inbounds [100 x i32], [100 x i32]* %proj.7, i32 0, i32 0
  %tmp.11 = add i32 0, 2147483647
  store i32 %tmp.11, i32* %elem0.4
  %sizeof.gep.6 = getelementptr inbounds i32, i32* null, i32 1
  %sizeof.6 = ptrtoint i32* %sizeof.gep.6 to i64
  %repeat.ptr.6 = bitcast i32* %elem0.4 to i8*
  call void @__builtin_array_repeat_copy(i8* %repeat.ptr.6, i64 %sizeof.6, i64 100)
  %proj.8 = getelementptr inbounds %Graph, %Graph* %local_1, i32 0, i32 7
  %repeat.ptr.7 = bitcast [100 x [100 x i32]]* %proj.8 to i8*
  %sizeof.gep.7 = getelementptr inbounds [100 x i32], [100 x i32]* null, i32 1
  %sizeof.7 = ptrtoint [100 x i32]* %sizeof.gep.7 to i64
  call void @__builtin_array_repeat_copy(i8* %repeat.ptr.7, i64 %sizeof.7, i64 100)
  %proj.9 = getelementptr inbounds %Graph, %Graph* %local_1, i32 0, i32 8
  %elem0.5 = getelementptr inbounds [100 x i32], [100 x i32]* %proj.9, i32 0, i32 0
  %tmp.12 = add i32 0, 0
  store i32 %tmp.12, i32* %elem0.5
  %sizeof.gep.8 = getelementptr inbounds i32, i32* null, i32 1
  %sizeof.8 = ptrtoint i32* %sizeof.gep.8 to i64
  %repeat.ptr.8 = bitcast i32* %elem0.5 to i8*
  call void @__builtin_array_repeat_copy(i8* %repeat.ptr.8, i64 %sizeof.8, i64 100)
  %proj.10 = getelementptr inbounds %Graph, %Graph* %local_1, i32 0, i32 9
  %elem0.6 = getelementptr inbounds [100 x i32], [100 x i32]* %proj.10, i32 0, i32 0
  %tmp.13 = add i32 0, 0
  store i32 %tmp.13, i32* %elem0.6
  %sizeof.gep.9 = getelementptr inbounds i32, i32* null, i32 1
  %sizeof.9 = ptrtoint i32* %sizeof.gep.9 to i64
  %repeat.ptr.9 = bitcast i32* %elem0.6 to i8*
  call void @__builtin_array_repeat_copy(i8* %repeat.ptr.9, i64 %sizeof.9, i64 100)
  %proj.11 = getelementptr inbounds %Graph, %Graph* %local_1, i32 0, i32 10
  %elem0.7 = getelementptr inbounds [100 x i1], [100 x i1]* %proj.11, i32 0, i32 0
  %tmp.14 = add i1 0, 0
  store i1 %tmp.14, i1* %elem0.7
  %sizeof.gep.10 = getelementptr inbounds i1, i1* null, i32 1
  %sizeof.10 = ptrtoint i1* %sizeof.gep.10 to i64
  %repeat.ptr.10 = bitcast i1* %elem0.7 to i8*
  call void @__builtin_array_repeat_copy(i8* %repeat.ptr.10, i64 %sizeof.10, i64 100)
  %proj.12 = getelementptr inbounds %Graph, %Graph* %local_1, i32 0, i32 11
  %elem0.8 = getelementptr inbounds [100 x i32], [100 x i32]* %proj.12, i32 0, i32 0
  %tmp.15 = add i32 0, 0
  store i32 %tmp.15, i32* %elem0.8
  %sizeof.gep.11 = getelementptr inbounds i32, i32* null, i32 1
  %sizeof.11 = ptrtoint i32* %sizeof.gep.11 to i64
  %repeat.ptr.11 = bitcast i32* %elem0.8 to i8*
  call void @__builtin_array_repeat_copy(i8* %repeat.ptr.11, i64 %sizeof.11, i64 100)
  %proj.13 = getelementptr inbounds %Graph, %Graph* %local_1, i32 0, i32 15
  %elem0.9 = getelementptr inbounds [100 x i32], [100 x i32]* %proj.13, i32 0, i32 0
  %tmp.16 = add i32 0, 0
  store i32 %tmp.16, i32* %elem0.9
  %sizeof.gep.12 = getelementptr inbounds i32, i32* null, i32 1
  %sizeof.12 = ptrtoint i32* %sizeof.gep.12 to i64
  %repeat.ptr.12 = bitcast i32* %elem0.9 to i8*
  call void @__builtin_array_repeat_copy(i8* %repeat.ptr.12, i64 %sizeof.12, i64 100)
  %proj.14 = getelementptr inbounds %Graph, %Graph* %local_1, i32 0, i32 16
  %elem0.10 = getelementptr inbounds [100 x i32], [100 x i32]* %proj.14, i32 0, i32 0
  %tmp.17 = add i32 0, 0
  store i32 %tmp.17, i32* %elem0.10
  %sizeof.gep.13 = getelementptr inbounds i32, i32* null, i32 1
  %sizeof.13 = ptrtoint i32* %sizeof.gep.13 to i64
  %repeat.ptr.13 = bitcast i32* %elem0.10 to i8*
  call void @__builtin_array_repeat_copy(i8* %repeat.ptr.13, i64 %sizeof.13, i64 100)
  %field = getelementptr inbounds %Graph, %Graph* %local_1, i32 0, i32 0
  store i32 %t1, i32* %field
  %field.1 = getelementptr inbounds %Graph, %Graph* %local_1, i32 0, i32 2
  %tmp.18 = add i32 0, 0
  store i32 %tmp.18, i32* %field.1
  %field.2 = getelementptr inbounds %Graph, %Graph* %local_1, i32 0, i32 12
  %tmp.19 = add i32 0, 0
  store i32 %tmp.19, i32* %field.2
  %field.3 = getelementptr inbounds %Graph, %Graph* %local_1, i32 0, i32 13
  %tmp.20 = add i32 0, 0
  store i32 %tmp.20, i32* %field.3
  %field.4 = getelementptr inbounds %Graph, %Graph* %local_1, i32 0, i32 14
  %tmp.21 = add i32 0, 0
  store i32 %tmp.21, i32* %field.4
  %field.5 = getelementptr inbounds %Graph, %Graph* %local_1, i32 0, i32 17
  %tmp.22 = add i32 0, 0
  store i32 %tmp.22, i32* %field.5
  %tmp.23 = add i32 0, 0
  store i32 %tmp.23, i32* %local_2
  br label %bb1
bb1:
  %t2 = load i32, i32* %local_2
  %t3 = load i32, i32* %local_0
  %t4 = icmp slt i32 %t2, %t3
  br i1 %t4, label %bb2, label %bb3
bb2:
  %tmp.24 = add i32 0, 0
  store i32 %tmp.24, i32* %local_3
  br label %bb4
bb3:
  %sizeof.gep.14 = getelementptr inbounds %Graph, %Graph* null, i32 1
  %sizeof.14 = ptrtoint %Graph* %sizeof.gep.14 to i64
  %cpy.dest = bitcast %Graph* %t0 to i8*
  %cpy.src = bitcast %Graph* %local_1 to i8*
  %tmp.25 = call i32 @llvm.memcpy.p0i8.p0i8.i64(i8* %cpy.dest, i8* %cpy.src, i64 %sizeof.14, i1 false)
  ret void
bb4:
  %t5 = load i32, i32* %local_3
  %t6 = load i32, i32* %local_0
  %t7 = icmp slt i32 %t5, %t6
  br i1 %t7, label %bb5, label %bb6
bb5:
  %t8 = load i32, i32* %local_2
  %t9 = load i32, i32* %local_3
  %t10 = icmp eq i32 %t8, %t9
  br i1 %t10, label %bb7, label %bb8
bb6:
  %t23 = load i32, i32* %local_2
  %tmp.26 = add i32 0, 1
  %t24 = add i32 %t23, %tmp.26
  store i32 %t24, i32* %local_2
  br label %bb1
bb7:
  %t11 = load i32, i32* %local_2
  %t12 = add i32 %t11, 0
  %t13 = load i32, i32* %local_3
  %t14 = add i32 %t13, 0
  %t15 = add i32 0, 0
  %proj.15 = getelementptr inbounds %Graph, %Graph* %local_1, i32 0, i32 7, i32 %t12, i32 %t14
  store i32 %t15, i32* %proj.15
  br label %bb9
bb8:
  %t16 = load i32, i32* %local_2
  %t17 = add i32 %t16, 0
  %t18 = load i32, i32* %local_3
  %t19 = add i32 %t18, 0
  %t20 = add i32 0, 2147483647
  %proj.16 = getelementptr inbounds %Graph, %Graph* %local_1, i32 0, i32 7, i32 %t17, i32 %t19
  store i32 %t20, i32* %proj.16
  br label %bb9
bb9:
  %t21 = load i32, i32* %local_3
  %tmp.27 = add i32 0, 1
  %t22 = add i32 %t21, %tmp.27
  store i32 %t22, i32* %local_3
  br label %bb4
}

define void @main() {
entry:
  %local_0 = alloca %Graph
  %tmp = add i32 0, 10
  call void @Graph_new(%Graph* %local_0, i32 %tmp)
  %tmp.1 = add i32 0, 0
  call void @exit(i32 %tmp.1)
  ret void
}
