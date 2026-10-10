# OmniGhost Admin API

Serviço administrativo separado do executável. O Seller Key do KeyAuth nunca é
incluído no cliente, nos logs ou no repositório.

## Configuração obrigatória

1. A conta de cada operador deve ter a subscrição KeyAuth `Admin`, nível `10`, ativa.
2. Gera uma credencial por operador:

```powershell
.\tools\New-AdminApiKey.ps1 -Actor "username-keyauth"
```

3. Configura os segredos apenas no host/secret manager do servidor:

```text
OMNIGHOST_ADMIN_KEYAUTH_SELLER_KEY=<seller-key>
OMNIGHOST_ADMIN_API_KEY_HASHES=username-keyauth=<sha256>;outro-admin=<sha256>
```

O operador envia `Authorization: Bearer username-keyauth.<segredo>`. O serviço
valida a hash em tempo constante e revalida no KeyAuth a subscrição `Admin`
nível 10 antes de autorizar. Pedidos mutáveis forçam uma revalidação fresca.

## Execução local

```powershell
dotnet run --project server/OmniGhost.AdminApi/OmniGhost.AdminApi.csproj
```

Em produção, publica atrás de HTTPS e de um reverse proxy com limites de rede.
Não atives HTTP público. O endpoint `/readyz` só responde `200` quando Seller
Key e pelo menos uma hash de operador estão configurados.

Exemplo de container, executado a partir da raiz do repositório:

```powershell
docker build -f server/OmniGhost.AdminApi/Dockerfile -t omnighost-admin-api .
docker run --rm -p 127.0.0.1:8080:8080 --env-file .\secrets\admin.env omnighost-admin-api
```

O TLS deve terminar no reverse proxy. Define `OMNIGHOST_ADMIN_TRUST_FORWARDED_HTTPS=true`
apenas quando o proxy substitui `X-Forwarded-Proto` e a porta 8080 está bloqueada
para tráfego externo. Sem essa opção, Production exige HTTPS diretamente no Kestrel.

## Rotas implementadas

- `GET /api/admin/users`
- `GET /api/admin/users/{username}`
- `GET /api/admin/users/export`
- `POST /api/admin/users/{username}/ban`
- `POST /api/admin/users/{username}/unban`
- `POST /api/admin/users/{username}/reset-hwid`
- `POST /api/admin/users/bulk`
- `GET /api/admin/licenses`
- `GET /api/admin/licenses/export`
- `POST /api/admin/licenses/generate`
- `POST /api/admin/licenses/{key}/ban`
- `POST /api/admin/licenses/{key}/unban`
- `DELETE /api/admin/licenses/{key}`
- `POST /api/admin/licenses/{key}/assign`
- `GET /api/admin/analytics/summary`
- `GET /api/admin/audit-log`
- `GET /api/admin/capabilities`

O audit log é JSONL encadeado por SHA-256. Identificadores de licenças são
registados por fingerprint, nunca em texto integral.
