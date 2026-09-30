# Config reset helper (manual procedure)

This documents the corrective reset applied when legacy DeFlux wrote zero-traffic / zero-density settings into Rockstar Documents config.

## Scope

**In scope**

- `Documents\Rockstar Games\GTA San Andreas Definitive Edition\Config\WindowsNoEditor\CustomSettings.ini`
- `Documents\Rockstar Games\GTA San Andreas Definitive Edition\Config\WindowsNoEditor\GameUserSettings.ini`

**Out of scope**

- `Profiles\` and all save files (including `GTASAsf8.sav`)
- GTA install files under Steam

## Procedure

1. Create backup directory:  
   `Documents\Rockstar Games\GTA San Andreas Definitive Edition\Config\deflux_vanilla_reset_backup_<timestamp>\`
2. Copy affected active files into that backup directory.
3. Delete or rename the active files from `WindowsNoEditor\`.
4. Launch GTA normally once (outside M1 tooling) to regenerate defaults.
5. Confirm no DeFlux console appears and traffic/NPC behaviour is vanilla.

Never edit individual keys to guessed values.
