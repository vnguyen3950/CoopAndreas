Run this standalone harness after the combined build, in an initialized C++ compiler environment:

```powershell
python tests/run_vehicle_packet_extensions.py --evidence "$env:TEMP\vehicle-packet-results.json"
```

Use `--compiler` with a compiler path if it is not on PATH. C++ compilation and execution occur in a unique temporary directory; no game, shared build output, or production file is loaded or changed.

The runner extracts the current dirt normalization/compression and flag/alarm sections from **both** `VehicleIdleUpdate` and `VehicleDriverUpdate`. It also copies their actual initialized extension field declarations. Generated fixtures call the repository's real `third_party/serialize.h`; there is no replacement bit packer. Extraction fails if the expected declarations or codec order change.

Coverage includes all 16 boolean combinations, alarms 0 and 65,535, finite bounds and rounding boundaries, NaN and both infinities, all eight starting bit offsets, every shorter advertised byte length, and equal measured/written/read bit counts. Backing buffers retain dword padding required by the real reader. Failed comparisons return a nonzero process status; debug assertions remain enabled by default.

Runtime JSON evidence records actual SHA-256 hashes for the vehicle header, serializer header, shared configuration header, extracted sections, and test source. It also records compiler output, exit codes, and test output. The tracked reference JSON records extraction only, not a claimed C++ test run. The configuration hash may differ after root's protocol bump; each run captures the headers it actually used.

The fixture joins two extension sections that occupy different positions in the full packet. This deliberately does **not** certify the surrounding native packet types, full packet offsets, ENet channel ordering, driver-entry timing, or native lighting/alarm behavior. Those remain separate integration tests.

The production authority predicate has its own standalone test:

```powershell
cl /nologo /std:c++17 /EHsc tests/vehicle_authority_tests.cpp /Fe:"$env:TEMP\vehicle-authority-tests.exe" /Fo:"$env:TEMP\vehicle-authority-tests.obj"
& "$env:TEMP\vehicle-authority-tests.exe"
```
