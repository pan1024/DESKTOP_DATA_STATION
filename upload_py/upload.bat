@echo off 
if "%1" == "h" goto begin 
mshta vbscript:createobject("wscript.shell").run("%~0 h",0)(window.close)&&exit 
:begin 
cd /d %~dp0
start /b cmd /k "python upload.py"