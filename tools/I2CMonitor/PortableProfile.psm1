#requires -Version 5.1
# Private codec/serial layer. The public module functions provide user help and
# policy; this file owns format validation, byte encoding and bounded replies.
# IPRF numbers are little-endian; CRC covers the body, not the header. USB PRF1
# uses a per-connection token so stale replies cannot acknowledge a new upload.
$ErrorActionPreference='Stop'
Import-Module (Join-Path $PSScriptRoot 'Schema.psm1') -Force
. (Join-Path $PSScriptRoot 'ReportSchema.ps1')
function Convert-ProfileInteger($Value,[int]$Maximum=65535){
    $s=[string]$Value
    if($s -match '^0x([0-9A-Fa-f]+)$'){$n=[Convert]::ToInt64($Matches[1],16)}
    elseif($s -match '^\d+$'){$n=[long]$s}else{throw "Invalid unsigned integer: $s"}
    if($n -lt 0 -or $n -gt $Maximum){throw "Integer out of range: $s"};[int]$n
}
if (!('PortableCrc' -as [type])) {
    Add-Type -TypeDefinition @'
public static class PortableCrc {
    public static uint Compute(byte[] data, int offset, int count) {
        uint c=0xffffffff; for(int i=offset;i<offset+count;i++) {
            c ^= data[i]; for(int b=0;b<8;b++)c=(c>>1)^((c&1)!=0?0xedb88320u:0u);
        } return ~c;
    }
}
'@
}
function Convert-PortableText([string]$Text) {
    $Text=$Text.Replace([string][char]0x2013,'-').Replace([string][char]0x2014,'-').Replace([string][char]0x00D7,'x')
    return $Text.Replace('**','').Replace('`','').Trim()
}
function Convert-PortableProfile([string]$Path) {
    # Decode YAML into the validated model, then serialize the format contract.
    # Keep schema checks here aligned with the firmware loader's rejection rules.
    $Path=$ExecutionContext.SessionState.Path.GetUnresolvedProviderPathFromPSPath($Path)
    $profile=Import-DeviceProfile $Path; $src=$profile.Source; $device=$src.device
    if ([string]$device.id -cnotmatch '^[a-z][a-z0-9_.-]{0,47}$') {throw 'Invalid device.id (maximum 48 characters).'}
    if (!$device.name -or ([string]$device.name).Length -gt 80 -or $device.name -match '[\x00-\x1f]') {throw 'Invalid device.name.'}
    $revision=if($device.Contains('profile_revision')){Convert-ProfileInteger $device.profile_revision}else{1}
    if(!$revision){throw 'profile_revision must be positive.'}
    $addresses=if($device.Contains('typical_addresses')){@($device.typical_addresses | ForEach-Object {Convert-ProfileInteger $_ 127})}else{@($profile.Address)}
    if(!$addresses.Count -or $addresses.Count -gt 112 -or @($addresses | Select-Object -Unique).Count -ne $addresses.Count -or @($addresses | Where-Object {$_ -lt 8 -or $_ -ge 120}).Count){throw 'Invalid typical_addresses.'}
    $protocol=0
    if($src.transport.active_read){$a=$src.transport.active_read
        if($a.protocol -ne 'single_byte_pointer_restart' -or $a.clock_Hz -ne 10000 -or $a.max_registers -ne 3){throw 'Unsupported active-read contract.'};$protocol=1}
    $stream=New-Object IO.MemoryStream; $script:writer=New-Object IO.BinaryWriter($stream)
    function B($v){$script:writer.Write([byte]$v)}
    function W($v){$script:writer.Write([uint16]$v)}
    function D($v){$script:writer.Write([uint32]$v)}
    function F($v){$n=[double]$v;if([double]::IsNaN($n)-or[double]::IsInfinity($n)){throw 'Non-finite number.'};$script:writer.Write($n)}
    function T($v,[int]$max=4096){$s=Convert-PortableText ([string]$v);if($s.Contains([string][char]0)){throw 'NUL in text.'};$b=[Text.Encoding]::UTF8.GetBytes($s);if($b.Length -gt $max){throw "Text exceeds $max UTF-8 bytes: $($s.Substring(0,[Math]::Min(40,$s.Length)))"};W ($b.Length+1);$script:writer.Write($b);B 0}
    function Count($values,[int]$maximum){$n=@($values).Count;if($n -gt $maximum){throw "Collection exceeds $maximum entries."};W $n}
    function Reference($s,[switch]$AllowAll){
        if($AllowAll -and $s -ceq 'All'){return}
        if([string]$s -cnotmatch '^0x([0-9A-Fa-f]{2})_([01]{8}|[01]{16}|[01]{24})$'){throw "Invalid property reference: $s"}
        $reg=[Convert]::ToInt32($Matches[1],16);$bits=$Matches[2];$mask=[Convert]::ToInt32($bits,2)
        $found=@($profile.Properties | Where-Object {$_.Registers[0] -eq $reg -and $_.Mask -eq $mask -and $_.Registers.Count*8 -eq $bits.Length})
        if($found.Count -ne 1){throw "Reference must resolve to exactly one property: $s"}
    }
    try {
        T $device.id 48;T $device.name 80;W $revision;B $addresses.Count
        foreach($a in $addresses){B $a};B $protocol;T $src.provenance.reference_url
        Count $profile.Properties 512
        if(!$profile.Properties.Count){throw 'Empty property list.'}
        foreach($item in $profile.Properties){$p=$item.Definition
            T $p.id 48;T $p.name 192;T $p.unit 64;T $p.note
            T $item.BitMeanings[0].status 8;T $item.BitMeanings[1].status 8
            T $item.BitMeanings[0].interpretation;T $item.BitMeanings[1].interpretation
            T $p.description;T $p.friendlydesc 2048
            $access=if($p.access){$p.access}else{if($p.operation -eq 'read_write'){'R/W'}else{'R'}}
            T ($access.Replace(' (inferred)',', inferred')) 64
            B $profile.Address;B $item.Registers[0];B $(if($item.Registers.Count -gt 1){$item.Registers[-1]}else{0})
            B $item.Registers.Count;B $item.Shift;B $item.Precision
            D $item.Mask;D $item.Minimum;D $item.Maximum;F $item.Scale;F $item.Offset
            $step=0
            if($p.Contains('atomic_step')){
                if($p.atomic_step -isnot [ValueType] -or $p.atomic_step -is [bool]){throw "Non-numeric atomic_step: $($p.id)"}
                $step=[double]$p.atomic_step
                if($step -lt 0 -or ($step -gt 0 -and ($item.BitMeanings.Count -or $item.Enum.Count -or $p.encoding -eq 'ascii' -or $p.alternatives))){throw "Invalid atomic_step: $($p.id)"}
            };F $step
            $flags=0;if($p.operation -eq 'read_write'){$flags=$flags -bor 1};if($p.encoding -eq 'ascii'){$flags=$flags -bor 2}
            if($p.alternatives){$flags=$flags -bor 4};if($p.encoding -eq 'unsigned_code_le'){$flags=$flags -bor 8}
            if($protocol -eq 1){$flags=$flags -bor 16};if($item.BitMeanings.Count){$flags=$flags -bor 32};B $flags
            $codes=@($item.Enum.Keys | Sort-Object);Count $codes 256;foreach($code in $codes){D $code;T $item.Enum[$code] 192}
        }
        $pages=@(Get-ReportSchema $src);Count $pages 8
        foreach($page in $pages){T $page.header 192;Count $page.groups 16
            foreach($group in $page.groups){T $group.header 192;Count $group.items 512;foreach($ref in $group.items){Reference $ref -AllowAll;T $ref 40}}}
        $rules=@($src.identification | Where-Object {$null -ne $_});Count $rules 32
        foreach($r in $rules){$kind=([string]$r.class).ToUpperInvariant()
            if($kind -notin @('A','B','C') -or !$r.source -or !$r.label){throw 'Identification needs class A/B/C, label and source.'}
            if($kind -eq 'A' -and ($r.exclusive_verified -isnot [bool] -or !$r.exclusive_verified)){throw 'Class A requires exclusive_verified: true.'}
            if($kind -eq 'B' -and ($r.mandatory_verified -isnot [bool] -or !$r.mandatory_verified)){throw 'Class B requires mandatory_verified: true.'}
            $mask=Convert-ProfileInteger $r.mask 255;$min=Convert-ProfileInteger $r.min 255;$max=Convert-ProfileInteger $r.max 255
            if(!$mask -or $min -gt $max){throw 'Invalid identification range.'}
            T $r.label 192;B (Convert-ProfileInteger $r.register 255);B $mask;B $min;B $max;B (@('A','B','C').IndexOf($kind))
        }
        $graphs=@($src.graphs | Where-Object {$null -ne $_});Count $graphs 8
        foreach($g in $graphs){if(!$g.header -or !$g.lines.Count -or $g.lines.Count -gt 8){throw 'Graph requires header and 1..8 lines.'};T $g.header 192;B $g.lines.Count
            foreach($l in $g.lines){Reference $l.property;T $l.property 40;F $l.min;F $l.max
                if([double]$l.min -ge [double]$l.max){throw 'Invalid graph range.'}
                if($null -ne $l.area -and $l.area -isnot [bool]){throw 'Graph area must be boolean.'}
                D (Convert-ProfileInteger $l.color 16777215);B ([bool]$l.area)}}
        $filters=@($src.filter_lists | Where-Object {$null -ne $_});Count $filters 8;$names=@{}
        foreach($f in $filters){if(!$f.name -or $names.ContainsKey([string]$f.name)){throw 'Invalid/duplicate filter name.'};$names[[string]$f.name]=$true
            $regs=@($f.registers | ForEach-Object {Convert-ProfileInteger $_ 255})
            if(!$regs.Count -or @($regs | Select-Object -Unique).Count -ne $regs.Count){throw 'Filter requires unique registers.'}
            T $f.name 192;Count $regs 128;foreach($reg in $regs){B $reg}}
        $metadata=[ordered]@{provenance=$src.provenance;transport=$src.transport;hardware=$src.hardware;policy=$src.policy;identification=$src.identification}
        T ($metadata | ConvertTo-Json -Depth 30 -Compress) 16384
        $script:writer.Flush();$body=$stream.ToArray()
        if($body.Length+24 -gt 393216){throw 'Package exceeds 384 KiB.'}
        $crc=[PortableCrc]::Compute($body,0,$body.Length)
        $result=New-Object IO.MemoryStream;$header=New-Object IO.BinaryWriter($result)
        $header.Write([Text.Encoding]::ASCII.GetBytes('IPRF'));$header.Write([uint16]1);$header.Write([uint16]24)
        $header.Write([uint32]($body.Length+24));$header.Write([uint32]$crc);$header.Write([uint32]0);$header.Write([uint32]0);$header.Write($body);$header.Flush()
        $bytes=$result.ToArray();$header.Dispose();$result.Dispose()
        return [pscustomobject]@{Id=[string]$device.id;Name=[string]$device.name;Revision=$revision;Crc=$crc;Bytes=$bytes;Path=$Path;Properties=$profile.Properties.Count}
    } finally {$script:writer.Dispose();$stream.Dispose();$script:writer=$null}
}
function Open-ProfilePort([string]$Port) {
    # Explicit named COM selection wins; otherwise discover candidates. Avoid
    # asserting an unidentified serial port belongs to the tablet without consent.
    if(!$Port){
        $ports=@([IO.Ports.SerialPort]::GetPortNames() | Sort-Object)
        if(!$ports.Count){throw 'No COM ports detected. Connect the tablet USB port.'}
        $devices=@();try{$devices=@(Get-CimInstance Win32_PnPEntity -Filter "Name LIKE '%(COM%'" -ErrorAction Stop)}catch{}
        $preferred=@($ports | Where-Object {$name=$_;@($devices | Where-Object {$_.Name -match "\($([regex]::Escape($name))\)" -and $_.PNPDeviceID -match 'VID_303A'}).Count})
        if($preferred.Count -eq 1){$Port=$preferred[0]}else{
            $choices=if($preferred.Count){$preferred}else{$ports}
            for($i=0;$i -lt $choices.Count;$i++){$name=$choices[$i];$friendly=@($devices | Where-Object {$_.Name -match "\($([regex]::Escape($name))\)"} | Select-Object -First 1);Write-Host ('{0}: {1} {2}' -f ($i+1),$name,$friendly.Name)}
            $answer=Read-Host 'Select tablet port number';$index=0
            if(![int]::TryParse($answer,[ref]$index) -or $index -lt 1 -or $index -gt $choices.Count){throw 'Invalid port selection.'};$Port=$choices[$index-1]
        }
        Write-Host "Using $Port (tablet identity will be verified)."
    }
    if($Port -notmatch '^COM[1-9][0-9]*$'){throw 'Port must be a COM name, for example COM7.'}
    $serial=New-Object IO.Ports.SerialPort($Port,115200,[IO.Ports.Parity]::None,8,[IO.Ports.StopBits]::One)
    $serial.DtrEnable=$false;$serial.RtsEnable=$false;$serial.ReadTimeout=500;$serial.WriteTimeout=3000;$serial.NewLine="`n"
    $serial.Encoding=New-Object Text.UTF8Encoding($false)
    try{$serial.Open()}catch{$serial.Dispose();throw}
    $link=[pscustomobject]@{Serial=$serial;Token=([Guid]::NewGuid().ToString('N').Substring(0,8));Partial='';Port=$Port}
    return $link
}
function Send-ProfileCommand($Link,[string]$Command){$Link.Serial.WriteLine("PRF1 $($Link.Token) $Command")}
function Receive-ProfileReply($Link,[int]$TimeoutSeconds=10){
    # Preserve a partial line across reads and ignore other tokens/boot chatter.
    # The timeout bounds the complete reply, not just the next available byte.
    $timer=[Diagnostics.Stopwatch]::StartNew();$prefix="PRF1 $($Link.Token) "
    while($timer.Elapsed.TotalSeconds -lt $TimeoutSeconds){
        $chunk=$Link.Serial.ReadExisting()
        if($chunk){$Link.Partial+=$chunk}
        while(($at=$Link.Partial.IndexOf("`n")) -ge 0){$line=$Link.Partial.Substring(0,$at).TrimEnd("`r");$Link.Partial=$Link.Partial.Substring($at+1)
                if($line.StartsWith($prefix)){ $reply=$line.Substring($prefix.Length);if($reply.StartsWith('ERROR ')){throw "Tablet: $reply"};return $reply}
        }
        if($Link.Partial.Length -gt 8192){$Link.Partial=''}
        Start-Sleep -Milliseconds 10
    };throw "Tablet acknowledgement timeout on $($Link.Port). No command was automatically retried."
}
function Get-ProfileInventory($Link){
    Send-ProfileCommand $Link 'HELLO';$reply=Receive-ProfileReply $Link
    if($reply -notmatch '^TABLET V\d+ IPRF1 '){throw "Not a compatible profile-capable tablet: $reply"}
    $chips=[Collections.Generic.List[object]]::new()
    while($true){$line=Receive-ProfileReply $Link;if($line -eq 'END'){break}
        if($line -match '^CHIP ([a-z][a-z0-9_.-]*) (\d+) ([0-9A-F]{8}) (builtin|uploaded) (.+)$'){
            $chips.Add([pscustomobject]@{Chip=$Matches[1];Revision=[int]$Matches[2];Crc=$Matches[3];Source=$Matches[4];Name=$Matches[5]})
        }else{throw "Unexpected inventory reply: $line"}
    };return $chips.ToArray()
}
Export-ModuleMember -Function Convert-PortableProfile,Open-ProfilePort,Send-ProfileCommand,Receive-ProfileReply,Get-ProfileInventory
