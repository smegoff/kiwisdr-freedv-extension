param(
  [Parameter(Mandatory)][string]$Api,
  [Parameter(Mandatory)][string]$Node,
  [Parameter(Mandatory)][int]$Vmid,
  [Parameter(Mandatory)][ValidateNotNullOrEmpty()][string[]]$KeepSnapshots,
  [string]$Username = 'root@pam',
  [switch]$Apply
)

$ErrorActionPreference = 'Stop'

function Invoke-PveApi(
  [Parameter(Mandatory)][string]$Method,
  [Parameter(Mandatory)][string]$Path,
  [hashtable]$Fields = @{}
) {
  $args = @('-sS', '-k', '-X', $Method)
  if ($script:TokenHeader) { $args += @('-H', $script:TokenHeader) }
  if ($script:Cookie) { $args += @('-H', "Cookie: $script:Cookie") }
  if ($script:Csrf -and $Method -ne 'GET') {
    $args += @('-H', "CSRFPreventionToken: $script:Csrf")
  }
  foreach ($item in $Fields.GetEnumerator()) {
    $args += @('--data-urlencode', "$($item.Key)=$($item.Value)")
  }
  $raw = & curl.exe @args "$($Api.TrimEnd('/'))/$Path"
  if ($LASTEXITCODE) { throw "Proxmox API transport failed: $Path" }
  $json = $raw | ConvertFrom-Json
  if ($json.errors) { throw ($json.errors | ConvertTo-Json -Compress) }
  return $json.data
}

function Wait-PveTask([Parameter(Mandatory)][string]$Upid) {
  $task = [uri]::EscapeDataString($Upid)
  $deadline = (Get-Date).AddMinutes(2)
  do {
    $status = Invoke-PveApi GET "nodes/$Node/tasks/$task/status"
    if ($status.status -eq 'stopped') {
      if ($status.exitstatus -ne 'OK') {
        throw "Snapshot deletion task failed: $($status.exitstatus)"
      }
      return
    }
    Start-Sleep -Seconds 1
  } while ((Get-Date) -lt $deadline)
  throw "Timed out waiting for snapshot deletion task $Upid"
}

$tokenId = $env:PVE_API_TOKEN_ID
$tokenSecret = $env:PVE_API_TOKEN_SECRET
if ($tokenId -or $tokenSecret) {
  if (-not ($tokenId -and $tokenSecret)) {
    throw 'Set both PVE_API_TOKEN_ID and PVE_API_TOKEN_SECRET'
  }
  $script:TokenHeader = "Authorization: PVEAPIToken=$tokenId=$tokenSecret"
} else {
  if (-not $env:PVE_PASSWORD) {
    throw 'Set PVE_API_TOKEN_ID/PVE_API_TOKEN_SECRET or PVE_PASSWORD'
  }
  $auth = Invoke-PveApi POST 'access/ticket' @{
    username = $Username
    password = $env:PVE_PASSWORD
  }
  $script:Cookie = "PVEAuthCookie=$($auth.ticket)"
  $script:Csrf = $auth.CSRFPreventionToken
}

$snapshots = @(Invoke-PveApi GET "nodes/$Node/lxc/$Vmid/snapshot")
$names = @($snapshots | ForEach-Object name | Where-Object { $_ -and $_ -ne 'current' })
$missing = @($KeepSnapshots | Where-Object { $_ -notin $names })
if ($missing) { throw "Required keep snapshot is missing: $($missing -join ', ')" }

$remove = @($names | Where-Object { $_ -notin $KeepSnapshots } | Sort-Object)
"retained=$($KeepSnapshots -join ',')"
"remove_count=$($remove.Count)"
$remove | ForEach-Object { "remove=$_" }

if (-not $Apply) {
  'dry_run=true; rerun with -Apply after reviewing the list'
  exit 0
}

foreach ($name in $remove) {
  $encoded = [uri]::EscapeDataString($name)
  "deleting=$name"
  $upid = Invoke-PveApi DELETE "nodes/$Node/lxc/$Vmid/snapshot/$encoded"
  Wait-PveTask $upid
}

$remaining = @(Invoke-PveApi GET "nodes/$Node/lxc/$Vmid/snapshot" |
  ForEach-Object name | Where-Object { $_ -and $_ -ne 'current' } | Sort-Object)
if (@($remaining | Where-Object { $_ -notin $KeepSnapshots })) {
  throw "Unexpected snapshots remain: $($remaining -join ', ')"
}
"remaining=$($remaining -join ',')"
