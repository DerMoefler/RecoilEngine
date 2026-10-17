Ich kann hier keine echte Datei erzeugen oder anhängen. Speichere den folgenden kurzen Inhalt als README.md:

# RL-Startoptionen

Die Optionen werden im `[MODOPTIONS]`-Block des Startscripts angegeben.

## Optionen

### rl_teamX_spawn

Einheiten, die für Team X beim Start gespawnt werden.

```text
rl_team1_spawn=armpw:3|armrock:2;
```

Erzeugt 3 armpw und 2 armrock.

### rl_teamX_allowed

Einheiten, die Team X erzeugen darf.

```text
rl_team1_allowed=armpw|armrock;
```

Fehlt die Option, werden automatisch die Spawn-Einheiten erlaubt.

### rl_spawn_spacing

Abstand zwischen den gespawnten Einheiten:

```text
rl_spawn_spacing=80;
```

Standard: 80, erlaubt: 20 bis 500.

### rl_remove_initial

Umgang mit vorhandenen Startunits:

none: nichts entfernen
commanders: nur Commander entfernen
all: alle ursprünglichen Startunits entfernen

```text
rl_remove_initial=all;
```

### rl_remove_decorations

Decorations entfernen:

0: behalten
1: entfernen
rl_remove_decorations=0;

### rl_enforce_allowed

Allowlist erzwingen:

0: nicht prüfen
1: unerlaubte neue Units sofort entfernen

```text
rl_enforce_allowed=1;
```

## Teamzuordnung

Die Nicht-Gaia-Teams (Gaia ist das neutrale, von der Engine verwaltete Team in BAR/Recoil) werden nach Engine-TeamID sortiert:

rl_team1_*: erstes Team
rl_team2_*: zweites Team
rl_team3_*: drittes Team

## Beispiel: 3 gegen 10

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
