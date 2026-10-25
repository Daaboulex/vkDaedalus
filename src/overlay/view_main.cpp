#include "overlay/ui_map.hpp"
#include "imgui_overlay.hpp"
#include "overlay/definition_title.hpp"
#include "overlay/overlay_style.hpp"
#include "overlay/frame_draw.hpp"
#include "overlay/widgets.hpp"
#include "effects/effect_registry.hpp"
#include "settings_manager.hpp"
#include "config_serializer.hpp"
#include "params/field_editor.hpp"
#include "input_blocker.hpp"
#include "logger.hpp"
#include "wayland_display.hpp"
#include "util.hpp"
#include "config_value.hpp"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <cctype>
#include <cstdio>
#include <filesystem>
#include <limits>

#include "imgui.h"
#include "imgui_internal.h"

namespace vkBasalt
{
    namespace
    {
        bool renderPreprocessorDef(PreprocessorDefinition& def, EffectRegistry* registry, const std::string& effectName, bool& changed,
                                   const std::vector<std::string>* users)
        {
            const OverlayPalette& palette  = activePalette();
            const bool            added    = def.defaultValue.empty();
            const float           gap      = ImGui::GetStyle().ItemSpacing.x;
            const float           control  = ui::px(ui::Size::control);
            const bool            switched = definitionIsSwitch(def.name, def.value, def.defaultValue);
            int                   whole    = 0;
            const bool            stepped  = !switched && parseWhole(def.value, whole);
            bool                  remove   = false;
            std::string           caption  = def.name;
            if (users && users->empty())
                caption += ", no loaded effect reads it";
            else if (users)
            {
                caption += ", used by ";
                for (size_t i = 0; i < users->size() && i < 3; i++)
                    caption += (i ? ", " : "") + (*users)[i];
                if (users->size() > 3)
                    caption += " and " + std::to_string(users->size() - 3) + " more";
            }
            ui::panelRow(ui::Size::tallRow);
            ImGui::BeginGroup();
            ImGui::TextUnformatted(definitionTitle(def.name).c_str());
            ImGui::PushFont(nullptr, FontSize::caption);
            ImGui::TextColored(palette.faint, "%s", caption.c_str());
            ImGui::PopFont();
            ImGui::EndGroup();
            const float nameLeft  = ImGui::GetItemRectMin().x;
            const float nameRight = ImGui::GetItemRectMax().x;
            const float valueW    = switched ? ui::px(ui::Size::box) : stepped ? 2.0f * control + ui::px(52.0f) + 2.0f * gap : ui::px(160.0f);
            ui::alignRight(valueW + gap + (added ? control : ui::px(restartMarkSize)));
            const ImVec2 valueAt = ImGui::GetCursorScreenPos();
            if (switched)
            {
                bool on = def.value == "1";
                if (ui::checkbox(("##" + def.name).c_str(), &on))
                {
                    registry->setPreprocessorDefValue(effectName, def.name, on ? "1" : "0");
                    changed = true;
                }
            }
            else if (stepped)
            {
                if (ui::intField(def.name.c_str(), &whole, std::numeric_limits<int>::min(), std::numeric_limits<int>::max()))
                {
                    registry->setPreprocessorDefValue(effectName, def.name, std::to_string(whole));
                    changed = true;
                }
            }
            else
            {
                char valueBuf[64];
                strncpy(valueBuf, def.value.c_str(), sizeof(valueBuf) - 1);
                valueBuf[sizeof(valueBuf) - 1] = '\0';
                if (ui::textField(def.name.c_str(), valueBuf, sizeof(valueBuf), "", ui::px(160.0f)))
                {
                    registry->setPreprocessorDefValue(effectName, def.name, valueBuf);
                    changed = true;
                }
            }
            if (added)
            {
                ImGui::SameLine();
                remove = ui::button(("x##" + def.name).c_str(), ui::Kind::Secondary, control);
                ImGui::SetItemTooltip("Remove this definition");
            }
            else if (defaultMark(def.name.c_str(), def.value != def.defaultValue, def.defaultValue, palette))
            {
                registry->setPreprocessorDefValue(effectName, def.name, def.defaultValue);
                changed = true;
            }
            const std::string shown = def.name + " name";
            UiMap::nameNextItem(shown.c_str());
            ImGui::ItemAdd(ImRect(nameLeft, valueAt.y, std::max(nameRight, valueAt.x - ui::px(ui::Size::gap)), valueAt.y + control), ImGui::GetID("##definitionname"));
            return remove;
        }

        bool renderGlobalDefinitions(EffectRegistry* registry, GlobalDefinitionDraft& draft)
        {
            const OverlayPalette&               palette = activePalette();
            std::vector<PreprocessorDefinition> globals;
            std::vector<std::vector<std::string>> users;
            for (const PreprocessorDefinition& global : registry->getGlobalDefinitions())
            {
                std::vector<std::string> readers = registry->globalDefinitionUsers(global.name);
                if (!global.defaultValue.empty() && readers.empty())
                    continue;
                globals.push_back(global);
                users.push_back(std::move(readers));
            }
            const std::string count = std::to_string(globals.size());
            if (!ui::disclosure("globaldefinitions", "Definitions for every effect", palette.text, ImGui::GetContentRegionAvail().x, count.c_str()))
                return false;

            bool changed = false;
            std::optional<std::string> removal;
            ui::beginPanel();
            for (size_t i = 0; i < globals.size(); i++)
            {
                ImGui::PushID(static_cast<int>(i));
                if (renderPreprocessorDef(globals[i], registry, "", changed, &users[i]))
                    removal = globals[i].name;
                ImGui::PopID();
            }
            ui::panelRow(ui::Size::tallRow);
            const float gap      = ImGui::GetStyle().ItemSpacing.x;
            const float addWidth = ui::buttonWidth("Add##globaldefinition");
            const float valueW   = ui::px(160.0f);
            if (ui::textField("##globalname", draft.name, sizeof(draft.name), "NAME of a new definition", ImGui::GetContentRegionAvail().x - valueW - addWidth - 2.0f * gap))
                draft.error.clear();
            ImGui::SameLine();
            if (ui::textField("##globalvalue", draft.value, sizeof(draft.value), "value", valueW))
                draft.error.clear();
            ImGui::SameLine();
            const bool add = ui::button("Add##globaldefinition", ui::Kind::Secondary, addWidth);
            ui::endPanel();
            if (removal)
            {
                registry->removeGlobalDefinition(*removal);
                changed = true;
            }
            if (add)
            {
                draft.error = registry->addGlobalDefinition(draft.name, draft.value);
                if (!draft.error.empty())
                    Logger::info("global definition refused: " + draft.error);
                if (draft.error.empty())
                {
                    draft.name[0]  = '\0';
                    draft.value[0] = '\0';
                    changed        = true;
                }
            }
            if (!draft.error.empty())
                ui::errorText(draft.error.c_str());
            ImGui::TreePop();
            return changed;
        }

    }

    void ImGuiOverlay::renderGlobalDefinitionsView()
    {
        if (pEffectRegistry)
            if (renderGlobalDefinitions(pEffectRegistry, globalDefinitionDraft))
                markDefinitionChanged();
    }

    void ImGuiOverlay::renderEffectRows(const std::string& effectName)
    {
        const OverlayPalette&           palette = activePalette();
        const std::vector<EffectParam*> params  = pEffectRegistry->getParametersForEffect(effectName);
        std::string category;
        bool        categoryOpen = true;
        bool        anyShown     = false;
        for (size_t i = 0; i < params.size(); i++)
        {
            if (params[i]->hidden)
                continue;
            if (params[i]->category != category)
            {
                category     = params[i]->category;
                categoryOpen = true;
                if (!category.empty())
                {
                    size_t rows = 0;
                    for (size_t j = i; j < params.size() && params[j]->category == category; j++)
                        rows += params[j]->hidden ? 0 : 1;
                    ImGui::PushID(static_cast<int>(i + 2000));
                    ui::panelRow(ui::Size::control);
                    ImGui::PushFont(ui::fonts().bold, FontSize::label);
                    categoryOpen = ui::disclosure("category", category.c_str(), palette.muted, ImGui::GetContentRegionAvail().x, std::to_string(rows).c_str(), !params[i]->categoryClosed);
                    ImGui::PopFont();
                    if (categoryOpen)
                        ImGui::TreePop();
                    ImGui::PopID();
                }
            }
            anyShown = true;
            if (!categoryOpen)
                continue;
            ImGui::PushID(static_cast<int>(i));
            if (renderFieldEditor(*params[i]))
                markEffectValuesChanged(effectName);
            ImGui::PopID();
        }

        const bool                           halfShown = pEffectRegistry->effectUsesMinPrecision(effectName);
        std::vector<PreprocessorDefinition>& defs      = pEffectRegistry->getPreprocessorDefs(effectName);
        if (!halfShown && defs.empty())
        {
            if (!anyShown)
            {
                ui::panelRow();
                ImGui::AlignTextToFramePadding();
                ImGui::TextColored(palette.muted, "No parameters");
            }
            return;
        }

        ui::panelRow(ui::Size::control);
        ImGui::AlignTextToFramePadding();
        ui::sectionLabel("This effect");
        if (halfShown)
        {
            ui::panelRow();
            bool half = pEffectRegistry->getAllowHalfPrecision(effectName);
            ImGui::BeginGroup();
            const bool flipped = ui::parameterSwitch("Half precision", &half);
            ImGui::EndGroup();
            ImGui::SetItemTooltip("16-bit math: faster, but it can band or flicker. Applies at the next rebuild.");
            const bool reset = defaultMark("Half precision", half, "off", palette);
            if (flipped || reset)
            {
                pEffectRegistry->setAllowHalfPrecision(effectName, half && !reset);
                markEdited();
            }
        }
        if (defs.empty())
            return;

        ui::panelRow();
        if (!ui::disclosure("preprocessor", "Definitions", palette.text, ImGui::GetContentRegionAvail().x, std::to_string(defs.size()).c_str()))
            return;
        for (size_t i = 0; i < defs.size(); i++)
        {
            ImGui::PushID(static_cast<int>(i + 1000));
            if (pEffectRegistry->isSetGlobally(defs[i].name))
            {
                ui::panelRow(ui::Size::tallRow);
                ImGui::BeginGroup();
                ImGui::TextUnformatted(definitionTitle(defs[i].name).c_str());
                ImGui::PushFont(nullptr, FontSize::caption);
                ImGui::TextColored(palette.faint, "%s", defs[i].name.c_str());
                ImGui::PopFont();
                ImGui::EndGroup();
                const char* global = "set for every effect";
                ImGui::PushFont(nullptr, FontSize::caption);
                const float globalW = ImGui::CalcTextSize(global).x + ui::px(16.0f);
                ImGui::PopFont();
                ui::alignRight(globalW);
                ui::chip(global, palette.secondary);
            }
            else
            {
                bool changed = false;
                renderPreprocessorDef(defs[i], pEffectRegistry, effectName, changed, nullptr);
                if (changed)
                    markDefinitionChanged();
            }
            ImGui::PopID();
        }
        ImGui::TreePop();
    }

    void ImGuiOverlay::renderWarnings(const LayoutRect& rect, const OverlayPalette& palette)
    {
        std::vector<std::string> warnings;
        if (conflictingLayerLoaded())
            warnings.push_back("Upstream vkBasalt runs in this game too, from " + conflictingLayerPath()
                               + ", so effects can apply twice: leave ENABLE_VKBASALT out of its launch options.");
        if (saveFailed)
            warnings.push_back("Profile not saved: changes run now and are lost at next launch.");
        if (settingsSaveFailed)
            warnings.push_back("Settings not saved: changes run now and are lost at next launch.");
        if (shaderMgrSaveFailed)
            warnings.push_back("Shader folders not saved: changes run now and are lost at next launch.");
        if (!isInputBlocked())
            warnings.push_back(isWayland() ? "The game still gets your input: start it with vkdaedalus-run (in Steam: vkdaedalus-run %command%)."
                                           : "The game still gets your input: the overlay could not grab the keyboard and pointer. Started with vkdaedalus-run, it would hold them with no grab.");
        if (warnings.empty())
            return;
        ImDrawList* list  = ImGui::GetWindowDrawList();
        const float s     = rect.h / 32.0f;
        const float size  = FontSize::small * s;
        const ImU32 ink   = ImGui::GetColorU32(palette.warn);
        const float textY = rect.y + (rect.h - size) * 0.5f;
        drawIcon(list, Icon::Warning, ImVec2(rect.x, rect.y + (rect.h - 16.0f * s) * 0.5f), 16.0f * s, ink);
        const ImVec4 clip(rect.x, rect.y, rect.right(), rect.bottom());
        std::string  shown = warnings.front();
        if (warnings.size() > 1)
            shown += ", and " + std::to_string(warnings.size() - 1) + " more";
        list->AddText(fonts.body, size, ImVec2(rect.x + 24.0f * s, textY), ink, shown.c_str(), nullptr, 0.0f, &clip);
        if (Logger::logLevel() <= LogLevel::Trace)
            for (const std::string& warning : warnings)
                Logger::trace("top bar warning: " + warning);
        ImGui::SetCursorScreenPos(topLeft(rect));
        ImGui::InvisibleButton("##warnings", ImVec2(rect.w, rect.h));
        if (ImGui::BeginItemTooltip())
        {
            for (const std::string& warning : warnings)
                ImGui::TextColored(palette.warn, "%s", warning.c_str());
            ImGui::EndTooltip();
        }
    }

    void ImGuiOverlay::renderEffectsSwitchTile(const LayoutRect& rect, const OverlayPalette& palette)
    {
        ImDrawList*     list = ImGui::GetWindowDrawList();
        const float     s    = rect.h / 64.0f;
        const bool      on   = state.effectsEnabled;
        const TileState tile = ui::tile("Effects###effectsswitch", rect, false);
        UiMap::markChecked(ImGui::GetItemID(), on);
        drawIcon(list, Icon::Power, ImVec2(rect.x + 14.0f * s, rect.y + (rect.h - 22.0f * s) * 0.5f), 22.0f * s, ImGui::GetColorU32(on ? palette.accent : palette.faint));
        const float big = 16.0f * s;
        list->AddText(fonts.bold, big, ImVec2(rect.x + 48.0f * s, rect.y + (rect.h - big) * 0.5f), ImGui::GetColorU32(palette.text), on ? "Effects on" : "Effects off");
        const std::string key = settingsManager.getToggleKey();
        const ImVec2      cap = keyCapSize(fonts.body, FontSize::caption * s, key.c_str());
        drawKeyCap(list, fonts.body, FontSize::caption * s, ImVec2(rect.right() - 14.0f * s - cap.x, rect.y + (rect.h - cap.y) * 0.5f), key.c_str(), palette);
        if (tile.clicked)
            toggleEffectsRequested = true;
    }

    void ImGuiOverlay::renderSafeAntiCheatTile(const LayoutRect& rect, const OverlayPalette& palette)
    {
        ImDrawList*        list          = ImGui::GetWindowDrawList();
        const float        s             = rect.h / 64.0f;
        const bool         safeAntiCheat = settingsManager.getSafeAntiCheat();
        const std::string& lock          = settingsManager.getSafeAntiCheatLock();
        const bool         noProfile     = activeGameName.empty();
        const std::optional<std::string> fixed = fixedConfigFile();
        const TileState    tile          = ui::tile("Safe Anti-Cheat", rect, false, lock.empty() && !noProfile);
        UiMap::markChecked(ImGui::GetItemID(), safeAntiCheat);
        if (!lock.empty())
            ImGui::SetItemTooltip("Locked on: this game ships %s.", lock.c_str());
        else if (noProfile)
            ImGui::SetItemTooltip("This program's name could not be read, so there is no file to keep the choice in.");
        else if (fixed)
            ImGui::SetItemTooltip("Running %s from VKDAEDALUS_CONFIG_FILE: a change here runs now and is not kept, since that file decides at every launch.",
                                  fixed->c_str());
        else if (ImGui::BeginItemTooltip())
        {
            ImGui::Text("Effects cannot read depth, so none can see through walls.");
            ImGui::Text("Effects that need depth are marked and blocked.");
            ImGui::TextColored(palette.warn, "vkDaedalus is still visible to anti-cheat software.");
            ImGui::EndTooltip();
        }
        drawIcon(list, Icon::Shield, ImVec2(rect.x + 14.0f * s, rect.y + (rect.h - 22.0f * s) * 0.5f), 22.0f * s,
                 ImGui::GetColorU32(safeAntiCheat ? palette.accent : palette.faint));
        const float big = 16.0f * s;
        const float cap = FontSize::caption * s;
        const float top = rect.y + (rect.h - big - cap - 2.0f * s) * 0.5f;
        list->AddText(fonts.body, cap, ImVec2(rect.x + 48.0f * s, top), ImGui::GetColorU32(palette.muted), "Safe Anti-Cheat");
        list->AddText(fonts.bold, big, ImVec2(rect.x + 48.0f * s, top + cap + 2.0f * s), ImGui::GetColorU32(palette.text), safeAntiCheat ? "On" : "Off");
        if (!lock.empty())
            drawIcon(list, Icon::Lock, ImVec2(rect.right() - 30.0f * s, rect.y + (rect.h - 16.0f * s) * 0.5f), 16.0f * s, ImGui::GetColorU32(palette.muted));
        if (tile.clicked && lock.empty() && !noProfile)
        {
            settingsManager.setSafeAntiCheat(!safeAntiCheat);
            if (!fixed)
                ConfigSerializer::saveGameSettings(activeGameName, {settingsManager.getSafeAntiCheat()});
            applyRequested = true;
            lastChangeTime = std::chrono::steady_clock::now();
        }
    }

    void ImGuiOverlay::refreshProfileList()
    {
        if (!profileListStale)
            return;
        profileList      = ConfigSerializer::listProfiles();
        profileGames     = ConfigSerializer::profileUsers();
        profileListStale = false;
    }

    void ImGuiOverlay::renderProfileTile(const LayoutRect& rect, const OverlayPalette& palette)
    {
        ImDrawList* list        = ImGui::GetWindowDrawList();
        const float s           = rect.h / 64.0f;
        const float big         = 16.0f * s;
        const float cap         = FontSize::caption * s;
        const float top         = rect.y + (rect.h - big - cap - 2.0f * s) * 0.5f;
        const auto  drawContent = [&](const std::string& caption, const std::string& value, bool open) {
            const ImVec4 clip(rect.x, rect.y, rect.right() - 34.0f * s, rect.bottom());
            drawIcon(list, Icon::Person, ImVec2(rect.x + 14.0f * s, rect.y + (rect.h - 22.0f * s) * 0.5f), 22.0f * s, ImGui::GetColorU32(open ? palette.accent : palette.secondary));
            list->AddText(fonts.body, cap, ImVec2(rect.x + 48.0f * s, top), ImGui::GetColorU32(palette.muted), caption.c_str(), nullptr, 0.0f, &clip);
            list->AddText(fonts.bold, big, ImVec2(rect.x + 48.0f * s, top + cap + 2.0f * s), ImGui::GetColorU32(open ? palette.bright : palette.text), value.c_str(), nullptr, 0.0f, &clip);
            ui::drawChevron(list, ImVec2(rect.right() - 22.0f * s, rect.y + rect.h * 0.5f), 16.0f * s, open ? 3.0f : 1.0f, ImGui::GetColorU32(palette.muted));
        };

        if (activeGameName.empty() || activeProfileName.empty())
        {
            ui::tile("Profile###profiletile", rect, false, false);
            const std::optional<std::string> fixed = fixedConfigFile();
            if (activeGameName.empty())
                ImGui::SetItemTooltip("This program's name could not be read, so it has no profiles.");
            else
                ImGui::SetItemTooltip("Running %s from VKDAEDALUS_CONFIG_FILE: changes here run now and are not saved.", fixed ? fixed->c_str() : "a fixed config file");
            drawContent(activeGameName.empty() ? "Profile" : "Config file",
                        activeGameName.empty() ? "none" : (fixed ? std::filesystem::path(*fixed).filename().string() : std::string("fixed")), false);
            return;
        }

        refreshProfileList();
        const auto gamesOf = [&](const std::string& profile) {
            const auto found = profileGames.find(profile);
            return found == profileGames.end() ? std::vector<std::string>{} : found->second;
        };
        const auto switchTo = [&](const std::string& profile, const std::string& renamedFrom) {
            activeProfileName = profile;
            activeProfilePath = profile == DEFAULT_PROFILE_NAME ? ConfigSerializer::ensureDefaultProfile() : ConfigSerializer::getProfilePath(profile);
            pendingConfigPath = activeProfilePath;
            applyRequested    = true;
            profileListStale  = true;
            frameGenerationAssignmentsChanged(renamedFrom);
        };
        if (std::exchange(comeBackSwitchPending, false))
            switchTo(comeBackProfile, "");

        bool            openOffProfile    = false;
        bool            openNewProfile    = false;
        bool            openRenameProfile = false;
        bool            openDeleteProfile = false;
        const TileState opener            = ui::tile("##profile", rect, profileMenuWasOpen);
        if (ImGui::IsItemActivated())
            profileMenuClosedByPress = profileMenuWasOpen;
        if (opener.clicked && !profileMenuClosedByPress)
            ImGui::OpenPopup("ProfileMenu");
        if (opener.hovered && !profileMenuWasOpen)
            ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
        ImGui::SetNextWindowPos(ImVec2(rect.x, rect.bottom() + ui::px(4.0f)), ImGuiCond_Appearing);
        ImGui::SetNextWindowSizeConstraints(ImVec2(rect.w, 0.0f), ImVec2(FLT_MAX, FLT_MAX));
        const bool menuOpen = ui::beginPopup("ProfileMenu");
        drawContent("Profile for " + activeGameName, activeProfileName, menuOpen);
        if (menuOpen)
        {
            for (size_t p = 0; p < profileList.size(); p++)
            {
                const std::string& profile  = profileList[p];
                const bool         selected = profile == activeProfileName;
                if (profile == BYPASS_PROFILE_NAME && p > 0)
                    ui::menuSeparator();
                const size_t      users = gamesOf(profile).size();
                const std::string shown = profile == BYPASS_PROFILE_NAME
                                            ? profile + " (vkDaedalus off)"
                                            : profile + " (" + std::to_string(users) + (users == 1 ? " game)" : " games)") + "###profile " + profile;
                if (ui::option(shown.c_str(), selected) && !selected)
                {
                    if (profileDirty)
                        autoSaveProfile();
                    paramsDirty = false;
                    if (profile == BYPASS_PROFILE_NAME)
                        openOffProfile = true;
                    else
                    {
                        ConfigSerializer::setActiveProfile(activeGameName, profile);
                        switchTo(profile, "");
                    }
                }
                if (profile == BYPASS_PROFILE_NAME)
                    ImGui::SetItemTooltip("Asks first. Stops the effects and the frame copy now, and from the next launch nothing of vkDaedalus loads in this game");
                if (profile != BYPASS_PROFILE_NAME && users > 0)
                {
                    std::string list;
                    for (const std::string& game : gamesOf(profile))
                        list += (list.empty() ? "" : ", ") + game;
                    ImGui::SetItemTooltip("Used by %s", list.c_str());
                }
                if (selected)
                    ImGui::SetItemDefaultFocus();
            }
            ui::menuSeparator();
            if (ui::option("New...##newprofile", false))
                openNewProfile = true;
            const bool fixedProfile = activeProfileName == DEFAULT_PROFILE_NAME || activeProfileName == BYPASS_PROFILE_NAME;
            const char* keptWhy     = activeProfileName == DEFAULT_PROFILE_NAME ? "default is kept: every game without a profile of its own uses it"
                                                                                : "none is kept: it turns vkDaedalus off for a game";
            if (ui::option(("Rename " + activeProfileName + "...##renameprofile").c_str(), false, !fixedProfile))
                openRenameProfile = true;
            if (fixedProfile)
                ImGui::SetItemTooltip("%s", keptWhy);
            if (ui::option(("Delete " + activeProfileName + "...##delprofile").c_str(), false, !fixedProfile))
                openDeleteProfile = true;
            if (fixedProfile)
                ImGui::SetItemTooltip("%s", keptWhy);
            ui::endPopup();
        }
        profileMenuWasOpen = menuOpen;
        if (!ImGui::IsMouseDown(ImGuiMouseButton_Left))
            profileMenuClosedByPress = false;
        if (activeProfileName == BYPASS_PROFILE_NAME)
            ImGui::SetItemTooltip("vkDaedalus off from the next launch");

        const ImVec2 anchor(rect.x, rect.bottom() + ui::px(4.0f));
        const float  dialogWidth = ui::px(340.0f);
        const auto   buttonRow   = [&](const char* cancelLabel, const char* primaryLabel, ui::Kind kind, bool enabled, const char* why) {
            const float gap      = ImGui::GetStyle().ItemSpacing.x;
            const float cancelW  = ui::buttonWidth(cancelLabel);
            const float primaryW = ui::buttonWidth(primaryLabel, kind);
            ui::rightAlign(cancelW + gap + primaryW);
            const bool cancelled = ui::button(cancelLabel);
            ImGui::SameLine();
            ImGui::BeginDisabled(!enabled);
            const bool confirmed = ui::button(primaryLabel, kind);
            ImGui::EndDisabled();
            if (!enabled && why != nullptr)
                ImGui::SetItemTooltip("%s", why);
            return std::make_pair(cancelled, confirmed);
        };
        if (openOffProfile)
            ImGui::OpenPopup("OffProfilePopup");
        if (openNewProfile)
            ImGui::OpenPopup("NewProfilePopup");
        if (openRenameProfile)
            ImGui::OpenPopup("RenameProfilePopup");
        if (openDeleteProfile)
            ImGui::OpenPopup("DeleteProfilePopup");

        ImGui::SetNextWindowPos(anchor, ImGuiCond_Appearing);
        ImGui::SetNextWindowSize(ImVec2(dialogWidth, 0.0f));
        if (ui::beginPopup("OffProfilePopup"))
        {
            ui::popupTitle(("Turn vkDaedalus off for " + activeGameName + "?").c_str());
            ui::note(("The effects stop now, and from the next launch nothing of vkDaedalus loads in " + activeGameName + ". Until the game closes, " +
                      settingsManager.getOverlayKey() + " brings it back.")
                         .c_str());
            ui::note("Afterwards, in Steam's launch options:");
            ui::note("vkdaedalus-run --profile default %command%");
            ui::note("or in a terminal:");
            ui::note(("vkdaedalus-profile " + activeGameName + " default").c_str());
            const auto [cancelled, confirmed] = buttonRow("Cancel##offprofile", "Turn off", ui::Kind::Danger, true, nullptr);
            if (confirmed)
            {
                if (ConfigSerializer::setActiveProfile(activeGameName, BYPASS_PROFILE_NAME))
                {
                    profileBeforeOff = activeProfileName;
                    activeProfileName = BYPASS_PROFILE_NAME;
                    activeProfilePath.clear();
                    profileListStale       = true;
                    offInThisRunRequested  = true;
                    frameGenerationAssignmentsChanged("");
                }
                else
                    Logger::warn("configs/.active_profiles could not be written, so " + activeGameName + " keeps " + activeProfileName);
                ImGui::CloseCurrentPopup();
            }
            if (cancelled)
                ImGui::CloseCurrentPopup();
            ui::endPopup();
        }

        ImGui::SetNextWindowPos(anchor, ImGuiCond_Appearing);
        ImGui::SetNextWindowSize(ImVec2(dialogWidth, 0.0f));
        if (ui::beginPopup("NewProfilePopup"))
        {
            static char        newProfileName[64] = "";
            static std::string newProfileProblem;
            ui::popupTitle("New profile");
            ui::note(("A copy of " + activeProfileName + "; this game switches to it.").c_str());
            if (ui::textField("##newprofname", newProfileName, sizeof(newProfileName), "name", ImGui::GetContentRegionAvail().x))
                newProfileProblem.clear();
            if (!newProfileProblem.empty())
                ui::errorText(newProfileProblem.c_str());
            const auto [cancelled, create] = buttonRow("Cancel##newprofile", "Create", ui::Kind::Primary, newProfileName[0] != '\0', "Type a name first");
            if (create)
            {
                if (profileDirty)
                    autoSaveProfile();
                const std::string wanted = newProfileName;
                newProfileProblem        = ConfigSerializer::createProfile(wanted, activeProfileName);
                if (newProfileProblem.empty() && !ConfigSerializer::setActiveProfile(activeGameName, wanted))
                    newProfileProblem = "made, but configs/.active_profiles could not be written, so this game keeps " + activeProfileName;
                if (newProfileProblem.empty())
                {
                    switchTo(wanted, "");
                    newProfileName[0] = '\0';
                    ImGui::CloseCurrentPopup();
                }
                else
                    Logger::info("new profile refused: " + newProfileProblem);
            }
            if (cancelled)
            {
                newProfileProblem.clear();
                ImGui::CloseCurrentPopup();
            }
            ui::endPopup();
        }

        ImGui::SetNextWindowPos(anchor, ImGuiCond_Appearing);
        ImGui::SetNextWindowSize(ImVec2(dialogWidth, 0.0f));
        if (ui::beginPopup("RenameProfilePopup"))
        {
            static char        renamedTo[64] = "";
            static std::string renameProblem;
            if (ImGui::IsWindowAppearing())
            {
                std::snprintf(renamedTo, sizeof(renamedTo), "%s", activeProfileName.c_str());
                renameProblem.clear();
            }
            const size_t users = gamesOf(activeProfileName).size();
            ui::popupTitle(("Rename " + activeProfileName).c_str());
            ui::note((std::to_string(users) + (users == 1 ? " game keeps it" : " games keep it")).c_str());
            if (ui::textField("##renameprofile", renamedTo, sizeof(renamedTo), "new name", ImGui::GetContentRegionAvail().x))
                renameProblem.clear();
            if (!renameProblem.empty())
                ui::errorText(renameProblem.c_str());
            const bool changedName          = renamedTo[0] != '\0' && activeProfileName != renamedTo;
            const auto [cancelled, renamed] = buttonRow("Cancel##renameprofile", "Rename", ui::Kind::Primary, changedName, "Type another name first");
            if (renamed)
            {
                if (profileDirty)
                    autoSaveProfile();
                const std::string wanted  = renamedTo;
                const std::string renamed = activeProfileName;
                renameProblem             = ConfigSerializer::renameProfile(renamed, wanted);
                if (renameProblem.empty())
                {
                    switchTo(wanted, renamed);
                    ImGui::CloseCurrentPopup();
                }
                else
                    Logger::info("rename refused: " + renameProblem);
            }
            if (cancelled)
                ImGui::CloseCurrentPopup();
            ui::endPopup();
        }

        ImGui::SetNextWindowPos(anchor, ImGuiCond_Appearing);
        ImGui::SetNextWindowSize(ImVec2(dialogWidth, 0.0f));
        if (ui::beginPopup("DeleteProfilePopup"))
        {
            static std::string deleteProblem;
            if (ImGui::IsWindowAppearing())
                deleteProblem.clear();
            const std::vector<std::string> users = gamesOf(activeProfileName);
            std::string                    goBack;
            for (const std::string& game : users)
                goBack += (goBack.empty() ? "" : ", ") + game;
            ui::popupTitle(("Delete profile " + activeProfileName + "?").c_str());
            ui::note(((goBack.empty() ? activeGameName : goBack) + (users.size() > 1 ? " go" : " goes") + " back to default; its file is deleted").c_str());
            if (!deleteProblem.empty())
                ui::errorText(deleteProblem.c_str());
            const auto [cancelled, confirmed] = buttonRow("Cancel", "Yes, delete", ui::Kind::Danger, true, nullptr);
            if (confirmed)
            {
                deleteProblem = ConfigSerializer::deleteProfile(activeProfileName);
                if (deleteProblem.empty())
                {
                    profileDirty = false;
                    paramsDirty  = false;
                    switchTo(DEFAULT_PROFILE_NAME, "");
                    ImGui::CloseCurrentPopup();
                }
                else
                    Logger::info("delete refused: " + deleteProblem);
            }
            if (cancelled)
                ImGui::CloseCurrentPopup();
            ui::endPopup();
        }
    }

    void ImGuiOverlay::renderMainView(const KeyboardState& keyboard)
    {
        if (!pEffectRegistry)
            return;

        std::vector<std::string> selectedEffects = pEffectRegistry->getSelectedEffects();
        const OverlayPalette&    palette         = activePalette();
        const ImGuiStyle&        style           = ImGui::GetStyle();

        ui::heading("Effects");
        ui::alignRight(ui::buttonWidth("Add effects..."));
        if (ui::button("Add effects..."))
        {
            inSelectionMode = true;
            insertPosition.reset();
            pendingAddEffects.clear();
        }

        const std::vector<std::string> compiling = effectCompileJobs.compiling();
        if (compiling.empty())
            compilingSince.reset();
        else if (!compilingSince)
            compilingSince = std::chrono::steady_clock::now();
        if (compilingSince && std::chrono::steady_clock::now() - *compilingSince >= std::chrono::seconds(1))
        {
            std::string names;
            for (const std::string& name : compiling)
                names += (names.empty() ? "" : ", ") + name;
            ImGui::AlignTextToFramePadding();
            ImGui::TextColored(palette.warn, "Compiling: %s", names.c_str());
            ImGui::SameLine();
            if (ui::lineButton("Cancel##compiling"))
                effectCompileJobs.cancel();
            ImGui::SetItemTooltip("Stops the effects still compiling. The one compiling now finishes\nand stays in compiled effects; the rest never start.");
        }

        const float  listWidth = ImGui::GetContentRegionAvail().x;
        const float  margin    = ui::px(ui::Size::gap);
        ImGuiWindow* card      = ImGui::GetCurrentWindow();
        ImGui::PushClipRect(card->Pos, ImVec2(card->Pos.x + card->Size.x, card->Pos.y + card->Size.y), false);
        ImGui::SetNextWindowContentSize(ImVec2(listWidth - 2.0f * margin, 0.0f));
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(margin, 0.0f));
        ImGui::BeginChild("EffectList", ImVec2(listWidth, ui::heightLeftAboveFooter()), ImGuiChildFlags_AlwaysUseWindowPadding);
        ImGui::PopStyleVar();
        ImGui::PushTextWrapPos(0.0f);

        dragTargetIndex.reset();
        std::optional<size_t>                    removeIndex;
        std::optional<std::pair<size_t, size_t>> moveIndex;

        std::vector<uint32_t> ownGpus;
        for (const std::string& name : selectedEffects)
            ownGpus.push_back(pEffectRegistry ? pEffectRegistry->getEffectGpu(name) : 0);
        const std::vector<uint32_t> rowGpus     = effectiveGpus(ownGpus);
        const bool                  crossing    = std::any_of(rowGpus.begin(), rowGpus.end(), [](uint32_t gpu) { return gpu != gameGpu; });
        const bool                  severalGpus = state.gpus.size() > 1;

        const float rowH   = ui::px(ui::Size::row);
        const float lineH  = ui::px(ui::Size::control);
        const float inRowH = ui::px(ui::Size::inRow);
        const float gap    = ui::px(ui::Size::gap);

        if (selectedEffects.empty())
            ui::note("No effects yet");

        for (size_t i = 0; i < selectedEffects.size(); i++)
        {
            const std::string& effectName = selectedEffects[i];
            ImGui::PushID(effectName.c_str());

            const ImVec2 rowMin   = ImGui::GetCursorScreenPos();
            const float  rowWidth = ImGui::GetContentRegionAvail().x;
            const ImVec2 rowMax(rowMin.x + rowWidth, rowMin.y + rowH);
            const ImVec2 panelMin(rowMin.x - margin, rowMin.y);
            const ImVec2 panelMax(rowMax.x + margin, rowMax.y);
            const bool   rowHovered = ImGui::IsMouseHoveringRect(panelMin, panelMax) && ImGui::IsWindowHovered(ImGuiHoveredFlags_AllowWhenBlockedByActiveItem);
            const float  rowHover   = ui::eased(ImGui::GetID("row"), "hover", rowHovered);
            ImGuiWindow* list       = ImGui::GetCurrentWindow();
            list->DrawList->PushClipRect(list->Pos, ImVec2(list->Pos.x + list->Size.x, list->Pos.y + list->Size.y), true);
            const bool   opened     = ui::disclosureOpen("effect");
            list->DrawList->AddRectFilled(panelMin, panelMax, ImGui::GetColorU32(ui::mix(palette.row, ui::mix(palette.row, palette.accent, 0.07f), rowHover)),
                                          ui::px(opened ? ui::Size::surface : ui::Size::radius), opened ? ImDrawFlags_RoundCornersTop : ImDrawFlags_RoundCornersAll);
            if (isDragging && dragSourceIndex != i && rowHovered)
            {
                dragTargetIndex = i;
                list->DrawList->AddRect(panelMin, panelMax, ImGui::GetColorU32(palette.accent), ui::px(ui::Size::radius), 1.0f);
            }
            list->DrawList->PopClipRect();

            const bool           effectFailed = pEffectRegistry ? pEffectRegistry->hasEffectFailed(effectName) : false;
            const EffectOnDevice onDevice     = pEffectRegistry ? pEffectRegistry->getEffectOnDevice(effectName, pLogicalDevice) : EffectOnDevice{};
            const std::string    leftOut      = onDevice.readsDepth && !settingsManager.getSafeAntiCheat() ? "" : onDevice.leftOut;
            std::string          notes;
            for (const std::string& notice : onDevice.notices)
                notes += (notes.empty() ? "" : "\n") + notice;
            const std::string effectError = effectFailed && pEffectRegistry ? pEffectRegistry->getEffectError(effectName) : "";

            const float lineY = rowMin.y + (rowH - lineH) * 0.5f;
            ImGui::SetCursorScreenPos(ImVec2(rowMin.x, lineY));
            bool effectEnabled = pEffectRegistry ? pEffectRegistry->isEffectEnabled(effectName) : true;
            if (ui::checkbox("##enabled", &effectEnabled))
            {
                if (pEffectRegistry)
                    pEffectRegistry->setEffectEnabled(effectName, effectEnabled);
                markEdited();
            }

            const auto  timing          = state.effectGpuMilliseconds.find(effectName);
            const float dotsWidth       = inRowH;
            const float dotsX           = rowMax.x - dotsWidth;
            char        timingLabel[32] = "";
            if (timing != state.effectGpuMilliseconds.end())
                std::snprintf(timingLabel, sizeof(timingLabel), "%.2f ms", timing->second);
            ImGui::PushFont(nullptr, FontSize::small);
            const float timingWidth = timingLabel[0] ? ImGui::CalcTextSize("88.88 ms").x : 0.0f;
            const float timingText  = timingLabel[0] ? ImGui::CalcTextSize(timingLabel).x : 0.0f;
            ImGui::PopFont();
            const float timingX = dotsX - gap - timingWidth;
            const bool  showGpu = severalGpus && crossing;
            char        gpuTag[16];
            std::snprintf(gpuTag, sizeof(gpuTag), "GPU %u", rowGpus[i]);
            const bool  stray      = showGpu && rowGpus[i] == gameGpu && i > 0 && i + 1 < rowGpus.size() && rowGpus[i - 1] == rowGpus[i + 1] && rowGpus[i - 1] != gameGpu;
            const float pillWidth  = showGpu ? ImGui::CalcTextSize(gpuTag).x + ui::px(20.0f) + (stray ? ui::px(12.0f) : 0.0f) : 0.0f;
            const float pillX      = (timingLabel[0] ? timingX : dotsX) - gap - pillWidth;
            const float rightStart = showGpu ? pillX : (timingLabel[0] ? timingX : dotsX);
            const std::string authorNote = pEffectRegistry ? pEffectRegistry->authorText(effectName) : "";
            const float       infoX      = rightStart - gap - inRowH;
            const float       nameEnd    = authorNote.empty() ? rightStart : infoX;

            const bool        refusedHere = !onDevice.gpuRefusal.empty() && rowGpus[i] != gameGpu;
            const bool        marked      = effectFailed || !leftOut.empty() || !notes.empty() || refusedHere;
            const ImVec4      ink         = !marked ? palette.text : effectFailed ? palette.bad : palette.warn;
            const std::string onGpu       = onDevice.gpu > gameGpu ? "GPU " + std::to_string(onDevice.gpu) : "this GPU";
            const std::string mark        = effectFailed      ? " (failed)"
                                          : !leftOut.empty() ? (onDevice.readsDepth ? " (blocked by Safe Anti-Cheat)" : " (left out on " + onGpu + ")")
                                          : !notes.empty()   ? " (runs differently on " + onGpu + ")"
                                          : refusedHere      ? " (on GPU 1)"
                                                             : "";
            const std::string shown       = effectName + mark;
            ImGui::SameLine(0.0f, gap);
            const bool treeOpen = ui::disclosure("effect", shown.c_str(), ink, nameEnd - gap - ImGui::GetCursorScreenPos().x);
            if (ImGui::IsItemClicked(ImGuiMouseButton_Right))
                ImGui::OpenPopup("effect_context");
            if (effectFailed)
                ImGui::SetItemTooltip("%s", effectError.c_str());
            else if (!leftOut.empty() || !notes.empty())
                ImGui::SetItemTooltip("%s", leftOut.empty() ? notes.c_str() : leftOut.c_str());
            else if (refusedHere)
                ImGui::SetItemTooltip("Planned for GPU %u, runs on GPU 1: %s", rowGpus[i], onDevice.gpuRefusal.c_str());
            if (ImGui::IsItemActive() && ImGui::IsMouseDragging(0) && !isDragging)
            {
                isDragging      = true;
                dragSourceIndex = i;
            }

            const float inRowY = rowMin.y + (rowH - inRowH) * 0.5f;
            if (!authorNote.empty())
            {
                ImGui::SetCursorScreenPos(ImVec2(infoX, inRowY));
                const std::string infoName = effectName + " info";
                ImGui::InvisibleButton(infoName.c_str(), ImVec2(inRowH, inRowH));
                const bool   infoHovered = ImGui::IsItemHovered();
                const ImVec2 centre((ImGui::GetItemRectMin().x + ImGui::GetItemRectMax().x) * 0.5f, (ImGui::GetItemRectMin().y + ImGui::GetItemRectMax().y) * 0.5f);
                ImGui::GetWindowDrawList()->AddCircle(centre, inRowH * 0.38f, ImGui::GetColorU32(infoHovered ? palette.accent : palette.edge), 0, 1.0f);
                ImGui::PushFont(nullptr, FontSize::caption);
                const ImVec2 glyph = ImGui::CalcTextSize("i");
                ImGui::GetWindowDrawList()->AddText(ImVec2(centre.x - glyph.x * 0.5f, centre.y - glyph.y * 0.5f), ImGui::GetColorU32(palette.secondary), "i");
                ImGui::PopFont();
                if (ImGui::BeginItemTooltip())
                {
                    ImGui::PushTextWrapPos(ImGui::GetFontSize() * 30.0f);
                    ui::popupTitle(("About " + effectName + ", from its author").c_str());
                    ImGui::TextUnformatted(authorNote.c_str());
                    ImGui::PopTextWrapPos();
                    ImGui::EndTooltip();
                }
            }
            if (showGpu)
            {
                ImGui::SetCursorScreenPos(ImVec2(pillX, inRowY));
                const ImVec4 strayBorder = palette.warn;
                if (ui::rowButton((std::string(gpuTag) + "##gpu").c_str(), pillWidth, stray ? &strayBorder : nullptr))
                    ImGui::OpenPopup("gpu_picker");
                const ImVec2 pillBottom(ImGui::GetItemRectMin().x, ImGui::GetItemRectMax().y + ui::px(4.0f));
                if (stray)
                {
                    const ImVec2 at = ImGui::GetItemRectMax();
                    ImGui::GetWindowDrawList()->AddCircleFilled(ImVec2(at.x - ui::px(10.0f), (ImGui::GetItemRectMin().y + at.y) * 0.5f), ui::px(3.0f), ImGui::GetColorU32(palette.warn));
                    ImGui::SetItemTooltip("Between two GPU %u effects, so the frame goes to GPU %u and back twice", rowGpus[i - 1], rowGpus[i - 1]);
                }
                else if (ownGpus[i] != 0)
                    ImGui::SetItemTooltip("This effect is set to run on GPU %u.", rowGpus[i]);
                else if (i == 0)
                    ImGui::SetItemTooltip("Runs on GPU 1, the game's GPU.");
                else
                    ImGui::SetItemTooltip("Runs where the effect above it runs, on GPU %u.", rowGpus[i]);
                ImGui::SetNextWindowPos(pillBottom, ImGuiCond_Appearing);
                if (ui::beginPopup("gpu_picker"))
                {
                    renderGpuChoices(effectName, i == 0 ? gameGpu : rowGpus[i - 1]);
                    ui::endPopup();
                }
            }
            if (timingLabel[0])
            {
                ImGui::PushFont(nullptr, FontSize::small);
                ImGui::SetCursorScreenPos(ImVec2(timingX + std::max(0.0f, timingWidth - timingText), rowMin.y + (rowH - ImGui::GetFontSize()) * 0.5f));
                ImGui::TextColored(palette.faint, "%s", timingLabel);
                ImGui::PopFont();
                ImGui::SetItemTooltip("GPU time for this effect, smoothed");
            }
            ImGui::SetCursorScreenPos(ImVec2(dotsX, inRowY));
            if (ui::moreButton("...##actions", dotsWidth, ImGui::IsPopupOpen("effect_context")))
                ImGui::OpenPopup("effect_context");
            ImGui::SetItemTooltip(severalGpus ? "Reset, insert, move, GPU, remove (or right-click the name)" : "Reset, insert, move, remove (or right-click the name)");
            ImGui::SetNextWindowPos(ImVec2(dotsX + dotsWidth, inRowY + inRowH + ui::px(4.0f)), ImGuiCond_Appearing, ImVec2(1.0f, 0.0f));
            if (ui::beginPopup("effect_context"))
            {
                if (ui::option("Reset to defaults", false))
                {
                    for (auto* param : pEffectRegistry->getParametersForEffect(effectName))
                    {
                        FieldEditor* editor = FieldEditorFactory::instance().getEditor(param->getType());
                        if (editor)
                            editor->resetToDefault(*param);
                    }
                    markEffectValuesChanged(effectName);
                }

                ui::menuSeparator();

                if (ui::option("Insert effects here...", false))
                {
                    insertPosition  = i;
                    inSelectionMode = true;
                    pendingAddEffects.clear();
                }

                if (ui::option("Move up", false, i > 0))
                    moveIndex = std::make_pair(i, i - 1);
                if (i == 0)
                    ImGui::SetItemTooltip("First in the chain");
                if (ui::option("Move down", false, i + 1 < selectedEffects.size()))
                    moveIndex = std::make_pair(i, i + 1);
                if (i + 1 == selectedEffects.size())
                    ImGui::SetItemTooltip("Last in the chain");

                renderGpuMenu(effectName, i == 0 ? gameGpu : rowGpus[i - 1]);

                ui::menuSeparator();
                if (ui::option("Remove", false, true, &palette.bad))
                    removeIndex = i;

                ui::endPopup();
            }

            ImGui::SetCursorScreenPos(rowMin);
            ImGui::Dummy(ImVec2(rowWidth, rowH));

            ImGui::PopID();

            if (!treeOpen)
                continue;

            const float contentIndent = ui::px(ui::Size::box) + gap;
            ImGui::SetCursorScreenPos(ImVec2(rowMin.x, rowMax.y));
            ui::beginPanelUnder(margin);
            ImGui::Indent(contentIndent);
            if (effectFailed)
            {
                ui::panelRow();
                ui::errorText(("Error: " + effectError).c_str());
            }
            else if (pEffectRegistry)
                renderEffectRows(effectName);
            ImGui::Unindent(contentIndent);
            ui::endPanel();
            ImGui::TreePop();
        }

        if (removeIndex && *removeIndex < selectedEffects.size())
        {
            const std::string removedName = selectedEffects[*removeIndex];
            selectedEffects.erase(selectedEffects.begin() + static_cast<std::ptrdiff_t>(*removeIndex));
            pEffectRegistry->setSelectedEffects(selectedEffects);
            pEffectRegistry->removeEffect(removedName);
            markEdited();
        }

        if (moveIndex && moveIndex->first < selectedEffects.size() && moveIndex->second < selectedEffects.size())
        {
            std::swap(selectedEffects[moveIndex->first], selectedEffects[moveIndex->second]);
            pEffectRegistry->setSelectedEffects(selectedEffects);
            markEdited();
        }

        if (isDragging && dragSourceIndex && *dragSourceIndex < selectedEffects.size())
        {
            ImGui::SetTooltip("Moving: %s", selectedEffects[*dragSourceIndex].c_str());

            if (!ImGui::IsMouseDown(0))
            {
                if (dragTargetIndex && *dragTargetIndex != *dragSourceIndex)
                {
                    std::string moving = selectedEffects[*dragSourceIndex];
                    selectedEffects.erase(selectedEffects.begin() + *dragSourceIndex);
                    selectedEffects.insert(selectedEffects.begin() + *dragTargetIndex, moving);
                    pEffectRegistry->setSelectedEffects(selectedEffects);
                    markEdited();
                }
                isDragging = false;
                dragSourceIndex.reset();
                dragTargetIndex.reset();
            }
        }

        ImGui::Dummy(ImVec2(0.0f, ui::px(ui::Size::groupGap)));
        renderGlobalDefinitionsView();

        ImGui::PopTextWrapPos();
        ImGui::EndChild();
        ImGui::PopClipRect();

        ui::footer();
        ImGui::AlignTextToFramePadding();
        if (activeGameName.empty())
            ui::note("No profile for this program");
        else if (activeProfileName.empty())
        {
            const std::optional<std::string> fixed = fixedConfigFile();
            ui::notef("Config file %s, not saved", fixed ? std::filesystem::path(*fixed).filename().string().c_str() : "fixed");
        }
        else
        {
            refreshProfileList();
            const auto   found = profileGames.find(activeProfileName);
            const size_t games = found == profileGames.end() ? 0 : found->second.size();
            ui::notef("Profile %s, %zu %s", activeProfileName.c_str(), games, games == 1 ? "game" : "games");
        }
        const float applyWidth  = std::max(ui::px(96.0f), ui::buttonWidth("Apply", ui::Kind::Primary));
        const bool  justApplied = appliedAt && std::chrono::steady_clock::now() - *appliedAt < std::chrono::milliseconds(1600);
        if (justApplied)
        {
            const float doneWidth = ImGui::CalcTextSize("Applied").x;
            ui::alignRight(doneWidth + style.ItemSpacing.x + applyWidth);
            ImGui::AlignTextToFramePadding();
            ImGui::TextColored(palette.ok, "Applied");
        }
        else if (waitingForApply || (paramsDirty && !settingsManager.getAutoApply()))
        {
            const char* waiting  = "Waits for Apply";
            const float waitingW = ImGui::CalcTextSize(waiting).x;
            ui::alignRight(waitingW + style.ItemSpacing.x + applyWidth);
            const ImVec2 at = ImGui::GetCursorScreenPos();
            ImGui::AlignTextToFramePadding();
            ImGui::TextColored(palette.warn, "%s", waiting);
            UiMap::nameNextItem(waiting);
            ImGui::ItemAdd(ImRect(at, ImVec2(at.x + waitingW, at.y + ui::px(ui::Size::control))), ImGui::GetID("##waiting"));
        }
        ui::alignRight(applyWidth);
        if (ui::button("Apply", ui::Kind::Primary, applyWidth))
        {
            applyRequested = true;
            paramsDirty    = false;
            profileDirty   = true;
            appliedAt      = std::chrono::steady_clock::now();
        }
        ImGui::SetItemTooltip("Apply (%s)", settingsManager.getReloadKey().c_str());
    }

}
