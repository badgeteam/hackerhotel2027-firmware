// A small RV32IMC interpreter: runs an app's code (apps/hhapp.h) in the
// emulator, so the emulator runs the same .hhapp file as the badge.
//
// It knows what an app built by apps/Makefile can contain: the base
// integer instructions, M (multiply, divide) and C (the 16-bit forms). An
// atomic or CSR instruction is reported as a fault. Memory is the
// emulated RAM and ROM (emu.h), which sit at their own addresses in this
// process, so a guest address is used as a host pointer after a range check.
#include <stdio.h>
#include <string.h>

#include "emu.h"

typedef struct {
    rv_cpu* cpu;
    char* err;
    int err_len;
    bool fault;
} run_t;

static bool fail(run_t* r, const char* what, uint32_t addr) {
    if (!r->fault) snprintf(r->err, r->err_len, "%s at 0x%08x (pc 0x%08x)", what, addr, r->cpu->pc);
    r->fault = true;
    return false;
}

static bool readable(uint32_t a, unsigned n) {
    return (a >= EMU_RAM_BASE && a + n <= EMU_RAM_BASE + EMU_RAM_SIZE) ||
           (a >= EMU_ROM_BASE && a + n <= EMU_ROM_BASE + EMU_ROM_SIZE);
}

static bool writable(uint32_t a, unsigned n) { return a >= EMU_RAM_BASE && a + n <= EMU_RAM_BASE + EMU_RAM_SIZE; }

static uint32_t load(run_t* r, uint32_t a, unsigned n, bool sign) {
    if (!readable(a, n)) return fail(r, "read outside RAM", a), 0;
    const uint8_t* p = (const uint8_t*)(uintptr_t)a;
    uint32_t v = 0;
    for (unsigned i = 0; i < n; i++) v |= (uint32_t)p[i] << (8 * i);
    if (sign && n < 4 && (v >> (8 * n - 1)) & 1) v |= ~0u << (8 * n);
    return v;
}

static void store(run_t* r, uint32_t a, unsigned n, uint32_t v) {
    if (!writable(a, n)) {
        fail(r, "write outside RAM", a);
        return;
    }
    uint8_t* p = (uint8_t*)(uintptr_t)a;
    for (unsigned i = 0; i < n; i++) p[i] = (uint8_t)(v >> (8 * i));
}

static int32_t sext(uint32_t v, int bits) { return (int32_t)(v << (32 - bits)) >> (32 - bits); }

#define BITS(v, hi, lo) (((v) >> (lo)) & ((1u << ((hi) - (lo) + 1)) - 1))

static uint32_t mulh(int64_t a, int64_t b) { return (uint32_t)((a * b) >> 32); }

// One 32-bit instruction. Returns the next pc.
static uint32_t exec32(run_t* r, uint32_t pc, uint32_t in) {
    uint32_t* x = r->cpu->x;
    unsigned rd = BITS(in, 11, 7), rs1 = BITS(in, 19, 15), rs2 = BITS(in, 24, 20), f3 = BITS(in, 14, 12),
             f7 = BITS(in, 31, 25);
    uint32_t a = x[rs1], b = x[rs2];
    int32_t imm_i = sext(in >> 20, 12);
    int32_t imm_s = sext((BITS(in, 31, 25) << 5) | BITS(in, 11, 7), 12);
    int32_t imm_b = sext((BITS(in, 31, 31) << 12) | (BITS(in, 7, 7) << 11) | (BITS(in, 30, 25) << 5) | (BITS(in, 11, 8) << 1), 13);
    int32_t imm_j = sext((BITS(in, 31, 31) << 20) | (BITS(in, 19, 12) << 12) | (BITS(in, 20, 20) << 11) | (BITS(in, 30, 21) << 1), 21);
    uint32_t v = 0, next = pc + 4;
    bool write = true;

    switch (in & 0x7F) {
        case 0x37: v = in & 0xFFFFF000u; break;       // LUI
        case 0x17: v = pc + (in & 0xFFFFF000u); break;  // AUIPC
        case 0x6F: v = next; next = pc + imm_j; break;   // JAL
        case 0x67: v = next; next = (a + imm_i) & ~1u; break;  // JALR
        case 0x63: {  // branches
            bool t;
            switch (f3) {
                case 0: t = a == b; break;
                case 1: t = a != b; break;
                case 4: t = (int32_t)a < (int32_t)b; break;
                case 5: t = (int32_t)a >= (int32_t)b; break;
                case 6: t = a < b; break;
                case 7: t = a >= b; break;
                default: return fail(r, "bad branch", pc), pc;
            }
            if (t) next = pc + imm_b;
            write = false;
            break;
        }
        case 0x03: {  // loads
            static const unsigned SIZE[8] = {1, 2, 4, 0, 1, 2, 0, 0};
            if (!SIZE[f3]) return fail(r, "bad load", pc), pc;
            v = load(r, a + imm_i, SIZE[f3], f3 < 4);
            break;
        }
        case 0x23:  // stores
            if (f3 > 2) return fail(r, "bad store", pc), pc;
            store(r, a + imm_s, 1u << f3, b);
            write = false;
            break;
        case 0x13: {  // immediate arithmetic
            unsigned sh = rs2;
            switch (f3) {
                case 0: v = a + imm_i; break;
                case 2: v = (int32_t)a < imm_i; break;
                case 3: v = a < (uint32_t)imm_i; break;
                case 4: v = a ^ imm_i; break;
                case 6: v = a | imm_i; break;
                case 7: v = a & imm_i; break;
                case 1: v = a << sh; break;
                case 5: v = f7 & 0x20 ? (uint32_t)((int32_t)a >> sh) : a >> sh; break;
            }
            break;
        }
        case 0x33:  // register arithmetic
            if (f7 == 1) {  // M
                switch (f3) {
                    case 0: v = a * b; break;
                    case 1: v = mulh((int32_t)a, (int32_t)b); break;
                    case 2: v = mulh((int32_t)a, (int64_t)(uint64_t)b); break;
                    case 3: v = (uint32_t)(((uint64_t)a * b) >> 32); break;
                    case 4: v = b == 0 ? ~0u : ((int32_t)a == INT32_MIN && (int32_t)b == -1) ? a : (uint32_t)((int32_t)a / (int32_t)b); break;
                    case 5: v = b == 0 ? ~0u : a / b; break;
                    case 6: v = b == 0 ? a : ((int32_t)a == INT32_MIN && (int32_t)b == -1) ? 0 : (uint32_t)((int32_t)a % (int32_t)b); break;
                    case 7: v = b == 0 ? a : a % b; break;
                }
            } else {
                switch (f3) {
                    case 0: v = f7 & 0x20 ? a - b : a + b; break;
                    case 1: v = a << (b & 31); break;
                    case 2: v = (int32_t)a < (int32_t)b; break;
                    case 3: v = a < b; break;
                    case 4: v = a ^ b; break;
                    case 5: v = f7 & 0x20 ? (uint32_t)((int32_t)a >> (b & 31)) : a >> (b & 31); break;
                    case 6: v = a | b; break;
                    case 7: v = a & b; break;
                }
            }
            break;
        case 0x0F: write = false; break;  // FENCE, FENCE.I
        default: return fail(r, "unsupported instruction", pc), pc;
    }
    if (write && rd) x[rd] = v;
    return next;
}

// One 16-bit instruction, as its 32-bit equivalent where that is simple
static uint32_t exec16(run_t* r, uint32_t pc, uint16_t c) {
    uint32_t* x = r->cpu->x;
    unsigned op = c & 3, f3 = c >> 13;
    unsigned rd = BITS(c, 11, 7), rs2 = BITS(c, 6, 2);
    unsigned rdp = 8 + BITS(c, 4, 2), rs1p = 8 + BITS(c, 9, 7);  // x8..x15
    uint32_t next = pc + 2;

    if (op == 0) {
        uint32_t uimm = (BITS(c, 6, 6) << 2) | (BITS(c, 12, 10) << 3) | (BITS(c, 5, 5) << 6);  // C.LW / C.SW
        switch (f3) {
            case 0: {  // C.ADDI4SPN
                uint32_t nz = (BITS(c, 6, 6) << 2) | (BITS(c, 5, 5) << 3) | (BITS(c, 12, 11) << 4) | (BITS(c, 10, 7) << 6);
                if (!nz) return fail(r, "illegal instruction", pc), pc;
                x[rdp] = x[2] + nz;
                break;
            }
            case 2: x[rdp] = load(r, x[rs1p] + uimm, 4, false); break;  // C.LW
            case 6: store(r, x[rs1p] + uimm, 4, x[rdp]); break;          // C.SW
            default: return fail(r, "unsupported instruction", pc), pc;
        }
    } else if (op == 1) {
        int32_t imm6 = sext((BITS(c, 12, 12) << 5) | BITS(c, 6, 2), 6);
        int32_t jimm = sext((BITS(c, 12, 12) << 11) | (BITS(c, 8, 8) << 10) | (BITS(c, 10, 9) << 8) | (BITS(c, 6, 6) << 7) |
                                (BITS(c, 7, 7) << 6) | (BITS(c, 2, 2) << 5) | (BITS(c, 11, 11) << 4) | (BITS(c, 5, 3) << 1),
                            12);
        int32_t bimm = sext((BITS(c, 12, 12) << 8) | (BITS(c, 6, 5) << 6) | (BITS(c, 2, 2) << 5) | (BITS(c, 11, 10) << 3) |
                                (BITS(c, 4, 3) << 1),
                            9);
        switch (f3) {
            case 0: if (rd) x[rd] += imm6; break;  // C.ADDI (C.NOP)
            case 1: x[1] = next; next = pc + jimm; break;  // C.JAL
            case 2: if (rd) x[rd] = imm6; break;           // C.LI
            case 3:
                if (rd == 2) {  // C.ADDI16SP
                    x[2] += sext((BITS(c, 12, 12) << 9) | (BITS(c, 4, 3) << 7) | (BITS(c, 5, 5) << 6) | (BITS(c, 2, 2) << 5) |
                                     (BITS(c, 6, 6) << 4),
                                 10);
                } else if (rd) {  // C.LUI
                    x[rd] = (uint32_t)imm6 << 12;
                }
                break;
            case 4: {
                unsigned sh = BITS(c, 6, 2);
                switch (BITS(c, 11, 10)) {
                    case 0: x[rs1p] >>= sh; break;                                     // C.SRLI
                    case 1: x[rs1p] = (uint32_t)((int32_t)x[rs1p] >> sh); break;       // C.SRAI
                    case 2: x[rs1p] &= imm6; break;                                    // C.ANDI
                    case 3: {
                        uint32_t b = x[8 + BITS(c, 4, 2)];
                        if (BITS(c, 12, 12)) return fail(r, "unsupported instruction", pc), pc;
                        switch (BITS(c, 6, 5)) {
                            case 0: x[rs1p] -= b; break;  // C.SUB
                            case 1: x[rs1p] ^= b; break;  // C.XOR
                            case 2: x[rs1p] |= b; break;  // C.OR
                            case 3: x[rs1p] &= b; break;  // C.AND
                        }
                        break;
                    }
                }
                break;
            }
            case 5: next = pc + jimm; break;                           // C.J
            case 6: if (!x[rs1p]) next = pc + bimm; break;             // C.BEQZ
            case 7: if (x[rs1p]) next = pc + bimm; break;              // C.BNEZ
        }
    } else {  // op == 2
        switch (f3) {
            case 0: if (rd) x[rd] <<= BITS(c, 6, 2); break;  // C.SLLI
            case 2: {                                         // C.LWSP
                uint32_t off = (BITS(c, 6, 4) << 2) | (BITS(c, 12, 12) << 5) | (BITS(c, 3, 2) << 6);
                uint32_t v = load(r, x[2] + off, 4, false);
                if (rd) x[rd] = v;
                break;
            }
            case 4:
                if (!BITS(c, 12, 12)) {
                    if (!rs2) next = x[rd] & ~1u;  // C.JR
                    else if (rd) x[rd] = x[rs2];   // C.MV
                } else if (!rs2) {
                    if (!rd) return fail(r, "ebreak", pc), pc;  // C.EBREAK
                    uint32_t t = x[rd] & ~1u;                    // C.JALR
                    x[1] = next;
                    next = t;
                } else if (rd) {
                    x[rd] += x[rs2];  // C.ADD
                }
                break;
            case 6: {  // C.SWSP
                uint32_t off = (BITS(c, 12, 9) << 2) | (BITS(c, 8, 7) << 6);
                store(r, x[2] + off, 4, x[rs2]);
                break;
            }
            default: return fail(r, "unsupported instruction", pc), pc;
        }
    }
    x[0] = 0;
    return next;
}

bool rv_call(rv_cpu* cpu, uint32_t fn, const uint32_t* args, int nargs, uint64_t max_steps,
             void (*trap)(rv_cpu* cpu, unsigned n), char* err, int err_len) {
    run_t r = {cpu, err, err_len, false};
    for (int i = 0; i < nargs && i < 8; i++) cpu->x[10 + i] = args[i];
    cpu->x[1] = EMU_RETURN;
    cpu->x[2] = EMU_STACK_TOP;
    cpu->pc = fn;
    for (uint64_t steps = 0; steps < max_steps; steps++) {
        uint32_t pc = cpu->pc;
        if (pc == EMU_RETURN) return true;
        if (pc >= EMU_TRAP_BASE && pc < EMU_RETURN) {  // a call into the firmware's API
            trap(cpu, (pc - EMU_TRAP_BASE) / 4);
            cpu->pc = cpu->x[1];
            continue;
        }
        if (pc & 1 || pc < 0x20002000u || pc + 2 > EMU_RAM_BASE + EMU_RAM_SIZE) return fail(&r, "jump outside the app", pc);
        uint16_t lo = *(const uint16_t*)(uintptr_t)pc;
        if ((lo & 3) == 3) {
            if (pc + 4 > EMU_RAM_BASE + EMU_RAM_SIZE) return fail(&r, "jump outside the app", pc);
            uint32_t in = lo | (uint32_t)*(const uint16_t*)(uintptr_t)(pc + 2) << 16;
            cpu->pc = exec32(&r, pc, in);
        } else {
            cpu->pc = exec16(&r, pc, lo);
        }
        cpu->x[0] = 0;
        if (r.fault) return false;
    }
    snprintf(err, err_len, "no return after %llu instructions (pc 0x%08x): the app hangs", (unsigned long long)max_steps, cpu->pc);
    return false;
}
