#include "editor/AudioReactiveUi.hpp"

#include "platform/FileDialog.hpp"
#include "protocol/OscTypes.hpp"

#include <imgui.h>

#include <algorithm>
#include <cstdio>
#include <filesystem>

namespace genesis {

namespace {

void drawMeter(const char* label, float value) {
    ImGui::TextUnformatted(label);
    ImGui::SameLine(72.0f);
    ImGui::ProgressBar(std::clamp(value, 0.0f, 1.0f), ImVec2(-1.0f, 0.0f), "");
}

} // namespace

void drawAudioReactivePanel(AudioReactive& reactive, bool* open,
                            const std::string& selected_particle_node_path) {
    ImGui::Begin("Audio", open);

    ImGui::TextColored(ImVec4(0.55f, 0.85f, 0.65f, 1.0f), "ANALYSER");
    ImGui::Separator();
    ImGui::TextWrapped(
        "Live mic / line-in or a WAV file → bass / mid / treble / onset values that drive scene "
        "params (fluids, lights, particles). FFT is 512-point, once per frame.");

    AudioReactive::Settings& settings = reactive.settings();
    ImGui::Checkbox("Enabled", &settings.enabled);
    ImGui::SameLine();
    ImGui::SetNextItemWidth(120.0f);
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
    if (!reactive.capture().isActive()) {
        if (ImGui::Button("Start live capture")) {
            std::string error;
            if (!reactive.capture().startDefaultCapture(&error)) {
                reactive.setLastError(error);
            } else {
                reactive.setLastError({});
            }
        }
        ImGui::SameLine();
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
            ImGui::TextColored(ImVec4(0.4f, 0.9f, 0.5f, 1.0f), "Live capture @ %.0f Hz",
                               reactive.capture().sampleRate());
        } else {
            ImGui::TextColored(ImVec4(0.4f, 0.9f, 0.5f, 1.0f), "WAV: %s",
                               reactive.capture().wavPath().filename().string().c_str());
        }
        ImGui::SameLine();
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
    ImGui::SetNextItemWidth(120.0f);
    ImGui::Combo("Feature", &link_channel, channel_names, IM_ARRAYSIZE(channel_names));
    ImGui::SameLine();
    if (ImGui::Button("Begin link")) {
        reactive.beginLink(static_cast<AudioFeatureChannel>(link_channel));
    }
    if (reactive.isAwaitingParameterLink()) {
        ImGui::SameLine();
        ImGui::TextColored(ImVec4(0.95f, 0.85f, 0.35f, 1.0f), "Linking %s…",
                           audioFeatureChannelLabel(reactive.pendingLinkChannel()));
        if (ImGui::Button("Cancel link")) {
            reactive.cancelLink();
        }
    }

    if (!selected_particle_node_path.empty()) {
        if (ImGui::Button("Bass → selected emitter (rate + lift)")) {
            reactive.addParticleBassRatePreset(selected_particle_node_path);
        }
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("Maps bass→particle rate and beat→gravity Y on:\n%s",
                             selected_particle_node_path.c_str());
        }
    } else {
        ImGui::BeginDisabled();
        ImGui::Button("Bass → selected emitter (rate + lift)");
        ImGui::EndDisabled();
    }

    if (ImGui::BeginTable("AudioBindings", 3,
                          ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg |
                              ImGuiTableFlags_SizingStretchProp)) {
        ImGui::TableSetupColumn("Map");
        ImGui::TableSetupColumn("bias / gain / min / max");
        ImGui::TableSetupColumn("");
        ImGui::TableHeadersRow();
        for (std::size_t i = 0; i < reactive.bindingsMutable().size(); ++i) {
            AudioBinding& binding = reactive.bindingsMutable()[i];
            ImGui::PushID(static_cast<int>(i));
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            ImGui::Checkbox("##en", &binding.enabled);
            ImGui::SameLine();
            ImGui::TextUnformatted(audioBindingSummary(binding).c_str());
            ImGui::TableNextColumn();
            ImGui::SetNextItemWidth(54.0f);
            ImGui::DragFloat("##bias", &binding.bias, 0.01f);
            ImGui::SameLine();
            ImGui::SetNextItemWidth(54.0f);
            ImGui::DragFloat("##gain", &binding.gain, 0.01f);
            ImGui::SameLine();
            ImGui::SetNextItemWidth(54.0f);
            ImGui::DragFloat("##min", &binding.min_value, 0.01f);
            ImGui::SameLine();
            ImGui::SetNextItemWidth(54.0f);
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
