param(
  [Parameter(Mandatory)][string]$Kiwi,
  [Parameter(Mandatory)][string]$KiwiHostKey,
  [string]$BackupDestination = '',
  [string]$OutputRoot = "$PSScriptRoot/../backups"
)
$ErrorActionPreference = 'Stop'
if (-not $env:KIWI_PASSWORD) { throw 'Set KIWI_PASSWORD in the current process environment' }

$stamp = (Get-Date).ToUniversalTime().ToString('yyyyMMddTHHmmssZ')
$out = Join-Path $OutputRoot "kiwi-config-$stamp"
New-Item -ItemType Directory -Force -Path $out | Out-Null
$archive = Join-Path $out 'kiwi.config.tgz'
$stderr = Join-Path $out 'stream.stderr.txt'
$plink = 'C:\Program Files\PuTTY\plink.exe'
$hostKey = $KiwiHostKey

# Redirect native stdout directly to the archive so PowerShell never decodes or
# rewrites the gzip bytes. Nothing is staged on the Kiwi filesystem.
$arguments = @(
  '-batch', '-ssh', '-hostkey', $hostKey, '-l', 'root',
  '-pw', $env:KIWI_PASSWORD, $Kiwi,
  'set -e; test -d /root/kiwi.config; tar -C /root -czf - kiwi.config'
)
$process = Start-Process -FilePath $plink -ArgumentList $arguments -Wait -PassThru `
  -RedirectStandardOutput $archive -RedirectStandardError $stderr -NoNewWindow
if ($process.ExitCode -ne 0) {
  throw "Kiwi configuration stream failed with exit code $($process.ExitCode)"
}

$entries = @(& tar -tzf $archive 2>$null)
if ($LASTEXITCODE -ne 0 -or $entries.Count -lt 5 -or $entries[0] -ne 'kiwi.config/') {
  throw 'Kiwi configuration archive failed structural validation'
}
$hash = (Get-FileHash $archive -Algorithm SHA256).Hash.ToLowerInvariant()
$statusRaw = & $plink -batch -ssh -hostkey $hostKey -l root -pw $env:KIWI_PASSWORD `
  $Kiwi 'wget -q -T 5 -O - http://127.0.0.1:8073/status'
if ($LASTEXITCODE -ne 0) { throw 'Unable to record Kiwi firmware status' }
$status = @($statusRaw | Where-Object { $_ -match '^(sdr_hw|status)=' })
if ($status.Count -lt 2) { throw 'Kiwi firmware status is incomplete' }

@{
  captured_utc = $stamp
  kiwi = $Kiwi
  backup_destination = $BackupDestination
  ssh_host_key = $hostKey
  archive = 'kiwi.config.tgz'
  archive_sha256 = $hash
  archive_entries = $entries.Count
  firmware_status = ($status -join '; ')
  purpose = 'streamed pre-change Kiwi configuration backup'
  physical_recovery = 'not available until a supported backup microSD is created'
} | ConvertTo-Json | Out-File (Join-Path $out 'manifest.json') -Encoding utf8

"$hash  kiwi.config.tgz" | Out-File (Join-Path $out 'SHA256SUMS.txt') -Encoding ascii
if ((Get-Item $stderr).Length -eq 0) { Remove-Item -LiteralPath $stderr }
Write-Output $out
