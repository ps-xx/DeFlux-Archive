#pragma once

#include <cstdint>
#include <string>

namespace deflux::launcher {

// FusionFix SaveSlot for DeFlux freeroam (SADE file: GTASAsf8.sav).
constexpr int kFreeroamSaveSlot = 8;

// This archival snapshot does not include DeFlux_clean.sav.
// Slot 8 deployment is therefore unavailable here: deploy_clean_freeroam_saves()
// returns false when the template is absent. No replacement save is shipped.
//
// Historical behavior, when a template existed:
//  - Write DeFlux_clean.sav ONLY to GTASAsf8.sav (Slot 8).
//  - Never touch GTSAautosave.sav or GTASAsf1.sav .. GTASAsf7.sav.
//  - Temporarily point cloudsavedata.dat at Slot 8 so FusionFix SkipMenu
//    ResumeGameFromMenu loads Slot 8 (full cloud file restored after session).
bool deploy_clean_freeroam_saves();

// Restore previous Slot 8 / cloudsavedata backups after the game session ends.
bool restore_clean_freeroam_saves();

// Resolve path to DeFlux_clean.sav (beside exe, assets\, or cwd).
std::wstring find_clean_save_template();

} // namespace deflux::launcher
