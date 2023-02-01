@echo off 
if "%1" == "h" goto begin 
mshta vbscript:createobject("wscript.shell").run("%~0 h",0)(window.close)&&exit 
:begin 
start /b cmd /k "python D:\ARDUINO_PLATFORM_PROJECT\DESKTOP_DATA_STATION\upload_py\upload.py"