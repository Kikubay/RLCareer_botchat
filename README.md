# RLCareer_botchat

**Give the bots a voice.** A BakkesMod plugin that makes bots use Rocket League's real quick chats in offline matches — with the same text, `[Team]` tags and voice lines you hear from a human player.

![BakkesMod](https://img.shields.io/badge/platform-BakkesMod-blue) ![Version](https://img.shields.io/badge/version-2.7-green) ![Build](https://img.shields.io/badge/build-VS%202022-lightgrey)

---

In a career match the bots play well but stay silent. This changes that:

- **Tactical calls picked from the situation**, not a dice roll. The bot closest to the ball calls *`I got it!`*, a beaten bot yields with *`All yours.`*, a defender says *`Defending...`*, a last man facing a shot warns *`Incoming!`*
- **Real quick chats**, sent through the game's own chat system. Nothing is typed, no keys are pressed, the local player never talks.
- **Conversations.** Bots answer each other — *`Sorry!`* → *`No problem.`*, *`Thanks!`* → *`No problem.`*, *`I got it!`* → *`In position.`* — and they answer **your** quick chats too.
- **Reactions to the match.** Goals, saves, demos and centers get a rotating compliment or apology. At the final whistle the teams exchange *`gg`* / *`Well played.`*
- **Quiet when it should be.** No chatter over goal replays or the podium, and boost calls are rate-limited so they never become spam.
- **Fully configurable** in BakkesMod. No console commands needed.

## Example exchanges

| Situation | What you hear |
|---|---|
| Kickoff | *I got it!* → *Good luck!* |
| Teammate runs dry | *Need Boost!* → *I got it!* |
| You score | a bot on your team: *Nice shot!* → you: *Thanks!* → bot: *We got this.* |
| Bot concedes | *My bad...* → *No problem.* |
| Bot gets demolished | *$#@%!* → *Oops!* |
| Ball flying at your goal | *Incoming!* → *Defending...* |
| Final whistle | *gg* → *Well played.* → *That was fun!* |

## Install

1. Download the *ZIP* file from the **[Releases](https://github.com/Kikubay/RLCareer_botchat/releases/latest)** page and extract it.
2. Close Rocket League.
3. Run `install.bat`. It checks that BakkesMod is installed, copies `botchat.dll`, adds `plugin load botchat` to `plugins.cfg`, and reports each step.
4. Start Rocket League and play a career match with bots.

Manual install: copy `botchat.dll` to `%APPDATA%\bakkesmod\bakkesmod\plugins\` and append this to `%APPDATA%\bakkesmod\bakkesmod\cfg\plugins.cfg`:

```text
plugin load botchat
```

## Settings

Press **F2 → Plugins → Botchat**. Every option is a checkbox or a slider, applies immediately, and is saved automatically.

| Section | Settings |
|---|---|
| **General** | Enable bot chatter, Overall chat chance, Situation-aware lines, Tactical alerts, Bots answer bots, Answer your chats, Event reactions, End-of-match exchange, Quiet in replays and podium, Echo in chat box |
| **Timing** | Repeat delay, Reply delay, Idle interval, Idle chance, Per-bot cooldown, Gap between chats, Boost-call gap |
| **Boost and chat channels** | Low boost below, Tactical as team chat, Allow match chat, Allow team chat |

**Too chatty or too quiet?** Move **Overall chat chance**. `15` is a quiet match, `60` is lively, `100` removes the throttle. It gates every message, including replies and the post-match exchange.

These are ordinary saved cvars (`botchat_enabled`, `botchat_rate`, `botchat_smart`, ...), so the `F6` console still works. If the tab does not appear after an upgrade, run `cl_settings_refreshplugins` in the console or restart Rocket League.

If you don't see chats in your offline matches, enable **Echo in chat box** to print them through BakkesMod's own chat box as a fallback.

## How it works

Rocket League quick chats are preset messages: a key like `Group1Message7` (the text *"All yours."*) sent with `bPreset = 1`. The client turns that key into the localized text, the `[Team]` tag and the voice line.

The plugin calls `PlayerController_TA.ChatMessage_TA` on the local controller with a bot's PRI as the sender and `bPreset = 1` — the same call the game makes when a chat arrives over the network.

Every second it reads the ball, each bot's car, speed, boost and team, then scores the lines that fit. The best-fitting line wins, with a small jitter to break ties and a repeat penalty per bot. Boost calls, replies, events and the post-match exchange have their own limits. Only offline matches are touched; bots are found via `PRI.bBot`, and the local player and spectators are skipped.

## Build from source

[`botchat.cpp`](botchat.cpp) is the whole plugin. [`build.bat`](build.bat) builds it with Visual Studio 2022 and the BakkesMod SDK, then installs it. It also compiles the SDK's `imgui.cpp`, `imgui_draw.cpp` and `imgui_widgets.cpp` into the DLL. BakkesMod does not export those symbols, so they must be compiled in for the settings tab. Edit the Visual Studio path in the batch file if yours differs.

## Compatibility and limits

- Requires BakkesMod and Rocket League on Windows. Offline matches only; nothing is sent online.
- Tested by building against the BakkesMod SDK with VS 2022. Not verified on every setup or game version.
- The plugin sees what a spectator sees (ball, cars, speed, boost, stat events), not the AI's intentions. *Faking.* is inferred from ball height, opponent distance and the bot's velocity.
- Team sides assume the standard field layout (blue defends `−X`, orange `+X`).
- Text and voice come from the game's localized assets; the log and `botchat_echo` show the English text.
- The stat-event hook relies on the `HandleStatTickerMessage` parameter layout used by BakkesMod plugins; a game update could change it.

## Credits

Quick-chat keys and texts follow the mapping used by [BetterChat](https://github.com/JulienML/BetterChat) and [CustomQuickchat](https://github.com/smallest-cock/CustomQuickchat), which credit the game's own chat data.
