"""recomp.h: fallback sem __int128 para 3DS/ARM32 (MIPS DMULT/DMULTU) + autoteste contra __int128 no PC."""
import sys, subprocess, os

FB = '''#else
/* 3DS/ARM32: sem __int128. Produto 64x64->128 por partes de 32 bits (MIPS DMULTU/DMULT). */
static inline void DMULTU(uint64_t a, uint64_t b, uint64_t* lo64, uint64_t* hi64) {
    uint64_t al = (uint32_t)a, ah = a >> 32, bl = (uint32_t)b, bh = b >> 32;
    uint64_t ll = al * bl, lh = al * bh, hl = ah * bl, hh = ah * bh;
    uint64_t mid = (ll >> 32) + (uint32_t)lh + (uint32_t)hl;
    *lo64 = (mid << 32) | (uint32_t)ll;
    *hi64 = hh + (lh >> 32) + (hl >> 32) + (mid >> 32);
}
static inline void DMULT(int64_t a, int64_t b, int64_t* lo64, int64_t* hi64) {
    uint64_t lo, hi;
    DMULTU((uint64_t)a, (uint64_t)b, &lo, &hi);
    if (a < 0) hi -= (uint64_t)b;
    if (b < 0) hi -= (uint64_t)a;
    *lo64 = (int64_t)lo; *hi64 = (int64_t)hi;
}
#endif'''
OLD = '#else\n#error "128-bit integer type not found"\n#endif'

TEST = r'''#include <stdint.h>
#include <stdio.h>
#include <assert.h>
%s
int main(){ uint64_t v[]={0,1,0xFFFFFFFFull,0xFFFFFFFFFFFFFFFFull,0x8000000000000000ull,0x123456789ABCDEFull,0xDEADBEEFCAFEBABEull,0x7FFFFFFFFFFFFFFFull};
 for(int i=0;i<8;i++)for(int j=0;j<8;j++){uint64_t lo,hi; DMULTU(v[i],v[j],&lo,&hi); unsigned __int128 r=(unsigned __int128)v[i]*v[j];
  assert(lo==(uint64_t)r&&hi==(uint64_t)(r>>64));
  int64_t sl,sh; DMULT((int64_t)v[i],(int64_t)v[j],&sl,&sh); __int128 q=(__int128)(int64_t)v[i]*(int64_t)v[j];
  assert(sl==(int64_t)q&&sh==(int64_t)(q>>64));}
 puts("DMULT/DMULTU ok"); return 0; }
'''

if __name__ == '__main__':
    p = sys.argv[1]
    body = FB.split('\n', 1)[1].rsplit('\n', 1)[0]
    t = '/tmp/t_int128.c'
    open(t, 'w').write(TEST % body)
    subprocess.run(['gcc', '-O2', t, '-o', '/tmp/t_int128'], check=True)
    subprocess.run(['/tmp/t_int128'], check=True)
    s = open(p).read()
    if OLD in s:
        open(p, 'w').write(s.replace(OLD, FB)); print('recomp.h corrigido')
    else:
        print('recomp.h já corrigido' if '3DS/ARM32' in s else 'padrão não encontrado')
