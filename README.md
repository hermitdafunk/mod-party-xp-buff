# mod-party-xp-buff

**"It's Party Time!"**

A server-side [AzerothCore](https://github.com/azerothcore/azerothcore-wotlk) module for World of Warcraft: Wrath of the Lich King that provides an optional bonus to experience earned while adventuring in a party.

The module is designed for servers where the default party experience scaling makes leveling in small groups—particularly groups of 3–5 players—feel significantly slower than solo leveling.

Party members can enable the bonus through the party leader using:

```text
.partytime on
.partytime off
.partytime status
```

No client patch is required, and no visible spell aura is used.

---

## Features

* Server-side only
* No client modification required
* Configurable experience bonus
* Configurable minimum party size
* Bonus is enabled/disabled per party
* Only the party leader can toggle the bonus
* Automatically stops applying when the party falls below the configured minimum size
* Automatically clears when the party is disbanded
* Optional support for bot characters
* Optional party announcements
* Configuration reload support
* Protection against invalid/unsafe bonus percentages

---

## How It Works

The module tracks which parties have activated **Party Time**.

When a player receives experience, the module checks:

1. Is the module enabled?
2. Is the player eligible?
3. Is the player in a party?
4. Does the party meet the minimum size?
5. Has the party leader enabled Party Time?
6. If bot support is disabled, is the player a bot?

If all conditions are met, the configured bonus is applied to the XP amount.

For example, with:

```ini
PartyXPBuff.BonusPercent = 100
```

a player who would normally receive:

```text
100 XP
```

will receive:

```text
200 XP
```

The bonus is an **additional percentage**, not the final XP multiplier.

| BonusPercent |    Result |
| -----------: | --------: |
|            0 | Normal XP |
|           25 |  1.25× XP |
|           50 |  1.50× XP |
|          100 |  2.00× XP |
|          200 |  3.00× XP |

---

## Commands

### `.partytime on`

Enables Party Time for the current party.

Only the party leader can use this command.

Example:

```text
.partytime on
```

The party will be notified that the experience bonus has been enabled.

---

### `.partytime off`

Disables Party Time for the current party.

```text
.partytime off
```

Only the party leader can disable the bonus.

---

### `.partytime status`

Displays the current Party Time state and configuration.

```text
.partytime status
```

Example:

```text
Party Time is ON (+100% XP, needs 3+ members).
```

If the party is currently below the minimum size:

```text
Party Time is ON (+100% XP, needs 3+ members).
Party Time is currently PAUSED because the group has 2 member(s).
```

---

## Configuration

The module provides the following configuration options:

```ini
###############################################
# mod-party-xp-buff
###############################################

# Enable the Party XP Buff module.
PartyXPBuff.Enable = 1

# Additional XP percentage.
#
# 100 = +100% XP (2x normal XP)
# 50  = +50% XP
# 200 = +200% XP (3x normal XP)
PartyXPBuff.BonusPercent = 100

# Minimum number of party members required.
PartyXPBuff.MinGroupSize = 3

# Apply the bonus to bot characters.
PartyXPBuff.ApplyToBots = 1

# Announce Party Time state changes to the party.
PartyXPBuff.Announce = 1
```

### Configuration Options

| Option                     | Default | Description                                |
| -------------------------- | ------: | ------------------------------------------ |
| `PartyXPBuff.Enable`       |     `1` | Enables or disables the module             |
| `PartyXPBuff.BonusPercent` |   `100` | Additional XP percentage                   |
| `PartyXPBuff.MinGroupSize` |     `2` | Minimum party size required                |
| `PartyXPBuff.ApplyToBots`  |     `1` | Allows bot characters to receive the bonus |
| `PartyXPBuff.Announce`     |     `1` | Enables party notifications                |

The module currently limits `BonusPercent` to a maximum of **1000%**.

`MinGroupSize` cannot be configured below 2.

---

## Installation

### 1. Clone or copy the module

Place the module inside your AzerothCore `modules` directory:

```text
azerothcore/
└── modules/
    └── mod-party-xp-buff/
```

The expected structure is:

```text
mod-party-xp-buff/
├── CMakeLists.txt
├── README.md
├── conf/
│   └── mod-party-xp-buff.conf.dist
└── src/
    └── mod-party-xp-buff.cpp
```

### 2. Configure AzerothCore

The module uses the standard AzerothCore module CMake integration.

Build AzerothCore normally after adding the module.

For example:

```bash
cd /path/to/azerothcore
mkdir -p build
cd build
cmake ..
make -j$(nproc)
```

Use the normal build procedure for your existing AzerothCore installation if it is already configured.

### 3. Install the configuration

Copy the distributed configuration file into your server's configuration directory.

For example:

```bash
cp modules/mod-party-xp-buff/conf/mod-party-xp-buff.conf.dist \
   env/dist/etc/modules/mod-party-xp-buff.conf
```

The exact configuration path may differ depending on your AzerothCore installation.

### 4. Configure the module

Edit:

```text
mod-party-xp-buff.conf
```

and adjust the settings to suit your server.

---

## Recommended Configuration

For a server primarily trying to make small-group leveling more comparable to solo leveling:

```ini
PartyXPBuff.Enable = 1
PartyXPBuff.BonusPercent = 100
PartyXPBuff.MinGroupSize = 3
PartyXPBuff.ApplyToBots = 1
PartyXPBuff.Announce = 1
```

This means:

```text
1 player       → normal XP
2 players      → normal XP
3 players      → +100% XP
4 players      → +100% XP
5 players      → +100% XP
```

The party must explicitly enable Party Time with:

```text
.partytime on
```

---

## Party Size Behaviour

Party Time remains enabled for the party even if the party temporarily falls below the configured minimum size.

For example, with:

```ini
PartyXPBuff.MinGroupSize = 3
```

the following can occur:

```text
5 members → Party Time ON
      ↓
4 members → Bonus active
      ↓
3 members → Bonus active
      ↓
2 members → Bonus paused
      ↓
3 members → Bonus active again
```

The party leader does not need to repeatedly toggle Party Time when members join or leave.

If the party is disbanded, its Party Time state is removed.

---

## Bots

Bot support is controlled by:

```ini
PartyXPBuff.ApplyToBots = 1
```

When enabled, bot characters receive the Party Time XP bonus when they otherwise qualify.

When disabled, bot characters do not receive the bonus.

This setting does not determine whether bots count toward the minimum party size. The minimum size is based on the group's member count.

---

## Important Behaviour / Limitations

### XP Award Hook

The module currently applies its bonus through AzerothCore's player XP award hook.

This means Party Time is an **additional XP multiplier applied when XP is awarded to an eligible grouped player**.

It is not currently a replacement for, or modification of, AzerothCore's underlying party XP formula.

Consequently, this module should be considered an additional party XP bonus rather than a complete reimplementation of group XP scaling.

### No Client Changes

The module does not require:

* Modified client files
* Custom MPQ files
* Client-side addons
* Custom spells
* Custom auras

The feature is entirely server-side.

---

## Example

A three-player party activates Party Time:

```text
.partytime on
```

Assuming:

```ini
PartyXPBuff.BonusPercent = 100
```

and a player would normally receive:

```text
75 XP
```

the module changes the awarded amount to:

```text
150 XP
```

The normal AzerothCore XP calculation still occurs first; Party Time adds the configured bonus afterward.

---

## Building

The module is intended to be built as part of an AzerothCore source tree.

The module's `CMakeLists.txt` registers:

```cmake
AC_ADD_SCRIPT("${CMAKE_CURRENT_LIST_DIR}/src/mod-party-xp-buff.cpp")
AC_ADD_SCRIPT_LOADER(
    "PartyXPBuff"
    "${CMAKE_CURRENT_LIST_DIR}/src/mod-party-xp-buff.cpp"
)
```

The source file contains the module registration function:

```cpp
void Addmod_party_xp_buffScripts()
{
    new PartyXPBuffWorld();
    new PartyXPBuffPlayer();
    new PartyXPBuffGroup();
    new PartyXPBuffCommand();
}
```

---

## Compatibility

This module is intended for:

* AzerothCore
* World of Warcraft: Wrath of the Lich King
* 3.3.5a-era server cores

Compatibility with specific AzerothCore commits or branches may vary as the core scripting API changes.

---

## Development Status

**Status: Experimental / Development**

The module was created to address the experience penalty perceived when leveling in small parties.

The current implementation deliberately keeps the bonus simple and predictable.

Future development may investigate modifying AzerothCore's underlying group XP calculation rather than applying an additional multiplier after XP has been calculated.

Possible future features include:

* Party-size-specific XP bonuses
* Automatic party XP scaling
* Configurable bonuses for 2, 3, 4 and 5-player groups
* More granular XP source filtering
* Better handling of party/raid distinctions
* Optional visual or chat feedback
* Additional bot-specific behaviour

---

## Contributing

Issues, testing results, suggestions and pull requests are welcome.

If reporting a problem, please include:

* AzerothCore commit/version
* Operating system
* Module version/commit
* Relevant configuration
* Party size
* Whether bots were present
* Expected XP
* Actual XP
* Relevant server log output

---

## License

This module is intended to be distributed under the same license terms as the surrounding AzerothCore module ecosystem.

See the project's license and AzerothCore's licensing information before redistributing modified versions.

---

## Credits

Created for AzerothCore WotLK servers looking for a simple way to make small-group leveling more rewarding.

**It's Party Time!**
