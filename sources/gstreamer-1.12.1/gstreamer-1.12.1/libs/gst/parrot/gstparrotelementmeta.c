/* GStreamer
 * Copyright (C) <2015> Aurélien Zanelli <aurelien.zanelli@parrot.com>
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

#include "gstparrotelementmeta.h"

static gboolean
gst_parrot_element_meta_init (GstMeta * meta, gpointer params,
    GstBuffer * buffer)
{
  GstParrotElementMeta *emeta = (GstParrotElementMeta *) meta;

  emeta->structure = gst_structure_new_empty ("element-meta");

  return TRUE;
}

static void
gst_parrot_element_meta_free (GstMeta * meta, GstBuffer * buffer)
{
  GstParrotElementMeta *emeta = (GstParrotElementMeta *) meta;

  gst_structure_free (emeta->structure);
}

static gboolean
gst_parrot_element_meta_transform (GstBuffer * dest, GstMeta * meta,
    GstBuffer * buffer, GQuark type, gpointer data)
{
  GstParrotElementMeta *src_meta = (GstParrotElementMeta *) meta;
  GstParrotElementMeta *dest_meta;

  if (GST_META_TRANSFORM_IS_COPY (type)) {
    /* FIXME: if we assume that GstParrotElementMeta doesn't depend on region,
     * we could safely copy them. */
    dest_meta = gst_buffer_add_parrot_element_meta (dest);
    if (dest_meta == NULL)
      return FALSE;

    /* free allocated dest_meta structure in init method because we will
     * copy it from src */
    gst_structure_free (dest_meta->structure);
    dest_meta->structure = gst_structure_copy (src_meta->structure);
  } else {
    /* transform type not supported */
    return FALSE;
  }

  return TRUE;
}

GType
gst_parrot_element_meta_api_get_type (void)
{
  static volatile GType type = 0;
  static const gchar *tags[] = { NULL };

  if (g_once_init_enter (&type)) {
    GType _type = gst_meta_api_type_register ("GstParrotElementMetaAPI", tags);
    g_once_init_leave (&type, _type);
  }

  return type;
}

const GstMetaInfo *
gst_parrot_element_meta_get_info (void)
{
  static const GstMetaInfo *info = NULL;

  if (g_once_init_enter (&info)) {
    const GstMetaInfo *_info =
        gst_meta_register (GST_PARROT_ELEMENT_META_API_TYPE,
            "GstParrotElementMeta", sizeof (GstParrotElementMeta),
            gst_parrot_element_meta_init, gst_parrot_element_meta_free,
            gst_parrot_element_meta_transform);
    g_once_init_leave (&info, _info);
  }

  return info;
}
