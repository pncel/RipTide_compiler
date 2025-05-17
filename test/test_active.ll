; ModuleID = '../test/test_active.c'
source_filename = "../test/test_active.c"
target datalayout = "e-m:e-p270:32:32-p271:32:32-p272:64:64-i64:64-i128:128-f80:128-n8:16:32:64-S128"
target triple = "x86_64-unknown-linux-gnu"

; Function Attrs: mustprogress nofree norecurse nosync nounwind willreturn memory(none) uwtable
define dso_local i32 @example1(i32 noundef %A, i32 noundef %n) local_unnamed_addr #0 {
entry:
  %add = add nsw i32 %A, 8
  %cmp = icmp sgt i32 %A, 34
  %foo.0 = add i32 %add, %n
  %add1 = shl i32 %n, 1
  %0 = add i32 %add1, 18
  %reass.add = select i1 %cmp, i32 %0, i32 0
  %add4 = add i32 %foo.0, %reass.add
  ret i32 %add4
}

attributes #0 = { mustprogress nofree norecurse nosync nounwind willreturn memory(none) uwtable "min-legal-vector-width"="0" "no-trapping-math"="true" "stack-protector-buffer-size"="8" "target-cpu"="x86-64" "target-features"="+cmov,+cx8,+fxsr,+mmx,+sse,+sse2,+x87" "tune-cpu"="generic" }

!llvm.module.flags = !{!0, !1, !2, !3}
!llvm.ident = !{!4}

!0 = !{i32 1, !"wchar_size", i32 4}
!1 = !{i32 8, !"PIC Level", i32 2}
!2 = !{i32 7, !"PIE Level", i32 2}
!3 = !{i32 7, !"uwtable", i32 2}
!4 = !{!"clang version 21.0.0git (https://github.com/llvm/llvm-project.git 099a0fa3f2cf1a434e20b2fa97b6251088321467)"}
