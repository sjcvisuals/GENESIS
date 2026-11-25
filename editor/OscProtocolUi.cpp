#include "editor/OscProtocolUi.hpp"

#include "protocol/OscParser.hpp"

#include <imgui.h>

#include <algorithm>
#include <cstdio>
#include <string>
#include <vector>

namespace genesis {

namespace {

void applyTypedPort(OscProtocol& protocol, OscProtocol::Settings& settings, const char* port_buf) {
    int port = 0;
    if (std::sscanf(port_buf, "%d", &port) != 1) {
        return;
    }
    settings.port = static_cast<std::uint16_t>(std::clamp(port, 1, 65535));
    protocol.setSettings(settings);
}

void drawListenControls(OscProtocol& protocol) {
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
        ImGui::SetTooltip("0.0.0.0 = all interfaces\n127.0.0.1 = this PC only");
    }

    ImGui::SameLine();
    // Typed port (no +/- steppers) — commit on Enter / focus-loss.
    static char port_buf[16]{};
    static std::uint16_t port_buf_synced = 0;
    if (port_buf_synced != settings.port || port_buf[0] == '\0') {
        std::snprintf(port_buf, sizeof(port_buf), "%u", static_cast<unsigned>(settings.port));
        port_buf_synced = settings.port;
    }
    ImGui::SetNextItemWidth(90.0f);
    ImGui::InputText("Port", port_buf, sizeof(port_buf), ImGuiInputTextFlags_CharsDecimal);
    if (ImGui::IsItemDeactivatedAfterEdit()) {
        applyTypedPort(protocol, settings, port_buf);
        port_buf_synced = protocol.settings().port;
        std::snprintf(port_buf, sizeof(port_buf), "%u",
                      static_cast<unsigned>(protocol.settings().port));
    }
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("Type UDP port 1–65535, then Enter or click away.");
    }

    settings = protocol.settings();
    if (ImGui::Checkbox("Learn addresses", &settings.learn_enabled)) {
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
        ImGui::TextColored(ImVec4(0.4f, 0.9f, 0.5f, 1.0f), "Listening %s:%u",
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

    ImGui::TextDisabled("packets %llu · messages %llu · errors %llu",
                        static_cast<unsigned long long>(protocol.totalPackets()),
                        static_cast<unsigned long long>(protocol.totalMessages()),
                        static_cast<unsigned long long>(protocol.parseErrors()));
}

void drawLearnedAndBindings(OscProtocol& protocol) {
    if (protocol.isAwaitingParameterLink()) {
        ImGui::TextColored(ImVec4(0.95f, 0.85f, 0.35f, 1.0f),
                           "Link mode: pick a parameter in Inspector / Tugela.");
        ImGui::Text("OSC %s  arg[%d]", protocol.pendingLinkAddress().c_str(),
                    protocol.pendingLinkArgIndex());
        if (ImGui::Button("Cancel Link")) {
            protocol.cancelLink();
        }
        ImGui::Spacing();
    }

    ImGui::TextUnformatted("Learned Messages");
    ImGui::SameLine();
    if (ImGui::SmallButton("Clear##learned")) {
        protocol.clearLearned();
    }

    const ImGuiTableFlags table_flags =
        ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersOuter | ImGuiTableFlags_BordersInnerV |
        ImGuiTableFlags_ScrollY | ImGuiTableFlags_SizingStretchProp;
    if (ImGui::BeginTable("##OscLearned", 3, table_flags, ImVec2(0.0f, 140.0f))) {
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
                ImGui::TextUnformatted(preview.c_str());
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
    ImGui::TextUnformatted("Bindings");
    ImGui::SameLine();
    if (ImGui::SmallButton("Clear##bindings")) {
        protocol.clearBindings();
    }

    const std::vector<OscBinding>& bindings = protocol.bindings();
    if (bindings.empty()) {
        ImGui::TextDisabled("No OSC bindings yet.");
    } else if (ImGui::BeginTable("##OscBindings", 3, table_flags, ImVec2(0.0f, 100.0f))) {
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
            ImGui::TextUnformatted(oscBindingTargetSummary(binding).c_str());

            ImGui::TableSetColumnIndex(2);
            const std::string remove_id = std::string("X##osc_bind_") + std::to_string(i);
            if (ImGui::SmallButton(remove_id.c_str())) {
                protocol.removeBinding(i);
                break;
            }
        }
        ImGui::EndTable();
    }
}

void drawOscDebugLogControls(OscProtocol& protocol, float log_height) {
    bool capture = protocol.debugLogEnabled();
    if (ImGui::Checkbox("Capture", &capture)) {
        protocol.setDebugLogEnabled(capture);
    }
    ImGui::SameLine();
    ImGui::TextDisabled("%zu / %zu", protocol.debugLog().size(), protocol.debugLogCapacity());

    ImGui::SameLine();
    int capacity = static_cast<int>(protocol.debugLogCapacity());
    ImGui::SetNextItemWidth(72.0f);
    if (ImGui::InputInt("##RingSize", &capacity, 0, 0)) {
        protocol.setDebugLogCapacity(static_cast<std::size_t>(std::clamp(capacity, 32, 4096)));
    }
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("Ring buffer size (rows).");
    }

    ImGui::SameLine();
    if (ImGui::Button("Copy All")) {
        ImGui::SetClipboardText(protocol.exportDebugLog().c_str());
    }
    ImGui::SameLine();
    if (ImGui::Button("Copy Last 50")) {
        const std::vector<OscDebugLogEntry>& all = protocol.debugLog();
        const std::size_t start = all.size() > 50 ? all.size() - 50 : 0;
        std::vector<OscDebugLogEntry> slice(all.begin() + static_cast<std::ptrdiff_t>(start),
                                            all.end());
        ImGui::SetClipboardText(formatOscDebugLog(slice).c_str());
    }
    ImGui::SameLine();
    if (ImGui::Button("Clear Log")) {
        protocol.clearDebugLog();
    }

    const ImGuiTableFlags table_flags =
        ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersOuter | ImGuiTableFlags_BordersInnerV |
        ImGuiTableFlags_ScrollY | ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_Resizable;
    if (ImGui::BeginTable("##OscDebugLog", 5, table_flags, ImVec2(0.0f, log_height))) {
        ImGui::TableSetupColumn("Time", ImGuiTableColumnFlags_WidthFixed, 88.0f);
        ImGui::TableSetupColumn("Pkt", ImGuiTableColumnFlags_WidthFixed, 44.0f);
        ImGui::TableSetupColumn("D", ImGuiTableColumnFlags_WidthFixed, 28.0f);
        ImGui::TableSetupColumn("Address", ImGuiTableColumnFlags_WidthStretch, 0.45f);
        ImGui::TableSetupColumn("Args / note", ImGuiTableColumnFlags_WidthStretch, 0.55f);
        ImGui::TableHeadersRow();

        const std::vector<OscDebugLogEntry>& log = protocol.debugLog();
        ImGuiListClipper clipper;
        clipper.Begin(static_cast<int>(log.size()));
        while (clipper.Step()) {
            for (int row = clipper.DisplayStart; row < clipper.DisplayEnd; ++row) {
                const OscDebugLogEntry& entry = log[static_cast<std::size_t>(row)];
                ImGui::TableNextRow();
                ImGui::TableSetColumnIndex(0);
                ImGui::TextUnformatted(entry.wall_clock.empty() ? "-" : entry.wall_clock.c_str());

                ImGui::TableSetColumnIndex(1);
                ImGui::Text("%llu", static_cast<unsigned long long>(entry.packet_seq));

                ImGui::TableSetColumnIndex(2);
                ImGui::Text("%d", entry.nest_depth);

                ImGui::TableSetColumnIndex(3);
                std::string addr;
                for (int d = 0; d < entry.nest_depth; ++d) {
                    addr += "· ";
                }
                if (entry.kind == OscDebugKind::Bundle) {
                    addr += "#bundle";
                    ImGui::TextColored(ImVec4(0.75f, 0.85f, 0.45f, 1.0f), "%s", addr.c_str());
                } else if (entry.kind == OscDebugKind::Error) {
                    addr += entry.address;
                    ImGui::TextColored(ImVec4(1.0f, 0.45f, 0.45f, 1.0f), "%s", addr.c_str());
                } else {
                    addr += entry.address;
                    ImGui::TextUnformatted(addr.c_str());
                }

                ImGui::TableSetColumnIndex(4);
                if (entry.kind == OscDebugKind::Message) {
                    std::string preview = entry.type_tags;
                    if (!entry.arg_preview.empty()) {
                        if (!preview.empty()) preview += "  ";
                        for (std::size_t i = 0; i < entry.arg_preview.size(); ++i) {
                            if (i > 0) preview += ", ";
                            preview += entry.arg_preview[i];
                        }
                    }
                    if (preview.empty()) {
                        ImGui::TextDisabled("(no args)");
                    } else {
                        ImGui::TextUnformatted(preview.c_str());
                    }
                } else if (!entry.note.empty()) {
                    ImGui::TextUnformatted(entry.note.c_str());
                } else {
                    ImGui::TextDisabled("—");
                }

                if (ImGui::TableGetHoveredColumn() >= 0 &&
                    ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) {
                    ImGui::SetClipboardText(formatOscDebugLogEntry(entry).c_str());
                }
            }
        }
        ImGui::EndTable();
    }
    ImGui::TextDisabled("Depth = bundle nesting. Double-click row to copy.");
}

} // namespace

void drawOscProtocolPanel(OscProtocol& protocol, bool* open) {
    ImGui::SetNextWindowSize(ImVec2(520.0f, 680.0f), ImGuiCond_FirstUseEver);
    if (!ImGui::Begin("Protocol", open)) {
        ImGui::End();
        return;
    }

    ImGui::TextColored(ImVec4(0.55f, 0.85f, 0.65f, 1.0f), "OSC");
    ImGui::Separator();
    drawListenControls(protocol);

    ImGui::Spacing();
    ImGui::Separator();
    drawLearnedAndBindings(protocol);

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::TextColored(ImVec4(0.55f, 0.75f, 0.95f, 1.0f), "Incoming Debug Log");
    drawOscDebugLogControls(protocol, 220.0f);

    ImGui::End();
}

void drawOscDebugLogPanel(OscProtocol& protocol, bool* open) {
    ImGui::SetNextWindowSize(ImVec2(640.0f, 420.0f), ImGuiCond_FirstUseEver);
    if (!ImGui::Begin("OSC Debug Log", open)) {
        ImGui::End();
        return;
    }
    ImGui::TextWrapped(
        "Raw incoming OSC (including bundles). Copy All to share format / how messages were separated.");
    ImGui::Separator();
    drawOscDebugLogControls(protocol, ImGui::GetContentRegionAvail().y - 8.0f);
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
