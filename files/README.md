# LITE CITY (beta) on LiteEngine

A tiny from-scratch 3D game engine (Win32 + OpenGL, **zero dependencies**) and a
GTA-style open-world sample game that runs on it. Builds to a single `LiteCity.exe`.

## Build
1. Install a compiler (one time): **MSYS2/MinGW-w64** or **Visual Studio Build Tools**.
2. Double-click `build.bat`  ->  produces `LiteCity.exe`. Run it.

Manual MinGW command:
`g++ -std=c++17 -O2 main.cpp -o LiteCity.exe -mwindows -static -lopengl32 -lglu32 -lgdi32 -luser32`

## Controls
| Key | On foot | In car |
|---|---|---|
| W / S | walk forward / back | gas / brake + reverse |
| A / D | turn left / right | steer |
| SHIFT | sprint | - |
| SPACE | shoot | handbrake |
| F | enter nearest car (steal it!) | exit car (below 30 km/h) |
| E | use shop / job / bills / hospital marker | same |
| P / ESC | pause / quit | |

## What is in the game
- 8x8 block city, 250+ buildings with window textures, roads, sidewalks, minimap
- Day/night cycle with fog, sunset colours and headlight beams at night
- Driving physics (arcade), crashes damage the car and you, cars explode
- AI traffic that follows lanes and brakes for obstacles/pedestrians; pedestrians that flee
- Combat with auto-aim assist, tracers, ammo; pedestrians drop cash
- Wanted level (1-5 stars): police chase, ram and shoot you; lose them to cool down
- Economy: supermarket (health), ammo store, delivery jobs, bills office, hospital
- A new $100 bill arrives every 75 s. Let it reach $600 and you are evicted (game over, press R)
- Death = "WASTED", respawn at the hospital with a $150 fee

## Engine layout
- `engine.h` - platform (window, GL context, input, timer), renderer helpers (3D/2D passes,
  boxes, procedural textures, bitmap text, projection). Reusable for any game.
- `main.cpp` - the game: world gen, entities, AI, economy, HUD.

## Roadmap ideas
Audio (waveOut), real car models (OBJ loader), shadows, save/load, radio, more jobs,
interiors, mouse-look camera, then migrate the renderer to OpenGL 3.3 shaders.
