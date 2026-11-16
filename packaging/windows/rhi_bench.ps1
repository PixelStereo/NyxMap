# NyxMap's QRhi renderer on this Windows machine: the same pictures as OpenGL, and the cost (docs/developers/qrhi-prototype.md).
# Run RunBench.cmd (or this script) from the extracted folder. Writes report.txt next to it.
#  1. test videos made here with the bundled ffmpeg.exe (4K: H.264, HEVC 10-bit, HAP, HAP Alpha, HAP Q), and any of
#     your own put in media\yours
#  2. every shipped ISF shader baked for Direct3D, Metal and Vulkan
#  3. each video through QRhi (Direct3D 11, frames decoded on its device) and through OpenGL: pictures and cost
#  4. a whole composition (4, 9, 16 layers of 4K video, an ISF effect each, an RGBA mask, a 4K output)
param([int]$Seconds = 5)
$ErrorActionPreference = "Continue"
Set-Location $PSScriptRoot
$report = Join-Path $PSScriptRoot "report.txt"
function Log([string]$text) { $text | Tee-Object -FilePath $report -Append }

"NyxMap QRhi bench, $(Get-Date -Format s)" | Set-Content -Encoding utf8 $report
Log "Windows: $((Get-CimInstance Win32_OperatingSystem).Caption) $((Get-CimInstance Win32_OperatingSystem).Version)"
Log "CPU: $((Get-CimInstance Win32_Processor | Select-Object -First 1).Name)"
foreach ($g in Get-CimInstance Win32_VideoController) { Log "GPU: $($g.Name), driver $($g.DriverVersion)" }
Get-Content "$PSScriptRoot\BUILD.txt" -ErrorAction SilentlyContinue | ForEach-Object { Log $_ }

# 1. Test media (made once)
$media = Join-Path $PSScriptRoot "media"
New-Item -ItemType Directory -Force $media | Out-Null
$src = "gradients=s=3840x2160:d=4:r=60:speed=0.02"
$alpha = "$src,format=rgba,geq=r='r(X,Y)':g='g(X,Y)':b='b(X,Y)':a='60+180*X/W'"
# Each video: the first encoder this ffmpeg has (the bundled one is LGPL: no x264/x265, so Windows' own and the
# GPU vendors' encoders next)
$made = @(
    @{ File = "h264_4k.mp4"; Input = "$src,format=yuv420p"; Tries = @(
        @("-c:v", "libx264", "-preset", "veryfast", "-crf", "18"),
        @("-c:v", "h264_nvenc", "-b:v", "60M"), @("-c:v", "h264_amf", "-b:v", "60M"), @("-c:v", "h264_qsv", "-b:v", "60M"),
        @("-c:v", "h264_mf", "-b:v", "60M")) },
    @{ File = "hevc10_4k.mp4"; Input = $src; Tries = @(
        @("-c:v", "libx265", "-preset", "veryfast", "-crf", "18", "-pix_fmt", "yuv420p10le", "-tag:v", "hvc1"),
        @("-c:v", "hevc_nvenc", "-profile:v", "main10", "-pix_fmt", "p010le", "-b:v", "60M", "-tag:v", "hvc1"),
        @("-c:v", "hevc_amf", "-profile:v", "main10", "-pix_fmt", "p010le", "-b:v", "60M", "-tag:v", "hvc1"),
        @("-c:v", "hevc_qsv", "-profile:v", "main10", "-pix_fmt", "p010le", "-b:v", "60M", "-tag:v", "hvc1")) },
    @{ File = "hap_4k.mov"; Input = "$src,format=rgba"; Tries = @(,@("-c:v", "hap", "-format", "hap", "-chunks", "8")) },
    @{ File = "hap_alpha_4k.mov"; Input = $alpha; Tries = @(,@("-c:v", "hap", "-format", "hap_alpha", "-chunks", "8")) },
    @{ File = "hap_q_4k.mov"; Input = "$src,format=rgba"; Tries = @(,@("-c:v", "hap", "-format", "hap_q", "-chunks", "8")) }
)
$videos = @()
foreach ($m in $made) {
    $path = Join-Path $media $m.File
    foreach ($try in $m.Tries) {
        if (Test-Path $path) { break }
        Write-Host "Making $($m.File) ($($try[1]))..."
        & "$PSScriptRoot\ffmpeg.exe" -v error -y -f lavfi -i $m.Input @try $path
        if ($LASTEXITCODE -ne 0) { Remove-Item $path -ErrorAction SilentlyContinue }
    }
    if (Test-Path $path) { $videos += $path } else { Log "($($m.File): no encoder for it in this ffmpeg, left out)" }
}
# Any video of your own dropped in media\yours is played too
$yours = Join-Path $media "yours"
New-Item -ItemType Directory -Force $yours | Out-Null
$videos += @(Get-ChildItem $yours -File | ForEach-Object { $_.FullName })

# 2-4. The benches
function Bench([string]$title, [string]$exe, [string[]]$arguments) {
    Log ""
    Log "== $title"
    & "$PSScriptRoot\$exe" @arguments 2>&1 | ForEach-Object { Log "$_" }
    Log "(exit code $LASTEXITCODE)"
}
Bench "ISF shaders baked for every backend" "nyxmap_isf_rhi_test.exe" @("$PSScriptRoot\isf")
Bench "Videos: QRhi against OpenGL" "nyxmap_rhi_video_test.exe" (@("--frames", "120") + $videos)
$effect = "$PSScriptRoot\isf\effects\Kaleidoscope.fs"
foreach ($v in @("h264_4k.mp4", "hevc10_4k.mp4", "hap_q_4k.mov")) {
    $path = Join-Path $media $v
    if (-not (Test-Path $path)) { continue }
    foreach ($n in 4, 9, 16) {
        Bench "Composition: $n layers of $v, 4K output" "nyxmap_rhi_composition_test.exe" @("--layers", "$n", "--seconds", "$Seconds", "--size", "3840x2160", "--effect", $effect, $path)
    }
}
Log ""
Log "Done. Please send report.txt back."
Write-Host ""
Write-Host "Report: $report"
