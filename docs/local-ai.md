# Local AI for Ask Bun

Ask Bun understands a fixed set of commands (`volume 40`, `remind me at 3pm to call Sam`, `wifi
off`…). With **Local AI** on, anything it doesn't understand goes to a small language model that
runs on your own PC. It either answers in a few sentences or turns your words into one of Bun's
commands ("it's too loud" becomes `volume down`).

- **Offline.** The model runs on your CPU. No account, no API key, no internet; nothing you type
  leaves your PC.
- **Optional.** It's off by default. Bunbun works the same without it.
- **Not in the repository.** Model files are 0.5–2.5 GB, so they're never part of Bunbun's code or
  downloads. Bunbun itself never fetches one.

## The quick way: the installer

Run `Bunbun-<version>-setup.exe` and tick **Set up Local AI for Ask Bun**. The installer downloads
Qwen2.5 1.5B (about 1.1 GB, checked against its published SHA-256) into
`%LOCALAPPDATA%\Bunbun\models\` and turns Local AI on. That's all: open Ask Bun (Ctrl+Alt+B) and
ask away, or read [Bun as an agent](#bun-as-an-agent). The rest of this page is for the portable zip, other models, or building the helper yourself.

## What you need

| Piece | What it is | Where it goes |
|---|---|---|
| `bunbun-brain.exe` | A small helper that runs the model with [llama.cpp](https://github.com/ggml-org/llama.cpp) (MIT) | Next to `bunbun.exe` |
| A `.gguf` model file | The language model's weights | `%LOCALAPPDATA%\Bunbun\models\` |

About 2 GB of free memory while the model is loaded, and a CPU with AVX2 (most PCs since 2013).

## 1. Get the helper

If your Bunbun folder already has `bunbun-brain.exe`, skip to step 2.

Otherwise build it from source (see [Building](building.md) for the basic setup):

```bash
./scripts/setup.sh --local-ai   # fetches portable CMake, Ninja and llama.cpp into .toolchain/
./build.sh                      # builds llama.cpp once (a minute or two), then build/bunbun-brain.exe
```

A normal `./build.sh` without `--local-ai` skips the helper entirely.

## 2. Download a model

Pick one. Download the single `.gguf` file, not the whole repository.

| Model | Size | Good for | Licence |
|---|---|---|---|
| **Qwen2.5 1.5B Instruct, Q4_K_M** (recommended) | 1.1 GB | Fast, follows Bun's commands well | Apache 2.0 |
| Qwen2.5 0.5B Instruct, Q4_K_M | 0.5 GB | Older or low-memory PCs; simpler answers | Apache 2.0 |
| Llama 3.2 3B Instruct, Q4_K_M | 2.0 GB | Better answers, about twice as slow | Llama 3.2 Community Licence |
| Phi-3.5 mini Instruct, Q4_K_M | 2.4 GB | Better answers, about twice as slow | MIT |

Download links (Hugging Face):

- Qwen2.5 1.5B: <https://huggingface.co/Qwen/Qwen2.5-1.5B-Instruct-GGUF/resolve/main/qwen2.5-1.5b-instruct-q4_k_m.gguf>
- Qwen2.5 0.5B: <https://huggingface.co/Qwen/Qwen2.5-0.5B-Instruct-GGUF/resolve/main/qwen2.5-0.5b-instruct-q4_k_m.gguf>
- Llama 3.2 3B: <https://huggingface.co/bartowski/Llama-3.2-3B-Instruct-GGUF> (file `Llama-3.2-3B-Instruct-Q4_K_M.gguf`)
- Phi-3.5 mini: <https://huggingface.co/bartowski/Phi-3.5-mini-instruct-GGUF> (file `Phi-3.5-mini-instruct-Q4_K_M.gguf`)

Each model's licence is its own; read it on the model's page before you use it.

From a terminal, for the recommended one:

```powershell
mkdir "$env:LOCALAPPDATA\Bunbun\models" -Force
curl.exe -L -o "$env:LOCALAPPDATA\Bunbun\models\qwen2.5-1.5b-instruct-q4_k_m.gguf" https://huggingface.co/Qwen/Qwen2.5-1.5B-Instruct-GGUF/resolve/main/qwen2.5-1.5b-instruct-q4_k_m.gguf
```

## 3. Put it in place

Move the `.gguf` file into `%LOCALAPPDATA%\Bunbun\models\`. **Settings → Assistant & agents →
Local AI → Models folder** opens it. Bunbun uses the first `.gguf` there (alphabetically). With
more than one, choose under **Model**, or set `LocalAiModel` in `settings.ini` to a full path.

## 4. Turn it on

**Settings → Assistant & agents → Local AI**. The status line should say *Helper installed* and
name your model. Then press **Ctrl+Alt+B** and ask something Bun doesn't know as a command:

- `why is the sky blue` (Bun answers)
- `it's way too loud in here` (Bun turns the volume down)
- `I need to remember to call mum tonight` (Bun adds a reminder)

The first question takes a few seconds while the model loads; later ones start almost at once.

## Bun as an agent

Bun can do several things for one request, and look things up before it answers:

- `get me ready for a meeting`: do not disturb on, volume 20, opens Teams, then "You're all set".
- `wind down for the night`: dims the screen, lowers the volume, turns on do not disturb.
- `is it warmer in Delhi or Mumbai right now?`: looks up both, then compares them.
- `what's on my list and is it going to rain tomorrow?`: reads your to-dos and the forecast, then answers.

The card lists what Bun did (✓ done, ✗ didn't work) above its answer. A plan has at most five
steps, and every step is an ordinary Ask Bun command, so `undo` reverts the last one.

## How it works

1. Ask Bun's own parser always goes first. Known commands run straight away, without the model.
2. Anything it doesn't understand goes to `bunbun-brain.exe`, which Bunbun starts in the background
   at low priority. It gets your question, the time, battery, weather, what's playing, your next
   meeting and a few open to-dos, so it can answer "what's next today?".
3. The model can only reply in one of two shapes, enforced by a grammar while it writes:
   `SAY: <answer>`, or a plan of one to five `RUN: <Ask Bun command>` lines with an optional
   closing `SAY:`. The answer streams into the Ask Bun card while the mascot talks.
4. Every `RUN` goes through Ask Bun's normal parser, so the model can only do what you could type
   yourself. It can't lock the PC, take screenshots or undo. If the command isn't one Bun knows,
   the model is asked again for a plain answer instead. Steps that look something up (weather,
   battery, your list, what's playing) send their results back to the model, which then answers.
   Weather for another city is the only step that uses the network (wttr.in).
5. After 5 idle minutes (**Free the model's memory after**) the helper closes and its memory is
   freed. It closes with Bunbun too.

The helper talks to Bunbun only through its own standard input and output. It has no network
code and opens no ports.

## Troubleshooting

| Symptom | Fix |
|---|---|
| Status: *bunbun-brain.exe isn't built yet* | Step 1. The exe must sit next to `bunbun.exe`. |
| Status: *No model yet* | Step 3. The file must end in `.gguf`. |
| "I didn't catch that" with Local AI on | Check the status line. Without the helper or a model, Bun falls back to its usual reply. |
| "Local AI: couldn't load the model file" | The download is incomplete or not a GGUF model. Download it again. |
| Slow answers | Try the 0.5B model. Close other heavy apps; the helper runs at low priority by design. |
| Odd or wrong answers | Small models make mistakes. Check anything important. Commands still go through Bun's normal parser. |

To remove it, turn Local AI off and delete the models folder and `bunbun-brain.exe`.
