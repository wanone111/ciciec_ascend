# Ascend 310B preprocessing acceleration TODO

Updated: 2026-07-18. The current real input is a Hikrobot GigE camera; PYNQ is not connected.

## Completed in code and local/board unit tests

- [x] Preserve `cpu + float_nchw` as the default and add explicit per-model modes.
- [x] Add static AIPP BGR and full-range BT.601 NV12 configuration candidates.
- [x] Add non-overwriting ATC variants, checksums, metadata and OM contract audit.
- [x] Keep one reference-counted ACL device/context across all sessions.
- [x] Add strict `execute_device_input(..., copy_mode="d2d")` input-size checking and profiling.
- [x] Add lazy-import DVPP wrapper, reusable channel/stream/buffers and idempotent release.
- [x] Match pose centered 114/128 letterbox and face top-left 0/128 canvas geometry.
- [x] Add aligned expression crop metadata and invalid-ROI handling.
- [x] Add owned raw UDP frames so NV12 can bypass `cvtColor`.
- [x] Add pipeline switches, JSONL contract/config/checksum records and emergency cleanup.
- [x] Add fake-ACL tests and board validation/benchmark scripts.
- [x] Restore the verified Hikrobot GigE source in the formal workspace.

## Real board results

- [x] CANN 7.0.RC1 / 310B4 API probe: required DVPP resize and crop/paste APIs exist.
- [x] Existing OM audit: pose and face are 4,915,200-byte float32 inputs; expression is 150,528-byte float32.
- [x] Original board baseline: 12/12 tests pass.
- [x] Updated local and board code: 28/28 tests pass.
- [x] Hikrobot `MV-CS060-10GC` smoke test: 10/10 frames, zero timeouts/lost packets.
- [x] Hikrobot CPU full pipeline: 100 frames completed, zero timeouts/lost packets.
- [x] Switch the default pose model to the contract-matched `yolo26n-pose-640.om`; retain `yolo26s-pose.om` as a rollback candidate.
- [x] Confirm `yolo26n-pose-640.onnx` matches the deployed n640 float OM: a fresh ATC `audit_float` build produced bit-exact outputs on all validation inputs, including the saved Hik frame.
- [x] Generate non-overwriting n640 pose AIPP models: packed-BGR input is 1,228,800 bytes and NV12 input is 614,400 bytes.
- [x] Pose packed-BGR AIPP parity on the person sample: 2/2 detections, main bbox IoU 1.0, score delta 0, keypoint maximum error 0.5 px and identical action rule output.
- [x] Pose DVPP+NV12-AIPP parity on the same sample: 2/2 detections, main bbox IoU 0.99545, score delta 0, keypoint mean/max error 0.581/6.5 px and identical action rule output.
- [x] Current camera address is `192.168.2.253`; board-side reachability and a 10-frame acquisition smoke test passed.
- [x] Three 100-frame Hik runs completed: n640 CPU, n640 packed-BGR AIPP, and experimental BGR-to-NV12+DVPP+AIPP. Packed-BGR AIPP is the recommended Hik path; host BGR-to-NV12 makes the DVPP path slower for this source.
- [x] Static expression AIPP BGR numerical parity: synthetic color inputs are bit-exact; a saved Hik frame keeps top-1 and has probability max-abs error 0.00020.
- [x] DVPP geometry on a saved 1928x1078 Hik frame: pose/face/expression buffers, padding values, ROI alignment and the CANN 7 six-argument crop/paste call were measured on 310B.
- [x] Expression DVPP+D2D+NV12-AIPP parity: 18,816-byte input, all synthetic/real samples keep top-1; saved Hik probability max-abs error 0.00708.
- [x] Hik mixed pipeline lifecycle run: pose/face legacy plus expression NV12-AIPP completed 100 frames after lazy-upload fix, with zero overwrite/timeout/loss.

## Explicitly deferred

- [ ] `external` zero-copy dataset input. It remains blocked; D2D is the safe implementation.
- [ ] Production face AIPP default. Face remains `float_nchw` until bbox/score parity passes.
- [ ] PYNQ NV12 direct path. The receiver is ready, but the current PYNQ sender still requests RGB24/MJPEG and hardware is not connected.
- [ ] Full three-model DVPP+AIPP production switch. Pose and expression now have independent A/B evidence; face AIPP output parity remains the only model-validation blocker.
- [ ] Native-NV12 source performance acceptance. The n640 NV12 model and DVPP geometry are numerically validated, but the current Hik SDK delivers BGR, so host BGR-to-NV12 conversion invalidates the intended performance benefit.

Detailed evidence and commands: `docs/DVPP_AIPP_INTEGRATION_REPORT.md`.
