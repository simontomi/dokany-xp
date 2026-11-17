# Runs kd.exe (Debugging Tools 6.12 of the WDK 7.1) attached to a VirtualBox
# VM's COM1 named pipe, for testing the XP build. Everything kd prints goes to
# -Log; commands written into -CmdFile (one per line) are sent to kd, so a
# break-in (bugcheck, assert) can be examined without a console:
#   echo "!analyze -v" > <CmdFile>
# Usage:
#   powershell -File kdhost.ps1 -Pipe \\.\pipe\vbox-xp-com1 -Log xp-kd.log
#       -CmdFile xp-kd.cmd -SymPath <folder with dokan2.pdb>
param(
    [Parameter(Mandatory = $true)][string]$Pipe,
    [Parameter(Mandatory = $true)][string]$Log,
    [Parameter(Mandatory = $true)][string]$CmdFile,
    [string]$SymPath = '',
    [string]$Kd = 'C:\WinDDK\7600.16385.1\Debuggers\kd.exe'
)

$psi = New-Object System.Diagnostics.ProcessStartInfo
$psi.FileName = $Kd
$args = "-k com:pipe,port=$Pipe,resets=0,reconnect -logo `"$Log`""
if ($SymPath) { $args += " -y `"$SymPath`"" }
$psi.Arguments = $args
$psi.UseShellExecute = $false
$psi.RedirectStandardInput = $true
$p = [System.Diagnostics.Process]::Start($psi)
"KDHOST started kd pid $($p.Id): $args"
while (-not $p.HasExited) {
    if (Test-Path $CmdFile) {
        $lines = @(Get-Content $CmdFile)
        Remove-Item $CmdFile -Force
        foreach ($line in $lines) {
            "KDHOST > $line"
            $p.StandardInput.WriteLine($line)
        }
        $p.StandardInput.Flush()
    }
    Start-Sleep -Milliseconds 300
}
"KDHOST kd ended with $($p.ExitCode)"
