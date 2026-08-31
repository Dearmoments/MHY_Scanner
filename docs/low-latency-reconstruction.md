# Low-latency reconstruction

This branch reconstructs the performance-critical path of the closed 1.2 release on top of the last public source tree. The goal is behavioral compatibility for livestream QR scanning while keeping the code maintainable.

Confirmed from the 1.2 release binary/PDB:
- ZXing-C++ replaces OpenCV WeChatQRCode/Caffe models.
- Bilibili uses a lower-quality stream target (`qn=80`).
- FFmpeg low-latency options include `analyzeduration=0`, `fflags=nobuffer`, and low-delay decoding.
- The release is built with `/O2` and `/arch:AVX2`.

Additional changes in this branch:
- A persistent CPR session is used for the official miHoYo scan/confirm pair so DNS/TCP/TLS work can be reused.
- A warm-up request is issued before livestream scanning starts.
- Stream QR work uses a latest-frame slot instead of accepting an older frame whenever a worker happens to become free.

The 1.2 license/anti-debug implementation is intentionally not reconstructed because it is unrelated to scan latency.
