# TCC C23 任务清单（可执行版）

配套文档：`C23-SUPPORT.md`（路线与参照索引）。本文只回答**怎么排任务、每条怎么验收**。
分支：`dev-c23`。参照实现：`/data/work/compiler/gcc/gcc-src-extracted/gcc-16.2.0`。

行号为 2026-11-17 在 `dev-c23@d5cd95f2` 上实测；改动后请按文中 grep 关键字重新定位。

---

## 0. 拓扑扫描结论（决定任务落点的事实）

| 事实 | 证据 | 对任务的影响 |
|---|---|---|
| `cversion` 只有 4 处消费者 | `libtcc.c:883`（默认 199901）、`:1994-1997`（`-std=`）、`tccpp.c:3622`（`__STDC_VERSION__`）、`tccdbg.c:1202`（DWARF 语言码）、`tcc.h:749`（字段） | 引入标准档位是**低风险**改动：新增枚举+判断函数，不动现有 4 处语义 |
| `-std=` 选项已注册，只是不校验 | `libtcc.c:1567`（`TCC_OPTION_std`）、`:1649`（选项表）、`:1994`（case） | T0.1 只改 case 体，不碰选项表 |
| 未知指令只有 warning | `tccpp.c:1976` | T0.3 一行改 `tcc_error` + 白名单 |
| **跳过态不校验指令名** | `tccpp.c:927-946` 的 `start_of_line` 分支只认 `TOK_ELSE`/`TOK_ELIF`/`TOK_ENDIF`/`TOK_IF*`/`TOK_ERROR`/`TOK_WARNING`，其它整行当文本丢掉 | **新发现**：`#elifdef` 在 `#ifdef` 为假的块里**零诊断**（实测两个分支都没编出来，符号为空）→ 必须单列 T0.4 |
| 关键字是指令表驱动 | `tcctok.h` 的 `DEF(id, str)` → `tccpp.c:64` 拼成关键字串 | 加关键字是机械活，但要注意关键字与标识符的冲突面 |
| 测试编号用到 153，共 145 例 | `tests/tests2/`，`Makefile:6` 用 wildcard 自动发现 | 新用例从 **154** 起编号 |
| `.expect` 由 gcc 自动生成；规则容错非零退出 | `tests2/Makefile:187`（`|| true`）、`:213`（`%.expect` 用 gcc） | 负向用例无需新基建，`.expect` 里放诊断文本即可（`149_parameter_qualifier_errors` 是先例） |

---

## 1. 多路径推演：标准门控怎么做

| 路径 | 做法 | 复杂度 | 可维护性 | 风险 | 结论 |
|---|---|---|---|---|---|
| A. 散落比较 | 各处写 `if (s->cversion >= 202311)` | 最低 | 差：门控散落、易漏、无法表达 gnu 差异 | 低 | 备选 |
| **B. 档位枚举 + 判断函数 + 每档默认表**（对齐 gcc 的 `flag_isoc23` + `lang_flags`） | `enum TCCStd`，`std_at_least(s, TCC_STD_C23)`；每档默认（数字分隔符、`#elifdef`、属性表）集中一处 | 中 | 好：新特性只需在表里加一行 | 低 | **采用** |
| C. 靠 `__STDC_VERSION__` 宏条件编译 | 用宏驱动核心特性开关 | — | — | 不可行 | 否决：宏只能影响用户代码，改不了编译器自身分支 |

**采用 B**，并保留 `cversion` 整数（`__STDC_VERSION__` 与 DWARF 继续用它），新增字段只做门控，避免改坏既有 4 处。

---

## 2. 任务总表

规模：S（<100 行）/ M（100–400 行）/ L（400–1500 行）/ XL（跨后端，>1500 行）。
批次 = 一个可独立提交与验收的 PR。

| ID | 任务 | 规模 | 依赖 | 批次 |
|---|---|---|---|---|
| T0.1 | 标准档位枚举 + `-std=` 严格校验 | S | — | P1 |
| T0.2 | `__STDC_VERSION__` 按档位输出（C23=202311） | S | T0.1 | P1 |
| T0.3 | 未知预处理指令 warning→error（含白名单） | S | — | P1 |
| T0.4 | 跳过态指令校验：`#elifdef` 不得被静默吞掉 | S | T0.3 | P1 |
| T0.5 | 负向测试基建 + 首批 4 个负向用例 | S | T0.1/3/4 | P1 |
| T0.6 | `auto x = <expr>;` 在 C23 档位下未推导即报错 | S | T0.1 | P2 |
| T1.1 | 数字分隔符 `1'000` | S | T0.1 | P2 |
| T1.2 | `u8'c'` 字符字面量 | S | T0.1 | P2 |
| T1.3 | `#elifdef`/`#elifndef` 实现（T0.4 的正向化） | S | T0.4 | P2 |
| T1.4 | `__VA_OPT__` | M | T0.1 | P3 |
| T1.5 | `#embed` + `__has_embed` | M | T0.1 | P3 |
| T2.1 | `nullptr` + `nullptr_t` | S | T0.1 | P4 |
| T2.2 | `bool`/`true`/`false` 关键字 | S | T0.1 | P4 |
| T2.3 | `static_assert`/`alignas`/`alignof`/`thread_local` 别名 | S | T0.1 | P4 |
| T2.4 | `typeof_unqual` | S | T0.1 | P4 |
| T2.5 | `[[…]]` 属性（含标准属性表） | M | T0.1 | P5 |
| T2.6 | 已有 C23 碎片补测试（防回退） | S | — | P1 |
| T3.1 | `auto` 类型推导 | L | T0.6 | P6 |
| T3.2 | `constexpr` 对象 | M | T0.1 | P6 |
| T3.3 | 属性诊断语义（`nodiscard`/`deprecated`/`fallthrough`） | M | T2.5 | P7 |
| T4.1 | `_BitInt` 类型模型（`N ≤ 64`） | L | — | P8 |
| T4.2 | `_BitInt` 类型提升/常量折叠 | L | T4.1 | P8 |
| T4.3 | `_BitInt` x86_64 后端 | M | T4.2 | P8 |
| T4.4 | `_BitInt` 其余 4 后端 | XL | T4.3 | P9 |
| T4.5 | `_BitInt` ABI 与 `N > 64` | XL | T4.3 | P10 |

---

## 3. 任务详情

### T0.1 标准档位枚举 + `-std=` 严格校验

- 落点：`libtcc.c:1994`（case 体）、`libtcc.c:1567`（枚举）、`tcc.h:749`（新增字段）
- 做法：`enum TCCStd { TCC_STD_C89, TCC_STD_C99, TCC_STD_C11, TCC_STD_C17, TCC_STD_C23, TCC_STD_GNU89 ... }`；`-std=` 前缀为 `=`（选项是 NOSEP 形式）；非法值 `tcc_error("unknown -std= value '%s'", optarg + 1)`
- gcc 参照：`c-family/c-opts.cc:1966/1982`（`flag_isoc23 = 1`）；`libcpp/init.cc:101/177`（每标准默认表）
- 验收：`tcc -std=bogus -c x.c` 报错非零退出；`-std=c23` / `-std=gnu23` / `-std=c11` / `-std=gnu11` / `-std=c99` / `-std=c89` 全部接受；`make test` 全过
- 注意：`-std` 历史上接受 `=c11` 带等号（`TCC_OPTION_NOSEP`），实现要按现有 optarg 形态取参

### T0.2 `__STDC_VERSION__` 按档位输出

- 落点：`tccpp.c:3622`（`cstr_printf(cs, "#define __STDC_VERSION__ %dL\n", s1->cversion)`）、`libtcc.c:883`
- 验收：`-std=c23` 下预处理输出 `__STDC_VERSION__` 为 `202311L`；`c11` 仍 201112；`c99` 199901；`c89` 不定义（与 gcc 一致：`-std=c89` 不定义该宏）
- gcc 参照：`gcc/c-family/c-opts.cc` 的 c23 分支 + `libcpp/init.cc` 每档默认

### T0.3 未知预处理指令 warning→error

- 落点：`tccpp.c:1976`
- 做法：改 `tcc_error`，白名单保留 `#pragma`（已有 `:1963` 分支）、`#line`、`#warning`/`#error`；对 GCC 已知但 tcc 不支持的指令给出**指名**错误（而非泛化"unknown"）
- gcc 参照：gcc 对 `#foobar` 报 `invalid preprocessing directive #foobar`
- 验收：`#foobar`、`#embed`（T1.5 前）、`#include_next` 之外的真未知指令均报错；`#pragma once` 等现有用法不受影响；`make test` 全过
- 风险：现有用户代码里可能有被静默忽略的陌生指令 → 在 `Changelog` 显式标注破坏性变更

### T0.4 跳过态指令校验（本次扫描新发现）

- 落点：`tccpp.c:927-946`（`start_of_line` 的指令识别分支）
- 现状：只匹配 `TOK_ELSE`/`TOK_ELIF`/`TOK_ENDIF`/`TOK_IF*`/`TOK_ERROR`/`TOK_WARNING`，**其它 `#xxx` 整行丢弃且零诊断**
- 实测症状：`#ifdef A / #elifdef B / #else / #endif` 且未定义 `A` 时，`#elifdef` 被跳过，两个分支都不编译（`nm` 无符号），零警告；`#elif defined(B)` 正常得到符号
- 做法：跳过态遇到 `#` 后无法识别的指令名时，按 T0.3 同样策略报错（T1.3 落地后 `#elifdef` 走正常分支）
- 验收：上述三分支用例必须报错（T1.3 后必须正确选分支）；再加一条 `-DA`/`-DB`/都不定义的矩阵用例

### T0.5 负向测试基建 + 首批负向用例

- 落点：`tests/tests2/154_*.c` 起（现有最大编号 153）
- 做法：沿用现有机制——`.test` 规则 `Makefile:187` 里 `|| true`，非零退出不中断；`.expect` 写预期诊断（先例 `149_parameter_qualifier_errors`）
- 首批用例：`154_c23_std_invalid`（`-std=bogus`）、`155_unknown_directive`、`156_elifdef_skip`、`157_auto_no_infer`
- 验收：`make test` 中这 4 条通过；人为回退修复后这 4 条必须失败（**证明测试真的会报警**，这是防"假绿"的关键一步）

### T0.6 `auto` 在 C23 档位下未推导即报错

- 落点：`tccgen.c` 声明解析（`auto` 目前作为存储类，类型回落 int）
- 做法：C23 档位下，函数内 `auto <declarator> = <init>;` 若没有类型说明符 → 报错（提示 `auto` 类型推导尚未支持）；非 C23 档位维持现状
- 验收：`auto x = 1.5;` 报错；`auto int x = 1;`（旧式存储类）仍可编（与 gcc 的 C23 语义差异要写进文档）

### T1.1 数字分隔符 `1'000`

- 落点：`tccpp.c` 的 `parse_number()`
- gcc 参照：`libcpp/lex.cc:2345-2360`（相邻分隔符、结尾分隔符报错）；开关字段 `libcpp/include/cpplib.h:540`
- 验收：`1'000'000` == `1000000`；`0x1'F`、`1.5e1'0`、浮点/十六进制/二进制后缀组合；负向 `1''0`、`1'` 报错（与 gcc 同文案级别）
- 坑：`'` 同时是字符字面量起始符，必须限定在 pp-number 上下文

### T1.2 `u8'c'`

- 落点：`tccpp.c` 字符字面量解析
- 验收：`u8'c'` 类型为 `char`、值为 ASCII；多字节字符与 `u8'é'` 的行为需与 gcc 对照后写进用例（gcc 允许，值按 UTF-8 首字节？以实测为准，不臆断）

### T1.3 `#elifdef` / `#elifndef`

- 落点：`tccpp.c:1868`（`TOK_ELIF` 分支）、`tcctok.h`（加 `TOK_ELIFDEF`/`TOK_ELIFNDEF`）、`:927-946`（跳过态识别）
- gcc 参照：`libcpp/directives.cc:162-163`（表项，门控 `STDC23`）、`:482`
- 验收：`-DA` / `-DB` / 都不定义 三分支矩阵；`#elifndef` 对称用例；跳过态用例（T0.4）转为正向

### T1.4 `__VA_OPT__`

- 落点：`tccpp.c` 宏展开
- gcc 参照：`libcpp/macro.cc:94` 起
- 验收：空参数/非空参数、与 `##` 组合、嵌套必须报错；与 gcc 差分输出一致

### T1.5 `#embed` + `__has_embed`

- 落点：`tccpp.c`（指令分派 + 文件读取，复用现有 include 栈）
- gcc 参照：`libcpp/directives.cc:1378-1444`（`do_embed`，正文 67 行）
- 验收：真实二进制文件逐字节一致（`41424344` 级实测）；缺文件报错；`__has_embed` 至少返回 0/1 且不崩
- 可选延后：`limit`/`prefix`/`suffix` 参数

### T2.1 `nullptr` + `nullptr_t`

- 落点：`tcctok.h`、`tccgen.c`（表达式/赋值/比较）、`include/stddef.h`
- gcc 参照：关键字表 `c-family/c-common.cc:395`（`{"nullptr", RID_NULLPTR, D_C23 | D_CXX11}`）；`ginclude/stddef.h:465`（`typedef __typeof__(nullptr) nullptr_t;`）
- 验收：`void *p = nullptr;`、`if (p == nullptr)`、`sizeof(nullptr)` 与 gcc 一致、`_Generic` 匹配；`nullptr_t` 可作形参类型

### T2.2 `bool`/`true`/`false` 关键字

- 落点：`tcctok.h`、`tccgen.c`
- 验收：C23 档位下免头文件可用；同时 `#include <stdbool.h>` 不冲突（该头在 C23 下退化为空/仅 typedef）；`-std=c11` 下行为不变

### T2.3 `static_assert`/`alignas`/`alignof`/`thread_local` 别名

- 落点：`tcctok.h` + 现有 `_Static_assert`/`_Alignas`/`_Alignof`/`_Thread_local` 实现
- 验收：单参/双参 `static_assert`；`alignas(16)`/`alignof(double)`；`thread_local int`；两种拼写等价

### T2.4 `typeof_unqual`

- 落点：`tccgen.c` typeof 分支
- gcc 参照：`c/c-parser.cc:637`、`:843`；关键字表 `__typeof_unqual`（D_CONLY）
- 验收：`const int a` → `typeof_unqual(a)` 可赋值；`volatile`/`restrict` 同样剥掉；数组/指针的顶层限定语义

### T2.5 `[[…]]` 属性

- 落点：`tccpp.c`（`[[`/`]]` 前瞻）、`tccgen.c:3949`（复用 `__attribute__` 解析器）、新增标准属性表
- gcc 参照：`c/c-parser.cc:1804`（`c_parser_std_attribute_specifier_sequence`）、`:3714` 调用点；属性表在 `c-family/c-attribs.cc`
- 首批支持：`nodiscard`、`deprecated`、`fallthrough`、`noreturn`、`maybe_unused`
- 验收：与 `__attribute__` 等价性用例；`[[noreturn]]` 影响代码生成的行为用例；未知标准属性按 C23 要求（忽略或告警，与 gcc 对齐后写死）

### T2.6 已有 C23 碎片补测试

- 落点：仅 `tests/tests2/`
- 覆盖：复合语句末尾标签（`tccgen.c:7558`）、`enum E : type`（`:4504`）、`= {}`、`0b1010`、typedef 重定义
- 验收：5 条用例与 gcc 差分一致（防回退基线）

### T3.1 `auto` 类型推导

- 落点：`tccgen.c` 声明解析（需局部倒序：先解析初始化器求类型，再分配对象）
- gcc 参照：`c/c-parser.cc:820`、`:3836`
- 验收：整型/浮点/指针/结构体/数组边界；`auto` 与旧式存储类用法不冲突；`sizeof` 与 gcc 一致
- 风险：tcc 单遍无 AST，这是语法糖之外**第一个结构性改动**，必须小步提交（先支持标量初始化器）

### T3.2 `constexpr` 对象

- 落点：`tccgen.c` + 常量求值 + `.rodata`
- gcc 参照：`c/c-parser.cc:733`（RID_CONSTEXPR 分支）
- 验收：`constexpr int N = 3; int a[N];`；未初始化报错（文案对齐 gcc 的 `requires an initialized data declaration`）；**函数形式明确报不支持**（gcc 16 亦不支持）

### T3.3 属性诊断语义

- 落点：`tccgen.c` 属性处理
- 验收：丢返回值产生 `nodiscard` 警告、`deprecated` 产生警告、`fallthrough` 位置错误告警；告警文本与 `.expect` 对齐

### T4.1–T4.5 `_BitInt`

- T4.1 类型模型：`CType { int t; Sym *ref; }` 只有 32 位位域（`tcc.h:1044` `VT_BTYPE` 4 位；`tcc.h:1079` 位域宽度 12 位、上限 63）→ 新增位宽存储；先钉 `N ≤ 64`
- gcc 参照：`gcc/tree.h:543`（`BITINT_TYPE` 并入整数类型族）；`c/c-parser.cc:632`、`:838`
- T4.3 单后端（x86_64）验收：`sizeof(_BitInt(8))==1`、范围截断、与 gcc 差分算术
- T4.4 其余后端：`make cross-<target>` + QEMU 运行，逐个后端过 smoke
- T4.5 ABI：`N > 64` 的参数/返回值/对齐，与 gcc 编译对象互链

---

## 4. 批次（PR）划分

| 批次 | 内容 | 交付判据 |
|---|---|---|
| **P1** | T0.1–T0.5 + T2.6 | `make test` 全过；4 条负向用例能报警；`-std=c23` 不再静默 |
| **P2** | T0.6 + T1.1–T1.3 | 词法/预处理三件套差分通过；`#elifdef` 正向矩阵全过 |
| **P3** | T1.4、T1.5 | `__VA_OPT__`、`#embed` 与 gcc 输出一致 |
| **P4** | T2.1–T2.4 | 免头文件 `nullptr`/`bool`/`static_assert` 可用 |
| **P5** | T2.5 | `[[…]]` 与 `__attribute__` 等价 |
| **P6** | T3.1、T3.2 | `auto` 标量推导 + `constexpr` 对象 |
| **P7** | T3.3 | 三条诊断告警 |
| **P8** | T4.1–T4.3 | `_BitInt(≤64)` 在 x86_64 可用 |
| **P9/P10** | T4.4、T4.5 | 其余后端 + ABI |

---

## 5. 统一验收命令（每条任务都要跑）

```sh
cd /data/training/tinycc
./configure --prefix=/opt/tinycc --cc=gcc     # 仅首次/改了 configure
make -j"$(nproc)"
make test                                      # tests + tests2 全过
make test-install                              # 装了之后验证安装面
```

差分（每个特性至少一条）：

```sh
# 同一源码两编译器，比较运行输出
./tcc -run case.c > tcc.out 2>&1; gcc -std=c23 -o case.gcc case.c && ./case.gcc > gcc.out
diff tcc.out gcc.out
```

**防假完成的三条硬判据**：
1. 正向用例的 `.expect` 必须由 gcc 生成（`make tests2.NNN+`），不能手写；
2. 每条负向用例必须验证过"回退修复就变红"；
3. 涉及后端的任务（T4.x）必须在目标架构上真跑，不只是编译通过。

---

## 6. 不做（与 `C23-SUPPORT.md` §6 一致）

`constexpr` 函数（gcc 16 也不支持）、GNU 扩展长尾（`__builtin_shufflevector` 等）、C23 库设施完整实现、gcc 级 conformance 矩阵。
