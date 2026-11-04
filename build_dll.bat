@echo off
call "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat" >nul
cd /d D:\AstralPartyGit\MultiLaunch
if not exist build mkdir build
cl /nologo /LD /O2 /EHsc /MT /D_CRT_SECURE_NO_WARNINGS ^
  /I third_party\minhook\include /I third_party\minhook\src ^
  src\Redirect\UserDataRedirect.cpp ^
  third_party\minhook\src\buffer.c third_party\minhook\src\hde\hde64.c ^
  third_party\minhook\src\hook.c third_party\minhook\src\trampoline.c ^
  /Fe:build\UserDataRedirect.dll ^
  /link /DLL ole32.lib
