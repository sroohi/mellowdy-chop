# Rubber Band 4.0.0 allocation fix

Samplify builds Rubber Band tag `v4.0.0`, commit
`1d95888bec3ae0a17c0c4af791810d5a63f6bc35`. The archive checksum remains
unchanged. [PatchRubberBand.cmake](../cmake/PatchRubberBand.cmake) validates the
complete original or already-patched SHA256 of both affected files before
changing either file. Unknown source changes stop configuration.

The R2 `PercussiveAudioCurve` previously reallocated its previous-magnitude
buffer on every `setFftSize`, including reductions and sizes already prepared.
The patch records allocated capacity separately from the logical FFT size.
Construction allocates the original amount; growth allocates only when needed;
shrinking and returning to prepared sizes retain storage. The base FFT-size
update, active-history zeroing in `reset`, processing arithmetic and destructor
remain unchanged. Preparation must still visit the largest required size before
entering the audio callback. This patch alone does not guarantee that other
Rubber Band components never allocate.

`tests/stretch_tests.cpp` checks that warmed storage survives repeated FFT-size
changes and that post-change detector output equals a fresh detector processing
the same data. The audio integration suite separately checks callback allocations
and playback behavior.

Both modified upstream files retain their full original copyright and
GPL-2.0-or-later/commercial-license notices. The macOS bundle includes the original
upstream `COPYING` as `Contents/Resources/RubberBand-COPYING.txt`; Samplify is
distributed under GPL-3.0. Source and patch references are included in
`THIRD_PARTY_NOTICES.md`.
