@echo off
setlocal
pushd "%~dp0.."

python -m pip install -r "mbedtls\scripts\basic.requirements.txt"
if errorlevel 1 goto :fail

cd /d "%~dp0..\mbedtls"
wsl.exe --cd "%~dp0..\mbedtls" perl scripts/generate_errors.pl
if errorlevel 1 goto :fail
wsl.exe --cd "%~dp0..\mbedtls" perl scripts/generate_features.pl
if errorlevel 1 goto :fail
python framework\scripts\generate_ssl_debug_helpers.py --mbedtls-root . library
if errorlevel 1 goto :fail
python scripts\generate_config_checks.py library
if errorlevel 1 goto :fail

cd /d "%~dp0..\mbedtls\tf-psa-crypto"
python scripts\generate_driver_wrappers.py core
if errorlevel 1 goto :fail
python scripts\generate_config_checks.py core
if errorlevel 1 goto :fail

cd /d "%~dp0.."
git apply "LibraryPatches\MbedTLS_threading_alt_add.patch"
if errorlevel 1 goto :fail

set "exitCode=0"
goto :finish

:fail
set "exitCode=%errorlevel%"

:finish
popd
endlocal & exit /b %exitCode%
