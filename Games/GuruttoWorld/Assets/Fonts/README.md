# Editor fonts

The editor uses Fira Mono Regular for Latin text (16 px) and merges M PLUS 1p Regular for Japanese. ASCII remains in Fira Mono. Dear ImGui 1.92 loads supported glyphs on demand, rather than limiting kanji to the legacy Japanese glyph range.

The TTF files are bundled and loaded by the editor; no OS font installation or build-time download is needed. Each font includes its SIL Open Font License 1.1.

| File | Upstream source | SHA-256 |
| --- | --- | --- |
| FiraMono/FiraMono-Regular.ttf | https://github.com/mozilla/Fira/blob/master/ttf/FiraMono-Regular.ttf | 8c86f2963208a353c3435e28ecb38a99ab68f14d7433ba00c1822cef9a9c1b44 |
| MPlus1p/MPLUS1p-Regular.ttf | https://github.com/google/fonts/blob/main/ofl/mplus1p/MPLUS1p-Regular.ttf | 2f294ad496432b1608f070d310e3aa2adcf1de4af429f4901df97ec4bd361ed1 |

Vendored on 2026-10-06. Licenses: FiraMono/LICENSE.txt and MPlus1p/OFL.txt. Shared Content copying includes the font binaries, licenses, and this provenance file in distribution builds.
