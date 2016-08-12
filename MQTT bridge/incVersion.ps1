write-host "incVersion"
$file = "user/include/version.h"
if (!(Test-Path $file)) {
    write-host $File" doesn't exist"
    exit
}
$fileVersion = Get-Content $file -encoding utf8| Out-String
$fv = $fileVersion -match '"[^"]*"' # Get string between '"'
$fv = $matches[0].replace('"', '').split('.') # Remove quotes and split on'.'
if ($Args.Length -eq 0) {
    $i = $fv.GetUpperBound(0) # If no level specified use lowest (rightmost) part
} else {
    $i = [int]$Args[0] # Otherwise use 
}
if ($i -le $fv.GetUpperBound(0)) {
    $fv[$i] = ([int]$fv[$i] + 1)
    for ($j=$i+1; $j -le $fv.GetUpperBound(0); $j++) {
        $fv[$j] = 0 # Reset lower levels to 0
    }
} else {
    write-host "Wrong level: "$i
    exit
}
'char *version = "' + [string]::Join(".", $fv) + '";' | Out-File $file -encoding utf8
Get-Content $file | write-host 