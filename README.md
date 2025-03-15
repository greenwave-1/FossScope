# This is an archive of the last commit before moving to drawing text directly on the framebuffer

# FossScope (working title)

An open source alternative to SmashScope.

Currently not feature-complete, but in a workable state.

## Current features:
- Polls at ~1400 hz in the oscilloscope menu, ~120 hz otherwise
- Draws waveform over ~2 seconds or when stick stops moving (whichever comes first, depending on mode)
- Input viewer/button tester with melee coordinates
- 2D stick plot with stickplot maps
- Basic Coordinate Viewer with coordinate tests
- Works on GameCube/Wii, and with 480p

## Current issues:
- Polling rate is not perfectly uniform. The min time for polls at ~1400hz is around 600 microseconds,
where as the normal and max are around 700 microseconds
- Measuring the stick requires the user to press A first, investigate how to change this without losing current polling 
method
- Waveform doesn't show C-Stick (yet)
- No way to currently have a continuous waveform, though I have some ideas
- No way to scale the waveform (yet)
- Dashback logic is implemented, but I am unsure of their correctness. The previous pivot logic has been confirmed to
be wrong, so its been disabled for now
- Coordinate viewer is incomplete, I want to draw on the screen where the tested coordinate is visually, as well as
adding more coordinates for testing
- All drawing is done with printf and modifying the framebuffer directly, investigate if that can be changed
- A lot of logic that shouldn't be in menu.c is, needs to be moved to other files (mostly drawing functions)
- Other things I can't think of right now...

## Building:
- Install devkitpro
  - [Follow the instructions for your device from here](https://devkitpro.org/wiki/devkitPro_pacman)
- Install libogc2
  - [Add the extremscorner repo](https://github.com/extremscorner/pacman-packages#readme)
- Run ```sudo (dkp-)pacman -S libogc2 libogc2-libdvm-git```
  - The ```(dkp-)``` is for systems that don't normally have pacman. On those systems, any command should use ```dkp-pacman```
  - Systems that normally use pacman should run ```pacman```
- Run ```make``` in the root of the project

## Why?
When I started making this I didn't realize that an updated SmashScope was supposedly in the works, so I guess this was
more of an experiment for myself. I'll keep updating it if interest is there but if a second SmashScope is incoming
this is probably useless.

## Thanks to:
- The PhobGCC team (especially for phobconfigtool and PhobVision, this wouldn't have been done without referencing
it repeatedly)
- SmashScope and its creators for giving me a baseline to strive for
- Z. B. Wells for being my "Archaic Language Consultant"
- The DevkitPro team and Extrems for making stuff like this a lot more accessible
