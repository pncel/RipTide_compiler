; ModuleID = '../test/test_cfg.c'
source_filename = "../test/test_cfg.c"
target datalayout = "e-m:e-p270:32:32-p271:32:32-p272:64:64-i64:64-i128:128-f80:128-n8:16:32:64-S128"
target triple = "x86_64-unknown-linux-gnu"

; Function Attrs: mustprogress nofree norecurse nosync nounwind willreturn memory(none) uwtable
define dso_local range(i32 -2147483645, -2147483648) i32 @test_cfg(i32 noundef %a, i32 noundef %b) local_unnamed_addr #0 {
entry:
  %cmp = icmp sgt i32 %a, 0
  br i1 %cmp, label %if.then, label %for.cond

if.then:                                          ; preds = %entry
  %cmp1 = icmp sgt i32 %b, 0
  br i1 %cmp1, label %if.then2, label %if.else

if.then2:                                         ; preds = %if.then
  %add = add nuw nsw i32 %b, %a
  br label %if.end17

if.else:                                          ; preds = %if.then
  %sub = sub nsw i32 %a, %b
  br label %if.end17

for.cond:                                         ; preds = %entry
  %add12.1 = shl i32 %a, 1
  %add13.1 = or disjoint i32 %add12.1, 1
  %cmp14.1 = icmp sgt i32 %add12.1, 9
  br i1 %cmp14.1, label %if.end17, label %for.cond.1

for.cond.1:                                       ; preds = %for.cond
  %0 = mul i32 %a, 3
  %1 = add i32 %0, 3
  %add12.2 = add i32 %add13.1, %a
  %add13.2 = add i32 %add12.2, 2
  %cmp14.2 = icmp sgt i32 %add13.2, 10
  %spec.select = select i1 %cmp14.2, i32 %add13.2, i32 %1
  br label %if.end17

if.end17:                                         ; preds = %for.cond.1, %for.cond, %if.else, %if.then2
  %sum.2 = phi i32 [ %add, %if.then2 ], [ %sub, %if.else ], [ %add13.1, %for.cond ], [ %spec.select, %for.cond.1 ]
  %rem = srem i32 %b, 3
  %switch.selectcmp = icmp eq i32 %rem, 1
  %switch.select = select i1 %switch.selectcmp, i32 5, i32 7
  %switch.selectcmp43 = icmp eq i32 %rem, 0
  %switch.select44 = select i1 %switch.selectcmp43, i32 3, i32 %switch.select
  %add21 = add nsw i32 %sum.2, %switch.select44
  ret i32 %add21
}

attributes #0 = { mustprogress nofree norecurse nosync nounwind willreturn memory(none) uwtable "min-legal-vector-width"="0" "no-trapping-math"="true" "stack-protector-buffer-size"="8" "target-cpu"="x86-64" "target-features"="+cmov,+cx8,+fxsr,+mmx,+sse,+sse2,+x87" "tune-cpu"="generic" }

!llvm.module.flags = !{!0, !1, !2, !3}
!llvm.ident = !{!4}

!0 = !{i32 1, !"wchar_size", i32 4}
!1 = !{i32 8, !"PIC Level", i32 2}
!2 = !{i32 7, !"PIE Level", i32 2}
!3 = !{i32 7, !"uwtable", i32 2}
!4 = !{!"clang version 21.0.0git (https://github.com/llvm/llvm-project.git 099a0fa3f2cf1a434e20b2fa97b6251088321467)"}
