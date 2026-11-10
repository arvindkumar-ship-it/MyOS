# assets

- `font.psf`: 8x16 VGA console font in PSF1 format, from Ubuntu's
  `console-setup-linux` package (`/usr/share/consolefonts/Lat15-VGA16.psf.gz`, unzipped).
  The package states: "All console fonts are public domain by nature."
  It is built into the kernel by `kernel/font.asm` (NASM `incbin`).
