# Samplify's narrow Rubber Band 4.0.0 real-time allocation fix.
#
# Upstream PercussiveAudioCurve::setFftSize reallocates its magnitude history
# even after a larger FFT was prepared. Keep capacity independently of the
# logical FFT size. Construction, growth, reset, DSP arithmetic and destruction
# retain their original behavior; reset still zeros every active element.
#
# Modified upstream files retain their complete Particular Programs copyright
# and GPL-2.0-or-later / commercial-license notices. Their original source is
# pinned by AudioBackends.cmake. This script contains only the minimal edits.
# It may also run with cmake -DSAMPLIFY_RUBBERBAND_PATCH_ROOT=... -P this-file.
if(NOT DEFINED SAMPLIFY_RUBBERBAND_PATCH_ROOT)
    message(FATAL_ERROR "SAMPLIFY_RUBBERBAND_PATCH_ROOT must name Rubber Band 4.0.0 source")
endif()

set(_rubberband_curve_dir "${SAMPLIFY_RUBBERBAND_PATCH_ROOT}/src/faster")
set(_rubberband_header "${_rubberband_curve_dir}/PercussiveAudioCurve.h")
set(_rubberband_source "${_rubberband_curve_dir}/PercussiveAudioCurve.cpp")
set(_rubberband_header_original 774d16e564efd20313d7db07f0fdaef2944a96d487727e32ac4f2c6d408d0131)
set(_rubberband_header_patched 6a94673b0cc6d00a18524e3b74184f45e56e1a83d304b4e6d3835c710f4c940d)
set(_rubberband_source_original 729d3cdb7ed0ba77ab3a1d76ae349e34f6904043efba7f15e682fb0199426f97)
set(_rubberband_source_patched 6c77131bc555dcf80eee38ba475405d9eb8680e45f096720f603e886b2a8fd24)

# Validate both files before writing either, including already-patched trees.
foreach(_kind header source)
    file(SHA256 "${_rubberband_${_kind}}" _hash)
    if(NOT _hash STREQUAL _rubberband_${_kind}_original AND
       NOT _hash STREQUAL _rubberband_${_kind}_patched)
        message(FATAL_ERROR "Rubber Band allocation patch rejected unexpected ${_rubberband_${_kind}} (SHA256 ${_hash})")
    endif()
endforeach()

file(SHA256 "${_rubberband_header}" _hash)
if(_hash STREQUAL _rubberband_header_original)
    file(READ "${_rubberband_header}" _text)
    string(REPLACE "    double *R__ m_prevMag;"
                   "    double *R__ m_prevMag;\n    int m_prevMagCapacity;" _text "${_text}")
    string(SHA256 _hash "${_text}")
    if(NOT _hash STREQUAL _rubberband_header_patched)
        message(FATAL_ERROR "Rubber Band allocation patch produced an unexpected header")
    endif()
    file(WRITE "${_rubberband_header}" "${_text}")
endif()

file(SHA256 "${_rubberband_source}" _hash)
if(_hash STREQUAL _rubberband_source_original)
    file(READ "${_rubberband_source}" _text)
    string(REPLACE
        "    m_prevMag = allocate_and_zero<double>(m_fftSize/2 + 1);"
        "    m_prevMagCapacity = m_fftSize/2 + 1;\n    m_prevMag = allocate_and_zero<double>(m_prevMagCapacity);"
        _text "${_text}")
    string(REPLACE
        "    m_prevMag = reallocate(m_prevMag, m_fftSize/2 + 1, newSize/2 + 1);"
        "    if (newSize/2 + 1 > m_prevMagCapacity) {\n        m_prevMag = reallocate(m_prevMag, m_prevMagCapacity, newSize/2 + 1);\n        m_prevMagCapacity = newSize/2 + 1;\n    }"
        _text "${_text}")
    string(SHA256 _hash "${_text}")
    if(NOT _hash STREQUAL _rubberband_source_patched)
        message(FATAL_ERROR "Rubber Band allocation patch produced unexpected implementation")
    endif()
    file(WRITE "${_rubberband_source}" "${_text}")
endif()

unset(_rubberband_curve_dir)
unset(_rubberband_header)
unset(_rubberband_source)
unset(_rubberband_header_original)
unset(_rubberband_header_patched)
unset(_rubberband_source_original)
unset(_rubberband_source_patched)
unset(_kind)
unset(_hash)
unset(_text)
