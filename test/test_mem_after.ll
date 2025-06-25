; ModuleID = './test/test_mem.ll'
source_filename = "../test/test_active.c"
target datalayout = "e-m:e-p270:32:32-p271:32:32-p272:64:64-i64:64-i128:128-f80:128-n8:16:32:64-S128"
target triple = "x86_64-unknown-linux-gnu"

; Function Attrs: nofree norecurse nosync nounwind optsize memory(argmem: readwrite) uwtable
define dso_local void @example(ptr noundef captures(none) %A, i32 noundef %n, i32 noundef %m) local_unnamed_addr #0 {
entry:
  %lso.entry = call i1 @lso.entry.token()
  %idxprom = sext i32 %m to i64
  %arrayidx = getelementptr inbounds i32, ptr %A, i64 %idxprom
  %0 = call i1 @lso.store.i32(ptr %arrayidx, i32 1, i1 %lso.entry)
  %cmp18 = icmp sgt i32 %n, 0
  br i1 %cmp18, label %for.body.preheader, label %for.cond.cleanup

for.body.preheader:                               ; preds = %entry
  %lso.token.phi = phi i1 [ %0, %entry ]
  %wide.trip.count = zext nneg i32 %n to i64
  br label %for.body

for.cond.cleanup:                                 ; preds = %for.body, %entry
  %lso.token.phi1 = phi i1 [ %3, %for.body ], [ %0, %entry ]
  ret void

for.body:                                         ; preds = %for.body, %for.body.preheader
  %lso.token.phi2 = phi i1 [ %3, %for.body ], [ %lso.token.phi, %for.body.preheader ]
  %indvars.iv = phi i64 [ 0, %for.body.preheader ], [ %indvars.iv.next, %for.body ]
  %arrayidx2 = getelementptr inbounds nuw i32, ptr %A, i64 %indvars.iv
  %1 = call i32 @lso.load.i32(ptr %arrayidx2, i1 %lso.token.phi2)
  %cmp3 = icmp sgt i32 %1, 42
  %spec.select = select i1 %cmp3, i32 0, i32 %1
  %2 = trunc nuw nsw i64 %indvars.iv to i32
  %add = add nsw i32 %1, %2
  %add8 = add nsw i32 %add, %spec.select
  %3 = call i1 @lso.store.i32(ptr %arrayidx2, i32 %add8, i1 %lso.token.phi2)
  %indvars.iv.next = add nuw nsw i64 %indvars.iv, 1
  %exitcond.not = icmp eq i64 %indvars.iv.next, %wide.trip.count
  br i1 %exitcond.not, label %for.cond.cleanup, label %for.body, !llvm.loop !5
}

declare i1 @lso.entry.token()

declare i1 @lso.store.i32(ptr, i32, i1)

declare i32 @lso.load.i32(ptr, i1)

attributes #0 = { nofree norecurse nosync nounwind optsize memory(argmem: readwrite) uwtable "min-legal-vector-width"="0" "no-trapping-math"="true" "stack-protector-buffer-size"="8" "target-cpu"="x86-64" "target-features"="+cmov,+cx8,+fxsr,+mmx,+sse,+sse2,+x87" "tune-cpu"="generic" }

!llvm.module.flags = !{!0, !1, !2, !3}
!llvm.ident = !{!4}

!0 = !{i32 1, !"wchar_size", i32 4}
!1 = !{i32 8, !"PIC Level", i32 2}
!2 = !{i32 7, !"PIE Level", i32 2}
!3 = !{i32 7, !"uwtable", i32 2}
!4 = !{!"clang version 21.0.0git (https://github.com/llvm/llvm-project.git 099a0fa3f2cf1a434e20b2fa97b6251088321467)"}
!5 = distinct !{!5, !6}
!6 = !{!"llvm.loop.mustprogress"}
