gzmzone - Visual ZoneMinder Zone Setting Tester
===============================================

![gzmzone image](http://github.com/cbagwell/gzmzone/blob/master/gzmzone-screenshot.png)

This is a tool that will display a series of jpeg images
from a single directory while running the motion detection
logic used by ZoneMinder and overlaying the image with
green where difference is detected.

The tool is not much more than a quick hack I wiped up
using Gtk+ 3 and glade so that I could see why ZoneMinder
was stopping recording while I could see motion occuring
in the images.

The window will allow you to modify most all settings from
the Zone setup window.  The major exceptions are that there
is no support for Blobs, frame size, or multi-zone support
to see affects of exclusion/inclusion rules.

All my issues with motion detection can easily be seen with out
all those advanced settings.

Compiling
---------

Compiling this requires already having the Gtk3 and glib2
development libraries and header files already installed.

From there, run "make".  There is no support for installing.

Using
-----

When the gzmzone is first ran, you will need to select the
drop down Directory selector and navigate to the directory
of your choice were series of jpeg's are stored.

ZoneMinder stores the series of month events  in a path similar to:

/var/lib/zoneminder/events/1/[year]/[month]/[day]/[hour]/[minute]/[seconds]

The exact path may be different depending on how it was compiled.

One you select a valid directory, you can click on the play button and
start watching for motion detection.  You can change the options and
see their effect in real time.

There are a few options that do not exist in ZoneMinder that need
to be explained.

The first is the checkbox for "Display Reference Image".  By default,
gzmzone displays each new image overlayed in green.  The difference
is calculated by comparing a reference image to the current image.
After motion detection logic is ran, the reference image is slightly
adjusted to be closer to current image so that motion will effectional
stop being detected.  Some times its useful to see what the reference
image currently looks like to better understand why motion is not
being detected.

The option "Test Simple Blend" enables behavior that is not
supported in ZoneMinder.  It simplifies the update to reference image
and only allows 1 level change per pixel for each frame processed.
In my testing, this helps ZoneMinder detect motion better than
its default Blend Percentage routines.

When viewing images, the bottom of the screen displays a status
line.  It displays the percentage of difference detected to help
gauge if your crossing your min/max threshold settings.
