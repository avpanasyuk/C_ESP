# Windows alarm listener

One instance per Windows PC. `alarm_listener.py` serves
`GET /alarm?msg=<text>&color=<hex>&secs=<seconds>` on TCP **8765** and spawns
`popup_banner.py`, a top-most banner. bsd's `fleet_alarm.py` (this repo, one dir up) is the
caller: the message, colour and duration all come from its rules, so an alarm's look is
configured on the server, not here.

`secs=0` — the default for alarms — makes the banner **persistent until the user clicks it**
(or presses Esc/Enter/Space while it has focus). It does not close on mouse movement, and it
re-asserts topmost every 2 s so a full-screen app can't bury it.

## Run

```powershell
pythonw alarm_listener.py        # background, no console window
python  alarm_listener.py        # foreground, prints each alarm
pythonw alarm_listener.py 9000   # non-default port
```

The port must match the `"<host>:<port>"` entry in bsd's `fleet_alarm.json` `popup_targets`.

## Autostart at logon (Windows)

The banner needs an interactive desktop, so a logon-scoped task is the right scope — there is
nothing to see when nobody is logged in.

```powershell
$py = "$env:LOCALAPPDATA\Programs\Python\Python313\pythonw.exe"   # your pythonw
$L  = "<path>\windows_listener\alarm_listener.py"
$A = New-ScheduledTaskAction -Execute $py -Argument "`"$L`"" -WorkingDirectory (Split-Path $L)
$T = New-ScheduledTaskTrigger -AtLogOn -User "$env:USERDOMAIN\$env:USERNAME"
$S = New-ScheduledTaskSettingsSet -AllowStartIfOnBatteries -DontStopIfGoingOnBatteries `
        -StartWhenAvailable -RestartCount 3 -RestartInterval (New-TimeSpan -Minutes 1) `
        -ExecutionTimeLimit (New-TimeSpan -Seconds 0)      # 0 = never time out; it's a daemon
$P = New-ScheduledTaskPrincipal -UserId "$env:USERDOMAIN\$env:USERNAME" `
        -LogonType Interactive -RunLevel Limited            # must be the interactive user to draw
Register-ScheduledTask -TaskName "Fleet alarm listener" -Action $A -Trigger $T -Settings $S `
    -Principal $P -Force
Start-ScheduledTask -TaskName "Fleet alarm listener"        # start it now, without logging out
```

Registering a task for your own user needs no elevation. **The firewall rule does** — without
it Windows silently drops the server's connection:

```powershell
New-NetFirewallRule -DisplayName "Fleet alarm listener (TCP 8765)" -Direction Inbound `
    -Action Allow -Protocol TCP -LocalPort 8765 -Profile Private,Domain -RemoteAddress LocalSubnet
```

Verify from the server: `nc -z -w3 <pc> 8765`, then fire a real alarm through the daemon
(append a `LEAK` row to a throwaway `<log_dir>/WaterLeak-TEST99.csv` and delete it afterwards)
rather than only curling `/alarm` locally — that also proves DNS, firewall and the rule.
