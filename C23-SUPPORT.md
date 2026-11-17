# TCC 的 C23 支持路线（参照 GCC 16.2.0 源码）

本文是 tcc 增加 C23 支持的工程文档：目标基线、参考实现索引、tcc 侧落点、可独立合并的任务点、验收方式与不做项。

- 参照实现：`/data/work/compiler/gcc/gcc-src-extracted/gcc-16.2.0`（GCC 16.2.0 源码）
- tcc 基线：本仓 `mob` 分支，`0.9.28rc`（`f830a169`）
- 本文所有行号均为写入时的实测值，代码变动后需重新定位（用文中给出的 grep 关键字）

---

## 0. 目标定义（先收缩范围，否则无法验收）

"对标 gcc" 必须先定义成可判定的集合。本机实测 GCC 16.2.0 的 `-std=c23` 结果：

| 检查 | GCC 16.2.0 | 结论 |
|---|---|---|
| `nullptr` | PASS | 在目标集内 |
| `_BitInt(8)` | PASS | 在目标集内 |
| `constexpr` **对象** | PASS | 在目标集内 |
| `constexpr` **函数** | **FAIL**（`'constexpr' requires an initialized data declaration`） | **排除**，gcc 自己也没做 |
| `auto x = 1.5;` 推导 | PASS | 在目标集内 |
| `typeof_unqual` | PASS | 在目标集内 |
| `[[nodiscard]]` 等标准属性 | PASS | 在目标集内 |
| `#embed "file"` | PASS（真实文件嵌入，实测输出 `41424344 size=4`） | 在目标集内 |

**基线定义：目标集 = GCC 16.2.0 `-std=c23` 可接受的语法集合，其中 `constexpr` 只做对象、不做函数。**

---

## 1. tcc 现状（实测，作为起点）

### 1.1 能力面

- C89/C99：完整（VLA、指定初始化、复合字面量、`inline`、`restrict`、`long long`、`//`）
- C11：实用子集可用——`_Static_assert`、`_Generic`、`_Alignas`、`_Thread_local`、匿名 struct/union、灵活数组成员；`_Atomic` + `stdatomic.h` + `__atomic_*` 是**真内建**（`tccgen.c:5543` 起的内建表），`atomic_fetch_add` 可链可跑
- 精度：`long double` 真 x87 80 位，`1.0L/3.0L` 与 gcc 逐位一致
- 不支持：`_Complex`（`error: _Complex is not yet supported`）
- C23：零散实现了几项（见 1.3），其余不支持

### 1.2 三个"静默骗人"的失败模式（比"不支持"危险）

| 现象 | 根因 | 实测证据 |
|---|---|---|
| `-std=c23` 静默无效 | `libtcc.c:883` 默认 `cversion=199901`，`libtcc.c:1996` 是**唯一**的映射（精确 `=c11`/`=gnu11` → `201112`），其它任何值既不报错也不警告；`-std=c23` 仍报 `__STDC_VERSION__=199901L` | 逐版本 `-std=` 实测 |
| `#elifdef B` 静默走错分支 | `tccpp.c:1868` 的 `#elif` 分支把 `def B` 当表达式求值，`def` 未定义 → 0 → 假 | `#ifdef A / #elifdef B / #else`，`-DB` 时 tcc 编出 `ccc`、gcc 编出 `bbb`，零警告 |
| `#embed` 静默产出空数组 | `tccpp.c:1976` 只发 `warning: ignoring unknown preprocessing directive`，初始化器为空 | 数组 `size=0`，程序能编能跑 |
| `auto x = 1.5;` 静默变 int | tcc 把 `auto` 当存储类，类型回落 int | `sizeof` 实测 tcc=4 / gcc=8 |

### 1.3 tcc 已有的 C23 碎片（说明它是 cherry-pick，不是按标准推进）

| 已实现 | 位置 |
|---|---|
| 复合语句末尾标签（C23 6.8.2p2） | `tccgen.c:7558` |
| `enum E : <type>` 固定底层类型 | `tccgen.c:4504`（`sizeof` 实测与 gcc 一致）|
| 单参 `_Static_assert(expr)` | `tcc-doc.texi:692` |
| `0b1010`、空初始化 `= {}`、typedef 重复声明 | 实测值/尺寸与 gcc 一致 |

---

## 2. GCC 侧参考实现索引（按特性指到文件/函数）

读 gcc 的顺序建议：**预处理/词法（libcpp）→ 标准开关（c-family/c-opts.cc）→ 关键字表（c-common.cc）→ 语法与语义（c/c-parser.cc）→ 类型系统（tree.h）**。

| C23 特性 | GCC 实现位置 | 语义要点 / 坑 |
|---|---|---|
| 标准档位开关 | `gcc/c-family/c-opts.cc:1966`、`:1982`（`flag_isoc23 = 1`） | c23/gnu23 的差异只在扩展开关，语法开关共用 `flag_isoc23` |
| libcpp 每标准默认 | `libcpp/init.cc:101`（`struct lang_flags` 位域）、`:177`（拷进 `CPP_OPTION`） | **按标准给预处理选项打默认值**，这是最值得照抄的结构 |
| `#elifdef` / `#elifndef` | `libcpp/directives.cc:162-163`（指令表项，门控 `STDC23`）、`:482`（选项检查） | 表驱动，加两项即可 |
| `#embed` | `libcpp/directives.cc:1378-1444`（`do_embed`，正文约 67 行） | 还需配套 `__has_embed`、`#embed` 的limit/prefix/suffix 参数（可后续） |
| `__VA_OPT__` | `libcpp/macro.cc:94` 起（`'##' cannot appear at either end of __VA_OPT__`、宏展开状态跟踪） | 与 `##` 占位、参数计数一起处理 |
| 数字分隔符 `1'000` | `libcpp/lex.cc:2345-2360`（相邻分隔符/结尾分隔符报错）、`:5558`、`:5665`（选项检查） | 开关字段 `cpplib.h:540 digit_separators`；注意 `'` 同时是字符字面量起始符，词法要按 pp-number 上下文区分 |
| `u8'c'` 字符字面量 | `libcpp/lex.cc`（字符字面量分支，按 `cpp_options` 的宽度/UTF-8 选项） | C11 有 `u8"…"`，C23 才允许 `u8'…'` |
| `nullptr` / `nullptr_t` | 关键字：`gcc/c-family/c-common.cc:395` 的 `c_common_reswords[]`（`{"nullptr", RID_NULLPTR, D_C23 \| D_CXX11}`）；语义：`gcc/c/c-parser.cc:632-860` 的 RID 处理；类型：`gcc/ginclude/stddef.h:465`（`typedef __typeof__(nullptr) nullptr_t;`） | `nullptr_t` 是实现定义类型（gcc 用 `__typeof__(nullptr)`），不要硬编码成 `void*` 的 typedef 了事 |
| `constexpr`（对象） | `gcc/c/c-parser.cc:733` 起（RID_CONSTEXPR 分支） | gcc 16 只支持"必须初始化的数据声明"；函数形式直接报同一条错 |
| `auto` 推导 | `gcc/c/c-parser.cc:820`、`:3836` 附近（RID_AUTO 出现在声明与声明说明符两条路径） | 关键难点：**先解析初始化器再定类型** |
| `typeof_unqual` | `gcc/c/c-parser.cc:637`、`:843`（RID_TYPEOF_UNQUAL）；关键字表 `c-common.cc` 的 `__typeof_unqual`、`__typeof_unqual__`（D_CONLY） | 语义 = `typeof` 后剥掉顶层限定符 |
| `[[…]]` 标准属性 | `gcc/c/c-parser.cc:1804`（`c_parser_std_attribute_specifier_sequence`）、`:3714` 等多处调用点 | 与 `__attribute__` 是**两套表**，但可共用属性处理与诊断 |
| `_BitInt(N)` | `gcc/tree.h:543`（`BITINT_TYPE` 并入整数类型族）、`c-parser.cc:632`、`:838` | 任意位宽整数，牵连类型提升、常量折叠、ABI |

---

## 3. tcc 侧落点（架构约束决定任务顺序）

### 3.1 三个必须先认的架构事实

1. **类型是 32 位位域编码**：`typedef struct CType { int t; Sym *ref; }`，`VT_BTYPE` 只占 4 位（`tcc.h:1044`），宽度由基本类型隐含；`VT_BITFIELD` 的宽度字段只有 12 位（`tcc.h:1079-1080`，上限 63 位）。
   → `_BitInt(N)`（N 可达 65535）**无法塞进现有编码**，必须新增宽度存储。这是唯一"改动量 × 后端数"的特性。
2. **单遍、无独立 AST**：以 vstack + 直接发射工作。
   → 语法糖无痛；但 `auto` 推导（需先看初始化器）要局部倒序，`constexpr` 函数需要常量求值器（`tccrun.c` 的执行器原则上可复用，但属独立工程）。
3. **关键字与指令都是表驱动**：
   - 关键字：`tccpp.c:64` 的 `tcc_keywords[]` 由 `tcctok.h` 的 `DEF(id, str)` 生成 → 加关键字是机械活
   - 指令：`tccpp.c` 的 `#` 分派（`TOK_IFDEF:1836`、`TOK_ELIF:1868`），未知指令落到 `:1976` 的 warning
   - 属性：`tccgen.c:3949` 已有完整的 `__attribute__((...))` 解析器可复用

### 3.2 落点对照表

| C23 特性 | tcc 落点 | 触及后端 |
|---|---|---|
| `-std=c23` 与特性门控 | `libtcc.c:1993`、`TCCState` | 否 |
| `#elifdef`/`#elifndef`、`#embed`、`__VA_OPT__` | `tccpp.c`（指令分派 + 宏展开） | 否 |
| `1'000`、`u8'c'` | `tccpp.c`（`parse_number()` 与字符字面量） | 否 |
| `nullptr`、`bool/true/false`、`static_assert`、`typeof_unqual` | `tcctok.h` + `tccgen.c` 表达式/声明解析 | 否 |
| `[[…]]` | `tccgen.c:3949` 属性解析器 + 新 token `[[`/`]]` 前瞻 | 仅 `noreturn` 类 |
| `auto` 推导 | `tccgen.c` 声明解析（局部倒序） | 否 |
| `constexpr` 对象 | `tccgen.c` + `.rodata` 只读属性 | 少量 |
| `_BitInt(N)` | `tcc.h` 类型模型 + `tccgen.c` 提升/折叠 + 全部 `*-gen.c` + ABI | **是，5 个后端** |

---

## 4. 任务点（每项可独立合并、独立验收）

验收统一格式：**正向用例**（`tests/tests2/NNN_*.c` + `.expect`）、**负向用例**（同机制，`.expect` 内放预期诊断文本）、**gcc 差分**（同一源码两编译器运行结果一致）。

### T0 —— 标准档位与"停止静默"（不新增任何语法，优先级最高）

| 编号 | 任务 | 落点 | 验收 |
|---|---|---|---|
| T0.1 | 引入标准档位：`cversion` 从整数扩为枚举（c89/c99/c11/c17/c23/gnuXX）+ `-std=` 严格校验，未知值报错 | `libtcc.c:883`（默认）、`:1996`（现有唯一映射）、`tcc.h` | `-std=c23` 不再静默；`-std=bogus` 报错 |
| T0.2 | C23 档位下 `__STDC_VERSION__` = `202311L` | 同上 | 预处理输出核对 |
| T0.3 | 未知预处理指令从 warning 改为 error（保留 `#pragma`/`#line`/已知 GCC 扩展白名单） | `tccpp.c:1976` | `#embed`（T1.5 之前）、`#foobar` 均报错 |
| T0.4 | `#elifdef`/`#elifndef` 未实现时必须报错，不得被当 `#elif def` | `tccpp.c:1868` | 负向用例：`#elifdef` 报错（T1.3 完成后转为正向用例）|
| T0.5 | C23 档位下 `auto x = <expr>;` 不推导就报错，禁止回落 int | `tccgen.c` 声明解析 | 负向用例：`sizeof(auto x = 1.5)` 不再等于 4 |
| T0.6 | 负向测试写进 `tests2`（机制已存在，见 §5） | `tests/tests2/` | `make test` 覆盖失败路径 |

> T0 的价值：把"能编但语义错"变成"明确拒绝"。**建议作为第一个独立 PR 提交**，它不依赖任何后续任务。

### T1 —— 词法/预处理（零后端影响）

| 编号 | 任务 | 落点 | 验收 |
|---|---|---|---|
| T1.1 | 数字分隔符 `1'000`（含相邻/结尾分隔符报错） | `tccpp.c` `parse_number()` | 与 gcc 差分；负向用例 `1''0`、`1'` |
| T1.2 | `u8'c'` 字符字面量（顺带核对 `u8"…"`） | `tccpp.c` 字面量 | 差分比对码点；负向用例多字节字符 |
| T1.3 | `#elifdef` / `#elifndef`（T0.4 的正向化） | `tccpp.c` 指令表 + `1868` | 三分支语义用例（`-DA`/`-DB`/都无）|
| T1.4 | `__VA_OPT__`（含与 `##` 组合、嵌套非法） | `tccpp.c` 宏展开 | 差分；负向用例 `__VA_OPT__` 嵌套 |
| T1.5 | `#embed`（文件读取 + 生成初始化器；`__has_embed` 至少返回 0） | `tccpp.c` | 真实二进制文件字节比对；缺文件报错 |

### T2 —— 语法糖（零/极少后端影响）

| 编号 | 任务 | 落点 | 验收 |
|---|---|---|---|
| T2.1 | `nullptr` + `nullptr_t`（类型按 gcc 做法 = `__typeof__(nullptr)`，不要硬编码 `void*`） | `tcctok.h`、`tccgen.c`、`include/stddef.h` | 赋值/比较/`_Generic` 用例；`sizeof(nullptr)` 与 gcc 一致 |
| T2.2 | `bool`/`true`/`false` 关键字（C23 档位下，与 `<stdbool.h>` 宏共存不冲突） | `tcctok.h`、`tccgen.c` | 与 gcc 差分；`#include <stdbool.h>` 后仍可编 |
| T2.3 | `static_assert` / `alignas` / `alignof` / `thread_local` 拼写别名 | `tcctok.h` + 现有 `_Static_assert`/`_Alignas` 实现 | 单参/双参两种形式 × 两种拼写 |
| T2.4 | `typeof_unqual`（`typeof` 后剥顶层限定符） | `tccgen.c` typeof 分支 | `const int` → 非 const 的用例（用赋值检查可写性）|
| T2.5 | `[[…]]` 属性：token 前瞻 + 复用 `tccgen.c:3949` 解析器 + 标准属性表（先支持 `nodiscard`/`deprecated`/`fallthrough`/`noreturn`/`maybe_unused`） | `tccpp.c`（`[[`）、`tccgen.c` | 与 `__attribute__` 等价性用例；`[[noreturn]]` 影响代码生成的行为用例 |
| T2.6 | 已实现项补测试（复合语句末尾标签、`enum : type`、`= {}`、`0b`、typedef 重定义） | 仅 `tests/tests2/` | 防回退 |

### T3 —— 语义级

| 编号 | 任务 | 落点 | 验收 |
|---|---|---|---|
| T3.1 | `auto` 类型推导（声明解析局部倒序：先解析初始化器求类型，再分配对象） | `tccgen.c` 声明路径 | `auto x = 1.5` → `sizeof`=8；数组/复合字面量/位域等边界 |
| T3.2 | `constexpr` 对象（必须初始化、只读、可作常量表达式） | `tccgen.c` + 常量求值 | `constexpr int N=3; int a[N];` 通过；未初始化报错（与 gcc 同文案）|
| T3.3 | 属性诊断语义（`nodiscard`/`deprecated` 告警、`fallthrough` 检查） | `tccgen.c` 属性处理 | 负向用例：丢返回值/漏 fallthrough 有告警 |

### T4 —— `_BitInt`（跨后端，最后做）

| 编号 | 任务 | 落点 | 验收 |
|---|---|---|---|
| T4.1 | 类型模型：`CType` 增加位宽存储（不能复用 `VT_BTYPE`/12 位位域），最小可用范围先定 `N ≤ 64` | `tcc.h`、`tccgen.c` | `sizeof(_BitInt(8))==1`、`(_BitInt(12))` 的范围截断 |
| T4.2 | 类型提升/转换/常量折叠 | `tccgen.c` | 与 gcc 差分的算术用例 |
| T4.3 | x86_64 后端 | `x86_64-gen.c` | 本机可实测 |
| T4.4 | i386/arm/arm64/riscv64 后端 | 各 `*-gen.c` | 交叉编译 + QEMU 运行 |
| T4.5 | ABI（`N > 64` 的传递/返回、对齐） | `tccelf.c` + 各后端 | 与 gcc 编译的对象互链测试 |

> `N > 64` 建议单独里程碑，不要和 T4.1–T4.4 混在一起。

---

## 5. 测试与差分验收（复用既有机制，不新建框架）

tcc 的 `tests2` 已经具备本文需要的一切：

| 机制 | 位置 | 用途 |
|---|---|---|
| 用例自动发现 | `tests/tests2/Makefile:6`（`??_*.c` / `???_*.c` wildcard） | 新用例直接放文件即可 |
| 执行规则 | `Makefile:187`：`tcc -run 或 -o+运行`，stderr/stdout 合并后 `diff` 对 `.expect`，且 `|| true` | **编译失败不会让 make 中断** → 天然支持负向用例 |
| `.expect` 由 gcc 生成 | `Makefile:213` 附近的 `%.expect` 规则 | 现成的**差分 oracle**：期望输出取自 gcc |
| 诊断类用例先例 | `tests/tests2/149_parameter_qualifier_errors.c` + `.expect`（`.expect` 里就写着 `warning: assignment of read-only location`） | 负向用例照此写：`.expect` 放预期诊断文本 |

建议新增两类约定：

1. **特性矩阵用例** `1xx_c23_<feature>.c`：一个特性一个文件，`.expect` 用 gcc 生成，`make test` 直接成为"与 gcc 行为一致"的回归门。
2. **档位用例**：同一文件在 `-std=c11`（应报错/走保守分支）与 `-std=c23`（应通过）两种 `ARGS` 下各跑一次，锁死"档位差异"。

交叉后端（T4.4）在本机无法直接跑：用 `make cross-<target>` 产物 + QEMU 运行，或用现有 `tests/tests2` 在这几个架构上的 CI 结果做旁证。

---

## 6. 明确不做（写清楚，避免范围蔓延）

| 不做 | 理由 |
|---|---|
| `constexpr` 函数 | gcc 16.2 自己也不支持（实测报错），"对标 gcc"不包含 |
| GNU 扩展长尾（`__builtin_shufflevector`、`__builtin_convertvector` 等） | 属于"对标 gcc 扩展集"，比 C23 本身更大，单独立项 |
| C23 库设施（`stdbit.h`、`<stdckdint.h>` 等的完整实现） | 主要是头文件工作，可由 `include/` 提供，不算编译器特性；但 `__builtin_*` 依赖要单列 |
| gcc 级 conformance 测试矩阵 | 需要独立工程；本文只做"真实代码覆盖 + 与 gcc 差分" |
| `_BitInt(N)` 的 `N > 64` | 实现需要新的类型表示 + 各后端 ABI 改造；tcc 在解析期显式报错（`the maximum width is 64`），gcc 支持更宽 |

### 6.1 五个排除项的评估（2026-11-17 实测）

结论先行：**五项都不建议做**，但理由各不相同；其中真正的"痛点"不在这些里面，而在 GNU 内建函数（见 §6.2）。

| 项 | 实测事实 | 结论 | 若要做的工作量 |
|---|---|---|---|
| `constexpr` **函数** | gcc 16.2 自己报 `'constexpr' requires an initialized data declaration`（`constexpr int sq(int){...}` 直接拒） | **不做**：对标 gcc 本就不含它，任何依赖它的代码在 gcc 16 下也编不过 | 需"编译期求值器"。tcc 有 `tcc_run`/`tcc_relocate`（libtcc 可在内存里执行），理论路径是"把调用在内存里跑出常量"，但要处理循环/递归上限/副作用禁令，属 M–L 独立工程 |
| GNU 扩展长尾 | 实测 15 个常见 `__builtin_*`，tcc **只有 6 个是真内建**（`expect`/`unreachable`/`alloca`/`types_compatible_p`/`constant_p`/`choose_expr`）；其余 9 个是**隐式声明**（有 warning、链接时 `unresolved reference`）：`popcount`/`clz`/`ctz`/`bswap32`/`bswap64`/`prefetch`/`object_size`/`add_overflow`/`assume_aligned` | **只做有界子集**：`popcount`/`clz`/`ctz`/`bswap*`（S，各后端几十行）、`prefetch`（零代码）、`object_size`/`assume_aligned`（返回保守值，S）；**`shufflevector`/`convertvector` 不做**——它们要求向量类型，tcc 完全没有，属 XL | 有界子集约 S×6；真正长尾无界 |
| C23 库设施 | glibc **2.43** 已带 `/usr/include/stdbit.h`，tcc 直接可用（`stdc_count_ones(7)`=3、`stdc_leading_zeros(1)`=31，与 gcc 逐值一致）；`stdckdint.h` 在 glibc 2.43 里**不存在**；tcc 自带 `stdatomic/stdalign/stdbool/stdnoreturn` | **基本不是编译器的事**：库设施由 libc 提供。可选补一份 `stdckdint.h` + `stdbit.h` 的兜底头（header-only，S），只在目标 libc 缺时生效 | S（纯头文件） |
| gcc 级 conformance 矩阵 | 本机 gcc 源码 `gcc/testsuite/gcc.dg/` 里 **c2x/c23 相关 285 个**、整个目录 **7520 个** | **不做**：这些用例大量依赖 gcc 内部头、`dg-*` 指令与 gcc 私有扩展，无法直接移植；自建等价矩阵是月级工程。现有"tests2 + gcc 生成 `.expect` 的差分"以约 1% 成本覆盖实用需求 | 月级（XL） |
| `_BitInt` `N > 64` | 位宽字段只有 6 位（bit20–25），存不下 >64；真要实现需：新的类型表示 + 多字运算（5 个后端）+ `>64` 位的参数/返回值 ABI + 大整数常量折叠 | **不做，改为解析期显式报错**（`'_BitInt(65)' is not supported: the maximum width is 64`）。与 gcc 的差异已在文案里写明 | XL（独立项目） |

### 6.2 真正值得做的：GNU 内建的有界子集

上面第 2 项里"9 个隐式声明"才是实际使用中最容易踩的坑——它们不会静默出错（tcc 有 implicit declaration warning，链接期 unresolved），但会让"看起来能编"的代码在链接时失败。按性价比排序，并记录落地情况：

| 内建 | 状态 | 实现与实测 |
|---|---|---|
| `__builtin_bswap16/32/64` | **已完成** | 纯前端实现（`tccgen.c` 的 `TOK_builtin_bswap*`）：`parse_builtin_params(0,"e")` 取值 → `gen_cast` 到无符号容器 → `gv_dup()` 复制 → 逐字节 `& mask` + 移位 + `|` → 再 `gen_cast` 回窄类型。零后端改动。与 gcc 16.2 差分逐值一致（含 `sizeof` 为 2/4/8、位域/比较/窄化实参、`-1`、嵌套调用）；实参只求值一次 |
| `__builtin_prefetch` | **已完成** | 求值并丢弃 1–3 个实参，结果类型 `void`。与 gcc 对齐：0 个实参报 `too few arguments`（gcc 也拒），多给实参 gcc 与 tcc 都接受 |
| `__builtin_object_size` / `__builtin_assume_aligned` | 待做 | 分析类，返回保守值即可（`object_size` 返回 `(size_t)-1` 一类，`assume_aligned` 原样返回指针），S |
| `__builtin_popcount/clz/ctz` | 待做 | 通用"移位+掩码"或查表实现即可前端化，S–M |
| `__builtin_add_overflow` 系列 | 待做 | 需要进位/溢出标志，属后端工作，M |

两个已落地项的**代价**（有意接受，不是缺陷）：bswap 走前端掩码/移位/或，x86_64 上 `unsigned f32(unsigned x){return __builtin_bswap32(x);}` 生成约 19 条指令，而 gcc 后端直接发 1 条 `bswap`。tcc 的定位是"编译快、代码朴素"，且加后端快速路径需要给 5 个后端各加一个单目运算派发点（`gen_opi`/`gen_opl` 目前没有单目入口），已单列为后续项。行为正确性不受影响。

明确不做：`__builtin_shufflevector`、`__builtin_convertvector`（要求向量类型，tcc 无）。


---

## 7. 风险

1. **把 warning 改 error 会打断现有用户构建** → T0.3/T0.4 需要提供逃逸阀（沿用 `-w`/新增按类关闭），并在 `Changelog` 里显式声明破坏性变更。
2. **`_BitInt` 的范围失控**：先把范围钉在"已有 5 个后端 + `N ≤ 64`"，`N > 64` 单列。
3. **单遍架构对 `auto`/`constexpr` 的阻力被低估**：T3.1/T3.2 应先做最小用例（函数内 `auto`、整型 `constexpr`），再扩到复杂场景。
4. **表驱动处的"前缀匹配"陷阱**：`#elifdef` 这类问题根源是"用前缀匹配未知词"，T0.4 之后应加一条总规则——**未知指令/未知关键字一律报错，禁止静默降级**，否则还会出现下一个 `#elifdef`。

---

## 8. 建议执行顺序（最小切片）

1. **T0 全部**（一个 PR）：诚实化 + 负向测试基建。不动任何语法，风险最低，立即消除"错编还绿"。
2. **T1 全部**（一个 PR）：预处理/词法，零后端影响。
3. **T2.1–T2.4**（语法糖，零后端）→ **T2.5–T2.6**。
4. **T3.1**（`auto`）→ **T3.2**（`constexpr` 对象）→ **T3.3**（属性诊断）。
5. **T4.1–T4.3**（`_BitInt` 单后端）→ **T4.4**（其余后端）→ **T4.5**（ABI）。

T0+T1+T2 完成后，实际效果是：现代头文件与 `nullptr`/`[[attr]]`/`bool`/`static_assert`/`typeof_unqual`/数字分隔符 风格的真实代码基本可编，且**任何编不过的情况都会明确报错**——这比"标准条款覆盖率"更接近使用者的收益。
