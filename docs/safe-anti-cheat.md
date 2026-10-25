# Safe Anti-Cheat

Safe Anti-Cheat is on for every game unless the game's own file, `configs/<program>.conf`, says
`safeAntiCheat = false`. While it is on, no effect gets the depth buffer: depth capture stays off, and an
effect that reads depth is left out of the chain and marked, until you turn it off.

A profile holds no Safe Anti-Cheat, so picking another profile never turns it off. A game whose folder
ships an anti-cheat, one of those SteamDB's file rules name, keeps it locked on, and the tile says which
file. VAC ships no file and is not seen.

## What it does not do

It does not hide the layer. An anti-cheat can see an injected Vulkan layer however this is set.
vkDaedalus only post-processes the finished frame, like NVIDIA's or AMD's filters: it reads no game
state and touches no game files or network traffic. Whether a game allows that is the game's choice, and
the game can change it. No guarantee is possible; the original vkBasalt FAQ put it as "Maybe. To my
knowledge this hasn't happened yet."
