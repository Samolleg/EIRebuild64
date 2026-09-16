@echo off
call "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars32.bat" >nul
cl /nologo /EHsc /std:c++17 /Zi trace-startup.cpp /Fe:out\trace-startup.exe /Fo:out\trace-startup.obj /Fd:out\trace-startup.pdb
cl /nologo /EHsc /I"C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\DIA SDK\include" resolve-pdb.cpp /Fe:out\resolve-pdb.exe /Fo:out\resolve-pdb.obj
