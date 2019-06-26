/* GStreamer
 * Copyright (c) 2019 Parrot Drones SAS
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

#include "gstparrotimagemeta.h"

static gboolean
gst_parrot_image_meta_init (GstMeta * meta, gpointer params,
                            GstBuffer * buffer)
{
  GstParrotImageMeta *imeta = (GstParrotImageMeta *) meta;

  imeta->thumbnail = NULL;
  imeta->screennail = NULL;
  imeta->alternative_img = NULL;

  return TRUE;
}

static void
gst_parrot_image_meta_free (GstMeta * meta, GstBuffer * buffer)
{
  GstParrotImageMeta *imeta = (GstParrotImageMeta *) meta;

  if (imeta->thumbnail)
    gst_sample_unref (imeta->thumbnail);
  if (imeta->screennail)
    gst_sample_unref (imeta->screennail);
  if (imeta->alternative_img)
    gst_sample_unref (imeta->alternative_img);
}

static gboolean
gst_parrot_image_meta_transform (GstBuffer * dest, GstMeta * meta,
    GstBuffer * buffer, GQuark type, gpointer data)
{
  GstParrotImageMeta *buffer_meta = (GstParrotImageMeta *) meta;
  GstParrotImageMeta *dest_meta;

  if (GST_META_TRANSFORM_IS_COPY (type)) {
    dest_meta = gst_buffer_add_parrot_image_meta (dest);
    if (dest_meta == NULL)
      return FALSE;

    if (buffer_meta->thumbnail)
      dest_meta->thumbnail = gst_sample_copy (buffer_meta->thumbnail);
    if (buffer_meta->screennail)
      dest_meta->screennail = gst_sample_copy (buffer_meta->screennail);
    if (buffer_meta->alternative_img)
      dest_meta->alternative_img =
          gst_sample_copy (buffer_meta->alternative_img);

    /* not freeing the current buffer's meta as it will be done automatically
     * by the transform element */
  } else {
    return FALSE;
  }

  return TRUE;
}

GType
gst_parrot_image_meta_api_get_type (void)
{
  static volatile GType type = 0;
  static const gchar *tags[] = { NULL };

  if (g_once_init_enter (&type)) {
    GType _type = gst_meta_api_type_register ("GstParrotImageMetaAPI", tags);
    g_once_init_leave (&type, _type);
  }

  return type;
}

const GstMetaInfo *
gst_parrot_image_meta_get_info (void)
{
  static const GstMetaInfo *info = NULL;

  if (g_once_init_enter (&info)) {
    const GstMetaInfo *_info =
        gst_meta_register (GST_PARROT_IMAGE_META_API_TYPE,
            "GstParrotImageMeta", sizeof (GstParrotImageMeta),
            gst_parrot_image_meta_init, gst_parrot_image_meta_free,
            gst_parrot_image_meta_transform);
    g_once_init_leave (&info, _info);
  }

  return info;
}
