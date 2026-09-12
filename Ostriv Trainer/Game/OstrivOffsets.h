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
    constexpr uintptr_t MONEY_STATE_POINTER_OFFSET = 0x67F4F0;
    constexpr uintptr_t MONEY_OFFSET = 0x139D18;

    // ============================================================
    // Live Building Array (INGAME_BUILDINGS_TABLE)
    //
    // The runtime array of every building instance that currently exists
    // in the city. Static offset from module base — no signature needed.
    // Both its location and its element count are read directly from
    // memory every slow tick; nothing here is guessed.
    // ============================================================

    constexpr uintptr_t INGAME_BUILDINGS_TABLE_OFFSET = 0x687E88;

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

    // Child -> owning RowHouse container. The parent's own position and
    // active/demolishing status are authoritative for its children.
    constexpr uintptr_t INGAME_BUILDING_PARENT_OFFSET_APARTMENT = 0x7D8;
    constexpr uintptr_t INGAME_BUILDING_PARENT_OFFSET_SHOP = 0x828;

    // Apartment / Village house / Fenceless village house -> the resident
    // family object. May be null if no family has moved in yet.
    constexpr uintptr_t INGAME_BUILDING_FAMILY_POINTER_OFFSET = 0x7C8;

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
    constexpr uintptr_t BUILDING_DICTIONARY_TABLE_POINTER_OFFSET = 0x67FBF8;
    constexpr uintptr_t BUILDING_DICTIONARY_TABLE_COUNT_OFFSET = 0x67FBF0;

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

    // Updated for game version 0.5.9.61 — confirmed via Ghidra decompile
    // (lea rsi,[+68A840] loading the base, followed by
    // [rsi+resourceId*2*8] / [rsi+resourceId*2*8+08] for the name
    // pointer/length pair — the same *0x10 stride expressed as *2 then
    // *8, since x86 addressing only supports scale factors 1/2/4/8).
    // Previous value for 0.5.9.60: 0x6AE850.
    constexpr uintptr_t RESOURCE_TABLE_OFFSET = 0x68A840;

    constexpr size_t RESOURCE_ENTRY_SIZE = 0x10;

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

    // Updated for game version 0.5.9.61 — confirmed via Ghidra byte-pattern
    // search for the unchanged instruction bytes/struct offset
    // (movss [r15+0x11B678], xmm0). Previous value for 0.5.9.60: 0x2C6952.
    constexpr uintptr_t CAMERA_UPDATE_RVA = 0x2B01B2;

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
    // Updated for game version 0.5.9.61 — same hook bytes/logic as
    // before, only the module-relative location shifted. Previous value
    // for 0.5.9.60: 0x219680
    constexpr uintptr_t SELECTION_HOOK_RVA = 0x203FF0;

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
}