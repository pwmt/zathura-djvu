/* See LICENSE file for license and copyright information */

#include <stdlib.h>

#include "djvu.h"
#include "../../zathura.h"

void
plugin_register(zathura_document_plugin_t* plugin)
{
  plugin->file_extension = "djvu";
  plugin->open_function  = djvu_document_open;
}

bool
djvu_document_open(zathura_document_t* document)
{
  if (!document) {
    goto error_out;
  }

  document->functions.document_free             = djvu_document_free;
  document->functions.document_index_generate   = djvu_document_index_generate;;
  document->functions.document_save_as          = djvu_document_save_as;
  document->functions.document_attachments_get  = djvu_document_attachments_get;
  document->functions.page_get                  = djvu_page_get;
  document->functions.page_search_text          = djvu_page_search_text;
  document->functions.page_links_get            = djvu_page_links_get;
  document->functions.page_form_fields_get      = djvu_page_form_fields_get;
  document->functions.page_render               = djvu_page_render;
  document->functions.page_free                 = djvu_page_free;

  document->data = malloc(sizeof(djvu_document_t));
  if (!document->data) {
    goto error_out;
  }

  djvu_document_t* djvu_document = (djvu_document_t*) document->data;
  djvu_document->context  = NULL;
  djvu_document->document = NULL;
  djvu_document->format   = NULL;

  /* setup format */
  djvu_document->format = ddjvu_format_create(DDJVU_FORMAT_RGB24, 0, NULL);

  if (!djvu_document->format) {
    goto error_free;
  }

  ddjvu_format_set_row_order(djvu_document->format, TRUE);

  /* setup context */
  djvu_document->context = ddjvu_context_create("zathura");

  if (!djvu_document->context) {
    goto error_free;
  }

  /* setup document */
  djvu_document->document = ddjvu_document_create_by_filename(djvu_document->context, document->file_path, FALSE);

  if (!djvu_document->document) {
    goto error_free;
  }

  /* load document info */
  ddjvu_message_t* msg;
  ddjvu_message_wait(djvu_document->context);

  while ((msg = ddjvu_message_peek(djvu_document->context)) && (msg->m_any.tag != DDJVU_DOCINFO)) {
    if (msg->m_any.tag == DDJVU_ERROR) {
      goto error_free;
    }

    ddjvu_message_pop(djvu_document->context);
  }

  /* decoding error */
  if (ddjvu_document_decoding_error(djvu_document->document)) {
    goto error_free;
  }

  document->number_of_pages = ddjvu_document_get_pagenum(djvu_document->document);

  return true;

error_free:

  if (djvu_document->format) {
    ddjvu_format_release(djvu_document->format);
  }

  if (djvu_document->context) {
    ddjvu_context_release(djvu_document->context);
  }

  free(document->data);
  document->data = NULL;

error_out:

  return false;
}

bool
djvu_document_free(zathura_document_t* document)
{
  if (!document) {
    return false;
  }

  if (document->data) {
    djvu_document_t* djvu_document = (djvu_document_t*) document->data;
    ddjvu_context_release(djvu_document->context);
    ddjvu_document_release(djvu_document->document);
    ddjvu_format_release(djvu_document->format);
    free(document->data);
  }

  return true;
}

girara_tree_node_t*
djvu_document_index_generate(zathura_document_t* document)
{
  return NULL;
}

bool
djvu_document_save_as(zathura_document_t* document, const char* path)
{
  if (!document || !document->data || !path) {
    return false;
  }

  djvu_document_t* djvu_document = (djvu_document_t*) document->data;

  FILE* fp = fopen(path, "w");
  if (!fp) {
    return false;
  }

  ddjvu_document_save(djvu_document->document, fp, 0, NULL);
  fclose(fp);

  return true;
}

zathura_list_t*
djvu_document_attachments_get(zathura_document_t* document)
{
  return NULL;
}

zathura_page_t*
djvu_page_get(zathura_document_t* document, unsigned int page)
{
  if (!document || !document->data) {
    return NULL;
  }

  djvu_document_t* djvu_document  = (djvu_document_t*) document->data;
  zathura_page_t* document_page = malloc(sizeof(zathura_page_t));

  if (!document_page) {
    return NULL;
  }

  document_page->document = document;
  document_page->data     = NULL;

  ddjvu_status_t status;
  ddjvu_pageinfo_t page_info;

  while ((status = ddjvu_document_get_pageinfo(djvu_document->document, page, &page_info)) < DDJVU_JOB_OK);

  if (status >= DDJVU_JOB_FAILED) {
    free(document_page);
    return NULL;
  }

  document_page->width  = 0.2 * page_info.width;
  document_page->height = 0.2 * page_info.height;

  return document_page;
}

bool
djvu_page_free(zathura_page_t* page)
{
  if (!page) {
    return false;
  }

  free(page);

  return true;
}

zathura_list_t*
djvu_page_search_text(zathura_page_t* page, const char* text)
{
  return NULL;
}

zathura_list_t*
djvu_page_links_get(zathura_page_t* page)
{
  return NULL;
}

zathura_list_t*
djvu_page_form_fields_get(zathura_page_t* page)
{
  return NULL;
}

zathura_image_buffer_t*
djvu_page_render(zathura_page_t* page)
{
  if (!Zathura.document || !page || !page->document) {
    return NULL;
  }

  /* calculate sizes */
  unsigned int page_width  = Zathura.document->scale * page->width;
  unsigned int page_height = Zathura.document->scale * page->height;

  if (!page_width || !page_height) {
    goto error_out;
  }

  /* init ddjvu render data */
  djvu_document_t* djvu_document = (djvu_document_t*) page->document->data;
  ddjvu_page_t* djvu_page        = ddjvu_page_create_by_pageno(djvu_document->document, page->number);

  if (!djvu_page) {
    goto error_out;
  }

  while (!ddjvu_page_decoding_done(djvu_page));

  ddjvu_rect_t rrect = { 0, 0, page_width, page_height };
  ddjvu_rect_t prect = { 0, 0, page_width, page_height };

  guchar* buffer = malloc(sizeof(char) * (page_width * page_height * 3));
  if (!buffer) {
    goto error_free;
  }

  /* set rotation */
  GdkPixbufRotation gdk_angle       = GDK_PIXBUF_ROTATE_NONE;
  ddjvu_page_rotation_t ddjvu_angle = DDJVU_ROTATE_0;

  switch(Zathura.document->rotate) {
    case 90:
      gdk_angle   = GDK_PIXBUF_ROTATE_CLOCKWISE;
      ddjvu_angle = DDJVU_ROTATE_90;
      break;
    case 180:
      gdk_angle   = GDK_PIXBUF_ROTATE_UPSIDEDOWN;
      ddjvu_angle = DDJVU_ROTATE_180;
      break;
    case 270:
      gdk_angle   = GDK_PIXBUF_ROTATE_COUNTERCLOCKWISE;
      ddjvu_angle = DDJVU_ROTATE_270;
      break;
    default:
      gdk_angle       = GDK_PIXBUF_ROTATE_NONE;
      ddjvu_angle = DDJVU_ROTATE_0;
      break;
  }

  ddjvu_page_set_rotation(djvu_page, ddjvu_angle);

  /* render page */
  ddjvu_page_render(djvu_page, DDJVU_RENDER_COLOR, &prect, &rrect, djvu_document->format,
      3 * page_width, (char*) buffer);

  /* create pixbuf */
  GdkPixbuf* pixbuf = gdk_pixbuf_new_from_data(buffer, GDK_COLORSPACE_RGB, FALSE, 8,
      page_width, page_height, 3 * page_width, NULL, NULL);

  if (!pixbuf) {
    goto error_free;
  }

  /* rotate page */
  if (Zathura.document->rotate != 0) {
    GdkPixbuf* pixbuf_temp = gdk_pixbuf_rotate_simple(pixbuf, gdk_angle);
    if (!pixbuf_temp) {
      goto error_free;
    }

    pixbuf = pixbuf_temp;
  }

  GtkWidget* image = gtk_image_new();

  if (!image) {
    goto error_free;
  }

  gtk_image_set_from_pixbuf(GTK_IMAGE(image), pixbuf);
  gtk_widget_show(image);

  ddjvu_page_release(djvu_page);

  return image;

error_free:

    ddjvu_page_release(djvu_page);
    free(buffer);
    g_object_unref(pixbuf);

error_out:

  return NULL;
}
