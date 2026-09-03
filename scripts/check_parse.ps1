$tokens = $null
$errors = $null
$path = Join-Path $PSScriptRoot "phase4_demo.ps1"
$null = [System.Management.Automation.Language.Parser]::ParseFile($path, [ref]$tokens, [ref]$errors)
Write-Output "Found $($errors.Count) parse errors:"
foreach ($err in $errors) {
    $ln = $err.Extent.StartLineNumber
    $col = $err.Extent.StartColumnNumber
    $txt = $err.Extent.Text
    Write-Output "${ln}:${col} [$txt] $($err.Message)"
}
Write-Output "---DONE---"
