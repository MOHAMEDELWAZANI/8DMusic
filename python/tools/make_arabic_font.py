"""Rebuild the bundled Arabic face so Tk can reach its joined letter forms.

An Arabic font stores one glyph per letter *shape* -- beh alone, beh at the
start of a word, beh in the middle, beh at the end -- and an OpenType engine
picks between them by running the ``init``, ``medi`` and ``fina`` features while
it lays the line out.  Tk has no such engine: it looks each character up in the
font's ``cmap`` and draws whatever it finds, so it can only ever reach the
standalone shape.  Arabic then comes out as a row of disconnected letters.

Unicode has a code point for every one of those shapes, in the Presentation
Forms-B block, and :mod:`eight_d.bidi` converts text into them.  KO Methlama
already contains all the shapes -- what it lacks is any ``cmap`` entry pointing
at them.  This tool reads the substitutions out of the font's own GSUB table and
writes those entries in, so the glyphs the designer drew become addressable.

Nothing is redrawn and no outline is touched; the result is the same font with a
fuller index.  It is renamed so it can never be confused with the original.

    .venv/bin/pip install fonttools
    .venv/bin/python tools/make_arabic_font.py

The app does not need fontTools -- only this tool does, and only when the source
font changes.
"""

from __future__ import annotations

import sys
import unicodedata as ud
from pathlib import Path

from fontTools.ttLib import TTFont

SOURCE = Path.home() / "Documents/arfonts-ko-methlama-medium/ko-methlama-medium.otf"
TARGET = Path(__file__).resolve().parent.parent / "eight_d/assets/fonts/ko-methlama-8d.otf"

FAMILY = "KO Methlama 8D"
STYLE = "Medium"
POSTSCRIPT = "KOMethlama8D-Medium"

LAM = "ل"
ALEFS = ("آ", "أ", "إ", "ا")

#: KO Methlama sets its word space at 9% of the em, against 22% in Noto Naskh
#: Arabic and 26% in Noto Sans Arabic.  A layout engine makes up the difference
#: with justification and cursive positioning; Tk does neither, so words end up
#: touching.  Widened to match the closest peer, and only when the value really
#: is unusually tight, so a font that already spaces properly is left alone.
SPACE_EM = 0.22
TIGHT_EM = 0.15


def presentation_forms():
    """Every Presentation Forms-B code point, by the letter and shape it means."""
    letters: dict[str, dict[str, int]] = {}
    ligatures: dict[tuple[str, str], dict[str, int]] = {}
    for cp in range(0xFE70, 0xFF00):
        decomposition = ud.decomposition(chr(cp))
        if not decomposition.startswith("<"):
            continue
        tag, _, rest = decomposition.partition("> ")
        parts = [chr(int(p, 16)) for p in rest.split()]
        if len(parts) == 1:
            letters.setdefault(parts[0], {})[tag[1:]] = cp
        elif len(parts) == 2:
            ligatures.setdefault((parts[0], parts[1]), {})[tag[1:]] = cp
    return letters, ligatures


def _subtables(lookup):
    """Walk a lookup, stepping through the extension wrapper when there is one."""
    for subtable in lookup.SubTable:
        if lookup.LookupType == 7:                    # extension substitution
            yield subtable.ExtensionLookupType, subtable.ExtSubTable
        else:
            yield lookup.LookupType, subtable


def read_features(font: TTFont):
    """The single and ligature substitutions, gathered per feature tag."""
    singles: dict[str, dict[str, str]] = {}
    ligatures: dict[str, dict[tuple[str, ...], str]] = {}
    if "GSUB" not in font:
        return singles, ligatures

    gsub = font["GSUB"].table
    wanted = {"init", "medi", "fina", "rlig", "ccmp", "dlig"}
    for record in gsub.FeatureList.FeatureRecord:
        tag = record.FeatureTag
        if tag not in wanted:
            continue
        for index in record.Feature.LookupListIndex:
            for kind, subtable in _subtables(gsub.LookupList.Lookup[index]):
                if kind == 1:
                    singles.setdefault(tag, {}).update(subtable.mapping)
                elif kind == 4:
                    for first, entries in subtable.ligatures.items():
                        for entry in entries:
                            key = (first, *entry.Component)
                            ligatures.setdefault(tag, {})[key] = entry.LigGlyph
    return singles, ligatures


def widen_space(font: TTFont) -> str | None:
    """Give the word space a usable width, reporting the change if one is made."""
    glyph = font.getBestCmap().get(0x20)
    if glyph is None:
        return None
    upem = font["head"].unitsPerEm
    width, bearing = font["hmtx"][glyph]
    if width >= TIGHT_EM * upem:
        return None
    widened = round(SPACE_EM * upem)
    font["hmtx"][glyph] = (widened, bearing)
    return f"{width / upem * 100:.0f}% -> {widened / upem * 100:.0f}% of the em"


def rename(font: TTFont) -> None:
    """Give the derived font its own identity, so nothing can mistake the two."""
    replacements = {1: FAMILY, 2: STYLE, 3: f"{FAMILY} {STYLE}",
                    4: f"{FAMILY} {STYLE}", 6: POSTSCRIPT,
                    16: FAMILY, 17: STYLE}
    for record in font["name"].names:
        if record.nameID in replacements:
            record.string = replacements[record.nameID]
    if "CFF " in font:
        cff = font["CFF "].cff
        if cff.fontNames:
            cff.fontNames[0] = POSTSCRIPT


def main() -> int:
    source = Path(sys.argv[1]) if len(sys.argv) > 1 else SOURCE
    if not source.exists():
        print(f"source font not found: {source}", file=sys.stderr)
        return 1

    font = TTFont(str(source))
    singles, ligature_sets = read_features(font)
    cmap = font.getBestCmap()
    glyphs = set(font.getGlyphOrder())
    letters, ligature_forms = presentation_forms()

    added: dict[int, str] = {}
    missing: list[str] = []

    # The joined shapes, taken from the font's own substitution tables.
    for letter, forms in letters.items():
        base = cmap.get(ord(letter))
        if base is None:
            continue
        for shape, codepoint in forms.items():
            if shape == "isolated":
                glyph = base
            else:
                feature = {"initial": "init", "medial": "medi", "final": "fina"}[shape]
                glyph = singles.get(feature, {}).get(base)
            if glyph is None:
                missing.append(f"{letter} {shape}")
            elif glyph in glyphs:
                added[codepoint] = glyph

    # Lam followed by an alef is written as a single glyph.  The font builds it
    # with a ligature rule, and that rule is written against the *joined* shapes
    # rather than the plain letters -- the pair is fused only after the lam has
    # already taken its initial or medial form, which is precisely what decides
    # whether the ligature is the standalone one or the tail-end one.
    joins = {}
    for tag in ("rlig", "ccmp", "dlig"):
        joins.update(ligature_sets.get(tag, {}))
    lam = cmap.get(ord(LAM))
    lam_by_shape = {"isolated": singles.get("init", {}).get(lam),
                    "final": singles.get("medi", {}).get(lam)}

    for alef in ALEFS:
        alef_glyph = cmap.get(ord(alef))
        forms = ligature_forms.get((LAM, alef))
        if not (lam and alef_glyph and forms):
            continue
        # An alef never joins to what follows, so it is always in its final form
        # by the time the ligature rule sees it.
        tail = singles.get("fina", {}).get(alef_glyph, alef_glyph)
        for shape, codepoint in forms.items():
            head = lam_by_shape.get(shape)
            fused = joins.get((head, tail)) if head else None
            if fused is None or fused not in glyphs:
                missing.append(f"lam-alef {alef} {shape}")
                continue
            added[codepoint] = fused

    if not added:
        print("no joined forms could be resolved -- is this an Arabic font?",
              file=sys.stderr)
        return 1

    for table in font["cmap"].tables:
        if table.isUnicode():
            table.cmap.update(added)

    spacing = widen_space(font)
    rename(font)
    TARGET.parent.mkdir(parents=True, exist_ok=True)
    font.save(str(TARGET))

    print(f"source  {source}")
    print(f"written {TARGET.relative_to(Path.cwd()) if TARGET.is_relative_to(Path.cwd()) else TARGET}")
    print(f"        {len(added)} presentation forms now addressable "
          f"({len(glyphs)} glyphs in the font)")
    if spacing:
        print(f"        word space widened {spacing}")
    if missing:
        print(f"        {len(missing)} shapes had no substitution and were left out:")
        print("        " + ", ".join(sorted(set(missing))[:12])
              + (" ..." if len(set(missing)) > 12 else ""))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
