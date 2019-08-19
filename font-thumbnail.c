#include <stdio.h>
#include <stdlib.h>
#include <argp.h>
#include <cairo.h>
#include <cairo-ft.h>
#include <ft2build.h>
#include <png.h>
#include <zlib.h>
#include <sys/stat.h>
#include <sys/types.h>
#include FT_SFNT_NAMES_H
#include FT_FREETYPE_H
#include FT_GLYPH_H
#include FT_OUTLINE_H
#include FT_BBOX_H
#include FT_TYPE1_TABLES_H
#include "asprintf/asprintf.h"

enum status {
	SUCCESS,
	FAILURE
};

const char *argp_program_version = "0.0";
const char *argp_program_bug_address = "<me@somewhere>";
static const char program_description[] = "Create a thumbnail image from the given font file\vIf only width or height is specified, the given value is used for both dimensions.";
static const char argument_names[] = "FILE";

static struct argp_option options[] = {
	{"size",	's',	"NUMBER",	0,	"single value in pixels to be used for image width and height (default: 256)" },
	{"width",	'w',	"NUMBER",	0,	"image width in pixels (default: 256)" },
	{"height",	'h',	"NUMBER",	0,	"image height in pixels (default: 256)" },
	{"text",	't',	"STRING",	0,	"text to display" },
	{"output",	'o',	"FILE",	0,	"output filename (default: font filename + \".png\")" },
	{"uri",	'u',	"URI",	0,	"URI of the font file (will be added as a PNG attribute)" },
	{0}
};

// structure to hold the arguments
struct arguments {
	int width, height;
	char *text, *image_file, *font_file, *font_uri;
};

static error_t argument_parser(int key, char *argument, struct argp_state *state) {
	// state->input holds the input argument of argp_parse, which is a pointer to the arguments structure
	struct arguments *arguments = state->input;

	switch (key) {
		case 's':
			arguments->width = strtol(argument, NULL, 10);
			arguments->height = strtol(argument, NULL, 10);
			break;
		case 'w':
			arguments->width = strtol(argument, NULL, 10);
			break;
		case 'h':
			arguments->height = strtol(argument, NULL, 10);
			break;
		case 't':
			arguments->text = argument;
			break;
		case 'o':
			arguments->image_file = argument;
			break;
		case 'u':
			arguments->font_uri = argument;
			break;
		case ARGP_KEY_ARG:
			if (state->arg_num == 0) {
				arguments->font_file = argument;
			} else {
				// only 1 non-option argument is accepted
				argp_usage(state);
			}
			break;
		case ARGP_KEY_NO_ARGS:
			argp_usage(state);
			break;
		case ARGP_KEY_END:
			// validity checks
			break;
		default:
			return ARGP_ERR_UNKNOWN;
	}
	return 0;
}

static struct argp argp = {options, argument_parser, argument_names, program_description};


time_t get_mtime(char *file_path) {
	struct stat attributes;
	stat(file_path, &attributes);
	return attributes.st_mtime;
}

void fill_png_text_array(png_text *array, char *uri, char *mtime_string) {
/*void fill_png_text_array(png_text *array, char *uri, int mtime) {*/
	array[0].key = "Thumb::URI";
	array[0].text = uri;
	array[0].compression = PNG_TEXT_COMPRESSION_zTXt;
	array[1].key = "Thumb::MTime";
	array[1].text = mtime_string;
	array[1].compression = PNG_TEXT_COMPRESSION_zTXt;
}

int save_png (cairo_surface_t *cairo_surface, png_text *text, char *file_path) {
	enum status return_code = FAILURE;

	png_structp png = NULL;
	png_infop info = NULL;
	png_bytep *row_pointers = NULL;

	FILE *file = fopen(file_path, "wb");
	if (!file) {
		perror("fopen");
		goto out;
	}

	png = png_create_write_struct(PNG_LIBPNG_VER_STRING, NULL, NULL, NULL);
	if (!png) {
		fputs("Could not initialize PNG write structure\n", stderr);
		goto out;
	}

	info = png_create_info_struct (png);
	if (!png) {
		fputs("Could not initialize PNG info structure\n", stderr);
		goto out;
	}

	if (setjmp(png_jmpbuf(png))) {
		fputs("libpng error\n", stderr);
		goto out;
	}

	cairo_surface_flush(cairo_surface);
	unsigned char *bitmap_data = cairo_image_surface_get_data(cairo_surface);
	int width = cairo_image_surface_get_width(cairo_surface);
	int height = cairo_image_surface_get_height(cairo_surface);

	png_set_text (png, info, text, 2);

	png_set_IHDR (png,
		info,
		width,
		height,
		8,
		//PNG_COLOR_TYPE_GRAY,
		PNG_COLOR_TYPE_RGBA,
		PNG_INTERLACE_NONE,
		PNG_COMPRESSION_TYPE_DEFAULT,
		PNG_FILTER_TYPE_DEFAULT);

	row_pointers = malloc (sizeof (png_byte*) * height);
	if (!row_pointers) {
		perror("malloc");
		goto out;
	}

	for (int y = 0; y < height; ++y) {
		row_pointers[y] = bitmap_data + y*png_get_rowbytes(png, info);
	}
	png_set_rows(png, info, row_pointers);

	png_set_compression_level(png, Z_BEST_COMPRESSION);
	png_init_io(png, file);
	png_write_png(png, info, PNG_TRANSFORM_BGR, NULL);

	return_code = SUCCESS;

	out:
		png_destroy_write_struct(&png, &info);
		free(row_pointers);
		fclose(file);
		return return_code;
}

int main(int argc, char **argv) {
	int exit_code = EXIT_FAILURE;

	char *extended_filename = NULL;
	char* mtime_string = NULL;
	FT_Library ft_library = NULL;
	FT_Face ft_typeface = NULL;
	cairo_font_face_t *cairo_typeface = NULL;
	cairo_t *recording_context = NULL;
	cairo_t *image_context = NULL;
	cairo_surface_t *recording_surface = NULL;
	cairo_surface_t *image_surface = NULL;

	struct arguments arguments = {
		.text = "Aa"
	};

	argp_parse(&argp, argc, argv, 0, 0, &arguments);

	if (!arguments.width && !arguments.height) {
		arguments.width = 256; arguments.height = 256;
	} else if (!arguments.width) {
		arguments.width = arguments.height;
	} else if (!arguments.height) {
		arguments.height = arguments.width;
	}

	if (!arguments.image_file) {
		if (asprintf(&extended_filename, "%s.png", arguments.font_file) != -1) {
			arguments.image_file = extended_filename;
		} else {
			perror("asprintf");
			goto out;
		}
	}

	int margin = arguments.height/20;
	int text_width = arguments.width - 2*margin;
	int text_height = arguments.height - 2*margin;

	if (asprintf(&mtime_string, "%i", get_mtime(arguments.font_file)) == -1) {
		perror("asprintf");
		goto out;
	}

	if (FT_Init_FreeType(&ft_library) != 0) {
		fputs("Could not initialize FreeType\n", stderr);
		goto out;
	}

	if (FT_New_Face(ft_library, arguments.font_file, 0, &ft_typeface) != 0) {
		fprintf(stderr, "Could not open %s\n", arguments.font_file);
		goto out;
	}

	recording_surface = cairo_recording_surface_create(CAIRO_CONTENT_COLOR_ALPHA, NULL);
	//recording_surface = cairo_recording_surface_create(CAIRO_CONTENT_ALPHA, NULL);
	recording_context = cairo_create(recording_surface);

	cairo_typeface = cairo_ft_font_face_create_for_ft_face(ft_typeface, 0);
	cairo_set_font_face(recording_context, cairo_typeface);

	cairo_set_font_size(recording_context, 100.0);
	cairo_text_extents_t text_extents;
	cairo_text_extents(recording_context, arguments.text, &text_extents);

	if ((float)text_extents.width/text_extents.height < (float)text_width/text_height) {
		cairo_set_font_size(recording_context, 100.0*text_height/text_extents.height);
	} else {
		cairo_set_font_size(recording_context, 100.0*text_width/text_extents.width);
	}
	cairo_text_extents(recording_context, arguments.text, &text_extents);

	int text_x = text_width/2 - (text_extents.width/2 + text_extents.x_bearing);
	int text_y = text_height/2 - (text_extents.height/2 + text_extents.y_bearing);
	cairo_move_to(recording_context, text_x, text_y);
	cairo_show_text(recording_context, arguments.text);

	image_surface = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, arguments.width, arguments.height);
	image_context = cairo_create(image_surface);

	cairo_set_source_surface(image_context, recording_surface, margin, margin);
	cairo_paint(image_context);

	png_text thumb_tags[2];
	fill_png_text_array(thumb_tags, arguments.font_uri, mtime_string);

	//cairo_surface_write_to_png(image_surface, arguments.image_file);
	if (save_png(image_surface, thumb_tags, arguments.image_file) != SUCCESS) {
		fputs("Could not save PNG file\n", stderr);
		goto out;
	}

	exit_code = EXIT_SUCCESS;

	out:
		free(extended_filename);
		free(mtime_string);
		cairo_font_face_destroy(cairo_typeface);
		cairo_destroy(recording_context);
		cairo_destroy(image_context);
		cairo_surface_destroy(recording_surface);
		cairo_surface_destroy(image_surface);
		cairo_debug_reset_static_data();
		FT_Done_Face(ft_typeface);
		FT_Done_FreeType(ft_library);

		return exit_code;
}
