# [Claude] 0096: tira o pc_trail e o watchdog de cada rotulo dos ucodes do GB gerados pelo RSPRecomp.
# Uso: python3 strip_watchdog.py arquivo.cpp [...]  (backup .antes-0096; idempotente)
import re, sys, shutil, os
for p in sys.argv[1:]:
    s = open(p).read().split('\n')
    out, i, n = [], 0, 0
    while i < len(s):
        l = s[i]
        if re.match(r'\s*ctx->pc_trail\[ctx->pc_trail_idx & 31\] = 0x[0-9A-Fa-f]+;$', l) and i + 2 < len(s) \
           and 'ctx->pc_trail_idx++;' in s[i+1] and 'if (++ctx->watchdog_count >' in s[i+2]:
            ind = len(s[i+2]) - len(s[i+2].lstrip())
            j = i + 3
            while not (s[j].strip() == '}' and len(s[j]) - len(s[j].lstrip()) == ind):
                j += 1
            i = j + 1; n += 1
            continue
        out.append(l); i += 1
    if n:
        if not os.path.exists(p + '.antes-0096'): shutil.copy(p, p + '.antes-0096')
        open(p, 'w').write('\n'.join(out))
    left = sum('watchdog_count >' in l for l in out)
    print(p, 'rotulos limpos:', n, 'watchdog restante:', left)
    assert left == 0
