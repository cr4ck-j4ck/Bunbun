# Mascots

Bun lives beside the clock on the collapsed island, on agent pills and approval cards, and in the
Ask Bun box. Pick a character in **Settings → Mascot studio**.

## Built-in characters

| Character | Name | Do-not-disturb snack |
|---|---|---|
| Bunny | Bun | A carrot |
| Cat | Miso | A fish |
| Dog | Biscuit | A bone |
| Fox | Ember | A drumstick |
| Panda | Bao | Bamboo |
| Robot | Bolt | His visor turns into a terminal |

Each one remembers its own colour; the first swatch in the Mascot studio is the original.

## Moods

| Mood | When | What it looks like |
|---|---|---|
| Idle | Nothing going on | Breathing, blinking, eyes follow the cursor |
| Working | An agent is working | Ears sway |
| Thinking | An agent is thinking | Eyes up, a "?" bobs beside the head |
| Needs you | An agent is waiting for you | A double hop and an amber "!" |
| Celebrate | A task finished | A hop |
| Error | A task failed | Droopy eyes, a tear, a rain cloud, a little head shake |
| Sleepy | Idle for a while | Eyes closed, "Z"s |
| Music | Music is playing | Headphones on, nodding to the beat |
| Do not disturb | Mascot DND is on | Eating its favourite snack |

**Mascot do-not-disturb** is Bunbun's own switch, separate from Windows' Do Not Disturb. Turn it on
in the Mascot studio or ask Bun `dnd on` / `dnd off`.

## Custom skin packs

Use your own art. Make a folder in `%APPDATA%\Bunbun\mascots\` (the Mascot studio's **Open folder**
button takes you there), and put square, transparent PNGs in it, 256 px or larger, named after the
moods:

```
idle.png        required
working.png
thinking.png
alert.png       needs you
happy.png       celebrate
sad.png         error
sleepy.png
music.png
snack.png       do not disturb
```

Any mood without its own PNG uses `idle.png`. Click **Refresh** in the Mascot studio and pick the
pack.

Skin packs are for artwork you own or have permission to use. Please don't submit third-party
characters (film, TV, comic or game characters) to this repository.
