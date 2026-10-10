# Space-only managed cutscene votes

During a synchronized native co-op cutscene, release Space and then press Space once to vote. Enter, other keyboard keys, mouse buttons/wheel and controller buttons do not cast a vote. The overlay shows Space and the current tally; a counted vote is shown after the host/server state acknowledges it. Everyone captured for that scene must vote before the original native skip path is released.

Space already held before playback or a same-name replacement scene must be released and pressed again. Typing in chat or losing foreground disarms the release latch; after returning, release Space while focused with chat closed before pressing it. The normal frame Process also disarms inhibition when a native skip query did not occur during that frame. Repeated queries, held Space and repeat presses cannot send a second local vote for the same generation/identity.

Offline, unmanaged and excluded native scenes retain the original game's broad skip input. Short script-only cinematics are not assumed to be native cutscenes. This increment changes no eligibility, unanimity, host migration, deferred START, cancellation, generation/identity, packet or shared state-machine contract.

## Verified native boundary

Supported executable SHA256: A559AA772FD136379155EFA71F00C47AAD34BBFEAE6196B0FE1047D0645CBD26. The actual bundled CPad.cpp binds NewKeyState to 0xB73190. Its actual CKeyboardState class is 0x270 bytes, has standardKeys at offset 0x18, and has two-byte short cells. Literal Space (32) therefore reads 0xB731E8; OldKeyState's corresponding cell is 0xB72F78. Supported disk instructions in the native 0x4D5D10 query confirm those word reads. The original query returns bool in AL with a cdecl/no-argument signature.

Native foreground helper 0x746070 reads the DWORD at 0xC920EC, tests the whole value and returns bool. The private vote predicate matches its 32-bit nonzero test and also requires chat closed. The branch reads only the Space keyboard cell for local votes; no native broad query is executed in managed scenes. Original broad 0x4D5D10 delegation occurs only in the offline/unmanaged/excluded fallback.

Tests compile unchanged SDK keyboard, controller and mouse class layouts into their recorded input fixture, with sizeof/offsetof assertions. The executable is read from disk to verify keyboard cells and the foreground helper. Actual service functions are extracted unchanged except includes and the one fixed-address foreground read mapped to recorded memory. Existing unanimous state-machine/server/codec, migration and relay tests are retained. No game/native addresses are executed by these headless tests.

## Reproducible checks

Run from this worktree, with a fresh unique output:

```
python tests/cutscene/run_tests.py --output .cache/cutscene-headless/unique
```

The runner clears GTA_SA_DIR and compiles all seven tests as x86 MSVC C++17. Source/support/native reference hashes are recorded. The input suite has 53 assertions plus exhaustive coverage of 255 non-Space standard-key cases. It covers special/function keys, mouse/controller inputs, focus/chat inhibition between queries, full-width foreground, held/release/repress, exact identity, duplicates, deferred playback, same-name scenes, commit and original fallbacks.

Preserved evidence distinguishes setup from source failures. space-red-001/002 stopped on initial foreground-proof assertions. space-red-003 also exposed unsafe empty-vector access in the new fixture. space-red-004 has baseline assertion failures but includes three inherited preparation-counter expectations and an unacknowledged overlay expectation, which were corrected. The final baseline-only nonshipping snapshot uses byte-identical final tests and records 41 failures against the unchanged base service; final green records 53 input checks and all other suites passing. Actual implemented production remains in the branch, never only in an overlay.

Human testing remains manual: try Enter, mouse and controller Cross during a managed scene; verify no vote. Hold Space before START/BEGIN, then release/repress. Open chat, type Space, close it while holding Space; verify rearm. Alt+Tab while held, return and rearm. Verify unanimous skip and the next identical-name scene. Test offline/excluded scenes separately; their native broader controls remain. Native foreground/message timing and game execution are not claimed by headless evidence.
