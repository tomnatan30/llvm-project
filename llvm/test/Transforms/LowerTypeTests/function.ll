; RUN: opt -S -passes=lowertypetests -mtriple=i686-unknown-linux-gnu %s | FileCheck --check-prefixes=X86,X86-LINUX,NATIVE,JT8 %s
; RUN: opt -S -passes=lowertypetests -mtriple=x86_64-unknown-linux-gnu %s | FileCheck --check-prefixes=X86,X86-LINUX,NATIVE,JT8 %s
; RUN: opt -S -passes=lowertypetests -mtriple=i686-pc-win32 %s | FileCheck --check-prefixes=X86,X86-WIN32,NATIVE,JT8 %s
; RUN: opt -S -passes=lowertypetests -mtriple=x86_64-pc-win32 %s | FileCheck --check-prefixes=X86,X86-WIN32,NATIVE,JT8 %s
; RUN: opt -S -passes=lowertypetests -mtriple=riscv32-unknown-linux-gnu %s | FileCheck --check-prefixes=RISCV,NATIVE,JT8 %s
; RUN: opt -S -passes=lowertypetests -mtriple=riscv64-unknown-linux-gnu %s | FileCheck --check-prefixes=RISCV,NATIVE,JT8 %s
; RUN: opt -S -passes=lowertypetests -mtriple=wasm32-unknown-unknown %s | FileCheck --check-prefix=WASM32 %s
; RUN: opt -S -passes=lowertypetests -mtriple=loongarch64-unknown-linux-gnu %s | FileCheck --check-prefixes=LOONGARCH64,NATIVE,JT8 %s

; The right format for Arm jump tables depends on the selected
; subtarget, so we can't get these tests right without the Arm target
; compiled in.
; RUN: %if arm-registered-target %{ opt -S -passes=lowertypetests -mtriple=arm-unknown-linux-gnu %s | FileCheck --check-prefixes=ARM,NATIVE,JT4 %s %}
; RUN: %if arm-registered-target %{ opt -S -passes=lowertypetests -mtriple=thumbv7m-unknown-linux-gnu %s | FileCheck --check-prefixes=THUMB,NATIVE,JT4 %s %}
; RUN: %if arm-registered-target %{ opt -S -passes=lowertypetests -mtriple=thumbv8m.base-unknown-linux-gnu %s | FileCheck --check-prefixes=THUMB,NATIVE,JT4 %s %}
; RUN: %if arm-registered-target %{ opt -S -passes=lowertypetests -mtriple=thumbv6m-unknown-linux-gnu %s | FileCheck --check-prefixes=THUMBV6M,NATIVE,JT16 %s %}
; RUN: %if arm-registered-target %{ opt -S -passes=lowertypetests -mtriple=thumbv5-unknown-linux-gnu %s | FileCheck --check-prefixes=ARM,NATIVE,JT4 %s %}
; RUN: %if arm-registered-target %{ opt -S -passes=lowertypetests -mtriple=aarch64-unknown-linux-gnu %s | FileCheck --check-prefixes=ARM,NATIVE,JT4 %s %}

; Tests that we correctly handle bitsets containing 2 or more functions.

target datalayout = "e-p:64:64"


; NATIVE: @0 = private unnamed_addr constant [2 x ptr] [ptr @f, ptr @g], align 16
@0 = private unnamed_addr constant [2 x ptr] [ptr @f, ptr @g], align 16

; NATIVE: private constant [0 x i8] zeroinitializer
; WASM32: private constant [0 x i8] zeroinitializer

; JT4: @f = alias [4 x i8], ptr @[[JT:.*]]
; JT8: @f = alias [8 x i8], ptr @[[JT:.*]]
; JT16: @f = alias [16 x i8], ptr @[[JT:.*]]

; JT4: @g = internal alias [4 x i8], getelementptr inbounds ([2 x [4 x i8]], ptr @[[JT]], i64 0, i64 1)
; JT8: @g = internal alias [8 x i8], getelementptr inbounds ([2 x [8 x i8]], ptr @[[JT]], i64 0, i64 1)
; JT16: @g = internal alias [16 x i8], getelementptr inbounds ([2 x [16 x i8]], ptr @[[JT]], i64 0, i64 1)

; NATIVE: define hidden void @f.cfi()
; WASM32: define void @f() !type !{{[0-9]+}} !wasm.index ![[I0:[0-9]+]]
define void @f() !type !0 {
  ret void
}

; NATIVE: define internal void @g.cfi()
; WASM32: define internal void @g() !type !{{[0-9]+}} !wasm.index ![[I1:[0-9]+]]
define internal void @g() !type !0 {
  ret void
}

!0 = !{i32 0, !"typeid1"}

declare i1 @llvm.type.test(ptr %ptr, metadata %bitset) noinline readnone

define i1 @foo(ptr %p) {
  ; JT4: sub i64 ptrtoint (ptr getelementptr (i8, ptr @[[JT]], i64 4) to i64), {{.*}}
  ; JT8: sub i64 ptrtoint (ptr getelementptr (i8, ptr @[[JT]], i64 8) to i64), {{.*}}
  ; JT16: sub i64 ptrtoint (ptr getelementptr (i8, ptr @[[JT]], i64 16) to i64), {{.*}}
  ; WASM32: sub i64 ptrtoint (ptr getelementptr (i8, ptr null, i64 2) to i64), {{.*}}
  ; WASM32: icmp ule i64 {{.*}}, 1
  %x = call i1 @llvm.type.test(ptr %p, metadata !"typeid1")
  ret i1 %x
}

; JT4:  define private void @[[JT]]() #[[ATTR:.*]] align 4 {
; JT8:  define private void @[[JT]]() #[[ATTR:.*]] align 8 {
; JT16: define private void @[[JT]]() #[[ATTR:.*]] align 16 {

; X86:      jmp ${0:c}@plt
; X86-SAME: int3
; X86-SAME: int3
; X86-SAME: int3

; ARM:      b $0

; THUMB:      b.w $0

; THUMBV6M:      push {r0,r1}
; THUMBV6M-SAME: ldr r0, 1f
; THUMBV6M-SAME: 0: add r0, r0, pc
; THUMBV6M-SAME: str r0, [sp, #4]
; THUMBV6M-SAME: pop {r0,pc}
; THUMBV6M-SAME: .balign 4
; THUMBV6M-SAME: 1: .word $0 - (0b + 4)

; RISCV:      tail $0@plt

; LOONGARCH64:      pcalau12i $$t0, %pc_hi20($0)
; LOONGARCH64-SAME: jirl $$r0, $$t0, %pc_lo12($0)

; NATIVE-SAME: "s"(ptr @f.cfi)

; X86-NEXT: jmp ${0:c}@plt
; X86-SAME: int3
; X86-SAME: int3
; X86-SAME: int3

; ARM-NEXT: b $0

; THUMB-NEXT: b.w $0

; THUMBV6M-NEXT: push {r0,r1}
; THUMBV6M-SAME: ldr r0, 1f
; THUMBV6M-SAME: 0: add r0, r0, pc
; THUMBV6M-SAME: str r0, [sp, #4]
; THUMBV6M-SAME: pop {r0,pc}
; THUMBV6M-SAME: .balign 4
; THUMBV6M-SAME: 1: .word $0 - (0b + 4)

; RISCV-NEXT: tail $0@plt

; LOONGARCH64-NEXT: pcalau12i $$t0, %pc_hi20($0)
; LOONGARCH64-SAME: jirl $$r0, $$t0, %pc_lo12($0)

; NATIVE-SAME: "s"(ptr @g.cfi)

; X86-LINUX: attributes #[[ATTR]] = { naked nocf_check noinline }
; X86-WIN32: attributes #[[ATTR]] = { nocf_check noinline }
; ARM: attributes #[[ATTR]] = { naked noinline
; THUMB: attributes #[[ATTR]] = { naked noinline "target-cpu"="cortex-a8" "target-features"="+thumb-mode" }
; THUMBV6M: attributes #[[ATTR]] = { naked noinline "target-features"="+thumb-mode" }
; RISCV: attributes #[[ATTR]] = { naked noinline "target-features"="-c,-relax" }
; LOONGARCH64: attributes #[[ATTR]] = { naked noinline }

; WASM32: ![[I0]] = !{i64 1}
; WASM32: ![[I1]] = !{i64 2}
