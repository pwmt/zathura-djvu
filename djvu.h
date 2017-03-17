/* See LICENSE file for license and copyright information */

#ifndef DJVU_H
#define DJVU_H

#include <stdbool.h>
#include <zathura/plugin-api.h>
#include <libdjvu/ddjvuapi.h>
#include <cairo.h>

/**
 * DjVu document
 */
typedef struct djvu_document_s
{
  ddjvu_context_t*  context; /**< Document context */
  ddjvu_document_t* document; /**< Document */
  ddjvu_format_t*   format; /**< Format */
} djvu_document_t;

/**
 * Open a DjVU document
 *
 * @param document Zathura document
 * @return ZATHURA_ERROR_OK when no error occurred, otherwise see
 *    zathura_error_t
 */
GIRARA_HIDDEN zathura_error_t djvu_document_open(zathura_document_t* document);

/**
 * Closes and frees the internal document structure
 *
 * @param document Zathura document
 * @return ZATHURA_ERROR_OK when no error occurred, otherwise see
 *    zathura_error_t
 */
GIRARA_HIDDEN zathura_error_t djvu_document_free(zathura_document_t* document, djvu_document_t* djvu_document);

/**
 * Generates the index of the document
 *
 * @param document Zathura document
 * @param error Set to an error value (see zathura_error_t) if an
 *   error occurred
 * @return Tree node object or NULL if an error occurred (e.g.: the document has
 *   no index)
 */
GIRARA_HIDDEN girara_tree_node_t* djvu_document_index_generate(zathura_document_t* document,
    djvu_document_t* djvu_document, zathura_error_t* error);

/**
 * Saves the document to the given path
 *
 * @param document Zathura document
 * @param path File path
 * @return ZATHURA_ERROR_OK when no error occurred, otherwise see
 *    zathura_error_t
 */
GIRARA_HIDDEN zathura_error_t djvu_document_save_as(zathura_document_t* document, djvu_document_t* djvu_document, const char* path);

/**
 * Initializes the page
 *
 * @param page The page object
 * @return ZATHURA_ERROR_OK when no error occurred, otherwise see
 *    zathura_error_t
 */
GIRARA_HIDDEN zathura_error_t djvu_page_init(zathura_page_t* page, void* data);

/**
 * Frees a DjVu page
 *
 * @param page Page
 * @return ZATHURA_ERROR_OK when no error occurred, otherwise see
 *    zathura_error_t
 */
GIRARA_HIDDEN zathura_error_t djvu_page_clear(zathura_page_t* page, void* data);

/**
 * Searches for a specific text on a page and returns a list of results
 *
 * @param page Page
 * @param text Search item
 * @param error Set to an error value (see zathura_error_t) if an
 *   error occurred
 * @return List of search results or NULL if an error occurred
 */
GIRARA_HIDDEN girara_list_t* djvu_page_search_text(zathura_page_t* page, void* data, const char* text, zathura_error_t* error);

/**
 * Get text for selection
 *
 * @param page Page
 * @param rectangle Selection
 * @error Set to an error value (see \ref zathura_error_t) if an error
 * occurred
 * @return The selected text (needs to be deallocated with g_free)
 */
GIRARA_HIDDEN char* djvu_page_get_text(zathura_page_t* page, void* data, zathura_rectangle_t rectangle, zathura_error_t* error);

/**
 * Returns list of links
 *
 * @param page The page
 * @param data Unused page data
 * @param error Error code
 * @return List of links or NULL if an error occurred
 */
GIRARA_HIDDEN girara_list_t* djvu_page_links_get(zathura_page_t* page, void* data,
    zathura_error_t* error);

/**
 * Renders a page and returns a allocated image buffer which has to be freed
 * with zathura_image_buffer_free
 *
 * @param page Page
 * @param error Set to an error value (see zathura_error_t) if an
 *   error occurred
 * @return Image buffer or NULL if an error occurred
 */
GIRARA_HIDDEN zathura_image_buffer_t* djvu_page_render(zathura_page_t* page, void* data, zathura_error_t* error);

/**
 * Renders a page onto a cairo object
 *
 * @param page Page
 * @param cairo Cairo object
 * @param printing Set to true if page should be rendered for printing
 * @return ZATHURA_ERROR_OK when no error occurred, otherwise see
 *    zathura_error_t
 */
GIRARA_HIDDEN zathura_error_t djvu_page_render_cairo(zathura_page_t* page, void* data, cairo_t* cairo, bool printing);

#endif // DJVU_H
