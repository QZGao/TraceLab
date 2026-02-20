param(
    [string]$Distro = ""
)

$ErrorActionPreference = "Stop"

$repoWin = (Resolve-Path (Join-Path $PSScriptRoot "..")).Path

if ($repoWin -match "^[A-Za-z]:\\") {
    $drive = $repoWin.Substring(0, 1).ToLowerInvariant()
    $rest = $repoWin.Substring(2).Replace('\', '/')
    $repoWsl = "/mnt/$drive$rest"
} else {
    throw "Unsupported repository path for WSL conversion: $repoWin"
}

$repoWslEscaped = $repoWsl.Replace("'", "'""'""'")
$inner = "cd '$repoWslEscaped' && bash ./scripts/wsl_local_tests.sh"

if ([string]::IsNullOrWhiteSpace($Distro)) {
    & wsl bash -lc $inner
} else {
    & wsl -d $Distro bash -lc $inner
}

exit $LASTEXITCODE
