# Make a map

Open **Retake → Mod Kit Workspace**, click **+ New mod**, select **Map**, and enter a name. If the workspace asks you to enable authoring tools, use its **Enable authoring tools** button.

**Empty** is the default and contains no demonstrations. Choose **Basic** for a simple starting area with geometry, lighting and map setup. An unfinished Empty map is editable but is not yet exportable.

Use **Example** in the map workspace to open or create the separate original SDK demonstration. Create your own mod before adapting its content.

The example generator refuses an existing or edited workspace. It demonstrates supported mode starts/objectives, doors, ladders, destruction, traversal, surfaces, ambience, acoustic regions and map presentation. Its geometry, generated audio and art are independent of the weapon example. It is not placed in the Empty or Basic template.

Minefields remain available in the palette but are not preplaced in this example: they require a creator-owned explosion effect, spatial cues and a generated mine layout. The validator reports missing resources before export.

Place SDK elements using the Retake palette, configure the supported modes and keep all imported assets under your mod's content root. Configure map-owned sounds through the supported ambience/interaction definitions. Reference the shared SDK material where appropriate; custom materials are supported only by qualified shader combinations.

## Map cover images

Import a loading poster and a vote thumbnail into your mod. On the map's **Retake Map Settings** actor, open **Presentation** and assign both textures.

| Field | Used in | Recommended size | Maximum per side |
| --- | --- | --- | --- |
| **Loading Art** | Full-screen map loading | 1920 × 1080 | 2048 px |
| **Preview** | Map lists and callvote | 1024 × 576 | 1024 px |

Use 16:9 images and keep the main landmark away from the edges, where cards can crop it. In each Texture asset, set **Compression Settings** to **UserInterface2D**, **Texture Group** to **UI**, **Mip Gen Settings** to **NoMipmaps**, and enable **Never Stream** and **sRGB**. The game displays the map name and mode over the image.

Both textures are required by the map check and export. The export includes the referenced textures and generates a small image from **Preview** so Call Vote can show it before players download the full map. List editable image sources in `SourceFiles.json` if you want them included in your recovery archive.

## Prepare your map

Click **Save mod content**, then **Check map**. Review the listed issues and click **Prepare map data** when preparation is needed. When the checks pass, choose a revision in **Export your mod** and click **Export mod**.

The preparation workflow reports missing/outdated geometry and required acoustics. Configure and bake visibility and minimaps using their respective actions when used. Optional data must be explicitly absent or current; stale data cannot be silently accepted. Server visibility policy may impose additional requirements.

Preview in the editor is an authoring view, not Retake gameplay. Test actual interaction, movement, objectives, multiplayer and round reset using an installed compatible game after export.
