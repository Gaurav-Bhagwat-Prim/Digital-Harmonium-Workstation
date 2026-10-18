# Run the Virtual Harmonium Workstation application
Set-Location -Path "$PSScriptRoot\HarmoniumWorkstation"
& "$env:USERPROFILE\.cargo\bin\cargo.exe" run
