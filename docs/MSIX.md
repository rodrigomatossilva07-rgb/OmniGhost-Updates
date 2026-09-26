# Omnighost MSIX packaging

`Release|x64` builds the native Win32 application, signs the executable, creates
`publish\msix\Omnighost.msix`, and signs the package. `Publish|x64` uses the same
packaging target before its existing remote publishing flow.

## One-time local setup

Run `powershell -ExecutionPolicy Bypass -File tools\Initialize-MsixDevelopmentCertificate.ps1`
and restart Visual Studio/your terminal so it receives the user environment variables.
The private key stays non-exportable in `CurrentUser\My`; only its public certificate is
added to `CurrentUser\TrustedPeople`. No PFX or password is stored in the repository.
The certificate is self-signed and suitable only for local testing.

## Build

From a Developer PowerShell for Visual Studio:

```powershell
msbuild OmiGhost.slnx /restore /t:Build /p:Configuration=Release /p:Platform=x64
```

The equivalent project command is:

```powershell
msbuild OmiGhost.vcxproj /t:Build /p:Configuration=Release /p:Platform=x64
```

`dotnet build` is not the supported entry point for this native C++ project because it
requires the Visual C++ targets installed with Visual Studio.

## Trusted certificate or Microsoft Store

Install the trusted code-signing certificate (with private key) in `CurrentUser\My`,
set `OMNIGHOST_SIGNING_THUMBPRINT` and `OMNIGHOST_MSIX_CERT_THUMBPRINT` to its thumbprint,
and pass its exact subject as `PackagePublisher` if it differs from the development value.
Never commit a PFX or password.

For Store submission, copy the exact **Package/Identity/Name**, **Publisher**, and
**PublisherDisplayName** values from Partner Center and build with:

```powershell
msbuild OmiGhost.vcxproj /t:Build /p:Configuration=Release /p:Platform=x64 `
  /p:PackageIdentityName="PARTNER_CENTER_IDENTITY_NAME" `
  /p:PackagePublisher="CN=PARTNER_CENTER_PUBLISHER" `
  /p:PublisherDisplayName="PARTNER_CENTER_DISPLAY_NAME" `
  /p:AppxPackageSigningEnabled=false
```

Submit the resulting unsigned MSIX to Partner Center; the Store applies its distribution
signature. The package declares only `runFullTrust`, which is required for this Win32 app.
