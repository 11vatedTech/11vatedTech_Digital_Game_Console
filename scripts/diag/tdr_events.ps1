$since = (Get-Date).AddHours(-3)
$events = Get-WinEvent -FilterHashtable @{LogName='System'; StartTime=$since} -ErrorAction SilentlyContinue |
    Where-Object { $_.ProviderName -match 'Display|nvlddmkm|igfx|amdwddmg|Watchdog' -or $_.Id -in 4101,141,153 }
if (-not $events) { Write-Output "NO_DISPLAY_EVENTS_IN_WINDOW"; exit 0 }
foreach ($e in $events) {
    $msg = $e.Message
    if ($msg -and $msg.Length -gt 240) { $msg = $msg.Substring(0, 240) }
    Write-Output ("{0} | id={1} | {2} | {3}" -f $e.TimeCreated, $e.Id, $e.ProviderName, $msg)
}
