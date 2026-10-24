# Pinol · Bitfocus Companion

A Companion **connection module** for Stream Deck show control: GO/BACK,
sequence and cue selection, snapshot/preset recalls, timeline transport,
live elapsed/remaining time and phase-colored progress bars.

Requires **Companion 4.2 or newer**, supporting module API 1.14 and Node 22,
and a Pinol build with the show-control OSC API. It runs independently of
Open Stage Control. See [installation and operating help](companion/HELP.md)
or the [illustrated layout guide](../../docs/companion.md).

GitHub CI validates the manifest, runs protocol/preset tests, exercises the
real Companion SDK over IPC, builds an importable `.tgz`, and repeats the
runtime scenario against the packaged entrypoint without external dependencies.
Download **Pinol-Companion** from the
[module workflow](https://github.com/PixelStereo/Pinol/actions/workflows/companion.yml).

For development, use Node 22.18+:

```sh
cd integrations/companion
npm install --ignore-scripts --omit=optional
npm run check
npm run package
```

The source and packaged module are MIT licensed. The SDK packaging tool includes
dependency notices and licenses. This is a custom module distributed with Pinol;
it is not automatically listed in Bitfocus's online module catalog.

Implementation follows the official [module API](https://bitfocus.github.io/companion-module-base/)
and [API 1.x preset format](https://companion.free/for-developers/module-development/connection-basics/presets-1.x/).
