#include <cpu/exec.h>
#include "../local-include/decode.h"
#include "../local-include/rtl.h"
#include "../local-include/intr.h"
#include <cpu/difftest.h>
#include <cpu/cpu-exec.h>

#define goto_EHelper(addr) goto *(addr)
#undef decode_empty
static inline def_DHelper(empty) { }

static inline void difftest_skip_delay_slot() {
#ifndef __DIFF_REF_NEMU__
  difftest_skip_dut(2, 1);
#endif
}

static inline void mux(DecodeExecState *s, rtlreg_t* dest, const rtlreg_t* cond,
    const rtlreg_t* src1, const rtlreg_t* src2) {
  // dest <- (cond ? src1 : src2)
  rtl_setrelopi(s, RELOP_EQ, s0, cond, 0);
  rtl_subi(s, s0, s0, 1);
  // s0 = mask
  rtl_and(s, s1, src1, s0);
  rtl_not(s, s0, s0);
  rtl_and(s, dest, src2, s0);
  rtl_or(s, dest, dest, s1);
}


#if 0
#undef CASE_ENTRY
#define CASE_ENTRY(idx, id, ex, w) case idx: id(s); ex(s); break;

static inline def_EHelper(special) {
  switch (s->isa.instr.r.func) {
#ifndef __ICS_EXPORT
    IDEX (000, shift, slli)                       IDEX (002, shift, srli)IDEX (003, shift, srai)
    IDEX (004, R, sll)                            IDEX (006, R, srl)     IDEX (007, R, sra)
    IDEX (010, R, jr)      IDEX (011, R, jalr)    IDEX (012, cmov, movz) IDEX (013, cmov, movn)
    EX   (014, syscall)
    IDEX (020, R, mfhi)    IDEX (021, R, mthi)    IDEX (022, R, mflo)    IDEX (023, R, mtlo)

    IDEX (030, R, mult)    IDEX (031, R, multu)   IDEX (032, R, div)     IDEX (033, R, divu)

                           IDEX (041, R, add)                            IDEX (043, R, sub)
    IDEX (044, R, and)     IDEX (045, R, or)      IDEX (046, R, xor)     IDEX (047, R, nor)
                                                  IDEX (052, R, slt)     IDEX (053, R, sltu)
#endif
    default: exec_inv(s);
  }
}

#ifndef __ICS_EXPORT
static inline def_EHelper(special2) {
  switch (s->isa.instr.r.func) {
    IDEX (2, R, mul)
    default: exec_inv(s);
  }
}

static inline def_EHelper(regimm) {
  switch (s->isa.instr.r.rt) {
    IDEX (0, B, bltz)
    IDEX (1, B, bgez)
    default: exec_inv(s);
  }
}

static inline def_EHelper(cop0) {
#define pair(x, y) (((x) << 1) | (y))
  bool cop0co = (s->isa.instr.r.rs & 0x10) != 0;
  uint32_t op = pair((cop0co ? s->isa.instr.r.func : s->isa.instr.r.rs), cop0co);
  switch (op) {
    EX   (pair(002, 1), tlbwi)
    EX   (pair(006, 1), tlbwr)
    EX   (pair(010, 1), tlbp)
    EX   (pair(030, 1), eret)
    IDEX (pair(000, 0), R, mfc0)
    IDEX (pair(004, 0), R, mtc0)
    default: exec_inv(s);
  }
#undef pair
}
#endif

static inline void fetch_decode_exec(DecodeExecState *s) {
  s->isa.instr.val = instr_fetch(&s->snpc, 4);
  s->npc = s->snpc;
#ifndef __ICS_EXPORT
  return_on_mem_ex();
#endif
  switch (s->isa.instr.r.opcode) {
#ifdef __ICS_EXPORT
    EX   (000, special)
    IDEX (017, IU, lui)
    IDEX (043, ld, lw)
    IDEX (053, st, sw)
    EX   (074, nemu_trap)
#else
    EX   (000, special)    EX   (001, regimm)     IDEX (002, J, j)       IDEX (003, J, jal)
    IDEX (004, B, beq)     IDEX (005, B, bne)     IDEX (006, B, blez)    IDEX (007, B, bgtz)
                           IDEX (011, I, addi)    IDEX (012, I, slti)    IDEX (013, I, sltui)
    IDEX (014, IU, andi)   IDEX (015, IU, ori)    IDEX (016, IU, xori)   IDEX (017, IU, lui)
    EX   (020, cop0)


    EX   (034, special2)
    IDEX (040, ld, lb)     IDEX (041, ld, lh)     IDEX (042, st, lwl)    IDEX (043, ld, lw)
    IDEX (044, ld, lbu)    IDEX (045, ld, lhu)    IDEX (046, st, lwr)
    IDEX (050, st, sb)     IDEX (051, st, sh)     IDEX (052, st, swl)    IDEX (053, st, sw)
                                                  IDEX (056, st, swr)



    EX   (074, nemu_trap)
#endif
    default: exec_inv(s);
  }
}
#endif

static inline void reset_zero() {
  reg_l(0) = 0;
}

void isa_execute(uint64_t n) {
  for (; n > 0; n --) {
    DecodeExecState state;
    DecodeExecState *s = &state;
    vaddr_t pc = cpu.pc;
    s->snpc = pc;

#include "all-instr.h"

//    fetch_decode_exec(&s);
exec_finish:
    update_pc(s);

    reset_zero();

    cpu_exec_2nd_part(pc, s->snpc, s->npc);
  }
}
