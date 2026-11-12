@echo off
REM Wgrywanie OpenDTU Fusion v2 + MIT-5000-8T + SolarEdge
REM Uzycie: flash.bat [wifi|poe] COMx      np. flash.bat wifi COM5
set VARIANT=%1
if "%VARIANT%"=="" set VARIANT=wifi
set PORT=%2
if "%VARIANT%"=="wifi" set IMG=%~dp0wifi\opendtufusionv2_factory.bin
if "%VARIANT%"=="poe" set IMG=%~dp0poe\opendtufusionv2_poe_factory.bin
python -m esptool version >nul 2>&1 || pip install esptool
set PORTARG=
if not "%PORT%"=="" set PORTARG=--port %PORT%
echo Kasowanie flash...
python -m esptool --chip esp32s3 %PORTARG% erase-flash || goto err
echo Wgrywanie %IMG% ...
python -m esptool --chip esp32s3 %PORTARG% --baud 921600 write-flash 0x0 "%IMG%" || goto err
echo Gotowe. Odlacz i podlacz zasilanie, potem polacz sie z Wi-Fi OpenDTU-xxxxxx (haslo openDTU42).
pause
exit /b 0
:err
echo BLAD wgrywania. Przytrzymaj BOOT, nacisnij RESET, pusc BOOT i sprobuj ponownie.
pause
exit /b 1
