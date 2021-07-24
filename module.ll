%Box = type { i64, void (%Box*)* }
%Cell = type { i64, %Box* }
%List = type { %Box, i64, i64, [0 x %Box*] }

declare i8* @malloc(i64)
declare void @free(i8*)

define private void @box_rc_decr(%Box* %val) {
  %rc_ptr = getelementptr %Box, %Box* %val, i64 0, i32 0
  %rc = load i64, i64* %rc_ptr
  %rc_is_0 = icmp eq i64 %rc, 0
  br i1 %rc_is_0, label %free, label %decr
free:
  %dtor_ptr = getelementptr %Box, %Box* %val, i64 0, i32 1
  %dtor = load void (%Box*)*, void (%Box*)** %dtor_ptr
  call void %dtor(%Box* %val)
  ret void
decr:
  %rc_m_1 = sub i64 %rc, 1
  store i64 %rc_m_1, i64* %rc_ptr
  ret void
}

define private void @cell_rc_decr(%Cell* %cell) {
  %rc_ptr = getelementptr %Cell, %Cell* %cell, i64 0, i32 0
  %rc = load i64, i64* %rc_ptr
  %rc_is_0 = icmp eq i64 %rc, 0
  br i1 %rc_is_0, label %free, label %decr
free:
  %box_ptr = getelementptr %Cell, %Cell* %cell, i64 0, i32 1
  %box = load %Box*, %Box** %box_ptr
  call void @box_rc_decr(%Box* %box)
  %cell_vp = bitcast %Cell* %cell to i8*
  call void @free(i8* %cell_vp)
  ret void
decr:
  %rc_m_1 = sub i64 %rc, 1
  store i64 %rc_m_1, i64* %rc_ptr
  ret void
}

define private void @list_dtor(%Box* %val) {
entry:
  %list = bitcast %Box* %val to %List*
  %len_ptr = getelementptr %List, %List* %list, i64 0, i32 1
  %len = load i64, i64* %len_ptr
  br label %loop
loop:
  %i = phi i64 [ 0, %entry ], [ %i_p_1, %loop ]
  %elem_ptr = getelementptr %List, %List* %list, i64 0, i32 3, i64 %i
  %elem = load %Box*, %Box** %elem_ptr
  call void @box_rc_decr(%Box* %elem)
  %i_p_1 = add i64 %i, 1
  %loop_cont = icmp ult i64 %i_p_1, %len
  br i1 %loop_cont, label %loop, label %end
end:
  %val_vp = bitcast %Box* %val to i8*
  call void @free(i8* %val_vp)
  ret void
}
