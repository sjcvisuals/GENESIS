#include "editor/RoomTrackingUi.hpp"

#include "protocol/OscRoomPerson.hpp"

#include <imgui.h>

#include <algorithm>
#include <cstring>

namespace genesis {

namespace {

bool drawWallRayModeCombo(WallRaySpawnMode& mode) {
    const char* labels[] = {
        "Fixed distance (cylinder)",
        "Plane collision only",
        "Prefer plane, else distance",
    };
    int index = static_cast<int>(mode);
    if (index < 0 || index > 2) {
        index = 0;
    }
    if (!ImGui::Combo("Wall ray mode", &index, labels, 3)) {
        return false;
    }
    mode = static_cast<WallRaySpawnMode>(index);
    return true;
}

} // namespace

bool drawRoomTrackingPanel(OscProtocol& protocol, bool* open) {
    ImGui::SetNextWindowSize(ImVec2(440.0f, 520.0f), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSizeConstraints(ImVec2(320.0f, 280.0f), ImVec2(10000.0f, 10000.0f));
    if (!ImGui::Begin("Room Tracking", open)) {
        ImGui::End();
        return false;
    }

    bool dirty = false;
    OscRoomPersonSettings room = protocol.roomPersonSettings();

    ImGui::TextWrapped(
        "Author people tracking with the scene (saved on File → Save). "
        "Build Medusa-style setups from scratch: enable tracking, point at a Wall Plane with "
        "Collision on, choose wall-ray mode, then Listen in Protocol.");
    ImGui::Spacing();

    dirty |= ImGui::Checkbox("Enable room / people tracking", &room.enabled);
    ImGui::TextDisabled("When on, OSC / TUIO drives Feet + Wall under Person Pool.");

    ImGui::BeginDisabled(!room.enabled);
    {
        char address[128];
        std::snprintf(address, sizeof(address), "%s", room.address.c_str());
        if (ImGui::InputText("Person address", address, sizeof(address))) {
            room.address = address;
            dirty = true;
        }

        dirty |= ImGui::Checkbox("TUIO /tuio/2Dcur (via OSC listen)", &room.tuio_enabled);
        ImGui::TextDisabled("TouchDesigner owns TUIO → forwards as OSC. No extra UDP client.");
        if (room.tuio_enabled) {
            char tuio_addr[128];
            std::snprintf(tuio_addr, sizeof(tuio_addr), "%s", room.tuio_address.c_str());
            if (ImGui::InputText("TUIO address", tuio_addr, sizeof(tuio_addr))) {
                room.tuio_address = tuio_addr;
                dirty = true;
            }
            dirty |= ImGui::DragFloat("TUIO scale (m)", &room.tuio_scale, 0.01f, 0.1f, 50.0f);
            dirty |= ImGui::DragFloat2("TUIO offset XZ (m)", &room.tuio_offset.x, 0.01f);
        }

        ImGui::Separator();
        ImGui::TextColored(ImVec4(0.55f, 0.85f, 0.65f, 1.0f), "Scene paths");
        char pool[160];
        std::snprintf(pool, sizeof(pool), "%s", room.person_pool_path.c_str());
        if (ImGui::InputText("Person Pool path", pool, sizeof(pool))) {
            room.person_pool_path = pool;
            room.feet_emitter_path = room.person_pool_path + "/Feet 00";
            room.wall_emitter_path = room.person_pool_path + "/Wall 00";
            dirty = true;
        }
        char plane[160];
        std::snprintf(plane, sizeof(plane), "%s", room.wall_plane_path.c_str());
        if (ImGui::InputText("Wall Plane path", plane, sizeof(plane))) {
            room.wall_plane_path = plane;
            dirty = true;
        }
        ImGui::TextDisabled("Add a Plane mesh, enable Collision, Hidden in Game as needed.");

        ImGui::Separator();
        ImGui::TextColored(ImVec4(0.55f, 0.75f, 0.95f, 1.0f), "Layout");
        int max_persons = static_cast<int>(room.max_persons);
        if (ImGui::SliderInt("Max persons", &max_persons, 1,
                             static_cast<int>(kRoomPersonSlotCapacity))) {
            room.max_persons = static_cast<std::uint32_t>(max_persons);
            dirty = true;
        }
        dirty |= ImGui::DragFloat("Room radius (m)", &room.room_radius, 0.01f, 0.5f, 50.0f);
        dirty |= ImGui::DragFloat("Tank radius (m)", &room.tank_radius, 0.01f, 0.1f, 50.0f);
        room.room_half_x = room.room_radius;
        room.room_half_z = room.room_radius;
        dirty |= ImGui::DragFloat("Feet height (m)", &room.feet_height, 0.005f, 0.0f, 2.0f);
        dirty |= ImGui::DragFloat("Wall spawn height (m)", &room.wall_spawn_height, 0.01f, 0.0f,
                                  10.0f);

        ImGui::Separator();
        ImGui::TextColored(ImVec4(0.95f, 0.75f, 0.45f, 1.0f), "Wall ray");
        dirty |= drawWallRayModeCombo(room.wall_ray_mode);
        dirty |= ImGui::DragFloat("Ray max distance (m)", &room.wall_ray_max_distance, 0.1f, 0.5f,
                                  200.0f);
        dirty |= ImGui::Checkbox("Debug draw wall ray (editor)", &room.debug_draw_wall_ray);
        dirty |= ImGui::Checkbox("Track feet emitter", &room.track_feet_emitter);
        dirty |= ImGui::Checkbox("Track wall emitter", &room.track_wall_emitter);

        ImGui::Spacing();
        ImGui::TextWrapped(
            "Feet/Wall emitters are created under Person Pool when people appear — "
            "not pre-baked. Per-emitter Line Trace (Tugela) is separate and fully manual.");
    }
    ImGui::EndDisabled();

    if (dirty) {
        protocol.setRoomPersonSettings(room);
    }

    ImGui::End();
    return dirty;
}

} // namespace genesis
