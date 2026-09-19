# FTDI D3XX driver manifest — GitHub setup

The signed manifest is the policy authority for the FTDI driver updater. The
private signing key must never be added to this repository, a release archive,
or the OmniGhost executable.

## Project layout

Keep the source policy in the OmniGhost project:

```
drivers/ftdi/driver-manifest.json
.github/workflows/publish-ftdi-driver-manifest.yml
```

On Publish, the workflow sends the JSON and detached signature to the existing
update repository under `drivers/ftdi/`. The JSON is public. The detached
signature is public. Only the private signing key is secret.

## Create the signing key once

Run this locally in a private directory, not inside the repository:

```powershell
openssl genpkey -algorithm ED25519 -out ftdi-manifest-private.pem
openssl pkey -in ftdi-manifest-private.pem -pubout -out ftdi-manifest-public.pem
```

Keep `ftdi-manifest-private.pem` offline. In the GitHub repository settings,
create the Actions secret `FTDI_MANIFEST_SIGNING_KEY_PEM` and paste the entire
private PEM file. Commit only `ftdi-manifest-public.pem` to a private safe
location or provide it to the OmniGhost build owner for embedding.

## GitHub configuration (once)

In the **OmniGhost-Updates repository**, configure:

- Actions secret `FTDI_MANIFEST_SIGNING_KEY_PEM`: the entire private PEM;

The workflow publishes back to that same repository using GitHub Actions'
temporary `GITHUB_TOKEN`; no personal access token or repository variable is
needed.

Never put either secret in a source file, config, release, or Discord message.

## First manifest

Edit `drivers/ftdi/driver-manifest.json` in this project. Before enabling the
workflow, replace all values marked `REPLACE_*` with values obtained from
the official FTDI D3XX release:

- exact HTTPS URL on `ftdichip.com`;
- SHA-256 of the downloaded official package;
- exact byte size;
- the INF path inside the package.

Do not use a GitHub mirror or a guessed hash. The updater refuses a manifest
without an HTTPS FTDI URL, a SHA-256, a size limit, and accepted driver rules.

## What to provide to OmniGhost

After the first signed manifest is available, provide:

1. the raw URL for `driver-manifest.json`;
2. the raw URL for `driver-manifest.sig`;
3. the contents of `ftdi-manifest-public.pem`.

Those three public values let OmniGhost verify the manifest without ever
embedding a GitHub token or a private signing key.
