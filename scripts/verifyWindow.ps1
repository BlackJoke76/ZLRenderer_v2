param(
    [Parameter(Mandatory=$true)][string]$executablePath,
    [string]$logDirectory,
    [switch]$closeWhileMinimized,
    [ValidateRange(10,60)][int]$durationSeconds = 10,
    [ValidateSet('', 'alternate-window', 'fifo-window')][string]$testCase = ''
)
$ErrorActionPreference = 'Stop'
Add-Type -AssemblyName System.Drawing
Add-Type @"
using System;
using System.Runtime.InteropServices;
public static class WindowVerification {
    [StructLayout(LayoutKind.Sequential)] public struct Rect { public int left, top, right, bottom; }
    [StructLayout(LayoutKind.Sequential)] public struct Point { public int x, y; }
    [DllImport("user32.dll")] public static extern bool IsWindowVisible(IntPtr windowHandle);
    [DllImport("user32.dll")] public static extern bool SetProcessDPIAware();
    [DllImport("user32.dll")] public static extern bool IsIconic(IntPtr windowHandle);
    [DllImport("user32.dll")] public static extern bool GetWindowRect(IntPtr windowHandle, out Rect rect);
    [DllImport("user32.dll")] public static extern bool GetClientRect(IntPtr windowHandle, out Rect rect);
    [DllImport("user32.dll")] public static extern bool ClientToScreen(IntPtr windowHandle, ref Point point);
    [DllImport("user32.dll")] public static extern bool SetForegroundWindow(IntPtr windowHandle);
    [DllImport("user32.dll")] public static extern bool MoveWindow(IntPtr windowHandle, int x, int y, int width, int height, bool repaint);
    [DllImport("user32.dll")] public static extern bool ShowWindow(IntPtr windowHandle, int command);
    [DllImport("user32.dll")] public static extern IntPtr SendMessageTimeout(IntPtr windowHandle, uint message, IntPtr firstParameter, IntPtr secondParameter, uint flags, uint timeout, out IntPtr result);
    [DllImport("user32.dll")] public static extern bool PostMessage(IntPtr windowHandle, uint message, IntPtr firstParameter, IntPtr secondParameter);
}
"@
[void][WindowVerification]::SetProcessDPIAware() # Keep capture and Win32 geometry in physical pixels.
$resolvedExecutablePath = (Resolve-Path -LiteralPath $executablePath).Path
if (-not $logDirectory) { $logDirectory = Join-Path (Split-Path $resolvedExecutablePath) 'window-logs' }
[void](New-Item -ItemType Directory -Path $logDirectory -Force)
$resolvedLogDirectory = (Resolve-Path -LiteralPath $logDirectory).Path
$stdoutPath = Join-Path $resolvedLogDirectory 'window.stdout.log'
$stderrPath = Join-Path $resolvedLogDirectory 'window.stderr.log'
$script:colorEvidence = @()
function assertClientColor([IntPtr]$windowHandle, [string]$stage, [int[]]$expectedRgb) {
    [void][WindowVerification]::SetForegroundWindow($windowHandle)
    Start-Sleep -Milliseconds 400
    $clientRect = [WindowVerification+Rect]::new()
    $clientOrigin = [WindowVerification+Point]::new()
    if (-not [WindowVerification]::GetClientRect($windowHandle, [ref]$clientRect) -or
        -not [WindowVerification]::ClientToScreen($windowHandle, [ref]$clientOrigin)) { throw 'Client area query failed.' }
    $clientWidth = $clientRect.right - $clientRect.left
    $clientHeight = $clientRect.bottom - $clientRect.top
    if ($clientWidth -le 0 -or $clientHeight -le 0) { throw 'Cannot sample zero client area.' }
    $clientBitmap = [Drawing.Bitmap]::new($clientWidth, $clientHeight)
    $captureGraphics = [Drawing.Graphics]::FromImage($clientBitmap)
    try {
        $captureGraphics.CopyFromScreen($clientOrigin.x, $clientOrigin.y, 0, 0, $clientBitmap.Size)
        $clientBitmap.Save((Join-Path $resolvedLogDirectory ($stage + '.png')), [Drawing.Imaging.ImageFormat]::Png)
        foreach ($horizontalFraction in @(0.25,0.5,0.75)) {
            foreach ($verticalFraction in @(0.25,0.5,0.75)) {
                $pixelColor = $clientBitmap.GetPixel([int]($clientWidth * $horizontalFraction), [int]($clientHeight * $verticalFraction))
                $sampleRgb = @([int]$pixelColor.R, [int]$pixelColor.G, [int]$pixelColor.B)
                for ($channelIndex = 0; $channelIndex -lt 3; $channelIndex++) {
                    if ([Math]::Abs($sampleRgb[$channelIndex] - $expectedRgb[$channelIndex]) -gt 8) {
                        throw "Clear color mismatch at ${stage}: actual=$sampleRgb expected=$expectedRgb; inspect saved PNG."
                    }
                }
            }
        }
        $script:colorEvidence += "${stage}: client=${clientWidth}x${clientHeight}; 9 samples RGB=$($sampleRgb -join ','); expected=$($expectedRgb -join ',')"
    } finally { $captureGraphics.Dispose(); $clientBitmap.Dispose() }
}
function toDisplayByte([double]$linearValue, [bool]$isSrgb) {
    $encodedValue = $linearValue
    if ($isSrgb) {
        $encodedValue = if ($linearValue -le 0.0031308) { 12.92 * $linearValue } else { 1.055 * [Math]::Pow($linearValue, 1.0 / 2.4) - 0.055 }
    }
    return [int][Math]::Round(255 * $encodedValue)
}
# Restrict settings to this child process; do not modify registry/global layer configuration.
$previousSyncSetting = [Environment]::GetEnvironmentVariable('VK_LAYER_VALIDATE_SYNC', 'Process')
$previousTestCase = [Environment]::GetEnvironmentVariable('ZL_VULKAN_TEST_CASE', 'Process')
try {
    $env:VK_LAYER_VALIDATE_SYNC = '1'
    if ($testCase) { $env:ZL_VULKAN_TEST_CASE = $testCase } else { Remove-Item Env:ZL_VULKAN_TEST_CASE -ErrorAction SilentlyContinue }
    # Hide the helper console; GLFW's application window is visible for the runtime acceptance test.
    $process = Start-Process -FilePath $resolvedExecutablePath -WindowStyle Hidden -PassThru -RedirectStandardOutput $stdoutPath -RedirectStandardError $stderrPath
    $retainedProcessHandle = $process.Handle
} finally {
    [Environment]::SetEnvironmentVariable('VK_LAYER_VALIDATE_SYNC', $previousSyncSetting, 'Process')
    [Environment]::SetEnvironmentVariable('ZL_VULKAN_TEST_CASE', $previousTestCase, 'Process')
}
try {
    $initializationObserved = $false
    for ($attempt = 0; $attempt -lt 150; $attempt++) {
        Start-Sleep -Milliseconds 100
        $process.Refresh()
        if ($process.HasExited) { throw "Program exited before verification: $(Get-Content -LiteralPath $stderrPath -Raw)" }
        $standardOutput = Get-Content -LiteralPath $stdoutPath -Raw
        if ($standardOutput -match 'RHI initialization succeeded' -and $process.MainWindowHandle -ne [IntPtr]::Zero) {
            $initializationObserved = $true
            break
        }
    }
    if (-not $initializationObserved) { throw 'RHI initialization/window timed out.' }
    $windowHandle = $process.MainWindowHandle
    if ($process.MainWindowTitle -ne 'ZLRenderer' -or -not [WindowVerification]::IsWindowVisible($windowHandle)) { throw 'Visible titled window not found.' }
    [void][WindowVerification]::MoveWindow($windowHandle, 100, 100, 900, 600, $true)
    Start-Sleep -Milliseconds 400
    $standardOutput = Get-Content -LiteralPath $stdoutPath -Raw
    $colorMatch = [regex]::Match($standardOutput, 'clearColor=\(([\d.]+),([\d.]+),([\d.]+),([\d.]+)\)')
    $formatMatch = [regex]::Match($standardOutput, 'mode=(MAILBOX|FIFO) format=(\d+)')
    if (-not $colorMatch.Success -or -not $formatMatch.Success) { throw 'Color/format evidence missing.' }
    $selectedFormat = [int]$formatMatch.Groups[2].Value
    # B8G8R8A8 / R8G8B8A8, UNORM or SRGB. Other encodings require manual screenshot review.
    if ($selectedFormat -notin @(37,43,44,50)) { throw "Pixel verifier does not support format $selectedFormat; review screenshots manually." }
    $expectedRgb = @(1..3 | ForEach-Object {
        toDisplayByte ([double]::Parse($colorMatch.Groups[$_].Value, [Globalization.CultureInfo]::InvariantCulture)) ($selectedFormat -in @(43,50))
    })
    assertClientColor $windowHandle 'initial' $expectedRgb
    Start-Sleep -Seconds $durationSeconds
    assertClientColor $windowHandle 'sustained' $expectedRgb
    foreach ($windowSize in @(@(1100,700),@(720,480),@(900,600))) {
        if (-not [WindowVerification]::MoveWindow($windowHandle, 100, 100, $windowSize[0], $windowSize[1], $true)) { throw 'Move/resize failed.' }
        assertClientColor $windowHandle ("resize-$($windowSize[0])x$($windowSize[1])") $expectedRgb
    }
    $messageResult = [IntPtr]::Zero
    if ([WindowVerification]::SendMessageTimeout($windowHandle, 0, [IntPtr]::Zero, [IntPtr]::Zero, 2, 2000, [ref]$messageResult) -eq [IntPtr]::Zero) { throw 'Window unresponsive.' }
    [void][WindowVerification]::ShowWindow($windowHandle, 6)
    Start-Sleep -Milliseconds 600
    if (-not [WindowVerification]::IsIconic($windowHandle)) { throw 'Minimize not observed.' }
    $standardOutput = Get-Content -LiteralPath $stdoutPath -Raw
    if ($standardOutput -notmatch 'Presentation paused: minimized/zero framebuffer') { throw 'Pause path not observed.' }
    if (-not $closeWhileMinimized) {
        [void][WindowVerification]::ShowWindow($windowHandle, 9)
        assertClientColor $windowHandle 'restored' $expectedRgb
        if ((Get-Content -LiteralPath $stdoutPath -Raw) -notmatch 'Presentation resumed') { throw 'Resume path not observed.' }
    }
    if (-not [WindowVerification]::PostMessage($windowHandle, 0x112, [IntPtr]0xF060, [IntPtr]::Zero)) { throw 'System close command failed.' }
    if (-not $process.WaitForExit(5000)) { throw 'Window close did not exit.' }
    $process.WaitForExit()
    if ($process.ExitCode -ne 0) { throw "Unexpected exit code: $($process.ExitCode)" }
    $standardOutput = Get-Content -LiteralPath $stdoutPath -Raw
    $frameSummary = [regex]::Match($standardOutput, 'Frame summary: submitted=(\d+) presented=(\d+) generations=(\d+)')
    if (-not $frameSummary.Success -or [long]$frameSummary.Groups[1].Value -lt 12 -or [long]$frameSummary.Groups[2].Value -lt 12 -or [int]$frameSummary.Groups[3].Value -lt 3) { throw 'Continuous frame/rebuild evidence missing.' }
    $actualCleanup = @([regex]::Matches($standardOutput, 'Destroyed [^\r\n]+|Terminated GLFW') | ForEach-Object { $_.Value }) -join ';'
    if ($actualCleanup -ne 'Destroyed device;Destroyed surface;Destroyed debug messenger;Destroyed instance;Destroyed window;Terminated GLFW') { throw "Cleanup order/count mismatch: $actualCleanup" }
    foreach ($resourceName in @('command pool','acquire semaphore','submission fence','swapchain','render pass','image view','framebuffer','present semaphore')) {
        $createCount = [regex]::Matches($standardOutput, "Created $resourceName[\r\n]").Count
        $releaseCount = [regex]::Matches($standardOutput, "Released $resourceName[\r\n]").Count
        if ($createCount -ne $releaseCount) { throw "Unbalanced ${resourceName}: $createCount vs $releaseCount" }
    }
    $standardError = Get-Content -LiteralPath $stderrPath -Raw
    $environmentPatterns = Get-Content -LiteralPath (Join-Path $PSScriptRoot '../tests/knownEnvironmentDiagnostics.txt')
    foreach ($diagnosticLine in ($standardError -split '\r?\n')) {
        if ($diagnosticLine -notmatch '\[Vulkan (WARNING|ERROR)\]') { continue }
        $knownEnvironmentMessage = $false
        foreach ($environmentPattern in $environmentPatterns) {
            if ($diagnosticLine -match ('^' + $environmentPattern + '$')) { $knownEnvironmentMessage = $true; break }
        }
        if (-not $knownEnvironmentMessage) { throw "Unexpected Vulkan diagnostic: $diagnosticLine" }
    }
    $verificationEvidence = @("VK_LAYER_VALIDATE_SYNC=1 supplied to child process; test executable also uses explicit layer settings when available", $frameSummary.Value, "mode=$($formatMatch.Groups[1].Value); format=$selectedFormat; closeWhileMinimized=$closeWhileMinimized") + $script:colorEvidence
    $verificationEvidence | Set-Content -LiteralPath (Join-Path $resolvedLogDirectory 'verification.txt') -Encoding UTF8
    "PASS: sustained clear color; repeated resize; minimized/restore or minimized close; exit=0; balanced resources; no application validation WARNING/ERROR. $($frameSummary.Value). Logs: $resolvedLogDirectory"
} finally {
    if (-not $process.HasExited) { [void]$process.CloseMainWindow(); [void]$process.WaitForExit(5000) }
    $process.Dispose()
}
