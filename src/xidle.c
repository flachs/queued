/*

This routine returns the "idle time" of the X user.  The "idle
time" is the number of milliseconds since input was received on any
input device.  If unsuccessful, the program prints a message to stderr
and exits with a non-zero exit code.

Copyright (c) 2005, 2008 Magnus Henoch <henoch@dtek.chalmers.se>
Copyright (c) 2006, 2007 by Danny Kukawka
                         <dkukawka@suse.de>, <danny.kukawka@web.de>
Copyright (c) 2008 Eivind Magnus Hvidevold <hvidevold@gmail.com>

This program is free software; you can redistribute it and/or modify
it under the terms of version 2 of the GNU General Public License
as published by the Free Software Foundation.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program; if not, write to the
Free Software Foundation, Inc.,
51 Franklin Street, Fifth Floor, Boston, MA 02110-1301, USA.

The function workaroundCreepyXServer was adapted from kpowersave-0.7.3 by
Eivind Magnus Hvidevold <hvidevold@gmail.com>. kpowersave is licensed under
the GNU GPL, version 2 _only_.

*/

#include <X11/Xlib.h>
#include <X11/extensions/dpms.h>
#include <X11/extensions/scrnsaver.h>
#include <stdio.h>

/*!
 * This function works around an XServer idleTime bug in the
 * XScreenSaverExtension if dpms is running. In this case the current
 * dpms-state time is always subtracted from the current idletime.
 * This means: XScreenSaverInfo->idle is not the time since the last
 * user activity, as descriped in the header file of the extension.
 * This result in SUSE bug # and sf.net bug #. The bug in the XServer itself
 * is reported at https://bugs.freedesktop.org/buglist.cgi?quicksearch=6439.
 *
 * Workaround: Check if if XServer is in a dpms state, check the 
 *             current timeout for this state and add this value to 
 *             the current idle time and return.
 *
 * \param _idleTime a unsigned long value with the current idletime from
 *                  XScreenSaverInfo->idle
 * \return a unsigned long with the corrected idletime
 */
unsigned long workaroundCreepyXServer(Display *dpy,
                                      unsigned long _idleTime )
  {
  int dummy;
  if (DPMSQueryExtension(dpy, &dummy, &dummy))
    {
    if (DPMSCapable(dpy))
      {
      CARD16 standby, suspend, off;
      DPMSGetTimeouts(dpy, &standby, &suspend, &off);

      CARD16 state;
      BOOL onoff;
      DPMSInfo(dpy, &state, &onoff);

      if (onoff)
        {
        switch (state)
          {
          case DPMSModeStandby:
            /* this check is a littlebit paranoid, but be sure */
            if (_idleTime < (unsigned) (standby * 1000))
              _idleTime += (standby * 1000);
            break;
          case DPMSModeSuspend:
            if (_idleTime < (unsigned) ((suspend + standby) * 1000))
              _idleTime += ((suspend + standby) * 1000);
            break;
          case DPMSModeOff:
            if (_idleTime < (unsigned) ((off + suspend + standby) * 1000))
              _idleTime += ((off + suspend + standby) * 1000);
            break;
          case DPMSModeOn:
          default:
            break;
          }
        }
      } 
    }

  return _idleTime;
  }

long x_idle()
  {
  int event_basep, error_basep;
  
  Display *dpy = XOpenDisplay(":0");
  if (dpy == NULL)
    {
    fprintf(stderr, "couldn't open display\n");
    return -1;
    }
  
  if (!XScreenSaverQueryExtension(dpy, &event_basep, &error_basep))
    {
    fprintf(stderr, "screen saver extension not supported\n");
    return -2;
    }

  XScreenSaverInfo *ssi = XScreenSaverAllocInfo();
  if (ssi == NULL)
    {
    fprintf(stderr, "couldn't allocate screen saver info\n");
    return -3;
    }   
  
  if (!XScreenSaverQueryInfo(dpy, DefaultRootWindow(dpy), ssi))
    {
    fprintf(stderr, "couldn't query screen saver info\n");
    return -4;
    }

   // milliseconds -> seconds
  long rv = workaroundCreepyXServer(dpy, ssi->idle)/1000;

  XFree(ssi);
  XCloseDisplay(dpy);
  
  return rv;
  }

