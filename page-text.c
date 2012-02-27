/* See LICENSE file for license and copyright information */

#include <libdjvu/miniexp.h>
#include <string.h>

#include "page-text.h"
#include "internal.h"

/* forward declaration */
static void djvu_text_page_content_append(djvu_page_text_t* page_text, miniexp_t exp);

djvu_page_text_t*
djvu_page_text_new(djvu_document_t* document, unsigned int page_number)
{
  if (document == NULL || document->document == NULL) {
    goto error_ret;
  }

  djvu_page_text_t* page_text = calloc(1, sizeof(page_text));
  if (page_text == NULL) {
    goto error_ret;
  }

  page_text->text_information = miniexp_nil;
  page_text->begin            = miniexp_nil;
  page_text->end              = miniexp_nil;
  page_text->document         = document;

  /* read page text */
  while ((page_text->text_information = ddjvu_document_get_pagetext(document->document, page_number,
          "char")) == miniexp_dummy) {
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

void
djvu_page_text_free(djvu_page_text_t* page_text)
{
  if (page_text == NULL) {
    return;
  }

  if (page_text->text_information != miniexp_nil && page_text->document != NULL) {
    ddjvu_miniexp_release(page_text->document->document, page_text->text_information);
  }
}

girara_list_t*
djvu_page_text_search(djvu_page_text_t* page_text, const char* text)
{
  if (page_text == NULL || text == NULL) {
    goto error_ret;
  }

  /* get page content */
  if (page_text->content != NULL) {
    g_free(page_text->content);
    page_text->content = NULL;
  }

  djvu_text_page_content_append(page_text, page_text->text_information);

  if (page_text->content == NULL || strlen(page_text->content) == 0) {
    goto error_ret;
  }

  /* search through content */
  int search_length = strlen(text);
  char* tmp  = page_text->content;

  while ((tmp = strstr(tmp, text)) != NULL) {
    tmp += search_length;
  }

error_ret:

  return NULL;
}

static void
djvu_text_page_content_append(djvu_page_text_t* page_text, miniexp_t exp)
{
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
      /* append text */
      char* text = (char*) miniexp_to_str(data);

      if (page_text->content == NULL) {
        page_text->content = g_strdup(text);
      } else {
        char* tmp = g_strjoin(" ", page_text->content, text, NULL);
        g_free(page_text->content);
        page_text->content = tmp;
      }
    /* not a string, recursive call */
    } else {
      djvu_text_page_content_append(page_text, data);
    }

    /* move to next object */
    inner = miniexp_cdr(inner);
  }
}
