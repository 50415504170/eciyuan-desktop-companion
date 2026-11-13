param(
    [Parameter(Mandatory = $true)][ValidateSet('Inspect','Enable','Restore')][string]$Action,
    [Parameter(Mandatory = $true)][string]$EciyuanExe
)
$ErrorActionPreference = 'Stop'
$runtime = Split-Path -Parent $MyInvocation.MyCommand.Path
$report = Join-Path $runtime 'result.txt'
$utf8 = New-Object System.Text.UTF8Encoding $true
function FileHash([string]$Path) {
    $stream = [IO.File]::OpenRead($Path)
    $algorithm = [Security.Cryptography.SHA256]::Create()
    try { return [BitConverter]::ToString($algorithm.ComputeHash($stream)).Replace('-', '') }
    finally { $stream.Dispose(); $algorithm.Dispose() }
}
try {
    $executable = (Resolve-Path -LiteralPath $EciyuanExe).ProviderPath
    if ([IO.Path]::GetFileName($executable) -ine 'Eciyuan.exe') { throw '请选择 Eciyuan.exe。' }
    $folder = Split-Path -Parent $executable
    $expectedExe = 'F3C5771A2B058552294C0FA6FB2A27D13E5998A54FA8B7D69621DE2AF8A859F7'
    $expectedZlib = 'DC2DB882C915EA54B95C59B03867ABE18D28404BA2420E51D93B9C2A4B575801'
    if ((FileHash $executable) -ne $expectedExe) {
        throw '不支持此主程序。目前仅支持经过验证的 0.1.15.624；未作任何更改。'
    }
    $library = Join-Path $folder 'zlib1.dll'
    $original = Join-Path $folder 'zlib1_real.dll'
    $payload = Join-Path $runtime 'capture.dll'
    $anchor = Join-Path $runtime 'Valorant.exe'
    $backup = if (Test-Path -LiteralPath $original) { $original } else { $library }
    if ((FileHash $backup) -ne $expectedZlib) {
        throw '原版 zlib 校验失败，拒绝覆盖。请恢复官方安装文件后重试。'
    }
    if ($Action -eq 'Inspect') {
        [IO.File]::WriteAllText($report, '兼容性校验通过：鹅次元 0.1.15.624。未修改文件或重启程序。', $utf8)
        exit 0
    }
    $identity = [Security.Principal.WindowsIdentity]::GetCurrent()
    if (-not ([Security.Principal.WindowsPrincipal]$identity).IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)) {
        throw '此操作需要管理员权限。'
    }
    if ($Action -eq 'Enable') {
        if (-not (Test-Path -LiteralPath $payload)) { throw '补丁文件缺失，请重新解压工具。' }
        if (-not (Test-Path -LiteralPath $anchor)) { throw '共享入口文件缺失，请重新解压工具。' }
        $active = Get-Process Eciyuan -ErrorAction SilentlyContinue | Where-Object { $_.Path -ieq $executable }
        if ($active -and (FileHash $library) -eq (FileHash $payload)) {
            Start-Process -FilePath $anchor -WorkingDirectory $runtime
            [IO.File]::WriteAllText($report, '已恢复共享窗口；未重启鹅次元，也未主动挂断当前通话。', $utf8)
            exit 0
        }
        if (-not (Test-Path -LiteralPath $original)) {
            Copy-Item -LiteralPath $library -Destination $original -ErrorAction Stop
        }
    }
    Get-Process Eciyuan -ErrorAction SilentlyContinue | Where-Object { $_.Path -ieq $executable } | Stop-Process -Force
    Start-Sleep -Seconds 2
    if ($Action -eq 'Enable') {
        try {
            Copy-Item -LiteralPath $payload -Destination $library -Force
            $env:QT_MEDIA_BACKEND = 'windows'
            Start-Process -FilePath $anchor -WorkingDirectory $runtime
            Start-Process -FilePath $executable -WorkingDirectory $folder
        } catch {
            Copy-Item -LiteralPath $original -Destination $library -Force
            throw
        }
        [IO.File]::WriteAllText($report, '已安装并重启鹅次元。请确认已进入陪玩；保持共享小窗口可见。画面识别效果以实际对话为准。', $utf8)
    } else {
        Get-Process Valorant -ErrorAction SilentlyContinue | Where-Object { $_.Path -ieq $anchor } | Stop-Process -Force
        Copy-Item -LiteralPath $original -Destination $library -Force
        Remove-Item Env:QT_MEDIA_BACKEND -ErrorAction SilentlyContinue
        Start-Process -FilePath $executable -WorkingDirectory $folder
        [IO.File]::WriteAllText($report, '已恢复原版 zlib 并重启。原版备份仍然保留；本工具不修改游戏名单。', $utf8)
    }
    exit 0
} catch {
    [IO.File]::WriteAllText($report, ('失败：' + $_.Exception.Message), $utf8)
    exit 1
}
