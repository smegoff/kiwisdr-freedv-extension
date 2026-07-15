param(
  [Parameter(Mandatory)][string]$Api,
  [Parameter(Mandatory)][string]$Node,
  [Parameter(Mandatory)][int]$Vmid,
  [Parameter(Mandatory)][string]$Storage,
  [Parameter(Mandatory)][string]$Template,
  [Parameter(Mandatory)][string]$Mac,
  [string]$Hostname = 'freedv-decoder',
  [string]$Bridge = 'vmbr0',
  [ValidateRange(1, 64)][int]$Cores = 2,
  [ValidateRange(512, 131072)][int]$MemoryMB = 2048,
  [ValidateRange(8, 1024)][int]$DiskGB = 16
)
$ErrorActionPreference = 'Stop'
if (-not $env:PVE_PASSWORD) { throw 'Set PVE_PASSWORD in the current process environment' }
if ($Mac -notmatch '^(?i:[0-9a-f]{2}:){5}[0-9a-f]{2}$') { throw 'Mac must be six hexadecimal octets' }

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

$task = Invoke-PveCurl POST "nodes/$Node/lxc" @{
  vmid=$Vmid; hostname=$Hostname; ostemplate=$Template
  storage=$Storage; rootfs="${Storage}:${DiskGB}"; cores=$Cores; memory=$MemoryMB; swap=0
  unprivileged=1; onboot=1; start=0; net0="name=eth0,bridge=$Bridge,firewall=1,hwaddr=$Mac,ip=dhcp,type=veth"
  features='nesting=0'; tags='freedv;kiwisdr'
}
"creation_task=$task"
"Reserve MAC $Mac in DHCP before starting decoder LXC $Vmid."
