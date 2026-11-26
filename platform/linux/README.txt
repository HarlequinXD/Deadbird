DEADBIRD for Linux
==================

Needs SDL2 (most desktops already have it):
  Mint / Ubuntu / Debian:  sudo apt install libsdl2-2.0-0
  Fedora:                  sudo dnf install SDL2
  Arch:                    sudo pacman -S sdl2

Run:
  ./deadbird            the game
  ./deadbird-studio     Deadbird Studio (the game and level editor)

(If double-clicking does nothing, right-click > Properties > Permissions > "Allow executing",
 or: chmod +x deadbird deadbird-studio)

Controls: any game controller (Xbox, PlayStation, Switch Pro, Steam Deck...) or keyboard + mouse.
F11 or Alt+Enter: fullscreen.

Your levels are saved in ~/.local/share/deadbird/levels (Studio projects next to them). To keep
them with the game instead (a portable copy, e.g. on a USB stick), make a folder named "levels"
next to the deadbird file.

Build from source:
  sudo apt install build-essential libsdl2-dev
  make -C platform/linux                    (the game)
  make -C platform/linux deadbird-studio    (the editor)
