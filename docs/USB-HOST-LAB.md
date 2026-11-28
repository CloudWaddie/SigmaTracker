# Experimental USB host Launcher build — laboratory only

This is the app-only ESP32-S3 USB CDC host variant for the T-Embed CC1101.
It does not provide VBUS, enable boost, configure USB-C source role, or establish
a safe power arrangement. It is NOT a direct-connect solution. Do not connect
a keyboard, USB hub or adapter to the T-Embed's USB port for host testing
unless the complete electrical arrangement has been verified as safe by a
qualified person. Do not use improvised power cables or inject VBUS.
The known working serial-device build remains the supported test arrangement.

The build recognizes the Javelin Polyglot Gemini PR CDC interface (9000:400D,
interface 3) and retains legacy stock Polyglot support.
The app can be tested with the USB port disconnected: boot via Launcher,
verify the PMU check and DEMO mode, and open Diagnostics. HOST WAITING is
expected with no peripheral. This is NOT proof of hardware compatibility.
Restore the serial-device Launcher app for PC bridge use.
