@echo off
setlocal
pushd "%~dp0.."
git apply "LibraryPatches\ResizableLib_FixMemoryLeak.patch"
popd
endlocal
