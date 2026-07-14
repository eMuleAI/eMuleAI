@echo off
setlocal EnableExtensions DisableDelayedExpansion

set "SCRIPT_DIR=%~dp0"
set "CONFIG_FILE=%SCRIPT_DIR%eMuleAI.conf"
set "ERROR_MESSAGE="
set "EXIT_CODE=2"
set "OLD_IMAGE_IDS_FILE="
set "DOCKER_IMAGE="
set "DOCKER_TAG="

for %%K in (
	IMAGE_SOURCE REGISTRY_IMAGE REGISTRY_TAG LOCAL_IMAGE LOCAL_TAG CLEAN_OLD_IMAGES
	CONTAINER_NAME PLATFORM DISPLAY_MODE DATA_DIR_WINDOWS DATA_DIR_LINUX DATA_DIR_MACOS
	PUID_WINDOWS PGID_WINDOWS PUID_LINUX PGID_LINUX PUID_MACOS PGID_MACOS
	DISPLAY_BIND_ADDRESS DISPLAY_CONNECT_ADDRESS EMULE_BIND_ADDRESS XPRA_PORT NOVNC_PORT
	EMULE_TCP_PORT EMULE_UDP_PORT DISPLAY_WIDTH DISPLAY_HEIGHT XPRA_CANVAS_WIDTH
	XPRA_CANVAS_HEIGHT DISPLAY_DEPTH XPRA_PASSWORD XPRA_USERNAME AUTO_ATTACH_XPRA
	XPRA_CLIENT_EXE_WINDOWS XPRA_CLIENT_EXE_LINUX XPRA_CLIENT_EXE_MACOS
	XPRA_CLIENT_BACKEND_WINDOWS XPRA_DESKTOP_SCALING SHM_SIZE RESTART_POLICY
) do set "%%K="

if not exist "%CONFIG_FILE%" (
	set "ERROR_MESSAGE=Configuration file was not found: %CONFIG_FILE%"
	goto :fail
)

for /f "usebackq eol=# tokens=1,* delims==" %%A in ("%CONFIG_FILE%") do (
	call :is_supported_config_key "%%A"
	if errorlevel 1 (
		set "ERROR_MESSAGE=Unknown configuration key: %%A"
		goto :fail
	)
	set "%%A=%%B"
)

for %%K in (
	IMAGE_SOURCE REGISTRY_IMAGE REGISTRY_TAG LOCAL_IMAGE LOCAL_TAG CLEAN_OLD_IMAGES
	CONTAINER_NAME PLATFORM DISPLAY_MODE DATA_DIR_WINDOWS PUID_WINDOWS PGID_WINDOWS
	DISPLAY_BIND_ADDRESS DISPLAY_CONNECT_ADDRESS EMULE_BIND_ADDRESS XPRA_PORT NOVNC_PORT
	EMULE_TCP_PORT EMULE_UDP_PORT DISPLAY_WIDTH DISPLAY_HEIGHT XPRA_CANVAS_WIDTH
	XPRA_CANVAS_HEIGHT DISPLAY_DEPTH XPRA_PASSWORD XPRA_USERNAME AUTO_ATTACH_XPRA
	XPRA_CLIENT_BACKEND_WINDOWS XPRA_DESKTOP_SCALING SHM_SIZE RESTART_POLICY
) do (
	call :require_config_value "%%K"
	if errorlevel 1 goto :fail
)

call :expand_windows_path "%DATA_DIR_WINDOWS%"
set "DATA_DIR=%EXPANDED_PATH%"
if not defined DATA_DIR set "DATA_DIR=%USERPROFILE%\AppData\Local\eMuleAI"
set "PUID=%PUID_WINDOWS%"
set "PGID=%PGID_WINDOWS%"
if /I "%PUID%"=="auto" set "PUID=1000"
if /I "%PGID%"=="auto" set "PGID=1000"
set "XPRA_CLIENT_EXE="
if not defined XPRA_CLIENT_EXE_WINDOWS goto :xpra_client_exe_ready
call :expand_windows_path "%XPRA_CLIENT_EXE_WINDOWS%"
set "XPRA_CLIENT_EXE=%EXPANDED_PATH%"
:xpra_client_exe_ready
set "XPRA_CLIENT_BACKEND=%XPRA_CLIENT_BACKEND_WINDOWS%"

if /I "%IMAGE_SOURCE%"=="registry" (
	set "DOCKER_IMAGE=%REGISTRY_IMAGE%"
	set "DOCKER_TAG=%REGISTRY_TAG%"
) else if /I "%IMAGE_SOURCE%"=="local" (
	set "DOCKER_IMAGE=%LOCAL_IMAGE%"
	set "DOCKER_TAG=%LOCAL_TAG%"
) else (
	set "ERROR_MESSAGE=IMAGE_SOURCE must be registry or local."
	goto :fail
)
set "IMAGE_REFERENCE=%DOCKER_IMAGE%:%DOCKER_TAG%"

if /I not "%DISPLAY_MODE%"=="xpra" if /I not "%DISPLAY_MODE%"=="novnc" (
	set "ERROR_MESSAGE=DISPLAY_MODE must be xpra or novnc."
	goto :fail
)
if /I not "%CLEAN_OLD_IMAGES%"=="yes" if /I not "%CLEAN_OLD_IMAGES%"=="no" (
	set "ERROR_MESSAGE=CLEAN_OLD_IMAGES must be yes or no."
	goto :fail
)
if /I not "%AUTO_ATTACH_XPRA%"=="yes" if /I not "%AUTO_ATTACH_XPRA%"=="no" (
	set "ERROR_MESSAGE=AUTO_ATTACH_XPRA must be yes or no."
	goto :fail
)
if /I not "%XPRA_CLIENT_BACKEND%"=="gtk" if /I not "%XPRA_CLIENT_BACKEND%"=="win32" if /I not "%XPRA_CLIENT_BACKEND%"=="auto" (
	set "ERROR_MESSAGE=XPRA_CLIENT_BACKEND_WINDOWS must be gtk, win32 or auto."
	goto :fail
)
if /I not "%XPRA_DESKTOP_SCALING%"=="off" if /I not "%XPRA_DESKTOP_SCALING%"=="on" if /I not "%XPRA_DESKTOP_SCALING%"=="auto" (
	set "ERROR_MESSAGE=XPRA_DESKTOP_SCALING must be off, on or auto."
	goto :fail
)
call :validate_port "%XPRA_PORT%"
if errorlevel 1 (
	set "ERROR_MESSAGE=XPRA_PORT must be between 1 and 65535."
	goto :fail
)
call :validate_port "%NOVNC_PORT%"
if errorlevel 1 (
	set "ERROR_MESSAGE=NOVNC_PORT must be between 1 and 65535."
	goto :fail
)
call :validate_port "%EMULE_TCP_PORT%"
if errorlevel 1 (
	set "ERROR_MESSAGE=EMULE_TCP_PORT must be between 1 and 65535."
	goto :fail
)
call :validate_udp_port "%EMULE_UDP_PORT%"
if errorlevel 1 (
	set "ERROR_MESSAGE=EMULE_UDP_PORT must be between 0 and 65535."
	goto :fail
)
call :validate_dimension "%DISPLAY_WIDTH%"
if errorlevel 1 (
	set "ERROR_MESSAGE=DISPLAY_WIDTH must be between 320 and 8192."
	goto :fail
)
call :validate_dimension "%DISPLAY_HEIGHT%"
if errorlevel 1 (
	set "ERROR_MESSAGE=DISPLAY_HEIGHT must be between 320 and 8192."
	goto :fail
)
call :validate_dimension "%XPRA_CANVAS_WIDTH%"
if errorlevel 1 (
	set "ERROR_MESSAGE=XPRA_CANVAS_WIDTH must be between 320 and 8192."
	goto :fail
)
call :validate_dimension "%XPRA_CANVAS_HEIGHT%"
if errorlevel 1 (
	set "ERROR_MESSAGE=XPRA_CANVAS_HEIGHT must be between 320 and 8192."
	goto :fail
)
if not "%DISPLAY_DEPTH%"=="16" if not "%DISPLAY_DEPTH%"=="24" (
	set "ERROR_MESSAGE=DISPLAY_DEPTH must be 16 or 24."
	goto :fail
)
call :validate_id "%PUID%"
if errorlevel 1 (
	set "ERROR_MESSAGE=PUID_WINDOWS must be auto or an integer between 0 and 2147483647."
	goto :fail
)
call :validate_id "%PGID%"
if errorlevel 1 (
	set "ERROR_MESSAGE=PGID_WINDOWS must be auto or an integer between 0 and 2147483647."
	goto :fail
)

where docker >nul 2>&1
if errorlevel 1 (
	set "ERROR_MESSAGE=Docker CLI was not found in PATH."
	goto :fail
)
docker info >nul 2>&1
if errorlevel 1 (
	set "ERROR_MESSAGE=Docker Desktop is not running or the Docker engine is unavailable."
	goto :fail
)

if not exist "%DATA_DIR%\" (
	mkdir "%DATA_DIR%"
	if errorlevel 1 (
		set "ERROR_MESSAGE=Could not create data directory: %DATA_DIR%"
		goto :fail
	)
)
for %%I in ("%DATA_DIR%") do set "DATA_DIR=%%~fI"

set "OLD_IMAGE_IDS_FILE=%TEMP%\eMuleAI-old-images-%RANDOM%-%RANDOM%.txt"
docker image ls --all --no-trunc --quiet --filter "reference=%DOCKER_IMAGE%:*" >"%OLD_IMAGE_IDS_FILE%" 2>nul

set "BEFORE_IMAGE_ID="
for /f "usebackq delims=" %%I in (`docker image inspect --format "{{.Id}}" "%IMAGE_REFERENCE%" 2^>nul`) do set "BEFORE_IMAGE_ID=%%I"

if /I "%IMAGE_SOURCE%"=="registry" (
	echo Checking registry image: %IMAGE_REFERENCE%
	docker pull --platform "%PLATFORM%" "%IMAGE_REFERENCE%"
	if errorlevel 1 (
		set "ERROR_MESSAGE=Could not pull %IMAGE_REFERENCE%."
		goto :fail
	)
) else (
	echo Using local image: %IMAGE_REFERENCE%
	docker image inspect "%IMAGE_REFERENCE%" >nul 2>&1
	if errorlevel 1 (
		set "ERROR_MESSAGE=Local image %IMAGE_REFERENCE% was not found. Build it first or change IMAGE_SOURCE to registry."
		goto :fail
	)
)

set "NEW_IMAGE_ID="
for /f "usebackq delims=" %%I in (`docker image inspect --format "{{.Id}}" "%IMAGE_REFERENCE%" 2^>nul`) do set "NEW_IMAGE_ID=%%I"
if not defined NEW_IMAGE_ID (
	set "ERROR_MESSAGE=Could not determine the selected image ID."
	goto :fail
)
if /I "%IMAGE_SOURCE%"=="registry" (
	if not defined BEFORE_IMAGE_ID (
		echo Downloaded registry image: %NEW_IMAGE_ID%
	) else if /I "%BEFORE_IMAGE_ID%"=="%NEW_IMAGE_ID%" (
		echo The local registry image is already current. Existing layers were reused.
	) else (
		echo Updated registry image: %BEFORE_IMAGE_ID% -^> %NEW_IMAGE_ID%
	)
)

docker container inspect "%CONTAINER_NAME%" >nul 2>&1
if not errorlevel 1 (
	echo Replacing existing container: %CONTAINER_NAME%
	docker rm -f "%CONTAINER_NAME%" >nul
	if errorlevel 1 (
		set "ERROR_MESSAGE=Could not remove the existing container."
		goto :fail
	)
)

if /I "%DISPLAY_MODE%"=="xpra" (
	set "DISPLAY_PORT_ARGUMENT=--publish %DISPLAY_BIND_ADDRESS%:%XPRA_PORT%:%XPRA_PORT%/tcp"
	set "DISPLAY_PORT=%XPRA_PORT%"
	set "DISPLAY_DESCRIPTION=Xpra"
) else (
	set "DISPLAY_PORT_ARGUMENT=--publish %DISPLAY_BIND_ADDRESS%:%NOVNC_PORT%:%NOVNC_PORT%/tcp"
	set "DISPLAY_PORT=%NOVNC_PORT%"
	set "DISPLAY_DESCRIPTION=noVNC"
)
set "UDP_PORT_ARGUMENT="
if not "%EMULE_UDP_PORT%"=="0" set "UDP_PORT_ARGUMENT=--publish %EMULE_BIND_ADDRESS%:%EMULE_UDP_PORT%:%EMULE_UDP_PORT%/udp"

docker run -d ^
	--name "%CONTAINER_NAME%" ^
	--platform "%PLATFORM%" ^
	--restart "%RESTART_POLICY%" ^
	--shm-size "%SHM_SIZE%" ^
	--mount "type=bind,source=%DATA_DIR%,target=/data" ^
	-e "PUID=%PUID%" ^
	-e "PGID=%PGID%" ^
	-e "DISPLAY_MODE=%DISPLAY_MODE%" ^
	-e "DISPLAY_WIDTH=%DISPLAY_WIDTH%" ^
	-e "DISPLAY_HEIGHT=%DISPLAY_HEIGHT%" ^
	-e "XPRA_CANVAS_WIDTH=%XPRA_CANVAS_WIDTH%" ^
	-e "XPRA_CANVAS_HEIGHT=%XPRA_CANVAS_HEIGHT%" ^
	-e "DISPLAY_DEPTH=%DISPLAY_DEPTH%" ^
	-e "XPRA_PORT=%XPRA_PORT%" ^
	-e "NOVNC_PORT=%NOVNC_PORT%" ^
	-e "XPRA_PASSWORD=%XPRA_PASSWORD%" ^
	-e "EMULE_TCP_PORT=%EMULE_TCP_PORT%" ^
	-e "EMULE_UDP_PORT=%EMULE_UDP_PORT%" ^
	%DISPLAY_PORT_ARGUMENT% ^
	--publish %EMULE_BIND_ADDRESS%:%EMULE_TCP_PORT%:%EMULE_TCP_PORT%/tcp ^
	%UDP_PORT_ARGUMENT% ^
	"%IMAGE_REFERENCE%" >nul
if errorlevel 1 (
	set "ERROR_MESSAGE=Container could not be created."
	goto :fail
)

for /l %%N in (1,1,10) do (
	call :container_is_running
	if not errorlevel 1 goto :container_ready
	timeout /t 1 /nobreak >nul
)
call :read_container_state
docker logs "%CONTAINER_NAME%" 2>&1
call :set_container_startup_error
goto :fail

:container_ready
call :wait_for_tcp "%DISPLAY_CONNECT_ADDRESS%" "%DISPLAY_PORT%" 60
if errorlevel 1 (
	docker logs "%CONTAINER_NAME%" 2>&1
	set "ERROR_MESSAGE=The container is running but %DISPLAY_DESCRIPTION% is unavailable on %DISPLAY_CONNECT_ADDRESS%:%DISPLAY_PORT%."
	goto :fail
)

if /I "%CLEAN_OLD_IMAGES%"=="yes" (
	for /f "usebackq delims=" %%R in (`docker image ls --filter "reference=%DOCKER_IMAGE%:*" --format "{{.Repository}}:{{.Tag}}" 2^>nul`) do (
		if /I not "%%R"=="%IMAGE_REFERENCE%" if /I not "%%R"=="<none>:<none>" docker image rm "%%R" >nul 2>&1
	)
	if exist "%OLD_IMAGE_IDS_FILE%" (
		for /f "usebackq delims=" %%I in ("%OLD_IMAGE_IDS_FILE%") do if /I not "%%I"=="%NEW_IMAGE_ID%" docker image rm "%%I" >nul 2>&1
	)
)

if exist "%OLD_IMAGE_IDS_FILE%" del /q "%OLD_IMAGE_IDS_FILE%" >nul 2>&1
set "OLD_IMAGE_IDS_FILE="

echo.
echo eMule AI is running.
echo Container:    %CONTAINER_NAME%
echo Image source: %IMAGE_SOURCE%
echo Image:        %IMAGE_REFERENCE%
echo Image ID:     %NEW_IMAGE_ID%
echo Data:         %DATA_DIR%
echo eMule TCP:    %EMULE_BIND_ADDRESS%:%EMULE_TCP_PORT%
if "%EMULE_UDP_PORT%"=="0" (
	echo eMule UDP:    disabled
) else (
	echo eMule UDP:    %EMULE_BIND_ADDRESS%:%EMULE_UDP_PORT%
)
if /I "%DISPLAY_MODE%"=="xpra" (
	echo Xpra canvas:  %XPRA_CANVAS_WIDTH%x%XPRA_CANVAS_HEIGHT%x%DISPLAY_DEPTH%
	echo Xpra:         tcp://%DISPLAY_CONNECT_ADDRESS%:%XPRA_PORT%
	echo Password:     %XPRA_PASSWORD%
	if /I "%AUTO_ATTACH_XPRA%"=="yes" call :attach_xpra
) else (
	echo noVNC:        http://%DISPLAY_CONNECT_ADDRESS%:%NOVNC_PORT%/vnc.html?autoconnect=1^&resize=scale
)
exit /b 0

:attach_xpra
set "XPRA_EXE=%XPRA_CLIENT_EXE%"
if defined XPRA_EXE (
	for %%I in ("%XPRA_EXE%") do if /I "%%~nxI"=="Xpra_cmd.exe" if exist "%%~dpIXpra.exe" set "XPRA_EXE=%%~dpIXpra.exe"
)
if not defined XPRA_EXE if exist "%ProgramFiles%\Xpra\Xpra.exe" set "XPRA_EXE=%ProgramFiles%\Xpra\Xpra.exe"
if not defined XPRA_EXE if defined ProgramFiles(x86) if exist "%ProgramFiles(x86)%\Xpra\Xpra.exe" set "XPRA_EXE=%ProgramFiles(x86)%\Xpra\Xpra.exe"
if not defined XPRA_EXE (
	for /f "tokens=2,*" %%A in ('reg query "HKLM\Software\Xpra" /v InstallPath 2^>nul ^| find /I "InstallPath"') do if exist "%%B\Xpra.exe" set "XPRA_EXE=%%B\Xpra.exe"
)
if not defined XPRA_EXE (
	for /f "delims=" %%I in ('where Xpra.exe 2^>nul') do if not defined XPRA_EXE set "XPRA_EXE=%%I"
)
if not defined XPRA_EXE (
	echo WARNING: Xpra.exe was not found. Install the Xpra client or set XPRA_CLIENT_EXE_WINDOWS in eMuleAI.conf.
	echo Direct connection: tcp://%XPRA_USERNAME%@%DISPLAY_CONNECT_ADDRESS%:%XPRA_PORT%/
	exit /b 0
)
if not exist "%XPRA_EXE%" (
	echo WARNING: Configured Xpra desktop client was not found: %XPRA_EXE%
	exit /b 0
)
echo Starting Xpra desktop client with %XPRA_CLIENT_BACKEND% backend...
start "" "%XPRA_EXE%" attach "tcp://%XPRA_USERNAME%@%DISPLAY_CONNECT_ADDRESS%:%XPRA_PORT%/" ^
	--backend=%XPRA_CLIENT_BACKEND% ^
	--desktop-scaling=%XPRA_DESKTOP_SCALING% ^
	--challenge-handlers=env ^
	--file-transfer=no ^
	--printing=no ^
	--open-files=no
if errorlevel 1 (
	set "ERROR_MESSAGE=Xpra desktop client could not be started."
	goto :fail
)
exit /b 0

:expand_windows_path
set "EXPANDED_PATH=%~1"
if /I "%EXPANDED_PATH:~0,14%"=="{LOCALAPPDATA}" if defined LOCALAPPDATA set "EXPANDED_PATH=%LOCALAPPDATA%%EXPANDED_PATH:~14%"
if /I "%EXPANDED_PATH:~0,14%"=="{LOCALAPPDATA}" if not defined LOCALAPPDATA set "EXPANDED_PATH=%USERPROFILE%\AppData\Local%EXPANDED_PATH:~14%"
if /I "%EXPANDED_PATH:~0,13%"=="{USERPROFILE}" set "EXPANDED_PATH=%USERPROFILE%%EXPANDED_PATH:~13%"
if /I "%EXPANDED_PATH:~0,14%"=="{PROGRAMFILES}" set "EXPANDED_PATH=%ProgramFiles%%EXPANDED_PATH:~14%"
if /I "%EXPANDED_PATH:~0,17%"=="{PROGRAMFILESX86}" if defined ProgramFiles(x86) set "EXPANDED_PATH=%ProgramFiles(x86)%%EXPANDED_PATH:~17%"
if /I "%EXPANDED_PATH:~0,17%"=="{PROGRAMFILESX86}" if not defined ProgramFiles(x86) set "EXPANDED_PATH=%ProgramFiles%%EXPANDED_PATH:~17%"
exit /b 0

:is_supported_config_key
for %%K in (
	IMAGE_SOURCE REGISTRY_IMAGE REGISTRY_TAG LOCAL_IMAGE LOCAL_TAG CLEAN_OLD_IMAGES
	CONTAINER_NAME PLATFORM DISPLAY_MODE DATA_DIR_WINDOWS DATA_DIR_LINUX DATA_DIR_MACOS
	PUID_WINDOWS PGID_WINDOWS PUID_LINUX PGID_LINUX PUID_MACOS PGID_MACOS
	DISPLAY_BIND_ADDRESS DISPLAY_CONNECT_ADDRESS EMULE_BIND_ADDRESS XPRA_PORT NOVNC_PORT
	EMULE_TCP_PORT EMULE_UDP_PORT DISPLAY_WIDTH DISPLAY_HEIGHT XPRA_CANVAS_WIDTH
	XPRA_CANVAS_HEIGHT DISPLAY_DEPTH XPRA_PASSWORD XPRA_USERNAME AUTO_ATTACH_XPRA
	XPRA_CLIENT_EXE_WINDOWS XPRA_CLIENT_EXE_LINUX XPRA_CLIENT_EXE_MACOS
	XPRA_CLIENT_BACKEND_WINDOWS XPRA_DESKTOP_SCALING SHM_SIZE RESTART_POLICY
) do if /I "%~1"=="%%K" exit /b 0
exit /b 1

:require_config_value
if not defined %~1 (
	set "ERROR_MESSAGE=Configuration key %~1 is missing or empty in %CONFIG_FILE%"
	exit /b 1
)
exit /b 0

:container_is_running
set "CONTAINER_RUNNING="
for /f "usebackq delims=" %%R in (`docker container inspect --format "{{.State.Running}}" "%CONTAINER_NAME%" 2^>nul`) do set "CONTAINER_RUNNING=%%R"
if /I "%CONTAINER_RUNNING%"=="true" exit /b 0
exit /b 1

:read_container_state
set "CONTAINER_STATUS=unknown"
set "CONTAINER_EXIT_CODE=unknown"
for /f "usebackq delims=" %%R in (`docker container inspect --format "{{.State.Status}}" "%CONTAINER_NAME%" 2^>nul`) do set "CONTAINER_STATUS=%%R"
for /f "usebackq delims=" %%R in (`docker container inspect --format "{{.State.ExitCode}}" "%CONTAINER_NAME%" 2^>nul`) do set "CONTAINER_EXIT_CODE=%%R"
exit /b 0

:set_container_startup_error
if /I "%CONTAINER_STATUS%"=="running" (
	set "ERROR_MESSAGE=Container is running but its startup state could not be confirmed."
) else (
	set "ERROR_MESSAGE=Container stopped during startup. Status: %CONTAINER_STATUS%. Exit code: %CONTAINER_EXIT_CODE%."
)
exit /b 0

:wait_for_tcp
set "EMULEAI_WAIT_HOST=%~1"
set "EMULEAI_WAIT_PORT=%~2"
for /l %%N in (1,1,%~3) do (
	powershell.exe -NoLogo -NoProfile -NonInteractive -Command "$client = New-Object System.Net.Sockets.TcpClient; try { $result = $client.BeginConnect($env:EMULEAI_WAIT_HOST, [int]$env:EMULEAI_WAIT_PORT, $null, $null); if (-not $result.AsyncWaitHandle.WaitOne(1000, $false)) { exit 1 }; $client.EndConnect($result); exit 0 } catch { exit 1 } finally { $client.Close() }" >nul 2>&1
	if not errorlevel 1 exit /b 0
	timeout /t 1 /nobreak >nul
)
exit /b 1

:validate_id
call :validate_nonnegative_integer "%~1"
if errorlevel 1 exit /b 1
if %~1 GTR 2147483647 exit /b 1
exit /b 0

:validate_port
call :validate_nonnegative_integer "%~1"
if errorlevel 1 exit /b 1
if %~1 LSS 1 exit /b 1
if %~1 GTR 65535 exit /b 1
exit /b 0

:validate_udp_port
call :validate_nonnegative_integer "%~1"
if errorlevel 1 exit /b 1
if %~1 GTR 65535 exit /b 1
exit /b 0

:validate_dimension
call :validate_nonnegative_integer "%~1"
if errorlevel 1 exit /b 1
if %~1 LSS 320 exit /b 1
if %~1 GTR 8192 exit /b 1
exit /b 0

:validate_nonnegative_integer
for /f "delims=0123456789" %%A in ("%~1") do exit /b 1
if "%~1"=="" exit /b 1
exit /b 0

:fail
if defined OLD_IMAGE_IDS_FILE if exist "%OLD_IMAGE_IDS_FILE%" del /q "%OLD_IMAGE_IDS_FILE%" >nul 2>&1
echo.
echo ERROR: %ERROR_MESSAGE%
echo.
pause
exit /b %EXIT_CODE%
