# docs

`scope.png` is a static reproduction of the live radar scope and ops console.
It is generated directly from the application's own renderer — not an external
screen capture — by the built-in snapshot mode:

```sh
./build/sector_control --snapshot docs/scope.png --seed 7
```

That advances a fresh simulation to minute 60 and writes one rendered frame, so
the preview always matches the current palette and layout in `src/render.c`.
Run the application for the real-time, animated view.
