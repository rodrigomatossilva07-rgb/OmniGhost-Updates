iblioteca
/
README_OMNIGHOST_ENGLISH.md


<p align="center"> <img width="100%" alt="OMNIGHOST" src="https://capsule-render.vercel.app/api?type=waving&height=230&color=0:080808,35:17130A,55:D4AF37,75:17130A,100:080808&text=OMNIGHOST&fontColor=FFFFFF&fontSize=52&fontAlignY=38&desc=DMA%20Launcher%20for%20Windows&descAlignY=59&descSize=17" /> </p>

<div align="center">






<br>

Library, configuration, and DMA experience in one launcher.
OMNIGHOST brings hardware, modules, settings, and updates together
in a modern interface designed for Windows x64.

<br>




</div>

⚠️ Legal notice and responsible use
OMNIGHOST is an independent project intended exclusively for research, development, and use on systems or in environments where the user has explicit authorization.

The user is responsible for complying with:

applicable laws and regulations;

the terms of service of any games involved;

the rules of the platforms being used;

third-party service policies;

the policies of the environment in which the software is run.

[!CAUTION]
Do not use OMNIGHOST on accounts, systems, servers, or environments where this type of hardware or software is not authorized. This project does not grant permission to bypass rules, security measures, or restrictions imposed by third parties.

[!IMPORTANT]
OMNIGHOST requires a compatible DMA card for DMA-dependent features.
MAKCU and a fuser are optional components and depend on the setup being used.

All names, trademarks, logos, and products mentioned belong to their respective owners. References to those products do not imply affiliation, approval, sponsorship, or an official partnership.

✦ Table of contents
<table> <tr> <td width="50%">

About OMNIGHOST

Highlights

Launcher experience

Installation

Updates page

</td> <td width="50%">

Requirements

Troubleshooting

Support

Project status

</td> </tr> </table>

✦ About OMNIGHOST
OMNIGHOST is a Windows launcher developed to centralize the experience of a project built around DMA hardware.

The application brings the following into a single interface:

the available library and integrations;

setup component status;

module-based configuration;

launch actions;

a real version history;

update checking;

environment compatibility information.

Its design combines a dark visual identity with gold accents, compact navigation, and clear states for every operation.

Setup architecture
┌──────────────────────────────────────────────────────┐
│                    OMNIGHOST                         │
│          Library · Configuration · Updates           │
└──────────────────────────┬───────────────────────────┘
                           │
             ┌─────────────┴─────────────┐
             │                           │
       Compatible DMA card         Optional components
            Required                 MAKCU · Fuser
Main hardware
Component	Status	Role in the setup
Compatible DMA card	Required	Main component for DMA-dependent features
MAKCU	Optional	Additional input integration when supported by the setup
Fuser	Optional	Combines or displays multiple video sources
Network connection	Recommended	Updates, version history, and remote content
[!NOTE]
Compatibility depends on the card model, firmware, drivers, cables, ports, and the configuration of the computers involved.

✦ Highlights
<table> <tr> <td width="50%" valign="top">

◈ Centralized interface
Library, modules, settings, and system status brought together in one application.

</td> <td width="50%" valign="top">

◈ DMA integration
Designed to work with a compatible DMA card and different hardware configurations.

</td> </tr>

<tr> <td width="50%" valign="top">

◈ Modular hardware
The setup can be extended with MAKCU and a fuser without making them mandatory for every installation.

</td> <td width="50%" valign="top">

◈ Real updates
The version history is based on published releases, without presenting sample data as real changes.

</td> </tr>

<tr> <td width="50%" valign="top">

◈ Consistent experience
Dark theme, gold accents, compact cards, and organization by component.

</td> <td width="50%" valign="top">

◈ Background checks
The launcher can check for new versions without blocking the normal startup experience.

</td> </tr>

<tr> <td width="50%" valign="top">

◈ Clear states
Visual feedback for loading, offline mode, temporary failures, and cached content.

</td> <td width="50%" valign="top">

◈ Continuous development
An architecture prepared for future UI improvements, compatibility updates, and new integrations.

</td> </tr> </table>

✦ Launcher experience
◇ Library
The Library presents the available games and integrations in one centralized area.

Each entry may provide information such as:

module status;

installation availability;

compatibility;

primary actions;

related settings;

setup-related warnings.

◇ Configuration
Options are separated by component to avoid mixing global settings with integration-specific configuration.

This structure makes it easier to:

find settings;

identify the affected component;

reduce configuration mistakes;

maintain a consistent experience;

prepare future integrations.

◇ Hardware status
The launcher can display information about the availability of the components required by the environment.

The goal is to let the user confirm, before use, whether:

the DMA card is available;

the expected components have been detected;

the required configuration has been loaded;

hardware-related warnings are present;

the setup is ready.

◇ Navigation
The interface is designed to provide:

<table> <tr> <td width="33%" align="center">

Fast navigation

Simple transitions between the Library, Updates, and other areas.

</td> <td width="33%" align="center">

Organized content

Search, filters, cards, and consistent categories.

</td> <td width="33%" align="center">

Visual identity

Dark theme with gold accents and subtle status indicators.

</td> </tr> </table>

✦ Installation
01 — Download
Open the latest release page:

<div align="center">



</div>

Under Assets, download:

OmniGhost.zip
[!WARNING]
Do not use Source code (zip) or Source code (tar.gz) as the launcher package. Those files are generated automatically by GitHub.

02 — Extract
Extract the entire archive into its own folder.

Example:

C:\OmniGhost\
The structure should include:

C:\OmniGhost\
├── OmniGhost.exe
├── data\
├── fonts\
├── images\
└── other required files
03 — Prepare the hardware
Before starting the launcher, confirm that:

the DMA card is correctly installed and connected;

the manufacturer-required drivers are available;

the firmware is compatible with the hardware;

all cables and ports are connected correctly;

MAKCU is connected only when it is part of the setup;

the fuser is configured only when required.

04 — Run
Open:

OmniGhost.exe
[!IMPORTANT]
Do not run the application directly from inside the ZIP archive. It must be extracted together with all required files.

[!NOTE]
A DMA card is required for DMA-dependent features. MAKCU and the fuser are optional.

✦ Updates page
The Updates page presents the real version history of published OMNIGHOST releases.

Entries are organized by version, component, and category.

Supported categories
Category	Identifier	Description
Added	NEW	New features, modules, or options
Improved	IMPROVED	Stability, usability, or interface improvements
Fixed	FIXED	Fixes for identified issues
Performance	PERFORMANCE	Speed and resource optimizations
Compatibility	COMPATIBILITY	Adjustments for hardware, versions, or environments
Security	SECURITY	Validation and protection improvements
Removed	REMOVED	Removed or discontinued features
Breaking change	BREAKING	Changes that may require additional attention
Page features
text search;

filtering by component;

filtering by category;

chronological sorting;

grouping by month and year;

identification of versions that have not yet been viewed;

background loading;

use of the latest valid history while offline.

[!IMPORTANT]
The page presents changes made to the OMNIGHOST project. Official game updates are not presented as changes developed by the launcher.

✦ Requirements
System
Requirement	Status
Windows 10 or Windows 11	Required
64-bit operating system	Required
Permission to run the application	Required
Space for the application and temporary files	Required
Internet connection	Recommended
Hardware
Component	Status
Compatible DMA card	Required for DMA-dependent features
Computer and connections compatible with the DMA setup	Required
Appropriate drivers	Required
Compatible firmware	Required
MAKCU	Optional
Fuser	Optional
Summary
DMA card ───────────────── Required
Windows x64 ────────────── Required
Drivers and firmware ───── Required
Internet ───────────────── Recommended
MAKCU ──────────────────── Optional
Fuser ──────────────────── Optional
✦ Troubleshooting
<details> <summary><strong>◈ The launcher does not open</strong></summary>

<br>

Confirm that all files were extracted.

Do not run the application from inside the ZIP archive.

Move the application to a simple folder such as C:\OmniGhost\.

Confirm that your antivirus did not remove any required files.

Make sure you have permission to run the application and write to its folder.

</details>

<details> <summary><strong>◈ The DMA card is not detected</strong></summary>

<br>

Check all physical connections.

Restart the systems involved.

Verify the firmware recommended by the manufacturer.

Confirm that the required drivers are installed.

Test the hardware using the manufacturer's official tool.

Check whether another application is currently using the device.

</details>

<details> <summary><strong>◈ MAKCU is not detected</strong></summary>

<br>

Confirm that MAKCU is part of your setup.

Check the cable, port, and power.

Confirm that the device is recognized by Windows.

Restart the launcher after connecting the hardware.

MAKCU is optional, and its absence should not prevent the launcher from opening normally.

</details>

<details> <summary><strong>◈ The fuser does not display an image</strong></summary>

<br>

Confirm the selected inputs and outputs.

Check the configured resolution and refresh rate.

Test each video source separately.

Confirm the power supply and the cables being used.

The fuser is optional and depends on the user's physical configuration.

</details>

<details> <summary><strong>◈ Updates could not be checked</strong></summary>

<br>

Confirm your Internet connection.

Try again after a few seconds.

Check whether GitHub opens normally in your browser.

Confirm that a firewall, VPN, or proxy is not blocking the application.

A temporary update-check failure should not prevent normal use of the launcher.

</details>

<details> <summary><strong>◈ Which file should I download?</strong></summary>

<br>

Always download:

OmniGhost.zip
Do not use the automatically generated source-code archives as the launcher package.

</details>

✦ Support
Before reporting an issue, gather the following information:

Information	Example
OMNIGHOST version	1.6.4
Windows version	Windows 11 x64
DMA card model	Model in use
Firmware	Installed version
MAKCU	In use / Not in use
Fuser	In use / Not in use
Issue	Clear description
Reproduction	Steps to reproduce
Evidence	Relevant screenshots and logs
Also include:

the expected result;

the observed result;

how often the issue occurs;

recent changes to the setup;

the message shown by the application.

[!WARNING]
Remove tokens, credentials, serial numbers, personal identifiers, and any other sensitive information before publishing logs or screenshots.

✦ Project status
<div align="center">





</div>

OMNIGHOST is under active development.

The interface, integrations, requirements, and hardware compatibility may change between releases. Always review the latest release notes before updating or changing your setup.

<div align="center">




</div>

<p align="center"> <img width="100%" alt="OMNIGHOST footer" src="https://capsule-render.vercel.app/api?type=waving&height=130&section=footer&color=0:080808,50:D4AF37,100:080808" /> </p>

<div align="center">

OMNIGHOST
DMA · Library · Configuration · Updates

<sub>Independent project for responsible use in authorized environments.</sub>

</div>
