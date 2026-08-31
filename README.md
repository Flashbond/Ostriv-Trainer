# Ostriv Trainer

A memory-editing trainer for [Ostriv](https://ostriv.fun/), the city-building simulation game. It gives you a live, always-up-to-date view of every relevant building in your city, lets you rename buildings, teleport the camera to them, add or remove resources, and lock resource or money amounts so they stay fixed while the trainer is running.

> **⚠️ Compatibility:** this release targets **Ostriv Alpha 5, Patch 9, Build 60 (`0.5.9.60`)**. Future releases of the trainer will follow the **same version numbering as the game itself** — download the trainer release that matches your installed game version. Using a trainer build against a different game version may not work correctly, since it relies on the game's internal memory layout.

## Features

- **Live building list** — every relevant building in your city, grouped by type, kept in sync automatically as you build and demolish.
- **Type filter** — narrow the list down to a single building type, or show only the types you currently own.
- **Custom names** — rename any building; the name persists across sessions.
- **Camera centering** — jump the in-game camera straight to any building, whether selected from the list or currently selected in-game.
- **Inventory editing** — view and set resource quantities for any building's inventory, including household savings for residential buildings (Apartments, Village houses, Fenceless village houses).
- **Add/remove resources** — introduce a resource type a building's inventory doesn't currently hold, or remove one entirely.
- **"Keep up" locks** — pin a resource (or the family's money, or your global money) to a fixed amount; the trainer will continuously top it back up while it's running.
- **Global money display and editing**, with the same lock behavior.
- **Low overhead** — the trainer only actively polls the handful of buildings you're currently looking at or have locked; it does not scan your whole city every tick, and only repaints an inventory panel when its contents actually changed.

## Installation

1. Download the release `.zip` for your game version and extract it anywhere.
2. **An exception in your antivirus may be needed.** The `.zip` includes the trainer's executable and its dependencies. Memory-reading/writing tools like this one are frequently flagged by antivirus software as a false positive — this is expected behavior for any trainer, not a sign of a problem. If your antivirus quarantines or blocks the `.exe`, add an exception for it.
3. Run Ostriv first, then run the trainer.
4. Click **Connect**.

The trainer needs to read and write the game process's memory to function, so make sure your antivirus isn't silently blocking that access even after adding an exception for the file itself.

## Usage

The interface is largely self-explanatory, but a few things are worth calling out:

### The building list and panels

- The **left panel** shows every building in your city. Select one to see and edit its inventory.
- The **right panel** always reflects whichever building is currently selected **in-game** — no need to find it in the list yourself.
- Click a resource row to load its current amount into the amount box below. Type a new value and press **Set Amount** to apply it once.

### Adding and removing resources

Next to the amount box, each panel has a **+** and a **−** button.

- **+** opens a small window with a dropdown of every known resource and an amount box. Pick a resource, enter a starting amount, and press **Add** to introduce it into that building's inventory — including a building whose inventory is currently completely empty.
- **−** removes whichever resource row is currently selected in the list, clearing it out of the building's inventory entirely. Any active "Keep up" lock on that resource is removed along with it, so it doesn't linger uselessly in the JSON file.
- Adding always reuses an empty slot inside the building's inventory rather than growing it, so this never bloats the save.

### "Keep up" locks

Each inventory row has a checkbox in the **Keep up** column, with a hint next to it: *"(Click to set amount to keep)"*.

- **Checking the box alone does nothing yet.** It only takes effect once you also press **Set Amount** — this is intentional, so you can't accidentally lock a value while adjusting the amount box.
- Once locked, the trainer will keep re-applying that amount to the resource for as long as the trainer is running and connected — even if the game consumes, produces, or otherwise changes it in between.
- **Unchecking the box unlocks it immediately** — no need to press Set Amount again. The resource is simply left as-is at whatever value it currently holds.
- The same locking behavior applies to a residential building's **Family Money** row, and to the **global money** field at the top of the window (via its own **Lock** checkbox next to "New Money").

### The JSON file

The trainer automatically creates and maintains an `ostriv_trainer.json` file next to the `.exe`.

- **Do not delete this file** unless you want to lose all custom names and locks you've set. It's what makes those persist across sessions.
- It stores:
  - A permanent registry of every building the trainer has ever seen, keyed by a stable per-building identifier — so a building keeps its custom name and identity even if it becomes temporarily inactive or you reconnect later.
  - Any **custom name** you've assigned to a building.
  - Any active **resource/money locks** and the amount each is locked to.
  - Your **"show only buildings I have"** filter preference and **global money lock** state, so they're remembered the next time you launch the trainer.
- The file is a plain, human-readable JSON file — feel free to open it in a text editor to inspect it, but avoid hand-editing it while the trainer is running.

## A note on save games

The building registry in `ostriv_trainer.json` is **not tied to a single save file or city**. If you load a different save (or even a different game entirely, since the game isn't fingerprinted), the trainer will simply treat any buildings it hasn't seen before as new and add them to the registry — old entries are never deleted automatically. Over a long time and many different saves, the file can grow, but this is expected and harmless.

## Disclaimer

This is a fan-made, third-party tool that reads and writes the Ostriv process's memory. It is not affiliated with or endorsed by the developers of Ostriv. Use it at your own risk, and consider keeping a backup of your save files before making sweeping changes to a city you care about.
