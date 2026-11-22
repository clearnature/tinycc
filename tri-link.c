/* tri-link.c —— TriRV 目标产物：TRV1 直写（绕过 ELF/tccelf）。
 *
 * TRV1 格式（trirv 仓库 write_trv/load_trv 同款）：
 *   "TRV1" + u32 version=1 + u32 n_inst + u32 n_seg
 *   + n_inst × u32（20-trit 指令，3^20 < 2^32）
 *   + n_seg × (u32 addr + u32 len + bytes)
 * 段布局（对齐 trirv-unified-memory-abi.md）：rodata@100 起，data 接后，
 * bss/common 只记账不写（VM 内存零初始化）；栈 4096+ 向上，零页 argc@0/argv@9/envp@18。
 * 入口：writer 前置 5 槽 _start（argc/argv 装载 → JAL main → exit(a0)）。
 * 槽 i 的 tryte 地址 = 4*i；JAL 立即数 = 槽偏移。
 *
 * 约定：TCCState 形参名必须是 s1（tcc.h 的 TCC_STATE_VAR 宏按 `s1->成员` 改写
 * text_section/rodata_section 等成员名）；symtab/sections/nb_sections 是普通成员。
 * 错误处理：fprintf+exit（tcc_error 宏在此上下文不可用）。
 */
#ifdef TARGET_DEFS_ONLY

/* tccelf.c 编译期所需的最小 ELF 常量（运行期不走 ELF，值仅保编译） */
#define EM_TCC_TARGET EM_RISCV
#define R_DATA_32  R_RISCV_32
#define R_DATA_PTR R_RISCV_64
#define R_JMP_SLOT R_RISCV_JUMP_SLOT
#define R_GLOB_DAT R_RISCV_64
#define R_COPY     R_RISCV_COPY
#define R_RELATIVE R_RISCV_RELATIVE
#define R_NUM      R_RISCV_NUM
#define ELF_START_ADDR 0x00010000
#define ELF_PAGE_SIZE  0x1000
#define PCRELATIVE_DLLPLT 0
#define RELOCATE_DLLPLT 0

#else /* !TARGET_DEFS_ONLY */

#include "tcc.h"
#include "tri-isa.h"

/* 补丁表类型在 tri-isa.h；表本体由 tri-gen.c 定义（ONE_SOURCE 下单 TU） */
ST_FUNC int tri_output_file(TCCState *s1, const char *filename);

static Section *TRI_SEC_DATA, *TRI_SEC_RODATA, *TRI_SEC_BSS, *TRI_SEC_COMMON;
static int TRI_LAYOUT_DONE;

/* 前置槽总数：_start 5 + __tri_syscall 9 = 14 槽（56 tryte）。
 * __tri_syscall 是链接器内建的 syscall 对接面（musl arch 层 syscall_arch.h
 * 的 TriRV 版——上游 riscv64 用内联汇编 ecall，TriRV 无内联汇编）：
 *   n→a7(x17)，a1..a6 整体下移一档到 a0..a5，ECALL（返回值写回 a0），
 *   JALR zero,ra,0 返回。C 侧原型：
 *   long __tri_syscall(long n, long a0, long a1, long a2, long a3, long a4, long a5);
 * 改槽数必须同步：sh_addr 基址 / n_inst / JAL 偏移 / 补丁 site / blob 槽位。 */
#define TRI_START_SLOTS  14
#define TRI_TEXT_BASE    (TRI_START_SLOTS * 4)   /* 56 */
#define TRI_SYSCALL_SLOT 5                        /* blob 首槽（_start 之后） */

static addr_t tri_get_section_base(TCCState *s1, Section *sec)
{
    addr_t pos = 100;                 /* rodata 起点（避开零页 argc/argv） */
    if (!TRI_LAYOUT_DONE) {
        TRI_SEC_RODATA = rodata_section;
        TRI_SEC_DATA   = data_section;
        TRI_SEC_BSS    = bss_section;
        TRI_SEC_COMMON = common_section;
        /* text 基址 = TRI_START_SLOTS 槽前置（TRI_TEXT_BASE 字节）——符号终址
           一次到位。坑实录：曾只给 rodata/data/bss/common 赋 sh_addr，text 恒 0，
           数据回填与 JAL 补丁各自散落 +20，TRI_R_LUI_ABS 分支漏加
           （&f 探针 exit=0，f 终址应 20）。 */
        if (text_section) text_section->sh_addr = TRI_TEXT_BASE;
        if (TRI_SEC_RODATA) { TRI_SEC_RODATA->sh_addr = pos; pos += TRI_SEC_RODATA->data_offset; }
        pos = (pos + 8) / 9 * 9;
        if (TRI_SEC_DATA)   { TRI_SEC_DATA->sh_addr   = pos; pos += TRI_SEC_DATA->data_offset; }
        pos = (pos + 8) / 9 * 9;
        if (TRI_SEC_BSS)    { TRI_SEC_BSS->sh_addr    = pos; pos += TRI_SEC_BSS->data_offset; }
        pos = (pos + 8) / 9 * 9;
        if (TRI_SEC_COMMON) { TRI_SEC_COMMON->sh_addr = pos; pos += TRI_SEC_COMMON->data_offset; }
        TRI_LAYOUT_DONE = 1;
    }
    return sec->sh_addr;
}

/* 符号节界与解析校验（UNDEF→weak 置 0 / 否则 unresolved 报错；
   COMMON 干净报错；shndx 越界拒绝——曾对 UNDEF 取 sections[0]=NULL
   解引用 SIGSEGV）。返回符号终址；弱未解析返回 0。 */
static addr_t tri_sym_final(TCCState *s1, int symidx, int for_data)
{
    ElfW(Sym) *es;
    Section *sec;
    if (symidx <= 0 || (unsigned)symidx >= s1->symtab->data_offset / sizeof(ElfW(Sym))) {
        fprintf(stderr, "TriRV: symbol #%d has no elf entry\n", symidx);
        exit(1);
    }
    es = (ElfW(Sym) *)s1->symtab->data + symidx;
    if (es->st_shndx == SHN_UNDEF) {
        if (ELFW(ST_BIND)(es->st_info) == STB_WEAK)
          return 0;
        /* 链接器内建符号：__tri_syscall（TRI_SYSCALL_SLOT 前置块，见上方注释）。
           绝对地址 = text 基址 + 首槽偏移；三槽语义 = n→a7/参数下移/ECALL。 */
        if (!strcmp((char *)s1->symtab->data + es->st_name, "__tri_syscall"))
          return TRI_SYSCALL_SLOT * 4;   /* 前置区绝对地址（非 text 相对）：槽 5 → 20 */
        fprintf(stderr, "TriRV: unresolved reference to '%s'%s\n",
                (char *)s1->symtab->data + es->st_name,
                for_data ? " (data)" : "");
        exit(1);
    }
    if (es->st_shndx == SHN_COMMON) {
        fprintf(stderr, "TriRV: common symbol '%s' unsupported (avoid -fcommon)\n",
                (char *)s1->symtab->data + es->st_name);
        exit(1);
    }
    if (es->st_shndx >= s1->nb_sections) {
        fprintf(stderr, "TriRV: bad sym section #%d\n", es->st_shndx);
        exit(1);
    }
    sec = s1->sections[es->st_shndx];
    return tri_get_section_base(s1, sec) + es->st_value;
}

/* 补丁符号解析：优先 elf_idx（补丁创建时快照，Sym 尚存活、索引精确），
   未定义过的按名查 symtab（extern 声明在先、定义在后续 TU——声明 Sym 的
   c 恒 0，跨 TU 解析靠名字）。均落空 → unresolved（对齐链接器语义）。 */
static addr_t tri_patch_symbol(TCCState *s1, const char *name, int elf_idx)
{
    int idx = (elf_idx > 0) ? elf_idx
            : (name ? find_elf_sym(s1->symtab, name) : 0);
    if (idx <= 0) {
        /* 链接器内建符号白名单（tri_sym_final 同款）：__tri_syscall 的
           extern 声明可能被 symtab 剪枝（idx<=0 走到这里）——按名直解。 */
        if (name && !strcmp(name, "__tri_syscall"))
          return TRI_SYSCALL_SLOT * 4;   /* 前置区绝对地址（非 text 相对）：槽 5 → 20 */
        fprintf(stderr, "TriRV: unresolved reference%s%s\n",
                name ? " to '" : "", name ? name : "");
        exit(1);
    }
    return tri_sym_final(s1, idx, 0);
}

/* 数据段重定位回填：R_DATA_PTR → 目标地址（45-trit 字，9 tryte LE）
 * 写入段数据 r_offset 处。文本符号地址经 text sh_addr=20 一次到位。 */
static Section *tri_find_section(TCCState *s1, const char *name)
{
    int i;
    for (i = 0; i < s1->nb_sections; i++)
      if (s1->sections[i] && !strcmp(s1->sections[i]->name, name))
        return s1->sections[i];
    return NULL;
}

static void tri_apply_data_relocs(TCCState *s1, Section *sec)
{
    char relname[64];
    Section *rel;
    int i, n;
    snprintf(relname, sizeof relname, ".rela%s", sec->name);
    rel = tri_find_section(s1, relname);
    if (!rel || rel->data_offset == 0)
      return;
    n = rel->data_offset / (int)sizeof(ElfW_Rel);
    for (i = 0; i < n; i++) {
        ElfW_Rel *relp = (ElfW_Rel *)rel->data + i;
        int symidx = ELFW(R_SYM)(relp->r_info);
        int type = ELFW(R_TYPE)(relp->r_info);
        addr_t val;
        int k;
        uint64_t v;
        if (type != R_DATA_PTR && type != R_DATA_32)
          continue;
        /* UNDEF→weak 置 0 / 否则 unresolved 报错（曾对 UNDEF 取
           sections[0]=NULL 解引用 SIGSEGV，extern 探针暴露） */
        val = tri_sym_final(s1, symidx, 1) + (addr_t)relp->r_addend;
        /* 45-trit 值按 base-243 小端写 9 tryte */
        v = (uint64_t)val;
        for (k = 0; k < 9; k++) {
            unsigned t = (unsigned)(v % 243);
            v /= 243;
            if (relp->r_offset + k < sec->data_offset)
              sec->data[relp->r_offset + k] = (unsigned char)t;
        }
    }
}

static void tri_write_le32(FILE *f, uint32_t v)
{
    unsigned char b[4] = { v & 0xff, (v >> 8) & 0xff, (v >> 16) & 0xff, (v >> 24) & 0xff };
    fwrite(b, 1, 4, f);
}

ST_FUNC int tri_output_file(TCCState *s1, const char *filename)
{
    Section *text = text_section;
    int text_bytes = text->data_offset;
    int n_inst = TRI_START_SLOTS + text_bytes / 4;
    int n_seg = 0;
    addr_t main_addr;
    uint32_t *out;
    FILE *f;
    int i;
    Section *segs[4]; int nsegc = 0;

    if (text_bytes & 3) {
        fprintf(stderr, "TriRV: text not 4-aligned\n");
        exit(1);
    }

    /* 入口符号 main（必须存在） */
    {
        int idx = find_elf_sym(s1->symtab, "main");
        ElfW(Sym) *es;
        if (!idx) {
            fprintf(stderr, "TriRV: no 'main' symbol\n");
            exit(1);
        }
        es = (ElfW(Sym) *)s1->symtab->data + idx;
        main_addr = tri_get_section_base(s1, s1->sections[es->st_shndx]) + es->st_value;
        /* _start JAL 目标必须用 text 内相对槽（st_value）：tri_sym_addr 返回
           绝对地址（含 text 基址 20），直接 /4 会把前置 5 槽再补一次——
           JAL 跳过序言、ra 未保存、ret 到 0、程序重启（位游标探针暴露）。 */
        main_addr = es->st_value;
    }

    /* 段清单：只写 rodata/data（bss/common 是零初始化，VM 内存天然为零，
       其 sec->data 为 NULL——f6 char 数组踩坑；地址仍经 tri_get_section_base
       分配给符号补丁）。其余 ALLOC 节诚实报错（曾静默丢弃：
       __attribute__((section(".foo"))) 全局直接消失、无任何警告）。 */
    {
        Section *cand[2] = { rodata_section, data_section };
        for (i = 0; i < 2; i++)
          if (cand[i] && cand[i]->data_offset > 0 && cand[i]->data)
            segs[nsegc++] = cand[i];
        for (i = 0; i < s1->nb_sections; i++) {
            Section *sec = s1->sections[i];
            if (!sec || sec == text_section || sec == cand[0] ||
                sec == cand[1] || sec == bss_section || sec == common_section)
              continue;
            /* .eh_frame 是 tcc 自生成元数据：TRV1 无展开器，历来丢弃（无害）；
               其余用户 ALLOC 节（如 __attribute__((section))）诚实报错 */
            if ((sec->sh_flags & SHF_ALLOC) && sec->sh_type != SHT_NOBITS &&
                sec->data_offset > 0 && strncmp(sec->name, ".eh_", 4) != 0) {
                fprintf(stderr, "TriRV: unsupported alloc section '%s'\n", sec->name);
                exit(1);
            }
        }
    }

    out = tcc_malloc(n_inst * 4);
    /* _start 槽 0-4 */
    out[0] = TRI_ENC_I(TRI_OP_LOAD, 0, 0, 2, 10);            /* LW a0,0(zero)  argc */
    out[1] = TRI_ENC_I(TRI_OP_LOAD, 9, 0, 2, 11);            /* LW a1,9(zero)  argv */
    out[2] = TRI_ENC_U(TRI_OP_JAL, main_addr / 4 + TRI_START_SLOTS - 2, 1); /* JAL ra,main：前置后 main 在槽 TRI_START_SLOTS */
    out[3] = TRI_ENC_I(TRI_OP_OPIMM, 93, 0, 0, 17);          /* ADDI a7,zero,93  exit */
    out[4] = TRI_ENC_I(TRI_OP_SYSTEM, 0, 0, 0, 0);           /* ECALL */
    /* __tri_syscall 槽 5-13（TRI_SYSCALL_SLOT 起；见文件头注释的约定） */
    out[5]  = TRI_ENC_I(TRI_OP_OPIMM, 0, 10, 0, 17);         /* ADDI a7,a0,0   n→号 */
    out[6]  = TRI_ENC_I(TRI_OP_OPIMM, 0, 11, 0, 10);         /* ADDI a0,a1,0   参数下移 */
    out[7]  = TRI_ENC_I(TRI_OP_OPIMM, 0, 12, 0, 11);         /* ADDI a1,a2,0 */
    out[8]  = TRI_ENC_I(TRI_OP_OPIMM, 0, 13, 0, 12);         /* ADDI a2,a3,0 */
    out[9]  = TRI_ENC_I(TRI_OP_OPIMM, 0, 14, 0, 13);         /* ADDI a3,a4,0 */
    out[10] = TRI_ENC_I(TRI_OP_OPIMM, 0, 15, 0, 14);         /* ADDI a4,a5,0 */
    out[11] = TRI_ENC_I(TRI_OP_OPIMM, 0, 16, 0, 15);         /* ADDI a5,a6,0 */
    out[12] = TRI_ENC_I(TRI_OP_SYSTEM, 0, 0, 0, 0);          /* ECALL */
    out[13] = TRI_ENC_I(TRI_OP_JALR, 0, 1, 0, 0);            /* JALR zero,ra,0 */
    for (i = 0; i < text_bytes / 4; i++)
      out[TRI_START_SLOTS + i] = read32le(text->data + i * 4);

    /* 补丁回填 */
    for (i = 0; i < NTP; i++) {
        struct tri_patch *p = &TRI_PATCHES[i];
        /* 符号终址含 text 基址 TRI_TEXT_BASE（tri_get_section_base 已赋 sh_addr） */
        addr_t tgt = tri_patch_symbol(s1, p->name, p->elf_idx) + p->addend;
        if (p->kind == TRI_R_JAL_SLOT) {
            /* 补丁槽自身随前置块前移（site 在 text 内 text_off 处） */
            int site_slot = (p->text_off + TRI_TEXT_BASE) / 4;
            int delta = (int)(tgt / 4) - site_slot;
            out[p->text_off / 4 + TRI_START_SLOTS] = TRI_ENC_U(TRI_OP_JAL, delta, p->rd);
            continue;
        }
        if (p->kind == TRI_R_LUI_ABS) {
            if (tgt >= (1 << 20)) {
                fprintf(stderr, "TriRV: symbol address %lld beyond VM memory\n", (long long)tgt);
                exit(1);
            }
            {
                long long hi = tgt / 2187, lo = tgt % 2187;
                if (lo > TRI_IMM7_MAX) { lo -= 2187; hi += 1; }
                out[p->text_off / 4 + TRI_START_SLOTS]     = TRI_ENC_U(TRI_OP_LUI, (unsigned)hi, p->rd);
                out[p->text_off / 4 + TRI_START_SLOTS + 1] = TRI_ENC_I(TRI_OP_OPIMM, (int)lo, p->rd, 0, p->rd);
            }
        }
    }

    f = fopen(filename, "wb");
    if (!f) {
        fprintf(stderr, "TriRV: cannot write %s\n", filename);
        return -1;
    }
    fwrite("TRV1", 1, 4, f);
    tri_write_le32(f, 1);
    tri_write_le32(f, n_inst);
    n_seg = nsegc;
    tri_write_le32(f, n_seg);
    for (i = 0; i < n_inst; i++) tri_write_le32(f, out[i]);
    for (i = 0; i < nsegc; i++) {
        Section *sec = segs[i];
        tri_apply_data_relocs(s1, sec);
        tri_write_le32(f, (uint32_t)sec->sh_addr);
        tri_write_le32(f, (uint32_t)sec->data_offset);
        fwrite(sec->data, 1, sec->data_offset, f);
    }
    fclose(f);
    tcc_free(out);
    return 0;
}

/* ---- ELF 时代接口桩（spike 不产生重定位；链接期不需要 GOT/PLT） ---- */
ST_FUNC void greloc(Section *sec, Sym *sym, unsigned long offset, int type)
{
    (void)sec; (void)sym; (void)offset; (void)type;
    fprintf(stderr, "TriRV: no ELF relocations (greloc)\n");
    exit(1);
}
ST_FUNC int code_reloc(int reloc_type) { return 1; }
ST_FUNC int gotplt_entry_type(int reloc_type) { return NO_GOTPLT_ENTRY; }
ST_FUNC unsigned create_plt_entry(TCCState *s1, unsigned got_offset, struct sym_attr *attr)
{
    (void)s1; (void)got_offset; (void)attr;
    fprintf(stderr, "TriRV: no PLT\n");
    exit(1);
}
ST_FUNC void relocate_plt(TCCState *s1) { (void)s1; }
ST_FUNC void relocate(TCCState *s1, ElfW_Rel *rel, int type, unsigned char *ptr,
                      addr_t addr, addr_t val)
{
    (void)s1; (void)rel; (void)type; (void)ptr; (void)addr; (void)val;
    fprintf(stderr, "TriRV: no ELF relocations\n");
    exit(1);
}

#endif /* !TARGET_DEFS_ONLY */
