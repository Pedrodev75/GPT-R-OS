[CmdletBinding(DefaultParameterSetName = 'Example')]
param(
    [Parameter(ParameterSetName = 'Example')]
    [AllowEmptyString()]
    [string] $Message = 'Ola, GPT-R OS!',

    [Parameter(Mandatory, ParameterSetName = 'Binary')]
    [string] $InputPath,

    [Parameter(ParameterSetName = 'Binary')]
    [uint32] $EntryOffset = 0,

    [Parameter()]
    [string] $OutputPath = 'OLA.APP'
)

$ErrorActionPreference = 'Stop'
$maxImageSize = 4096

if ($PSCmdlet.ParameterSetName -eq 'Binary') {
    $inputFile = Get-Item -LiteralPath $InputPath
    if ($inputFile -isnot [System.IO.FileInfo] -or
        $inputFile.Length -lt 1 -or $inputFile.Length -gt $maxImageSize) {
        throw 'The flat binary must contain 1 to 4096 bytes.'
    }
    [byte[]] $image = [System.IO.File]::ReadAllBytes($inputFile.FullName)
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

if ($image.Length -lt 1 -or $image.Length -gt $maxImageSize -or
    $EntryOffset -ge $image.Length) {
    throw 'The image or entry offset is out of range.'
}

$destination = $ExecutionContext.SessionState.Path.GetUnresolvedProviderPathFromPSPath($OutputPath)
$stream = New-Object System.IO.MemoryStream
$writer = New-Object System.IO.BinaryWriter -ArgumentList $stream
try {
    $writer.Write([byte[]] [System.Text.Encoding]::ASCII.GetBytes('GTRA'))
    $writer.Write([uint16] 1)
    $writer.Write([uint16] 32)
    $writer.Write([uint32] $image.Length)
    $writer.Write([uint32] $EntryOffset)
    $writer.Write([byte[]] (New-Object 'byte[]' 16))
    $writer.Write($image)
    $writer.Flush()
    [System.IO.File]::WriteAllBytes($destination, $stream.ToArray())
} finally {
    $writer.Dispose()
    $stream.Dispose()
}
Write-Output $destination
