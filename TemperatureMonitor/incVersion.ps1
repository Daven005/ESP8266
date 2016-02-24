write-host "incVersion"
$file = "include/version.h"
if (!(Test-Path $file)) {
    write-host $File" doesn't exist"
    exit
}
$fileVersion = Get-Content $file -encoding utf8| Out-String
$fv = $fileVersion -match '"[^"]*"'
$fv = $matches[0].replace('"', '').split('.')
if ($Args.Length -eq 0) {
    $i = $fv.GetUpperBound(0)
} else {
    $i = [int]$Args[0]
}
if ($i -le $fv.GetUpperBound(0)) {
    $fv[$i] = ([int]$fv[$i] + 1)
    for ($j=$i+1; $j -le $fv.GetUpperBound(0); $j++) {
        $fv[$j] = 0
    }
} else {
    write-host "Wrong level: "$i
    exit
}
'char *version = "' + [string]::Join(".", $fv) + '";' | Out-File $file -encoding utf8
Get-Content $file | write-host 