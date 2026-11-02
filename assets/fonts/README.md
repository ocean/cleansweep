# Fonts

The screen's words are drawn at build time (`tools/gen_screens.py`) in **Cooper Hewitt**, by Chester Jenkins for
the Cooper Hewitt, Smithsonian Design Museum (2014), SIL Open Font License 1.1 with the Reserved Font Name
"Cooper Hewitt" ([OFL.txt](OFL.txt)). Only its italics are here: the words lean forward, as if zooming along.

The four files are unmodified, from the museum's own download,
<https://www.cooperhewitt.org/wp-content/uploads/fonts/CooperHewitt-OTF-public.zip> (SHA-256
`df5b0869296092fca85742a6295db8bbdedb4e1e19ecaafd195bbabd1ef22d4e`, files dated 2014-06-06). The sources are at
<https://github.com/cooperhewitt/cooperhewitt-typeface>. `tools/gen_screens.py` checks each file's SHA-256 before
drawing with it.

| File | SHA-256 |
| --- | --- |
| CooperHewitt-HeavyItalic.otf | `d09f1625b5621dcfd15b223a9356ef8c1c4bb7860f37dad6dcf4e77d4a35de43` |
| CooperHewitt-SemiboldItalic.otf | `d8ab09c6d0cc436c1503b6c44945a117581456e2dbfbbad779d471a4e3aa7cba` |
| CooperHewitt-MediumItalic.otf | `cacf1a9af4e769fe51215c02e12d773937877c1645c1adb08b85ee39efbc4289` |
| CooperHewitt-BookItalic.otf | `54033f47585fa4c33bed67a9fb0752ca5e273769ce9ff65a69c42144964bb855` |
