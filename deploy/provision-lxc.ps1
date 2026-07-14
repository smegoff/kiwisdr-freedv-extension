param(
  [string]$Api = 'https://192.168.10.1:8006/api2/json',
  [string]$Node = 'pve01',
  [int]$Vmid = 112
)
$ErrorActionPreference = 'Stop'
if (-not $env:PVE_PASSWORD) { throw 'Set PVE_PASSWORD in the current process environment' }

function Invoke-PveCurl([string]$Method, [string]$Path, [hashtable]$Fields = @{}) {
  $args = @('-sS','-k','-X',$Method)
  if ($script:Cookie) { $args += @('-H', "Cookie: $script:Cookie") }
  if ($script:Csrf -and $Method -ne 'GET') { $args += @('-H', "CSRFPreventionToken: $script:Csrf") }
  foreach ($item in $Fields.GetEnumerator()) { $args += @('--data-urlencode', "$($item.Key)=$($item.Value)") }
  $raw = & curl.exe @args "$Api/$Path"
  if ($LASTEXITCODE) { throw "Proxmox API transport failed: $Path" }
  $json = $raw | ConvertFrom-Json
  if ($json.errors) { throw ($json.errors | ConvertTo-Json -Compress) }
  return $json.data
}

$auth = Invoke-PveCurl POST 'access/ticket' @{username='root@pam'; password=$env:PVE_PASSWORD}
$script:Cookie = "PVEAuthCookie=$($auth.ticket)"
$script:Csrf = $auth.CSRFPreventionToken

$existing = Invoke-PveCurl GET 'cluster/resources?type=vm' | Where-Object vmid -eq $Vmid
if ($existing) { throw "VMID $Vmid already exists" }

$mac = '02:46:52:44:56:70'
$task = Invoke-PveCurl POST "nodes/$Node/lxc" @{
  vmid=$Vmid; hostname='freedv-decoder'; ostemplate='Templates:vztmpl/debian-12-standard_12.7-1_amd64.tar.zst'
  storage='store2-256'; rootfs='store2-256:16'; cores=2; memory=2048; swap=0
  unprivileged=1; onboot=1; start=0; net0="name=eth0,bridge=vmbr0,firewall=1,hwaddr=$mac,ip=dhcp,type=veth"
  features='nesting=0'; tags='freedv;kiwisdr'
}
"creation_task=$task"
"Reserve MAC $mac in the router DHCP service before starting CT $Vmid."
