function getLevel($level) {
	$version = $level.toLower()
 	if ($version -match '^\d+') {
		$i = [int]$version
	} elseif ($version -eq "major") {
		$i = 0
	} elseif ($version -eq "minor") {
		$i = 1
	} elseif ($version -eq "patch") {
		$i = 2
	} elseif ($version -eq "build") {
		$i = 3
	} elseif ($version -eq "check") {
		$i = 9
	} else {
		write-host "Bad level: "$level
		$i = 99
	}
	$i
}

function makeVersionString($fv) {
    [string]$fv[0]+"."+$fv[1]+"."+$fv[2]+"+"+$fv[3]
}

if (!($Args[0] -and $Args[1])) {
    write-host "Need versionLevel and file arguments"
    exit
}
$file = $Args[1]
if (!(Test-Path $file)) {
    write-host $File" doesn't exist"
    exit
}
Try 
{
	$i = getLevel $Args[0]
	$fileVersion = Get-Content $file -encoding utf8| Out-String
	$fv = $fileVersion -match '"[^"]*"'
	$fv = $matches[0].replace('"', '').split('.')
	$bv = $fv[$fv.GetUpperBound(0)].split('+')
	if ($bv.Length -ge 2) {
		$fv[2] = $bv[0]
		$fv += $bv[1]
	}
	if ($i -le $fv.GetUpperBound(0)) {
	    $fv[$i] = ([int]$fv[$i] + 1)
	    for ($j=$i+1; $j -le $fv.GetUpperBound(0); $j++) {
	        $fv[$j] = 0
	    }
	} else {
    	    if ($i -eq 9) {
		$v = makeVersionString($fv)
    	        write-host "V: "$v
            } else {
	    	write-host "Wrong level: "$i" in "$fileVersion
	    }		
	    exit
	}
	$v = makeVersionString $fv
	'char *version = "' + $v + '";' | Out-File $file -encoding utf8
	Get-Content $file | write-host 
}
Catch 
{
	write-host $_.Exception.Message"--"$_.Exception.ItemName
}
