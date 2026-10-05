# GBA SD Card Info

GBA homebrew for viewing a SuperChis microSD card's identity, capacity and
speed classes, and saving a TXT report.

## Use

Copy `gba-sd-card-info.gba` to the card and launch it through SuperFW in GBA mode.

| Button | Action |
|---|---|
| Left/right or L/R | Change page |
| SELECT, then A | Save or replace `SDINFO.TXT` in the card root |
| B | Cancel saving |
| A after saving | Return to card info |

Saving requires FAT16/FAT32, not exFAT. Keep power on until saving finishes:
an interrupted write can damage the report or filesystem. Power off before
removing the card.

The app shows what the card reports; it does not measure speed or verify
health or authenticity. Unknown brands remain Unknown. Tested on SuperChis
with a DS Lite; other hardware and saving on SDSC cards (2 GB or smaller)
remain unverified.

## Build

Requires Git with a committed project and Docker Compose.

```sh
./docker-build.sh build
```

Output: `out/gba-sd-card-info.gba`.

## License

GPL-3.0-or-later. Include corresponding source and license notices when
redistributing the ROM. See [third-party notices](THIRD_PARTY.md).
