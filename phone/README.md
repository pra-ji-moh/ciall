# The newborn on a phone

`index.html` is the being's own brain (`brain/ob_life.c`, compiled to
WebAssembly, about 8 KB) in a page that gives it a phone's senses:

| Its sense | From |
|---|---|
| Its eyes, 8 by 8 | the live camera, or a photo or video you show it, or a drawn crib; it sees a square of the picture and moves the square itself |
| Its hearing, 8 bands | the microphone (which also hears its own voice from the speaker), a video's sound, and you holding *Talk to it* |
| Its skin, 8 places | your finger on the picture |
| Its inner ear | the phone's motion sensors, its own jerks, and you dragging across the picture |

What it does is its own: it looks left, right, up or down, makes a sound, or
jerks (the phone buzzes where it can). Nothing on the page chooses for it.

Everything stays on the phone. Nothing is uploaded. It remembers its life in
the browser's storage, on that device only.

Inside Claude (the published artifact), pages may not use the live camera,
microphone or motion sensors; there, *Show it a photo or video* opens the
phone's own camera instead. Opened from any https address (GitHub Pages, for
instance) or from `localhost`, *Use live camera and mic* gives it live senses.

`build_phone.sh` builds `index.html` from `crib.html` and the brain;
`check_phone.mjs` checks that the phone's brain lives exactly the life the
native one does (`build_c.sh` runs both).
