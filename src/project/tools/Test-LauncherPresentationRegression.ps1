[CmdletBinding()]
param(
    [string]$ProjectDir = ([IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..\..\..')))
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest
$ProjectDir = [IO.Path]::GetFullPath($ProjectDir)

function Read-Source([string]$RelativePath) {
    [IO.File]::ReadAllText((Join-Path $ProjectDir $RelativePath))
}

function Require-Match([string]$Text, [string]$Pattern, [string]$Reason) {
    if ($Text -notmatch $Pattern) { throw $Reason }
}

function Forbid-Match([string]$Text, [string]$Pattern, [string]$Reason) {
    if ($Text -match $Pattern) { throw $Reason }
}

$auth = Read-Source 'src/launcher/auth_page.cpp'
$cards = Read-Source 'src/window/cards.cpp'
$widgets = Read-Source 'src/window/widgets.cpp'
$admin = Read-Source 'src/launcher/admin/admin_panel.inl'

Forbid-Match $auth 'Usar login antigo|Usar conta OmniGhost|Usar registo antigo|Criar conta OmniGhost' `
    'A opção de autenticação antiga voltou a aparecer na interface.'
Forbid-Match $auth 'g_legacy_login|LoadRememberedRemoteCredentials' `
    'A página de autenticação ainda pode selecionar automaticamente o login antigo.'
Require-Match $auth 'RemoteOperation::WebsiteLogin' `
    'O login da conta OmniGhost deixou de ser o fluxo principal.'
Require-Match $auth 'RemoteOperation::WebsiteRegister' `
    'O registo da conta OmniGhost deixou de ser o fluxo principal.'

$endCard = [regex]::Match($cards, '(?s)void EndCard\(\).*?\n\s*}\n\n\s*void CardGap').Value
if ([string]::IsNullOrWhiteSpace($endCard)) { throw 'Não foi possível localizar EndCard().' }
Forbid-Match $endCard 'ImGui::PopID\(\);\s*ImGui::Dummy' `
    'EndCard() voltou a substituir a âncora usada por SameLine(), sobrepondo cartões.'

Require-Match $widgets 'hasAction \? 154\.0f : 104\.0f' `
    'EmptyState() não reserva altura suficiente para o texto e a ação.'
Forbid-Match $admin 'ImGuiWindowFlags_NoScrollbar\s*\|\s*ImGuiWindowFlags_NoScrollWithMouse' `
    'A navegação Admin voltou a impedir scroll quando as secções não cabem.'

Write-Host '[PASS] Launcher authentication and presentation regression invariants.' -ForegroundColor Green
