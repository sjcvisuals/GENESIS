#include "editor/AudioReactiveUi.hpp"

#include "editor/UiWrap.hpp"
#include "platform/FileDialog.hpp"
#include "protocol/OscTypes.hpp"

#include <imgui.h>

#include <algorithm>
#include <cstdio>
#include <filesystem>
#include <string>
#include <vector>

namespace genesis {

namespace {

void drawMeter(const char* label, float value) {
    const float label_col =
        std::max(ImGui::CalcTextSize("Treble").x, ImGui::CalcTextSize(label).x) + 12.0f;
    ImGui::TextUnformatted(label);
    // Font-relative column (not fixed 72px); stack when the panel is too narrow.
    if (ImGui::GetWindowContentRegionMax().x - label_col > 40.0f) {
        ImGui::SameLine(label_col);
    }
    ImGui::ProgressBar(std::clamp(value, 0.0f, 1.0f), ImVec2(-1.0f, 0.0f), "");
}

struct CaptureDevicePickerState {
    std::vector<AudioCapture::DeviceInfo> devices;
    int selected = 0; // 0 = system default; 1..N = devices[i-1]
    bool loaded_once = false;

    void refresh() {
        devices = AudioCapture::listRecordingDevices();
        if (selected > static_cast<int>(devices.size())) {
            selected = 0;
        }
        loaded_once = true;
    }

    std::uint32_t selectedDeviceId() const {
        if (selected <= 0 || selected > static_cast<int>(devices.size())) {
            return 0; // sentinel → startDefaultCapture
        }
        return devices[static_cast<std::size_t>(selected - 1)].id;
    }
};

} // namespace

void drawAudioReactivePanel(AudioReactive& reactive, bool* open,
                            const std::string& selected_particle_node_path) {
    ImGui::SetNextWindowSizeConstraints(ImVec2(280.0f, 200.0f), ImVec2(10000.0f, 10000.0f));
    ImGui::Begin("Audio", open);

    ImGui::TextColored(ImVec4(0.55f, 0.85f, 0.65f, 1.0f), "ANALYSER");
    ImGui::Separator();
    ImGui::TextWrapped(
        "Live mic / line-in or a WAV file → bass / mid / treble / onset values that drive scene "
        "params (fluids, lights, particles). FFT is 512-point, once per frame.");

    AudioReactive::Settings& settings = reactive.settings();
    ImGui::Checkbox("Enabled", &settings.enabled);
    const float gain_w = std::min(160.0f, std::max(80.0f, ImGui::GetContentRegionAvail().x * 0.45f));
    uiSameLineIfFits(gain_w + ImGui::CalcTextSize("Master gain").x + 8.0f);
    ImGui::SetNextItemWidth(gain_w);
    ImGui::DragFloat("Master gain", &settings.master_gain, 0.01f, 0.0f, 4.0f);

    AudioAnalyser::Settings a = reactive.analyser().settings();
    if (ImGui::DragFloat("Input gain", &a.input_gain, 0.01f, 0.1f, 8.0f)) {
        reactive.analyser().setSettings(a);
    }
    if (ImGui::DragFloat("Onset sensitivity", &a.onset_sensitivity, 0.01f, 0.2f, 4.0f)) {
        reactive.analyser().setSettings(a);
    }

    ImGui::Spacing();
    ImGui::TextUnformatted("Source");

    static CaptureDevicePickerState picker;
    if (!picker.loaded_once) {
        picker.refresh();
    }

    if (!reactive.capture().isActive()) {
        const char* preview = "System default";
        if (picker.selected > 0 && picker.selected <= static_cast<int>(picker.devices.size())) {
            preview = picker.devices[static_cast<std::size_t>(picker.selected - 1)].name.c_str();
        }
        const float refresh_w =
            ImGui::CalcTextSize("Refresh").x + ImGui::GetStyle().FramePadding.x * 2.0f;
        const float combo_w =
            std::max(80.0f, ImGui::GetContentRegionAvail().x - refresh_w -
                                ImGui::GetStyle().ItemSpacing.x -
                                ImGui::CalcTextSize("Input device").x - 8.0f);
        ImGui::SetNextItemWidth(combo_w);
        if (ImGui::BeginCombo("Input device", preview)) {
            const bool default_selected = picker.selected <= 0;
            if (ImGui::Selectable("System default", default_selected)) {
                picker.selected = 0;
            }
            if (default_selected) {
                ImGui::SetItemDefaultFocus();
            }
            for (std::size_t i = 0; i < picker.devices.size(); ++i) {
                const int idx = static_cast<int>(i + 1);
                const bool sel = picker.selected == idx;
                if (ImGui::Selectable(picker.devices[i].name.c_str(), sel)) {
                    picker.selected = idx;
                }
                if (sel) {
                    ImGui::SetItemDefaultFocus();
                }
            }
            ImGui::EndCombo();
        }
        uiSameLineIfFits(refresh_w);
        if (ImGui::Button("Refresh")) {
            picker.refresh();
        }
        if (picker.devices.empty()) {
            ImGui::TextDisabled("No recording devices reported (check OS permissions).");
        }

        if (ImGui::Button("Start live capture")) {
            std::string error;
            bool ok = false;
            if (picker.selected <= 0) {
                ok = reactive.capture().startDefaultCapture(&error);
            } else {
                ok = reactive.capture().startCapture(picker.selectedDeviceId(), &error);
            }
            if (!ok) {
                reactive.setLastError(error);
            } else {
                reactive.setLastError({});
            }
        }
        const float wav_w =
            ImGui::CalcTextSize("Open WAV…").x + ImGui::GetStyle().FramePadding.x * 2.0f;
        uiSameLineIfFits(wav_w);
        if (ImGui::Button("Open WAV…")) {
            openFileDialog(nullptr, {{"WAV", "wav"}, {"All Files", "*"}},
                           [&reactive](std::filesystem::path path) {
                               std::string error;
                               if (!reactive.capture().loadWavFile(path, true, &error)) {
                                   reactive.setLastError(error);
                               } else {
                                   reactive.setLastError({});
                               }
                           });
        }
    } else {
        if (reactive.capture().mode() == AudioInputMode::LiveCapture) {
            ImGui::TextWrapped("Live: %s @ %.0f Hz",
                               reactive.capture().activeDeviceName().empty()
                                   ? "Recording device"
                                   : reactive.capture().activeDeviceName().c_str(),
                               reactive.capture().sampleRate());
        } else {
            ImGui::TextWrapped("WAV: %s",
                               reactive.capture().wavPath().filename().string().c_str());
        }
        const float stop_w = ImGui::CalcTextSize("Stop").x + ImGui::GetStyle().FramePadding.x * 2.0f;
        uiSameLineIfFits(stop_w);
        if (ImGui::Button("Stop")) {
            reactive.capture().stop();
            reactive.analyser().clear();
        }
    }

    if (!reactive.lastError().empty()) {
        ImGui::TextColored(ImVec4(1.0f, 0.45f, 0.45f, 1.0f), "%s", reactive.lastError().c_str());
    }

    ImGui::Spacing();
    ImGui::TextUnformatted("Meters");
    const AudioFeatures& f = reactive.features();
    drawMeter("Bass", f.bass);
    drawMeter("Mid", f.mid);
    drawMeter("Treble", f.treble);
    drawMeter("RMS", f.rms);
    drawMeter("Onset", f.onset);
    drawMeter("Beat", f.beat);

    ImGui::Spacing();
    ImGui::PlotHistogram("Spectrum", f.spectrum.data(), AudioFeatures::kSpectrumBars, 0, nullptr,
                         0.0f, 1.0f, ImVec2(-1.0f, 56.0f));

    ImGui::Separator();
    ImGui::TextUnformatted("Bindings");
    ImGui::TextWrapped("Link a feature to a parameter, then click Audio beside that parameter in "
                       "the Inspector.");

    static int link_channel = 0;
    const char* channel_names[] = {"Bass", "Mid", "Treble", "RMS", "Onset", "Beat"};
    const float feature_w = std::min(140.0f, std::max(72.0f, ImGui::GetContentRegionAvail().x * 0.4f));
    ImGui::SetNextItemWidth(feature_w);
    ImGui::Combo("Feature", &link_channel, channel_names, IM_ARRAYSIZE(channel_names));
    const float begin_w =
        ImGui::CalcTextSize("Begin link").x + ImGui::GetStyle().FramePadding.x * 2.0f;
    uiSameLineIfFits(begin_w);
    if (ImGui::Button("Begin link")) {
        reactive.beginLink(static_cast<AudioFeatureChannel>(link_channel));
    }
    if (reactive.isAwaitingParameterLink()) {
        ImGui::TextColored(ImVec4(0.95f, 0.85f, 0.35f, 1.0f), "Linking %s…",
                           audioFeatureChannelLabel(reactive.pendingLinkChannel()));
        if (ImGui::Button("Cancel link")) {
            reactive.cancelLink();
        }
    }

    if (!selected_particle_node_path.empty()) {
        if (ImGui::Button("Bass → selected emitter (rate + lift)", ImVec2(-1.0f, 0))) {
            reactive.addParticleBassRatePreset(selected_particle_node_path);
        }
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("Maps bass→particle rate and beat→gravity Y on:\n%s",
                             selected_particle_node_path.c_str());
        }
    } else {
        ImGui::BeginDisabled();
        ImGui::Button("Bass → selected emitter (rate + lift)", ImVec2(-1.0f, 0));
        ImGui::EndDisabled();
    }

    if (ImGui::BeginTable("AudioBindings", 3,
                          ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg |
                              ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_Resizable)) {
        ImGui::TableSetupColumn("Map", ImGuiTableColumnFlags_WidthStretch, 0.45f);
        ImGui::TableSetupColumn("bias / gain / min / max", ImGuiTableColumnFlags_WidthStretch, 0.45f);
        ImGui::TableSetupColumn("", ImGuiTableColumnFlags_WidthFixed, 28.0f);
        ImGui::TableHeadersRow();
        for (std::size_t i = 0; i < reactive.bindingsMutable().size(); ++i) {
            AudioBinding& binding = reactive.bindingsMutable()[i];
            ImGui::PushID(static_cast<int>(i));
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            ImGui::Checkbox("##en", &binding.enabled);
            ImGui::SameLine();
            ImGui::TextWrapped("%s", audioBindingSummary(binding).c_str());
            ImGui::TableNextColumn();
            const float cell = ImGui::GetContentRegionAvail().x;
            const float field = std::max(40.0f, (cell - ImGui::GetStyle().ItemSpacing.x * 3.0f) * 0.25f);
            ImGui::SetNextItemWidth(field);
            ImGui::DragFloat("##bias", &binding.bias, 0.01f);
            uiSameLineIfFits(field);
            ImGui::SetNextItemWidth(field);
            ImGui::DragFloat("##gain", &binding.gain, 0.01f);
            uiSameLineIfFits(field);
            ImGui::SetNextItemWidth(field);
            ImGui::DragFloat("##min", &binding.min_value, 0.01f);
            uiSameLineIfFits(field);
            ImGui::SetNextItemWidth(field);
            ImGui::DragFloat("##max", &binding.max_value, 0.01f);
            ImGui::TableNextColumn();
            if (ImGui::SmallButton("X")) {
                reactive.removeBinding(i);
                ImGui::PopID();
                break;
            }
            ImGui::PopID();
        }
        ImGui::EndTable();
    }

    ImGui::End();
}

bool drawAudioLinkButton(AudioLinkUiContext& ctx, OscParamTarget target, const std::string& node_path,
                         std::uint32_t material_id, const char* id_suffix) {
    if (!ctx.reactive || !ctx.reactive->isAwaitingParameterLink()) {
        return false;
    }
    ImGui::SameLine();
    char label[64];
    std::snprintf(label, sizeof(label), "Audio##%s", id_suffix ? id_suffix : "x");
    if (ImGui::SmallButton(label)) {
        return ctx.reactive->completeLink(target, node_path, material_id);
    }
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("Link %s → %s",
                          audioFeatureChannelLabel(ctx.reactive->pendingLinkChannel()),
                          oscParamTargetLabel(target));
    }
    return false;
}

} // namespace genesis
