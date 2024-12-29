# mod-player-controls

**AzerothCore Module:** PlayerControls

This module provides a set of player utility commands, including teleportation, speed adjustments, and more.

## Features

- **Teleport Commands**:
  - `t <location>`: Teleport to a predefined location from the `game_tele` table.
  - `tb`: Teleport back to the most recent saved position.
  - `tb2`, `tb3`, ..., `tb8`: Teleport back to earlier saved positions (up to 8).
  - `tb list`: Display a list of all saved positions with location names.

- **Follow Commands**:
  - `app <playername>`: Teleport to another player's location.

- **Speed Commands**:
  - `s1`: Set player speed to normal (default speed).
  - `s2`: Set player speed to 2x.
  - `s3`: Set player speed to 3x.
  - `s4`: Set player speed to 4x.

- **Help Command**:
  - `pc help`: Display a list of all available commands with descriptions.

## Installation

1. Clone the repository into your AzerothCore `modules` directory:
   ```bash
   cd path/to/azerothcore/modules
   git clone https://github.com/zaynemayfield/mod-player-controls.git
