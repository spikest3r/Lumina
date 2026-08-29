# Lumina

Lumina is a visual game creation environment for building 3D games and interactive projects. Create games using either visual block-based programming or by writing Lumen code directly—choose the approach that works best for your project.

## Key Features

- **Two creation approaches:** Use an intuitive visual block editor, write Lumen code directly, or switch between them in your project
- **Visual block programming:** Drag-and-drop blocks for movement, logic, variables, math, physics, and interactions
- **3D grid-based world:** Build levels on a voxel-like grid with full 3D positioning and rotation
- **Built-in objects:** Pre-made interactive objects and tiles with physics support
- **Real-time editing:** See your changes instantly as you build
- **Quick launch:** Re-open your last project with `lumina --last`
- **Examples included:** Learn from packaged templates and example projects

## Build Requirements

- **CMake** ≥ 3.16
- **Vulkan SDK** (for graphics)
- **GLFW3** (window library)
- **C++20** compiler
- **Linux or macOS**

### System Dependencies (Ubuntu/Debian)

```bash
sudo apt-get install cmake vulkan-tools libvulkan-dev glfw3-dev
```

### System Dependencies (macOS)

```bash
brew install cmake vulkan-headers glfw3
```

## Building

```bash
cd lumencreator
mkdir -p build
cd build
cmake ..
make
```

The compiled executable will be at `build/LumenCreator`.

## Running

Launch Lumina:

```bash
./build/LumenCreator
```

Resume your last project:

```bash
./build/LumenCreator --last
```

## Getting Started

Alternatively, you can download Lumina on [SourceForge](https://sourceforge.net/projects/lumina-editor/) or from AUR on Arch Linux.

On Arch Linux `yay -S lumina-editor-git`.

When you launch Lumina, you'll see the main menu where you can:

- **Create a new project** – Start with a blank canvas
- **Open an example** – Learn from included templates and games
- **Load an existing project** – Continue working on saved projects

### The Editor

Once in a project, you'll find:

- **Viewport** – Your 3D level, navigated with mouse/scroll
- **Toolbar** – Quick access to brushes and object tools
- **Block editor or code editor** – Switch in settings (press F12)
- **Properties panel** – Adjust object properties
- **File menu** – Save, load, and export

### Building Your World

1. Use the **brush tool** to paint static tiles (walls, platforms, decorations)
2. Add **interactive objects** from the object menu to place game elements
3. Click an interactive object to edit its logic in the block editor or code editor
4. Press **F5** to test your game

## Examples & Templates

Lumina includes several ready-to-play examples in the `examples/` directory:

- **parkour.lumina** – A platformer with parkour mechanics
- **collector.lumina** – A collection-based gameplay example
- **template_movement.lumina** – Starter template for movement controls
- **template_camera_movement.lumina** – Starter template for camera-affected controls

Load any example from within the editor to explore how it works.

## Visual Block Programming

The block editor provides blocks for:

- **Movement:** Move forward, go to position, set position, set rotation
- **Physics:** Upward force, check if grounded, apply gravity
- **Control:** If/else, loops (forever, repeat, while)
- **Variables:** Create local and global variables, modify them
- **Logic:** Compare values, boolean operations
- **Interaction:** Check key presses, collision detection, ask/say text
- **Camera:** Move camera relative to player, rotate with camera
- **Procedures:** Create reusable routines
- **Comments:** Document your logic

Blocks are type-aware: slot connections validate that the right data types fit together.

## Lumen Code

Alternatively, write code directly using the LumenLang, which compiles to bytecode executed by the built-in virtual machine. The language supports variables, functions, loops, conditionals, and calls to engine functions for movement, physics, input, and more.

## File Format

Projects are saved as `.lumina` binary files, which store:

- Level layout (tiles and their properties)
- Interactive objects and their positions
- Code (either block data or source code)
- Textures and object configurations

## Project Structure

```
lumencreator/
├── src/                          # Source code
│   ├── main.cpp                  # Entry point
│   ├── freeplay.cpp/h            # Main editor scene
│   ├── blockeditor/              # Visual block programming
│   ├── lumen-src/                # Lumen compiler and VM
│   ├── mainmenu/                 # Main menu UI
│   └── ...                       # Other systems
├── include/                      # Engine headers
├── assets/                       # Models, textures, sounds
├── examples/                     # Example projects
├── shaders/                      # Vulkan shaders
├── CMakeLists.txt                # Build configuration
└── LICENSE                       # GPLv3
```

## Technology Stack

- **Graphics:** Vulkan (via VulkanEngine)
- **Window Management:** GLFW3
- **Language:** C++20
- **Build System:** CMake
- **UI:** ImGui

## License

Lumina is released under the **GNU General Public License v3.0**. See the LICENSE file for details.

## Related Projects

- **[LumenLang](https://github.com/spikest3r/LumenLang)** – The Lumen programming language
- **[VulkanEngine](https://github.com/spikest3r/VulkanEngine)** – The rendering engine

## Contributing

Lumina is in active development. If you'd like to contribute, check the repository for guidelines. For bug reports or feature requests, open an issue on GitHub.
