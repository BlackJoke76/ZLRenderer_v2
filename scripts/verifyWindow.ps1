param(
    [Parameter(Mandatory=$true)][string]$executablePath,
    [string]$logDirectory
)
$ErrorActionPreference = 'Stop'
Add-Type @'
using System;
using System.Runtime.InteropServices;
public static class WindowVerification {
    [StructLayout(LayoutKind.Sequential)] public struct Rect { public int left, top, right, bottom; }
    [DllImport("user32.dll")] public static extern bool IsWindowVisible(IntPtr windowHandle);
    [DllImport("user32.dll")] public static extern bool GetWindowRect(IntPtr windowHandle, out Rect rect);
    [DllImport("user32.dll")] public static extern bool MoveWindow(IntPtr windowHandle, int x, int y, int width, int height, bool repaint);
    [DllImport("user32.dll")] public static extern IntPtr SendMessageTimeout(IntPtr windowHandle, uint message, IntPtr firstParameter, IntPtr secondParameter, uint flags, uint timeout, out IntPtr result);
    [DllImport("user32.dll")] public static extern bool PostMessage(IntPtr windowHandle, uint message, IntPtr firstParameter, IntPtr secondParameter);
}
'@
$resolvedExecutablePath = (Resolve-Path -LiteralPath $executablePath).Path
if (-not $logDirectory) { $logDirectory = Join-Path (Split-Path $resolvedExecutablePath) 'window-logs' }
[void](New-Item -ItemType Directory -Path $logDirectory -Force)
$resolvedLogDirectory = (Resolve-Path -LiteralPath $logDirectory).Path
$stdoutPath = Join-Path $resolvedLogDirectory 'window.stdout.log'
$stderrPath = Join-Path $resolvedLogDirectory 'window.stderr.log'
# The application window is intentionally visible for this integration test.
$process = Start-Process -FilePath $resolvedExecutablePath -PassThru -RedirectStandardOutput $stdoutPath -RedirectStandardError $stderrPath
$processHandle = $process.Handle # Retain the process handle so Windows PowerShell can read ExitCode after exit.
try {
    $initializationObserved = $false
    for ($attempt = 0; $attempt -lt 150; $attempt++) {
        Start-Sleep -Milliseconds 100
        $process.Refresh()
        if ($process.HasExited) { throw "Program exited before verification: $(Get-Content -LiteralPath $stderrPath -Raw)" }
        $standardOutput = Get-Content -LiteralPath $stdoutPath -Raw
        if ($standardOutput -match 'Vulkan initialization succeeded' -and $process.MainWindowHandle -ne [IntPtr]::Zero) {
            $initializationObserved = $true
            break
        }
    }
    if (-not $initializationObserved) { throw 'Vulkan initialization/window timed out.' }
    $windowHandle = $process.MainWindowHandle
    if ($process.MainWindowTitle -ne 'ZLRenderer' -or -not [WindowVerification]::IsWindowVisible($windowHandle)) { throw 'Visible titled window not found.' }
    if (-not [WindowVerification]::MoveWindow($windowHandle, 100, 100, 900, 600, $true)) { throw 'Move/resize failed.' }
    Start-Sleep -Milliseconds 300
    $resizedRect = [WindowVerification+Rect]::new()
    if (-not [WindowVerification]::GetWindowRect($windowHandle, [ref]$resizedRect)) { throw 'Window rectangle query failed.' }
    if ($resizedRect.left -ne 100 -or $resizedRect.top -ne 100 -or ($resizedRect.right - $resizedRect.left) -ne 900 -or ($resizedRect.bottom - $resizedRect.top) -ne 600) { throw 'Move/resize not observed.' }
    $messageResult = [IntPtr]::Zero
    if ([WindowVerification]::SendMessageTimeout($windowHandle, 0, [IntPtr]::Zero, [IntPtr]::Zero, 2, 2000, [ref]$messageResult) -eq [IntPtr]::Zero) { throw 'Window unresponsive.' }
    # SC_CLOSE uses the title-bar close button's system command path; no simulated mouse click.
    if (-not [WindowVerification]::PostMessage($windowHandle, 0x112, [IntPtr]0xF060, [IntPtr]::Zero)) { throw 'System close command failed.' }
    if (-not $process.WaitForExit(5000)) { throw 'Window close did not exit.' }
    $process.WaitForExit()
    if ($process.ExitCode -ne 0) { throw "Unexpected exit code: $($process.ExitCode)" }
    $standardOutput = Get-Content -LiteralPath $stdoutPath -Raw
    if ($standardOutput -notmatch 'validation enabled: VK_LAYER_KHRONOS_validation' -or
        $standardOutput -notmatch 'Selected device: .+; graphicsFamily=\d+ presentFamily=\d+' -or
        $standardOutput -notmatch 'Queues: graphics=.+ present=.+ [(]index=0[)]') { throw 'Initialization evidence missing.' }
    $actualCleanup = @([regex]::Matches($standardOutput, 'Destroyed [^\r\n]+|Terminated GLFW') | ForEach-Object { $_.Value }) -join ';'
    $expectedCleanup = 'Destroyed device;Destroyed surface;Destroyed debug messenger;Destroyed instance;Destroyed window;Terminated GLFW'
    if ($actualCleanup -ne $expectedCleanup) { throw "Cleanup order/count mismatch: $actualCleanup" }
    $standardError = Get-Content -LiteralPath $stderrPath -Raw
    $unexpectedDiagnostics = [string]$standardError
    $knownEnvironmentPatterns = Get-Content -LiteralPath (Join-Path $PSScriptRoot '../tests/knownEnvironmentDiagnostics.txt')
    foreach ($environmentPattern in $knownEnvironmentPatterns) {
        foreach ($diagnosticMatch in [regex]::Matches($unexpectedDiagnostics, $environmentPattern)) {
            "Known environment diagnostic preserved: $($diagnosticMatch.Value)"
        }
        $unexpectedDiagnostics = [regex]::Replace($unexpectedDiagnostics, $environmentPattern, '')
    }
    if ($unexpectedDiagnostics -match '\[Vulkan (WARNING|ERROR)\]') { throw "Vulkan diagnostic requires review: $unexpectedDiagnostics" }
    "PASS: Vulkan initialized; visible; move/resize 900x600; responsive; SC_CLOSE; exit=0; exact cleanup order; no application validation WARNING/ERROR. Logs: $resolvedLogDirectory"
} finally {
    if (-not $process.HasExited) {
        [void]$process.CloseMainWindow()
        [void]$process.WaitForExit(5000)
    }
    $process.Dispose()
}
