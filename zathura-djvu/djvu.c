/* SPDX-License-Identifier: Zlib */

#include <stdlib.h>
#include <ctype.h>
#include <girara/datastructures.h>
#include <string.h>
#include <libdjvu/miniexp.h>
#include <glib.h>

#include "djvu.h"
#include "page-text.h"
#include "internal.h"

/* forward declarations */
static const char* get_extension(const char* path);
static void build_index(djvu_document_t* djvu_document, miniexp_t expression, girara_tree_node_t* root);
static bool exp_to_str(miniexp_t expression, const char** string);
static bool exp_to_int(miniexp_t expression, int* integer);
static bool exp_to_rect(miniexp_t expression, zathura_rectangle_t* rect);

ZATHURA_PLUGIN_REGISTER_WITH_FUNCTIONS("djvu", VERSION_MAJOR, VERSION_MINOR, VERSION_REV,
                                       ZATHURA_PLUGIN_FUNCTIONS({
                                           .document_open           = djvu_document_open,
                                           .document_free           = djvu_document_free,
                                           .document_index_generate = djvu_document_index_generate,
                                           .document_save_as        = djvu_document_save_as,
                                           .page_init               = djvu_page_init,
                                           .page_clear              = djvu_page_clear,
                                           .page_search_text        = djvu_page_search_text,
                                           .page_get_text           = djvu_page_get_text,
                                           .page_get_selection      = djvu_page_get_selection,
                                           .page_links_get          = djvu_page_links_get,
                                           .page_render_cairo       = djvu_page_render_cairo,
                                       }),
                                       ZATHURA_PLUGIN_MIMETYPES({
                                           "image/vnd.djvu",
                                           "image/vnd.djvu+multipage",
                                       }))

zathura_error_t djvu_document_open(zathura_document_t* document) {
  zathura_error_t error = ZATHURA_ERROR_OK;

  if (document == NULL) {
    error = ZATHURA_ERROR_INVALID_ARGUMENTS;
    goto error_out;
  }

  djvu_document_t* djvu_document = calloc(1, sizeof(djvu_document_t));
  if (djvu_document == NULL) {
    error = ZATHURA_ERROR_OUT_OF_MEMORY;
    goto error_out;
  }

  /* setup format */
  unsigned int masks[4] = {
      0x00FF0000,
      0x0000FF00,
      0x000000FF,
      0xFF000000,
  };

  djvu_document->format = ddjvu_format_create(DDJVU_FORMAT_RGBMASK32, 4, masks);
  if (djvu_document->format == NULL) {
    error = ZATHURA_ERROR_UNKNOWN;
    goto error_free;
  }

  ddjvu_format_set_row_order(djvu_document->format, TRUE);

  /* setup context */
  djvu_document->context = ddjvu_context_create("zathura");

  if (djvu_document->context == NULL) {
    error = ZATHURA_ERROR_UNKNOWN;
    goto error_free;
  }

  /* setup document */
  djvu_document->document =
      ddjvu_document_create_by_filename(djvu_document->context, zathura_document_get_path(document), FALSE);

  if (djvu_document->document == NULL) {
    error = ZATHURA_ERROR_UNKNOWN;
    goto error_free;
  }

  /* load document info */
  ddjvu_message_t* msg;
  ddjvu_message_wait(djvu_document->context);

  while ((msg = ddjvu_message_peek(djvu_document->context)) && (msg->m_any.tag != DDJVU_DOCINFO)) {
    if (msg->m_any.tag == DDJVU_ERROR) {
      error = ZATHURA_ERROR_UNKNOWN;
      goto error_free;
    }

    ddjvu_message_pop(djvu_document->context);
  }

  /* decoding error */
  if (ddjvu_document_decoding_error(djvu_document->document)) {
    handle_messages(djvu_document, true);
    error = ZATHURA_ERROR_UNKNOWN;
    goto error_free;
  }

  zathura_document_set_data(document, djvu_document);
  zathura_document_set_number_of_pages(document, ddjvu_document_get_pagenum(djvu_document->document));

  return error;

error_free:

  if (djvu_document->format != NULL) {
    ddjvu_format_release(djvu_document->format);
  }

  if (djvu_document->context != NULL) {
    ddjvu_context_release(djvu_document->context);
  }

  free(djvu_document);

error_out:

  return error;
}

zathura_error_t djvu_document_free(zathura_document_t* document, void* data) {
  djvu_document_t* djvu_document = data;
  if (document == NULL) {
    return ZATHURA_ERROR_INVALID_ARGUMENTS;
  }

  if (djvu_document != NULL) {
    ddjvu_context_release(djvu_document->context);
    ddjvu_document_release(djvu_document->document);
    ddjvu_format_release(djvu_document->format);
    free(djvu_document);
  }

  return ZATHURA_ERROR_OK;
}

girara_tree_node_t* djvu_document_index_generate(zathura_document_t* document, void* data, zathura_error_t* error) {
  djvu_document_t* djvu_document = data;
  if (document == NULL || djvu_document == NULL) {
    if (error != NULL) {
      *error = ZATHURA_ERROR_INVALID_ARGUMENTS;
    }
    return NULL;
  }

  miniexp_t outline = miniexp_dummy;
  while ((outline = ddjvu_document_get_outline(djvu_document->document)) == miniexp_dummy) {
    handle_messages(djvu_document, true);
  }

  if (outline == miniexp_dummy) {
    return NULL;
  }

  if (miniexp_consp(outline) == 0 || miniexp_car(outline) != miniexp_symbol("bookmarks")) {
    ddjvu_miniexp_release(djvu_document->document, outline);
    return NULL;
  }

  girara_tree_node_t* root = girara_node_new(zathura_index_element_new("ROOT"));
  build_index(djvu_document, miniexp_cdr(outline), root);

  ddjvu_miniexp_release(djvu_document->document, outline);

  return root;
}

zathura_error_t djvu_document_save_as(zathura_document_t* document, void* data, const char* path) {
  djvu_document_t* djvu_document = data;
  if (document == NULL || djvu_document == NULL || path == NULL) {
    return ZATHURA_ERROR_INVALID_ARGUMENTS;
  }

  FILE* fp = fopen(path, "w");
  if (fp == NULL) {
    return ZATHURA_ERROR_UNKNOWN;
  }

  const char* extension = get_extension(path);

  ddjvu_job_t* job = NULL;
  if (extension != NULL && g_strcmp0(extension, "ps") == 0) {
    job = ddjvu_document_print(djvu_document->document, fp, 0, NULL);
  } else {
    job = ddjvu_document_save(djvu_document->document, fp, 0, NULL);
  }
  while (ddjvu_job_done(job) != true) {
    handle_messages(djvu_document, true);
  }

  fclose(fp);

  return ZATHURA_ERROR_OK;
}

zathura_error_t djvu_page_init(zathura_page_t* page) {
  if (page == NULL) {
    return ZATHURA_ERROR_INVALID_ARGUMENTS;
  }

  zathura_document_t* document   = zathura_page_get_document(page);
  djvu_document_t* djvu_document = zathura_document_get_data(document);

  ddjvu_status_t status;
  ddjvu_pageinfo_t page_info;

  unsigned int index = zathura_page_get_index(page);
  while ((status = ddjvu_document_get_pageinfo(djvu_document->document, index, &page_info)) < DDJVU_JOB_OK) {
    handle_messages(djvu_document, true);
  }

  if (status >= DDJVU_JOB_FAILED) {
    handle_messages(djvu_document, true);
    return ZATHURA_ERROR_UNKNOWN;
  }

  zathura_page_set_width(page, ZATHURA_DJVU_SCALE * page_info.width);
  zathura_page_set_height(page, ZATHURA_DJVU_SCALE * page_info.height);

  return ZATHURA_ERROR_OK;
}

zathura_error_t djvu_page_clear(zathura_page_t* page, void* UNUSED(data)) {
  if (page == NULL) {
    return ZATHURA_ERROR_INVALID_ARGUMENTS;
  }

  return ZATHURA_ERROR_OK;
}

girara_list_t* djvu_page_search_text(zathura_page_t* page, void* UNUSED(data), const char* text,
                                     zathura_error_t* error) {
  if (page == NULL || text == NULL || strlen(text) == 0) {
    if (error != NULL) {
      *error = ZATHURA_ERROR_INVALID_ARGUMENTS;
    }
    goto error_ret;
  }

  zathura_document_t* document = zathura_page_get_document(page);
  if (document == NULL) {
    goto error_ret;
  }

  djvu_document_t* djvu_document = zathura_document_get_data(document);

  djvu_page_text_t* page_text = djvu_page_text_new(djvu_document, page);
  if (page_text == NULL) {
    goto error_ret;
  }

  girara_list_t* results = djvu_page_text_search(page_text, text);
  if (results == NULL) {
    goto error_free;
  }

  djvu_page_text_free(page_text);

  return results;

error_free:

  if (page_text != NULL) {
    djvu_page_text_free(page_text);
  }

error_ret:

  if (error != NULL && *error == ZATHURA_ERROR_OK) {
    *error = ZATHURA_ERROR_UNKNOWN;
  }

  return NULL;
}

char* djvu_page_get_text(zathura_page_t* page, void* UNUSED(data), zathura_rectangle_t rectangle,
                         zathura_error_t* error) {
  if (page == NULL) {
    if (error != NULL) {
      *error = ZATHURA_ERROR_INVALID_ARGUMENTS;
    }
    goto error_ret;
  }

  zathura_document_t* document = zathura_page_get_document(page);
  if (document == NULL) {
    goto error_ret;
  }

  djvu_document_t* djvu_document = zathura_document_get_data(document);

  djvu_page_text_t* page_text = djvu_page_text_new(djvu_document, page);
  if (page_text == NULL) {
    goto error_ret;
  }

  double tmp         = 0;
  double page_height = zathura_page_get_height(page);
  double page_width  = zathura_page_get_width(page);

  switch (zathura_document_get_rotation(document)) {
  case 90:
    tmp          = rectangle.x1;
    rectangle.x1 = rectangle.y1;
    rectangle.y1 = tmp;
    tmp          = rectangle.x2;
    rectangle.x2 = rectangle.y2;
    rectangle.y2 = tmp;
    break;
  case 180:
    tmp          = rectangle.x1;
    rectangle.x1 = (page_width - rectangle.x2);
    rectangle.x2 = (page_width - tmp);
    break;
  case 270:
    tmp          = rectangle.y2;
    rectangle.y2 = (page_height - rectangle.x1);
    rectangle.x1 = (page_width - tmp);
    tmp          = rectangle.y1;
    rectangle.y1 = (page_height - rectangle.x2);
    rectangle.x2 = (page_width - tmp);
    break;
  default:
    tmp          = rectangle.y1;
    rectangle.y1 = (page_height - rectangle.y2);
    rectangle.y2 = (page_height - tmp);
    break;
  }

  /* adjust to scale */
  rectangle.x1 /= ZATHURA_DJVU_SCALE;
  rectangle.x2 /= ZATHURA_DJVU_SCALE;
  rectangle.y1 /= ZATHURA_DJVU_SCALE;
  rectangle.y2 /= ZATHURA_DJVU_SCALE;

  char* text = djvu_page_text_select(page_text, rectangle);

  djvu_page_text_free(page_text);

  return text;

error_ret:

  if (error != NULL && *error == ZATHURA_ERROR_OK) {
    *error = ZATHURA_ERROR_UNKNOWN;
  }

  return NULL;
}

girara_list_t* djvu_page_get_selection(zathura_page_t* UNUSED(page), void* UNUSED(data), zathura_rectangle_t rectangle,
                                       zathura_error_t* error) {
  girara_list_t* list = girara_list_new_with_free(g_free);
  if (list == NULL) {
    if (error != NULL) {
      *error = ZATHURA_ERROR_OUT_OF_MEMORY;
    }
    goto error_free;
  }

  zathura_rectangle_t* rect = g_malloc0(sizeof(zathura_rectangle_t));
  *rect                     = rectangle;
  girara_list_append(list, rect);

  return list;

error_free:
  if (list != NULL) {
    girara_list_free(list);
  }
  return NULL;
}

girara_list_t* djvu_page_links_get(zathura_page_t* page, void* UNUSED(data), zathura_error_t* error) {
  if (page == NULL) {
    if (error != NULL) {
      *error = ZATHURA_ERROR_INVALID_ARGUMENTS;
    }
    goto error_ret;
  }

  zathura_document_t* document = zathura_page_get_document(page);
  if (document == NULL) {
    goto error_ret;
  }

  girara_list_t* list = girara_list_new_with_free((girara_free_function_t)zathura_link_free);
  if (list == NULL) {
    if (error != NULL) {
      *error = ZATHURA_ERROR_OUT_OF_MEMORY;
    }
    goto error_ret;
  }

  djvu_document_t* djvu_document = zathura_document_get_data(document);

  miniexp_t annotations = miniexp_nil;
  while ((annotations = ddjvu_document_get_pageanno(djvu_document->document, zathura_page_get_index(page))) ==
         miniexp_dummy) {
    handle_messages(djvu_document, true);
  }

  if (annotations == miniexp_nil) {
    goto error_free;
  }

  miniexp_t* hyperlinks = ddjvu_anno_get_hyperlinks(annotations);
  for (miniexp_t* iter = hyperlinks; *iter != NULL; iter++) {
    if (miniexp_car(*iter) != miniexp_symbol("maparea")) {
      continue;
    }

    miniexp_t inner = miniexp_cdr(*iter);

    /* extract url information */
    const char* target_string = NULL;

    if (miniexp_caar(inner) == miniexp_symbol("url")) {
      if (exp_to_str(miniexp_caddr(miniexp_car(inner)), &target_string) == false) {
        continue;
      }
    } else {
      if (exp_to_str(miniexp_car(inner), &target_string) == false) {
        continue;
      }
    }

    /* skip comment */
    inner = miniexp_cdr(inner);

    /* extract link area */
    inner = miniexp_cdr(inner);

    zathura_rectangle_t rect = {0, 0, 0, 0};
    if (exp_to_rect(miniexp_car(inner), &rect) == false) {
      continue;
    }

    /* update rect */
    unsigned int page_height = zathura_page_get_height(page) / ZATHURA_DJVU_SCALE;
    rect.x1                  = rect.x1 * ZATHURA_DJVU_SCALE;
    rect.x2                  = rect.x2 * ZATHURA_DJVU_SCALE;
    double tmp               = rect.y1;
    rect.y1                  = (page_height - rect.y2) * ZATHURA_DJVU_SCALE;
    rect.y2                  = (page_height - tmp) * ZATHURA_DJVU_SCALE;

    /* create zathura link */
    zathura_link_type_t type     = ZATHURA_LINK_INVALID;
    zathura_link_target_t target = {ZATHURA_LINK_DESTINATION_UNKNOWN, NULL, 0, -1, -1, -1, -1, 0};
    ;

    /* goto page */
    if (target_string[0] == '#' && target_string[1] == 'p') {
      type               = ZATHURA_LINK_GOTO_DEST;
      target.page_number = atoi(target_string + 2) - 1;
      /* url or other? */
    } else if (strstr(target_string, "//") != NULL) {
      type         = ZATHURA_LINK_URI;
      target.value = (char*)target_string;
      /* TODO: Parse all different links */
    } else {
      continue;
    }

    zathura_link_t* link = zathura_link_new(type, rect, target);
    if (link != NULL) {
      girara_list_append(list, link);
    }
  }

  return list;

error_free:

  if (list != NULL) {
    girara_list_free(list);
  }

error_ret:

  return NULL;
}

zathura_error_t djvu_page_render_cairo(zathura_page_t* page, void* UNUSED(data), cairo_t* cairo,
                                       bool GIRARA_UNUSED(printing)) {
  if (page == NULL || cairo == NULL) {
    return ZATHURA_ERROR_INVALID_ARGUMENTS;
  }

  zathura_document_t* document = zathura_page_get_document(page);
  if (document == NULL) {
    return ZATHURA_ERROR_UNKNOWN;
  }

  /* init ddjvu render data */
  djvu_document_t* djvu_document = zathura_document_get_data(document);
  ddjvu_page_t* djvu_page        = ddjvu_page_create_by_pageno(djvu_document->document, zathura_page_get_index(page));

  if (djvu_page == NULL) {
    return ZATHURA_ERROR_UNKNOWN;
  }

  while (!ddjvu_page_decoding_done(djvu_page)) {
    handle_messages(djvu_document, true);
  }

  cairo_surface_t* surface = cairo_get_target(cairo);

  if (surface == NULL || cairo_surface_status(surface) != CAIRO_STATUS_SUCCESS ||
      cairo_surface_get_type(surface) != CAIRO_SURFACE_TYPE_IMAGE) {
    ddjvu_page_release(djvu_page);
    return ZATHURA_ERROR_UNKNOWN;
  }

  unsigned int page_width  = cairo_image_surface_get_width(surface);
  unsigned int page_height = cairo_image_surface_get_height(surface);

  ddjvu_rect_t rrect = {0, 0, page_width, page_height};
  ddjvu_rect_t prect = {0, 0, page_width, page_height};

  char* surface_data = (char*)cairo_image_surface_get_data(surface);

  if (surface_data == NULL) {
    ddjvu_page_release(djvu_page);
    return ZATHURA_ERROR_UNKNOWN;
  }

  /* render page */
  ddjvu_page_render(djvu_page, DDJVU_RENDER_COLOR, &prect, &rrect, djvu_document->format,
                    cairo_image_surface_get_stride(surface), surface_data);

  ddjvu_page_release(djvu_page);

  return ZATHURA_ERROR_OK;
}

static const char* get_extension(const char* path) {
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

void handle_messages(djvu_document_t* document, bool wait) {
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

static void build_index(djvu_document_t* djvu_document, miniexp_t expression, girara_tree_node_t* root) {
  if (expression == miniexp_nil || root == NULL) {
    return;
  }

  int fileno  = ddjvu_document_get_filenum(djvu_document->document);
  int curfile = 0;

  while (miniexp_consp(expression) != 0) {
    miniexp_t inner = miniexp_car(expression);

    if (miniexp_consp(inner) && miniexp_consp(miniexp_cdr(inner)) && miniexp_stringp(miniexp_car(inner)) &&
        miniexp_stringp(miniexp_car(inner))) {
      const char* name = miniexp_to_str(miniexp_car(inner));
      const char* link = miniexp_to_str(miniexp_cadr(inner));

      /* TODO: handle other links? */
      if (link == NULL || link[0] != '#') {
        expression = miniexp_cdr(expression);
        continue;
      }

      zathura_link_type_t type     = ZATHURA_LINK_GOTO_DEST;
      zathura_rectangle_t rect     = {0};
      zathura_link_target_t target = {0};
      target.destination_type      = ZATHURA_LINK_DESTINATION_XYZ;

      /* Check if link+1 contains a number */
      bool number          = true;
      const size_t linklen = strlen(link);
      for (unsigned int k = 1; k < linklen; k++) {
        if (!isdigit(link[k])) {
          number = false;
          break;
        }
      }

      /* if link starts with a number assume it is a number */
      if (number == true) {
        target.page_number = atoi(link + 1) - 1;
      } else {
        /* otherwise assume it is an id for a page */
        ddjvu_fileinfo_t info;
        int f, i;
        for (i = 0; i < fileno; i++) {
          f = (curfile + i) % fileno;
          ddjvu_document_get_fileinfo(djvu_document->document, f, &info);
          if (info.id != NULL && !strcmp(link + 1, info.id)) {
            break;
          }
        }

        /* got a page */
        if (i < fileno && info.pageno >= 0) {
          curfile            = (f + 1) % fileno;
          target.page_number = info.pageno;
        } else {
          /* give up */
          expression = miniexp_cdr(expression);
          continue;
        }
      }

      zathura_index_element_t* index_element = zathura_index_element_new(name);
      if (index_element == NULL) {
        continue;
      }

      index_element->link = zathura_link_new(type, rect, target);
      if (index_element->link == NULL) {
        zathura_index_element_free(index_element);
        continue;
      }

      girara_tree_node_t* node = girara_node_append_data(root, index_element);

      /* search recursive */
      build_index(djvu_document, miniexp_cddr(inner), node);
    }

    expression = miniexp_cdr(expression);
  }
}

static bool exp_to_str(miniexp_t expression, const char** string) {
  if (string == NULL) {
    return false;
  }

  if (miniexp_stringp(expression)) {
    *string = miniexp_to_str(expression);
    return true;
  }

  return false;
}

static bool exp_to_int(miniexp_t expression, int* integer) {
  if (integer == NULL) {
    return false;
  }

  if (miniexp_numberp(expression)) {
    *integer = miniexp_to_int(expression);
    return true;
  }

  return false;
}

static bool exp_to_rect(miniexp_t expression, zathura_rectangle_t* rect) {
  if ((miniexp_car(expression) == miniexp_symbol("rect") || miniexp_car(expression) == miniexp_symbol("oval")) &&
      miniexp_length(expression) == 5) {
    int min_x  = 0;
    int min_y  = 0;
    int width  = 0;
    int height = 0;

    miniexp_t iter = miniexp_cdr(expression);
    if (exp_to_int(miniexp_car(iter), &min_x) == false) {
      return false;
    }
    iter = miniexp_cdr(iter);
    if (exp_to_int(miniexp_car(iter), &min_y) == false) {
      return false;
    }
    iter = miniexp_cdr(iter);
    if (exp_to_int(miniexp_car(iter), &width) == false) {
      return false;
    }
    iter = miniexp_cdr(iter);
    if (exp_to_int(miniexp_car(iter), &height) == false) {
      return false;
    }

    rect->x1 = min_x;
    rect->x2 = min_x + width;
    rect->y1 = min_y;
    rect->y2 = min_y + height;
  } else if (miniexp_car(expression) == miniexp_symbol("poly") && miniexp_length(expression) >= 5) {
    int min_x = 0;
    int min_y = 0;
    int max_x = 0;
    int max_y = 0;

    miniexp_t iter = miniexp_cdr(expression);
    while (iter != miniexp_nil) {
      int x = 0;
      int y = 0;

      if (exp_to_int(miniexp_car(iter), &x) == false) {
        return false;
      }
      iter = miniexp_cdr(iter);
      if (exp_to_int(miniexp_car(iter), &y) == false) {
        return false;
      }
      iter = miniexp_cdr(iter);

      min_x = MIN(min_x, x);
      min_y = MIN(min_y, y);
      max_x = MAX(max_x, x);
      max_y = MAX(max_y, y);
    }

    rect->x1 = min_x;
    rect->x2 = max_x;
    rect->y1 = min_y;
    rect->y2 = max_y;
  }

  return true;
}
