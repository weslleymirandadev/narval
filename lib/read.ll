declare i8* @malloc(i64)
declare i32 @getchar()
declare void @free(i8*)

define i8* @nv_read() {
  %buffer_size = alloca i64
  store i64 128, i64* %buffer_size

  %size = load i64, i64* %buffer_size
  %1 = call i8* @malloc(i64 %size)
  %buffer = alloca i8*
  store i8* %1, i8** %buffer

  %index = alloca i64
  store i64 0, i64* %index

  br label %read_loop

read_loop:
  %curr_idx = load i64, i64* %index
  %curr_size = load i64, i64* %buffer_size

  ; Verifica se precisa realocar (deixa 1 byte para \0)
  %need_more = icmp eq i64 %curr_idx, %curr_size
  br i1 %need_more, label %resize, label %read_char

resize:
  %new_size = mul i64 %curr_size, 2
  store i64 %new_size, i64* %buffer_size

  %old_buf = load i8*, i8** %buffer
  %2 = call i8* @malloc(i64 %new_size)
  %new_buf = insertvalue { i8*, i64 } undef, i8* %2, 0

  ; Copiar dados antigos (pode ser otimizado com memcpy, mas aqui manual)
  br label %copy_loop

copy_loop:
  %copy_idx = phi i64 [ 0, %resize ], [ %next_copy_idx, %copy_loop_body ]
  %cmp_copy = icmp ult i64 %copy_idx, %curr_idx
  br i1 %cmp_copy, label %copy_loop_body, label %copy_done

copy_loop_body:
  %src_ptr = getelementptr inbounds i8, i8* %old_buf, i64 %copy_idx
  %dst_ptr = getelementptr inbounds i8, i8* %2, i64 %copy_idx
  %char = load i8, i8* %src_ptr
  store i8 %char, i8* %dst_ptr
  %next_copy_idx = add i64 %copy_idx, 1
  br label %copy_loop

copy_done:
  call void @free(i8* %old_buf)
  store i8* %2, i8** %buffer
  br label %read_char

read_char:
  %ch_i32 = call i32 @getchar()
  %ch = trunc i32 %ch_i32 to i8

  ; EOF ou erro?
  %is_eof = icmp eq i32 %ch_i32, -1
  br i1 %is_eof, label %eof_check, label %store_char

eof_check:
  %curr_idx2 = load i64, i64* %index
  %is_empty = icmp eq i64 %curr_idx2, 0
  br i1 %is_empty, label %return_null, label %finalize

store_char:
  ; Quebra em \n
  %is_newline = icmp eq i8 %ch, 10
  br i1 %is_newline, label %finalize, label %append

append:
  %buf_ptr = load i8*, i8** %buffer
  %idx = load i64, i64* %index
  %ptr = getelementptr inbounds i8, i8* %buf_ptr, i64 %idx
  store i8 %ch, i8* %ptr

  %new_idx = add i64 %idx, 1
  store i64 %new_idx, i64* %index
  br label %read_loop

finalize:
  %final_buf = load i8*, i8** %buffer
  %final_idx = load i64, i64* %index
  %null_ptr = getelementptr inbounds i8, i8* %final_buf, i64 %final_idx
  store i8 0, i8* %null_ptr
  ret i8* %final_buf

return_null:
  ret i8* null
}