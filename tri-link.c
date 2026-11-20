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

static addr_t tri_get_section_base(TCCState *s1, Section *sec)
{
    addr_t pos = 100;                 /* rodata 起点（避开零页 argc/argv） */
    if (!TRI_LAYOUT_DONE) {
        TRI_SEC_RODATA = rodata_section;
        TRI_SEC_DATA   = data_section;
        TRI_SEC_BSS    = bss_section;
        TRI_SEC_COMMON = common_section;
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

/* 符号终址：sym->c = symtab 索引；st_value = 段内偏移（finalize 前） */
static addr_t tri_sym_addr(TCCState *s1, Sym *sym)
{
    int idx = sym->c;
    ElfW(Sym) *es;
    Section *sec;
    if (idx <= 0) {
        fprintf(stderr, "TriRV: symbol has no elf entry\n");
        exit(1);
    }
    es = (ElfW(Sym) *)s1->symtab->data + idx;
    if (es->st_shndx >= s1->nb_sections) {
        fprintf(stderr, "TriRV: bad sym section\n");
        exit(1);
    }
    sec = s1->sections[es->st_shndx];
    return tri_get_section_base(s1, sec) + es->st_value;
}

/* 数据段重定位回填：R_DATA_PTR → 目标地址（45-trit 字，9 tryte LE）
 * 写入段数据 r_offset 处。文本符号（函数指针）+20B 前移。 */
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
        ElfW(Sym) *es = (ElfW(Sym) *)s1->symtab->data + symidx;
        Section *tsec = s1->sections[es->st_shndx];
        addr_t base = tri_get_section_base(s1, tsec);
        addr_t val;
        int k;
        uint64_t v;
        if (tsec == text_section)
          base += 20;                        /* 文本符号随 _start 前移 */
        if (type != R_DATA_PTR && type != R_DATA_32)
          continue;
        val = base + es->st_value + (addr_t)relp->r_addend;
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
    int n_inst = 5 + text_bytes / 4;
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
    }

    /* 段清单：只写 rodata/data（bss/common 是零初始化，VM 内存天然为零，
       其 sec->data 为 NULL——f6 char 数组踩坑；地址仍经 tri_get_section_base
       分配给符号补丁） */
    {
        Section *cand[2] = { rodata_section, data_section };
        for (i = 0; i < 2; i++)
          if (cand[i] && cand[i]->data_offset > 0 && cand[i]->data)
            segs[nsegc++] = cand[i];
    }

    out = tcc_malloc(n_inst * 4);
    /* _start 槽 0-4 */
    out[0] = TRI_ENC_I(TRI_OP_LOAD, 0, 0, 2, 10);            /* LW a0,0(zero)  argc */
    out[1] = TRI_ENC_I(TRI_OP_LOAD, 9, 0, 2, 11);            /* LW a1,9(zero)  argv */
    out[2] = TRI_ENC_U(TRI_OP_JAL, main_addr / 4 + 3, 1); /* JAL ra,main：前置后 main 在槽 0+5 */
    out[3] = TRI_ENC_I(TRI_OP_OPIMM, 93, 0, 0, 17);          /* ADDI a7,zero,93  exit */
    out[4] = TRI_ENC_I(TRI_OP_SYSTEM, 0, 0, 0, 0);           /* ECALL */
    for (i = 0; i < text_bytes / 4; i++)
      out[5 + i] = read32le(text->data + i * 4);

    /* 补丁回填 */
    for (i = 0; i < NTP; i++) {
        struct tri_patch *p = &TRI_PATCHES[i];
        addr_t tgt = tri_sym_addr(s1, p->sym) + p->addend;
        if (p->kind == TRI_R_JAL_SLOT) {
            /* 文本内符号随 _start 前置 20 字节平移；补丁槽自身也平移 */
            tgt += 20;
            int site_slot = (p->text_off + 20) / 4;
            int delta = (int)(tgt / 4) - site_slot;
            out[p->text_off / 4 + 5] = TRI_ENC_U(TRI_OP_JAL, delta, p->rd);
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
                out[p->text_off / 4 + 5]     = TRI_ENC_U(TRI_OP_LUI, (unsigned)hi, p->rd);
                out[p->text_off / 4 + 5 + 1] = TRI_ENC_I(TRI_OP_OPIMM, (int)lo, p->rd, 0, p->rd);
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
