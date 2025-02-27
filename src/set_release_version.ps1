param (
    [string]$arg1
)

$arg1 = $arg1 -replace "[^0-9.]" , ''
$splitVersion = $arg1.split(".")
$major = If (!$splitVersion[0]) { 99 } Else { $splitVersion[0] }
$minor = If (!$splitVersion[1]) { 0 } Else { $splitVersion[1] }
$maintenance = If (!$splitVersion[2]) { 0 } Else { $splitVersion[2] }
$reshade = If (!$splitVersion[3]) { 0 } Else { $splitVersion[3] }

$guard = "#pragma once"
$restVersion = "#define REST_VERSION ${major},${minor},${maintenance},${reshade}"
$restVersionString = "#define REST_VERSION_STRING `"${major}.${minor}.${maintenance}.${reshade}`""
$versionFile = "${guard}`r`n${restVersion}`r`n${restVersionString}"

"${versionFile}" | Out-File -FilePath $PSScriptRoot\version.h