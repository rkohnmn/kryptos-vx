# Repeatable performance samples

Configure CMake with `-DCMAKE_BUILD_TYPE=Release -DBUILD_PERFORMANCE_SAMPLES=ON`, build, then run `python scripts/collect_performance.py <sampling-executable> performance.json`. Each measurement uses three discarded warm-up passes and 21 samples; the artifact includes every sample, the median and nearest-rank p95, iteration counts, commit, toolchain settings and runner details. Fixtures are deterministic and temporary files are isolated.

These are synthetic workload timings, not hardware-independent promises or end-user latency distributions. Compare equivalent fixtures, compiler options and machines. The Kinetix render sample measures CPU work with warm caches; it does not measure displayed FPS or SDL presentation. Existing generous correctness timeouts remain separate.
