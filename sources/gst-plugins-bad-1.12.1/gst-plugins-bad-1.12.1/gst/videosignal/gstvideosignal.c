/* GStreamer
 * Copyright (C) <2007> Wim Taymans <wim@fluendo.com>
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Library General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Library General Public License for more details.
 *
 * You should have received a copy of the GNU Library General Public
 * License along with this library; if not, write to the
 * Free Software Foundation, Inc., 51 Franklin St, Fifth Floor,
 * Boston, MA 02110-1301, USA.
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "gstvideoanalyse.h"
#include "gstsimplevideomarkdetect.h"
#include "gstsimplevideomark.h"

static gboolean
plugin_init (GstPlugin * plugin)
{
  gboolean res;

  res = gst_element_register (plugin, "videoanalyse", GST_RANK_NONE,
      GST_TYPE_VIDEO_ANALYSE);

  /* FIXME under no circumstances is anyone allowed to revive the
   * element formerly known as simplevideomarkdetect without changing the name
   * first.  XOXO  --ds  */

  res &= gst_element_register (plugin, "simplevideomarkdetect", GST_RANK_NONE,
      GST_TYPE_SIMPLE_VIDEO_MARK_DETECT);

  res &= gst_element_register (plugin, "simplevideomark", GST_RANK_NONE,
      GST_TYPE_SIMPLE_VIDEO_MARK);

  return res;
}

GST_PLUGIN_DEFINE (GST_VERSION_MAJOR,
    GST_VERSION_MINOR,
    videosignal,
    "Various video signal analysers",
    plugin_init, VERSION, GST_LICENSE, GST_PACKAGE_NAME, GST_PACKAGE_ORIGIN);
