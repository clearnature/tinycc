#!/usr/bin/env python3
"""C23 feature audit: one snippet per C23 change, compiled and (when it has
observable output) run under both gcc and tcc, then compared.

The point is coverage, not a handful of examples: every entry names the C23
feature it stands for.  A case is only "ok" when both compilers agree on
whether it compiles and on what it prints.
"""
import os, re, subprocess, sys

ROOT = "/data/training/tinycc"
TCC = os.path.join(ROOT, "tcc")
WORK = os.path.join(ROOT, ".bench", "c23-features")
os.makedirs(WORK, exist_ok=True)

# (feature, source)
CASES = [
 # --- keywords and spellings (C23 6.4.1) ---
 ("kw: bool/true/false", 'int main(void){ bool b = true; return (int)b - 1; }\n'),
 ("kw: static_assert (no message)", 'static_assert(1);\nint main(void){return 0;}\n'),
 ("kw: alignas/alignof", 'alignas(8) char c; int main(void){return (int)alignof(c) - 8;}\n'),
 ("kw: thread_local", 'thread_local int v; int main(void){v = 2; return v - 2;}\n'),
 ("kw: typeof", 'int main(void){ int x = 1; typeof(x) y = 2; return (int)sizeof y - 4; }\n'),
 ("kw: typeof_unqual drops const", 'int main(void){ const int x=1; typeof_unqual(x) y=2; y=3; return y-3; }\n'),
 ("kw: nullptr", 'int main(void){ int *p = nullptr; return p != nullptr; }\n'),
 ("kw: constexpr object", 'constexpr int n = 4; int a[n]; int main(void){return (int)(sizeof a/sizeof a[0]) - 4;}\n'),
 ("kw: constants are not macros", '#ifdef true\n#error true must not be a macro\n#endif\nint main(void){return 0;}\n'),

 # --- attribute support (C23 6.7.12.1) ---
 ("attr: [[deprecated]]", '[[deprecated]] void f(void){} int main(void){f();return 0;}\n'),
 ("attr: [[deprecated(\"msg\")]]", '[[deprecated("use g")]] void f(void){} int main(void){f();return 0;}\n'),
 ("attr: [[fallthrough]]", 'int main(void){int x=0; switch(x){case 0: x=1; [[fallthrough]]; case 1: break;} return 0;}\n'),
 ("attr: [[maybe_unused]]", '[[maybe_unused]] static int v; int main(void){return 0;}\n'),
 ("attr: [[nodiscard]]", '[[nodiscard]] int f(void){return 1;} int main(void){return f()-1;}\n'),
 ("attr: [[noreturn]]", '[[noreturn]] void f(void); int main(void){if(0)f(); return 0;}\n'),
 ("attr: [[unsequenced]]", '[[unsequenced]] int f(int x){return x;} int main(void){return f(1)-1;}\n'),
 ("attr: [[reproducible]]", '[[reproducible]] int f(void){return 0;} int main(void){return f();}\n'),
 ("attr: on a label", 'int main(void){ [[maybe_unused]] lbl: return 0; }\n'),
 ("attr: unknown attribute warns", '[[no_such_attr]] int x; int main(void){return 0;}\n'),
 ("attr: gnu scope applied", '#include <stdio.h>\nstruct [[gnu::packed]] S{char c;int i;};\nint main(void){printf("%d\\n",(int)sizeof(struct S));return 0;}\n'),
 ("attr: gnu scope unknown warns", '[[gnu::no_such(1)]] int y; int main(void){return 0;}\n'),
 ("has_c_attribute gnu:: is 1", 'int main(void){return __has_c_attribute(gnu::packed) == 1 ? 0 : 1;}\n'),
 ("has_c_attribute value", '#if __has_c_attribute(deprecated) != 202311\n#error wrong\n#endif\nint main(void){return 0;}\n'),
 ("has_c_attribute unknown is 0", '#if __has_c_attribute(bogus)\n#error wrong\n#endif\nint main(void){return 0;}\n'),

 # --- preprocessor (C23 6.10) ---
 ("pp: #elifdef", '#define A\n#if 0\n#elifdef A\nint v=1;\n#endif\nint main(void){return v-1;}\n'),
 ("pp: #elifndef", '#if 0\n#elifndef NOTDEF\nint v=1;\n#endif\nint main(void){return v-1;}\n'),
 ("pp: #warning", '#warning hello\nint main(void){return 0;}\n'),
 ("pp: __VA_OPT__ non-empty", '#define M(a,...) f(a __VA_OPT__(,) __VA_ARGS__)\nstatic int f(int a,int b){return a+b;}\nint main(void){return M(1,2)-3;}\n'),
 ("pp: __VA_OPT__ empty", '#define M(a,...) (a __VA_OPT__(+) __VA_ARGS__)\nint main(void){return M(3)-3;}\n'),
 ("pp: __VA_OPT__ nested parens", '#define M(...) g(__VA_OPT__(1,2))\nstatic int g(int a,int b){return a+b;}\nint main(void){return M(x)-3;}\n'),
 ("pp: __has_embed", '#if __has_embed("audit.bin") != __STDC_EMBED_FOUND__\n#error wrong\n#endif\nint main(void){return 0;}\n'),
 ("pp: __STDC_EMBED_* macros", '#if __STDC_EMBED_FOUND__ != 1 || __STDC_EMBED_EMPTY__ != 2 || __STDC_EMBED_NOT_FOUND__ != 0\n#error wrong\n#endif\nint main(void){return 0;}\n'),
 ("pp: #embed", '#include <stdio.h>\nstatic const unsigned char d[]={\n#embed "audit.bin"\n};\nint main(void){printf("%d %d\\n",(int)sizeof d,(int)d[1]);return 0;}\n'),
 ("pp: #embed limit", '#include <stdio.h>\nstatic const unsigned char d[]={\n#embed "audit.bin" limit(2)\n};\nint main(void){printf("%d\\n",(int)sizeof d);return 0;}\n'),
 ("pp: #embed prefix/suffix", '#include <stdio.h>\nstatic const unsigned char d[]={\n#embed "audit.bin" limit(1) prefix(9,) suffix(,8)\n};\nint main(void){printf("%d %d %d\\n",d[0],d[1],d[2]);return 0;}\n'),
 ("pp: #embed if_empty", '#include <stdio.h>\nstatic const unsigned char d[]={\n#embed "empty.bin" if_empty(0)\n};\nint main(void){printf("%d\\n",(int)sizeof d);return 0;}\n'),
 ("pp: #embed macro filename", '#define F "audit.bin"\n#include <stdio.h>\nstatic const unsigned char d[]={\n#embed F\n};\nint main(void){printf("%d\\n",(int)sizeof d);return 0;}\n'),

 # --- lexing (C23 6.4.4 / 6.4.5) ---
 ("lex: binary literal", 'int main(void){return 0b1010 - 10;}\n'),
 ("lex: digit separators", 'int main(void){return 1\'000\'000 == 1000000 ? 0 : 1;}\n'),
 ("lex: binary with separators", 'int main(void){return 0b1010\'1010 - 170;}\n'),
 ("lex: hex float still fine", 'int main(void){double d = 0x1p3; return (int)d - 8;}\n'),
 ("lex: u8 character constant", 'int main(void){return u8\'a\' - 97;}\n'),
 ("lex: u8 string", '#include <stdio.h>\nint main(void){printf("%d\\n",(int)sizeof(u8"ab"));return 0;}\n'),

 # --- _BitInt (C23 6.2.5) ---
 ("bitint: size 7", 'int main(void){return (int)sizeof(_BitInt(7)) - 1;}\n'),
 ("bitint: size 33", 'int main(void){return (int)sizeof(_BitInt(33)) - 8;}\n'),
 ("bitint: unsigned width 1", 'int main(void){unsigned _BitInt(1) u = 1; return (int)u - 1;}\n'),
 ("bitint: truncation signed", 'int main(void){_BitInt(4) a = 9; return (int)a + 7;}\n'),
 ("bitint: arithmetic no promotion", '#include <stdio.h>\nint main(void){_BitInt(16) a=30000,b=20000; printf("%d %d\\n",(int)(a+b),(int)sizeof(a+b)); return 0;}\n'),
 ("bitint: struct member", 'struct S{_BitInt(7) a;}; int main(void){struct S s; s.a=1; return (int)s.a - 1;}\n'),
 ("bitint: _Generic", 'int main(void){_BitInt(5) x=1; return _Generic(x, _BitInt(5): 0, default: 1);}\n'),
 ("bitint: width must be constant", 'int main(void){int n=4; return (int)sizeof(_BitInt(n));}\n'),
 ("bitint: width 0 rejected", 'int main(void){return (int)sizeof(_BitInt(0));}\n'),
 ("bitint: negative width rejected", 'int main(void){return (int)sizeof(_BitInt(-3));}\n'),
 ("bitint: N>64 diagnosed", 'int main(void){return (int)sizeof(_BitInt(65));}\n'),

 # --- other C23 semantic changes ---
 ("sem: auto inference", 'int main(void){ auto x = 3; return x - 3; }\n'),
 ("sem: auto from string literal", '#include <string.h>\nint main(void){ auto s = "hi"; return (int)strlen(s) - 2; }\n'),
 ("sem: constexpr aggregate", 'constexpr int t[2]={1,2}; int main(void){return t[1]-2;}\n'),
 ("sem: sizeof(nullptr) is pointer size", 'int main(void){return (int)sizeof(nullptr) == (int)sizeof(void*) ? 0 : 1;}\n'),
 ("sem: nullptr_t from stddef.h", '#include <stddef.h>\nint main(void){nullptr_t n = nullptr; return n != 0;}\n'),
 ("sem: unreachable()", '#include <stddef.h>\nint main(void){if(0) unreachable(); return 0;}\n'),
 ("sem: __STDC_VERSION__", '#include <stdio.h>\nint main(void){printf("%ld\\n",(long)__STDC_VERSION__);return 0;}\n'),
 ("sem: stdckdint", '#include <stdckdint.h>\nint main(void){int r; return ckd_add(&r,2,3) ? 0 : (r-5);}\n'),
 ("sem: stdbit", '#include <stdbit.h>\n#include <stdio.h>\nint main(void){printf("%u\\n",(unsigned)stdc_count_ones(7u));return 0;}\n'),
 ("sem: bool is 1 byte", 'int main(void){return (int)sizeof(bool) - 1;}\n'),
 ("sem: alignas cannot weaken", 'alignas(1) int x; int main(void){return 0;}\n'),
 ("sem: static_assert catches false", 'static_assert(0, "boom"); int main(void){return 0;}\n'),
 ("sem: K&R definition (C23 removed it)", 'int f(a) int a; { return a; } int main(void){return f(1)-1;}\n'),
 ("sem: _Noreturn deprecated but works", '_Noreturn void f(void); int main(void){if(0)f();return 0;}\n'),
 ("sem: empty initializer {}", 'struct S{int a;}; int main(void){struct S s = {}; return s.a;}\n'),
 ("sem: constexpr needs initializer", 'constexpr int x; int main(void){return 0;}\n'),

 # --- library facilities the compiler must supply or tolerate ---
 ("lib: stdbit.h present", '#include <stdbit.h>\nint main(void){return 0;}\n'),
 ("lib: stdckdint.h present", '#include <stdckdint.h>\nint main(void){return 0;}\n'),
 ("lib: stddef.h nullptr_t/unreachable", '#include <stddef.h>\nint main(void){nullptr_t p = nullptr; (void)p; return 0;}\n'),
]

def norm(s):
    s = re.sub(r"^\S*:\d+(:\d+)?:\s*", "", s.strip())
    s = re.sub(r"^.*?error: ", "error: ", s)
    return s

def run(cc, src, exe, extra=()):
    env = dict(os.environ, LC_ALL="C")
    r = subprocess.run([cc, "-w", "-std=c23", *extra, src, "-o", exe],
                       capture_output=True, text=True, cwd=WORK, env=env)
    if r.returncode != 0:
        lines = [l for l in r.stderr.strip().splitlines() if "error" in l]
        return ("ERR", norm(lines[0]) if lines else "ERR")
    p = subprocess.run([exe], capture_output=True, text=True, cwd=WORK)
    out = p.stdout.strip().replace("\n", " | ")
    err = p.stderr.strip().replace("\n", " | ")
    return ("OK", (out + (" / " + err if err else "")) or ("rc=%d" % p.returncode))

open(os.path.join(WORK, "audit.bin"), "wb").write(bytes([10, 20, 30, 40]))
open(os.path.join(WORK, "empty.bin"), "wb").write(b"")

rows = []
for name, code in CASES:
    src = os.path.join(WORK, "c.c")
    open(src, "w").write(code)
    g = run("gcc", src, os.path.join(WORK, "g.out"))
    t = run(TCC, src, os.path.join(WORK, "t.out"))
    same = (g[0] == t[0]) and (g[1] == t[1])
    rows.append((name, g, t, same))

bad = [r for r in rows if not r[3]]
for name, g, t, same in rows:
    if same:
        print(f"  ok   {name}")
    else:
        print(f"  DIFF {name}")
        print(f"        gcc[{g[0]}] {g[1][:70]}")
        print(f"        tcc[{t[0]}] {t[1][:70]}")
print(f"\n{len(rows) - len(bad)}/{len(rows)} match gcc")
if bad:
    print("differing features:", ", ".join(r[0] for r in bad))
