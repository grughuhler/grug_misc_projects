ordered_gerber.zip contains gerber files.  You can use it to order your
own PCBs from companies like

* Osh Park (USA)
* DK Red (USA)
* PCBWay (China)

I used Osh Park because it was the cheapest for USA residents.  I got
three boards for a total of $6.50 USD, including shipping.

To assemble the board, you need to solder stuff including surface
mount.  You will need:

* An ESP32-C3 Super Mini.  It has castellated pads.  This allows it
  to be soldered to the adapter board easily.

* A BSS138 NMOS MOSFET SOT-23 surface mount.  You must use a MOSFET
  that is good for 3.3V to 5V level shifting.  I suggest not using
  a 2N7002.  This is the hardest thing to solder.  If I can do it, so
  can you!

* Two 1206 SMD surface mount resistors.  I used 7.5K, but 6.8K or 4.7K
  should be fine, probably better in fact.  The DS1302 has an internal
  40K pull-down so the level converter pull-ups need to be strong.

* A 220uF (or bigger) electrolytic capacitor for bulk decoupling.  You
  must choose one that fits in the clock case.  I used a nice Nichicon.
  The edge of the board has a bunch of holes: one ground and several 3.3V.
  Solder the cap there (through-hole).
