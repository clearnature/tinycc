/* tri-gen.c —— TriRV（三进制 RISC-V 风格）代码生成器。
 *
 * 结构对齐 riscv64-gen.c：两遍 include（tcc.h pass-1 定义宏 / libtcc.c pass-2 实现）。
 *
 * spike 范围（T-0.3/T-0.4，诚实门控——宁可报错不可错编）：
 *   - 标量 int/char/short/long/指针 的 load/store/算术/比较/调用（≤8 个寄存器实参）
 *   - 帧偏移与访存偏移 |off| ≤ TRI_IMM7_MAX=1093（大帧/大偏移 UNSUPPORTED）
 *   - 常量 |c| < 2^31（LUI+ADDI 两段）；>32-bit 常量 UNSUPPORTED（tccgen 常量本就 int64）
 *   - 浮点全部 UNSUPPORTED（F 扩展 T-1.6）；结构体按值传参/返回 UNSUPPORTED
 *   - 对齐简化：全 align=1（TriRV VM 无对齐约束，9-tryte 字自然布局）——真实 ABI
 *     的 9-对齐在 T-1 收尾时以专用 hook 重做
 *
 * 语义真相源：trirv 仓库 trirv_isa.py（96 条）→ tri-isa.h 自动生成，组装宏已
 * 与 isa.encode 96/96 逐位对齐验证。
 * 编码要点（三进制 3^lo 加权，非二进制移位）：
 *   字段权重 op=1 rd=27 funct=2187 rs1=19683 rs2=1594323 funct2=129140163
 *   I 型 imm7 在 t13..t19；S/B 型 imm 低 4 trit 在 t3..t6、高 3 trit 在 t17..t19；
 *   U/J 型 imm13 在 t7..t19。3^20 = 3486784401 < 2^32 → u32 直发 o()。
 * 寄存器：tcc 索引 0..7→x10..x17(a0-a7)，8..15 浮点（spike 不用），TREG_RA→x1，TREG_SP→x2。
 * 帧：9-tryte 字，loc 以 tryte 计；ra 存 s0-9，s0 存 s0-18=sp。
 * PC：4*i（槽 i 的 tryte 地址）；J/B 立即数 = 槽偏移。
 * 三进制语义红线：布尔取反一律 SLTU/SLTIU（XORI 1 是逐 trit mod-3 加，不是取反）。
 */
#ifdef TARGET_DEFS_ONLY

/* ---- 寄存器分配器视图（tccgen 用） ---- */
#define NB_REGS 19              /* a0-a7(0-7) + fa0-fa7(8-15,spike 不用) + 0 + ra + sp */
#define TREG_R(x) (x)
#define TREG_F(x) (x + 8)
#define RC_INT (1 << 0)
#define RC_FLOAT (1 << 1)
#define RC_R(x) (1 << (2 + (x)))
#define RC_F(x) (1 << (10 + (x)))
#define RC_IRET (RC_R(0))
#define RC_IRE2 (RC_R(1))
#define RC_FRET (RC_F(0))
#define REG_IRET (TREG_R(0))
#define REG_IRE2 (TREG_R(1))
#define REG_FRET (TREG_F(0))
#define PTR_SIZE 9              /* TriRV 指针 = 9 tryte（45 trit） */
#define LDOUBLE_SIZE 9
#define LDOUBLE_ALIGN 9
#define MAX_ALIGN 9
#define CHAR_IS_UNSIGNED        /* TriRV char = 0..242 无符号（与 RISC-V psABI 一致） */
#define PROMOTE_RET
#define TREG_RA 17
#define TREG_SP 18

#else /* !TARGET_DEFS_ONLY —— 第二遍：实现 */

#define USING_GLOBALS
#include "tcc.h"
#include <assert.h>
#include "tri-isa.h"

#define XLEN 9                  /* TriRV 字宽 = 9 tryte */

ST_DATA const int reg_classes[NB_REGS] = {
  RC_INT | RC_R(0), RC_INT | RC_R(1), RC_INT | RC_R(2), RC_INT | RC_R(3),
  RC_INT | RC_R(4), RC_INT | RC_R(5), RC_INT | RC_R(6), RC_INT | RC_R(7),
  RC_FLOAT | RC_F(0), RC_FLOAT | RC_F(1), RC_FLOAT | RC_F(2), RC_FLOAT | RC_F(3),
  RC_FLOAT | RC_F(4), RC_FLOAT | RC_F(5), RC_FLOAT | RC_F(6), RC_FLOAT | RC_F(7),
  0, 1 << TREG_RA, 1 << TREG_SP
};
ST_DATA int func_bound_add_epilog;
ST_DATA const char * const target_machine_defs =
    "__trirv\0"
    "__trirv_xlen 45\0"
    ;

/* ---- 寄存器映射 ---- */
static int ireg(int r)
{
    if (r == TREG_RA) return TRI_RA;
    if (r == TREG_SP) return TRI_SP;
    assert(r >= 0 && r < 8);
    return 10 + r;              /* tcc r0..r7 -> a0..a7 = x10..x17 */
}
static int is_ireg(int r)
{
    return (unsigned)r < 8 || r == TREG_RA || r == TREG_SP;
}
static int freg(int r) { tcc_error("TriRV spike: no float regs"); return 0; }
static int is_freg(int r) { return r >= 8 && r < 16; }

/* ---- 指令直发（20-trit 编码进 u32） ---- */
ST_FUNC void o(unsigned int c)
{
    int ind1 = ind + 4;
    if (nocode_wanted)
        return;
    if (ind1 > cur_text_section->data_allocated)
        section_realloc(cur_text_section, ind1);
    write32le(cur_text_section->data + ind, c);
    ind = ind1;
}

static void TRI_JAL(unsigned rd, int slot_off)
{
    o(TRI_ENC_U(TRI_OP_JAL, slot_off, rd));   /* 宏内做符号安全取模 */
}
static void TRI_ADDI(unsigned rd, unsigned rs1, int imm7)
{
    assert(-TRI_IMM7_MAX <= imm7 && imm7 <= TRI_IMM7_MAX);
    o(TRI_ENC_I(TRI_OP_OPIMM, imm7, rs1, 0 /* ADDI funct=0 */, rd));
}
static void TRI_NOP(void)   /* ADDI zero,zero,0 */
{
    o(TRI_ENC_I(TRI_OP_OPIMM, 0, 0, 0, 0));
}
static void TRI_JALR(unsigned rd, unsigned rs1)
{
    o(TRI_ENC_I(TRI_OP_JALR, 0, rs1, 0, rd));
}
/* 布尔取反（唯一正确方式；XORI 1 是逐 trit mod-3 加，禁用） */
static void TRI_SEQZ_NOT(unsigned rd) { o(TRI_ENC_I(TRI_OP_OPIMM, 1, rd, 5, rd)); } /* SLTIU rd,rd,1 */
static void TRI_SNEZ(unsigned rd)     { o(TRI_ENC_R(TRI_SLTU_OP, TRI_SLTU_FUNCT2, 0, rd, TRI_SLTU_FUNCT, rd)); } /* SLTU rd,zero,rd */

/* ---- 符号地址补丁表（类型在 tri-isa.h；LUI 绝对/ JAL 槽偏移） ---- */
/* 定义（tentative）：见 tri-isa.h 声明 */

ST_FUNC void tri_add_patch(int kind, int text_off, Sym *sym, int addend, unsigned rd)
{
    if (!(NTP & 15)) TRI_PATCHES = tcc_realloc(TRI_PATCHES, (NTP + 16) * sizeof *TRI_PATCHES);
    TRI_PATCHES[NTP].text_off = text_off;
    TRI_PATCHES[NTP].kind = kind;
    TRI_PATCHES[NTP].sym = sym;
    TRI_PATCHES[NTP].addend = addend;
    TRI_PATCHES[NTP].rd = rd;
    NTP++;
}

static int tri_const_fits(int64_t v);
/* 全局/静态/函数地址：双槽 LUI(hi13) + ADDI(lo7) 占位，writer 按
 * addr = hi*3^7 + lo（平衡分段，带借位）回填。VM 内存 1<<20 < 3^21，
 * hi ≤ 479 ✓。绝对 tryte 地址必须经此拆分——LUI 语义 = imm×3^7，
 * 单槽 LUI 直填地址是错的（f1 全局 int 踩坑）。 */
static void tri_lui_load(unsigned rr, Sym *sym, int addend)
{
    o(TRI_ENC_U(TRI_OP_LUI, 0, rr));
    o(TRI_ENC_I(TRI_OP_OPIMM, 0, rr, 0, rr));      /* ADDI rr,rr,0 占位 */
    tri_add_patch(TRI_R_LUI_ABS, ind - 8, sym, addend, rr);
}

/* ---- 跳转与补丁链 ---- */
ST_FUNC void gsym_addr(int t_, int a_)
{
    uint32_t t = t_, a = a_;
    while (t) {
        unsigned char *ptr = cur_text_section->data + t;
        uint32_t next = read32le(ptr);
        int slot_off = (int)((a - t) / 4);
        assert(((a - t) & 3) == 0);
        write32le(ptr, TRI_ENC_U(TRI_OP_JAL, slot_off, 0));
        t = next;
    }
}

ST_FUNC int gjmp(int t)
{
    if (nocode_wanted)
        return t;
    o(t);
    return ind - 4;
}

ST_FUNC void gjmp_addr(int a)
{
    int r = a - ind;
    assert((r & 3) == 0);
    TRI_JAL(0, r / 4);
}

/* 比较 + 条件跳转：TriRV B 型 funct BEQ0 BNE1 BLT2 BGE3 BLTU4 BGEU5。
 * invert-branch-over 技巧：发"取反比较 +1 槽"分支，跳过 gjmp 槽。
 * LE/GT/ULE/UGT 无指令：交换操作数后取反（a<=b ⟺ ¬(b<a) 等）。 */
ST_FUNC int gjmp_cond(int op, int t)
{
    int a = vtop->cmp_r & 0xff;
    int b = (vtop->cmp_r >> 8) & 0xff;
    int funct;
    switch (op) {
    case TOK_EQ:  funct = 1; break;
    case TOK_NE:  funct = 0; break;
    case TOK_LT:  funct = 3; break;
    case TOK_GE:  funct = 2; break;
    case TOK_ULT: funct = 5; break;
    case TOK_UGE: funct = 4; break;
    case TOK_LE:  funct = 2; { int x = a; a = b; b = x; } break;
    case TOK_GT:  funct = 3; { int x = a; a = b; b = x; } break;
    case TOK_ULE: funct = 4; { int x = a; a = b; b = x; } break;
    case TOK_UGT: funct = 5; { int x = a; a = b; b = x; } break;
    default: tcc_error("TriRV: bad cond op %d", op); return t;
    }
    o(TRI_ENC_S(TRI_OP_BRANCH, 2 /* 取反分支 +2 槽：越过 gjmp 槽（对齐 riscv64 的 imm=8 字节=2 槽） */, b, a, funct));
    return gjmp(t);
}

ST_FUNC int gjmp_append(int n, int t)
{
    void *p;
    if (n) {
        uint32_t n1 = n, n2;
        while ((n2 = read32le(p = cur_text_section->data + n1)))
            n1 = n2;
        write32le(p, t);
        t = n;
    }
    return t;
}

ST_FUNC void gen_fill_nops(int bytes)
{
    if ((bytes & 3))
      tcc_error("alignment of code section not multiple of 4");
    while (bytes > 0) { TRI_NOP(); bytes -= 4; }
}

/* ---- 地址准备：返回基址寄存器（TriRV 号），*fc 出参最终偏移 ---- */
static int tri_addr_base(SValue *sv, int forstore, int *fc)
{
    int v = sv->r & VT_VALMASK;
    *fc = (int)(int64_t)sv->c.i;
    if (sv->r & VT_SYM) {
        /* 全局/静态：LUI 占位 + 补丁；偏移并入 addend */
        tri_lui_load(TRI_T0, sv->sym, *fc);
        *fc = 0;
        return TRI_T0;
    }
    if (v == VT_LOCAL) {
        if (*fc < -TRI_IMM7_MAX || *fc > TRI_IMM7_MAX)
          tcc_error("TriRV spike: frame offset %d too large", *fc);
        return TRI_S0;
    }
    if (v < VT_CONST) {
        *fc = 0;                 /* spike：寄存器带偏移 UNSUPPORTED */
        return ireg(v);
    }
    tcc_error("TriRV spike: bad addr class %d", v);
    return 0;
}

static void tri_check_off(int fc)
{
    if (fc < -TRI_IMM7_MAX || fc > TRI_IMM7_MAX)
      tcc_error("TriRV spike: mem offset %d too large", fc);
}

static int tri_const_fits(int64_t v) { return v >= -TRI_IMM7_MAX && v <= TRI_IMM7_MAX; }

/* 正数段装载：LUI(hi13) [+ ADDI(lo7)]；对齐 trirv_cc load_seg（非负路径） */
static void tri_load_seg_abs(unsigned rr, long long n)
{
    long long h = n / 2187, lo = n % 2187;
    const long long HALF7 = 1093;
    if (lo > HALF7) { lo -= 2187; h += 1; }
    if (h == 0) {
        if (lo) TRI_ADDI(rr, 0, (int)lo); else TRI_NOP();
    } else {
        o(TRI_ENC_U(TRI_OP_LUI, (unsigned)h, rr));
        if (lo) TRI_ADDI(rr, rr, (int)lo);
    }
}

/* 大小 -> mem funct（TriRV: 1/2/9 tryte；LBU/LHU funct3/4 零扩展，LB/LH 0/1 符号） */
static int tri_mem_funct(int size, int *ok)
{
    *ok = 1;
    switch (size) {
    case 1: return 0;
    case 2: return 1;
    case 9: return 2;
    default: *ok = 0; return 0;
    }
}

ST_FUNC void load(int r, SValue *sv)
{
    int fr = sv->r, v = fr & VT_VALMASK;
    int fc, base, funct, ok;
    unsigned rr = is_ireg(r) ? ireg(r) : freg(r);
    int bt = sv->type.t & VT_BTYPE;
    int align, size = type_size(&sv->type, &align);

    if (bt == VT_FLOAT || bt == VT_DOUBLE || bt == VT_LDOUBLE)
      tcc_error("TriRV spike: no float load");

    if (fr & VT_LVAL) {
        if (bt == VT_PTR || bt == VT_FUNC)
          size = PTR_SIZE;
        funct = tri_mem_funct(size, &ok);
        if (!ok) tcc_error("TriRV spike: bad load size %d", size);
        base = tri_addr_base(sv, 0, &fc);
        tri_check_off(fc);
        {
            unsigned funct_l = funct;
            if (size == 1) funct_l = (sv->type.t & VT_UNSIGNED) ? 3 : 0;  /* LBU/LB */
            else if (size == 2) funct_l = (sv->type.t & VT_UNSIGNED) ? 4 : 1; /* LHU/LH */
            o(TRI_ENC_I(TRI_OP_LOAD, fc, base, funct_l, rr));
        }
        return;
    }
    if (v == VT_CONST) {
        if (fr & VT_SYM) {
            tri_lui_load(rr, sv->sym, (int)(int64_t)sv->c.i);
            return;
        }
        if (tri_const_fits((int64_t)sv->c.i)) {
            TRI_ADDI(rr, 0, (int)(int64_t)sv->c.i);
        } else if ((int64_t)sv->c.i >= -(int64_t)2147483647 && (int64_t)sv->c.i <= (int64_t)2147483647) {
            /* LUI hi13 + ADDI lo7 两槽路径（i32 窗口内）。
               坑实录：CValue.i 是 uint64_t（tcc.h CValue 联合），裸写
               `sv->c.i >= -(int64_t)2147483647` 触发 usual arithmetic
               conversions——负常量转巨大无符号数，条件恒 FALSE，12345
               误入错误分支。比较必须显式 (int64_t) 转换（本次审计修复）。 */
            int64_t lo = (int64_t)sv->c.i % 2187, hi;
            if (lo > TRI_IMM7_MAX) lo -= 2187;
            hi = ((int64_t)sv->c.i - lo) / 2187;
            if (hi < -TRI_IMM13_MAX || hi > TRI_IMM13_MAX)
              tcc_error("TriRV spike: constant %lld beyond LUI window", (long long)sv->c.i);
            o(TRI_ENC_U(TRI_OP_LUI, (unsigned)((hi % 1594323) + 1594323) % 1594323u, rr));
            TRI_ADDI(rr, rr, (int)lo);
        } else {
            /* 3×20-trit 平衡分段（对齐 trirv_cc load_imm128 口径）：
               v = s0 + s1·3^20 + s2·3^20²，每段经 load_seg = LUI(hi13)+ADDI(lo7)。
               段间借位用 m++ 传播（trirv_cc 同款）。 */
            long long segs[8];
            int ns = 0;
            int64_t m = (int64_t)sv->c.i;
            while (m) {
                int64_t seg = m % 3486784401LL;
                m /= 3486784401LL;
                if (seg > 1743392200LL) { seg -= 3486784401LL; m += 1; }
                segs[ns++] = seg;
            }
            if (ns == 0) { TRI_NOP(); }
            else {
                int si;
                /* 高段：load_seg（LUI hi13 + ADDI lo7，负段用 SUB zero,reg 取负） */
                long long n = segs[ns - 1];
                if (n < 0) {
                    tri_load_seg_abs(rr, -n);
                    o(TRI_ENC_R(TRI_SUB_OP, TRI_SUB_FUNCT2, rr, 0, TRI_SUB_FUNCT, rr)); /* SUB rr,zero,rr */
                } else {
                    tri_load_seg_abs(rr, n);
                }
                for (si = ns - 2; si >= 0; si--) {
                    o(TRI_ENC_I(TRI_OP_OPIMM, 20, rr, 4 /* SLLI funct=4 */, rr)); /* SLLI rr,rr,20 */
                    n = segs[si];
                    if (n < 0) {
                        tri_load_seg_abs(5 /* t0 */, -n);
                        o(TRI_ENC_R(TRI_SUB_OP, TRI_SUB_FUNCT2, 5, 0, TRI_SUB_FUNCT, 5));
                    } else {
                        tri_load_seg_abs(5, n);
                    }
                    o(TRI_ENC_R(TRI_ADD_OP, TRI_ADD_FUNCT2, 5, rr, TRI_ADD_FUNCT, rr)); /* ADD rr,rr,t0 */
                }
            }
        }
        return;
    }
    if (v == VT_LOCAL || v == VT_LLOCAL) {
        base = tri_addr_base(sv, 0, &fc);
        tri_check_off(fc);
        TRI_ADDI(rr, base, fc);
        return;
    }
    if (v < VT_CONST) {
        /* reg-reg move：ADDI rr, src, 0 */
        TRI_ADDI(rr, ireg(v), 0);
        return;
    }
    if (v == VT_CMP) {
        /* 比较物化：全部经 SLT/SLTU + SLTIU-取反（三进制语义红线） */
        int op = vtop->cmp_op;
        int a = vtop->cmp_r & 0xff;
        int b = (vtop->cmp_r >> 8) & 0xff;
        int swap, notafter;
        /* 归一：GE/LE/GT/UGT/UGE/ULE → SLT/SLTU + 可选取反 */
        switch (op) {
        case TOK_LT:  swap = 0, notafter = 0; goto do_slt;
        case TOK_GT:  swap = 0, notafter = 1; goto do_slt;
        case TOK_GE:  swap = 1, notafter = 1; goto do_slt;
        case TOK_LE:  swap = 1, notafter = 0; goto do_slt;
        do_slt:
            o(TRI_ENC_R(TRI_SLT_OP, TRI_SLT_FUNCT2, swap ? a : b, swap ? b : a, TRI_SLT_FUNCT, rr)); /* SLT */
            if (notafter) TRI_SEQZ_NOT(rr);
            break;
        case TOK_ULT: swap = 0, notafter = 0; goto do_sltu;
        case TOK_UGT: swap = 0, notafter = 1; goto do_sltu;
        case TOK_UGE: swap = 1, notafter = 1; goto do_sltu;
        case TOK_ULE: swap = 1, notafter = 0; goto do_sltu;
        do_sltu:
            o(TRI_ENC_R(TRI_SLTU_OP, TRI_SLTU_FUNCT2, swap ? a : b, swap ? b : a, TRI_SLTU_FUNCT, rr)); /* SLTU */
            if (notafter) TRI_SEQZ_NOT(rr);
            break;
        case TOK_NE:
            o(TRI_ENC_R(TRI_OP_OP, 0, b, a, 1, rr));   /* SUB */
            TRI_SNEZ(rr);
            break;
        case TOK_EQ:
            o(TRI_ENC_R(TRI_OP_OP, 0, b, a, 1, rr));   /* SUB */
            TRI_SNEZ(rr);
            TRI_SEQZ_NOT(rr);
            break;
        default: tcc_error("TriRV: bad cmp op");
        }
        return;
    }
    if ((v & ~1) == VT_JMP) {
        int tval = v & 1;
        assert(is_ireg(r));
        TRI_ADDI(rr, 0, tval);
        gjmp_addr(ind + 8);
        gsym(fc);
        TRI_ADDI(rr, 0, tval ^ 1);
        return;
    }
    tcc_error("TriRV spike: load class %d", v);
}

ST_FUNC void store(int r, SValue *sv)
{
    int fr = sv->r & VT_VALMASK;
    int fc, base, funct, ok;
    unsigned rr = is_ireg(r) ? ireg(r) : freg(r);
    int bt = sv->type.t & VT_BTYPE;
    int align, size = type_size(&sv->type, &align);

    if (bt == VT_FLOAT || bt == VT_DOUBLE || bt == VT_LDOUBLE)
      tcc_error("TriRV spike: no float store");
    if (bt == VT_STRUCT)
      tcc_error("TriRV spike: store(struct)");
    if (size > 9) tcc_error("TriRV spike: large store %d", size);
    assert(sv->r & VT_LVAL);
    funct = tri_mem_funct(size, &ok);
    if (!ok) tcc_error("TriRV spike: bad store size %d", size);
    base = tri_addr_base(sv, 1, &fc);
    tri_check_off(fc);
    o(TRI_ENC_S(TRI_OP_STORE, fc, rr, base, funct));
}

/* ---- 函数序/跋（帧：9-tryte；ra@s0-9、s0@s0-18；序言 5 槽回填） ---- */
static int func_sub_sp_offset, num_va_regs, func_va_list_ofs;

ST_FUNC void gfunc_prolog(Sym *func_sym)
{
    CType *func_type = &func_sym->type;
    Sym *sym;
    int align, areg = 0, param_addr;

    sym = func_type->ref;
    loc = -18;
    func_sub_sp_offset = ind;
    ind += 5 * 4;                                /* 预留 5 槽（epilog 回填） */

    if (type_size(&func_vt, &align) > XLEN) {
        loc -= 9;
        func_vc = loc;
        o(TRI_ENC_S(TRI_OP_STORE, loc, TRI_A0, TRI_S0, 2)); /* SW a0,loc(s0) sret 隐形指针 */
        areg++;
    }
    while ((sym = sym->next) != NULL) {
        CType *type = &sym->type;
        int size;
        if (type->t & VT_ARRAY) { type = &char_pointer_type; }
        if ((type->t & VT_BTYPE) == VT_STRUCT)
          tcc_error("TriRV spike: struct param");
        size = type_size(type, &align);
        if (size > 9) size = 9;
        if (areg >= 8)
          tcc_error("TriRV spike: stack params");
        loc -= 9;
        param_addr = loc;
        o(TRI_ENC_S(TRI_OP_STORE, param_addr, 10 + areg, TRI_S0, 2)); /* SW aX,loc(s0) */
        gfunc_set_param(sym, param_addr, 0);
        areg++;
    }
    func_va_list_ofs = 0;
    num_va_regs = 0;
}

ST_FUNC int gfunc_sret(CType *vt, int variadic, CType *ret,
                       int *ret_align, int *regsize)
{
    int align, size = type_size(vt, &align);
    *ret_align = 1; *regsize = 9;
    if (size > XLEN)
      return 0;
    ret->t = vt->t & (VT_BTYPE | VT_UNSIGNED);
    ret->ref = NULL;
    return 1;
}

ST_FUNC void arch_transfer_ret_regs(int aftercall)
{
    tcc_error("TriRV spike: arch_transfer_ret_regs");
}

ST_FUNC void gfunc_epilog(void)
{
    int d, saved_ind;

    d = ((-loc + 8) / 9) * 9;                    /* 9-tryte 对齐帧大小 */

    TRI_ADDI(TRI_SP, TRI_S0, num_va_regs * 9);            /* addi sp,s0,varest */
    o(TRI_ENC_I(TRI_OP_LOAD, -9, TRI_S0, 2, TRI_RA));     /* LW ra,-9(s0) */
    o(TRI_ENC_I(TRI_OP_LOAD, -18, TRI_S0, 2, TRI_S0));    /* LW s0,-18(s0) */
    TRI_JALR(0, TRI_RA);                                   /* ret */

    saved_ind = ind;
    ind = func_sub_sp_offset;
    TRI_ADDI(TRI_SP, TRI_SP, -d);                          /* addi sp,sp,-d */
    o(TRI_ENC_S(TRI_OP_STORE, d - 9, TRI_RA, TRI_SP, 2));  /* SW ra,d-9(sp) */
    o(TRI_ENC_S(TRI_OP_STORE, d - 18, TRI_S0, TRI_SP, 2)); /* SW s0,d-18(sp) */
    TRI_ADDI(TRI_S0, TRI_SP, d);                           /* addi s0,sp,d */
    TRI_NOP();
    ind = saved_ind;
}

/* ---- 调用 ---- */
static void gcall_or_jmp(int docall)
{
    int tr = docall ? TRI_RA : TRI_T0;
    if ((vtop->r & (VT_VALMASK | VT_LVAL)) == VT_CONST &&
        (vtop->r & VT_SYM)) {
        o(TRI_ENC_U(TRI_OP_JAL, 0, tr));
        tri_add_patch(TRI_R_JAL_SLOT, ind - 4, vtop->sym, (int)(int64_t)vtop->c.i, tr);
    } else if (vtop->r < VT_CONST) {
        TRI_JALR(tr, ireg(vtop->r));
    } else {
        int r = TREG_RA;
        load(r, vtop);
        TRI_JALR(tr, ireg(r));
    }
}

ST_FUNC void gfunc_call(int nb_args)
{
    int i;
    if (nb_args > 8)
      tcc_error("TriRV spike: stack args (>%d)", 8);
    /* 实参依序装入 a0..a7：arg i 在 vtop[1+i-nb_args]，gv 进单寄存器类 RC_R(i) */
    for (i = 0; i < nb_args; i++) {
        int ii = nb_args - 1 - i;                /* 从最右往左弹 */
        vrotb(ii + 1);
        gv(RC_R(ii));
        if (vtop->r != ii) {
            /* gv 未落到目标寄存器：显式 mv */
            TRI_ADDI(ireg(ii), ireg(vtop->r), 0);
            vtop->r = ii;
        }
        vrott(ii + 1);
    }
    vrotb(nb_args + 1);                          /* 函数标记到顶 */
    save_regs(nb_args + 1);
    gcall_or_jmp(1);
    vtop -= nb_args + 1;
}

/* ---- 整数运算（spike 子集；const 融合限 ADDI 窗口） ---- */
static void gen_opil(int op)
{
    int a, b, d;
    if ((vtop->r & (VT_VALMASK | VT_LVAL | VT_SYM)) == VT_CONST) {
        int fc = (int)(int64_t)vtop->c.i;
        if (tri_const_fits(fc)) {
            int fuse = 0, im = fc;
            switch (op) {
            case '-': im = -fc; fuse = 1; break;
            case '+': fuse = 1; break;
            case '&': fuse = 2; break;   /* ANDI funct=6 */
            case '|': fuse = 3; break;   /* ORI funct=3 */
            case '^': fuse = 4; break;   /* XORI funct=2（注意：三进制 mod-3 加！
                                            spike 对 & | ^ 直接发 ANDI/ORI/XORI——
                                            语义=逐 trit 运算，与 trirv_cc 的 C 位运算
                                            方案 A 不一致，spike 只用于无位运算程序，
                                            T-1 收尾时接方案 A 适配层） */
            default: break;
            }
            if (fuse) {
                vswap();
                gv(RC_INT);
                a = ireg(vtop[0].r);
                --vtop;
                d = get_reg(RC_INT);
                ++vtop;
                vswap();
                if (fuse == 1) TRI_ADDI(ireg(d), a, im);
                else o(TRI_ENC_I(TRI_OP_OPIMM, im, a,
                                 fuse == 2 ? 6 : fuse == 3 ? 3 : 2, ireg(d)));
                --vtop;
                vtop[0].r = d;
                return;
            }
        }
    }
    /* C 移位 = 二进制移位：<<= ×2^k，>>= ÷2^k（TriRV SLL/SRL/SRA 是三进制
       ×3^k——直译是语义错误，t 左移/算术右移 用例暴露）。const 移位量走
       MUL/DIVU/DIV by 2^k 常量（方案 A 口径；DIV 向零截断，负数非精确除
       与 gcc floor 的差异记档）；变量移位量 spike 报错。 */
    if ((op == TOK_SHL || op == TOK_SHR || op == TOK_SAR)
        && (vtop[0].r & (VT_VALMASK | VT_LVAL | VT_SYM)) == VT_CONST
        && (int64_t)vtop[0].c.i >= 0 && (int64_t)vtop[0].c.i <= 45) {
        long long shift = (int64_t)vtop[0].c.i;
        long long mul2 = 1LL << shift;           /* ≤ 2^45，int64 内 */
        vtop[0].c.i = (uint64_t)(1LL << (int64_t)vtop[0].c.i);  /* 栈顶常量 k → 2^k（MUL/DIV 常量） */
        gv2(RC_INT, RC_INT);                     /* 双操作数物化（标准路径，无泄漏） */
        a = ireg(vtop[-1].r);
        b = ireg(vtop[0].r);
        vtop -= 2;
        d = get_reg(RC_INT);
        vtop++;
        vtop[0].r = d;
        d = ireg(d);
        if (op == TOK_SHL)
          o(TRI_ENC_R(TRI_MUL_OP, TRI_MUL_FUNCT2, b, a, TRI_MUL_FUNCT, d));    /* MUL d,a,2^k */
        else if (op == TOK_SHR)
          o(TRI_ENC_R(TRI_DIVU_OP, TRI_DIVU_FUNCT2, b, a, TRI_DIVU_FUNCT, d)); /* DIVU d,a,2^k */
        else
          o(TRI_ENC_R(TRI_DIV_OP, TRI_DIV_FUNCT2, b, a, TRI_DIV_FUNCT, d));   /* DIV d,a,2^k（向零截断） */
        return;
    }
    gv2(RC_INT, RC_INT);
    a = ireg(vtop[-1].r);
    b = ireg(vtop[0].r);
    vtop -= 2;
    d = get_reg(RC_INT);
    vtop++;
    vtop[0].r = d;
    d = ireg(d);
    switch (op) {
    default:
        if (op >= TOK_ULT && op <= TOK_GT) {
            vset_VT_CMP(op);
            vtop->cmp_r = a | b << 8;
            break;
        }
        tcc_error("TriRV spike: implement me %s", get_tok_str(op, NULL));
        break;
    /* R 型编码一律走 tri-isa.h 生成宏（op/funct/funct2 与 isa.encode 96/96 对齐）。
       坑实录：此处曾误用 RISC-V funct3 语义直填 funct——SUB 发成 SLL、
       比较物化发成 OR 等 6 类错码（t4/t 减 暴露）。 */
    case '+': o(TRI_ENC_R(TRI_ADD_OP, TRI_ADD_FUNCT2, b, a, TRI_ADD_FUNCT, d)); break;
    case '-': o(TRI_ENC_R(TRI_SUB_OP, TRI_SUB_FUNCT2, b, a, TRI_SUB_FUNCT, d)); break;
    case '*': o(TRI_ENC_R(TRI_MUL_OP, TRI_MUL_FUNCT2, b, a, TRI_MUL_FUNCT, d)); break;
    case '/': case TOK_PDIV: o(TRI_ENC_R(TRI_DIV_OP, TRI_DIV_FUNCT2, b, a, TRI_DIV_FUNCT, d)); break;
    case TOK_UDIV: o(TRI_ENC_R(TRI_DIVU_OP, TRI_DIVU_FUNCT2, b, a, TRI_DIVU_FUNCT, d)); break;
    case '%': o(TRI_ENC_R(TRI_REM_OP, TRI_REM_FUNCT2, b, a, TRI_REM_FUNCT, d)); break;
    case TOK_UMOD: o(TRI_ENC_R(TRI_REMU_OP, TRI_REMU_FUNCT2, b, a, TRI_REMU_FUNCT, d)); break;
    case TOK_SHL: o(TRI_ENC_R(TRI_SLL_OP, TRI_SLL_FUNCT2, b, a, TRI_SLL_FUNCT, d)); break;
    case TOK_SHR: o(TRI_ENC_R(TRI_SRL_OP, TRI_SRL_FUNCT2, b, a, TRI_SRL_FUNCT, d)); break;
    case TOK_SAR: o(TRI_ENC_R(TRI_SRA_OP, TRI_SRA_FUNCT2, b, a, TRI_SRA_FUNCT, d)); break;
    case '&': o(TRI_ENC_R(TRI_AND_OP, TRI_AND_FUNCT2, b, a, TRI_AND_FUNCT, d)); break;
    case '|': o(TRI_ENC_R(TRI_OR_OP, TRI_OR_FUNCT2, b, a, TRI_OR_FUNCT, d)); break;
    case '^': o(TRI_ENC_R(TRI_XOR_OP, TRI_XOR_FUNCT2, b, a, TRI_XOR_FUNCT, d)); break;
    }
}

ST_FUNC void gen_opi(int op) { gen_opil(op); }
ST_FUNC void gen_opl(int op) { gen_opil(op); }

/* ---- spike 不支持的路径（诚实报错） ---- */
ST_FUNC void gen_opf(int op) { tcc_error("TriRV spike: no float ops"); }
ST_FUNC void gen_cvt_itof(int t) { tcc_error("TriRV spike: no float"); }
ST_FUNC void gen_cvt_ftoi(int t) { tcc_error("TriRV spike: no float"); }
ST_FUNC void gen_cvt_ftof(int dt) { tcc_error("TriRV spike: no float"); }
ST_FUNC void gen_cvt_sxtw(void) { /* 45-trit 单宽，无 32→64 问题：no-op */ }
ST_FUNC void gen_cvt_csti(int t) { tcc_error("TriRV spike: gen_cvt_csti"); }
ST_FUNC void gen_increment_tcov(SValue *sv) { tcc_error("TriRV spike: tcov"); }
ST_FUNC void gen_va_start(void) { tcc_error("TriRV spike: varargs"); }
ST_FUNC void gen_clear_cache(void) { /* no-op */ }
ST_FUNC void gen_vla_sp_save(int addr) { tcc_error("TriRV spike: VLA"); }
ST_FUNC void gen_vla_sp_restore(int addr) { tcc_error("TriRV spike: VLA"); }
ST_FUNC void gen_vla_alloc(CType *type, int align) { tcc_error("TriRV spike: VLA"); }

ST_FUNC void ggoto(void)
{
    gcall_or_jmp(0);
    vtop--;
}

#endif /* !TARGET_DEFS_ONLY */
