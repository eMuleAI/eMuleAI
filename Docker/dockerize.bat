@echo off
setlocal EnableExtensions EnableDelayedExpansion

rem Docker Hub namespace and repository used for image tags.
set "IMAGE_NAME=emuleai/emuleai"
set "EXIT_CODE=1"

set "SCRIPT_DIR=%~dp0"
for %%I in ("%SCRIPT_DIR%..") do set "REPOSITORY_DIR=%%~fI"
for %%I in ("%REPOSITORY_DIR%\..\eMuleAI_Releases\_Template") do set "TEMPLATE_DIR=%%~fI"
set "DEFAULT_EXE_RELATIVE=..\_Build\eMuleAI\Release\x64\eMuleAI.exe"
for %%I in ("%SCRIPT_DIR%%DEFAULT_EXE_RELATIVE%") do set "DEFAULT_EXE_PATH=%%~fI"

set "EXE_INPUT=%~1"
if defined EXE_INPUT goto :exe_input_ready
echo.
echo Default: %DEFAULT_EXE_RELATIVE%
set /p "EXE_INPUT=Enter the path to the x64 eMuleAI.exe or press Enter for the default: "
if not defined EXE_INPUT set "EXE_INPUT=%DEFAULT_EXE_PATH%"

:exe_input_ready
set "EXE_INPUT=%EXE_INPUT:"=%"
for %%I in ("%EXE_INPUT%") do set "EXE_PATH=%%~fI"

if not exist "%EXE_PATH%" (
	echo ERROR: eMuleAI executable was not found:
	echo        %EXE_PATH%
	set "EXIT_CODE=2"
	goto :final_exit
)

for %%I in ("%EXE_PATH%") do set "EXE_NAME=%%~nxI"
if /I not "!EXE_NAME!"=="eMuleAI.exe" (
	echo ERROR: The executable path must point to the x64 eMuleAI.exe file.
	set "EXIT_CODE=2"
	goto :final_exit
)

where powershell.exe >nul 2>&1
if errorlevel 1 (
	echo ERROR: Windows PowerShell was not found.
	set "EXIT_CODE=2"
	goto :final_exit
)

set "EMULEAI_EXE_PATH=%EXE_PATH%"
set "VERSION_OUTPUT=%TEMP%\eMuleAI-Version-%RANDOM%-%RANDOM%.txt"
powershell.exe -NoLogo -NoProfile -NonInteractive ^
	-Command "$value = [System.Diagnostics.FileVersionInfo]::GetVersionInfo($env:EMULEAI_EXE_PATH).ProductVersion; if ([string]::IsNullOrWhiteSpace($value)) { exit 4 }; [Console]::WriteLine($value.Trim())" ^
	> "%VERSION_OUTPUT%" 2>nul
set "VERSION_READ_RESULT=!ERRORLEVEL!"
set "RAW_VERSION="
if "!VERSION_READ_RESULT!"=="0" set /p "RAW_VERSION="<"%VERSION_OUTPUT%"
if exist "%VERSION_OUTPUT%" del /q "%VERSION_OUTPUT%" >nul 2>&1
set "VERSION_OUTPUT="
set "EMULEAI_EXE_PATH="

if not "!VERSION_READ_RESULT!"=="0" (
	echo ERROR: ProductVersion could not be read from:
	echo        %EXE_PATH%
	set "EXIT_CODE=2"
	goto :final_exit
)
set "VERSION=!RAW_VERSION!"
set "VERSION_VALID=0"
echo(!VERSION!| findstr /R /X /C:"[0-9][0-9]*\.[0-9][0-9]*\.[0-9][0-9]*" >nul
if not errorlevel 1 set "VERSION_VALID=1"
echo(!VERSION!| findstr /R /X /C:"[0-9][0-9]*\.[0-9][0-9]*\.[0-9][0-9]*-[0-9A-Za-z][0-9A-Za-z.-]*" >nul
if not errorlevel 1 set "VERSION_VALID=1"
if "!VERSION_VALID!"=="0" (
	echo ERROR: The executable ProductVersion has an unsupported eMule AI release format:
	echo        !RAW_VERSION!
	echo        Expected format: 1.5.1 or 1.6.0-beta1
	echo        Update EMULEAI_VERSION in srchybrid\Opcodes.h and rebuild the executable.
	set "EXIT_CODE=2"
	goto :final_exit
)

echo.
echo Detected eMule AI version: !VERSION!

set "PUBLISH_INPUT=%~2"
if defined PUBLISH_INPUT goto :publish_input_ready
set /p "PUBLISH_INPUT=Publish eMule AI !VERSION! to Docker Hub? [y/N]: "

:publish_input_ready
if not defined PUBLISH_INPUT set "PUBLISH_INPUT=N"
set "PUBLISH_IMAGE="
if /I "%PUBLISH_INPUT%"=="Y" set "PUBLISH_IMAGE=1"
if /I "%PUBLISH_INPUT%"=="YES" set "PUBLISH_IMAGE=1"
if /I "%PUBLISH_INPUT%"=="PUBLISH" set "PUBLISH_IMAGE=1"
if /I "%PUBLISH_INPUT%"=="N" set "PUBLISH_IMAGE=0"
if /I "%PUBLISH_INPUT%"=="NO" set "PUBLISH_IMAGE=0"
if /I "%PUBLISH_INPUT%"=="LOCAL" set "PUBLISH_IMAGE=0"
if not defined PUBLISH_IMAGE (
	echo ERROR: The second parameter must be publish or local.
	set "EXIT_CODE=2"
	goto :final_exit
)

set "BUILD_CONTEXT=%TEMP%\eMuleAI-Docker-%RANDOM%-%RANDOM%"

where docker >nul 2>&1
if errorlevel 1 (
	echo ERROR: Docker CLI was not found in PATH.
	goto :cleanup
)

docker info >nul 2>&1
if errorlevel 1 (
	echo ERROR: Docker Desktop is not running or the Docker engine is unavailable.
	goto :cleanup
)

for /f "usebackq delims=" %%I in (`docker info --format "{{.OSType}}" 2^>nul`) do set "DOCKER_OS=%%I"
if /I not "!DOCKER_OS!"=="linux" (
	echo ERROR: Docker Desktop must use Linux containers.
	goto :cleanup
)

docker buildx version >nul 2>&1
if errorlevel 1 (
	echo ERROR: Docker Buildx is unavailable.
	goto :cleanup
)

if not exist "%TEMPLATE_DIR%\" (
	echo ERROR: Release template directory was not found:
	echo        %TEMPLATE_DIR%
	goto :cleanup
)

if "%PUBLISH_IMAGE%"=="1" (
	echo.
	echo Sign in to Docker Hub to publish %IMAGE_NAME%.
	docker login
	if errorlevel 1 (
		echo ERROR: Docker Hub login failed or was cancelled.
		goto :cleanup
	)
)

if exist "%BUILD_CONTEXT%" rmdir /s /q "%BUILD_CONTEXT%"
mkdir "%BUILD_CONTEXT%\app"
if errorlevel 1 (
	echo ERROR: Could not create temporary build context.
	goto :cleanup
)

copy /y "%SCRIPT_DIR%Dockerfile" "%BUILD_CONTEXT%\Dockerfile" >nul
if errorlevel 1 goto :copy_error
copy /y "%SCRIPT_DIR%entrypoint.sh" "%BUILD_CONTEXT%\entrypoint.sh" >nul
if errorlevel 1 goto :copy_error
copy /y "%SCRIPT_DIR%configure-settings.py" "%BUILD_CONTEXT%\configure-settings.py" >nul
if errorlevel 1 goto :copy_error
copy /y "%SCRIPT_DIR%.dockerignore" "%BUILD_CONTEXT%\.dockerignore" >nul
if errorlevel 1 goto :copy_error

robocopy "%TEMPLATE_DIR%" "%BUILD_CONTEXT%\app" /E /COPY:DAT /DCOPY:DAT /R:2 /W:1 /NFL /NDL /NJH /NJS /NP >nul
set "ROBOCOPY_RESULT=!ERRORLEVEL!"
if !ROBOCOPY_RESULT! GEQ 8 (
	echo ERROR: Could not copy the release template. Robocopy exit code: !ROBOCOPY_RESULT!
	goto :cleanup
)

copy /y "%EXE_PATH%" "%BUILD_CONTEXT%\app\eMuleAI.exe" >nul
if errorlevel 1 (
	echo ERROR: Could not copy eMuleAI.exe into the build context.
	goto :cleanup
)

set "IS_STABLE_VERSION=0"
echo(!VERSION!| findstr /R /X /C:"[0-9][0-9]*\.[0-9][0-9]*\.[0-9][0-9]*" >nul
if not errorlevel 1 set "IS_STABLE_VERSION=1"

set "TAG_ARGS=--tag %IMAGE_NAME%:!VERSION!"
set "DISPLAY_TAGS=%IMAGE_NAME%:!VERSION!"
if "%PUBLISH_IMAGE%"=="1" (
	if "!IS_STABLE_VERSION!"=="1" (
		for /f "tokens=1,2 delims=." %%A in ("!VERSION!") do set "MINOR_VERSION=%%A.%%B"
		set "TAG_ARGS=!TAG_ARGS! --tag %IMAGE_NAME%:!MINOR_VERSION! --tag %IMAGE_NAME%:latest"
		set "DISPLAY_TAGS=!DISPLAY_TAGS!, %IMAGE_NAME%:!MINOR_VERSION!, %IMAGE_NAME%:latest"
	)
) else (
	set "TAG_ARGS=!TAG_ARGS! --tag %IMAGE_NAME%:local"
	set "DISPLAY_TAGS=!DISPLAY_TAGS!, %IMAGE_NAME%:local"
)

echo.
echo Docker image:    %IMAGE_NAME%:!VERSION!
echo Source template: %TEMPLATE_DIR%
echo Executable:      %EXE_PATH%
echo Tags:            !DISPLAY_TAGS!
if "%PUBLISH_IMAGE%"=="1" (
	echo Mode:            Publish to Docker Hub
) else (
	echo Mode:            Local test build
)
echo.

if "%PUBLISH_IMAGE%"=="1" (
	docker buildx build ^
		--platform linux/amd64 ^
		--pull ^
		--build-arg "EMULEAI_VERSION=!VERSION!" ^
		!TAG_ARGS! ^
		--push ^
		"%BUILD_CONTEXT%"
) else (
	docker buildx build ^
		--platform linux/amd64 ^
		--pull ^
		--build-arg "EMULEAI_VERSION=!VERSION!" ^
		!TAG_ARGS! ^
		--load ^
		"%BUILD_CONTEXT%"
)
if errorlevel 1 (
	if "%PUBLISH_IMAGE%"=="1" (
		echo ERROR: Docker image build or Docker Hub push failed.
		echo        Verify that the signed-in Docker Hub account can push to %IMAGE_NAME%.
	) else (
		echo ERROR: Local Docker image build failed.
	)
	goto :cleanup
)

if "%PUBLISH_IMAGE%"=="1" (
	echo.
	echo Published Docker Hub image details:
	docker buildx imagetools inspect "%IMAGE_NAME%:!VERSION!"
	if errorlevel 1 (
		echo WARNING: The image was pushed, but its published details could not be inspected.
	)
) else (
	echo.
	echo Local test image:
	docker image ls "%IMAGE_NAME%"
	if errorlevel 1 (
		echo WARNING: The local image was built, but its details could not be displayed.
	)
	echo NOTE: Existing local version tags are preserved. Docker Desktop may still show older tags.
)

set "EXIT_CODE=0"
goto :cleanup

:copy_error
echo ERROR: Could not prepare the temporary Docker build context.
goto :cleanup

:cleanup
if defined VERSION_OUTPUT if exist "%VERSION_OUTPUT%" del /q "%VERSION_OUTPUT%" >nul 2>&1
if defined BUILD_CONTEXT if exist "%BUILD_CONTEXT%" rmdir /s /q "%BUILD_CONTEXT%"
if "%EXIT_CODE%"=="0" (
	echo.
	if "%PUBLISH_IMAGE%"=="1" (
		echo Docker Hub publication completed successfully.
	) else (
		echo Local Docker image build completed successfully.
		echo Test tag: %IMAGE_NAME%:local
	)
) else (
	echo.
	if "%PUBLISH_IMAGE%"=="1" (
		echo Docker Hub publication failed.
	) else (
		echo Local Docker image build failed.
	)
)
:final_exit
if not "%EXIT_CODE%"=="0" (
	echo.
	pause
)
exit /b %EXIT_CODE%
