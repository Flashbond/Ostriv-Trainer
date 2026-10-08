#pragma once

#include <cstdint>

namespace Ostriv
{
    // ============================================================
    // Process
    // ============================================================

    constexpr wchar_t PROCESS_NAME[] = L"ostriv.exe";

    // ============================================================
    // Money
    // ============================================================

    // Updated for game version 0.5.9.61 — confirmed via Cheat Engine
    // (wrote a unique known value via Set Money, found it with a fresh
    // Double scan, "find out what writes" showed
    // mov rax,[+67F4F0]; movsd xmm0,[rax+139D18]).
    // Switched from a signature scan to a direct static offset for the
    // state pointer slot itself — same reasoning as
    // INGAME_BUILDINGS_TABLE_OFFSET: this is a fixed global slot, not an
    // instruction whose own bytes need pattern-matching.
    // Previous values for 0.5.9.60: state pointer via signature scan
    // "48 8B 05 ?? ?? ?? ?? F2 0F 10 80 F0 9C 13 00", MONEY_OFFSET=0x139CF0.
    constexpr uintptr_t MONEY_STATE_POINTER_OFFSET = 0x675C40;
    constexpr uintptr_t MONEY_OFFSET = 0x139D18;

    // ============================================================
    // Live Building Array (INGAME_BUILDINGS_TABLE)
    //
    // The runtime array of every building instance that currently exists
    // in the city. Static offset from module base — no signature needed.
    // Both its location and its element count are read directly from
    // memory every slow tick; nothing here is guessed.
    // ============================================================

    constexpr uintptr_t INGAME_BUILDINGS_TABLE_OFFSET = 0x67E590;

    constexpr uintptr_t INGAME_BUILDINGS_TABLE_COUNT_OFFSET = 0x00;
    constexpr uintptr_t INGAME_BUILDINGS_TABLE_ARRAY_OFFSET = 0x08;

    // Sanity ceiling on the count read above — not a real capacity limit,
    // just a guard against a corrupted/misread value.
    constexpr int INGAME_BUILDINGS_TABLE_MAX_COUNT = 500000;

    // ============================================================
    // Building Object
    //
    // Fields on a single live building instance (an element of
    // INGAME_BUILDINGS_TABLE), NOT a table of its own.
    // ============================================================

    constexpr uintptr_t BUILDING_INVENTORY_SIZE_OFFSET = 0x1A8;

    constexpr uintptr_t INGAME_BUILDING_ACTIVE_STATUS_OFFSET = 0x1B0;

    constexpr uintptr_t INGAME_BUILDING_DEMOLISHING_STATUS_OFFSET = 0x298;

    // ------------------------------------------------------------
    // Re-finding the building-object offsets below after a game update
    //   INGAME_BUILDING_FAMILY_POINTER_OFFSET
    //   INGAME_BUILDING_PARENT_OFFSET_APARTMENT
    //   INGAME_BUILDING_PARENT_OFFSET_SHOP
    //
    // These are fields inside the building class, so they move together
    // when a field is added or removed before them. In 0.5.9.62 the family
    // pointer moved from 0x7C8 to 0x7E0 (+0x18); try the same shift first
    // for the other two.
    //
    // 1. Disconnect the trainer first: while connected, our own JMP patch
    //    sits on the hook site and collides with a debugger breakpoint.
    // 2. In Cheat Engine, put a breakpoint on ostriv.exe + SELECTION_HOOK_RVA
    //    (the OffsetResolver report prints the current value) and click a
    //    building in the game. RCX = that building's address (B). Remove the
    //    breakpoint right away: the function runs every frame while the panel
    //    is open.
    // 3. Dump B+0x780 .. B+0x880 and look for 8-byte pointers:
    //    FAMILY: pick a house that has a family and find the pointer whose
    //            target + FAMILY_MONEY_OFFSET holds the family's money (float).
    //    PARENT: find the pointer whose target's +0x18 id string starts with
    //            "building_rowhouse". Do it once for an Apartment and once
    //            for a Shop.
    //
    // A wrong parent offset does not crash: Building::ResolveParentAddress
    // checks the target's id string, so Apartments/Shops simply vanish from
    // the list. A wrong family offset is worse, since Family Money is
    // written to. Do not press Set Amount on that row until it is verified.
    //
    // FAMILY_MONEY_OFFSET lives in the family object, a different class, and
    // did not move in 0.5.9.62. It can still move on its own.
    //
    // Last verified: 0.5.9.62.
    // ------------------------------------------------------------
    // Child -> owning RowHouse container. The parent's own position and
    // active/demolishing status are authoritative for its children.
    constexpr uintptr_t INGAME_BUILDING_PARENT_OFFSET_APARTMENT = 0x7F0;
    constexpr uintptr_t INGAME_BUILDING_PARENT_OFFSET_SHOP = 0x840;

    // Apartment / Village house / Fenceless village house -> the resident
    // family object. May be null if no family has moved in yet.
    constexpr uintptr_t INGAME_BUILDING_FAMILY_POINTER_OFFSET = 0x7E0;

    // Family object -> household savings.
    constexpr uintptr_t FAMILY_MONEY_OFFSET = 0x128;

    // This building instance's own raw id string ("building_glassworks")
    // — a pointer + a byte length carried on the object itself, not
    // looked up in BUILDING_DICTIONARY_TABLE.
    constexpr uintptr_t INGAME_BUILDING_ID_PTR_OFFSET = 0x18;
    constexpr uintptr_t INGAME_BUILDING_ID_LENGTH_OFFSET = 0x20;

    constexpr uintptr_t INGAME_BUILDING_POSITION_X_OFFSET = 0x108;
    constexpr uintptr_t INGAME_BUILDING_POSITION_Y_OFFSET = 0x110;

    // ============================================================
    // Building Filters
    // ============================================================
    // NOT a fixed "building type" marker, despite the name and despite it
    // reliably reading exactly 64 in every building we tested for a long
    // time — it's actually the inventory's own CAPACITY, which starts at
    // 64 and grows in the same +64-per-step pattern we found for the
    // building dictionary table. A building that has accumulated more
    // than 64 distinct resource types over a long save (confirmed: a
    // ~100-year farm grew to 0x80/128) silently reads a LARGER value here
    // — an exact-equality check would then incorrectly treat it as
    // irrelevant and drop it from the list. Use ">=" against this floor,
    // never "==".
    constexpr uint32_t TARGET_INVENTORY_TYPE = 64;

    // The RowHouse container's own INVENTORY_TYPE_OFFSET value — confirmed
    // by measurement. It does not report TARGET_INVENTORY_TYPE like a
    // normal production building; this is its own accepted type.
    constexpr uint32_t ROWHOUSE_INVENTORY_TYPE = 0x00;

    constexpr uint32_t ACTIVE_BUILDING_VALUE = 1;

    // Updated for game version 0.5.9.61 — confirmed via Ghidra decompile
    // of the building-id dictionary lookup routine
    // (mov r8,[+67FBF8]; movsxd rax,[+67FBF0]; imul rax,rax,0xA8),
    // same exact pointer/count layout and 0xA8 entry size as before,
    // only the module-relative location shifted. Previous values for
    // 0.5.9.60: pointer=0x6A3B78, count=0x6A3B70.
    constexpr uintptr_t BUILDING_DICTIONARY_TABLE_POINTER_OFFSET = 0x676348;
    constexpr uintptr_t BUILDING_DICTIONARY_TABLE_COUNT_OFFSET = 0x676340;

    // Sanity ceiling on the count read above — not a real capacity limit,
    // just a guard against a corrupted/misread value.
    constexpr int BUILDING_DICTIONARY_TABLE_MAX_COUNT = 100000;

    // Per-row layout. Each row already carries its own raw id string
    // pointer at +0x00 — there is no separate "id table" to find or trust;
    // the dictionary is self-contained.
    constexpr size_t BUILDING_DICTIONARY_ENTRY_SIZE = 0xA8;

    constexpr uintptr_t BUILDING_DICTIONARY_ID_PTR_OFFSET = 0x00;
    constexpr uintptr_t BUILDING_DICTIONARY_NAME_PTR_OFFSET = 0x10;
    constexpr uintptr_t BUILDING_DICTIONARY_NAME_LENGTH_OFFSET = 0x18;

    // ============================================================
    // Resource Table
    // ============================================================

    // Confirmed in Ghidra for 0.5.9.62: DAT_140688a10 is referenced by
    // FUN_14021be70 (the function that defines every resource) and by the
    // exit-time destructor thunk. NOT 0x688A00: that is the capacity field
    // of a static vector inside FUN_140203ca0.
    // Previous: 0x688A10 (0.5.9.62), 0x68A840 (0.5.9.61), 0x6AE850 (0.5.9.60).
    constexpr uintptr_t RESOURCE_TABLE_OFFSET = 0x680F60;

    constexpr size_t RESOURCE_ENTRY_SIZE = 0x10;

    // Compiler-emitted literal count (and entry size) passed to the thunk
// that destructs the resource table at exit. Reading it directly means
// a future update that adds resources is picked up without a new
// trainer release. Both RIP-relative displacements (the destructor
// thunk's own address, and the table's address) are wildcarded; the
// entry size (0x10) is kept as a literal since it's part of what makes
// this exact call recognizable.
    constexpr char RESOURCE_TABLE_COUNT_SIGNATURE[] =
        "4C 8D 0D ?? ?? ?? ?? BA 10 00 00 00 41 B8 ?? ?? ?? ?? 48 8D 0D ?? ?? ?? ?? E9 ?? ?? ?? ??";
    constexpr size_t RESOURCE_TABLE_COUNT_SIG_IMM_OFFSET = 14;
    constexpr int32_t RESOURCE_TABLE_COUNT_SANITY_MAX = 4096;

    // Confirmed via decompilation, not guessed: a compiler-generated
    // `eh_vector_destructor_iterator(&DAT_1406ae850, 0x10, 0xbc, ...)`
    // call destructs this exact array — 0x10 matches RESOURCE_ENTRY_SIZE,
    // and 0xBC (188) is the true element count, both baked in as
    // compile-time constants.
    constexpr int32_t RESOURCE_TABLE_COUNT = 0xBC;

    constexpr uintptr_t RESOURCE_NAME_PTR_OFFSET = 0x00;
    constexpr uintptr_t RESOURCE_NAME_LENGTH_OFFSET = 0x08;

    // ============================================================
    // Inventory
    // ============================================================

    constexpr uintptr_t INVENTORY_COUNT_OFFSET = 0x198;
    constexpr uintptr_t INVENTORY_ARRAY_OFFSET = 0x1A0;

    // The entries region is exactly capacity * INVENTORY_ENTRY_SIZE bytes
    // long, with NO padding — confirmed by direct address-delta
    // measurement on two live inventories of different capacities (64
    // and 128), both matching exactly. Capacity is per-building, read
    // from INVENTORY_TYPE_OFFSET (see its comment) — there is no fixed
    // scan bound anymore.
    constexpr uintptr_t INVENTORY_ID_TRAILING_OFFSET = 0x08;

    // Sanity ceiling on a building's reported capacity — not a real
    // limit, just a guard against a corrupted/misread value driving an
    // absurd scan loop.
    constexpr int32_t INVENTORY_CAPACITY_SANITY_MAX = 4096;
    constexpr uintptr_t INVENTORY_STATUS_BLOCK_OFFSET = 0x08;
    constexpr uintptr_t INVENTORY_RESOURCE_ID_OFFSET = 0x00;
    constexpr uintptr_t INVENTORY_AMOUNT_OFFSET = 0x04;
    constexpr uintptr_t INVENTORY_AWAITING_OFFSET = 0x09;
    constexpr uintptr_t INVENTORY_RESERVED_OFFSET = 0x10;

    constexpr size_t INVENTORY_ENTRY_SIZE = 0x14;

    // The exact byte pattern a genuinely empty entry has, confirmed by
    // direct observation: id=0, amount=0.0f, status block=0, sentinel
    // stays -1.0f, tail=0. Used both to recognize a free slot
    // (AddResource) and to reset one back to this state (ClearResource).
    constexpr uint8_t INVENTORY_EMPTY_ENTRY_PATTERN[INVENTORY_ENTRY_SIZE] = {
        0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x80, 0xBF,
        0x00, 0x00, 0x00, 0x00
    };

    // ============================================================
    // Camera and Hook Constants
    // ============================================================
    // Fallback only: last known location, used when the signature scan
    // finds nothing — typically because a previous session left our own
    // JMP patch here (the bytes no longer match, but DetourHook can still
    // recognize and undo its own leftover patch at a known address).
    // Doesn't need updating every game patch. Last set for 0.5.9.62.
    constexpr uintptr_t CAMERA_UPDATE_RVA = 0x2B0022;

    constexpr SIZE_T CAMERA_HOOK_SIZE = 9;

    const BYTE CAMERA_ORIGINAL_BYTES[CAMERA_HOOK_SIZE] = { 0xF3, 0x41, 0x0F, 0x11, 0x87, 0x78, 0xB6, 0x11, 0x00 };

    // ============================================================
    // Camera Object (r15) Offsets
    // ============================================================

    constexpr uintptr_t CAMERA_TARGET_X_OFFSET = 0x11B66C;
    constexpr uintptr_t CAMERA_TARGET_Z_OFFSET = 0x11B674;
    constexpr uintptr_t CAMERA_CURRENT_X_OFFSET = 0x11B678;
    constexpr uintptr_t CAMERA_CURRENT_Z_OFFSET = 0x11B680;

    // ============================================================
    // Camera Shared (Code Cave Data) Offsets
    // ============================================================

    constexpr uintptr_t CAVE_DATA_FLAG_OFFSET = 0x00;
    constexpr uintptr_t CAVE_DATA_X_OFFSET = 0x04;
    constexpr uintptr_t CAVE_DATA_Z_OFFSET = 0x08;

    // ============================================================
    // Selection Hook
    // ============================================================
    // Updated for game version 0.5.9.62 — same hook bytes/logic as
    // before, only the module-relative location shifted. Previous value
    // for 0.5.9.60: 0x219680
    //for 0.5.9.61: 0x203FF0
    constexpr uintptr_t SELECTION_HOOK_RVA = 0x203EA0;

    constexpr size_t SELECTION_HOOK_SIZE = 5;

    constexpr uint8_t SELECTION_HOOK_ORIGINAL_BYTES[] =
    { 0x4C, 0x89, 0x44, 0x24,0x18 };


    // ============================================================
    // Memory Safety Limits
    // ============================================================

    constexpr uintptr_t MIN_VALID_POINTER = 0x10000000000ULL;
    constexpr uintptr_t MAX_VALID_POINTER = 0x7FFFFFFFFFFFULL;

    constexpr size_t MAX_STRING_LENGTH = 64;
    constexpr size_t MAX_BUILDING_ID_LENGTH = 1024;

    // ============================================================
    // Signatures (used by OffsetResolver, first candidate for each item)
    // "??" = bytes that change between builds (RIP-relative displacements).
    // Everything else in this file is the last-known fallback.
    // ============================================================

    constexpr char INGAME_BUILDINGS_TABLE_SIGNATURE[] =
        "48 63 05 ?? ?? ?? ?? 4D 85 C0 4D 8B E1 4D 8B F0 40 0F 94 C6 4C 8B EA 85 C0 7E ?? 48 8B 3D ?? ?? ?? ?? 48 8D 2C C7";
    constexpr size_t INGAME_BUILDINGS_TABLE_SIG_COUNT_INSN = 0;   // movsxd rax,[rip+count]
    constexpr size_t INGAME_BUILDINGS_TABLE_SIG_ARRAY_INSN = 27;  // mov rdi,[rip+array]

    constexpr char BUILDING_DICTIONARY_TABLE_SIGNATURE[] =
        "4C 8B 05 ?? ?? ?? ?? 48 63 05 ?? ?? ?? ?? 85 C0 49 0F 4F F0 7E ?? 48 69 C0 A8 00 00 00 49 03 C0";
    constexpr size_t BUILDING_DICTIONARY_TABLE_SIG_POINTER_INSN = 0;
    constexpr size_t BUILDING_DICTIONARY_TABLE_SIG_COUNT_INSN = 7;

    // The money field offset (18 9D 13 00) is deliberately NOT wildcarded:
    // many state fields are read with this same instruction shape, so the
    // concrete offset is what makes the match unique. Update it together
    // with MONEY_OFFSET.
    constexpr char MONEY_STATE_POINTER_SIGNATURE[] =
        "48 8B 05 ?? ?? ?? ?? F2 0F 10 80 18 9D 13 00";
    constexpr size_t MONEY_STATE_POINTER_SIG_INSN = 0;

    constexpr char CAMERA_HOOK_SIGNATURE[] = "F3 41 0F 11 87 78 B6 11 00";

    // Not derived yet — the resolver falls back to SELECTION_HOOK_RVA.
    constexpr char SELECTION_HOOK_SIGNATURE[] = "";

    // ============================================================
    // Manual overrides (hints for OffsetResolver)
    //
    // 0 = unused. Fill one in only when the resolver cannot find an item
    // on its own. The value is the item's module-relative address, same
    // meaning as the matching *_OFFSET / *_RVA constant:
    //   buildings table     -> count slot   (array pointer is 0x08 after it)
    //   building dictionary -> count slot   (pointer slot is 0x08 after it)
    //   resource table      -> address of entry 0
    //   money               -> the slot holding the state-object pointer
    //   hooks               -> the instruction to patch
    // An override is validated like any other candidate. A rejected one is
    // ignored and reported. Clear overrides after each game update, or
    // they go stale the same way the last-known values do.
    // ============================================================

    constexpr uintptr_t INGAME_BUILDINGS_TABLE_OVERRIDE = 0;
    constexpr uintptr_t BUILDING_DICTIONARY_TABLE_OVERRIDE = 0;
    constexpr uintptr_t RESOURCE_TABLE_OVERRIDE = 0;
    constexpr uintptr_t MONEY_STATE_POINTER_OVERRIDE = 0;
    constexpr uintptr_t CAMERA_HOOK_OVERRIDE = 0;
    constexpr uintptr_t SELECTION_HOOK_OVERRIDE = 0;
}