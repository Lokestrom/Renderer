REM backup to complie shader if the cmake does not work
@echo off

call glslangValidator -V -o out/basic.vert.spirv vert.vert
call glslangValidator -V -o out/basic.frag.spirv frag.frag
call glslangValidator -V -o out/shadow.vert.spirv shadow.vert
pause
