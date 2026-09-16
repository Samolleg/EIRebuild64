@echo off
call "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars32.bat" >nul
cl /nologo /EHsc /std:c++17 /Zi trace-headless-r26.cpp /Fe:out\trace-headless-r26.exe /Fo:out\trace-headless-r26.obj /Fd:out\trace-headless-r26.pdb
