# RL Start Options

The options are specified in the `[MODOPTIONS]` block of the start script.

## Options

### rl_teamX_spawn

Specifies the units spawned for team `X` at the start of the game.

```text
rl_team1_spawn=armpw:3|armrock:2;
```

Spawns 3 armpw units and 2 armrock units.

### rl_teamX_allowed

Specifies which units team X is allowed to create.

```text
rl_team1_allowed=armpw|armrock;
```

If this option is omitted, all units listed in rl_teamX_spawn are automatically allowed.

### rl_spawn_spacing

Specifies the spacing between spawned units:

```text
rl_spawn_spacing=80;
```

Standard: 80, erlaubt: 20 bis 500.

### rl_remove_initial

Specifies how existing starting units are handled:


none: Keep all existing starting units.
commanders: Remove commanders only.
all: Remove all original starting units.

```text
rl_remove_initial=all;
```

### rl_remove_decorations

Specifies whether decoration units are removed:

0: Keep decorations.
1: Remove decorations.
rl_remove_decorations=0;

### rl_enforce_allowed

Specifies whether the allowed-unit list is enforced:

0: Do not check newly created units.
1: Immediately remove newly created units that are not allowed.

```text
rl_enforce_allowed=1;
```

## List of All Available Units

[Units.json](https://github.com/beyond-all-reason/Beyond-All-Reason/blob/master/language/en/units.json)

## Team Assignment

Non-Gaia teams are sorted by their engine team ID.

Gaia is the neutral, engine-controlled team in BAR/Recoil and is not included in the RL team assignment.

rl_team1_*: First non-Gaia team
rl_team2_*: Second non-Gaia team
rl_team3_*: Third non-Gaia team

## Example: 3 vs. 10

[MODOPTIONS]
{
    rl_team1_spawn=armpw:3;
    rl_team1_allowed=armpw;

    rl_team2_spawn=corak:10;
    rl_team2_allowed=corak;

    rl_spawn_spacing=80;
    rl_remove_initial=all;
    rl_remove_decorations=0;
    rl_enforce_allowed=1;
}
