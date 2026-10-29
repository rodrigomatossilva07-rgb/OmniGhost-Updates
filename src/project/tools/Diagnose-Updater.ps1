[CmdletBinding()]
param(
    [string]$ManifestUrl = 'https://github.com/rodrigomatossilva07-rgb/OmniGhost-Updates/releases/latest/download/update.json',
    [string]$OutputPath = (Join-Path $env:USERPROFILE 'Desktop\OmniGhost-Updater-Diagnostic.txt')
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$lines = New-Object System.Collections.Generic.List[string]

function Add-DiagnosticLine {
    param([Parameter(Mandatory = $true)][string]$Text)

    $timestamp = [DateTime]::UtcNow.ToString('yyyy-MM-ddTHH:mm:ssZ')
    $line = '{0} {1}' -f $timestamp, $Text
    [void]$script:lines.Add($line)
    Write-Host $line
}

function Add-ExceptionDetails {
    param(
        [Parameter(Mandatory = $true)]$ErrorRecord,
        [Parameter(Mandatory = $true)][string]$Stage
    )

    Add-DiagnosticLine ('RESULT=FAIL STAGE={0}' -f $Stage)
    Add-DiagnosticLine ('EXCEPTION_TYPE={0}' -f $ErrorRecord.Exception.GetType().FullName)
    Add-DiagnosticLine ('MESSAGE={0}' -f $ErrorRecord.Exception.Message)
    Add-DiagnosticLine ('HRESULT=0x{0:X8}' -f ($ErrorRecord.Exception.HResult -band 0xFFFFFFFF))

    if ($ErrorRecord.Exception.InnerException) {
        Add-DiagnosticLine ('INNER_TYPE={0}' -f $ErrorRecord.Exception.InnerException.GetType().FullName)
        Add-DiagnosticLine ('INNER_MESSAGE={0}' -f $ErrorRecord.Exception.InnerException.Message)
    }

    $response = $null
    if ($ErrorRecord.Exception.PSObject.Properties.Name -contains 'Response') {
        $response = $ErrorRecord.Exception.Response
    }

    if ($response) {
        try { Add-DiagnosticLine ('HTTP_STATUS={0}' -f [int]$response.StatusCode) } catch {}
        try { Add-DiagnosticLine ('HTTP_DESCRIPTION={0}' -f [string]$response.StatusDescription) } catch {}
        try { Add-DiagnosticLine ('FINAL_URL={0}' -f [string]$response.ResponseUri.AbsoluteUri) } catch {}
    }

    Add-DiagnosticLine ('CATEGORY={0}' -f [string]$ErrorRecord.CategoryInfo)
    Add-DiagnosticLine ('FULL_ERROR={0}' -f ($ErrorRecord | Out-String).Trim())
}

function Convert-ResponseContentToText {
    param([Parameter(Mandatory = $true)]$Content)

    if ($Content -is [byte[]]) {
        return [System.Text.Encoding]::UTF8.GetString($Content)
    }

    return [string]$Content
}

try {
    $parent = Split-Path -Parent $OutputPath
    if ($parent -and -not (Test-Path -LiteralPath $parent)) {
        New-Item -ItemType Directory -Path $parent -Force | Out-Null
    }

    Add-DiagnosticLine 'OMNIGHOST UPDATER DIAGNOSTIC'
    Add-DiagnosticLine ('COMPUTER={0}' -f $env:COMPUTERNAME)
    Add-DiagnosticLine ('USER={0}' -f $env:USERNAME)
    Add-DiagnosticLine ('POWERSHELL={0}' -f $PSVersionTable.PSVersion)
    Add-DiagnosticLine ('OS={0}' -f [Environment]::OSVersion.VersionString)
    Add-DiagnosticLine ('MANIFEST_URL={0}' -f $ManifestUrl)

    try {
        $proxyOutput = (& netsh winhttp show proxy 2>&1 | Out-String).Trim()
        Add-DiagnosticLine ('WINHTTP_PROXY={0}' -f ($proxyOutput -replace '[\r\n]+', ' | '))
    }
    catch {
        Add-DiagnosticLine ('WINHTTP_PROXY_ERROR={0}' -f $_.Exception.Message)
    }

    foreach ($hostName in @('github.com', 'release-assets.githubusercontent.com')) {
        try {
            $addresses = [System.Net.Dns]::GetHostAddresses($hostName) |
                ForEach-Object { $_.IPAddressToString }
            Add-DiagnosticLine ('DNS_OK HOST={0} ADDRESSES={1}' -f $hostName, ($addresses -join ','))
        }
        catch {
            Add-ExceptionDetails -ErrorRecord $_ -Stage ('dns:{0}' -f $hostName)
        }
    }

    try {
        $tcp = New-Object System.Net.Sockets.TcpClient
        $async = $tcp.BeginConnect('github.com', 443, $null, $null)
        if (-not $async.AsyncWaitHandle.WaitOne(10000, $false)) {
            $tcp.Close()
            throw 'A ligação TCP a github.com:443 excedeu 10 segundos.'
        }
        $tcp.EndConnect($async)
        $tcp.Close()
        Add-DiagnosticLine 'TCP_OK HOST=github.com PORT=443'
    }
    catch {
        Add-ExceptionDetails -ErrorRecord $_ -Stage 'tcp:github.com:443'
    }

    [Net.ServicePointManager]::SecurityProtocol = [Net.SecurityProtocolType]::Tls12

    try {
        Add-DiagnosticLine 'MANIFEST_REQUEST_START'
        $response = Invoke-WebRequest `
            -Uri $ManifestUrl `
            -UseBasicParsing `
            -MaximumRedirection 10 `
            -Headers @{
                'Accept' = 'application/json, text/plain;q=0.9, application/octet-stream;q=0.8, */*;q=0.1'
                'Cache-Control' = 'no-cache'
                'Pragma' = 'no-cache'
            }

        Add-DiagnosticLine ('MANIFEST_HTTP={0}' -f [int]$response.StatusCode)
        Add-DiagnosticLine ('MANIFEST_FINAL_URL={0}' -f [string]$response.BaseResponse.ResponseUri.AbsoluteUri)
        Add-DiagnosticLine ('MANIFEST_CONTENT_TYPE={0}' -f [string]$response.Headers['Content-Type'])

        $text = Convert-ResponseContentToText -Content $response.Content
        if ([string]::IsNullOrWhiteSpace($text)) {
            throw 'O manifesto foi descarregado, mas está vazio.'
        }

        $manifest = $text | ConvertFrom-Json
        Add-DiagnosticLine ('MANIFEST_VERSION={0}' -f [string]$manifest.version)
        Add-DiagnosticLine ('MANIFEST_APP_ID={0}' -f [string]$manifest.appId)

        $package = @($manifest.packages | Where-Object {
            [string]$_.platform -eq 'windows' -and
            [string]$_.architecture -eq 'x64'
        }) | Select-Object -First 1

        if (-not $package) {
            throw 'O manifesto não contém um pacote Windows x64.'
        }

        Add-DiagnosticLine ('PACKAGE_URL={0}' -f [string]$package.url)
        Add-DiagnosticLine ('PACKAGE_SIZE={0}' -f [uint64]$package.size)
        Add-DiagnosticLine ('PACKAGE_SHA256={0}' -f [string]$package.sha256)

        Add-DiagnosticLine 'PACKAGE_HEAD_START'
        $packageResponse = Invoke-WebRequest `
            -Uri ([string]$package.url) `
            -UseBasicParsing `
            -Method Head `
            -MaximumRedirection 10 `
            -Headers @{ 'Accept' = 'application/octet-stream, application/zip, */*;q=0.1' }

        Add-DiagnosticLine ('PACKAGE_HTTP={0}' -f [int]$packageResponse.StatusCode)
        Add-DiagnosticLine ('PACKAGE_FINAL_URL={0}' -f [string]$packageResponse.BaseResponse.ResponseUri.AbsoluteUri)
        Add-DiagnosticLine ('PACKAGE_CONTENT_LENGTH={0}' -f [string]$packageResponse.Headers['Content-Length'])
        Add-DiagnosticLine ('PACKAGE_CONTENT_TYPE={0}' -f [string]$packageResponse.Headers['Content-Type'])
        Add-DiagnosticLine 'RESULT=SUCCESS'
    }
    catch {
        Add-ExceptionDetails -ErrorRecord $_ -Stage 'manifest-or-package'
    }
}
finally {
    $utf8Bom = New-Object System.Text.UTF8Encoding -ArgumentList $true
    [System.IO.File]::WriteAllLines(
        $OutputPath,
        [string[]]$lines,
        $utf8Bom)

    Write-Host ''
    Write-Host ('Diagnóstico guardado em: {0}' -f $OutputPath)
}
