# Film-type library

File: `core/detection/film_types.json` (embedded into the core at build time). Values are *priors*:
detection measures every print; a mismatch is reported, never forced.

| id | Name | Card (mm, W×H portrait) | Image (mm) | Inner shape | Corner radius | Source |
| --- | --- | --- | --- | --- | --- | --- |
| polaroid_600 | Polaroid 600 / i-Type / SX-70 | 88 × 107 | 79 × 79 | quad | ~1 mm | retailer listing of Polaroid 600 film: "107 mm × 88 mm, image area 79 mm × 79 mm" ([Diamonds Camera](https://www.diamondscamera.com.au/polaroid-600-yellow-instant-film), accessed 2026-10-28) |
| polaroid_round | Polaroid 600/i-Type Round Frame | 88 × 107 | circle, diameter measured (prior: 75 mm, assumption) | ellipse | ~1 mm | card size: [Roberts Camera](https://robertscamera.com/pol-color-film-for-600-round-pol-004672) "4.2 × 3.5 in (107 × 88 mm)"; diameter not published |
| instax_mini | Instax Mini | 54 × 86 | 46 × 62 | quad | ~3 mm | [Wikipedia: Instax](https://en.wikipedia.org/wiki/Instax) (cites Fujifilm data sheets), accessed 2026-10-28 |
| instax_square | Instax Square | 72 × 85.6 | 62 × 62 | quad | ~3 mm | same |
| instax_wide | Instax Wide (landscape card) | 108 × 86 | 99 × 62 | quad | ~3 mm | same |

Vertical position of the image area (top margin) is not published; priors use top margin = side margin
+ 1.5 mm for Polaroid and = side margin + 2 mm for Instax, with ±45 % search windows. These are assumptions
marked `"approximate": true` in the JSON and are superseded by measurement.

Instax Wide is the only landscape card: 108 mm wide × 86 mm tall when upright, image 99 × 62 mm, with the
wide margin along the bottom long edge. Orientation logic uses "widest border = bottom" for all types.
