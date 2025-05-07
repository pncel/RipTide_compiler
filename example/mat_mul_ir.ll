; ModuleID = 'mat_mul.c'
source_filename = "mat_mul.c"
target datalayout = "e-m:e-p270:32:32-p271:32:32-p272:64:64-i64:64-i128:128-f80:128-n8:16:32:64-S128"
target triple = "x86_64-pc-linux-gnu"

; Function Attrs: nofree norecurse nosync nounwind memory(argmem: readwrite) uwtable
define dso_local void @matrixMultiply(ptr nocapture noundef readonly %0, ptr nocapture noundef readonly %1, ptr nocapture noundef writeonly %2, i32 noundef %3, i32 noundef %4, i32 noundef %5) local_unnamed_addr #0 {
  %7 = icmp sgt i32 %3, 0
  br i1 %7, label %8, label %28

8:                                                ; preds = %6
  %9 = icmp sgt i32 %5, 0
  %10 = icmp sgt i32 %4, 0
  %11 = sext i32 %5 to i64
  %12 = zext nneg i32 %3 to i64
  %13 = zext nneg i32 %5 to i64
  %14 = zext i32 %4 to i64
  %15 = and i64 %14, 3
  %16 = icmp ult i32 %4, 4
  %17 = and i64 %14, 2147483644
  %18 = icmp eq i64 %15, 0
  br label %19

19:                                               ; preds = %8, %33
  %20 = phi i64 [ 0, %8 ], [ %34, %33 ]
  br i1 %9, label %21, label %33

21:                                               ; preds = %19
  %22 = mul nsw i64 %20, %11
  %23 = trunc i64 %20 to i32
  %24 = mul i32 %23, %4
  %25 = zext i32 %24 to i64
  %26 = getelementptr float, ptr %0, i64 %25
  %27 = getelementptr float, ptr %2, i64 %22
  br label %29

28:                                               ; preds = %33, %6
  ret void

29:                                               ; preds = %21, %53
  %30 = phi i64 [ 0, %21 ], [ %56, %53 ]
  br i1 %10, label %31, label %53

31:                                               ; preds = %29
  %32 = getelementptr float, ptr %1, i64 %30
  br i1 %16, label %36, label %58

33:                                               ; preds = %53, %19
  %34 = add nuw nsw i64 %20, 1
  %35 = icmp eq i64 %34, %12
  br i1 %35, label %28, label %19, !llvm.loop !5

36:                                               ; preds = %58, %31
  %37 = phi float [ undef, %31 ], [ %88, %58 ]
  %38 = phi i64 [ 0, %31 ], [ %89, %58 ]
  %39 = phi float [ 0.000000e+00, %31 ], [ %88, %58 ]
  br i1 %18, label %53, label %40

40:                                               ; preds = %36, %40
  %41 = phi i64 [ %50, %40 ], [ %38, %36 ]
  %42 = phi float [ %49, %40 ], [ %39, %36 ]
  %43 = phi i64 [ %51, %40 ], [ 0, %36 ]
  %44 = getelementptr float, ptr %26, i64 %41
  %45 = load float, ptr %44, align 4, !tbaa !7
  %46 = mul nsw i64 %41, %11
  %47 = getelementptr float, ptr %32, i64 %46
  %48 = load float, ptr %47, align 4, !tbaa !7
  %49 = tail call float @llvm.fmuladd.f32(float %45, float %48, float %42)
  %50 = add nuw nsw i64 %41, 1
  %51 = add i64 %43, 1
  %52 = icmp eq i64 %51, %15
  br i1 %52, label %53, label %40, !llvm.loop !11

53:                                               ; preds = %36, %40, %29
  %54 = phi float [ 0.000000e+00, %29 ], [ %37, %36 ], [ %49, %40 ]
  %55 = getelementptr float, ptr %27, i64 %30
  store float %54, ptr %55, align 4, !tbaa !7
  %56 = add nuw nsw i64 %30, 1
  %57 = icmp eq i64 %56, %13
  br i1 %57, label %33, label %29, !llvm.loop !13

58:                                               ; preds = %31, %58
  %59 = phi i64 [ %89, %58 ], [ 0, %31 ]
  %60 = phi float [ %88, %58 ], [ 0.000000e+00, %31 ]
  %61 = phi i64 [ %90, %58 ], [ 0, %31 ]
  %62 = getelementptr float, ptr %26, i64 %59
  %63 = load float, ptr %62, align 4, !tbaa !7
  %64 = mul nsw i64 %59, %11
  %65 = getelementptr float, ptr %32, i64 %64
  %66 = load float, ptr %65, align 4, !tbaa !7
  %67 = tail call float @llvm.fmuladd.f32(float %63, float %66, float %60)
  %68 = or disjoint i64 %59, 1
  %69 = getelementptr float, ptr %26, i64 %68
  %70 = load float, ptr %69, align 4, !tbaa !7
  %71 = mul nsw i64 %68, %11
  %72 = getelementptr float, ptr %32, i64 %71
  %73 = load float, ptr %72, align 4, !tbaa !7
  %74 = tail call float @llvm.fmuladd.f32(float %70, float %73, float %67)
  %75 = or disjoint i64 %59, 2
  %76 = getelementptr float, ptr %26, i64 %75
  %77 = load float, ptr %76, align 4, !tbaa !7
  %78 = mul nsw i64 %75, %11
  %79 = getelementptr float, ptr %32, i64 %78
  %80 = load float, ptr %79, align 4, !tbaa !7
  %81 = tail call float @llvm.fmuladd.f32(float %77, float %80, float %74)
  %82 = or disjoint i64 %59, 3
  %83 = getelementptr float, ptr %26, i64 %82
  %84 = load float, ptr %83, align 4, !tbaa !7
  %85 = mul nsw i64 %82, %11
  %86 = getelementptr float, ptr %32, i64 %85
  %87 = load float, ptr %86, align 4, !tbaa !7
  %88 = tail call float @llvm.fmuladd.f32(float %84, float %87, float %81)
  %89 = add nuw nsw i64 %59, 4
  %90 = add i64 %61, 4
  %91 = icmp eq i64 %90, %17
  br i1 %91, label %36, label %58, !llvm.loop !14
}

; Function Attrs: mustprogress nocallback nofree nosync nounwind speculatable willreturn memory(none)
declare float @llvm.fmuladd.f32(float, float, float) #1

attributes #0 = { nofree norecurse nosync nounwind memory(argmem: readwrite) uwtable "min-legal-vector-width"="0" "no-trapping-math"="true" "stack-protector-buffer-size"="8" "target-cpu"="x86-64" "target-features"="+cmov,+cx8,+fxsr,+mmx,+sse,+sse2,+x87" "tune-cpu"="generic" }
attributes #1 = { mustprogress nocallback nofree nosync nounwind speculatable willreturn memory(none) }

!llvm.module.flags = !{!0, !1, !2, !3}
!llvm.ident = !{!4}

!0 = !{i32 1, !"wchar_size", i32 4}
!1 = !{i32 8, !"PIC Level", i32 2}
!2 = !{i32 7, !"PIE Level", i32 2}
!3 = !{i32 7, !"uwtable", i32 2}
!4 = !{!"Ubuntu clang version 18.1.3 (1ubuntu1)"}
!5 = distinct !{!5, !6}
!6 = !{!"llvm.loop.mustprogress"}
!7 = !{!8, !8, i64 0}
!8 = !{!"float", !9, i64 0}
!9 = !{!"omnipotent char", !10, i64 0}
!10 = !{!"Simple C/C++ TBAA"}
!11 = distinct !{!11, !12}
!12 = !{!"llvm.loop.unroll.disable"}
!13 = distinct !{!13, !6}
!14 = distinct !{!14, !6}
