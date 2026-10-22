# Samplify third-party notices

Samplify is an independent implementation of a documented sampling workflow.
It does not contain Serato's Pitch 'n Time or Serato Stems engines. Serato's
names are used in source documentation to identify the behavioral reference.

- JUCE 8.0.6, Raw Material Software / JUCE contributors. GPL-3.0 distribution.
  Source: https://github.com/juce-framework/JUCE
- Rubber Band 4.0.0, Particular Programs Ltd / Breakfast Quay and contributors.
  GPL-2.0-or-later; this application is distributed under GPL-3.0.
  Source: https://github.com/breakfastquay/rubberband
- demucs.cpp, Sevagh and contributors. MIT. Source and model notices accompany
  the native inference backend in this bundle.
  Source: https://github.com/sevagh/demucs.cpp
- Demucs / HTDemucs, Meta and contributors. MIT. Converted from the official
  checkpoint 955717e8-8726e21a.th to the native four-source FP16 format.
  Source: https://github.com/facebookresearch/demucs
- Eigen, Eigen contributors. MPL-2.0; individual file notices apply.
  Source: https://gitlab.com/libeigen/eigen

The exact source revisions, archive checksums, build-local compatibility patches,
checkpoint checksum and converted model checksum are recorded in the source
repository's CMake fragments and script/prepare_stem_model.py. Corresponding
Samplify source is available at https://github.com/sroohi/mellowdy-chop.
