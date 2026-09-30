#pragma once

#include <string>

namespace deflux::launcher {

// Permanently replaces the Rockstar logo stinger / UE splash for SADE so launches
// go straight past the R* intro (does NOT touch the in-game CJ airport cutscene).
// Originals are kept as *.deflux_bak next to the media files.
bool apply_permanent_rockstar_intro_skip(const std::wstring& gameface_or_win64_dir);

// Before Steam launch: zero ped/car density + Traffic=0 in user Documents config
// so the next boot starts with an empty street population.
bool apply_freeroam_empty_world_settings();

// Move story GTASAsf*.sav aside so the session cannot ResumeGameFromMenu a story slot.
// Call restore_story_saves_after_session() when the game process exits.
bool quarantine_story_saves_for_new_game();
bool restore_story_saves_after_session();

// Patch scripts\GTASADE.FusionFix.ini:
//  mode "clean_save" (default freeroam): SkipMenu=1 + SaveSlot=kFreeroamSaveSlot
//  mode "new_game":                     SkipMenu=0 (DeFlux StartNewGameFromMenu)
//  mode "keep_saves":                   SkipMenu=1 (resume user SaveSlot as-is)
enum class FusionFixEntryMode {
    CleanSave = 0,
    NewGame = 1,
    KeepSaves = 2,
};

bool configure_fusionfix_entry_mode(const std::wstring& win64_or_hint, FusionFixEntryMode mode);

// Back-compat wrapper (new_game=true -> NewGame, false -> CleanSave).
inline bool configure_fusionfix_entry_mode(const std::wstring& win64_or_hint, bool new_game) {
    return configure_fusionfix_entry_mode(
        win64_or_hint,
        new_game ? FusionFixEntryMode::NewGame : FusionFixEntryMode::CleanSave);
}

} // namespace deflux::launcher
