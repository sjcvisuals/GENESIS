#include "editor/OscProtocolUi.hpp"

#include "protocol/OscParser.hpp"

#include <imgui.h>

#include <algorithm>
#include <cstdio>

namespace genesis {

void drawOscProtocolPanel(OscProtocol& protocol, bool* open) {
    ImGui::Begin("Protocol", open);

    ImGui::TextColored(ImVec4(0.55f, 0.85f, 0.65f, 1.0f), "OSC");
    ImGui::Separator();

    ImGui::TextWrapped(
        "Bind IP is the local network interface to listen on, not the sender address. "
        "Use 0.0.0.0 unless you only want loopback (127.0.0.1) or one NIC.");
    ImGui::Spacing();

    OscProtocol::Settings settings = protocol.settings();
    char ip_buf[64]{};
    std::snprintf(ip_buf, sizeof(ip_buf), "%s", settings.bind_ip.c_str());
    ImGui::SetNextItemWidth(140.0f);
    if (ImGui::InputText("Listen Interface", ip_buf, sizeof(ip_buf))) {
        settings.bind_ip = ip_buf;
        protocol.setSettings(settings);
    }
    if (ImGui::IsItemDeactivatedAfterEdit()) {
        settings.bind_ip = ip_buf;
        protocol.setSettings(settings);
    }
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("0.0.0.0 = all interfaces\n127.0.0.1 = this PC only (local)");
    }

    ImGui::SameLine();
    ImGui::SetNextItemWidth(90.0f);
    int port = static_cast<int>(settings.port);
    if (ImGui::InputInt("Port", &port)) {
        settings.port = static_cast<std::uint16_t>(std::clamp(port, 1, 65535));
        protocol.setSettings(settings);
    }

    if (ImGui::Checkbox("Learn incoming addresses", &settings.learn_enabled)) {
        protocol.setSettings(settings);
    }

    if (protocol.isListening()) {
        if (ImGui::Button("Stop Listening")) {
            OscProtocol::Settings stop_settings = protocol.settings();
            stop_settings.listening = false;
            protocol.setSettings(stop_settings);
            protocol.stopListening();
        }
        ImGui::SameLine();
        ImGui::TextColored(ImVec4(0.4f, 0.9f, 0.5f, 1.0f), "Listening on %s:%u",
                           protocol.settings().bind_ip.c_str(), protocol.settings().port);
    } else {
        ImGui::TextColored(ImVec4(0.95f, 0.55f, 0.35f, 1.0f), "Not listening");
        ImGui::SameLine();
        if (ImGui::Button("Start Listening")) {
            OscProtocol::Settings listen_settings = protocol.settings();
            listen_settings.listening = true;
            protocol.setSettings(listen_settings);
            std::string error;
            if (!protocol.startListening(&error)) {
                ImGui::OpenPopup("OSC Listen Error");
            }
        }
    }

    if (!protocol.lastError().empty()) {
        ImGui::TextColored(ImVec4(1.0f, 0.45f, 0.45f, 1.0f), "%s", protocol.lastError().c_str());
    }

    if (ImGui::BeginPopupModal("OSC Listen Error", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::TextWrapped("%s", protocol.lastError().c_str());
        if (ImGui::Button("OK")) {
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }

    ImGui::TextDisabled("%s", "Position uses cm, scale uses %, rotation uses degrees.");
    ImGui::TextDisabled("UDP packets: %llu   parsed messages: %llu   parse errors: %llu",
                        static_cast<unsigned long long>(protocol.totalPackets()),
                        static_cast<unsigned long long>(protocol.totalMessages()),
                        static_cast<unsigned long long>(protocol.parseErrors()));
    if (protocol.isListening() && protocol.totalPackets() == 0) {
        ImGui::TextWrapped(
            "No UDP packets yet. Send OSC to this computer's IP on port %u. "
            "If sending from the same PC, use 127.0.0.1 and listen on 0.0.0.0 or 127.0.0.1.",
            protocol.settings().port);
    } else if (protocol.totalPackets() > 0 && protocol.learnedMessages().empty()) {
        ImGui::TextColored(ImVec4(1.0f, 0.65f, 0.35f, 1.0f),
                           "Packets received but no OSC addresses learned yet. Check parse error above.");
    }

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::TextColored(ImVec4(0.55f, 0.75f, 0.95f, 1.0f), "Particle emit (XY tracker)");
    ImGui::TextWrapped(
        "Send floats x,y to move the OSC Tracker emitter (and optionally burst). "
        "Continuous bubble spawn uses the emitter Rate. Optional: x,y,count or x,y,z,count.");

    OscParticleEmitSettings emit = protocol.particleEmitSettings();
    bool emit_dirty = false;
    emit_dirty |= ImGui::Checkbox("Enable XY particle emit", &emit.enabled);
    emit_dirty |= ImGui::Checkbox("Track emitter position", &emit.track_emitter_position);
    emit_dirty |= ImGui::Checkbox("Burst on each message", &emit.burst_on_message);
    char addr_buf[128]{};
    std::snprintf(addr_buf, sizeof(addr_buf), "%s", emit.address.c_str());
    if (ImGui::InputText("Emit address", addr_buf, sizeof(addr_buf))) {
        emit.address = addr_buf;
        emit_dirty = true;
    }
    char path_buf[128]{};
    std::snprintf(path_buf, sizeof(path_buf), "%s", emit.emitter_path.c_str());
    if (ImGui::InputText("Emitter path", path_buf, sizeof(path_buf))) {
        emit.emitter_path = path_buf;
        emit_dirty = true;
    }
    int plane = static_cast<int>(emit.plane);
    if (ImGui::Combo("XY plane", &plane, "XZ (floor)\0XY (screen)\0")) {
        emit.plane = static_cast<OscParticlePlane>(plane);
        emit_dirty = true;
    }
    emit_dirty |= ImGui::DragFloat("Scale", &emit.scale, 0.01f, 0.001f, 100.0f, "%.3f");
    emit_dirty |= ImGui::DragFloat2("Offset", &emit.offset.x, 0.01f);
    emit_dirty |= ImGui::DragFloat("Plane height", &emit.plane_height, 0.01f);
    int default_count = static_cast<int>(emit.default_count);
    if (ImGui::InputInt("Burst count", &default_count)) {
        emit.default_count = static_cast<std::uint32_t>(std::clamp(default_count, 1, 256));
        emit_dirty = true;
    }
    if (emit_dirty) {
        protocol.setParticleEmitSettings(emit);
    }
    ImGui::TextDisabled("Particle emit messages: %llu",
                        static_cast<unsigned long long>(protocol.totalParticleEmits()));
    if (ImGui::Button("Clear particle emit queue")) {
        protocol.clearParticleEmits();
    }

    if (protocol.isAwaitingParameterLink()) {
        ImGui::Spacing();
        ImGui::TextColored(ImVec4(0.95f, 0.85f, 0.35f, 1.0f),
                           "Link mode: choose a parameter in the Inspector.");
        ImGui::Text("OSC %s  arg[%d]", protocol.pendingLinkAddress().c_str(),
                    protocol.pendingLinkArgIndex());
        if (ImGui::Button("Cancel Link")) {
            protocol.cancelLink();
        }
    }

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::TextUnformatted("Learned Messages");
    if (ImGui::Button("Clear Learned")) {
        protocol.clearLearned();
    }

    const ImGuiTableFlags table_flags =
        ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersOuter | ImGuiTableFlags_BordersInnerV |
        ImGuiTableFlags_ScrollY | ImGuiTableFlags_SizingStretchProp;
    if (ImGui::BeginTable("##OscLearned", 3, table_flags, ImVec2(0.0f, 180.0f))) {
        ImGui::TableSetupColumn("Address", ImGuiTableColumnFlags_WidthStretch, 0.55f);
        ImGui::TableSetupColumn("Values", ImGuiTableColumnFlags_WidthStretch, 0.30f);
        ImGui::TableSetupColumn("Link", ImGuiTableColumnFlags_WidthFixed, 52.0f);
        ImGui::TableHeadersRow();

        const std::vector<OscLearnedMessage>& learned = protocol.learnedMessages();
        for (std::size_t i = 0; i < learned.size(); ++i) {
            const OscLearnedMessage& entry = learned[i];
            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            ImGui::TextUnformatted(entry.address.c_str());

            ImGui::TableSetColumnIndex(1);
            if (entry.arg_preview.empty()) {
                ImGui::TextDisabled("(no args)");
            } else {
                std::string preview;
                for (std::size_t arg = 0; arg < entry.arg_preview.size(); ++arg) {
                    if (arg > 0) preview += ", ";
                    preview += entry.arg_preview[arg];
                }
                ImGui::TextWrapped("%s", preview.c_str());
            }

            ImGui::TableSetColumnIndex(2);
            const std::string link_id = std::string("Link##osc_learn_") + std::to_string(i);
            if (ImGui::SmallButton(link_id.c_str())) {
                protocol.beginLink(entry.address, 0);
            }
            if (entry.arg_preview.size() > 1 && ImGui::BeginPopupContextItem(link_id.c_str())) {
                ImGui::TextUnformatted("Link argument");
                for (int arg = 0; arg < static_cast<int>(entry.arg_preview.size()); ++arg) {
                    const std::string item = std::string("[") + std::to_string(arg) + "] " +
                                             entry.arg_preview[static_cast<std::size_t>(arg)];
                    if (ImGui::Selectable(item.c_str())) {
                        protocol.beginLink(entry.address, arg);
                    }
                }
                ImGui::EndPopup();
            }
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip("Link arg 0. Right-click for other arguments.");
            }
        }
        ImGui::EndTable();
    }

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::TextUnformatted("Bindings");
    if (ImGui::Button("Clear Bindings")) {
        protocol.clearBindings();
    }

    const std::vector<OscBinding>& bindings = protocol.bindings();
    if (bindings.empty()) {
        ImGui::TextDisabled("No OSC bindings yet.");
    } else if (ImGui::BeginTable("##OscBindings", 3, table_flags, ImVec2(0.0f, 140.0f))) {
        ImGui::TableSetupColumn("OSC", ImGuiTableColumnFlags_WidthStretch, 0.35f);
        ImGui::TableSetupColumn("Target", ImGuiTableColumnFlags_WidthStretch, 0.50f);
        ImGui::TableSetupColumn("", ImGuiTableColumnFlags_WidthFixed, 28.0f);
        ImGui::TableHeadersRow();

        for (std::size_t i = 0; i < bindings.size(); ++i) {
            const OscBinding& binding = bindings[i];
            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            ImGui::Text("%s [%d]", binding.address.c_str(), binding.arg_index);

            ImGui::TableSetColumnIndex(1);
            ImGui::TextWrapped("%s", oscBindingTargetSummary(binding).c_str());

            ImGui::TableSetColumnIndex(2);
            const std::string remove_id = std::string("X##osc_bind_") + std::to_string(i);
            if (ImGui::SmallButton(remove_id.c_str())) {
                protocol.removeBinding(i);
                break;
            }
        }
        ImGui::EndTable();
    }

    ImGui::End();
}

bool drawOscLinkButton(OscLinkUiContext& ctx, OscParamTarget target, const std::string& node_path,
                       std::uint32_t material_id, const char* id_suffix) {
    if (!ctx.protocol || !ctx.protocol->isAwaitingParameterLink()) {
        return false;
    }

    ImGui::SameLine();
    const bool active = ctx.protocol->pendingLinkAddress().find('/') != std::string::npos;
    if (active) {
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.25f, 0.55f, 0.35f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.30f, 0.65f, 0.42f, 1.0f));
    }

    const std::string button_id =
        std::string("Link##osc_") + (id_suffix ? id_suffix : oscParamTargetLabel(target));
    const bool clicked = ImGui::SmallButton(button_id.c_str());
    if (active) {
        ImGui::PopStyleColor(2);
    }
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("Link pending OSC message to this parameter.");
    }
    if (clicked) {
        return ctx.protocol->completeLink(target, node_path, material_id);
    }
    return false;
}

} // namespace genesis
