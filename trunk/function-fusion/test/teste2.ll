; ModuleID = 'teste.ll'
target datalayout = "e-p:64:64:64-i1:8:8-i8:8:8-i16:16:16-i32:32:32-i64:64:64-f32:32:32-f64:64:64-v64:64:64-v128:128:128-a0:0:64-s0:64:64-f80:128:128-n8:16:32:64-S128"
target triple = "x86_64-unknown-linux-gnu"

@.str = private unnamed_addr constant [3 x i8] c"%d\00", align 1

define internal i32 @add.alwaysinline(i32 %a, i32 %b) #0 {
entry:
  %add = add nsw i32 %a, %b
  ret i32 %add
}

define i32 @add(i32 %a, i32 %b) #1 {
entry:
  %add = add nsw i32 %a, %b
  ret i32 %add
}

define internal i32 @muladd0.fused(i32 %adda, i32 %addb, i32 %muly) {
entry:
  %0 = call i32 @add.alwaysinline(i32 %adda, i32 %addb)
  %1 = call i32 @mul.alwaysinline(i32 %0, i32 %muly)
  ret i32 %1
}

define internal i32 @mul.alwaysinline(i32 %x, i32 %y) #0 {
entry:
  %mul = mul nsw i32 %x, %y
  ret i32 %mul
}

define i32 @mul(i32 %x, i32 %y) #1 {
entry:
  %mul = mul nsw i32 %x, %y
  ret i32 %mul
}

define i32 @main() #1 {
entry:
  %call = call i32 @add(i32 1, i32 1)
  %call1 = call i32 @mul(i32 %call, i32 2)
  %call2 = call i32 (i8*, ...)* @printf(i8* getelementptr inbounds ([3 x i8]* @.str, i32 0, i32 0), i32 %call1)
  ret i32 0
}

declare i32 @printf(i8*, ...) #2

attributes #0 = { alwaysinline nounwind uwtable "less-precise-fpmad"="false" "no-frame-pointer-elim"="false" "no-frame-pointer-elim-non-leaf"="true" "no-infs-fp-math"="false" "no-nans-fp-math"="false" "unsafe-fp-math"="false" "use-soft-float"="false" }
attributes #1 = { nounwind uwtable "less-precise-fpmad"="false" "no-frame-pointer-elim"="false" "no-frame-pointer-elim-non-leaf"="true" "no-infs-fp-math"="false" "no-nans-fp-math"="false" "unsafe-fp-math"="false" "use-soft-float"="false" }
attributes #2 = { "less-precise-fpmad"="false" "no-frame-pointer-elim"="false" "no-frame-pointer-elim-non-leaf"="true" "no-infs-fp-math"="false" "no-nans-fp-math"="false" "unsafe-fp-math"="false" "use-soft-float"="false" }
