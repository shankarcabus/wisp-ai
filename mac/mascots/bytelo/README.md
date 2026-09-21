# Bytelo, as sprites for the Mac

**Generated. Do not edit by hand.** Regenerate with:

```bash
cmake --build sim/build -j
WISP_MASCOT=bytelo ./sim/sprites.sh mac/mascots/bytelo
```

## Why these are committed, and the Terminal's are not

The two characters reach the Mac from different places because they are drawn
in different ways.

The **Terminal** is image art to begin with: the board mounts it from the
assets partition, so it already lives in `firmware/assets/` and the installer
copies it from there. Duplicating it here would cost 668KB to say what that
folder already says.

**Bytelo** the board *draws in code* — `firmware/main/mascote_bytelo.c`, with
adornments generated from the ASCII maps in `firmware/props/`. There is no art
file of him anywhere, so there was nothing for the installer to copy and he
never appeared in the panel's character picker for anyone who cloned the
repository. These eight PNGs are the same drawing, exported once from the
simulator, so the Mac can show the character the board shows.

Committing a generated artifact is the same call `firmware/main/mascote_bytelo_props.c`
already makes: the generator needs SDL2 and a built simulator, which is a lot
to ask of an installer, and the output is small and stable.

Stable is measured, not assumed: two consecutive runs of `sim/sprites.sh` are
identical byte for byte. So a diff here means the character actually changed,
and that is worth seeing in review.
