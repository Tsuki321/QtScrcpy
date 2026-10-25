# Wireless pairing, scaling and live mappings

This fork includes upstream QtScrcpy through `b018bec` and QtScrcpyCore through
`db8ccba`, plus the local input/rendering work that was present before synchronization.
The core submodule points to the matching public fork so recursive clones and
GitHub Actions use the same code.

## Connect a phone

1. Enable **Developer options → Wireless debugging** on the phone. Put the phone
   and computer on the same network.
2. Open **Pair device with pairing code** on the phone.
3. In QtScrcpy's **Wireless pairing** tab, enter the full IP address, **pairing
   port**, and six-digit code. Click **Pair and connect**.
4. The app searches ADB mDNS services for that IP and connects to its debugging
   port. The phone appears in the device list; select it and click **Start**.

The pairing port and debugging connection port are different. If discovery is
blocked by the network, enter the **connection port** shown on the phone's main
Wireless debugging screen. An already paired phone needs only its current IP and
connection port and **Connect paired phone**. A new pairing code is required when
the phone revokes this PC's authorization.

Discovery polls every five seconds and lists only ADB wireless connection
services. It does not scan the subnet. Double-click a discovered endpoint to
connect. Pairing codes are sent to ADB on stdin, cleared from the UI, and never
saved in settings or logged. Addresses and ports are remembered locally. ADB
commands have a timeout and run outside the GUI event loop.

The app enables ADB's OpenScreen mDNS backend before starting ADB. An ADB server
that was already started by another application may use a different backend;
the explicit connection-port field still works without discovery.

## Low latency and integrated graphics

Select **Wireless: 720p / 60 FPS / 4 Mbps** before starting the stream.
This sets the encoded longest edge to 1280 pixels (the other edge follows the
phone's aspect ratio), caps capture at 60 FPS and requests 4 Mbps H.264.
For slow or variable networks, **Weak Wi-Fi: 540p / 60 FPS / 1.5 Mbps** lowers
the longest edge to 960 and leaves more network headroom. Both presets disable
rendering of expired frames. Select **Manual capture settings** for manual control. Capture settings apply on the next
connection. The phone may send fewer frames when its screen is idle.

Resize or maximize the video window to scale the picture. The default is GPU
bilinear scaling. **Ctrl+U** toggles mild luma sharpening for enlarged images.
Sharpening uses four additional luma samples within the existing YUV conversion
shader. It adds no render pass, frame queue or AI model. It cannot recover detail
that was never encoded by the phone. Bilinear scaling is the lowest GPU cost option.

The renderer uploads the current frame directly instead of uploading the previous
pixel buffer. The GUI uses a reference to FFmpeg's frame buffers instead of copying
all three image planes, and releases the decoder mutex before upload. Late frames
replace pending frames, and input packets are sent immediately on the controller's
thread. TCP_NODELAY is applied after the control socket connects.

## Map controls while streaming

Click **Map** on the video or press **Ctrl+E**. The live video continues behind
the editor. Phone input is suspended during editing.

- Click a location, then press a keyboard key or mouse button in the capture box.
- Drag a marker to reposition it; **Bind** changes its button, and **Delete** removes it.
- **Tab** hides/shows the toolbar so locations underneath it can also be mapped.
- **Save** writes a JSON profile atomically. **Load** opens a saved profile.
- **Apply** activates the profile immediately. It does not save the file; use
  **Save** for persistence between sessions.
- **Close** restores the previous profile. A dirty editor prompts before closing;
  its edits remain available while that phone window stays open.

Profiles use coordinates relative to the video, so resizing and desktop DPI do
not change their positions. New profiles use the backtick key to toggle mapping
mode. Click mappings can use single keyboard keys or mouse buttons; modifiers may
be assigned separately. Avoid shortcut combinations reserved by the desktop app.
Existing steering-wheel, swipe and mouse-look nodes are preserved when importing
a profile; this editor currently edits click nodes. Imported toggle keys remain
unchanged. Held touches, keyboard keys and simulated pinch fingers are released
on profile replacement or loss of window activation.

The save dialog defaults to `keymap` under Qt's writable application-data location.
Profiles are ordinary JSON files and can be copied to other computers or opened
through the editor's Load button.

## Reproduce the measurements

All compilation happens in **Wireless validation** in GitHub Actions. Windows and
Linux build the application and run Qt tests for pairing success/failure,
discovery, command timeouts, profile validation, input release, latest-frame
replacement, buffer lifetime and current-frame rendering with padded strides.
The Windows artifact contains a runnable application, ADB, server and Qt libraries.

The same test run publishes `ctest-results.xml`, `benchmark-results.xml`, and a
synthetic framebuffer image. Microbenchmarks compare the previous 1080p plane-copy
operation with frame referencing, and measure 1280×720 → 1920×1080 rendering with
bilinear scaling and mild sharpening. Rendering waits for GPU completion. Hosted
runners use software OpenGL; their scaling results are not integrated-GPU results.
Renderer identity is recorded in the test output. No phone or pairing key is
uploaded to Actions.

After downloading the Windows artifact, the supplied test executable can measure
the same scaling workloads on the actual PC without compiling anything locally:

```powershell
.\qtscrcpy_tests.exe renderBenchmark -o gpu-benchmark.xml,xml -o -,txt
```

With an authorized wireless phone connected, run the Python transport and live
frame benchmark using the Actions-built app:

```powershell
python benchmarks/live_phone.py --adb "path/to/adb.exe" `
  --serial "PHONE_IP:CONNECTION_PORT" --app "path/to/QtScrcpy.exe" `
  --seconds 30 --output "phone-benchmark.json"
```

Use `--preset low-bandwidth` to test the Weak Wi-Fi profile. Add `--render-only`
to start the stream immediately without running the transport probes first.

Close other QtScrcpy instances first. Keep the phone unlocked, with repeatable
on-screen motion when comparing frame rates. The script measures 30 ADB shell
round trips and three 4 MiB downloads, then opens the mirror using the wireless
preset and saves frame statistics. The app exits when measurement finishes.
Transport reports omit phone addresses and pairing codes.

ADB shell round trips include process and shell overhead. Frame callback-to-GL
submission time excludes capture, encoding, network transport, compositor and
display scanout. Neither is a measurement of full touch-to-photon latency. Measuring
that requires an external synchronized observation, such as a high-speed camera
recording both screens and the input event. No end-to-end latency number is inferred
from these tests.
