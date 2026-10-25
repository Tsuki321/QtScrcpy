# Custom key mapping instructions

The key map file is in json format, and the new key map file needs to be placed in the keymap directory to be recognized by QtScrcpy.

The specific writing format of the button mapping file will be introduced below, and you can also refer to the button mapping file that comes with it.

## Built-in editor

Open **Key mapping** from the device window toolbar. Editing temporarily releases
active fingers and keys. The editor supports all mapping types below and Android
key events; existing profiles can be loaded and edited directly.

1. Choose a mapping type and click **Add**, or double-click a blank area of the
   video to place it. Use **Edit / Bind** or double-click a marker to change it.
2. Enter a Qt key name or click **Capture** and press a key or mouse button.
   Escape and Tab can be captured; use Cancel to dismiss capture.
3. Drag touch positions, swipe endpoints, steering centers or sequence points on
   the video. Properties also accept normalized coordinates from 0 to 1. Tab
   hides or shows the toolbar when it covers a position you need to edit.
4. **Apply** activates the edited profile and closes the editor.
   Use the profile's **Toggle key** to switch
   between normal input and custom mappings (the default is the backtick key).

**Clear all**, then **Apply**, disables the current profile and releases its
inputs. You can immediately load or create another profile and apply it.
**Undo** and **Redo** also cover clearing, deleting, property edits and dragging.
**New** starts an empty profile. **Save / Save as** writes a JSON file;
**Load** accepts existing JSON files. Saved profiles default to the application's
per-user data directory. Place a file in the application's `keymap` directory if
you also want it listed in the main window's script selector.

Closing the editor restores the last applied profile; unapplied edits remain in
the editor until a different profile is selected. **JSON** provides advanced
editing. Invalid positions, missing fields and conflicting key assignments are
reported before a profile is accepted, keeping the last valid profile intact.
Extra fields in valid profiles are retained.

| Editor type | Behavior and settings |
|---|---|
| Touch / hold | Holds a finger while the binding is held. Optional Android key; optionally releases or recaptures mouse look on binding release. |
| Tap on press and release | One tap on press and another on release, rather than two taps on press. Optional Android key remains held until release. |
| Timed tap sequence | Up to 50 ordered positions with a delay before each tap. Finishes after trigger release; retriggering restarts that binding's sequence. |
| Steering pad (WASD) | Four keyboard bindings, center and independent directional offsets. Supports diagonals and opposite-direction cancellation. One pad per profile. |
| Swipe | Start/end positions, initial delay and speed from 0 (slowest) to 1 (fastest). The finger stays at the endpoint until release. A new swipe replaces the active swipe. |
| Android key | Sends an Android numeric keycode on press/release, such as Home (3), Back (4), or volume up/down (24/25). |
| Mouse look | Origin and separate X/Y speed ratios; lower ratios are faster. Optional free-look binding holds a second origin, then returns to the main origin on release. |

Keyboard and mouse buttons can trigger touches, swipes, sequences and Android
keys. A binding is assigned once per profile, including the toggle and free-look
bindings. Modifier keys are individual bindings; key chords and wheel bindings
are not part of this format. Clearing, replacing, toggling off, resizing the
device frame or losing window focus releases held inputs and cancels pending
gestures. Mouse movement continues across touch boundaries without discarding a
fixed number of movement events.

## Key mapping script format description

### General Instructions

-The coordinate positions in the key map are all expressed by relative positions, and the width and height of the screen are expressed by 1, for example, the pixels of the screen are 1920x1080, then the coordinates (0.5,0.5) indicate
Taking the upper left corner of the screen as the origin, the position of the pixel coordinates (1920,1080)*(0.5,0.5)=(960,540).
    
    Or when the left mouse button clicks, the console will output the pos at this time, just use this pos directly
    ![](image/debug-keymap-pos.png)

-The key codes in the key map are represented by Qt enumerations, detailed description can be [refer to Qt documentation](https://doc.qt.io/qt-5/qt.html) (search for The key names used by Qt. can be quickly located).
-Open the following two settings in the developer options, you can easily observe the coordinates of the touch point:
![](image/display pointer position.jpg)

### Mapping type description

-switchKey: Switch the key of the custom key mapping. The default is the normal mapping. You need to use this key to switch between the normal mapping and the custom mapping.

-mouseMoveMap: mouse movement mapping, the movement of the mouse will be mapped to startPos as the starting point, and the direction of the mouse movement as the direction of the finger drag operation (after the mouse movement map is turned on, the mouse will be hidden, limiting the range of mouse movement).
Generally used to adjust the character field of vision in FPS mobile games.
    -startPos finger drag starting point
    -speedRatio mouse sensitivity of the finger dragging. The value must be at least 0.00225. The greater the value, the lower the sensitivity. The Y-axis translates with a ratio of 2.25. If this does not fit your phone screen, please use the following two settings to set individual sensitivity values.
    -speedRatioX sensitivity of the mouse X-axis. This value must be at least 0.001.
    -speedRatioY sensitivity of the mouse Y-axis. This value must be at least 0.001.
    -smallEyes The button that triggers the small eyes. After pressing this button, the mouse movement will be mapped to the finger drag operation with the smallEyes.pos as the starting point and the mouse movement direction as the movement direction

-keyMapNodes general key map, json array, all general key maps are placed in this array, map the keys of the keyboard to ordinary finger clicks.

There are several types of key mapping as follows:

-type The type of key mapping, each element in keyMapNodes needs to be specified, and can be of the following types:
    -KMT_CLICK Ordinary click, key press simulates finger press, key lift simulates finger lift
    -KMT_CLICK_TWICE Double click, key press simulates finger press and then lift, key lift simulates finger press and then lift
    - KMT_CLICK_MULTI Click multiple times. According to the delay and pos in the clickNodes array, press one key to simulate touching multiple positions
    -KMT_DRAG drag and drop, the key press is simulated as a finger press and drag a distance, the key lift is simulated as a finger lift
    -KMT_STEER_WHEEL steering wheel mapping, which is dedicated to the mapping of the steering wheel for moving characters in FPS games, requires 4 buttons to cooperate.

Description of the unique attributes of different key mapping types:

-KMT_CLICK
    -key The key code to be mapped
    -pos simulates the location of the touch
    -Whether the switchMap releases the mouse. After clicking this button, besides the default simulated touch map, whether the mouse operation is released. (You can refer to the effect of M map mapping in Peace Elite Map)

-KMT_CLICK_TWICE
    -key The key code to be mapped
    -pos Simulates the location of the touch

-KMT_CLICK_MULTI
    -delay Delay `delay` ms before simulating touch
    -pos Simulates the location of the touch

-KMT_DRAG
    -key The key code to be mapped
    -startPos Simulate the start position of touch drag
    -endPos Simulate the end position of touch drag
    -dragSpeed Speed of the drag movement (range 0-1, default 1.0). Higher values result in faster movements
    -startDelay Optional delay in milliseconds to wait after the initial touch before starting the drag movement

-KMT_STEER_WHEEL
    -centerPos steering wheel center point
    -leftKey key control in the left direction
    -rightKey Right key control
    -UpKey key control
    -downKey key control in down direction
    -leftOffset After dragging the left arrow key, drag to the leftOffset horizontally to the centerPos
    -rightOffset After pressing the right direction key, drag it to the right offset of the center to the right of the centerPos position
    -upOffset After pressing the up arrow key, drag it to the upper offset position horizontally relative to the centerPos position
    -downOffset Press the down arrow key and drag it to the downOffset position horizontally relative to the centerPos position

## Visual Key Mapping Tool

1. Just use [QuickAssistant](https://lrbnfell4p.feishu.cn/drive/folder/Hqckfxj5el1Wjpd9uezcX71lnBh)

![game](../screenshot/game.png)

2. A web-based GUI tool is available to help you create and manage key mappings visually: [ScrcpyKeyMapper](https://github.com/w4po/ScrcpyKeyMapper)

![ScrcpyKeyMapper Screenshot](https://raw.githubusercontent.com/w4po/ScrcpyKeyMapper/main/assets/screenshot.png)

You can use this tool to:
- Create key mappings visually
- Test your mappings in real-time
- Export mappings as JSON files
- Import existing mappings for editing

Try it online: [ScrcpyKeyMapper Web App](https://w4po.github.io/ScrcpyKeyMapper)

