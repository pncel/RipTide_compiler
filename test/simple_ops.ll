; ModuleID = '../test/simple_ops.c'
source_filename = "../test/simple_ops.c"
target datalayout = "e-m:e-p270:32:32-p271:32:32-p272:64:64-i64:64-i128:128-f80:128-n8:16:32:64-S128"
target triple = "x86_64-unknown-linux-gnu"

; Function Attrs: nofree norecurse nosync nounwind memory(argmem: readwrite) uwtable
define dso_local void @example(ptr noundef captures(none) %A, i32 noundef %n, i32 noundef %m) local_unnamed_addr #0 {
entry:
  %idxprom = sext i32 %m to i64
  %arrayidx = getelementptr inbounds i32, ptr %A, i64 %idxprom
  store i32 1, ptr %arrayidx, align 4, !tbaa !5
  %cmp18 = icmp sgt i32 %n, 0
  br i1 %cmp18, label %for.body.preheader, label %for.cond.cleanup

for.body.preheader:                               ; preds = %entry
  %wide.trip.count = zext nneg i32 %n to i64
  %min.iters.check = icmp ult i32 %n, 8
  br i1 %min.iters.check, label %for.body.preheader22, label %vector.ph

vector.ph:                                        ; preds = %for.body.preheader
  %n.vec = and i64 %wide.trip.count, 2147483640
  br label %vector.body

vector.body:                                      ; preds = %vector.body, %vector.ph
  %index = phi i64 [ 0, %vector.ph ], [ %index.next, %vector.body ]
  %vec.ind = phi <4 x i32> [ <i32 0, i32 1, i32 2, i32 3>, %vector.ph ], [ %vec.ind.next, %vector.body ]
  %step.add = add <4 x i32> %vec.ind, splat (i32 4)
  %0 = getelementptr inbounds nuw i32, ptr %A, i64 %index
  %1 = getelementptr inbounds nuw i8, ptr %0, i64 16
  %wide.load = load <4 x i32>, ptr %0, align 4, !tbaa !5
  %wide.load21 = load <4 x i32>, ptr %1, align 4, !tbaa !5
  %2 = icmp sgt <4 x i32> %wide.load, splat (i32 42)
  %3 = icmp sgt <4 x i32> %wide.load21, splat (i32 42)
  %4 = select <4 x i1> %2, <4 x i32> zeroinitializer, <4 x i32> %wide.load
  %5 = select <4 x i1> %3, <4 x i32> zeroinitializer, <4 x i32> %wide.load21
  %6 = add nsw <4 x i32> %wide.load, %vec.ind
  %7 = add nsw <4 x i32> %wide.load21, %step.add
  %8 = add nsw <4 x i32> %6, %4
  %9 = add nsw <4 x i32> %7, %5
  store <4 x i32> %8, ptr %0, align 4, !tbaa !5
  store <4 x i32> %9, ptr %1, align 4, !tbaa !5
  %index.next = add nuw i64 %index, 8
  %vec.ind.next = add <4 x i32> %vec.ind, splat (i32 8)
  %10 = icmp eq i64 %index.next, %n.vec
  br i1 %10, label %middle.block, label %vector.body, !llvm.loop !9

middle.block:                                     ; preds = %vector.body
  %cmp.n = icmp eq i64 %n.vec, %wide.trip.count
  br i1 %cmp.n, label %for.cond.cleanup, label %for.body.preheader22

for.body.preheader22:                             ; preds = %for.body.preheader, %middle.block
  %indvars.iv.ph = phi i64 [ 0, %for.body.preheader ], [ %n.vec, %middle.block ]
  br label %for.body

for.cond.cleanup:                                 ; preds = %for.body, %middle.block, %entry
  ret void

for.body:                                         ; preds = %for.body.preheader22, %for.body
  %indvars.iv = phi i64 [ %indvars.iv.next, %for.body ], [ %indvars.iv.ph, %for.body.preheader22 ]
  %arrayidx2 = getelementptr inbounds nuw i32, ptr %A, i64 %indvars.iv
  %11 = load i32, ptr %arrayidx2, align 4, !tbaa !5
  %cmp3 = icmp sgt i32 %11, 42
  %spec.select = select i1 %cmp3, i32 0, i32 %11
  %12 = trunc nuw nsw i64 %indvars.iv to i32
  %add = add nsw i32 %11, %12
  %add8 = add nsw i32 %add, %spec.select
  store i32 %add8, ptr %arrayidx2, align 4, !tbaa !5
  %indvars.iv.next = add nuw nsw i64 %indvars.iv, 1
  %exitcond.not = icmp eq i64 %indvars.iv.next, %wide.trip.count
  br i1 %exitcond.not, label %for.cond.cleanup, label %for.body, !llvm.loop !13
}
