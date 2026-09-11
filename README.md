<p align="center">
  <img
    width="100%"
    alt="OMNIGHOST"
    src="https://capsule-render.vercel.app/api?type=waving&height=230&color=0:080808,35:17130A,55:D4AF37,75:17130A,100:080808&text=OMNIGHOST&fontColor=FFFFFF&fontSize=52&fontAlignY=38&desc=Everywhere.%20Nowhere.&descAlignY=59&descSize=17"
  />
</p>

<div align="center">

[![Windows](https://img.shields.io/badge/WINDOWS-x64-D4AF37?style=for-the-badge\&logo=windows\&logoColor=white\&labelColor=101010)](#-requirements)
[![DMA](https://img.shields.io/badge/DMA-REQUIRED-C9A227?style=for-the-badge\&labelColor=101010)](#-requirements)
[![MAKCU](https://img.shields.io/badge/MAKCU-OPTIONAL-8D7627?style=for-the-badge\&labelColor=101010)](#-requirements)
[![Fuser](https://img.shields.io/badge/FUSER-OPTIONAL-8D7627?style=for-the-badge\&labelColor=101010)](#-requirements)

<br>

### Everywhere. Nowhere.

**OMNIGHOST** brings authentication, your game library, hardware status, diagnostics, licensing, and automatic updates together in a modern Windows x64 launcher.

<br>

[![Latest Release](https://img.shields.io/github/v/release/rodrigomatossilva07-rgb/OmniGhost-Updates?display_name=tag\&style=for-the-badge\&label=LATEST%20RELEASE\&labelColor=101010\&color=D4AF37)](https://github.com/rodrigomatossilva07-rgb/OmniGhost-Updates/releases/latest)
[![Download](https://img.shields.io/badge/DOWNLOAD-OMNIGHOST.EXE-C9A227?style=for-the-badge\&logo=github\&logoColor=white\&labelColor=101010)](https://github.com/rodrigomatossilva07-rgb/OmniGhost-Updates/releases/latest)
[![Discord](https://img.shields.io/badge/DISCORD-JOIN%20COMMUNITY-D4AF37?style=for-the-badge\&logo=discord\&logoColor=white\&labelColor=101010)](https://discord.gg/wsXAyWd5)

</div>

---

## ⚠️ Legal notice and responsible use

**OMNIGHOST** is an independent project intended exclusively for research, development, and use on systems or in environments where the user has explicit authorization.

The user is responsible for complying with:

* applicable laws and regulations;
* the terms of service of any games involved;
* the rules of the platforms being used;
* third-party service policies;
* the policies of the environment in which the software is run.

> [!CAUTION]
> Do not use OMNIGHOST on accounts, systems, servers, or environments where this type of hardware or software is not authorized.
>
> This project does not grant permission to bypass rules, security measures, access controls, or restrictions imposed by third parties.

> [!IMPORTANT]
> OMNIGHOST requires a **compatible DMA card** for DMA-dependent features.
>
> **MAKCU** and a **fuser** remain optional components and depend on the setup being used.

All names, trademarks, logos, games, services, and products mentioned belong to their respective owners.

References to third-party products do not imply affiliation, approval, sponsorship, or an official partnership.

---

## ✦ Table of contents

<table>
<tr>
<td width="50%">

* [About OMNIGHOST](#-about-omnighost)
* [Highlights](#-highlights)
* [Authentication](#-authentication)
* [Home](#-home)
* [Library](#-library)

</td>
<td width="50%">

* [Marketplace](#-marketplace)
* [Diagnostics](#-diagnostics)
* [Automatic updates](#-automatic-updates)
* [Installation](#-installation)
* [Requirements](#-requirements)
* [Troubleshooting](#-troubleshooting)
* [Support](#-support)
* [Project status](#-project-status)

</td>
</tr>
</table>

---

## ✦ About OMNIGHOST

**OMNIGHOST** is a Windows x64 launcher developed to centralize the experience around supported games, licenses, DMA hardware, optional peripherals, diagnostics, and application updates.

The launcher is organized around four main areas:

```text
┌──────────────────────────────────────────────────────────┐
│                       OMNIGHOST                          │
│                  Everywhere. Nowhere.                    │
├──────────────┬──────────────┬──────────────┬─────────────┤
│     Home     │   Library    │ Marketplace  │ Diagnostics │
└──────────────┴──────────────┴──────────────┴─────────────┘
```

Access to available games is determined by the licenses associated with the authenticated OMNIGHOST account.

### Setup architecture

```text
                         OMNIGHOST
                             │
             ┌───────────────┴───────────────┐
             │                               │
        User account                    Local system
             │                               │
      Authentication                  Hardware status
        & licensing                 DMA · MAKCU · System
             │                               │
             └───────────────┬───────────────┘
                             │
                    Available library
                             │
                     Licensed games
```

### Main hardware

| Component               |    Status    | Role                                                          |
| :---------------------- | :----------: | :------------------------------------------------------------ |
| **Compatible DMA card** | **Required** | Main component for DMA-dependent functionality                |
| **MAKCU**               |   Optional   | Additional input-related hardware when supported by the setup |
| **Fuser**               |   Optional   | External hardware used when required by the physical setup    |
| **Internet connection** |  Recommended | Authentication, licensing, updates, and online services       |

> [!NOTE]
> Hardware compatibility may depend on the card model, firmware, drivers, cables, ports, and configuration of the computers involved.

---

## ✦ Highlights

<table>
<tr>
<td width="50%" valign="top">

### ◈ Account authentication

Secure account-based access using username and password authentication.

</td>
<td width="50%" valign="top">

### ◈ License-based library

The Library automatically displays games available for the licenses associated with the authenticated account.

</td>
</tr>

<tr>
<td width="50%" valign="top">

### ◈ System overview

The Home page provides quick access to relevant system and hardware information.

</td>
<td width="50%" valign="top">

### ◈ Complete diagnostics

Built-in diagnostics help verify the application's environment, hardware, connectivity, configuration, and other relevant components.

</td>
</tr>

<tr>
<td width="50%" valign="top">

### ◈ Automatic updates

OMNIGHOST can detect new versions and offer to update the launcher when a new release becomes available.

</td>
<td width="50%" valign="top">

### ◈ Lightweight installation

OMNIGHOST is distributed as a standalone executable without requiring a ZIP-based installation structure.

</td>
</tr>

<tr>
<td width="50%" valign="top">

### ◈ Local application data

Cache, logs, and other application-generated data are stored under the user's Local AppData directory.

</td>
<td width="50%" valign="top">

### ◈ Marketplace-ready architecture

A dedicated Marketplace section is part of the interface and is being prepared for future functionality.

</td>
</tr>
</table>

---

## ✦ Authentication

OMNIGHOST uses an account-based authentication system powered by **KeyAuth**.

### Existing users

To sign in, the user provides:

```text
Username
Password
```

After successful authentication, OMNIGHOST loads the account information and determines which games and features are available according to the licenses linked to that account.

### New users

Account registration requires:

```text
Username
Password
Valid license
```

A valid license must be provided during registration.

> [!IMPORTANT]
> Never share your OMNIGHOST username, password, license information, authentication tokens, or other account credentials.

### Licensing

OMNIGHOST supports different licensing options.

Licenses may provide access to:

* an individual game;
* specific available content;
* multiple supported games;
* broader packages that include access to the complete available library.

The content displayed in the Library depends on the licenses currently associated with the authenticated account.

---

## ✦ Home

The **Home** page provides a centralized overview of the current OMNIGHOST environment.

It may display information such as:

* the most recently played game;
* DMA status;
* MAKCU status;
* system information;
* launcher version;
* update status;
* relevant environment information.

The goal of the Home page is to provide the most useful information without requiring the user to navigate through multiple sections.

### Example overview

```text
┌─────────────────────────────────────────────┐
│                   HOME                      │
├─────────────────────────────────────────────┤
│ Last played        → Available game         │
│ DMA                → Connected / Status     │
│ MAKCU              → Connected / Optional   │
│ OMNIGHOST          → Up to date             │
│ System             → Ready                  │
└─────────────────────────────────────────────┘
```

---

## ✦ Library

The **Library** displays the games currently available to the authenticated user.

The available content is determined automatically according to the licenses associated with the account.

This means different users may see different games depending on their active licenses.

### Library flow

```text
User signs in
      │
      ▼
Licenses are validated
      │
      ▼
Available access is determined
      │
      ▼
Library is populated
      │
      ▼
Licensed games are displayed
```

This keeps the interface focused on the content that is actually available to the current account.

> [!NOTE]
> A game that is not included in the account's current licenses may not appear as available in the Library.

---

## ✦ Marketplace

The **Marketplace** is a dedicated section of OMNIGHOST intended for future integration with the project's licensing and product ecosystem.

> [!NOTE]
> Marketplace functionality is currently under development and is not yet fully implemented.

Its presence in the interface prepares OMNIGHOST for future functionality without requiring major changes to the main navigation structure.

Features and availability may change as development continues.

---

## ✦ Diagnostics

The **Diagnostics** page provides tools for checking the current OMNIGHOST environment.

Diagnostics may verify areas such as:

* DMA availability;
* MAKCU availability;
* hardware status;
* Internet connectivity;
* authentication connectivity;
* application configuration;
* local application files;
* permissions;
* system information;
* launcher version;
* update status;
* required services or dependencies;
* application-generated logs;
* general environment readiness.

The goal is to make it easier to identify configuration or environment problems before attempting to use the available content.

### Diagnostic overview

```text
Hardware ─────────────── Checked
DMA ─────────────────── Checked
MAKCU ───────────────── Checked when applicable
Network ──────────────── Checked
Authentication ───────── Checked
Application ──────────── Checked
Updates ──────────────── Checked
Environment ──────────── Checked
```

> [!NOTE]
> The exact checks performed may change as OMNIGHOST continues to evolve.

---

## ✦ Automatic updates

OMNIGHOST includes an automatic update system.

When a newer version is available, the launcher can detect it and notify the user.

The user is asked before the update process begins.

```text
New release published
        │
        ▼
OMNIGHOST detects it
        │
        ▼
User is notified
        │
        ▼
Update confirmation
     ┌──┴──┐
    Yes    No
     │      │
     ▼      └── Continue current version
Update
```

This allows users to remain on the latest available version while keeping control over when an update is installed.

> [!IMPORTANT]
> It is recommended to use the latest available OMNIGHOST release whenever possible.

The badge at the top of this README automatically displays the latest version published through GitHub Releases.

---

## ✦ Installation

OMNIGHOST no longer requires a ZIP archive or a manually maintained folder structure.

### 01 — Download

Open the latest release:

<div align="center">

[![Download Latest](https://img.shields.io/badge/DOWNLOAD-LATEST%20VERSION-D4AF37?style=for-the-badge\&logo=github\&logoColor=white\&labelColor=101010)](https://github.com/rodrigomatossilva07-rgb/OmniGhost-Updates/releases/latest)

</div>

Download the OMNIGHOST executable from the release assets.

```text
OmniGhost.exe
```

> [!WARNING]
> Download OMNIGHOST only from an official project release or another officially provided source.

### 02 — Run

Place the executable in a suitable location and open:

```text
OmniGhost.exe
```

There is no requirement to manually create folders such as:

```text
data\
fonts\
images\
```

OMNIGHOST manages its own application data automatically.

### 03 — Local application data

Runtime information generated by OMNIGHOST is stored under:

```text
%LOCALAPPDATA%\OmniGhost\
```

This location may contain application-generated information such as:

```text
OmniGhost\
├── cache\
├── logs\
└── other application data
```

The exact internal structure may change between versions.

> [!IMPORTANT]
> Avoid manually modifying files inside the OMNIGHOST application-data directory unless instructed to do so for troubleshooting purposes.

---

## ✦ Requirements

### System

| Requirement                              |    Status   |
| :--------------------------------------- | :---------: |
| Windows 10 or Windows 11                 |   Required  |
| 64-bit operating system                  |   Required  |
| Permission to run the application        |   Required  |
| Space for the application and local data |   Required  |
| Internet connection                      | Recommended |

### Hardware

| Component                                              |                  Status                 |
| :----------------------------------------------------- | :-------------------------------------: |
| Compatible DMA card                                    | **Required for DMA-dependent features** |
| Computer and connections compatible with the DMA setup |                 Required                |
| Appropriate drivers                                    |                 Required                |
| Compatible firmware                                    |                 Required                |
| MAKCU                                                  |                 Optional                |
| Fuser                                                  |                 Optional                |

### Summary

```text
Windows x64 ────────────── Required
DMA card ───────────────── Required
Drivers and firmware ───── Required
Internet ───────────────── Recommended
MAKCU ──────────────────── Optional
Fuser ──────────────────── Optional
```

---

## ✦ Troubleshooting

<details>
<summary><strong>◈ OMNIGHOST does not open</strong></summary>

<br>

* Restart the application.
* Move the executable to a normal user-accessible directory.
* Confirm that your antivirus did not quarantine or remove the executable.
* Make sure your Windows account has permission to run the application.
* Check `%LOCALAPPDATA%\OmniGhost\` for relevant logs.
* Restart Windows if the issue persists.

</details>

<details>
<summary><strong>◈ I cannot sign in</strong></summary>

<br>

* Confirm that the username is correct.
* Confirm that the password is correct.
* Check your Internet connection.
* Confirm that the account is valid.
* Restart OMNIGHOST and try again.
* Check whether an authentication-related error appears in the application.

Do not publish passwords, licenses, tokens, or other account credentials when requesting support.

</details>

<details>
<summary><strong>◈ Registration fails</strong></summary>

<br>

Registration requires:

* a username;
* a password;
* a valid license.

Confirm that the provided information is valid and that the computer has an active Internet connection.

</details>

<details>
<summary><strong>◈ A game does not appear in the Library</strong></summary>

<br>

The Library is generated according to the licenses associated with the authenticated account.

Confirm that:

* you are signed in to the correct account;
* the required license is associated with that account;
* the license is valid;
* OMNIGHOST has an active Internet connection.

Restart OMNIGHOST after making changes to your account or licenses.

</details>

<details>
<summary><strong>◈ The DMA card is not detected</strong></summary>

<br>

* Check all physical connections.
* Restart the systems involved.
* Verify the firmware recommended for the hardware.
* Confirm that the required drivers are installed.
* Test the hardware using the manufacturer's official tools when available.
* Check whether another application is currently using the device.
* Run the OMNIGHOST Diagnostics page.

</details>

<details>
<summary><strong>◈ MAKCU is not detected</strong></summary>

<br>

* Confirm that MAKCU is part of your setup.
* Check its cable, port, and power.
* Confirm that the device is recognized by Windows.
* Restart OMNIGHOST after reconnecting the hardware.
* Run the Diagnostics page.

MAKCU is optional, and its absence should not prevent OMNIGHOST from opening normally.

</details>

<details>
<summary><strong>◈ The fuser does not display an image</strong></summary>

<br>

* Confirm the selected inputs and outputs.
* Check the configured resolution and refresh rate.
* Test each video source separately.
* Confirm the power supply.
* Check all video cables.

The fuser is optional external hardware and is not required for every OMNIGHOST setup.

</details>

<details>
<summary><strong>◈ Diagnostics reports a problem</strong></summary>

<br>

Review the affected component and follow the information displayed by OMNIGHOST.

If the issue persists:

* restart OMNIGHOST;
* restart the affected hardware;
* check physical connections;
* verify your Internet connection;
* inspect the relevant logs;
* contact support with the diagnostic information.

</details>

<details>
<summary><strong>◈ OMNIGHOST cannot check for updates</strong></summary>

<br>

* Confirm your Internet connection.
* Restart OMNIGHOST.
* Check whether GitHub is accessible.
* Confirm that a firewall, VPN, proxy, or security application is not blocking OMNIGHOST.

A temporary update-check failure may not prevent the launcher from opening normally.

</details>

<details>
<summary><strong>◈ Where are the logs stored?</strong></summary>

<br>

OMNIGHOST application data is stored under:

```text
%LOCALAPPDATA%\OmniGhost\
```

Logs may be located inside the application-data structure.

</details>

---

## ✦ Support

For help, troubleshooting, announcements, and community support, join the official OMNIGHOST Discord:

<div align="center">

[![Discord](https://img.shields.io/badge/JOIN-OMNIGHOST%20DISCORD-D4AF37?style=for-the-badge\&logo=discord\&logoColor=white\&labelColor=101010)](https://discord.gg/wsXAyWd5)

</div>

Before requesting support, gather the following information:

| Information       | Example                       |
| :---------------- | :---------------------------- |
| OMNIGHOST version | Latest installed version      |
| Windows version   | Windows 11 x64                |
| DMA card model    | Model in use                  |
| Firmware          | Installed version             |
| MAKCU             | In use / Not in use           |
| Fuser             | In use / Not in use           |
| Account access    | Relevant license/game only    |
| Issue             | Clear description             |
| Reproduction      | Steps to reproduce            |
| Diagnostics       | Relevant diagnostic result    |
| Evidence          | Relevant screenshots and logs |

Also include:

* the expected result;
* the observed result;
* how often the issue occurs;
* recent changes to the setup;
* the exact error message shown by OMNIGHOST.

> [!WARNING]
> Before publishing screenshots or logs, remove:
>
> * passwords;
> * license keys;
> * authentication tokens;
> * serial numbers;
> * personal identifiers;
> * other sensitive information.

---

## ✦ Project status

<div align="center">

[![Development](https://img.shields.io/badge/DEVELOPMENT-ACTIVE-D4AF37?style=for-the-badge\&labelColor=101010)](#)
[![Platform](https://img.shields.io/badge/PLATFORM-WINDOWS%20x64-C9A227?style=for-the-badge\&labelColor=101010)](#)
[![Hardware](https://img.shields.io/badge/HARDWARE-DMA-8D7627?style=for-the-badge\&labelColor=101010)](#)

</div>

OMNIGHOST is under active development.

The launcher currently includes:

```text
✓ Account authentication
✓ License-based Library
✓ Home system overview
✓ Hardware information
✓ Diagnostics
✓ Automatic update checking
✓ User-confirmed updates
◈ Marketplace — In development
```

The interface, integrations, licensing options, requirements, diagnostics, and hardware compatibility may change between releases.

Always use the latest available OMNIGHOST version whenever possible.

<div align="center">

[![Releases](https://img.shields.io/badge/VIEW-RELEASES-D4AF37?style=for-the-badge\&logo=github\&logoColor=white\&labelColor=101010)](https://github.com/rodrigomatossilva07-rgb/OmniGhost-Updates/releases)
[![Latest](https://img.shields.io/badge/OPEN-LATEST%20VERSION-C9A227?style=for-the-badge\&logo=github\&logoColor=white\&labelColor=101010)](https://github.com/rodrigomatossilva07-rgb/OmniGhost-Updates/releases/latest)
[![Discord](https://img.shields.io/badge/JOIN-DISCORD-8D7627?style=for-the-badge\&logo=discord\&logoColor=white\&labelColor=101010)](https://discord.gg/wsXAyWd5)

</div>

---

<p align="center">
  <img
    width="100%"
    alt="OMNIGHOST footer"
    src="https://capsule-render.vercel.app/api?type=waving&height=130&section=footer&color=0:080808,50:D4AF37,100:080808"
  />
</p>

<div align="center">

## OMNIGHOST

### Everywhere. Nowhere.

**Authentication · Library · Marketplace · Diagnostics**

<sub>Independent project for responsible use in authorized environments.</sub>

</div>
