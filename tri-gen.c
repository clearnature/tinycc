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
/* 浮点寄存器：tcc 浮点槽 r8-15 → freg 索引 0-7（fa0-fa7，独立 freg 堆，
 * 与整数 x0-x31 分离——TriRV F 扩展在两套 VM 各有 freg[32]） */
static int freg(int r)
{
    assert(r >= 8 && r < 16);
    return r - 8;
}
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
/* SNEZ：SLTU rd,zero,rd = (0 <u rd) = rd≠0。坑实录：曾把宏实参写成
   (0, rd)——TRI_ENC_R 第 3 参是 rs2，结果编成 SLTU rd,rd,zero ≡ 恒 0
   （NE 物化恒 0 / EQ 物化恒 1，3!=4 探针暴露）。 */
static void TRI_SNEZ(unsigned rd)     { o(TRI_ENC_R(TRI_SLTU_OP, TRI_SLTU_FUNCT2, rd, 0, TRI_SLTU_FUNCT, rd)); }

/* ---- 符号地址补丁表（类型在 tri-isa.h；LUI 绝对/ JAL 槽偏移） ---- */
/* 定义（tentative）：见 tri-isa.h 声明 */

ST_FUNC void tri_add_patch(int kind, int text_off, Sym *sym, int addend, unsigned rd)
{
    if (!(NTP & 15)) TRI_PATCHES = tcc_realloc(TRI_PATCHES, (NTP + 16) * sizeof *TRI_PATCHES);
    TRI_PATCHES[NTP].text_off = text_off;
    TRI_PATCHES[NTP].kind = kind;
    /* Sym 存活窗口内快照名字 + elf 索引（writer 期 Sym* 悬垂实证：
       extern 声明与定义是不同 Sym（声明 c 恒 0）、static 符号 Sym 在其
       TU 后回收——垃圾 c 曾致 "symbol #1719946136"。匿名符号（字符串
       数据）无名但创建即有 elf 条目，靠 elf_idx 解析。 */
    TRI_PATCHES[NTP].elf_idx = sym->c;
    TRI_PATCHES[NTP].name = (sym->v >= SYM_FIRST_ANOM)
        ? NULL : tcc_strdup(get_tok_str(sym->v, NULL));
    TRI_PATCHES[NTP].addend = addend;
    TRI_PATCHES[NTP].rd = rd;
    NTP++;
}

static int tri_const_fits(int64_t v);
ST_FUNC void tri_add_patch_idx(int kind, int text_off, int elf_idx, int addend, unsigned rd);
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

/* 绝对地址装载（编译期已建 elf 条目：rodata 浮点常量池） */
static void tri_lui_load_idx(unsigned rr, int elf_idx, int addend)
{
    o(TRI_ENC_U(TRI_OP_LUI, 0, rr));
    o(TRI_ENC_I(TRI_OP_OPIMM, 0, rr, 0, rr));
    tri_add_patch_idx(TRI_R_LUI_ABS, ind - 8, elf_idx, addend, rr);
}

/* ---- 跳转与补丁链 ---- */
ST_FUNC void gsym_addr(int t_, int a_)
{
    uint32_t t = t_;
    while (t) {
        unsigned char *ptr = cur_text_section->data + t;
        uint32_t next = read32le(ptr);
        /* 坑实录：曾用 uint32_t 的 (a - t) 做槽差——无符号减法对后向跳转
           回绕（(64-96) → 4294967264 → /4 = 1073741816 槽），JAL 编成垃圾。
           前向（if/else、while 退出）侥幸全对，第一个后向用例（for 的
           continue 绑定 a<site）由双工具链差分套件暴露。 */
        int slot_off = (int)((a_ - (int)t) / 4);
        assert(((a_ - (int)t) & 3) == 0);
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

/* 无 Sym 的补丁（rodata 常量池等编译期已建 elf 条目的场合） */
ST_FUNC void tri_add_patch_idx(int kind, int text_off, int elf_idx, int addend, unsigned rd)
{
    if (!(NTP & 15)) TRI_PATCHES = tcc_realloc(TRI_PATCHES, (NTP + 16) * sizeof *TRI_PATCHES);
    TRI_PATCHES[NTP].text_off = text_off;
    TRI_PATCHES[NTP].kind = kind;
    TRI_PATCHES[NTP].elf_idx = elf_idx;
    TRI_PATCHES[NTP].name = NULL;
    TRI_PATCHES[NTP].addend = addend;
    TRI_PATCHES[NTP].rd = rd;
    NTP++;
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

/* 3^45（__int128 常量表达式 = 3^20·3^20·3^5）；45-trit 字域 [0,3^45) */
#define TRI_P45 ((__int128)3486784401LL * (__int128)3486784401LL * 243)

/* 2^k mod 3^45（k≤4096 逐次翻倍，编译期求值；方案 A 移位用） */
static unsigned __int128 tri_pow2_mod(long long k)
{
    unsigned __int128 m = 1;
    long long i;
    for (i = 0; i < k; i++) { m <<= 1; if (m >= TRI_P45) m -= TRI_P45; }
    return m;
}

/* 通用分段常量装载：把 < 3^45 的值（raw 位模式）按 3×20-trit 平衡分段装进
 * 硬件寄存器 hw。段间借位双向归一（trirv_cc load_imm128 同款），负数段经
 * SUB zero,reg 取负。修 D6：旧 LUI+ADDI 两槽路径负 hi 回绕成 LUI 正数
 * （差 k×3^20）、lo 借位只处理上界、|v|>1743392200 误报窗口溢出——全部删除，
 * 非 ADDI 常量一律走这里。 */
static void tri_load_imm_hw(unsigned hw, unsigned __int128 v)
{
    long long segs[4];
    int ns = 0, si;
    long long n;
    while (v) {
        unsigned __int128 seg = v % 3486784401ULL;
        v /= 3486784401ULL;
        if (seg > 1743392200ULL) { seg -= 3486784401ULL; v += 1; }
        segs[ns++] = (long long)seg;
    }
    if (ns == 0) { TRI_NOP(); return; }
    n = segs[ns - 1];
    if (n < 0) {
        tri_load_seg_abs(hw, -n);
        o(TRI_ENC_R(TRI_SUB_OP, TRI_SUB_FUNCT2, hw, 0, TRI_SUB_FUNCT, hw)); /* SUB hw,zero,hw */
    } else {
        tri_load_seg_abs(hw, n);
    }
    for (si = ns - 2; si >= 0; si--) {
        n = segs[si];
        if (n == 0) {
            /* 中段 0：只移位不装载不加——坑实录：曾无条件 ADD T0，而
               tri_load_seg_abs(T0,0) 是 NOP 不清 T0——T0 的残留垃圾被加进
               累加器（strtod 的 mag<=-14130 判断装 -14130=3^45-14130 三段
               含中段 0 → 判断恒错 → return 0；负多段常量首暴露）。 */
            o(TRI_ENC_I(TRI_OP_OPIMM, 20, hw, 4 /* SLLI */, hw)); /* hw *= 3^20 */
            continue;
        }
        o(TRI_ENC_I(TRI_OP_OPIMM, 20, hw, 4 /* SLLI */, hw)); /* hw *= 3^20 */
        if (n < 0) {
            tri_load_seg_abs(TRI_T0, -n);
            o(TRI_ENC_R(TRI_SUB_OP, TRI_SUB_FUNCT2, TRI_T0, 0, TRI_SUB_FUNCT, TRI_T0));
        } else {
            tri_load_seg_abs(TRI_T0, n);
        }
        o(TRI_ENC_R(TRI_ADD_OP, TRI_ADD_FUNCT2, TRI_T0, hw, TRI_ADD_FUNCT, hw)); /* ADD hw,hw,t0 */
    }
}

/* ---- 方案 A 适配层（C 位运算/移位二进制语义，ternary-isa-design.md §5.1） ----
 * 关键事实：45-trit 寄存器的 raw 值就是位模式——负数 raw = 3^45-|x|，
 * 与 bits(x) = x mod 3^45 完全一致。全程模 3^45 域运算（ADD/SUB 天然回绕）。
 * 工作寄存器用空闲硬件槽（gp/tp/t1/t2/s1）：位游标块是直线代码——块内无
 * load/store/调用，寄存器块内定义块内消费，不需 callee-saved 预扫描
 * （trirv_cc 的 gate_check 方案在 tcc 单遍架构下无函数体可预扫）。 */
#define TRI_B_VA   3u   /* va：左值拷贝（gp 槽，tri-gen/tri-link 不用） */
#define TRI_B_VB   4u   /* vb：右值拷贝（tp 槽） */
#define TRI_B_AC   6u   /* acc：累加 / bitnot 常量（t1） */
#define TRI_B_PP   7u   /* pp：位游标权重 2^71→1（t2） */
#define TRI_B_TWO  9u   /* two：常量 2（s1） */
#define TRI_SCR    5u   /* scratch = t0：块内无访存，安全 */

/* dst = a & b / a | b / a ^ b（kind 0/1/2）——72 位位游标循环：
 * p 从 2^71 走到 1（DIVU /2 递推），逐位 SLTU+SLTIU 剥离 a/b 的该位，
 * & = TMIN、| = TMAX、^ = |ta-tb|，位为 1 时 acc += p。
 * 坑实录：曾直译 AND/OR/XOR/XORI 逐 trit 语义（6&4→3 而非 4）。
 * 参考 trirv_cc.c emit_bitop3；分支偏移全静态（+2 跳过 1 槽 / 回跳计算）。 */
static void tri_emit_bitop3(unsigned dst, unsigned aR, unsigned bR, int kind)
{
    unsigned va = TRI_B_VA, vb = TRI_B_VB, ac = TRI_B_AC, pp = TRI_B_PP, two = TRI_B_TWO;
    unsigned scr = TRI_SCR;
    int loop_ind;
    TRI_ADDI(va, aR, 0);
    TRI_ADDI(vb, bR, 0);
    TRI_ADDI(ac, 0, 0);
    tri_load_imm_hw(pp, (__int128)1 << 71);
    TRI_ADDI(two, 0, 2);
    loop_ind = (int)ind;
    /* ta = (va >=u pp)，同时剥离 va 的该位 */
    o(TRI_ENC_R(TRI_SLTU_OP, TRI_SLTU_FUNCT2, pp, va, TRI_SLTU_FUNCT, scr));
    o(TRI_ENC_I(TRI_OP_OPIMM, 1, scr, 5 /* SLTIU */, scr));
    o(TRI_ENC_S(TRI_OP_BRANCH, 2, 0, scr, 0 /* BEQ */));
    o(TRI_ENC_R(TRI_SUB_OP, TRI_SUB_FUNCT2, pp, va, TRI_SUB_FUNCT, va));
    /* tb 借 dst 当临时（a、b 已拷出，dst 此刻空闲） */
    o(TRI_ENC_R(TRI_SLTU_OP, TRI_SLTU_FUNCT2, pp, vb, TRI_SLTU_FUNCT, dst));
    o(TRI_ENC_I(TRI_OP_OPIMM, 1, dst, 5, dst));
    o(TRI_ENC_S(TRI_OP_BRANCH, 2, 0, dst, 0));
    o(TRI_ENC_R(TRI_SUB_OP, TRI_SUB_FUNCT2, pp, vb, TRI_SUB_FUNCT, vb));
    if (kind == 0)       /* & = 逐位 min */
      o(TRI_ENC_R(TRI_TMIN_OP, TRI_TMIN_FUNCT2, dst, scr, TRI_TMIN_FUNCT, scr));
    else if (kind == 1)  /* | = 逐位 max */
      o(TRI_ENC_R(TRI_TMAX_OP, TRI_TMAX_FUNCT2, dst, scr, TRI_TMAX_FUNCT, scr));
    else {               /* ^ = |ta - tb|（ta/tb ∈ {0,1}） */
      o(TRI_ENC_R(TRI_SUB_OP, TRI_SUB_FUNCT2, dst, scr, TRI_SUB_FUNCT, scr));
      o(TRI_ENC_R(TRI_TMAX_OP, TRI_TMAX_FUNCT2, 0, scr, TRI_TMAX_FUNCT, dst));
      o(TRI_ENC_R(TRI_TMIN_OP, TRI_TMIN_FUNCT2, 0, scr, TRI_TMIN_FUNCT, scr));
      o(TRI_ENC_R(TRI_SUB_OP, TRI_SUB_FUNCT2, scr, dst, TRI_SUB_FUNCT, scr));
    }
    o(TRI_ENC_S(TRI_OP_BRANCH, 2, 0, scr, 0));
    o(TRI_ENC_R(TRI_ADD_OP, TRI_ADD_FUNCT2, pp, ac, TRI_ADD_FUNCT, ac)); /* acc += p */
    o(TRI_ENC_R(TRI_DIVU_OP, TRI_DIVU_FUNCT2, two, pp, TRI_DIVU_FUNCT, pp)); /* p /= 2 */
    o(TRI_ENC_S(TRI_OP_BRANCH, (loop_ind - (int)ind) / 4, 0, pp, 1 /* BNE */));
    if (dst != ac) TRI_ADDI(dst, ac, 0);
}

/* dst = ~a = ((2^72-1) - bits(a)) mod 3^45。TILDE = (2^72-1) mod 3^45
 * ≈1.77e21 落在 45-trit 可表示域内，整个式子退化成一条 SUB（模域回绕）。
 * 注意 ~0 ≠ -1——回绕在 3^45 的必然结果（e2e 非回绕锁定用例）。 */
static void tri_emit_bitnot(unsigned dst, unsigned aR)
{
    tri_load_imm_hw(TRI_B_AC, (((__int128)1 << 72) - 1) % TRI_P45);
    o(TRI_ENC_R(TRI_SUB_OP, TRI_SUB_FUNCT2, aR, TRI_B_AC, TRI_SUB_FUNCT, TRI_B_AC)); /* AC = TILDE - a */
    if (dst != TRI_B_AC) TRI_ADDI(dst, TRI_B_AC, 0);
}

/* 变量移位量：单步循环（参考 trirv_cc emit_shl_v/emit_shr_v）。
 * cnt 在 VA、判定在 VB，有符号右移每步 REM+DIV+修正；负移位量按 0 次处理。
 * 分支偏移静态：循环体形状固定，回跳偏移按 ind 现算。 */
static void tri_emit_shl_v(unsigned dst, unsigned aR, unsigned cR)
{
    unsigned cnt = TRI_B_VA, pred = TRI_B_VB;
    int loop_ind;
    TRI_ADDI(cnt, cR, 0);
    if (dst != aR) TRI_ADDI(dst, aR, 0);
    loop_ind = (int)ind;
    o(TRI_ENC_R(TRI_SLT_OP, TRI_SLT_FUNCT2, cnt, 0, TRI_SLT_FUNCT, pred)); /* pred = (0 < cnt) */
    o(TRI_ENC_S(TRI_OP_BRANCH, 4, 0, pred, 0)); /* BEQ → 循环尾（跳过 ADD/ADDI/JAL） */
    o(TRI_ENC_R(TRI_ADD_OP, TRI_ADD_FUNCT2, dst, dst, TRI_ADD_FUNCT, dst)); /* dst += dst */
    TRI_ADDI(cnt, cnt, -1);
    TRI_JAL(0, (loop_ind - (int)ind) / 4);
}

static void tri_emit_shr_v(unsigned dst, unsigned aR, unsigned cR, int uns)
{
    unsigned cnt = TRI_B_VA, pred = TRI_B_VB;
    int loop_ind;
    TRI_ADDI(cnt, cR, 0);
    if (dst != aR) TRI_ADDI(dst, aR, 0);
    loop_ind = (int)ind;
    o(TRI_ENC_R(TRI_SLT_OP, TRI_SLT_FUNCT2, cnt, 0, TRI_SLT_FUNCT, pred));
    o(TRI_ENC_S(TRI_OP_BRANCH, uns ? 5 : 10, 0, pred, 0)); /* BEQ → 循环尾 */
    TRI_ADDI(pred, 0, 2);                                  /* 每步 ÷2 */
    if (uns) {
        o(TRI_ENC_R(TRI_DIVU_OP, TRI_DIVU_FUNCT2, pred, dst, TRI_DIVU_FUNCT, dst));
    } else {
        o(TRI_ENC_R(TRI_REM_OP, TRI_REM_FUNCT2, pred, dst, TRI_REM_FUNCT, TRI_B_AC));
        o(TRI_ENC_R(TRI_DIV_OP, TRI_DIV_FUNCT2, pred, dst, TRI_DIV_FUNCT, dst));
        /* SLT ac, ac, zero：rs2 是第 3 参——曾误写 (AC,AC) 编成 (ac<ac)≡0，
           floor 修正永不触发（-65>>3→-8 暴露） */
        o(TRI_ENC_R(TRI_SLT_OP, TRI_SLT_FUNCT2, 0, TRI_B_AC, TRI_SLT_FUNCT, TRI_B_AC));
        o(TRI_ENC_S(TRI_OP_BRANCH, 3, 0, TRI_B_AC, 0)); /* 余数非负 → 跳过修正 */
        TRI_ADDI(TRI_B_AC, 0, 1);
        o(TRI_ENC_R(TRI_SUB_OP, TRI_SUB_FUNCT2, TRI_B_AC, dst, TRI_SUB_FUNCT, dst));
    }
    TRI_ADDI(cnt, cnt, -1);
    TRI_JAL(0, (loop_ind - (int)ind) / 4);
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

/* ---- TF45 编码（对齐 trirv_float.py TF45.encode：exp_trits=10, mant_trits=34,
 * bias=29524）---- raw = s·3^44 + E·3^34 + M，v = ±(M/3^33)×3^(E-29524)。
 * long double 路径（arm64 宿主 = 128-bit quad，113-bit 尾数）：全 double 值域
 * 覆盖（exp3 ∈ [-678, 646]）。精确性论证：M = round(mant×3^33) 需 54 bit
 * 有效位，113-bit 中间精度的绝对误差 ≈ 1.7e16×2^-113 ≈ 2e-18 ≪ 0.5 → 非平局
 * 情形精确。已知边界（记档）：平局（a×3^(33-e) 恰为 x.5，可构造）舍入方向
 * 可能翻转——powl 误差 2^-113 下无法区分；实际常量命中概率 ~0。
 * e 边界判定可靠性：相邻 3 倍间隔 ≫ powl 相对误差；a 为 double（53-bit），
 * e>33 时 3^e 非双精度可表 → a 距边界至少 2^-53 相对 → 判定绝对可靠。 ---- */
#define TRI_TF_P_MANT   ((__int128)3486784401LL * 4782969LL)      /* 3^34 */
#define TRI_TF_P_SIGN   (TRI_TF_P_MANT * 59049)                    /* 3^44 */
#define TRI_TF_MAX_EXP  59048L
#define TRI_TF_BIAS     29524L

ST_FUNC unsigned __int128 tri_tf45_raw(double v)
{
    if (isnan(v)) return (__int128)TRI_TF_MAX_EXP * TRI_TF_P_MANT + 1;
    if (isinf(v))
      return (v < 0 ? TRI_TF_P_SIGN : 0) + (__int128)TRI_TF_MAX_EXP * TRI_TF_P_MANT;
    if (v == 0.0)
      return signbit(v) ? TRI_TF_P_SIGN : 0;
    {
        int s = v < 0;
        long double la = fabsl((long double)v);
        long e = (long)floorl(logl(la) / logl(3.0L));
        long E;
        __int128 M;
        /* 边界修正（±保险：logl 估计误差应 ≤2，超限即错不悬挂） */
        {
            int guard = 0;
            while (la < powl(3.0L, (long double)e)) {
                if (++guard > 8) tcc_error("TriRV: float encode e-underflow (%.17g)", v);
                e--;
            }
            guard = 0;
            while (la >= powl(3.0L, (long double)(e + 1))) {
                if (++guard > 8) tcc_error("TriRV: float encode e-overflow (%.17g)", v);
                e++;
            }
        }
        /* mant = a / 3^e ∈ [1,3)（113-bit 精度）；M = round-half-up(mant×3^33)
         * ∈ [3^33, 3^34)，54 bit——long double 下精确（非平局） */
        {
            long double mant = la / powl(3.0L, (long double)e);
            long double mx = mant * powl(3.0L, 33.0L);
            M = (__int128)floorl(mx + 0.5L);
        }
        E = e + TRI_TF_BIAS;
        if (M >= TRI_TF_P_MANT) { M /= 3; E += 1; }
        if (E >= TRI_TF_MAX_EXP)
          return (s ? TRI_TF_P_SIGN : 0) + (__int128)TRI_TF_MAX_EXP * TRI_TF_P_MANT;
        if (E <= 0) return s ? TRI_TF_P_SIGN : 0;
        return (s ? TRI_TF_P_SIGN : 0) + E * TRI_TF_P_MANT + M;
    }
}

/* 浮点常量 → rodata 9 tryte（base-243），返回 elf 符号索引（LUI 补丁用） */
static int tri_tf45_rodata(double v)
{
    unsigned __int128 raw = tri_tf45_raw(v);
    int roff = rodata_section->data_offset;
    unsigned char *p = section_ptr_add(rodata_section, 9);
    int k;
    for (k = 0; k < 9; k++) { p[k] = (unsigned char)(raw % 243); raw /= 243; }
    return put_elf_sym(symtab_section, roff, 9, 0, 0, rodata_section->sh_num, NULL);
}

ST_FUNC void load(int r, SValue *sv)
{
    int fr = sv->r, v = fr & VT_VALMASK;
    /* fc 初值 = 跳转链头（VT_JMP 的链头在 c.i，tccgen vset_VT_JMP）。
       坑实录：曾不初始化——VT_JMP 物化 gsym(fc) 用到未初始化栈值
       （(a&&a<5) 取值探针 SIGTRAP；对齐上游 riscv64-gen.c `int fc = sv->c.i`）。 */
    int fc = (int)(int64_t)sv->c.i, base, funct, ok;
    unsigned rr = is_ireg(r) ? ireg(r) : freg(r);
    int bt = sv->type.t & VT_BTYPE;
    int align, size = type_size(&sv->type, &align);

    if (bt == VT_FLOAT || bt == VT_DOUBLE || bt == VT_LDOUBLE) {
        /* F 扩展（TF45）：LVAL → FLW fd,fc(base)；常量 → TF45 raw 编码进
           rodata（9 tryte base-243）后 LUI t0 + FLW（TriRV 无 FMOV-imm，
           上游 riscv64 同款常量池形态）。FLW/FSW 搬 raw 与档位无关。 */
        if (fr & VT_LVAL) {
            base = tri_addr_base(sv, 0, &fc);
            tri_check_off(fc);
            o(TRI_ENC_I(TRI_FLW_OP, fc, base, 1, rr));   /* FLW fd,fc(base) */
            return;
        }
        if (v == VT_CONST) {
            double d;
            int idx;
            if (bt == VT_FLOAT) d = sv->c.f;
            else if (bt == VT_DOUBLE) d = sv->c.d;
            else d = (double)sv->c.ld;
            idx = tri_tf45_rodata(d);
            tri_lui_load_idx(TRI_T0, idx, 0);
            o(TRI_ENC_I(TRI_FLW_OP, 0, TRI_T0, 1, rr));  /* FLW fd,0(t0) */
            return;
        }
        tcc_error("TriRV: float load class %d", v);
        return;
    }

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
        } else {
            /* 其余一律走 3×20-trit 平衡分段（tri_load_imm_hw）。
               坑实录 1：CValue.i 是 uint64_t（tcc.h CValue 联合），裸写
               `sv->c.i >= -(int64_t)2147483647` 触发 usual arithmetic
               conversions——负常量转巨大无符号数，条件恒 FALSE。比较必须
               显式 (int64_t) 转换。
               坑实录 2（D6）：旧 LUI+ADDI 两槽路径负 hi 回绕成 LUI 正数
               （差 k×3^20）、lo 借位只处理上界（lo<-1093 不归一）、
               |v|>1743392200 误报窗口溢出（int x=-1300 断言 / x=2000000007
               编译错探针暴露）——统一删除，负数 = 3^45+v 转正分段。 */
            unsigned __int128 x = (unsigned __int128)(int64_t)sv->c.i;
            if ((int64_t)sv->c.i < 0)
              x += TRI_P45;              /* 负数 → 3^45+v（raw 即位模式） */
            tri_load_imm_hw(rr, x);
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
        /* 归一：GE/LE/GT/UGT/UGE/ULE → SLT/SLTU + 可选取反。
           SLT rd,rs1,rs2 = (rs1<rs2)；swap 表示 rs1/rs2 互换。
           坑实录：旧表 GT(0,1)/GE(1,1)/LE(1,0) 把 GT 物化成 a>=b、GE 成
           a<=b、LE 成 a>b（3>3→1 / 3>=4→1 / 4<=3→1 探针暴露）。 */
        switch (op) {
        case TOK_LT:  swap = 0, notafter = 0; goto do_slt;
        case TOK_GT:  swap = 1, notafter = 0; goto do_slt;   /* a>b ⟺ SLT b,a */
        case TOK_GE:  swap = 0, notafter = 1; goto do_slt;   /* a>=b ⟺ ¬(a<b) */
        case TOK_LE:  swap = 1, notafter = 1; goto do_slt;   /* a<=b ⟺ ¬(b<a) */
        do_slt:
            o(TRI_ENC_R(TRI_SLT_OP, TRI_SLT_FUNCT2, swap ? a : b, swap ? b : a, TRI_SLT_FUNCT, rr)); /* SLT */
            if (notafter) TRI_SEQZ_NOT(rr);
            break;
        case TOK_ULT: swap = 0, notafter = 0; goto do_sltu;
        case TOK_UGT: swap = 1, notafter = 0; goto do_sltu;
        case TOK_UGE: swap = 0, notafter = 1; goto do_sltu;
        case TOK_ULE: swap = 1, notafter = 1; goto do_sltu;
        do_sltu:
            o(TRI_ENC_R(TRI_SLTU_OP, TRI_SLTU_FUNCT2, swap ? a : b, swap ? b : a, TRI_SLTU_FUNCT, rr)); /* SLTU */
            if (notafter) TRI_SEQZ_NOT(rr);
            break;
        case TOK_NE:
            /* 坑实录：此处曾裸写 (TRI_OP_OP, 0, b, a, 1, rr)——funct=1 是
               SLL 不是 SUB（gen_opil 主路径 77fe1128 修过，此处漏改）。
               一律走 tri-isa.h 生成宏（单一真相源）。 */
            o(TRI_ENC_R(TRI_SUB_OP, TRI_SUB_FUNCT2, b, a, TRI_SUB_FUNCT, rr)); /* SUB rr,a,b */
            TRI_SNEZ(rr);
            break;
        case TOK_EQ:
            o(TRI_ENC_R(TRI_SUB_OP, TRI_SUB_FUNCT2, b, a, TRI_SUB_FUNCT, rr));
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

    if (bt == VT_FLOAT || bt == VT_DOUBLE || bt == VT_LDOUBLE) {
        base = tri_addr_base(sv, 1, &fc);
        tri_check_off(fc);
        o(TRI_ENC_S(TRI_FSW_OP, fc, rr, base, 3));   /* FSW fs2,fc(base) */
        return;
    }
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
        {
            int pbt = type->t & VT_BTYPE;
            if (pbt == VT_FLOAT || pbt == VT_DOUBLE || pbt == VT_LDOUBLE)
              o(TRI_ENC_S(TRI_FSW_OP, param_addr, areg, TRI_S0, 3)); /* FSW faX,loc(s0) */
            else
              o(TRI_ENC_S(TRI_OP_STORE, param_addr, 10 + areg, TRI_S0, 2)); /* SW aX,loc(s0) */
        }
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
        if (tr == (unsigned)ireg(r)) {
            /* JALR 先写 rd 后读 rs1（双 VM 语义）：rd==rs1 跳到旧 pc+1 槽
               （fp()/gfp() 函数指针探针 exit=0≠7 暴露）。目标经 ra 装载
               时改道 t0。 */
            TRI_ADDI(TRI_T0, TRI_RA, 0);
            TRI_JALR(TRI_RA, TRI_T0);
        } else {
            TRI_JALR(tr, ireg(r));
        }
    }
}

ST_FUNC void gfunc_call(int nb_args)
{
    int i;
    if (nb_args > 8)
      tcc_error("TriRV spike: stack args (>%d)", 8);
    /* 实参依序装入 a0..a7：vrotb(i+1) 把深度 i 的 arg_{n-1-i} 转到栈顶，
     * 装入它自己的寄存器 a_{n-1-i}（对齐 riscv64-gen.c 的 vrotb(i+1) 配
     * info[n-1-i]）。坑实录：曾写 vrotb(ii+1)（ii=n-1-i）——弹出 arg_i
     * 却装进 a_{n-1-i}，实参倒装（sub(10,4)→250=y−x 探针暴露）。 */
    for (i = 0; i < nb_args; i++) {
        int ii = nb_args - 1 - i;                /* 本次装载 arg_ii */
        int abt;
        vrotb(i + 1);
        abt = vtop->type.t & VT_BTYPE;           /* 须在 vrotb 后读（顶=arg_ii） */
        if (abt == VT_FLOAT || abt == VT_DOUBLE || abt == VT_LDOUBLE) {
            /* 浮点实参 → fa_ii（gv 经 spill/reload 落位；TriRV 无 freg-freg
               move，寄存器不符时由 tccgen 经内存搬运） */
            gv(RC_F(ii));
        } else {
            gv(RC_R(ii));
            if (vtop->r != ii) {
                /* gv 未落到目标寄存器：显式 mv */
                TRI_ADDI(ireg(ii), ireg(vtop->r), 0);
                vtop->r = ii;
            }
        }
        vrott(i + 1);           /* 逆旋转必须与 vrotb(i+1) 配对（vrott(ii+1) 会逐轮搅乱栈） */
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
        /* fc 必须按 int64 读：曾写 (int) 截断——VT_LLONG 常量如 2^32+1
           截成 1 误入 ADDI 融合（错误值差 2^32）。 */
        long long fc = (int64_t)vtop->c.i;
        if (tri_const_fits(fc)) {
            int fuse = 0;
            long long im = fc;
            switch (op) {
            case '-': im = -fc; fuse = 1; break;
            case '+': fuse = 1; break;
            /* & | ^ 不再融合 ANDI/ORI/XORI：它们是逐 trit 语义（XORI 是
               mod-3 加），与 C 二进制语义不符——一律走下方方案 A 位游标。 */
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
                if (fuse == 1) TRI_ADDI(ireg(d), a, (int)im);
                else o(TRI_ENC_I(TRI_OP_OPIMM, im, a,
                                 fuse == 2 ? 6 : fuse == 3 ? 3 : 2, ireg(d)));
                --vtop;
                vtop[0].r = d;
                return;
            }
        }
    }
    /* 方案 A：C 位运算二进制语义（& | ^）。45-trit raw 值即位模式，72 位
       位游标逐位 TMIN/TMAX/异或组合（tri_emit_bitop3）。~a 由 tccgen 折叠成
       a^(-1)——在此拦截为 TILDE-a（tri_emit_bitnot）。 */
    if (op == '&' || op == '|' || op == '^') {
        int notmode = 0;
        if (op == '^') {
            if ((vtop->r & (VT_VALMASK | VT_LVAL | VT_SYM)) == VT_CONST
                && (int64_t)vtop->c.i == -1)
              notmode = 1;                       /* a ^ -1（右侧常量） */
            else if ((vtop[-1].r & (VT_VALMASK | VT_LVAL | VT_SYM)) == VT_CONST
                     && (int64_t)vtop[-1].c.i == -1)
              notmode = 2;                       /* -1 ^ a（左侧常量，gen_opic 可能换序） */
        }
        gv2(RC_INT, RC_INT);
        a = ireg(vtop[-1].r);
        b = ireg(vtop[0].r);
        vtop -= 2;
        d = get_reg(RC_INT);
        vtop++;
        vtop[0].r = d;
        if (notmode)
          tri_emit_bitnot(ireg(d), notmode == 1 ? a : b);
        else
          tri_emit_bitop3(ireg(d), a, b, op == '&' ? 0 : op == '|' ? 1 : 2);
        return;
    }
    /* C 移位 = 二进制移位（方案 A §5.1.1）：<< = ×2^k mod 3^45（k≤10 ADD 链，
       否则 MUL by 2^k）；>> 无符号 = DIVU（k≥72 → 0）；>> 有符号 = DIV 向零
       截断 + REM 负余数 floor 修正（-65>>3 应 -9 非 -8）。k<0 / k>4096
       编译期报错（NEG 门控）；k∈[46,71] 用 128 位 2^k（tri_pow2_mod）。
       坑实录：TriRV SLL/SRL/SRA 是三进制 ×3^k——直译是语义错误；旧块只拦
       [0,45]，之外静默落 R 型三进制移位（s<<i→90 暴露）。 */
    if ((op == TOK_SHL || op == TOK_SHR || op == TOK_SAR)
        && (vtop[0].r & (VT_VALMASK | VT_LVAL | VT_SYM)) == VT_CONST) {
        long long k = (int64_t)vtop[0].c.i;
        int uns = (op == TOK_SHR);
        if (k < 0 || k > 4096)
          tcc_error("TriRV: shift constant %lld outside [0,4096]", k);
        gv2(RC_INT, RC_INT);
        a = ireg(vtop[-1].r);
        b = ireg(vtop[0].r);
        vtop -= 2;
        d = get_reg(RC_INT);
        vtop++;
        vtop[0].r = d;
        d = ireg(d);
        if (op == TOK_SHL) {
            if (k == 0) { if (d != a) TRI_ADDI(d, a, 0); }
            else if (k <= 10) {                    /* 小 k：自加链（trirv_cc emit_shl_k） */
                if (d != a) TRI_ADDI(d, a, 0);
                while (k--)
                  o(TRI_ENC_R(TRI_ADD_OP, TRI_ADD_FUNCT2, d, d, TRI_ADD_FUNCT, d));
            } else {
                tri_load_imm_hw(TRI_B_PP, k <= 62 ? (unsigned __int128)(1LL << k) : tri_pow2_mod(k));
                o(TRI_ENC_R(TRI_MUL_OP, TRI_MUL_FUNCT2, TRI_B_PP, a, TRI_MUL_FUNCT, d));
            }
        } else if (uns) {
            if (k >= 72) TRI_ADDI(d, 0, 0);        /* raw < 3^45 < 2^72 → 0 */
            else if (k == 0) { if (d != a) TRI_ADDI(d, a, 0); }
            else {
                tri_load_imm_hw(TRI_B_PP, k <= 62 ? (unsigned __int128)(1LL << k) : tri_pow2_mod(k));
                o(TRI_ENC_R(TRI_DIVU_OP, TRI_DIVU_FUNCT2, TRI_B_PP, a, TRI_DIVU_FUNCT, d));
            }
        } else { /* SAR：floor 语义 */
            if (k >= 71) {                         /* |x| ≤ (3^45-1)/2 < 2^71 → 0/-1 */
                o(TRI_ENC_R(TRI_SLT_OP, TRI_SLT_FUNCT2, 0, a, TRI_SLT_FUNCT, d)); /* d = a<0 */
                o(TRI_ENC_R(TRI_SUB_OP, TRI_SUB_FUNCT2, d, 0, TRI_SUB_FUNCT, d)); /* d = -d */
            } else if (k == 0) { if (d != a) TRI_ADDI(d, a, 0); }
            else {
                tri_load_imm_hw(TRI_B_PP, k <= 62 ? (unsigned __int128)(1LL << k) : tri_pow2_mod(k));
                o(TRI_ENC_R(TRI_REM_OP, TRI_REM_FUNCT2, TRI_B_PP, a, TRI_REM_FUNCT, TRI_B_AC));
                o(TRI_ENC_R(TRI_DIV_OP, TRI_DIV_FUNCT2, TRI_B_PP, a, TRI_DIV_FUNCT, d));
                /* SLT ac, ac, zero（rs2 第 3 参；同 emit_shr_v 坑） */
                o(TRI_ENC_R(TRI_SLT_OP, TRI_SLT_FUNCT2, 0, TRI_B_AC, TRI_SLT_FUNCT, TRI_B_AC));
                o(TRI_ENC_S(TRI_OP_BRANCH, 2, 0, TRI_B_AC, 0)); /* 余数非负 → 无修正 */
                TRI_ADDI(TRI_B_AC, 0, 1);
                o(TRI_ENC_R(TRI_SUB_OP, TRI_SUB_FUNCT2, TRI_B_AC, d, TRI_SUB_FUNCT, d)); /* d -= 1 */
            }
        }
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
    /* 变量移位量：单步循环（方案 A §5.1.1；tri_emit_*_v）。旧路径静默发
       三进制 SLL/SRL/SRA（s<<i→90=10×3² 暴露）——方案 A 落地后产物中
       不应再出现 SLL/SRL/SRA。 */
    case TOK_SHL: tri_emit_shl_v(d, a, b); break;
    case TOK_SHR: tri_emit_shr_v(d, a, b, 1); break;
    case TOK_SAR: tri_emit_shr_v(d, a, b, 0); break;
    }
}

ST_FUNC void gen_opi(int op) { gen_opil(op); }
ST_FUNC void gen_opl(int op) { gen_opil(op); }

/* ---- __tint 三值逻辑（Kleene 系统；批次 C1） ----
   语义锚：TAND=min（假优先）、TOR=max（真优先）、TNOT=2-x（0<->2、1->1）。
   == != 走整数路径（位型全等：SUB + SEQZ/SNEZ）；^ / 序比较 / 算术
   全部门控报错——__tint 不是 int 的别名，是独立代数系统。 */
ST_FUNC void gen_opit(int op)
{
    if (op == '&' || op == '|') {
        int a, b, d;
        gv2(RC_INT, RC_INT);
        a = ireg(vtop[-1].r);
        b = ireg(vtop[0].r);
        vtop -= 2;
        d = get_reg(RC_INT);
        vtop++;
        vtop[0].r = d;
        /* R 型编码走 tri-isa.h 生成宏（TRI_ENC_R 第 3 参 rs2 第 4 参 rs1） */
        if (op == '&')
          o(TRI_ENC_R(TRI_TAND_OP, TRI_TAND_FUNCT2, b, a, TRI_TAND_FUNCT, ireg(d)));
        else
          o(TRI_ENC_R(TRI_TOR_OP, TRI_TOR_FUNCT2, b, a, TRI_TOR_FUNCT, ireg(d)));
        vtop[0].type.t |= VT_TINT;
        return;
    }
    if (op == TOK_EQ || op == TOK_NE) {
        gen_opil(op); /* 位型全等：整数路径复用（结果 int 0/1，CMP_OP 已定） */
        return;
    }
    if (op == '^')
      tcc_error("TriRV: __tint has no '^' (Kleene system; use ~ for not)");
    /* 序比较门控：显式 switch（记档坑——比较族 token 不连续，
       TOK_ULT..TOK_GT 区间还涵盖 EQ/NE/Nset/Nclear，禁区间检查） */
    switch (op) {
    case TOK_ULT: case TOK_UGE: case TOK_ULE: case TOK_UGT:
    case TOK_LT:  case TOK_GE:  case TOK_LE:  case TOK_GT:
      tcc_error("TriRV: __tint has no ordering ('<' etc: ternary values are unordered)");
    default: break;
    }
    tcc_error("TriRV: __tint has no arithmetic ops (+ - * / % << >>)");
}

ST_FUNC void gen_tnot(void)
{
    int a, d;
    gv(RC_INT);
    a = ireg(vtop[0].r);
    vtop--;
    d = get_reg(RC_INT);
    vtop++;
    vtop[0].r = d;
    o(TRI_ENC_R(TRI_TNOT_OP, TRI_TNOT_FUNCT2, 0, a, TRI_TNOT_FUNCT, ireg(d)));
}

/* ---- 浮点运算（F 扩展 TF45 档；独立 freg 堆 fa0-fa7） ---- */
ST_FUNC void gen_opf(int op)
{
    int a, b, d;
    if (op == TOK_NEG) {
        /* -x = 0 - x（TriRV 无 FNEG；FCVT.F 读整数 0 产 +0.0）。
           记档：-(-0.0) = +0.0（IEEE 要求 -0.0）——ISA 语义边界。 */
        int fr = freg(gv(RC_FLOAT));
        int zd;
        vtop--;
        zd = get_reg(RC_FLOAT);
        vtop++;
        vtop->r = zd;
        o(TRI_ENC_R(TRI_FCVT_F_OP, TRI_FCVT_F_FUNCT2, 0, 0, TRI_FCVT_F_FUNCT, zd)); /* FCVT.F zd,zero */
        o(TRI_ENC_R(TRI_FSUB_OP, TRI_FSUB_FUNCT2, fr, zd, TRI_FSUB_FUNCT, zd));  /* zd = 0-x */
        return;
    }
    /* 比较族用显式列举（坑实录：tcc token 顺序 GE=0x9d 在 LE/GT 之前——
       区间检查 op<=TOK_GE 会把 LE/GT 排除，"bad float op 159" 暴露） */
    switch (op) {
    case TOK_EQ: case TOK_NE: case TOK_LT:
    case TOK_LE: case TOK_GT: case TOK_GE: {
        /* 浮点比较立即物化为 int 0/1（FEQ/FLT/FLE 写整数 rd；非 VT_CMP——
           条件分支走通用 !=0 路径）。GT/GE 交换操作数（b?a 与 LE/GT 同构）。 */
        gv2(RC_FLOAT, RC_FLOAT);
        a = freg(vtop[-1].r);
        b = freg(vtop[0].r);
        vtop -= 2;
        d = get_reg(RC_INT);
        vtop++;
        vtop[0].r = d;
        d = ireg(d);
        switch (op) {
        case TOK_EQ: o(TRI_ENC_R(TRI_FEQ_OP, TRI_FEQ_FUNCT2, b, a, TRI_FEQ_FUNCT, d)); break;
        case TOK_NE: o(TRI_ENC_R(TRI_FEQ_OP, TRI_FEQ_FUNCT2, b, a, TRI_FEQ_FUNCT, d));
                     TRI_SEQZ_NOT(d); break;
        case TOK_LT: o(TRI_ENC_R(TRI_FLT_OP, TRI_FLT_FUNCT2, b, a, TRI_FLT_FUNCT, d)); break;
        case TOK_LE: o(TRI_ENC_R(TRI_FLE_OP, TRI_FLE_FUNCT2, b, a, TRI_FLE_FUNCT, d)); break;
        case TOK_GT: o(TRI_ENC_R(TRI_FLT_OP, TRI_FLT_FUNCT2, a, b, TRI_FLT_FUNCT, d)); break;
        case TOK_GE: o(TRI_ENC_R(TRI_FLE_OP, TRI_FLE_FUNCT2, a, b, TRI_FLE_FUNCT, d)); break;
        }
        return;
    }
    }
    gv2(RC_FLOAT, RC_FLOAT);
    a = freg(vtop[-1].r);
    b = freg(vtop[0].r);
    vtop -= 2;
    d = get_reg(RC_FLOAT);
    vtop++;
    vtop[0].r = d;
    d = freg(d);
    switch (op) {
    case '+': o(TRI_ENC_R(TRI_FADD_OP, TRI_FADD_FUNCT2, b, a, TRI_FADD_FUNCT, d)); break;
    case '-': o(TRI_ENC_R(TRI_FSUB_OP, TRI_FSUB_FUNCT2, b, a, TRI_FSUB_FUNCT, d)); break;
    case '*': o(TRI_ENC_R(TRI_FMUL_OP, TRI_FMUL_FUNCT2, b, a, TRI_FMUL_FUNCT, d)); break;
    case '/': o(TRI_ENC_R(TRI_FDIV_OP, TRI_FDIV_FUNCT2, b, a, TRI_FDIV_FUNCT, d)); break;
    default: tcc_error("TriRV: bad float op %d", op);
    }
}

ST_FUNC void gen_cvt_itof(int t)
{
    int rr = ireg(gv(RC_INT));
    int dr;
    vtop--;
    dr = get_reg(RC_FLOAT);
    vtop++;
    vtop->r = dr;
    /* FCVT.F fd,rs1：整数（45-trit 三进制读法）→ TF45（VM round 到 mant 精度） */
    o(TRI_ENC_R(TRI_FCVT_F_OP, TRI_FCVT_F_FUNCT2, 0, rr, TRI_FCVT_F_FUNCT, freg(dr)));
}

ST_FUNC void gen_cvt_ftoi(int t)
{
    int fr = freg(gv(RC_FLOAT));
    int dr;
    vtop--;
    dr = get_reg(RC_INT);
    vtop++;
    vtop->r = dr;
    /* FCVT.W.T rd,fs1：TF45 → 整数，向零截断（rtz）——C 的 (int) cast 语义
       （拍板 2026-11-21 新指令；FCVT.W 的 round 语义保留给数学转换）。 */
    o(TRI_ENC_R(TRI_FCVT_W_T_OP, TRI_FCVT_W_T_FUNCT2, 0, fr, TRI_FCVT_W_T_FUNCT, ireg(dr)));
}

/* R 值窄化（寄存器驻留值）：char → mod 243、short → mod 59049；有符号再
 * 5/10-trit 符号扩展（阈值 122/29525，3^k 偶容量恰平衡）。对齐硬件 SB/LB/LBU
 * 与 trirv_cc:2350。内存驻留值无需此路径——LBU/LH 读低 tryte 天然 mod。 */
ST_FUNC void gen_cvt_tri_narrow(int bt, int uns)
{
    int rr = ireg(gv(RC_INT));
    int d;
    int mod = (bt == VT_BYTE) ? 243 : 59049;
    int half = (bt == VT_BYTE) ? 122 : 29525;
    vtop--;
    d = get_reg(RC_INT);
    vtop++;
    vtop->r = d;
    d = ireg(d);
    if (mod == 243) TRI_ADDI(TRI_T0, 0, 243);
    else o(TRI_ENC_U(TRI_OP_LUI, 27, TRI_T0));   /* 27×3^7 = 59049 */
    o(TRI_ENC_R(TRI_REMU_OP, TRI_REMU_FUNCT2, TRI_T0, rr, TRI_REMU_FUNCT, d)); /* d = rr mod */
    if (!uns) {
        /* 符号扩展：d ≥ half → d -= mod（SLTU+SEQZ_NOT+MUL+SUB） */
        TRI_ADDI(TRI_T1, 0, half);
        o(TRI_ENC_R(TRI_SLTU_OP, TRI_SLTU_FUNCT2, TRI_T1, d, TRI_SLTU_FUNCT, TRI_T1)); /* t1 = d<half */
        TRI_SEQZ_NOT(TRI_T1);                    /* t1 = d>=half */
        o(TRI_ENC_R(TRI_MUL_OP, TRI_MUL_FUNCT2, TRI_T0, TRI_T1, TRI_MUL_FUNCT, TRI_T1));
        o(TRI_ENC_R(TRI_SUB_OP, TRI_SUB_FUNCT2, TRI_T1, d, TRI_SUB_FUNCT, d));  /* d -= flag×mod */
    }
}

ST_FUNC void gen_cvt_ftof(int dt)
{
    /* float/double/long double 统一 TF45 档（type_size 均 9 tryte）——
       TF20 档（funct2=0 算术）留待后续；C 的 float 精度损失不建模，记档。 */
}
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
