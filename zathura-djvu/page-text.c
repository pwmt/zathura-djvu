/* SPDX-License-Identifier: Zlib */

#include <libdjvu/miniexp.h>
#include <string.h>
#include <sys/types.h>
#include <glib.h>

#include "page-text.h"
#include "internal.h"

/**
 * To determine the position of a character
 */
typedef struct text_position_s {
  unsigned int position; /**< Index */
  miniexp_t exp;         /**< Correspondending expression */
} text_position_t;

/* forward declaration */
static void djvu_page_text_content_append(djvu_page_text_t* page_text, miniexp_t exp);
static miniexp_t text_position_get_exp(djvu_page_text_t* page_text, unsigned int index);
static bool djvu_page_text_build_rectangle(djvu_page_text_t* page_text, miniexp_t exp, miniexp_t start, miniexp_t end);
static bool djvu_page_text_build_rectangle_process(djvu_page_text_t* page_text, miniexp_t exp, miniexp_t start,
                                                   miniexp_t end);
static void djvu_page_text_limit(djvu_page_text_t* page_text, miniexp_t exp, zathura_rectangle_t* rectangle);
static void djvu_page_text_limit_process(djvu_page_text_t* page_text, miniexp_t exp, zathura_rectangle_t* rectangle);
static bool djvu_page_text_select_content(djvu_page_text_t* page_text, miniexp_t exp, int delimiter);

djvu_page_text_t* djvu_page_text_new(djvu_document_t* document, zathura_page_t* page) {
  if (document == NULL || document->document == NULL || page == NULL) {
    goto error_ret;
  }

  djvu_page_text_t* page_text = calloc(1, sizeof(djvu_page_text_t));
  if (page_text == NULL) {
    goto error_ret;
  }

  page_text->text_information = miniexp_nil;
  page_text->begin            = miniexp_nil;
  page_text->end              = miniexp_nil;
  page_text->document         = document;
  page_text->page             = page;

  /* read page text */
  while ((page_text->text_information =
              ddjvu_document_get_pagetext(document->document, zathura_page_get_index(page), "char")) == miniexp_dummy) {
    handle_messages(document, true);
  }

  if (page_text->text_information == miniexp_nil) {
    goto error_free;
  }

  return page_text;

error_free:

  if (page_text != NULL) {
    djvu_page_text_free(page_text);
  }

error_ret:

  return NULL;
}

void djvu_page_text_free(djvu_page_text_t* page_text) {
  if (page_text == NULL) {
    return;
  }

  if (page_text->text_information != miniexp_nil && page_text->document != NULL) {
    ddjvu_miniexp_release(page_text->document->document, page_text->text_information);
  }

  if (page_text->content != NULL) {
    g_free(page_text->content);
  }

  if (page_text->text_positions != NULL) {
    girara_list_free(page_text->text_positions);
  }

  if (page_text->rectangle != NULL) {
    free(page_text->rectangle);
  }

  free(page_text);
}

girara_list_t* djvu_page_text_search(djvu_page_text_t* page_text, const char* text) {
  if (page_text == NULL || text == NULL) {
    goto error_ret;
  }

  /* clean and reset */
  if (page_text->content != NULL) {
    g_free(page_text->content);
    page_text->content = NULL;
  }

  if (page_text->text_positions != NULL) {
    girara_list_free(page_text->text_positions);
    page_text->text_positions = NULL;
  }

  /* create result list */
  girara_list_t* results = girara_list_new2((girara_free_function_t)free);

  if (results == NULL) {
    goto error_ret;
  }

  /* create list */
  page_text->text_positions = girara_list_new2((girara_free_function_t)free);

  if (page_text->text_positions == NULL) {
    goto error_free;
  }

  /* get page content */
  djvu_page_text_content_append(page_text, page_text->text_information);

  if (page_text->content == NULL || strlen(page_text->content) == 0) {
    goto error_free;
  }

  /* search through content */
  int search_length = strlen(text);
  char* tmp         = page_text->content;

  while ((tmp = strcasestr(tmp, text)) != NULL) {
    int start_pointer = tmp - page_text->content;
    int end_pointer   = start_pointer + search_length - 1;

    miniexp_t start = text_position_get_exp(page_text, start_pointer);
    miniexp_t end   = text_position_get_exp(page_text, end_pointer);

    /* reset rectangle */
    if (page_text->rectangle != NULL) {
      free(page_text->rectangle);
      page_text->rectangle = NULL;
    }

    djvu_page_text_build_rectangle(page_text, page_text->text_information, start, end);

    if (page_text->rectangle == NULL) {
      tmp += search_length;
      continue;
    }

    /* scale rectangle coordinates */
    page_text->rectangle->x1 = ZATHURA_DJVU_SCALE * page_text->rectangle->x1;
    page_text->rectangle->x2 = ZATHURA_DJVU_SCALE * page_text->rectangle->x2;
    page_text->rectangle->y1 = ZATHURA_DJVU_SCALE * page_text->rectangle->y1;
    page_text->rectangle->y2 = ZATHURA_DJVU_SCALE * page_text->rectangle->y2;

    /* invert */
    int y1                   = zathura_page_get_height(page_text->page) - page_text->rectangle->y1;
    page_text->rectangle->y1 = zathura_page_get_height(page_text->page) - page_text->rectangle->y2;
    page_text->rectangle->y2 = y1;

    /* add rectangle to result list */
    girara_list_append(results, page_text->rectangle);
    page_text->rectangle = NULL;

    tmp += search_length;
  }

  /* clean up */
  girara_list_free(page_text->text_positions);
  page_text->text_positions = NULL;

  if (girara_list_size(results) == 0) {
    girara_list_free(results);
    return NULL;
  }

  return results;

error_free:

  if (results != NULL) {
    girara_list_free(results);
  }

  if (page_text->text_positions != NULL) {
    girara_list_free(page_text->text_positions);
    page_text->text_positions = NULL;
  }

  if (page_text->content != NULL) {
    g_free(page_text->content);
    page_text->content = NULL;
  }

error_ret:

  return NULL;
}

static void djvu_page_text_content_append(djvu_page_text_t* page_text, miniexp_t exp) {
  if (page_text == NULL || exp == miniexp_nil) {
    return;
  }

  if (miniexp_consp(exp) == 0 || miniexp_symbolp(miniexp_car(exp)) == 0) {
    return;
  }

  miniexp_t inner = miniexp_cddr(miniexp_cdddr(exp));
  while (inner != miniexp_nil) {
    miniexp_t data = miniexp_car(inner);

    if (miniexp_stringp(data) != 0) {
      /* create position */
      if (page_text->text_positions != NULL) {
        text_position_t* position = malloc(sizeof(text_position_t));
        if (position == NULL) {
          inner = miniexp_cdr(inner);
          continue;
        }

        position->position = (page_text->content != NULL) ? strlen(page_text->content) : 0;
        position->exp      = exp;

        girara_list_append(page_text->text_positions, position);
      }

      /* append text */
      char* text = (char*)miniexp_to_str(data);

      if (page_text->content == NULL) {
        page_text->content = g_strdup(text);
      } else {
        char* tmp = g_strjoin(" ", page_text->content, text, NULL);
        g_free(page_text->content);
        page_text->content = tmp;
      }
      /* not a string, recursive call */
    } else {
      djvu_page_text_content_append(page_text, data);
    }

    /* move to next object */
    inner = miniexp_cdr(inner);
  }
}

static miniexp_t text_position_get_exp(djvu_page_text_t* page_text, unsigned int index) {
  if (page_text == NULL || page_text->text_positions == NULL) {
    goto error_ret;
  }

  int l = 0;
  int m = 0;
  int h = girara_list_size(page_text->text_positions) - 1;

  if (h < 0) {
    goto error_ret;
  }

  while (l <= h) {
    m = (l + h) >> 1;

    text_position_t* text_position = girara_list_nth(page_text->text_positions, m);
    if (text_position == NULL) {
      goto error_ret;
    }

    if (text_position->position == index) {
      break;
    } else if (text_position->position > index) {
      h = --m;
    } else {
      l = m + 1;
    }
  }

  text_position_t* text_position = girara_list_nth(page_text->text_positions, m);
  if (text_position == NULL) {
    goto error_ret;
  } else {
    return text_position->exp;
  }

error_ret:

  return miniexp_nil;
}

static bool djvu_page_text_build_rectangle_process(djvu_page_text_t* page_text, miniexp_t exp, miniexp_t start,
                                                   miniexp_t end) {
  if (page_text == NULL) {
    goto error_ret;
  }

  if (page_text->rectangle != NULL || exp == start) {
    zathura_rectangle_t* rectangle = calloc(1, sizeof(zathura_rectangle_t));
    if (rectangle == NULL) {
      goto error_ret;
    }

    rectangle->x1 = miniexp_to_int(miniexp_nth(1, exp));
    rectangle->y1 = miniexp_to_int(miniexp_nth(2, exp));
    rectangle->x2 = miniexp_to_int(miniexp_nth(3, exp));
    rectangle->y2 = miniexp_to_int(miniexp_nth(4, exp));

    if (page_text->rectangle != NULL) {
      if (rectangle->x1 < page_text->rectangle->x1) {
        page_text->rectangle->x1 = rectangle->x1;
      }

      if (rectangle->x2 > page_text->rectangle->x2) {
        page_text->rectangle->x2 = rectangle->x2;
      }

      if (rectangle->y1 < page_text->rectangle->y1) {
        page_text->rectangle->y1 = rectangle->y1;
      }

      if (rectangle->y2 > page_text->rectangle->y2) {
        page_text->rectangle->y2 = rectangle->y2;
      }

      free(rectangle);
    } else {
      page_text->rectangle = rectangle;
    }

    if (exp == end) {
      return false;
    }
  }

  return true;

error_ret:

  return false;
}

static bool djvu_page_text_build_rectangle(djvu_page_text_t* page_text, miniexp_t exp, miniexp_t start, miniexp_t end) {
  if (page_text == NULL) {
    goto error_ret;
  }

  if (miniexp_consp(exp) == 0 || miniexp_symbolp(miniexp_car(exp)) == 0) {
    return false;
  }

  miniexp_t inner = miniexp_cddr(miniexp_cdddr(exp));
  while (inner != miniexp_nil) {
    miniexp_t data = miniexp_car(inner);

    if (miniexp_stringp(data) != 0) {
      if (djvu_page_text_build_rectangle_process(page_text, exp, start, end) == false) {
        goto error_ret;
      }
    } else {
      if (djvu_page_text_build_rectangle(page_text, data, start, end) == false) {
        goto error_ret;
      }
    }

    inner = miniexp_cdr(inner);
  }

  return true;

error_ret:

  return false;
}

char* djvu_page_text_select(djvu_page_text_t* page_text, zathura_rectangle_t rectangle) {
  if (page_text == NULL) {
    return NULL;
  }

  djvu_page_text_limit(page_text, page_text->text_information, &rectangle);
  djvu_page_text_select_content(page_text, page_text->text_information, 0);

  return (page_text->content != NULL) ? g_strdup(page_text->content) : NULL;
}

static void djvu_page_text_limit_process(djvu_page_text_t* page_text, miniexp_t exp, zathura_rectangle_t* rectangle) {
  zathura_rectangle_t current_rectangle;
  current_rectangle.x1 = miniexp_to_int(miniexp_nth(1, exp));
  current_rectangle.y1 = miniexp_to_int(miniexp_nth(2, exp));
  current_rectangle.x2 = miniexp_to_int(miniexp_nth(3, exp));
  current_rectangle.y2 = miniexp_to_int(miniexp_nth(4, exp));

  if (current_rectangle.x2 >= rectangle->x1 && current_rectangle.y1 <= rectangle->y2 &&
      current_rectangle.x1 <= rectangle->x2 && current_rectangle.y2 >= rectangle->y1) {
    if (page_text->begin == miniexp_nil) {
      page_text->begin = exp;
    }

    page_text->end = exp;
  }
}

static void djvu_page_text_limit(djvu_page_text_t* page_text, miniexp_t exp, zathura_rectangle_t* rectangle) {
  if (page_text == NULL || rectangle == NULL) {
    return;
  }

  if (miniexp_consp(exp) == 0 || miniexp_symbolp(miniexp_car(exp)) == 0) {
    return;
  }

  miniexp_t inner = miniexp_cddr(miniexp_cdddr(exp));
  while (inner != miniexp_nil) {
    miniexp_t data = miniexp_car(inner);

    if (miniexp_stringp(data) != 0) {
      djvu_page_text_limit_process(page_text, exp, rectangle);
    } else {
      djvu_page_text_limit(page_text, data, rectangle);
    }

    inner = miniexp_cdr(inner);
  }
}

static bool djvu_page_text_select_content(djvu_page_text_t* page_text, miniexp_t exp, int delimiter) {
  if (page_text == NULL) {
    return false;
  }

  if (miniexp_consp(exp) == 0 || miniexp_symbolp(miniexp_car(exp)) == 0) {
    return false;
  }

  if (miniexp_car(exp) != miniexp_symbol("char")) {
    delimiter |= (miniexp_car(exp) == miniexp_symbol("word")) ? 1 : 2;
  }

  miniexp_t inner = miniexp_cddr(miniexp_cdddr(exp));
  while (inner != miniexp_nil) {
    miniexp_t data = miniexp_car(inner);

    if (miniexp_stringp(data) != 0) {
      if (page_text->content != NULL || exp == page_text->begin) {
        char* token_content = (char*)miniexp_to_str(miniexp_nth(5, exp));

        if (page_text->content != NULL) {
          char* content = g_strjoin((delimiter & 2)   ? "\n"
                                    : (delimiter & 1) ? " "
                                                      : NULL,
                                    page_text->content, token_content, NULL);
          g_free(page_text->content);
          page_text->content = content;
        } else {
          page_text->content = g_strdup(token_content);
        }

        if (exp == page_text->end) {
          return false;
        }
      }
    } else {
      if (djvu_page_text_select_content(page_text, data, delimiter) == false) {
        return false;
      }
    }

    delimiter = 0;
    inner     = miniexp_cdr(inner);
  }

  return true;
}
