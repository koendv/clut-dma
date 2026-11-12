# font

| file | | license |
|---|---|---|
| `NotoSansCJKsc-Regular-subset.otf` | text, English and Chinese | SIL Open Font License 1.1, `OFL.txt` |
| `MaterialSymbolsOutlined-Fill0-opsz20.ttf` | icons of the status row, the checkbox | Apache License 2.0, `LICENSE-MaterialSymbols.txt` |
| `MaterialSymbolsOutlined.codepoints` | icon names and code points | Apache License 2.0 |

## Noto Sans CJK SC

Noto Sans CJK SC Regular, version 2.004. Noto Sans CJK has its own Latin,
Greek and Cyrillic, drawn to go with the ideographs: one font for English
and Chinese, one baseline.

The SC face of `NotoSansCJK-Regular.ttc` of the Debian package
`fonts-noto-cjk` (1:20230817+repack1-3), cut down with fontTools to the code
points of that face, so that the file holds only what the SC face shows
(10.7 MB). The SC face matters: many ideographs differ between the regions,
e.g. 骨 直 角 文 置 in SC and JP. Layout features are dropped (the glyphs are
drawn one code point at a time); hinting stays.

```python
from fontTools.ttLib import TTCollection
from fontTools import subset

f = TTCollection('/usr/share/fonts/opentype/noto/NotoSansCJK-Regular.ttc').fonts[2]
assert f['name'].getDebugName(4) == 'Noto Sans CJK SC'
opt = subset.Options()
opt.name_IDs = ['*']
opt.name_languages = ['*']
opt.notdef_outline = True
opt.hinting = True
opt.layout_features = []
s = subset.Subsetter(opt)
s.populate(unicodes=f.getBestCmap().keys())
s.subset(f)
f.save('NotoSansCJKsc-Regular-subset.otf')
```

FreeType draws every glyph of this file as the SC face of the collection
does, bit for bit (checked with Pillow at 24 pixels).

## Material Symbols

Material Symbols Outlined, version 2.978, from
[google/material-design-icons](https://github.com/google/material-design-icons),
`variablefont/`, master of 2026-10-23 (commit 10ecc14790): a variable font
with the axes FILL, GRAD, opsz and wght, and its codepoints file.

Here one instance: outline (FILL 0), the smallest optical size (opsz 20),
weight 400, grade 0; the 4,267 icons with a code point of 16 bits (709 kB
instead of 10.7 MB). Outline, not filled: with FILL 1 the icons of an empty
state are filled as well (an empty battery, an empty check box look full).
Drawn at 20 pixels an icon has at most 16 pixels of ink, from 18 to 2 rows
above the baseline: the 16 pixel status row. The checkbox is drawn at 28
pixels, 20x20 pixels of ink.

```python
from fontTools.ttLib import TTFont
from fontTools.varLib import instancer
from fontTools import subset

f = TTFont('MaterialSymbolsOutlined[FILL,GRAD,opsz,wght].ttf')
s = instancer.instantiateVariableFont(f, {'FILL': 0, 'GRAD': 0, 'opsz': 20, 'wght': 400})
opt = subset.Options()
opt.name_IDs = ['*']
opt.name_languages = ['*']
opt.notdef_outline = True
opt.layout_features = []
sub = subset.Subsetter(opt)
sub.populate(unicodes=[c for c in s.getBestCmap() if c <= 0xFFFF])
sub.subset(s)
s.save('MaterialSymbolsOutlined-Fill0-opsz20.ttf')
```

`tools/icons.py` turns icon names into a C header of string literals with
their code points; the codepoints file is the one of the variable font.

## License of the generated fonts

`tools/fontconv.py` turns a font into bitmaps; the comment at the top of
each generated `.c` file names the font, its copyright and its license
(`-L`). Under the SIL Open Font License the bitmaps are a Modified Version
of the font and stay under that license; the icons stay under the Apache
License 2.0.
