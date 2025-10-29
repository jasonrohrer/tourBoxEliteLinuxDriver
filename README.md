# tourBoxEliteLinuxDriver
A Linux userland driver for TourBox Elite with haptics support

TourBox is a neat little USB macro controller with various knobs and buttons of different shapes and sizes, which means you can tell what control you're manipulating without looking at it.

The Elite version is even cooler, with haptic feedback on the knob, dial, and scroll wheel.  You adjust the strength of this feedback per control and per combo (so holding down the Side button while turning the Knob can have different haptics than just turning the Knob), and you can also adjust the rotation speeds, which effectively spaces out the "haptic detents" that correspond with macro commands being sent.

Official drivers are only provided for MacOS and Windows, and there has been no headway made in convicing them to make a Linux driver.

There are a few attempts at Linux drivers out there, but I found that they weren't compatible with the Elite version, which requires a haptics setup message over USB before it will start generating output.  Furthermore, none of them, that I could find, supported per-application mapping, which is where TourBox really shines.

This driver writes keyboard codes to `/dev/uinput`

It interacts over USB using `libusb-1.0`, which seems to be available everywhere.  I was originally hoping to do this using the `/dev/ttyACM` device file directly, without any libraries, but I found that this didn't work on older kernels, which apparently do way less automatic setup when creating these ACM devices.  Furthermore, using the `/dev/ttyACM` would require the end user figuring out which ttyACM was the correct one (`/dev/ttyACM0`, etc.), where using libusb-1.0 allows us to pick out the TourBox Elite using just the VID and PID of the device itself.
