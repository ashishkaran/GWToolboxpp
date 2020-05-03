#include "stdafx.h"
#include "ObjectiveTimerWindow.h"

#include <imgui.h>
#include <imgui_internal.h>

#include <GWCA\Constants\Constants.h>
#include <GWCA\GameContainers\Array.h>
#include <GWCA\GameContainers\GamePos.h>
#include <GWCA\Packets\StoC.h>

#include <GWCA\GameEntities\Map.h>
#include <GWCA\GameEntities\Agent.h>

#include <GWCA\Context\GameContext.h>
#include <GWCA\Context\WorldContext.h>

#include <GWCA\Managers\UIMgr.h>
#include <GWCA\Managers\MapMgr.h>
#include <GWCA\Managers\ChatMgr.h>
#include <GWCA\Managers\StoCMgr.h>
#include <GWCA\Managers\AgentMgr.h>

#include "GuiUtils.h"
#include "GWToolbox.h"

#include <Modules\Resources.h>
#include "logger.h"
#include <time.h>

#define countof(arr) (sizeof(arr) / sizeof(arr[0]))

#define TIME_UNKNOWN -1
unsigned int ObjectiveTimerWindow::ObjectiveSet::cur_ui_id = 0;

namespace {
    // settings in the unnamed namespace. This is ugly. DO NOT COPY. 
    // just doing it because Objective needs them and I'm too lazy to pass them all the way there.
    int n_columns = 4;
    bool show_decimal = false;
    bool show_start_column = true;
    bool show_end_column = true;
    bool show_time_column = true;

    enum DoA_ObjId : DWORD {
        Foundry = 0x273F,
        Veil,
        Gloom,
        City
    };
    uint32_t doa_get_next(uint32_t id) {
        switch (id) {
        case Foundry: return City;
        case City: return Veil;
        case Veil: return Gloom;
        case Gloom: return Foundry;
        }
        return 0;
    }
	// Hex values matching the first char of Kanaxai's dialogs in each room.
	enum kanaxai_room_dialogs {
		Room5 = 0x5336,
		Room6,
		Room8,
		Room10,
		Room12,
		Room13,
		Room14,
		Room15
	};
	const wchar_t* kanaxai_dialogs[] = {
		// Room 1-4 no dialog
		L"\x5336\xBEB8\x8555\x7267", // Room 5 "Fear not the darkness. It is already within you."
		L"\x5337\xAA3A\xE96F\x3E34", // Room 6 "Is it comforting to know the source of your fears? Or do you fear more now that you see them in front of you."
		// Room 7 no dialog
		L"\x5338\xFD69\xA162\x3A04", // Room 8 "Even if you banish me from your sight, I will remain in your mind."
		// Room 9 no dialog
		L"\x5339\xA7BA\xC67B\x5D81", // Room 10 "You mortals may be here to defeat me, but acknowledging my presence only makes the nightmare grow stronger."
		// Room 11 no dialog
		L"\x533A\xED06\x815D\x5FFB", // Room 12 "So, you have passed through the depths of the Jade Sea, and into the nightmare realm. It is too bad that I must send you back from whence you came."
		L"\x533B\xCAA6\xFDA9\x3277", // Room 13 "I am Kanaxai, creator of nightmares. Let me make yours into reality."
		L"\x533C\xDD33\xA330\x4E27", // Room 14 "I will fill your hearts with visions of horror and despair that will haunt you for all of your days."
		L"\x533D\x9EB1\x8BEE\x2637"	 // Kanaxai "What gives you the right to enter my lair? I shall kill you for your audacity, after I destroy your mind with my horrifying visions, of course."
	};
    void PrintTime(char* buf, size_t size, DWORD time, bool show_ms = true) {
        if (time == TIME_UNKNOWN) {
            GuiUtils::StrCopy(buf, "--:--", size);
        } else {
            DWORD sec = time / 1000;
            if (show_ms && show_decimal) {
                snprintf(buf, size, "%02d:%02d.%1d",
                    (sec / 60), sec % 60, (time / 100) % 10);
            } else {
                snprintf(buf, size, "%02d:%02d", (sec / 60), sec % 60);
            }
        }
    }

	void AsyncGetMapName(char *buffer, size_t n) {
		static wchar_t enc_str[16];
		GW::AreaInfo *info = GW::Map::GetCurrentMapInfo();
		if (!GW::UI::UInt32ToEncStr(info->name_id, enc_str, n)) {
			buffer[0] = 0;
			return;
		}
		GW::UI::AsyncDecodeStr(enc_str, buffer, n);
	}

    void ComputeNColumns() {
        n_columns = 0
            + (show_start_column ? 1 : 0)
            + (show_end_column ? 1 : 0)
            + (show_time_column ? 1 : 0);
    }
    
    float GetTimestampWidth() {
        return (65.0f * ImGui::GetIO().FontGlobalScale);
    }
    float GetLabelWidth() {
        return std::max(GetTimestampWidth(),ImGui::GetWindowContentRegionWidth() - (GetTimestampWidth() * n_columns));
    }
    static bool runs_dirty = false;
}

void ObjectiveTimerWindow::Initialize() {
    ToolboxWindow::Initialize();

    GW::StoC::RegisterPacketCallback<GW::Packet::StoC::MessageServer>(&MessageServer_Entry, 
        [this](GW::HookStatus* status, GW::Packet::StoC::MessageServer* packet) -> bool {
            uint32_t objective_id = 0; // Objective_id applicable for the check
            uint32_t msg_check = 0; // First encoded msg char to check for
            switch (GW::Map::GetMapID()) {
            case GW::Constants::MapID::Urgozs_Warren:
                msg_check = 0x6C9C; // Gained 10,000 or 5,000 Kurzick faction in Urgoz Warren - get Urgoz objective.
                objective_id = 15529;
                break;
            case GW::Constants::MapID::The_Deep:
                msg_check = 0x6D4D;  // Gained 10,000 or 5,000 Luxon faction in Deep - get Kanaxai objective.
				objective_id = RoomID::Deep_room_15;
                break;
            }
            if (!objective_id) return false;
            GW::Array<wchar_t>* buff = &GW::GameContext::instance()->world->message_buff;
            if (!buff || !buff->valid() || !buff->size())
                return true; // Message buffer empty!?
            const wchar_t* msg = buff->begin();
            if (msg[0] != msg_check || (msg[5] != 0x2810 && msg[5] != 0x1488))
                return false; // Not the right message            
            Objective* obj = GetCurrentObjective(objective_id);
            if (!obj || obj->IsDone())
                return false; // Already done!?
            obj->SetDone();
            // Cycle through all previous objectives and flag as done
            for (Objective& objective : current_objective_set->objectives) {
                objective.SetDone();
            }
            current_objective_set->CheckSetDone();
            return false;
        });
	GW::StoC::RegisterPacketCallback<GW::Packet::StoC::DisplayDialogue>(&DisplayDialogue_Entry,
		[this](GW::HookStatus* status, GW::Packet::StoC::DisplayDialogue* packet) -> void {
			DisplayDialogue(packet);
		});
    GW::StoC::RegisterPacketCallback<GW::Packet::StoC::PartyDefeated>(&PartyDefeated_Entry,
        [this](GW::HookStatus* status, GW::Packet::StoC::PartyDefeated* packet) -> void {
            if (current_objective_set)
                current_objective_set->StopObjectives();
        });

    GW::StoC::RegisterPacketCallback<GW::Packet::StoC::GameSrvTransfer>(&GameSrvTransfer_Entry,
    [this](GW::HookStatus *, GW::Packet::StoC::GameSrvTransfer *packet) -> void {
        if (current_objective_set)
            current_objective_set->StopObjectives();
        current_objective_set = nullptr;
    });
    GW::StoC::RegisterPacketCallback<GW::Packet::StoC::InstanceLoadFile>(&InstanceLoadFile_Entry, 
        [this](GW::HookStatus* status, GW::Packet::StoC::InstanceLoadFile* packet) -> bool {
            if (packet->map_fileID == 219215)
                AddDoAObjectiveSet(packet->spawn_point);
            return false;
        });
    GW::StoC::RegisterPacketCallback<GW::Packet::StoC::InstanceLoadInfo>(&InstanceLoadInfo_Entry,
        [this](GW::HookStatus* status, GW::Packet::StoC::InstanceLoadInfo* packet) -> bool {
			monitor_doors = false;
            if (!packet->is_explorable)
                return false;
            switch (static_cast<GW::Constants::MapID>(packet->map_id)) {
                case GW::Constants::MapID::Urgozs_Warren: 
                    AddUrgozObjectiveSet(); break;
                case GW::Constants::MapID::The_Deep: 
                    AddDeepObjectiveSet(); break;
                case GW::Constants::MapID::The_Fissure_of_Woe:
                    AddFoWObjectiveSet(); break;
                case GW::Constants::MapID::The_Underworld:
                    AddUWObjectiveSet(); break;
            }
            return false;
        });

    GW::StoC::RegisterPacketCallback<GW::Packet::StoC::ManipulateMapObject>(&ManipulateMapObject_Entry,
        [this](GW::HookStatus* status, GW::Packet::StoC::ManipulateMapObject* packet) -> bool {
            if (!monitor_doors || GW::Map::GetInstanceType() != GW::Constants::InstanceType::Explorable)
                return false; // Door not open or not in explorable area
			if (packet->animation_type == 16)
				DoorOpened(packet->object_id);
			else
				DoorClosed(packet->object_id);
            return false;
        });
	GW::StoC::RegisterPacketCallback<GW::Packet::StoC::ObjectiveAdd>(&ObjectiveAdd_Entry,
	[this](GW::HookStatus* status, GW::Packet::StoC::ObjectiveAdd *packet) -> bool {
		// type 12 is the "title" of the mission objective, should we ignore it or have a "title" objective ?
		/*
		Objective *obj = GetCurrentObjective(packet->objective_id);
		if (obj) return false;
		ObjectiveSet *os = objective_sets.back();
		os->objectives.emplace_back(packet->objective_id);
		obj = &os->objectives.back();
		GW::UI::AsyncDecodeStr(packet->name, obj->name, sizeof(obj->name));
		// If the name isn't "???" we consider that the objective started
		if (wcsncmp(packet->name, L"\x8102\x3236", 2))
			obj->SetStarted();
		*/
		return false;
	});

	GW::StoC::RegisterPacketCallback<GW::Packet::StoC::ObjectiveUpdateName>(&ObjectiveUpdateName_Entry,
	[this](GW::HookStatus *, GW::Packet::StoC::ObjectiveUpdateName* packet) -> void {
		Objective *obj = GetCurrentObjective(packet->objective_id);
        if (obj) obj->SetStarted();
	});
	
	GW::StoC::RegisterPacketCallback<GW::Packet::StoC::ObjectiveDone>(&ObjectiveDone_Entry,
	[this](GW::HookStatus *, GW::Packet::StoC::ObjectiveDone* packet) -> void {
		Objective *obj = GetCurrentObjective(packet->objective_id);
        if (obj) {
            obj->SetDone();
            objective_sets.rbegin()->second->CheckSetDone();
        }
	});

    GW::StoC::RegisterPacketCallback<GW::Packet::StoC::AgentUpdateAllegiance>(&AgentUpdateAllegiance_Entry,
        [this](GW::HookStatus *, GW::Packet::StoC::AgentUpdateAllegiance* packet) -> void {
        if (GW::Map::GetMapID() != GW::Constants::MapID::The_Underworld) return;

        GW::Agent* ag = GW::Agents::GetAgentByID(packet->agent_id);
        if (ag == nullptr) return;
        const GW::AgentLiving* agent = ag->GetAsAgentLiving();
        if (agent == nullptr) return;
        if (agent->player_number != GW::Constants::ModelID::UW::Dhuum) return;
        if (packet->allegiance_bits != 0x6D6F6E31) return;
        
        Objective* obj = GetCurrentObjective(157);
        if (obj && !obj->IsStarted()) obj->SetStarted();
    });

	GW::StoC::RegisterPacketCallback<GW::Packet::StoC::DoACompleteZone>(&DoACompleteZone_Entry,
	[this](GW::HookStatus *, GW::Packet::StoC::DoACompleteZone* packet) -> void {
		if (packet->message[0] != 0x8101) return;
		if (objective_sets.empty()) return;

		uint32_t id = packet->message[1];
		Objective *obj = GetCurrentObjective(id);
        ObjectiveSet* os = objective_sets.rbegin()->second;

        if (obj) {
            obj->SetDone();
            os->CheckSetDone();
            uint32_t next_id = doa_get_next(id);
            Objective *next = GetCurrentObjective(next_id);
            if (next && !next->IsStarted()) next->SetStarted();
        }
	});
}

void ObjectiveTimerWindow::ObjectiveSet::StopObjectives() {
    active = false;
	for (Objective& obj : objectives) {
        switch (obj.status) {
        case Objective::Started:
        case Objective::Failed:
            obj.status = Objective::Failed;
            failed = true;
            break;
        }
	}
}

void ObjectiveTimerWindow::AddDoAObjectiveSet(GW::Vec2f spawn) {
    static const GW::Vec2f area_spawns[] = {
        { -10514, 15231 },  // foundry
        { -18575, -8833 },  // city
        { 364, -10445 },    // veil
        { 16034, 1244 },    // gloom
    };
    const GW::Vec2f mallyx_spawn(-3931, -6214);

    const int n_areas = 4;
    double best_dist = GW::GetDistance(spawn, mallyx_spawn);
    int area = -1;
    for (int i = 0; i < n_areas; ++i) {
        float dist = GW::GetDistance(spawn, area_spawns[i]);
        if (best_dist > dist) {
            best_dist = dist;
            area = i;
        }
    }
    if (area == -1) return; // we're doing mallyx, not doa!

	ObjectiveSet *os = new ObjectiveSet;
	::AsyncGetMapName(os->name, sizeof(os->name));
    Objective objs[n_areas] = {{Foundry, "Foundry"}, {City, "City"}, {Veil, "Veil"}, {Gloom, "Gloom"}};

    for (int i = 0; i < n_areas; ++i) {
        os->objectives.push_back(objs[(area + i) % n_areas]);
    }

    os->objectives.front().SetStarted();
    AddObjectiveSet(os);
}
void ObjectiveTimerWindow::AddUrgozObjectiveSet() {
    // Zone 1, Weakness = already open on start
    // Zone 2, Life Drain = Starts when door 45420 opens
    // Zone 3, Levers = Starts when door 11692 opens
    // Zone 4, Bridge = Starts when door 54552 opens
    // Zone 5, Wolves = Starts when door 1760 opens
    // Zone 6, Energy Drain = Starts when door 40330 opens
    // Zone 7, Exhaustion = Starts when door 29537 opens 60114? 54756?
    // Zone 8, Pillars = Starts when door 37191 opens
    // Zone 9, Blood Drinkers = Starts when door 35500 opens
    // Zone 10, Jons Fail Room = Starts when door 34278 opens
    // Zone 11, Urgoz = Starts when door 15529 opens
    // Urgoz = 3750
    // Objective for Urgoz = 357

    ObjectiveTimerWindow::ObjectiveSet* os = new ObjectiveSet;
    ::AsyncGetMapName(os->name, sizeof(os->name));
    os->objectives.emplace_back(1, "Zone 1 | Weakness");
    os->objectives.emplace_back(45420, "Zone 2 | Life Drain");
    os->objectives.emplace_back(11692, "Zone 3 | Levers");
    os->objectives.emplace_back(54552, "Zone 4 | Bridge Wolves");
    os->objectives.emplace_back(1760, "Zone 5 | More Wolves");
    os->objectives.emplace_back(40330, "Zone 6 | Energy Drain");
    os->objectives.emplace_back(29537, "Zone 7 | Exhaustion");
    os->objectives.emplace_back(37191, "Zone 8 | Pillars");
    os->objectives.emplace_back(35500, "Zone 9 | Blood Drinkers");
    os->objectives.emplace_back(34278, "Zone 10 | Bridge");
    os->objectives.emplace_back(15529, "Zone 11 | Urgoz");
    // 45631 53071 are the object_ids for the left and right urgoz doors
    os->objectives.front().SetStarted();
    AddObjectiveSet(os);
	monitor_doors = true;
}
void ObjectiveTimerWindow::AddDeepObjectiveSet() {
    ObjectiveTimerWindow::ObjectiveSet* os = new ObjectiveSet;
    ::AsyncGetMapName(os->name, sizeof(os->name));
    os->objectives.emplace_back(1, "Room 1 | Soothing");
    os->objectives.emplace_back(2, "Room 2 | Death");
    os->objectives.emplace_back(3, "Room 3 | Surrender");
    os->objectives.emplace_back(4, "Room 4 | Exposure");
	for (size_t i = 0; i < 4; i++) {
		os->objectives[i].SetStarted(); // Start first 4 rooms
	}
    os->objectives.emplace_back(5, "Room 5 | Pain");
    os->objectives.emplace_back(RoomID::Deep_room_5, "Room 6 | Lethargy");
	os->objectives.emplace_back(RoomID::Deep_room_6, "Room 7 | Depletion");
    // 8 and 9 together because theres no boundary between
	os->objectives.emplace_back(RoomID::Deep_room_7, "Room 8-9 | Failure/Shadows");
	os->objectives.emplace_back(RoomID::Deep_room_10, "Room 10 | Scorpion"); // Trigger on dialog
	os->objectives.emplace_back(RoomID::Deep_room_11, "Room 11 | Fear"); // Trigger bottom door first spawn
	os->objectives.emplace_back(RoomID::Deep_room_12, "Room 12 | Depletion"); // Trigger on dialog
    // 13 and 14 together because theres no boundary between
	os->objectives.emplace_back(RoomID::Deep_room_13, "Room 13-14 | Decay/Torment"); // Trigger on dialog
    os->objectives.emplace_back(RoomID::Deep_room_15, "Room 15 | Kanaxai");
    AddObjectiveSet(os);
	monitor_doors = true;
}
void ObjectiveTimerWindow::DoorClosed(uint32_t door_id) {
	// Unused
}
void ObjectiveTimerWindow::DoorOpened(uint32_t door_id) {
	bool tick_all_preceeding_objectives = true;
	uint32_t objective_to_start = door_id;
	uint32_t objective_to_end = 0;
	switch (GW::Map::GetMapID()) {
	    case GW::Constants::MapID::Urgozs_Warren:
            switch (door_id) {
                case 45631: // Urgoz left door
                case 53071: // Urgoz right door
                    objective_to_start = 15529;
                    break;
                }
		    break;
	    case GW::Constants::MapID::The_Deep:
		    // For deep, rooms 1-4 can be opened in any order. Only tick all preceeding rooms if we're room 6 door or beyond
		    switch (door_id) {
				    // For deep rooms 1-4, any of these doors mean that room 5 is open
			    case RoomID::Deep_room_1_second:
			    case RoomID::Deep_room_1_first:
				    objective_to_end = 1;
				    objective_to_start = 5;
				    tick_all_preceeding_objectives = false;
				    break;
			    case RoomID::Deep_room_2_second:
			    case RoomID::Deep_room_2_first:
				    objective_to_end = 2;
				    objective_to_start = 5;
				    tick_all_preceeding_objectives = false;
				    break;
			    case RoomID::Deep_room_3_second:
			    case RoomID::Deep_room_3_first:
				    objective_to_end = 3;
				    objective_to_start = 5;
				    tick_all_preceeding_objectives = false;
				    break;
			    case RoomID::Deep_room_4_second:
			    case RoomID::Deep_room_4_first:
				    objective_to_end = 4;
				    objective_to_start = 5;
				    tick_all_preceeding_objectives = false;
				    break;
		    }
		    break;
	    default:
		    return;
	}
	if (objective_to_end) {
		Objective* obj = GetCurrentObjective(objective_to_end);
		if(obj) obj->SetDone();
	}
	// For Urgoz and Deep, the id of the objective is actually the door object_id that STARTS the room
	Objective* obj = GetCurrentObjective(objective_to_start);
	if (!obj || obj->IsStarted())
		return; // Already started
	obj->SetStarted();
	if (!tick_all_preceeding_objectives)
		return;
    ObjectiveSet* os = objective_sets.rbegin()->second;
	for (Objective& objective : os->objectives) {
		if (objective.id == objective_to_start)
			break;
		objective.SetDone();
	}
}
void ObjectiveTimerWindow::AddFoWObjectiveSet() {
	ObjectiveSet *os = new ObjectiveSet;
	::AsyncGetMapName(os->name, sizeof(os->name));
	os->objectives.emplace_back(309, "ToC");
	os->objectives.emplace_back(310, "Wailing Lord");
	os->objectives.emplace_back(311, "Griffons");
	os->objectives.emplace_back(312, "Defend");
	os->objectives.emplace_back(313, "Forge");
	os->objectives.emplace_back(314, "Menzies");
	os->objectives.emplace_back(315, "Restore");
	os->objectives.emplace_back(316, "Khobay");
	os->objectives.emplace_back(317, "ToS");
	os->objectives.emplace_back(318, "Burning Forest");
	os->objectives.emplace_back(319, "The Hunt");
    AddObjectiveSet(os);
}
void ObjectiveTimerWindow::AddObjectiveSet(ObjectiveSet* os) {
    for (auto cos : objective_sets) {
        cos.second->StopObjectives();
        cos.second->need_to_collapse = true;
    }
    objective_sets.emplace(os->system_time, os);
    if(os->active)
        current_objective_set = os;
    runs_dirty = true;
}
void ObjectiveTimerWindow::AddUWObjectiveSet() {
	ObjectiveSet *os = new ObjectiveSet;
	::AsyncGetMapName(os->name, sizeof(os->name));
	os->objectives.emplace_back(146, "Chamber");
	os->objectives.emplace_back(147, "Restore");
	os->objectives.emplace_back(148, "Escort");
	os->objectives.emplace_back(149, "UWG");
	os->objectives.emplace_back(150, "Vale");
	os->objectives.emplace_back(151, "Waste");
	os->objectives.emplace_back(152, "Pits");
	os->objectives.emplace_back(153, "Planes");
	os->objectives.emplace_back(154, "Mnts");
	os->objectives.emplace_back(155, "Pools");
	os->objectives.emplace_back(157, "Dhuum");
    AddObjectiveSet(os);
}
void ObjectiveTimerWindow::DisplayDialogue(GW::Packet::StoC::DisplayDialogue* packet) {
	uint32_t objective_id = 0; // Which objective has just been STARTED?
	switch (GW::Map::GetMapID()) {
	case GW::Constants::MapID::The_Deep:
		switch (packet->message[0]) {
		case kanaxai_room_dialogs::Room10: objective_id = RoomID::Deep_room_10; break;
		case kanaxai_room_dialogs::Room12: objective_id = RoomID::Deep_room_12; break;
		case kanaxai_room_dialogs::Room13: objective_id = RoomID::Deep_room_13; break;
		case kanaxai_room_dialogs::Room14: objective_id = RoomID::Deep_room_14; break;
		case kanaxai_room_dialogs::Room15: objective_id = RoomID::Deep_room_15; break;
		}
		break;
	}
	if (!objective_id)
		return;
	// For Urgoz and Deep, the id of the objective is actually the door object_id that STARTS the room
	Objective* obj = GetCurrentObjective(objective_id);
	if (!obj || obj->IsStarted())
		return; // Already started
	obj->SetStarted();
	for (Objective& objective : current_objective_set->objectives) {
		if (objective.id == objective_id)
			break;
		objective.SetDone();
	}
}
void ObjectiveTimerWindow::Update(float delta) {
    if (current_objective_set && current_objective_set->active) {
        current_objective_set->Update();
    }
    if (runs_dirty && GW::Map::GetInstanceType() == GW::Constants::InstanceType::Loading)
        SaveRuns(); // Save runs between map loads
}
void ObjectiveTimerWindow::Draw(IDirect3DDevice9* pDevice) {
	// Main objective timer window
	if (visible) {
		ImGui::SetNextWindowPosCenter(ImGuiSetCond_FirstUseEver);
		ImGui::SetNextWindowSize(ImVec2(300, 0), ImGuiSetCond_FirstUseEver);
		if (ImGui::Begin(Name(), GetVisiblePtr(), GetWinFlags())) {
			if (objective_sets.empty()) {
				ImGui::Text("Enter DoA, FoW, UW, Deep or Urgoz to begin");
			}
			else {
                ImGuiWindow* window = ImGui::GetCurrentWindow();
                ImGuiStorage* storage = window->DC.StateStorage;
				for (auto& it = objective_sets.rbegin(); it != objective_sets.rend(); it++) {
					bool show = (*it).second->Draw();
					if (!show) {
						objective_sets.erase(--(it.base()));
						break;
						// iterators go crazy, don't even bother, we're skipping a frame. NBD.
						// if you really want to draw the rest make sure you extensively test this.
					}
				}
			}
		}
		ImGui::End();
	}

	// Breakout objective set for current run
	if (show_current_run_window && current_objective_set) {
		ImGui::SetNextWindowPosCenter(ImGuiSetCond_FirstUseEver);
		ImGui::SetNextWindowSize(ImVec2(300, 0), ImGuiSetCond_FirstUseEver);
		char buf[256];
		sprintf(buf, "%s - %s###ObjectiveTimerCurrentRun", current_objective_set->name, current_objective_set->cached_time ? current_objective_set->cached_time : "--:--");
			
		if (ImGui::Begin(buf, &show_current_run_window, GetWinFlags())) {
			ImGui::PushID(current_objective_set->ui_id);
			for (Objective& objective : current_objective_set->objectives) {
				objective.Draw();
			}
			ImGui::PopID();
		}
			
		ImGui::End();
	}
}

ObjectiveTimerWindow::ObjectiveSet* ObjectiveTimerWindow::GetCurrentObjectiveSet() {
	if (objective_sets.empty()) return nullptr;
	if (!current_objective_set || !current_objective_set->active) return nullptr;
	return current_objective_set;
}

ObjectiveTimerWindow::Objective* ObjectiveTimerWindow::GetCurrentObjective(uint32_t obj_id) {
	ObjectiveTimerWindow::ObjectiveSet* o = GetCurrentObjectiveSet();
	if (!o) return nullptr;
    for (Objective& objective : o->objectives) {
        if (objective.id == obj_id) {
            return &objective;
        }
    }
    return nullptr;
}

void ObjectiveTimerWindow::DrawSettingInternal() {
	clear_cached_times = ImGui::Checkbox("Show second decimal", &show_decimal);
    ImGui::Checkbox("Show 'Start' column", &show_start_column);
    ImGui::Checkbox("Show 'End' column", &show_end_column);
    ImGui::Checkbox("Show 'Time' column", &show_time_column);
	ImGui::Checkbox("Show current run in separate window", &show_current_run_window);
	ImGui::Checkbox("Automatic /age on completion", &auto_send_age);
	ImGui::ShowHelp("As soon as final objective is complete, send /age command to game server to receive server-side completion time.");
    ComputeNColumns();
}

void ObjectiveTimerWindow::LoadSettings(CSimpleIni* ini) {
	ToolboxWindow::LoadSettings(ini);
    show_decimal = ini->GetBoolValue(Name(), VAR_NAME(show_decimal), show_decimal);
    show_start_column = ini->GetBoolValue(Name(), VAR_NAME(show_start_column), show_start_column);
    show_end_column = ini->GetBoolValue(Name(), VAR_NAME(show_end_column), show_end_column);
    show_time_column = ini->GetBoolValue(Name(), VAR_NAME(show_time_column), show_time_column);
	show_current_run_window = ini->GetBoolValue(Name(), VAR_NAME(show_current_run_window), show_current_run_window);
	auto_send_age = ini->GetBoolValue(Name(), VAR_NAME(auto_send_age), auto_send_age);
    ComputeNColumns();
    LoadRuns();
}
void ObjectiveTimerWindow::SaveSettings(CSimpleIni* ini) {
	ToolboxWindow::SaveSettings(ini);
    ini->SetBoolValue(Name(), VAR_NAME(show_decimal), show_decimal);
    ini->SetBoolValue(Name(), VAR_NAME(show_start_column), show_start_column);
    ini->SetBoolValue(Name(), VAR_NAME(show_end_column), show_end_column);
    ini->SetBoolValue(Name(), VAR_NAME(show_time_column), show_time_column);
	ini->SetBoolValue(Name(), VAR_NAME(show_current_run_window), show_current_run_window);
	ini->SetBoolValue(Name(), VAR_NAME(auto_send_age), auto_send_age);
    SaveRuns();
}
void ObjectiveTimerWindow::LoadRuns() {
    ClearObjectiveSets();
    
    WIN32_FIND_DATAW FindFileData;
    size_t max_objectives_in_memory = 200;
    std::wstring file_match = Resources::GetPath(L"ObjectiveTimerRuns_*.json");
    std::wstring filename;
    std::set<std::wstring> obj_timer_files;
    HANDLE hFind = FindFirstFileW(file_match.c_str(), &FindFileData);
    if (hFind != INVALID_HANDLE_VALUE) {
        obj_timer_files.insert(FindFileData.cFileName);
        while (FindNextFileW(hFind, &FindFileData) != 0) {
            obj_timer_files.insert(FindFileData.cFileName);
        }
    }
    FindClose(hFind);

    // Output the list of names found
    for (auto it = obj_timer_files.rbegin(); it != obj_timer_files.rend() && objective_sets.size() < max_objectives_in_memory; it++) {
        try {
            std::ifstream file;
            std::wstring fn = Resources::GetPath(*it);
            file.open(fn);
            if (file.is_open()) {
                nlohmann::json os_json_arr;
                file >> os_json_arr;
                for (nlohmann::json::iterator it = os_json_arr.begin(); it != os_json_arr.end(); ++it) {
                    ObjectiveSet* os = ObjectiveSet::FromJson(&it.value());
                    os->StopObjectives();
                    os->need_to_collapse = true;
                    objective_sets.emplace(os->system_time, os);
                }
                file.close();
            }
        }
        catch (...) {
            Log::Error("Failed to load ObjectiveSets from json");
        }
    }
}
void ObjectiveTimerWindow::SaveRuns() {
    if (objective_sets.empty())
        return;
    std::map<std::wstring, std::vector<ObjectiveSet*>> objective_sets_by_file;
    wchar_t filename[36];
    struct tm* structtime;
    for (auto os : objective_sets) {
        time_t tt = (time_t)os.second->system_time;
        structtime = gmtime(&tt);
        if (!structtime)
            continue;
        swprintf(filename, 36, L"ObjectiveTimerRuns_%02d-%02d-%02d.json", structtime->tm_year + 1900, structtime->tm_mon + 1, structtime->tm_mday);
        objective_sets_by_file[filename].push_back(os.second);
    }
    bool error_saving = false;
    for (auto it : objective_sets_by_file) {
        try {
            std::ofstream file;
            file.open(Resources::GetPath(it.first));
            if (file.is_open()) {
                nlohmann::json os_json_arr;
                for (auto os : it.second) {
                    os_json_arr.push_back(os->ToJson());
                }
                file << os_json_arr << std::endl;
                file.close();
            }
        }
        catch (...) {
            Log::Error("Failed to save ObjectiveSets to json");
            error_saving = true;
        }
    }
    runs_dirty = false;
}
void ObjectiveTimerWindow::ClearObjectiveSets() {
    for (auto os : objective_sets) {
        delete os.second;
    }
    objective_sets.clear();
}

ObjectiveTimerWindow::Objective::Objective(uint32_t _id, const char* _name) {
    id = _id;
    GuiUtils::StrCopy(name, _name, sizeof(name));
    start = TIME_UNKNOWN;
    done = TIME_UNKNOWN;
    duration = TIME_UNKNOWN;
    PrintTime(cached_done, sizeof(cached_done), TIME_UNKNOWN);
    PrintTime(cached_start, sizeof(cached_start), TIME_UNKNOWN);
    PrintTime(cached_duration, sizeof(cached_duration), TIME_UNKNOWN);
}

bool ObjectiveTimerWindow::Objective::IsStarted() const { 
    return start != TIME_UNKNOWN;
}
bool ObjectiveTimerWindow::Objective::IsDone() const { 
    return done != TIME_UNKNOWN;
}
void ObjectiveTimerWindow::Objective::SetStarted() {
    if (start == TIME_UNKNOWN) {
        if (GW::Map::GetInstanceType() != GW::Constants::InstanceType::Explorable) {
            start = 0; // still loading, just set to 0
        }
        else {
            start = GW::Map::GetInstanceTime();
        }
    }
    PrintTime(cached_start, sizeof(cached_start), start);
    status = Started;
}
void ObjectiveTimerWindow::Objective::SetDone() {
    if (start == TIME_UNKNOWN) SetStarted(); // something went wrong
    if (done == TIME_UNKNOWN)
        done = GW::Map::GetInstanceTime();
    PrintTime(cached_done, sizeof(cached_done), done);
    duration = done - start;
    PrintTime(cached_duration, sizeof(cached_duration), duration);
    status = Completed;
    runs_dirty = true;
}

void ObjectiveTimerWindow::Objective::Update() {
    if (start == TIME_UNKNOWN) {
        PrintTime(cached_duration, sizeof(cached_duration), TIME_UNKNOWN);
    } else if (done == TIME_UNKNOWN) {
        PrintTime(cached_duration, sizeof(cached_duration), GW::Map::GetInstanceTime() - start);
	} 
	if (ObjectiveTimerWindow::Instance().clear_cached_times) {
		switch (status) {
			case Completed:
				PrintTime(cached_done, sizeof(cached_done), done);
			case Started:
				PrintTime(cached_start, sizeof(cached_start), start);
				if(duration)
					PrintTime(cached_duration, sizeof(cached_duration), duration ? duration : GW::Map::GetInstanceTime() - start);
				break;
		}
	}
}
void ObjectiveTimerWindow::Objective::Draw() {
    switch (status) {
    case ObjectiveTimerWindow::Objective::NotStarted:
        ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
        break;
    case ObjectiveTimerWindow::Objective::Started:
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 1.0f, 1.0f, 1.0f));
        break;
    case ObjectiveTimerWindow::Objective::Completed:
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.0f, 1.0f, 0.0f, 1.0f));
        break;
    case ObjectiveTimerWindow::Objective::Failed:
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.0f, 0.0f, 1.0f));
        break;
    default:
        break;
    }
    auto& style = ImGui::GetStyle();
    style.ButtonTextAlign.x = 0.0f;
    float label_width = GetLabelWidth();
    if (ImGui::Button(name, ImVec2(label_width, 0))) {
        char buf[256];
		sprintf(buf,"[%s] ~ Start: %s ~ End: %s ~ Time: %s",
            name, cached_start, cached_done, cached_duration);
        GW::Chat::SendChat('#', buf);
    }
    style.ButtonTextAlign.x = 0.5f;
    ImGui::PopStyleColor();
    
    float ts_width = GetTimestampWidth();
    float offset = style.ItemSpacing.x + label_width + style.ItemSpacing.x;
    
    ImGui::PushItemWidth(ts_width);
    if (show_start_column) {
        ImGui::SameLine(offset);
        ImGui::Text(cached_start);//ImGui::InputText("##start", cached_start, sizeof(cached_start), ImGuiInputTextFlags_ReadOnly);
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Start");
        offset += ts_width;
        // ImGui::SameLine(offset += ts_width + style.ItemInnerSpacing.x, -1);
    }
    if (show_end_column) {
        ImGui::SameLine(offset);
        ImGui::Text(cached_done); //ImGui::InputText("##end", cached_done, sizeof(cached_done), ImGuiInputTextFlags_ReadOnly);
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("End");
        offset += ts_width + style.ItemSpacing.x;
        //ImGui::SameLine();//ImGui::SameLine(offset += ts_width + style.ItemInnerSpacing.x, -1);
    }
    if (show_time_column) {
        ImGui::SameLine(offset);
        ImGui::Text(cached_duration); //ImGui::InputText("##time", cached_duration, sizeof(cached_duration), ImGuiInputTextFlags_ReadOnly);
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Time");
        //ImGui::SameLine();//ImGui::SameLine(offset += ts_width + style.ItemInnerSpacing.x, -1);
    }
}

void ObjectiveTimerWindow::ObjectiveSet::Update() {
    if (!active) return;

    if (GW::Map::GetInstanceType() == GW::Constants::InstanceType::Explorable) {
        instance_time = GW::Map::GetInstanceTime();
    }

    for (Objective& obj : objectives) {
        obj.Update();
    }
}
void ObjectiveTimerWindow::ObjectiveSet::CheckSetDone() {
    bool done = true;
    for (const Objective& obj : objectives) {
        if (obj.done == TIME_UNKNOWN) {
            done = false;
            break;
        }
    }
    if (done) {
        active = false;
		if (ObjectiveTimerWindow::Instance().auto_send_age)
			GW::Chat::SendChat('/', "age");
    }
}

ObjectiveTimerWindow::ObjectiveSet::ObjectiveSet() : ui_id(cur_ui_id++) {
    system_time = static_cast<DWORD>(time(NULL));
}

ObjectiveTimerWindow::ObjectiveSet* ObjectiveTimerWindow::ObjectiveSet::FromJson(nlohmann::json* json) {
    ObjectiveSet* os = new ObjectiveSet;
    os->active = false;
    os->system_time = json->at("utc_start").get<DWORD>();
    std::string name = json->at("name").get<std::string>();
    snprintf(os->name, sizeof(os->name), "%s", name.c_str());
    os->instance_time = json->at("instance_start").get<DWORD>();
    nlohmann::json json_objs = json->at("objectives");
    for (nlohmann::json::iterator it = json_objs.begin(); it != json_objs.end(); ++it) {
        nlohmann::json o = it.value();
        name = o.at("name").get<std::string>();
        Objective obj(o.at("id").get<DWORD>(),name.c_str());
        obj.status = o.at("status").get<Objective::Status>();
        obj.start = o.at("start").get<DWORD>();
        obj.duration = o.at("duration").get<DWORD>();
        obj.done = o.at("done").get<DWORD>();
        if (obj.done == 1 && obj.duration)
            obj.done = obj.start + obj.duration;
        switch (obj.status) {
        case Objective::Status::Completed:
            PrintTime(obj.cached_done, sizeof(obj.cached_done), obj.done);
        case Objective::Status::Failed:
        case Objective::Status::Started:
            PrintTime(obj.cached_start, sizeof(obj.cached_start), obj.start);
            if (obj.duration)
                PrintTime(obj.cached_duration, sizeof(obj.cached_duration), obj.duration);
            break;
        }
        os->objectives.emplace_back(obj);
    }
    os->StopObjectives();
    return os;
}
nlohmann::json ObjectiveTimerWindow::ObjectiveSet::ToJson() {
    nlohmann::json json;
    json["name"] = name;
    json["instance_start"] = instance_time;
    json["utc_start"] = system_time;
    nlohmann::json json_objectives;
    for (auto o : objectives) {
        nlohmann::json obj_json;
        obj_json["id"] = o.id;
        obj_json["name"] = o.name;
        obj_json["status"] = o.status;
        obj_json["start"] = o.start;
        obj_json["done"] = o.done;
        obj_json["duration"] = o.duration;
        json_objectives.push_back(obj_json);
    }
    json["objectives"] = json_objectives;
    return json;
}
bool ObjectiveTimerWindow::ObjectiveSet::Draw() {
    char buf[256];
    if (!cached_start[0]) {
        time_t ts = (time_t)system_time;
        struct tm timeinfo;
        memcpy(&timeinfo,localtime(&ts),sizeof(timeinfo));
        time_t now = time(NULL);
        struct tm* nowinfo = localtime(&now);
        int cached_str_offset = 0;
        if (timeinfo.tm_yday != nowinfo->tm_yday || timeinfo.tm_year != nowinfo->tm_year || true) {
            char* months[] = { "Jan","Feb","Mar","Apr","May","Jun","Jul","Aug","Sep","Oct","Nov","Dec" };
            cached_str_offset += snprintf(&cached_start[cached_str_offset], sizeof(cached_start) - cached_str_offset, "%s %02d, ", months[timeinfo.tm_mon], timeinfo.tm_mday);
        }
        snprintf(&cached_start[cached_str_offset], sizeof(cached_start) - cached_str_offset, "%02d:%02d", timeinfo.tm_hour, timeinfo.tm_min);
    }
    PrintTime(cached_time, sizeof(cached_time), instance_time, false);

    sprintf(buf, "%s - %s - %s%s###header%d", cached_start, name, cached_time, failed ? " [Failed]" : "", ui_id);

    const auto& style = ImGui::GetStyle();
    float offset = 0;
    float ts_width = GetTimestampWidth();
    bool is_open;
    if (ImGui::CollapsingHeader(buf, &is_open, ImGuiTreeNodeFlags_DefaultOpen)) {
        ImGui::PushID(ui_id);
        for (Objective& objective : objectives) {
            objective.Draw();
        }
        ImGui::PopID();
    }
    if (need_to_collapse) {
        ImGui::GetCurrentWindow()->DC.StateStorage->SetInt(ImGui::GetID(buf), 0);
        need_to_collapse = false;
    }
    return is_open;
}
