[CmdletBinding(DefaultParameterSetName = 'Example')]
param(
    [Parameter(ParameterSetName = 'Example')]
    [AllowEmptyString()]
    [string] $Message = 'Ola, GPT-R OS!',

    [Parameter(Mandatory, ParameterSetName = 'Binary')]
    [string] $InputPath,

    [Parameter(ParameterSetName = 'Binary')]
    [uint32] $EntryOffset = 0,

    [Parameter(ParameterSetName = 'Binary')]
    [ValidateSet('v1', 'v1.1')]
    [string] $Format = 'v1',

    [Parameter(ParameterSetName = 'Binary')]
    [string] $DataPath,

    [Parameter(ParameterSetName = 'Binary')]
    [uint32] $BssSize = 0,

    [Parameter()]
    [string] $OutputPath = 'OLA.APP'
)

$ErrorActionPreference = 'Stop'
$extended = $Format -eq 'v1.1'
$maxImageSize = if ($extended) { 65536 } else { 4096 }
[byte[]] $data = @()
if (!$extended -and ($DataPath -or $BssSize)) {
    throw 'DataPath and BssSize require -Format v1.1.'
}
if ($BssSize -ge $maxImageSize) {
    throw 'BSS leaves no room for code.'
}

function Read-AppSegment([string] $Path, [int] $Limit) {
    $file = [System.IO.File]::OpenRead((Get-Item -LiteralPath $Path).FullName)
    $reader = New-Object System.IO.BinaryReader -ArgumentList $file
    try {
        [byte[]] $bytes = $reader.ReadBytes($Limit + 1)
        if ($bytes.Length -gt $Limit) {
            throw "Segment exceeds the available $Limit bytes."
        }
        return ,$bytes
    } finally {
        $reader.Dispose()
    }
}

if ($PSCmdlet.ParameterSetName -eq 'Binary') {
    [byte[]] $image = Read-AppSegment $InputPath ($maxImageSize - $BssSize)
    if ($DataPath) {
        $data = Read-AppSegment $DataPath ($maxImageSize - $BssSize - $image.Length)
    }
} else {
    [byte[]] $messageBytes = [System.Text.Encoding]::UTF8.GetBytes($Message + "`n")

    # Machine code equivalent to apps/OLA.S. The 32-bit immediate at
    # offset 13 holds the UTF-8 message length for the write call.
    [byte[]] $code = @(
        0xB8, 0x01, 0x00, 0x00, 0x00,
        0x48, 0x8D, 0x3D, 0x12, 0x00, 0x00, 0x00,
        0xBE, 0x00, 0x00, 0x00, 0x00,
        0xCD, 0x80,
        0xB8, 0x02, 0x00, 0x00, 0x00,
        0x31, 0xFF,
        0xCD, 0x80,
        0x0F, 0x0B
    )
    if ($messageBytes.Length -gt ($maxImageSize - $code.Length)) {
        throw 'The UTF-8 message is too long for one application page.'
    }
    $length = [uint32] $messageBytes.Length
    $code[13] = [byte] ($length -band 0xff)
    $code[14] = [byte] (($length -shr 8) -band 0xff)
    [byte[]] $image = $code + $messageBytes
}

if ($image.Length -lt 1 -or $image.Length + $data.Length + $BssSize -gt $maxImageSize -or
    $EntryOffset -ge $image.Length) {
    throw 'The image or entry offset is out of range.'
}

$destination = $ExecutionContext.SessionState.Path.GetUnresolvedProviderPathFromPSPath($OutputPath)
$stream = New-Object System.IO.MemoryStream
$writer = New-Object System.IO.BinaryWriter -ArgumentList $stream
try {
    $writer.Write([byte[]] [System.Text.Encoding]::ASCII.GetBytes('GTRA'))
    if ($extended) {
        $writer.Write([uint16] 0x0101)
        $writer.Write([uint16] 64)
        $writer.Write([uint32] 64)
        $writer.Write([uint32] $image.Length)
        $writer.Write([uint32] (64 + $image.Length))
        $writer.Write([uint32] $data.Length)
        $writer.Write([uint32] $BssSize)
        $writer.Write([uint32] $EntryOffset)
        $writer.Write([byte[]] (New-Object 'byte[]' 32))
    } else {
        $writer.Write([uint16] 1)
        $writer.Write([uint16] 32)
        $writer.Write([uint32] $image.Length)
        $writer.Write([uint32] $EntryOffset)
        $writer.Write([byte[]] (New-Object 'byte[]' 16))
    }
    $writer.Write($image)
    if ($extended) {
        $writer.Write($data)
    }
    $writer.Flush()
    [System.IO.File]::WriteAllBytes($destination, $stream.ToArray())
} finally {
    $writer.Dispose()
    $stream.Dispose()
}
Write-Output $destination
