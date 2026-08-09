"""Making Arabic readable in Tk.

Tk paints a string as a plain left-to-right run of the code points it is handed.
It carries no bidirectional algorithm and no OpenType shaping, so Arabic arrives
backwards *and* with every letter in its standalone form: the words read in
reverse and none of the letters join up.

Both jobs therefore have to be done before the text reaches a widget:

    shape()    swap each letter for the contextual form its neighbours call for,
               and fuse lam-alef pairs into their single glyph
    visual()   reorder from logical order into the order it should be painted,
               so right-to-left runs read correctly

:func:`present` runs both and is what the interface calls.  The letter forms are
read out of :mod:`unicodedata` rather than a table pasted into the source, so
they are exactly what the installed Unicode data says they are.

The reordering covers the everyday cases of UAX #9 -- a single paragraph with no
explicit embedding controls -- which is what a song title needs.  It is not a
conformant implementation and does not try to be; the pieces it leaves out
(isolates, overrides, paragraph splitting) cannot occur in a one-line label.
"""

from __future__ import annotations

import unicodedata as ud

TATWEEL = "ـ"
LAM = "ل"
ALEFS = ("آ", "أ", "إ", "ا")

#: Ranges that mean "this text is Arabic", used to pick the face to draw it in.
ARABIC_RANGES = (
    (0x0600, 0x06FF),   # Arabic
    (0x0750, 0x077F),   # Arabic Supplement
    (0x08A0, 0x08FF),   # Arabic Extended-A
    (0xFB50, 0xFDFF),   # Presentation Forms-A
    (0xFE70, 0xFEFF),   # Presentation Forms-B
)

#: Neutral characters that point the other way inside a right-to-left run.
MIRRORED = {"(": ")", ")": "(", "[": "]", "]": "[", "{": "}", "}": "{",
            "<": ">", ">": "<", "«": "»", "»": "«",
            "‹": "›", "›": "‹"}


def _build_forms():
    """Read the contextual forms straight out of the Unicode tables.

    Every character in Presentation Forms-B decomposes to the letter it stands
    for, tagged with the form it represents -- ``<initial> 0628`` and so on.
    Two-part decompositions are the lam-alef ligatures.
    """
    letters: dict[str, dict[str, str]] = {}
    ligatures: dict[tuple[str, str], dict[str, str]] = {}
    for cp in range(0xFE70, 0xFF00):
        glyph = chr(cp)
        decomposition = ud.decomposition(glyph)
        if not decomposition.startswith("<"):
            continue
        tag, _, rest = decomposition.partition("> ")
        parts = [chr(int(p, 16)) for p in rest.split()]
        if len(parts) == 1:
            letters.setdefault(parts[0], {})[tag[1:]] = glyph
        elif len(parts) == 2:
            ligatures.setdefault((parts[0], parts[1]), {})[tag[1:]] = glyph
    return letters, ligatures


_LETTERS, _LIGATURES = _build_forms()


def has_arabic(text: str) -> bool:
    return any(any(lo <= ord(ch) <= hi for lo, hi in ARABIC_RANGES) for ch in text)


def _transparent(ch: str) -> bool:
    """Marks that sit on a letter without interrupting the join."""
    return ud.category(ch) in ("Mn", "Me", "Cf")


def _joins_forward(ch: str) -> bool:
    forms = _LETTERS.get(ch)
    return ch == TATWEEL or bool(forms and ("initial" in forms or "medial" in forms))


def _accepts_prev(ch: str) -> bool:
    forms = _LETTERS.get(ch)
    return ch == TATWEEL or bool(forms and ("final" in forms or "medial" in forms))


def shape(text: str) -> str:
    """Give every Arabic letter the form its neighbours call for."""
    if not text:
        return text
    chars = list(text)
    solid = [i for i, ch in enumerate(chars) if not _transparent(ch)]
    order = {i: k for k, i in enumerate(solid)}
    out: list[str] = []
    skip: set[int] = set()

    for i, ch in enumerate(chars):
        if i in skip:
            continue
        if _transparent(ch):
            out.append(ch)
            continue

        k = order[i]
        previous = chars[solid[k - 1]] if k > 0 else ""
        next_index = solid[k + 1] if k + 1 < len(solid) else None
        following = chars[next_index] if next_index is not None else ""
        joined_before = bool(previous) and _joins_forward(previous) and _accepts_prev(ch)

        # Lam followed by an alef is written as one glyph, never as two letters.
        if ch == LAM and following in ALEFS:
            pair = _LIGATURES.get((ch, following), {})
            glyph = pair.get("final" if joined_before else "isolated")
            if glyph:
                out.append(glyph)
                skip.add(next_index)
                continue

        forms = _LETTERS.get(ch)
        if not forms:
            out.append(ch)
            continue
        joined_after = bool(following) and _joins_forward(ch) and _accepts_prev(following)
        if joined_before and joined_after and "medial" in forms:
            out.append(forms["medial"])
        elif joined_before and "final" in forms:
            out.append(forms["final"])
        elif joined_after and "initial" in forms:
            out.append(forms["initial"])
        else:
            out.append(forms.get("isolated", ch))
    return "".join(out)


def _first_strong(text: str) -> str:
    for ch in text:
        kind = ud.bidirectional(ch)
        if kind == "L":
            return "L"
        if kind in ("R", "AL"):
            return "R"
    return "L"


def is_rtl(text: str) -> bool:
    """Whether the line as a whole reads right to left."""
    return _first_strong(text) == "R"


def _levels(text: str, base: int) -> list[int]:
    """An embedding level per character, per the resolution rules of UAX #9."""
    kinds = [ud.bidirectional(ch) or "ON" for ch in text]
    n = len(kinds)

    # W1: a combining mark inherits the class of the letter it sits on.
    for i in range(n):
        if kinds[i] == "NSM":
            kinds[i] = kinds[i - 1] if i else ("AL" if base else "L")

    # W2/W7: a digit takes its direction from the last letter before it -- Arabic
    # digits after an Arabic letter, but plain left-to-right digits after a Latin
    # one, which is what keeps "Brave 2024" together inside an Arabic line.
    strong = "AL" if base else "L"
    for i in range(n):
        if kinds[i] in ("L", "R", "AL"):
            strong = kinds[i]
        elif kinds[i] == "EN":
            if strong == "AL":
                kinds[i] = "AN"
            elif strong == "L":
                kinds[i] = "L"

    # N1/N2: a neutral run takes the direction on either side when they agree,
    # and the paragraph direction when they disagree.  Numbers count as
    # right-to-left for this purpose, whichever kind they are.
    def strength(kind: str) -> str | None:
        if kind == "L":
            return "L"
        if kind in ("R", "AL", "EN", "AN"):
            return "R"
        return None

    paragraph = "R" if base else "L"
    sides = [strength(k) for k in kinds]
    i = 0
    while i < n:
        if sides[i] is not None:
            i += 1
            continue
        j = i
        while j < n and sides[j] is None:
            j += 1
        before = sides[i - 1] if i else paragraph
        after = sides[j] if j < n else paragraph
        fill = before if before == after else paragraph
        for k in range(i, j):
            sides[k] = fill
        i = j

    levels = []
    for kind, side in zip(kinds, sides):
        if kind in ("EN", "AN"):
            # Digits always run left to right, but sit inside the right-to-left
            # flow around them, so they take the next even level up.
            levels.append(2 if (base or side == "R") else 0)
        elif side == "R":
            levels.append(1)
        else:
            levels.append(2 if base else 0)
    return levels


def visual(text: str) -> str:
    """Reorder logical text into the order it should be painted."""
    if not text:
        return text
    base = 1 if is_rtl(text) else 0
    levels = _levels(text, base)
    if not any(level % 2 for level in levels):
        return text                      # nothing right-to-left in it

    chars = list(text)
    for i, level in enumerate(levels):
        if level % 2 and chars[i] in MIRRORED:
            chars[i] = MIRRORED[chars[i]]

    # L2: reverse every run at each level, working down from the deepest to the
    # shallowest odd one.  Deeper runs nest inside shallower ones, so reversing
    # a nested run and then its parent puts digits back the right way round --
    # which is why the levels stay pinned to positions rather than moving with
    # the characters.
    lowest_odd = min(level for level in levels if level % 2)
    for level in range(max(levels), lowest_odd - 1, -1):
        i = 0
        while i < len(chars):
            if levels[i] < level:
                i += 1
                continue
            j = i
            while j < len(chars) and levels[j] >= level:
                j += 1
            chars[i:j] = chars[i:j][::-1]
            i = j
    return "".join(chars)


def present(text: str) -> str:
    """Logical text as it should actually be painted by Tk."""
    if not text or not has_arabic(text):
        # Nothing to join, and nothing to flip unless some other right-to-left
        # script is in play.
        return visual(text) if any(
            ud.bidirectional(ch) in ("R", "AL") for ch in text) else text
    return visual(shape(text))
