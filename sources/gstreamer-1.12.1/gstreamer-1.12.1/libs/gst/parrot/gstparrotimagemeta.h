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

#ifndef __GST_PARROT_IMAGE_META_H__
#define __GST_PARROT_IMAGE_META_H__

#include <gst/gst.h>

G_BEGIN_DECLS

#define GST_BUFFER_POOL_OPTION_PARROT_IMAGE_META "GstBufferPoolOptionParrotImageMeta"

#define GST_PARROT_IMAGE_META_API_TYPE (gst_parrot_image_meta_api_get_type ())
#define GST_PARROT_IMAGE_META_INFO (gst_parrot_image_meta_get_info ())
typedef struct _GstParrotImageMeta GstParrotImageMeta;

/**
 * GstParrotImageMeta:
 *
 * @meta: parent #GstMeta
 * @thumbnail: a #GstSample holding the thumbnail of image
 * @screennail: a #GstSample holding a screennail resolution image
 * @alternative_img: a #GstSample holding an image that can replace the main
 *                   buffer image
 *
 * Additional image frames in different formats
 */
struct _GstParrotImageMeta
{
  GstMeta meta;

  GstSample *thumbnail;
  GstSample *screennail;
  GstSample *alternative_img;
};

GType gst_parrot_image_meta_api_get_type (void);
const GstMetaInfo *gst_parrot_image_meta_get_info (void);

#define gst_buffer_get_parrot_image_meta(buffer) \
    ((GstParrotImageMeta *) gst_buffer_get_meta ((buffer), GST_PARROT_IMAGE_META_API_TYPE))

#define gst_buffer_add_parrot_image_meta(buffer) \
    ((GstParrotImageMeta *) gst_buffer_add_meta ((buffer), GST_PARROT_IMAGE_META_INFO, NULL))

G_END_DECLS

#endif
