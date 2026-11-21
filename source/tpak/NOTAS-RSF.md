# Permissões para o save do VC (P1 do [Proton]) · 19/11/2026

- **.cia:** abrir o save de outro título (ARCHIVE_USER_SAVEDATA do VC) exige `CategoryFileSystemTool` em `AccessControlInfo/FileSystemAccess`. O template usado pelo build (`BanjoRecomp3D/lib/rt64-3ds/cia/template.rsf:51-57`) já tem a linha, e o RSF gerado em `build-3ds/PokemonStadium2Recompiled3DS.rsf` herda. Nada a mudar. Se um dia o template for trocado, manter `CategoryFileSystemTool`.
- **.3dsx:** o hbldr do Luma dá `fs_access_info = 0xFFFFFFFF`, então o VC abre também pelo Homebrew Launcher.
- Serviço: só `fs:USER`. Não usa `am:u` (não lista títulos; tenta os IDs da tabela de `vc_save_3ds.cpp`).
- **Não testado no console nem no Azahar.** O Azahar talvez não emule o save de outro título; sem VC, o jogo cai no save do SD.
