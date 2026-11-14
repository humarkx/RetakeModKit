# Compatibility and troubleshooting

Keep the Mod Kit and Retake up to date. Setup accepts UE6 source builds and installed engines without requiring a particular revision or a clean Git checkout. `ModKit/Engine.json` records the required major version. SDK compilation, asset loading and cooked-content compatibility are still checked separately.

Open `RetakeModKit.uproject` directly, then use **Retake → Mod Kit Workspace**. **Enable authoring tools** prepares dependencies and engine records using the engine's bundled Python. The editor workflow does not need a terminal or a separate Python installation.

The GitHub kit includes five prebuilt editor/runtime DLLs and their original Unreal module manifests. They load when the engine build identity matches. The kit includes no Unreal Engine installation or Retake game executable.

## Updating from SDK40

The SDK62 kit targets the newer game contract, whose oldest readable SDK is 43. Existing SDK40 cooked exports need to be re-exported from their editable sources using the updated kit. Keep the original workspace and its recovery archive before upgrading. After updating the project and compiling for your engine if needed, use **Enable authoring tools** to refresh the SDK checks and engine records, then check and prepare the saved content before exporting a new revision.

Use the kit with the matching game release named by its metadata. Development-candidate game bindings produce local test exports; they do not establish compatibility with the currently released game. SDK62 also supports standalone map-cover files for server browsers and Call Vote previews.

## Opening with another UE6 build

If opening the project reports missing or incompatible modules, your engine needs its own compiled copy of the kit modules. With the Visual Studio C++ tools supported by that engine installed, use Unreal's rebuild prompt. If compilation fails, report the error and build log. The bundled DLLs cannot be made compatible by editing their `.modules` BuildId.

`Setup.ps1` and `Open-ModKit.ps1` remain advanced repair tools. They are not prerequisites for opening a matching prebuilt project. For that optional command-line route, install Python 3.11+ and the engine's C++ build tools, run `Setup.ps1 -EngineRoot '<engine path>'`, then `Open-ModKit.ps1`. Finish building or repairing the engine's editor, ShaderCompileWorker and UnrealPak if they are missing.

Compatibility checks cover cooked serialization, collision, meshes, destruction, Niagara and shaders. SDK materials reduce shader dependencies but do not exempt a map from the other checks. Keep these checks enabled when creating and sharing mods.

Compatible game updates should continue accepting existing exports. Changes to native game logic do not inherently require mod updates. Breaking SDK/default/resource/body/cooked-data changes need explicit compatibility decisions. Clients and server still require identical content revisions, and replays retain their exact dependencies.

| Problem | Action |
|---|---|
| Unreal asks to build because `RetakeModKitEditor.target` is missing | Update the complete prebuilt kit; it includes the original editor target receipt alongside the DLLs. A matching engine does not need a build just to recreate this file. Different engine builds still need matching compiled modules. |
| **Edit → Plugins** is missing | Update the project and restart the editor. The kit enables Unreal's normal default plugins and explicitly enables `PluginBrowser`. If preserving a locally edited `.uproject`, set `DisableEnginePluginsByDefault` to `false` and add `{"Name":"PluginBrowser","Enabled":true}` to its `Plugins` array. Keep the local engine association. |
| Example fails with `Could not load Python file` and a broken `retake_*.py` path | Update the kit for the Windows script-path fix. If the failed attempt already created `Mods/RetakeMod_Example`, close the editor and move that unfinished folder to a backup outside `Mods`, then reopen and click **Example** again. Keep the backup if it contains any edits. |
| Project generation fails with `JsonReaderException: '0xFF'` | Open `RetakeModKit.uproject` in Notepad, replace any typographic dash in its Description with a normal `-`, and use **Save As → UTF-8**, keeping the `.uproject` filename. Preserve your EngineAssociation, then retry **Generate Visual Studio project files**. Affected UE6 version selectors can save non-ASCII project descriptions as UTF-16, which UBT rejects. The current kit description avoids this issue. |
| Unsupported engine major version | Select a UE6 installation when opening the project. |
| Prebuilt DLLs target another engine | Rerun Setup with your engine. It will build the included source; install the C++ toolchain required by that engine. |
| Missing modules / game module could not be found | Finish Setup successfully, then use `Open-ModKit.ps1`. If setup fails, include its console error and `Saved/ModKit/Logs/build.log` when reporting it. |
| SDK compile or contract failure on a UE6 build | Report the build log or `Saved/ModKit/contract-report.json`; accepting the engine does not establish SDK or content compatibility. |
| Authoring tools unavailable | Click **Enable authoring tools** or **Tool setup** in the workspace. Check **Activity & details** if setup fails. |
| Missing editor/compiler | Install or repair your UE6 engine; another engine build may need its C++ toolchain to rebuild the kit. |
| Missing binary assets / LFS pointers | Make a fresh clone or extract the complete kit ZIP. Assets are stored directly in Git; Git LFS is not required. |
| Missing or modified SDK contract | Restore this kit release. Do not edit snapshots to silence the check. |
| No matching platform compatibility | Update the kit and game, then rerun Setup. If the mismatch remains, report the diagnostic; do not bypass it. |
| Stale preparation | Save your inputs, run the relevant preparation action, then export a new revision. |
| Missing base runtime resource | Use approved shared SDK resources or mod-owned assets; do not copy private game content. |
| Editor moved | Rerun Setup with the new engine path. |

Logs are under `Saved/ModKit/Logs`. When reporting a problem, include the relevant diagnostic and setup information; omit credentials and personal paths where possible.

## Local files and updates

Opening the editor creates caches and logs under `Saved`, `Intermediate` and `DerivedDataCache`; these are already ignored. The kit also ignores your `Mods` workspaces, exports, recovery archives and local Python/IDE environments. Keep mod work in its own repository or backup.

Ignore rules apply to untracked files. They do not hide edits to tracked `.uproject`, config, DLL or target-receipt files. Changing the engine association, enabling plugins, editing project settings or rebuilding native modules can therefore leave local changes that need review before pulling an update. Close the editor first so its loaded DLLs can be replaced.

Unreal's Android File Server plugin generates local connection settings in `Config/DefaultEngine.ini` on first launch. Keep those generated settings local. If they are the only changes in that file, restoring the repository version before updating is safe; Unreal regenerates them on a later launch. Preserve any intentional config edits when merging an update. Do not ignore the entire Config folder or use a whole-workspace reset to clear one generated change.
