/*
 * GStreamer
 * Copyright (C) 2010  Intel Corporation.
 * Copyright (C) 2012 Matthew Waters <ystreet00@gmail.com>
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
 *
 * Authors:
 *  Matthew Allum
 *  Robert Bragg
 *  Kristian Høgsberg
 */

/* code originally from clutter's wayland backend found here
 * http://git.gnome.org/browse/clutter/tree/clutter/wayland/clutter-event-wayland.c
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <stdint.h>
#include <stdlib.h>
#include <wayland-client.h>

#include "wayland_event_source.h"

static void
sync_callback (void *data, struct wl_callback *callback, uint32_t serial)
{
  gboolean *done = data;

  *done = TRUE;
  wl_callback_destroy (callback);
}

static const struct wl_callback_listener sync_listener = {
  sync_callback
};

/* only thread safe iff called on the same thread @queue is being dispatched on */
gint
gst_gl_wl_display_roundtrip_queue (struct wl_display *display,
    struct wl_event_queue *queue)
{
  struct wl_callback *callback;
  gboolean done = FALSE;
  gint ret = 0;

  if (queue) {
    /* creating a wl_proxy and setting the queue is racy with the dispatching
     * of the default queue */
    while (wl_display_prepare_read_queue (display, queue) != 0) {
      if ((ret = wl_display_dispatch_queue_pending (display, queue)) < 0)
        return ret;
    }
  }
  if (!(callback = wl_display_sync (display))) {
    return -1;
  }
  wl_callback_add_listener (callback, &sync_listener, &done);
  if (queue) {
    wl_proxy_set_queue ((struct wl_proxy *) callback, queue);
    wl_display_cancel_read (display);
    while (!done && ret >= 0)
      ret = wl_display_dispatch_queue (display, queue);
  } else {
    while (!done && ret >= 0)
      ret = wl_display_dispatch (display);
  }

  if (ret == -1 && !done)
    wl_callback_destroy (callback);

  return ret;
}

typedef struct _WaylandEventSource
{
  GSource source;
  GPollFD pfd;
  uint32_t mask;
  struct wl_display *display;
  struct wl_event_queue *queue;
  gboolean reading;
} WaylandEventSource;

static gboolean
wayland_event_source_prepare (GSource * base, gint * timeout)
{
  WaylandEventSource *source = (WaylandEventSource *) base;

  *timeout = -1;

  /* we may be called multiple times for prepare */
  if (source->reading)
    wl_display_cancel_read (source->display);

  if (source->queue) {
    while (wl_display_prepare_read_queue (source->display, source->queue) != 0) {
      if (wl_display_dispatch_queue_pending (source->display,
              source->queue) < 0) {
        g_critical ("Failed to dispatch pending events\n");
      }
    }
  } else {
    while (wl_display_prepare_read (source->display) != 0) {
      if (wl_display_dispatch_pending (source->display) < 0) {
        g_critical ("Failed to dispatch pending events\n");
      }
    }
  }
  source->reading = TRUE;

  /* FIXME: this may return EAGAIN if the fd is full */
  if (wl_display_flush (source->display) < 0)
    g_critical ("Failed to flush Wayland connection\n");

  return FALSE;
}

static gboolean
wayland_event_source_check (GSource * base)
{
  WaylandEventSource *source = (WaylandEventSource *) base;
  gboolean retval;

  retval = source->pfd.revents;

  if (source->pfd.revents & G_IO_IN) {
    wl_display_read_events (source->display);
  } else {
    wl_display_cancel_read (source->display);
  }
  source->reading = FALSE;

  return retval;
}

static gboolean
wayland_event_source_dispatch (GSource * base,
    GSourceFunc callback, gpointer data)
{
  WaylandEventSource *source = (WaylandEventSource *) base;

  if (source->queue)
    wl_display_dispatch_queue_pending (source->display, source->queue);
  else
    wl_display_dispatch_pending (source->display);
  source->pfd.revents = 0;

  if (callback)
    callback (data);

  return TRUE;
}

static void
wayland_event_source_finalize (GSource * base)
{
  WaylandEventSource *source = (WaylandEventSource *) base;

  if (source->reading) {
    wl_display_cancel_read (source->display);
  }
  source->reading = FALSE;
}

static GSourceFuncs wayland_event_source_funcs = {
  wayland_event_source_prepare,
  wayland_event_source_check,
  wayland_event_source_dispatch,
  wayland_event_source_finalize
};

GSource *
wayland_event_source_new (struct wl_display *display,
    struct wl_event_queue *queue)
{
  WaylandEventSource *source;

  source = (WaylandEventSource *)
      g_source_new (&wayland_event_source_funcs, sizeof (WaylandEventSource));
  source->display = display;
  source->queue = queue;
  source->pfd.fd = wl_display_get_fd (display);
  source->pfd.events = G_IO_IN | G_IO_ERR;
  g_source_add_poll (&source->source, &source->pfd);

  return &source->source;
}
