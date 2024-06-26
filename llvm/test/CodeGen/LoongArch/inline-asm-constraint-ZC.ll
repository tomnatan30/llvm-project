; NOTE: Assertions have been autogenerated by utils/update_llc_test_checks.py
; RUN: llc --mtriple=loongarch32 -mattr=+d --verify-machineinstrs < %s | FileCheck %s --check-prefix=LA32
; RUN: llc --mtriple=loongarch64 -mattr=+d --verify-machineinstrs < %s | FileCheck %s --check-prefix=LA64

define i32 @ZC_offset_neg_32769(ptr %p) nounwind {
; LA32-LABEL: ZC_offset_neg_32769:
; LA32:       # %bb.0:
; LA32-NEXT:    lu12i.w $a1, -9
; LA32-NEXT:    ori $a1, $a1, 4095
; LA32-NEXT:    add.w $a0, $a0, $a1
; LA32-NEXT:    #APP
; LA32-NEXT:    ll.w $a0, $a0, 0
; LA32-NEXT:    #NO_APP
; LA32-NEXT:    ret
;
; LA64-LABEL: ZC_offset_neg_32769:
; LA64:       # %bb.0:
; LA64-NEXT:    lu12i.w $a1, -9
; LA64-NEXT:    ori $a1, $a1, 4095
; LA64-NEXT:    add.d $a0, $a0, $a1
; LA64-NEXT:    #APP
; LA64-NEXT:    ll.w $a0, $a0, 0
; LA64-NEXT:    #NO_APP
; LA64-NEXT:    ret
  %1 = getelementptr inbounds i8, ptr %p, i32 -32769
  %2 = call i32 asm "ll.w $0, $1", "=r,*^ZC"(ptr elementtype(i32) %1)
  ret i32 %2
}

define i32 @ZC_offset_neg_32768(ptr %p) nounwind {
; LA32-LABEL: ZC_offset_neg_32768:
; LA32:       # %bb.0:
; LA32-NEXT:    #APP
; LA32-NEXT:    ll.w $a0, $a0, -32768
; LA32-NEXT:    #NO_APP
; LA32-NEXT:    ret
;
; LA64-LABEL: ZC_offset_neg_32768:
; LA64:       # %bb.0:
; LA64-NEXT:    #APP
; LA64-NEXT:    ll.w $a0, $a0, -32768
; LA64-NEXT:    #NO_APP
; LA64-NEXT:    ret
  %1 = getelementptr inbounds i8, ptr %p, i32 -32768
  %2 = call i32 asm "ll.w $0, $1", "=r,*^ZC"(ptr elementtype(i32) %1)
  ret i32 %2
}

define i32 @ZC_offset_neg_4(ptr %p) nounwind {
; LA32-LABEL: ZC_offset_neg_4:
; LA32:       # %bb.0:
; LA32-NEXT:    #APP
; LA32-NEXT:    ll.w $a0, $a0, -4
; LA32-NEXT:    #NO_APP
; LA32-NEXT:    ret
;
; LA64-LABEL: ZC_offset_neg_4:
; LA64:       # %bb.0:
; LA64-NEXT:    #APP
; LA64-NEXT:    ll.w $a0, $a0, -4
; LA64-NEXT:    #NO_APP
; LA64-NEXT:    ret
  %1 = getelementptr inbounds i8, ptr %p, i32 -4
  %2 = call i32 asm "ll.w $0, $1", "=r,*^ZC"(ptr elementtype(i32) %1)
  ret i32 %2
}

define i32 @ZC_offset_neg_1(ptr %p) nounwind {
; LA32-LABEL: ZC_offset_neg_1:
; LA32:       # %bb.0:
; LA32-NEXT:    addi.w $a0, $a0, -1
; LA32-NEXT:    #APP
; LA32-NEXT:    ll.w $a0, $a0, 0
; LA32-NEXT:    #NO_APP
; LA32-NEXT:    ret
;
; LA64-LABEL: ZC_offset_neg_1:
; LA64:       # %bb.0:
; LA64-NEXT:    addi.d $a0, $a0, -1
; LA64-NEXT:    #APP
; LA64-NEXT:    ll.w $a0, $a0, 0
; LA64-NEXT:    #NO_APP
; LA64-NEXT:    ret
  %1 = getelementptr inbounds i8, ptr %p, i32 -1
  %2 = call i32 asm "ll.w $0, $1", "=r,*^ZC"(ptr elementtype(i32) %1)
  ret i32 %2
}

define i32 @ZC_offset_0(ptr %p) nounwind {
; LA32-LABEL: ZC_offset_0:
; LA32:       # %bb.0:
; LA32-NEXT:    #APP
; LA32-NEXT:    ll.w $a0, $a0, 0
; LA32-NEXT:    #NO_APP
; LA32-NEXT:    ret
;
; LA64-LABEL: ZC_offset_0:
; LA64:       # %bb.0:
; LA64-NEXT:    #APP
; LA64-NEXT:    ll.w $a0, $a0, 0
; LA64-NEXT:    #NO_APP
; LA64-NEXT:    ret
  %1 = call i32 asm "ll.w $0, $1", "=r,*^ZC"(ptr elementtype(i32) %p)
  ret i32 %1
}

define i32 @ZC_offset_1(ptr %p) nounwind {
; LA32-LABEL: ZC_offset_1:
; LA32:       # %bb.0:
; LA32-NEXT:    addi.w $a0, $a0, 1
; LA32-NEXT:    #APP
; LA32-NEXT:    ll.w $a0, $a0, 0
; LA32-NEXT:    #NO_APP
; LA32-NEXT:    ret
;
; LA64-LABEL: ZC_offset_1:
; LA64:       # %bb.0:
; LA64-NEXT:    addi.d $a0, $a0, 1
; LA64-NEXT:    #APP
; LA64-NEXT:    ll.w $a0, $a0, 0
; LA64-NEXT:    #NO_APP
; LA64-NEXT:    ret
  %1 = getelementptr inbounds i8, ptr %p, i32 1
  %2 = call i32 asm "ll.w $0, $1", "=r,*^ZC"(ptr elementtype(i32) %1)
  ret i32 %2
}

define i32 @ZC_offset_32764(ptr %p) nounwind {
; LA32-LABEL: ZC_offset_32764:
; LA32:       # %bb.0:
; LA32-NEXT:    #APP
; LA32-NEXT:    ll.w $a0, $a0, 32764
; LA32-NEXT:    #NO_APP
; LA32-NEXT:    ret
;
; LA64-LABEL: ZC_offset_32764:
; LA64:       # %bb.0:
; LA64-NEXT:    #APP
; LA64-NEXT:    ll.w $a0, $a0, 32764
; LA64-NEXT:    #NO_APP
; LA64-NEXT:    ret
  %1 = getelementptr inbounds i8, ptr %p, i32 32764
  %2 = call i32 asm "ll.w $0, $1", "=r,*^ZC"(ptr elementtype(i32) %1)
  ret i32 %2
}

define i32 @ZC_offset_32767(ptr %p) nounwind {
; LA32-LABEL: ZC_offset_32767:
; LA32:       # %bb.0:
; LA32-NEXT:    lu12i.w $a1, 7
; LA32-NEXT:    ori $a1, $a1, 4095
; LA32-NEXT:    add.w $a0, $a0, $a1
; LA32-NEXT:    #APP
; LA32-NEXT:    ll.w $a0, $a0, 0
; LA32-NEXT:    #NO_APP
; LA32-NEXT:    ret
;
; LA64-LABEL: ZC_offset_32767:
; LA64:       # %bb.0:
; LA64-NEXT:    lu12i.w $a1, 7
; LA64-NEXT:    ori $a1, $a1, 4095
; LA64-NEXT:    add.d $a0, $a0, $a1
; LA64-NEXT:    #APP
; LA64-NEXT:    ll.w $a0, $a0, 0
; LA64-NEXT:    #NO_APP
; LA64-NEXT:    ret
  %1 = getelementptr inbounds i8, ptr %p, i32 32767
  %2 = call i32 asm "ll.w $0, $1", "=r,*^ZC"(ptr elementtype(i32) %1)
  ret i32 %2
}
