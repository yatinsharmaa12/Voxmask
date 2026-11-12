# Vendored miniaudio

Version: 0.11.23 (version string in header, 2025-09-11).
Source: the exact `miniaudio.h` distributed in `github.com/gen2brain/malgo@v0.11.24`,
fetched through Go's module proxy/checksum verification during M1.
Upstream: https://github.com/mackron/miniaudio

SHA-256 of miniaudio.h:
`7e4f3f13c8fe66df2080ac3dd12a89193e3c2463cb7f067c798abd7331cd8ee6`

The header is unchanged. Its full dual-license text remains at the end of the
header; VoxMask selects the MIT No Attribution (MIT-0) option. This is a miniaudio
source copy, not a copy of Malgo's Go wrapper. Do not update it independently of
ABI, lifecycle, callback, sanitizer and platform qualification tests.
