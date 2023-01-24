@echo off

set interactive=1
echo %cmdcmdline% | find /i "%~0" > nul
if not errorlevel 1 set interactive=0

set origdir=%cd%

for /D %%d in ("%ProgramFiles%\Microsoft Visual Studio\", "%ProgramFiles(x86)%\Microsoft Visual Studio\") do (
    if exist %%d (
        cd /D %%d
        goto basedir_found
    )
)
goto try_old_versions

:basedir_found
for /D %%d in (2022\, 2019\, 2017\) do (
    if exist %%d (
        cd %%d
        goto version_found
    )
)
goto error

:version_found
for /D %%d in (Enterprise\, Professional\, Community\, BuildTools\) do (
    if exist %%d (
        cd %%d
        goto edition_found
    )
)
goto error

:edition_found
set vspath=%cd%
cd /D %origdir%

if %interactive% == 0 (
    start cmd /K "%vspath%\VC\Auxiliary\Build\vcvars64.bat"
) else (
    call "%vspath%\VC\Auxiliary\Build\vcvars64.bat"
)
goto :eof

:try_old_versions
rem VS 2015 (14.0) and older provided nice VSxxxCOMNTOOLS environment variable
rem amd64 tools are installed by default since VS 2010 (10.0)

if defined VS140COMNTOOLS (
    rem VS 2015
    if %interactive% == 0 (
        start cmd /K "%VS140COMNTOOLS%\..\..\VC\bin\amd64\vcvars64.bat"
    ) else (
        call "%VS140COMNTOOLS%\..\..\VC\bin\amd64\vcvars64.bat"
    )
) else if defined VS120COMNTOOLS (
    rem VS 2013
    if %interactive% == 0 (
        start cmd /K "%VS120COMNTOOLS%\..\..\VC\bin\amd64\vcvars64.bat"
    ) else (
        call "%VS120COMNTOOLS%\..\..\VC\bin\amd64\vcvars64.bat"
    )
) else if defined VS110COMNTOOLS (
    rem VS 2012
    if %interactive% == 0 (
        start cmd /K "%VS110COMNTOOLS%\..\..\VC\bin\amd64\vcvars64.bat"
    ) else (
        call "%VS110COMNTOOLS%\..\..\VC\bin\amd64\vcvars64.bat"
    )
) else if defined VS100COMNTOOLS (
    rem VS 2010
    if %interactive% == 0 (
        start cmd /K "%VS100COMNTOOLS%\..\..\VC\bin\amd64\vcvars64.bat"
    ) else (
        rem call "%VS100COMNTOOLS%\vsvars32.bat"
        rem call "%VS100COMNTOOLS%\..\..\VC\vcvarsall.bat" amd64
        call "%VS100COMNTOOLS%\..\..\VC\bin\amd64\vcvars64.bat"
    )
) else if defined VS90COMNTOOLS (
    rem VS 2008
    if %interactive% == 0 (
        start cmd /K "%VS90COMNTOOLS%\vsvars32.bat"
    ) else (
        call "%VS90COMNTOOLS%\vsvars32.bat"
        rem call "%VS90COMNTOOLS%\..\..\VC\vcvarsall.bat" amd64
        rem call "%VS90COMNTOOLS%\..\..\VC\bin\amd64\vcvarsamd64.bat"
    )
) else if defined VS80COMNTOOLS (
    rem VS 2005
    if %interactive% == 0 (
        start cmd /K "%VS80COMNTOOLS%\vsvars32.bat"
    ) else (
        call "%VS80COMNTOOLS%\vsvars32.bat"
    )
) else (
    goto error
)
goto :eof

:error
echo ERROR: Could not find Visual Studio
cd /D %origdir%
if %interactive% == 0 pause
goto :eof
