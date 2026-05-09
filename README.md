# WoW-HC Launcher

Official launcher for [WoW-HC](https://wow-hc.com), a WoW hardcore permadeath private server that has been running continuously since March 2023.

---

## What this launcher does

- Downloads and installs the Classic clients (1.12 or 1.14) configured for WoW-HC
- Keeps HermesProxy (the server connection layer) and the WoW-HC addon up to date automatically
- Launches the game with the correct settings for the server
- Optionally records a rolling video replay buffer so you can save the last X minutes of gameplay (like Shadowplay / Medal / OBS buffer)
- Optionally uploads replay clips to your personal Google Drive to appeal your death in case of bug/disconnect

That is the full scope of what it does. It does not touch anything outside the game folder and its own config directory (`%APPDATA%\WOWHCLauncher`).

---

## AV flags

The launcher is currently **unsigned**, a code-signing certificate is in the process of being obtained. Without a signature, Windows SmartScreen and some AV engines will warn on any new `.exe` they haven't seen before, regardless of what it actually does.

A few things specifically make unsigned game launchers look trigger scans:

- **Self-update**: the launcher can update itself by downloading a new version (with your approval) and replacing its own `.exe`. This is the same pattern used by Steam, Battle.net, and every other game launcher, but without a signature it can look bad.
- **Hermes/Wow Launch**: it launches HermesProxy and the WoW client

These are inherent to what a launcher has to do.

If you want to verify the binary yourself:

- The full source code is available on this repo. 
- Builds/releases are generated automatically on GitHub for clarity and transparency.
- There is only one contributor @novivy, which is the owner/developer of wow-hc.com

---

## About WoW-HC

WoW-HC is a WoW Classic private server with a permanent death ruleset. Characters who die are deleted. The server has been online and actively maintained since 2023.

Website: [wow-hc.com](https://wow-hc.com)  
Support / appeals: [wow-hc.com/support/appeal](https://wow-hc.com/support/appeal)

---

## Installation

1. Download `WOW-HC-Launcher.exe` from the [latest release](https://github.com/Novivy/wowhc-launcher/releases/latest)
2. Run `WOW-HC-Launcher.exe`

On first run the launcher will download and set up everything needed to play. Subsequent launches check for updates automatically.

If you already have the 1.12 or 1.14 client installed for another server, click **Browse** and point the launcher at your existing `client` folder (we recommend doing a fresh install instead). It will configure it for WoW-HC without re-downloading the full client.

---

## Config and data locations

| What                | Where                                                      |
|---------------------|------------------------------------------------------------|
| Launcher settings   | `%APPDATA%\WOWHCLauncher\launcher.ini`                     |
| Replay recordings   | Configurable in the recorder settings (default: `\Videos`) |
| Launcher files/logs | `%APPDATA%\WOWHCLauncher\`               |

To fully reset the launcher (as if freshly installed), delete `%APPDATA%\WOWHCLauncher\`.

---

## Credits

- 41-yard nameplate patch: @jameopotato
- Development assistance: @Claude
