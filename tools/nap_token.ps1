# Nap token cua kinh tu .env vao NVS cua board qua Serial — token KHONG hien ra man hinh.
#
#   powershell -ExecutionPolicy Bypass -File tools\nap_token.ps1            # COM8, .env o goc repo
#   powershell -ExecutionPolicy Bypass -File tools\nap_token.ps1 -Port COM9
#
# Firmware nhan lenh '=<token>\n' (serial_console.cpp), luu NVS, in lai DO DAI
# va mo lai phien WS. Dong Serial Monitor cua Arduino IDE truoc: no giu cong.
param(
  [string]$Port = "COM8",
  [string]$EnvFile = (Join-Path $PSScriptRoot "..\.env")
)

$line = Get-Content $EnvFile | Where-Object { $_ -match '^\s*VISIONCARE_DEVICE_CLIENT_TOKEN\s*=' } | Select-Object -First 1
if (-not $line) { Write-Error "Khong thay VISIONCARE_DEVICE_CLIENT_TOKEN trong $EnvFile"; exit 1 }
$token = ($line -split '=', 2)[1].Trim().Trim('"').Trim("'")
if (-not $token) { Write-Error "Token rong"; exit 1 }

$sp = New-Object System.IO.Ports.SerialPort $Port, 115200
$sp.NewLine = "`n"
$sp.ReadTimeout = 500
$sp.DtrEnable = $true
$sp.Open()
try {
  # Mo cong co the lam board khoi dong lai. Lenh Serial chi duoc doc khi task
  # audio da chay (dong "Giu nut GPIO..."), nen thay board dang boot thi doi.
  $booting = $false
  $deadline = (Get-Date).AddSeconds(3)
  while ((Get-Date) -lt $deadline) {
    try {
      $l = $sp.ReadLine()
      if ($l -match 'YOUR EYES') { $booting = $true; $deadline = (Get-Date).AddSeconds(45) }
      if ($booting -and $l -match 'Giu nut GPIO') { break }
    } catch [System.TimeoutException] { }
  }
  $sp.DiscardInBuffer()
  $sp.Write("=" + $token + "`n")
  $token = $null
  $deadline = (Get-Date).AddSeconds(6)
  while ((Get-Date) -lt $deadline) {
    try {
      $l = $sp.ReadLine().TrimEnd()
      # Chi in dong lien quan; firmware khong bao gio in token.
      if ($l -match 'token|Token|NVS|phien|Phien|hello|WS') { Write-Output $l }
      if ($l -match 'Da (luu|xoa) token|KHONG luu|HONG') { break }
    } catch [System.TimeoutException] { }
  }
} finally {
  $sp.Close()
}
