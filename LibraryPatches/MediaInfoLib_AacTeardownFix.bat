@echo off
setlocal
pushd "%~dp0.."
git apply "LibraryPatches\MediaInfoLib_AacTeardownFix.patch"
popd
endlocal
