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
| Robot | Bolt | A battery, charging bar by bar |

Each one remembers its own colour; the first swatch in the Mascot studio is the original.

Spidey's colour swatches are **Venom suits**: pick one and his suit turns dark in that shade, the
web lines go grey and the spider on his laptop turns white. The first swatch brings back the red
suit.

## Moods

| Mood | When | What it looks like |
|---|---|---|
| Idle | Nothing going on | Breathing, blinking, eyes follow the cursor |
| Working | An agent is working | Typing at a laptop, eyes reading across the screen (Bolt runs a terminal on his visor) |
| Thinking | An agent is thinking | Eyes up, a "?" bobs beside the head |
| Needs you | An agent is waiting for you | A double hop and an amber "!" |
| Celebrate | A task finished | A hop |
| Error | A task failed | Droopy eyes, a tear, a rain cloud, a little head shake |
| Sleepy | Idle for a while | Eyes closed, "Z"s |
| Music | Music is playing | Headphones on, nodding to the beat |
| Do not disturb | Mascot DND is on | Eating its favourite snack |
| Listening | You're typing in Ask Bun | Ears up, leaning toward the text, sound waves at the ear |
| Talking | Bun's local AI is answering | Mouth moving with the words, a speech bubble with dots (Bolt talks through an equaliser) |
| Confused | Bun didn't understand, or couldn't get an answer | Dizzy: spiral eyes, a wavy mouth, a golden ring with stars circling the head |

**Mascot do-not-disturb** is Bunbun's own switch, separate from Windows' Do Not Disturb. Turn it on
in the Mascot studio or ask Bun `dnd on` / `dnd off` (or just `turn on dnd`, `disable do not disturb`).

## Custom skin packs

### From a character sheet

The easiest way to bring your own character: **Mascot studio → Custom skins → Pick a character
sheet**. A character sheet is one image with a row per mood and a few frames per row, like the
sheets image generators and artists make. Label boxes on the left and arrows between frames are
ignored; the background can be transparent or a plain colour.

Bunbun finds the rows and frames and cuts each frame out. Choose what each row is (Idle, Working,
Thinking…, or skip it), name the pack and save. The frames of a row play in turn as that mood's
animation, all at the same size. To add more moods, import another sheet under the same name.

**Eye tracking.** On the resting idle frame the character's irises follow your cursor (when
*Eyes follow your cursor* is on). For a stronger effect, include a row of five look directions in
the order **left, up, centre, down, right** and map it to *Look directions*: the character then
turns its eyes with those drawn frames.

**Timing** lives in `pack.ini` in the pack's folder:

```ini
[Pack]
FrameSeconds=0.4          ; one frame length

[Frames]
idle=1*10,3,1*8,2*3       ; frame 1 for ten lengths, a blink (3), and so on
talking=1*0.7,2*0.7,3*0.9

[Eyes]
Frames=idle_1,listening_1 ; frames whose irises follow the cursor
```

Without a `[Frames]` line a mood plays its frames in order; idle rests on its first frame and plays
the rest now and then. Frames cross-fade into each other, and so do moods; `FadeSeconds` under
`[Pack]` sets how long that takes (0 turns it off).

**Live eyes (face rig).** For the smoothest eye tracking, give the pack a picture of the face with
the eyes and brows left out (`face.png`, on the same canvas as the frames) and tell Bunbun where they
go. Bunbun then draws the eyes and brows itself, sharp at any size: the gaze glides toward the
cursor in every direction, including diagonals, and glances around when the cursor rests. The upper
lids drop as the eyes look down, blinks come every few seconds (sometimes twice), and the brows
lift when the eyes look up. Happy gets closed "^ ^" eyes.

```ini
[Rig]
Face=face.png
Moods=idle,happy              ; moods drawn live; the rest use their frames
LeftEye=191.5,180,21.5,17.5   ; centre x, centre y, half width, half height (face.png pixels)
RightEye=260,179.6,20.5,17.2
Iris=12.3                     ; iris radius
Pupil=5.8
Converge=5                    ; how far the irises sit toward the nose when looking ahead
LeftBrow=213,141,152,164,159  ; inner end x, its top y, its bottom y, outer tip x, tip y
RightBrow=238,140,152,284,158
Ink=#150E0B                   ; outlines and brows
IrisColor=#3B2419
```

The live eyes follow *Blink every* and *Eyes follow your cursor* in the Mascot studio.

### By hand

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
listening.png   you're typing to Bun
talking.png     Bun is answering
confused.png    Bun didn't understand
```

Animated: name the frames `idle_1.png`, `idle_2.png`… (all the same canvas size) instead.
Any mood without its own PNG uses `idle.png`; `talking.png` falls back to `happy.png` and
`confused.png` to `thinking.png`. Click **Refresh** in the Mascot studio and pick the
pack.

Skin packs are for artwork you own or have permission to use. Please don't submit third-party
characters (film, TV, comic or game characters) to this repository.
