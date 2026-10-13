# Working on DistMultiply

Read README.md for control and routing semantics. This C++20 JUCE effect targets
VST3, AU on macOS, and a custom standalone app.

- `src/PluginProcessor.*` owns the sample-by-sample multiplication, bus mapping,
  Gain/Mix smoothing, parameters, and saved state. There is no cycle engine.
- `src/PluginEditor.*` and `src/LookAndFeel.*` retain the DistInterleave design.
- `src/Standalone.cpp` exposes hardware sidechain channels that JUCE's default
  standalone wrapper does not expose. Preserve this custom wrapper.
- `tests/ProcessorTests.cpp` verifies DSP, routing, smoothing, and state.
- `tools/AUSmoke.cpp` renders the actual AU; `tools/Screenshot.cpp` renders the UI.

Keep processing independent per channel. L/R produces dual mono with averaged
L/R dry; mono main input forces In/Sidechain. A disabled sidechain passes main
input through Gain; an enabled silent sidechain gives zero wet signal. Do not
add thresholding, clipping, normalization, latency, cycle capture, or a tail.
Read every input before writing output because mono-main/stereo-output layouts
can alias output R with sidechain L. Avoid allocation, locks, logging, and I/O
on the audio thread. Keep results independent of host block size.

Preserve parameter IDs `mode`, `gain`, `mix` and the `DistMultiplyState` root.
Use the existing palette/font helpers and fixed design coordinates with scaled
resizing. Keep typed values, double-click reset, shift-drag fine adjustment,
contextual help, and effective mono routing. Use ASCII UI strings.

JUCE is pinned in CMakeLists.txt. An existing checkout can be supplied through
JUCE_SOURCE_DIR; never track a machine-specific path or generated build tree.
For DSP or routing changes, update and run processor tests. For wrapper changes,
build affected formats and run the AU test on macOS:

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release --parallel 6
ctest --test-dir build -C Release --output-on-failure
```

For UI changes, build DistMultiplyShot and inspect both stereo and mono previews
using the commands in README.md. Preserve LICENSE.txt and source provenance.
