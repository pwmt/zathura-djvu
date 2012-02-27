/* See LICENSE file for license and copyright information */

#include <stdlib.h>
#include <girara/datastructures.h>
#include <string.h>
#include <libdjvu/miniexp.h>

#include "djvu.h"
#include "page-text.h"
#include "internal.h"

/* forward declarations */
static const char* get_extension(const char* path);

void
plugin_register(zathura_document_plugin_t* plugin)
{
  girara_list_append(plugin->content_types, g_content_type_from_mime_type("image/vnd.djvu"));
  plugin->open_function  = djvu_document_open;
}

zathura_plugin_error_t
djvu_document_open(zathura_document_t* document)
{
  zathura_plugin_error_t error = ZATHURA_PLUGIN_ERROR_OK;

  if (document == NULL) {
    error = ZATHURA_PLUGIN_ERROR_INVALID_ARGUMENTS;
    goto error_out;
  }

  document->functions.document_free             = djvu_document_free;
  document->functions.document_save_as          = djvu_document_save_as;
  document->functions.page_get                  = djvu_page_get;
  document->functions.page_search_text          = djvu_page_search_text;
  document->functions.page_render               = djvu_page_render;
#ifdef HAVE_CAIRO
  document->functions.page_render_cairo         = djvu_page_render_cairo;
#endif
  document->functions.page_free                 = djvu_page_free;

  document->data = malloc(sizeof(djvu_document_t));
  if (document->data == NULL) {
    error = ZATHURA_PLUGIN_ERROR_OUT_OF_MEMORY;
    goto error_out;
  }

  djvu_document_t* djvu_document = (djvu_document_t*) document->data;
  djvu_document->context  = NULL;
  djvu_document->document = NULL;
  djvu_document->format   = NULL;

  /* setup format */
  static unsigned int masks[4] = {0x00FF0000, 0x0000FF00, 0x000000FF, 0xFF000000};
  djvu_document->format = ddjvu_format_create(DDJVU_FORMAT_RGBMASK32, 4, masks);

  if (djvu_document->format == NULL) {
    error = ZATHURA_PLUGIN_ERROR_UNKNOWN;
    goto error_free;
  }

  ddjvu_format_set_row_order(djvu_document->format, TRUE);

  /* setup context */
  djvu_document->context = ddjvu_context_create("zathura");

  if (djvu_document->context == NULL) {
    error = ZATHURA_PLUGIN_ERROR_UNKNOWN;
    goto error_free;
  }

  /* setup document */
  djvu_document->document =
    ddjvu_document_create_by_filename(
        djvu_document->context,
        document->file_path,
        FALSE
    );

  if (djvu_document->document == NULL) {
    error = ZATHURA_PLUGIN_ERROR_UNKNOWN;
    goto error_free;
  }

  /* load document info */
  ddjvu_message_t* msg;
  ddjvu_message_wait(djvu_document->context);

  while ((msg = ddjvu_message_peek(djvu_document->context)) &&
         (msg->m_any.tag != DDJVU_DOCINFO)) {
    if (msg->m_any.tag == DDJVU_ERROR) {
      error = ZATHURA_PLUGIN_ERROR_UNKNOWN;
      goto error_free;
    }

    ddjvu_message_pop(djvu_document->context);
  }

  /* decoding error */
  if (ddjvu_document_decoding_error(djvu_document->document)) {
    handle_messages(djvu_document, true);
    error = ZATHURA_PLUGIN_ERROR_UNKNOWN;
    goto error_free;
  }

  document->number_of_pages =
    ddjvu_document_get_pagenum(djvu_document->document);

  return error;

error_free:

  if (djvu_document->format != NULL) {
    ddjvu_format_release(djvu_document->format);
  }

  if (djvu_document->context != NULL) {
    ddjvu_context_release(djvu_document->context);
  }

  free(document->data);
  document->data = NULL;

error_out:

  return error;
}

zathura_plugin_error_t
djvu_document_free(zathura_document_t* document)
{
  if (document == NULL) {
    return ZATHURA_PLUGIN_ERROR_INVALID_ARGUMENTS;
  }

  if (document->data != NULL) {
    djvu_document_t* djvu_document = (djvu_document_t*) document->data;
    ddjvu_context_release(djvu_document->context);
    ddjvu_document_release(djvu_document->document);
    ddjvu_format_release(djvu_document->format);
    free(document->data);
  }

  return ZATHURA_PLUGIN_ERROR_OK;
}

zathura_plugin_error_t
djvu_document_save_as(zathura_document_t* document, const char* path)
{
  if (document == NULL || document->data == NULL || path == NULL) {
    return ZATHURA_PLUGIN_ERROR_INVALID_ARGUMENTS;
  }

  djvu_document_t* djvu_document = (djvu_document_t*) document->data;

  FILE* fp = fopen(path, "w");
  if (fp == NULL) {
    return ZATHURA_PLUGIN_ERROR_UNKNOWN;
  }

  const char* extension = get_extension(path);

  if (extension != NULL && g_strcmp0(extension, "ps") == 0) {
    ddjvu_job_t* job = ddjvu_document_print(djvu_document->document, fp, 0, NULL);
    while (ddjvu_job_done(job) != true) {
      handle_messages(djvu_document, true);
    }
  } else {
    ddjvu_document_save(djvu_document->document, fp, 0, NULL);
  }

  fclose(fp);

  return ZATHURA_PLUGIN_ERROR_OK;
}

zathura_page_t*
djvu_page_get(zathura_document_t* document, unsigned int page, zathura_plugin_error_t* error)
{
  if (document == NULL || document->data == NULL) {
    if (error != NULL) {
      *error = ZATHURA_PLUGIN_ERROR_INVALID_ARGUMENTS;
    }
    return NULL;
  }

  djvu_document_t* djvu_document = (djvu_document_t*) document->data;
  zathura_page_t* document_page  = malloc(sizeof(zathura_page_t));

  if (document_page == NULL) {
    if (error != NULL) {
      *error = ZATHURA_PLUGIN_ERROR_OUT_OF_MEMORY;
    }
    return NULL;
  }

  document_page->document = document;
  document_page->data     = NULL;

  ddjvu_status_t status;
  ddjvu_pageinfo_t page_info;

  while ((status = ddjvu_document_get_pageinfo(djvu_document->document, page,
          &page_info)) < DDJVU_JOB_OK) {
    handle_messages(djvu_document, true);
  }

  if (status >= DDJVU_JOB_FAILED) {
    handle_messages(djvu_document, true);
    free(document_page);

    if (error != NULL) {
      *error = ZATHURA_PLUGIN_ERROR_UNKNOWN;
    }

    return NULL;
  }

  document_page->width  = 0.2 * page_info.width;
  document_page->height = 0.2 * page_info.height;

  return document_page;
}

zathura_plugin_error_t
djvu_page_free(zathura_page_t* page)
{
  if (page == NULL) {
    return ZATHURA_PLUGIN_ERROR_INVALID_ARGUMENTS;
  }

  free(page);

  return ZATHURA_PLUGIN_ERROR_OK;
}

girara_list_t*
djvu_page_search_text(zathura_page_t* page, const char* text, zathura_plugin_error_t* error)
{
  if (page == NULL || text == NULL || strlen(text) == 0
      || page->document->data == NULL) {
    if (error != NULL) {
      *error = ZATHURA_PLUGIN_ERROR_INVALID_ARGUMENTS;
    }
    goto error_ret;
  }

  djvu_document_t* djvu_document = (djvu_document_t*) page->document->data;

  djvu_page_text_t* page_text = djvu_page_text_new(djvu_document, page->number);
  if (page_text == NULL) {
    goto error_ret;
  }

  girara_list_t* results = djvu_page_text_search(page_text, text);
  if (results == NULL) {
    goto error_free;
  }

  djvu_page_text_free(page_text);

  return NULL;

error_free:

  if (page_text != NULL) {
    djvu_page_text_free(page_text);
  }

error_ret:

  if (error != NULL && *error == ZATHURA_PLUGIN_ERROR_OK) {
    *error = ZATHURA_PLUGIN_ERROR_UNKNOWN;
  }

  return NULL;
}

#ifdef HAVE_CAIRO
zathura_plugin_error_t
djvu_page_render_cairo(zathura_page_t* page, cairo_t* cairo, bool GIRARA_UNUSED(printing))
{
  if (page == NULL || page->document == NULL || cairo == NULL) {
    return ZATHURA_PLUGIN_ERROR_INVALID_ARGUMENTS;
  }

  /* init ddjvu render data */
  djvu_document_t* djvu_document = (djvu_document_t*) page->document->data;
  ddjvu_page_t* djvu_page        = ddjvu_page_create_by_pageno(djvu_document->document, page->number);

  if (djvu_page == NULL) {
    return ZATHURA_PLUGIN_ERROR_UNKNOWN;
  }

  while (!ddjvu_page_decoding_done(djvu_page)) {
    handle_messages(djvu_document, true);
  }

  cairo_surface_t* surface = cairo_get_target(cairo);

  if (surface == NULL) {
    ddjvu_page_release(djvu_page);
    return ZATHURA_PLUGIN_ERROR_UNKNOWN;
  }

  unsigned int page_width  = cairo_image_surface_get_width(surface);
  unsigned int page_height = cairo_image_surface_get_height(surface);;

  ddjvu_rect_t rrect = { 0, 0, page_width, page_height };
  ddjvu_rect_t prect = { 0, 0, page_width, page_height };

  char* data = (char*) cairo_image_surface_get_data(surface);

  if (data == NULL) {
    ddjvu_page_release(djvu_page);
    return ZATHURA_PLUGIN_ERROR_UNKNOWN;
  }

  /* render page */
  ddjvu_page_render(djvu_page, DDJVU_RENDER_COLOR, &prect, &rrect,
      djvu_document->format, cairo_image_surface_get_stride(surface), data);

  ddjvu_page_release(djvu_page);

  return ZATHURA_PLUGIN_ERROR_OK;
}
#endif

zathura_image_buffer_t*
djvu_page_render(zathura_page_t* page, zathura_plugin_error_t* error)
{
  if (page == NULL || page->document == NULL) {
    if (error != NULL) {
      *error = ZATHURA_PLUGIN_ERROR_INVALID_ARGUMENTS;
    }
    return NULL;
  }

  /* calculate sizes */
  unsigned int page_width  = page->document->scale * page->width;
  unsigned int page_height = page->document->scale * page->height;

  if (page_width == 0 || page_height == 0) {
    if (error != NULL) {
      *error = ZATHURA_PLUGIN_ERROR_UNKNOWN;
    }
    goto error_out;
  }

  /* init ddjvu render data */
  djvu_document_t* djvu_document = (djvu_document_t*) page->document->data;
  ddjvu_page_t* djvu_page        = ddjvu_page_create_by_pageno(
      djvu_document->document, page->number);

  if (djvu_page == NULL) {
    if (error != NULL) {
      *error = ZATHURA_PLUGIN_ERROR_UNKNOWN;
    }
    goto error_out;
  }

  while (!ddjvu_page_decoding_done(djvu_page)) {
    handle_messages(djvu_document, true);
  }

  ddjvu_rect_t rrect = { 0, 0, page_width, page_height };
  ddjvu_rect_t prect = { 0, 0, page_width, page_height };

  zathura_image_buffer_t* image_buffer =
    zathura_image_buffer_create(page_width, page_height);

  if (image_buffer == NULL) {
    if (error != NULL) {
      *error = ZATHURA_PLUGIN_ERROR_OUT_OF_MEMORY;
    }
    goto error_free;
  }

  /* set rotation */
  ddjvu_page_set_rotation(djvu_page, DDJVU_ROTATE_0);

  /* render page */
  ddjvu_page_render(djvu_page, DDJVU_RENDER_COLOR, &prect, &rrect,
      djvu_document->format, 3 * page_width, (char*) image_buffer->data);

  return image_buffer;

error_free:

    ddjvu_page_release(djvu_page);
    zathura_image_buffer_free(image_buffer);

error_out:

  return NULL;
}

static const char*
get_extension(const char* path)
{
  if (path == NULL) {
    return NULL;
  }

  unsigned int i = strlen(path);
  for (; i > 0; i--) {
    if (*(path + i) != '.') {
      continue;
    } else {
      break;
    }
  }

  if (i == 0) {
    return NULL;
  }

  return path + i + 1;
}

void
handle_messages(djvu_document_t* document, bool wait)
{
  if (document == NULL || document->context == NULL) {
    return;
  }

  ddjvu_context_t* context = document->context;
  const ddjvu_message_t* message;

  if (wait == true) {
    ddjvu_message_wait(context);
  }

  while ((message = ddjvu_message_peek(context)) != NULL) {
    ddjvu_message_pop(context);
  }
}
