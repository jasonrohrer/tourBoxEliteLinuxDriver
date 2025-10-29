# tourBoxEliteLinuxDriver
A Linux userland driver for TourBox Elite with **haptics support** and **per-application mapping**

TourBox is a neat little USB macro controller with various knobs and buttons of different shapes and sizes, which means you can tell what control you're manipulating without looking at it.

The Elite version is even cooler, with haptic feedback on the knob, dial, and scroll wheel.  You adjust the strength of this feedback per control and per combo (so holding down the Side button while turning the Knob can have different haptics than just turning the Knob), and you can also adjust the rotation speeds, which effectively spaces out the "haptic detents" that correspond with macro commands being sent.

Official drivers are only provided for MacOS and Windows, and there has been no headway made in convicing them to make a Linux driver.

There are a few attempts at Linux drivers out there, but I found that they weren't compatible with the Elite version, which requires a haptics setup message over USB before it will start generating output.  Furthermore, none of them, that I could find, supported per-application mapping, which is where TourBox really shines.

## Dependencies
This driver writes keyboard codes to `/dev/uinput`

It interacts over USB using `libusb-1.0`, which seems to be available everywhere.  I was originally hoping to do this using the `/dev/ttyACM` device file directly, without any libraries, but I found that this didn't work on older kernels, which apparently do way less automatic setup when creating these ACM devices.  Furthermore, using the `/dev/ttyACM` would require the end user figuring out which ttyACM was the correct one (`/dev/ttyACM0`, etc.), where using libusb-1.0 allows us to pick out the TourBox Elite using just the VID and PID of the device itself.

Finally, it handles tracking application switching using window titles obtained through the command-line program `xprop`

On Debian, you would install these dependencies as follows:

`sudo apt install x11-util libusb-1.0-0-dev`

## Compiling
The driver itself is a single file of C89 code, though it does include some POSIX stuff.  Compile it like so:

`gcc -o tourBoxEliteDriver tourBoxEliteDriver.c -lusb1.0`

Writing to `/dev/uinput`, and I think also doing USB stuff, requires that you run the driver using `sudo`.  Maybe there's a more elegant way to do this, but I haven't looked into it.

Settings are housed in a text file, with a sample provided.  The comments in the sample settings file document the settings format.

## Running
Run the driver like so:

`sudo ./tourBoxEliteDriver settingsFile.txt`

Leave the driver running in the background, and it will pay attention to window switches and map TourBox Elite controls to keyboard sequences.

## Notes
Keyboard commands sent through `/dev/uinput` are really fast.  Some applications can't keep up with them, especially for longer key sequences, so you may need to sprinkle SLEEP_ triggers in your sequences as-needed.  As one example, I noticed that if I used ALT-S to open a menu in FireFox, and then tried to send arrow keys to the menu, the arrow keys would go to the web page before the menu had a chance to open.  Putting a SLEEP_ trigger in there, to wait for the menu to open, fixed this problem.

This driver uses only static memory allocation at runtime, giving it a fixed memory footprint and no possible memory leaks over time.  There are various size definitions at the top of the C file, which you can adjust if you want to support more comprehensive functionality (like making mappings for more than 64 applications), or if you want to shrink the RAM footprint.  On my machine, the default size definitions give a virtual RAM footprint of about 15,872 KB, and if I shrink those sizes down in the C file, I can get to down to about 11,000 kB.  I'm guessing that the baseline virtual memory usage is coming from libusb.  The resident memory footprint is 1152 KB.

This driver only supports 2-button/knob combos.  Holding Side while pressing Top can do something different than just pressing Top, but holding Side and Top while pressing Tall cannot have its own unique mapping (and if you press Side + Top + Tall, it will act just like Side + Top followed by Side + Tall, where the first button held down is the only one that's counted as being held down).  In principle, there's no reason why 3-control combos can't work, other than implementation complexity.  However, for the knob/dial/scroll, haptic differentiation only supports 2-control combos at the hardware level.
