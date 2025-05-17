; ModuleID = '../test/simpler_ops.c'
source_filename = "../test/simpler_ops.c"
target datalayout = "e-m:e-p270:32:32-p271:32:32-p272:64:64-i64:64-i128:128-f80:128-n8:16:32:64-S128"
target triple = "x86_64-unknown-linux-gnu"

; Function Attrs: mustprogress nofree norecurse nosync nounwind willreturn memory(none) uwtable
define dso_local i32 @example1(i32 noundef %A, i32 noundef %n) local_unnamed_addr #0 {
entry:
  %add = add nsw i32 %A, 8
  %cmp = icmp sgt i32 %A, 34
  %add1 = add nsw i32 %n, 9
  %add2 = add nsw i32 %add1, %add
  %add3 = add nsw i32 %add, %A
  %mul = mul nsw i32 %add, %add
  %foo.0 = select i1 %cmp, i32 %add2, i32 %mul
  %output.0 = select i1 %cmp, i32 %add1, i32 %add3
  %add4 = add i32 %output.0, %n
  %add5 = add i32 %add4, %foo.0
  ret i32 %add5
}

attributes #0 = { mustprogress nofree norecurse nosync nounwind willreturn memory(none) uwtable "min-legal-vector-width"="0" "no-trapping-math"="true" "stack-protector-buffer-size"="8" "target-cpu"="x86-64" "target-features"="+cmov,+cx8,+fxsr,+mmx,+sse,+sse2,+x87" "tune-cpu"="generic" }

!llvm.module.flags = !{!0, !1, !2, !3}
!llvm.ident = !{!4}

!0 = !{i32 1, !"wchar_size", i32 4}
!1 = !{i32 8, !"PIC Level", i32 2}
!2 = !{i32 7, !"PIE Level", i32 2}
!3 = !{i32 7, !"uwtable", i32 2}
!4 = !{!"clang version 21.0.0git (https://github.com/llvm/llvm-project.git 099a0fa3f2cf1a434e20b2fa97b6251088321467)"}
