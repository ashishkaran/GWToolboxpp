#pragma once

#include <Windows.h>
#include <d3d9.h>

#include <SimpleIni.h>
#include <imgui.h>
#include <GWCA\Constants\Constants.h>
#include <GWCA\GameEntities\Item.h>
#include <GWCA\Managers\AgentMgr.h>
#include <GWCA\Managers\MapMgr.h>

#include <GWCA\Packets\CtoSHeaders.h>
#include "Timer.h"
#include <Color.h>
#include <mutex>
#include <vector>


class Pcon {
public:
	static int pcons_delay;
	static int lunar_delay;
	static bool map_has_effects_array;
	static float size;
	static bool disable_when_not_found;
	static bool refill_if_below_threshold;
    static bool pcons_by_character;
	static Color enabled_bg_color;
	static DWORD player_id;

	static DWORD alcohol_level;
	static bool suppress_drunk_effect;
	static bool suppress_drunk_text;
	static bool suppress_drunk_emotes;
	static bool suppress_lunar_skills;

	static std::vector<std::vector<clock_t>> reserved_bag_slots;

protected:
    Pcon(const char* chatname,
        const char* abbrevname,
        const char* ininame,
        const wchar_t* filename,
        WORD res_id, // you can use 0 and it will not load texture from resource, only from file.
        ImVec2 uv0, ImVec2 uv1, int threshold,
        const char* desc = nullptr);
	~Pcon();
    bool* GetSettingsByName(const wchar_t* name);
    static bool UnreserveSlotForMove(int bagId, int slot); // Unlock slot.
	static bool ReserveSlotForMove(int bagId, int slot); // Prevents more than 1 pcon from trying to add to the same slot at the same time.
	static bool IsSlotReservedForMove(int bagId, int slot); // Checks whether another pcon has reserved this slot.

	GW::Bag* pending_move_to_bag = nullptr;
	uint32_t pending_move_to_slot = 0;
	uint32_t pending_move_to_quantity = 0;
public:
	void Initialize();
	virtual void Draw(IDirect3DDevice9* device);
	virtual void Update(int delay = -1);
	// Similar to GW::Items::MoveItem, except this returns amount moved and uses the split stack header when needed.
	// Most of this logic should be integrated back into GWCA repo, but I've written it here for GWToolbox
	static uint32_t MoveItem(GW::Item *item, GW::Bag *bag, int slot, int quantity = 0);
	static GW::Bag* GetBag(uint32_t bag_id);
    wchar_t* SetPlayerName();
	// Fires off another thread to refill pcons. Sets refill_attempted to TRUE when finished.
    void Refill();
	void StopRefill();
	void SetEnabled(bool enabled);
    const bool IsEnabled() { return IsVisible() && *enabled; }
	virtual bool IsVisible() const;
	void AfterUsed(bool used, int qty);
	inline void Toggle() { SetEnabled(!IsEnabled()); }
	// Resets pcon counters so it needs to recalc number and refill.
	void ResetCounts();
	void LoadSettings(CSimpleIni* ini, const char* section);
	void SaveSettings(CSimpleIni* ini, const char* section);

public:
    bool* enabled; // This is a ptr to the current char's status if applicable.
	bool pcon_quantity_checked = false;
    bool refilling = false; // Set when a refill is in progress. Dont touch.
    bool refill_attempted = false; // Set to true when refill thread has run for this map
	int threshold = 0; 
    bool visible = true;
    int quantity = 0;
	// Icon vars
	WORD res_id = 0;
	std::wstring filename;
	int quantity_storage = 0;

	clock_t timer = 0;

	const char* const chat;
	const char* const abbrev;
    std::string desc;
	const char* const ini;

protected:
	// Cycles through character's inventory to find a matching (incomplete) stack, or an empty pane.
	GW::Item* FindVacantStackOrSlotInInventory(GW::Item* likeItem = nullptr);
	GW::AgentLiving* player = nullptr;

    // "default" is the fallback
    std::map<std::wstring, bool*> settings_by_charname;

	GW::Constants::MapID mapid = GW::Constants::MapID::None;
	GW::Constants::InstanceType maptype = GW::Constants::InstanceType::Loading;
	// loops over the inventory, counting the items according to QuantityForEach
	// if 'used' is not null, it will also use the first item found,
	// and, if so, used *used to true
	// returns the number of items found, or -1 in case of error
	int CheckInventory(bool* used = nullptr, int* used_qty = nullptr, int from_bag = static_cast<int>(GW::Constants::Bag::Backpack), int to_bag = static_cast<int>(GW::Constants::Bag::Bag_2)) const;

	virtual bool CanUseByInstanceType() const;
	virtual bool CanUseByEffect() const = 0;
	virtual int QuantityForEach(const GW::Item* item) const = 0;

private:
	IDirect3DTexture9* texture = nullptr;
	const ImVec2 uv0;
	const ImVec2 uv1;
    std::thread refill_thread;
};

// A generic Pcon has an item_id and effect_id
class PconGeneric : public Pcon {
public:
	PconGeneric(const char* chat,
		const char* abbrev,
		const char* ini,
		const wchar_t* file,
		WORD res_id,
		ImVec2 uv0, ImVec2 uv1,
		DWORD item, GW::Constants::SkillID effect, 
		int threshold,
        	const char* desc = nullptr)
		: Pcon(chat, abbrev, ini, file, res_id, uv0, uv1, threshold, desc),
		itemID(item), effectID(effect) {}

protected:
	bool CanUseByEffect() const override;
	int QuantityForEach(const GW::Item* item) const override;

private:
	const DWORD itemID;
	const GW::Constants::SkillID effectID;
};

// Same as generic pcon, but with more restrictions on usage
class PconCons : public PconGeneric {
public:
	PconCons(const char* chat,
		const char* abbrev,
		const char* ini,
		const wchar_t* file,
		WORD res_id,
		ImVec2 uv0, ImVec2 uv1,
		DWORD item, GW::Constants::SkillID effect, 
		int threshold,
        	const char* desc = nullptr)
		: PconGeneric(chat, abbrev, ini, file, res_id, uv0, uv1, item, effect, threshold, desc) {}

	bool CanUseByEffect() const override;
};

class PconCity : public Pcon {
public:
	PconCity(const char* chat,
		const char* abbrev,
		const char* ini,
		const wchar_t* file,
		WORD res_id,
		ImVec2 uv0, ImVec2 uv1, 
		int threshold,
        	const char* desc = nullptr)
		: Pcon(chat, abbrev, ini, file, res_id, uv0, uv1, threshold, desc) {}

	bool CanUseByInstanceType() const;
	bool CanUseByEffect() const override;
	int QuantityForEach(const GW::Item* item) const override;
};
// Used only in outposts for refilling
class PconRefiller : public PconCity {
public:
    
    PconRefiller(const char* chat,
		const char* abbrev,
        const char* ini,
        const wchar_t* file,
        WORD res_id,
        ImVec2 uv0, ImVec2 uv1,
        DWORD item,
        int threshold,
        const char* desc_ = nullptr)
        : PconCity(chat, abbrev, ini, file, res_id, uv0, uv1, threshold, desc_), itemID(item) {
        if (desc.size())
            desc += "\n";
        desc += "Enable in an outpost to refill your inventory.";
    }

    bool CanUseByInstanceType() const override { return false; }
    bool CanUseByEffect() const override { return false; }
	bool IsVisible() const { return visible && GW::Map::GetInstanceType() == GW::Constants::InstanceType::Outpost; }
    void Draw(IDirect3DDevice9* device) override;
    int QuantityForEach(const GW::Item* item) const override { return item->model_id == itemID ? 1 : 0; }
private:
    const DWORD itemID;
};

class PconAlcohol : public Pcon {
public:
	PconAlcohol(const char* chat,
		const char* abbrev,
		const char* ini,
		const wchar_t* file,
		WORD res_id,
		ImVec2 uv0, ImVec2 uv1,
		int threshold,
        	const char* desc = nullptr)
		: Pcon(chat, abbrev, ini, file, res_id, uv0, uv1, threshold, desc) {}

	bool CanUseByEffect() const override;
	int QuantityForEach(const GW::Item* item) const override;
	void ForceUse();
};

class PconLunar : public Pcon {
public:
	PconLunar(const char* chat,
		const char* abbrev,
		const char* ini,
		const wchar_t* file,
		WORD res_id,
		ImVec2 uv0, ImVec2 uv1, 
		int threshold,
        	const char* desc = nullptr)
		: Pcon(chat, abbrev, ini, file, res_id, uv0, uv1, threshold, desc) {}

	void Update(int delay = -1) override;
	bool CanUseByEffect() const override;
	int QuantityForEach(const GW::Item* item) const override;
};
