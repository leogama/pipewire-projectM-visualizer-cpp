**`PipeWire ProjectM Visualizer in CPP - README.md`**

# PipeWire projectM Visualizer

A barebones C++ program that captures audio from PipeWire and displays a music visualizer using libprojectM and .milk preset files. This project was an epic journey of debugging and learning!

![Placeholder for a cool screenshot of your visualizer in action!]
(You'll need to take a screenshot/gif and upload it here later)

## Features

*   Captures system audio via PipeWire (attempts to use the default monitor source).
*   Renders visualizations using the libprojectM library.
*   Supports .milk preset files.
*   Basic keyboard controls for preset navigation (Left/Right arrows).
*   (Future/Optional: Simple console-based menu for basic interactions).

## Why?

This project was born out of a desire to have a straightforward, from-scratch visualizer that integrates directly with PipeWire. While other visualizers exist, getting them to reliably work with PipeWire audio sinks/sources can sometimes be challenging. This aims to be a direct implementation.

This project also serves as a learning experience for integrating:
*   PipeWire for audio capture
*   SDL2 for windowing and OpenGL context
*   GLEW for OpenGL extension loading
*   libprojectM (version 4.x C API) for visualization rendering

## Prerequisites

Before you can build and run this project, you'll need the following development libraries installed:

*   **SDL2:** `libsdl2-dev` (Debian/Ubuntu) or `SDL2-devel` (Fedora)
*   **GLEW:** `libglew-dev` (Debian/Ubuntu) or `glew-devel` (Fedora)
*   **PipeWire (>= 0.3):** `libpipewire-0.3-dev` (Debian/Ubuntu) or `pipewire-devel` (Fedora)
*   **libSPA (PipeWire's plugin API):** `libspa-0.2-dev` (Debian/Ubuntu) or `spa-devel` (Fedora) - usually a dependency of `libpipewire-0.3-dev`.
*   **libprojectM (version 4.x):** This project was built against libprojectM 4.x, which provides a C API.
    *   You may need to compile and install it from source. Ensure it's installed to a location your compiler/linker can find (e.g., `/usr/local`).
    *   Crucially, you'll need both `libprojectM-4.so` and `libprojectM-4-playlist.so`.
*   **A C++17 compatible compiler:** (e.g., g++)
*   **pkg-config:** For locating library flags.

**Example Installation (Arch, BTW coming one day but your leet enough to know how to install pre-reqs, obviously, BTW/Bleh Debian/ Fuck Ubuntu):**
```bash
sudo apt update
sudo apt install g++ pkg-config libsdl2-dev libglew-dev libpipewire-0.3-dev libspa-0.2-dev
# For libprojectM, follow its source installation instructions if not available in repos
# (Make sure to install to /usr/local or similar and run sudo ldconfig)
```

## Building

1.  **Clone the repository:**
    ```bash
    git clone https://github.com/Nsomnia/pipewire-projectM-visualizer-cpp.git
    cd pipewire-projectM-visualizer-cpp
    ```

2.  **Compile:**
    ```bash
    g++ -o projectm_pipewire_visualizer projectm_pipewire_visualizer.cpp \
        -I/usr/local/include \
        -L/usr/local/lib \
        $(pkg-config --cflags sdl2 glew) \
        $(pkg-config --cflags libpipewire-0.3) \
        $(pkg-config --libs sdl2 glew) \
        $(pkg-config --libs libpipewire-0.3) \
        -lprojectM-4 \
        -lprojectM-4-playlist \
        -pthread -std=c++17
    ```
    *   Adjust `-I/usr/local/include` and `-L/usr/local/lib` if you installed libprojectM to a different prefix. If you installed from AUR/Chaotic-AUR/extra Arch repos then just remove "local/

## Running

1.  **Ensure libprojectM libraries are findable at runtime:**
    If you installed libprojectM to `/usr/local/lib` (or another non-standard system path), you might need to:
    *   **Option A (Recommended):** Ensure `/usr/local/lib` is in your system's library path cache. Add it to a file in `/etc/ld.so.conf.d/` (Reccomended, and a great learning exercise for link confs) (e.g., `echo "/usr/local/lib" | sudo tee /etc/ld.so.conf.d/usrlocal.conf`) and then run `sudo ldconfig`.
    *   **Option B (Temporary/Session-specific):**
        ```bash
        export LD_LIBRARY_PATH=/usr/local/lib:$LD_LIBRARY_PATH
        ./projectm_pipewire_visualizer
        ```
    *   **Option C (Compile-time):** Compile with an RPATH:
        Add `-Wl,-rpath=/usr/local/lib` to your `g++` command.

2.  **Ensure you have .milk presets (AUR, or gh search repos) son!):**
    The program currently defaults to looking for presets in `/usr/share/projectM/presets/`. Make sure this directory exists and contains `.milk` files, or update the `PRESET_PATH_DIR_STR` constant in the C++ code.
    A large collection of presets is often available with projectM installations or can be found online.

3.  **Run the executable (ensure chmod +x first):**
    ```bash
    ./projectm_pipewire_visualizer
    ```
    You can place it in /usr/local/bin too to be runnable from anywhere, assuming thats in your $PATH

4.  **Play some audio!** The visualizer captures audio from PipeWire's default monitor source.

## Keyboard Controls

*   **Left Arrow:** Previous preset (from playlist).
*   **Right Arrow:** Next preset (from playlist).
*   **ESC or Close Window:** Quit.

## Troubleshooting

*   **"cannot open shared object file: libprojectM-4.so.4" (or similar):** Your system's runtime linker cannot find the libprojectM libraries. See the "Running" section above.
*   **No visualization / "Warning: No presets found":**
    *   Ensure the `PRESET_PATH_DIR_STR` constant in the C++ code points to a valid directory containing `.milk` preset files.
    *   Check console output for messages from libprojectM about preset loading.
*   **No audio reaction:**
    *   Ensure audio is playing through PipeWire from another application.
    *   Check the console for PipeWire connection messages. Tools like `pw-top` or `qpwgraph` can help see if the application is connected to an audio source in PipeWire.
    *   The program attempts to connect to the default "monitor" source. If you have a complex audio setup, you might need to modify the PipeWire connection logic to target a specific node (this is an advanced topic not covered by this barebones example).
*   **Compilation errors:** Double-check that all prerequisite development libraries are installed, and that `pkg-config` is working correctly for them. Ensure paths for libprojectM (if manually installed) are correct in the compile command.

## The Epic Debugging Saga

This project involved a significant amount of iterative debugging, especially around:
*   Correctly identifying and using the libprojectM 4.x C API after initially assuming a C++ API.
*   Ensuring all necessary headers for libprojectM, PipeWire, and their dependencies were included.
*   Correctly forming the compilation and linker command line arguments, especially for libraries installed in `/usr/local/`.
*   Numerous typos and syntax errors that only the patient eye of a compiler (and a helpful user!) could find.
*   Understanding the split nature of `libprojectM-4.so` and `libprojectM-4-playlist.so`.

This README stands as a monument to that glorious struggle!

## Future Ideas (Contributions Welcome!)

*   More robust error handling.
*   A simple, keyboard-navigable, console-based menu for:
    *   Changing preset directory at runtime.
    *   Selecting specific audio devices (more advanced PipeWire).
    *   Displaying current preset name.
*   Visual feedback for menu interactions (even if just text on the SDL window).
*   CMake build system for easier compilation.
*   Ability to cycle through different PipeWire capture sources.

## License

This project itself is under GPL but also the "do whatever the fuck you want with my output tokens" license which is somewhat loose -- else you BETTER CALL SAUL son, but be aware that **libprojectM is licensed under the LGPL v2.1 or later**. If you distribute your compiled binary, you need to comply with the LGPL terms regarding libprojectM.

---


**`LICENSE` (Not for real)**

Go whatever the fuck you want License

Copyright (c) [2025] [Nsomnia]

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to do whatever the fuck you want so long as I am not deemed liable to it in anyway.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.


...Later gators!