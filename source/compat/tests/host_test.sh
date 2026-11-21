#!/bin/bash
# Teste de host da camada compat: compila 3 arquivos gerados + dispatch_aliases + s2_compat
# contra compat/include e o runtime upstream com os nmr-patches; lista o que fica indefinido.
set -u
C=~/stadium2-3ds/s2-3ds/compat
U=~/stadium2-3ds/s2-3ds/work/up-nmr
G=~/stadium2-3ds/pokemonStadiumGSRecomp/generated
O=$C/work/host; mkdir -p $O
INC="-I$C/include -I$G -I$U/librecomp/include -I$U/ultramodern/include -I$U/thirdparty/concurrentqueue -I$U/thirdparty"
DEF="-DRECOMP_MEM_MASK=0x00FFFFFFu -DRECOMP_3DS_PREEMPT_CHECK -DRECOMP_CF_NOTE_DISABLED"
ok=1
echo "== s2_compat.cpp"
rm -f $O/s2_compat.o
g++ -std=c++20 -O1 -Wall -c $INC $DEF $C/s2_compat.cpp -o $O/s2_compat.o 2>&1 | grep -E "error|warning" | head -20
[ -f $O/s2_compat.o ] || ok=0
for f in funcs_8 funcs_28 funcs_0 dispatch_aliases; do
  if [ "${REBUILD:-0}" = 1 ] || [ ! -f $O/$f.o ]; then
    echo "== $f.c"
    gcc -std=gnu11 -O0 -w -c $INC $DEF $G/$f.c -o $O/$f.o 2>&1 | grep error | head -10
  fi
  [ -f $O/$f.o ] || ok=0
done

nm -u $O/*.o | awk '{print $NF}' | sort -u > $O/undef.txt
nm --defined-only $O/*.o | awk '{print $NF}' | sort -u > $O/def.txt
comm -23 $O/undef.txt $O/def.txt > $O/ext.txt
# definidos em qualquer arquivo gerado (sem compilar o jogo inteiro)
grep -ohE '^RECOMP_FUNC void [A-Za-z0-9_]+\(' $G/*.c | sed -E 's/^RECOMP_FUNC void //; s/\($//' | sort -u > $O/gen_def.txt
comm -23 $O/ext.txt $O/gen_def.txt > $O/ext_runtime.txt
: > $O/falta.txt
while read -r sym; do
  case $sym in
    _GLOBAL_OFFSET_TABLE_|__assert_fail|fegetround|fesetround|lrintf|sqrtf|sqrt|memcpy|memset|abort|fprintf|stderr|__stack_chk_fail|_Z*|__cxa_*|__gxx_*|_Unwind*|pthread_*|__tls_get_addr) continue;;
  esac
  if grep -rqE "(^|[^A-Za-z0-9_])${sym}[[:space:]]*(\(|=|;)" $U/librecomp/src $U/ultramodern/src 2>/dev/null; then continue; fi
  echo "$sym" >> $O/falta.txt
done < $O/ext_runtime.txt
echo "externos ao jogo (fora dos funcs_*.c): $(wc -l < $O/ext_runtime.txt)"
cat $O/ext_runtime.txt | tr '\n' ' '; echo
echo "sem definicao no runtime upstream+nmr: $(wc -l < $O/falta.txt)"
cat $O/falta.txt
echo "compat define:"; nm --defined-only $O/s2_compat.o | awk '$2 ~ /[TWBDV]/ && $3 !~ /^_Z/ {print $3}' | tr '\n' ' '; echo
echo "ok=$ok"
