; ModuleID = '../test/test_active.c'
source_filename = "../test/test_active.c"
target datalayout = "e-m:e-p270:32:32-p271:32:32-p272:64:64-i64:64-i128:128-f80:128-n8:16:32:64-S128"
target triple = "x86_64-unknown-linux-gnu"

; Function Attrs: mustprogress nofree norecurse nosync nounwind optsize willreturn memory(argmem: readwrite) uwtable
define dso_local void @example(ptr noundef captures(none) initializes((0, 4)) %A, i32 noundef %n, i32 noundef %m) local_unnamed_addr #0 {
entry:
  store i32 69, ptr %A, align 4, !tbaa !5
  %0 = sext i32 %m to i64
  %1 = getelementptr i32, ptr %A, i64 %0
  %arrayidx1 = getelementptr i8, ptr %1, i64 8
  store i32 1, ptr %arrayidx1, align 4, !tbaa !5
  %sub = add nsw i32 %n, -6
  %arrayidx2 = getelementptr inbounds nuw i8, ptr %A, i64 4
  %2 = load i32, ptr %arrayidx2, align 4, !tbaa !5
  %cmp = icmp sgt i32 %sub, %2
  br i1 %cmp, label %if.then, label %if.end

if.then:                                          ; preds = %entry
  store i32 %sub, ptr %arrayidx2, align 4, !tbaa !5
  br label %if.end

if.end:                                           ; preds = %if.then, %entry
  ret void
}

attributes #0 = { mustprogress nofree norecurse nosync nounwind optsize willreturn memory(argmem: readwrite) uwtable "min-legal-vector-width"="0" "no-trapping-math"="true" "stack-protector-buffer-size"="8" "target-cpu"="x86-64" "target-features"="+cmov,+cx8,+fxsr,+mmx,+sse,+sse2,+x87" "tune-cpu"="generic" }

!llvm.module.flags = !{!0, !1, !2, !3}
!llvm.ident = !{!4}

!0 = !{i32 1, !"wchar_size", i32 4}
!1 = !{i32 8, !"PIC Level", i32 2}
!2 = !{i32 7, !"PIE Level", i32 2}
!3 = !{i32 7, !"uwtable", i32 2}
!4 = !{!"clang version 21.0.0git (https://github.com/llvm/llvm-project.git 099a0fa3f2cf1a434e20b2fa97b6251088321467)"}
!5 = !{!6, !6, i64 0}
!6 = !{!"int", !7, i64 0}
!7 = !{!"omnipotent char", !8, i64 0}
!8 = !{!"Simple C/C++ TBAA"}
