/*
 * Copyright (c) 2018 Bryan Cain
 * Copyright (c) 2004-2017 OpenBOR Team
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without modification, are permitted
 * provided that the following conditions are met:
 *
 * 1. Redistributions of source code must retain the above copyright notice, this list of
 *    conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright notice, this list of
 *    conditions and the following disclaimer in the documentation and/or other materials provided
 *    with the distribution.
 * 3. Neither the name of the copyright holder nor the names of its contributors may be used to
 *    endorse or promote products derived from this software without specific prior written
 *    permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND
 * FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR
 * CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
 * SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR
 * OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

/* Converts an RGBA png to an indexed image using a given palette, and creates an alpha
 * mask if needed. */

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <assert.h>
#include <png.h>
#include <zlib.h>

typedef struct {
    uint32_t w;
    uint32_t h;
    bool hasAlphaChannel;
    uint32_t *pixels;
} Image32;

png_color pal[256];
int pal_ncolors; // number of colors in palette (1-256)

bool readPal(const char *path)
{
    png_structp png_ptr;
    png_infop info_ptr;
    unsigned int sig_read = 0;
    png_colorp png_pal_ptr = 0;

    FILE *fp = fopen(path, "rb");
    if (!fp) return false;

    png_ptr = png_create_read_struct(PNG_LIBPNG_VER_STRING, NULL, NULL, NULL);
    if (png_ptr == NULL)
    {
        return false;
    }
    info_ptr = png_create_info_struct(png_ptr);
    if (info_ptr == NULL)
    {
        goto error;
    }
    if (setjmp(png_jmpbuf(png_ptr)))
    {
        goto error;
    }

    png_init_io(png_ptr, fp);
    png_set_sig_bytes(png_ptr, sig_read);
    png_read_info(png_ptr, info_ptr);

    if (png_get_PLTE(png_ptr, info_ptr, &png_pal_ptr, &pal_ncolors) != PNG_INFO_PLTE ||
        png_pal_ptr == NULL)
    {
        fprintf(stderr, "error: failed to read PLTE chunk from %s (is it indexed?)\n", path);
        goto error;
    }

    printf("read PLTE chunk with %i colors from %s\n", pal_ncolors, path);
    memcpy(pal, png_pal_ptr, pal_ncolors * sizeof(png_color));
    png_destroy_read_struct(&png_ptr, NULL, NULL);
    fclose(fp);
    return true;

error:
    png_destroy_read_struct(&png_ptr, NULL, NULL);
    fclose(fp);
    return false;
}

bool readSourcePNG(const char *path, Image32 *image)
{
    png_structp png_ptr;
    png_infop info_ptr;
    unsigned int sig_read = 0;
    png_uint_32 width, height;
    int bit_depth, color_type, interlace_type, y;
    uint32_t *line;
    FILE *fp;

    memset(image, 0, sizeof(*image));

    fp = fopen(path, "rb");
    if (!fp) return false;

    png_ptr = png_create_read_struct(PNG_LIBPNG_VER_STRING, NULL, NULL, NULL);
    if (png_ptr == NULL)
    {
        fclose(fp);
        return false;
    }
    info_ptr = png_create_info_struct(png_ptr);
    if (info_ptr == NULL)
    {
        goto error;
    }
    if (setjmp(png_jmpbuf(png_ptr)))
    {
        goto error;
    }

    png_init_io(png_ptr, fp);
    png_set_sig_bytes(png_ptr, sig_read);
    png_read_info(png_ptr, info_ptr);
    png_get_IHDR(png_ptr, info_ptr, &width, &height, &bit_depth, &color_type, &interlace_type, NULL, NULL);

    png_set_strip_16(png_ptr);
    png_set_packing(png_ptr);

    if (color_type & PNG_COLOR_MASK_ALPHA)
    {
        printf("has alpha channel\n");
        image->hasAlphaChannel = true;
    }
    else
    {
        printf("no alpha channel\n");
        image->hasAlphaChannel = false;
    }

    if (color_type == PNG_COLOR_TYPE_PALETTE)
    {
        png_set_palette_to_rgb(png_ptr);
    }
    else if (color_type == PNG_COLOR_TYPE_GRAY || color_type == PNG_COLOR_TYPE_GRAY_ALPHA)
    {
        png_set_gray_to_rgb(png_ptr);
    }

    if (color_type == PNG_COLOR_TYPE_GRAY && bit_depth < 8)
    {
        png_set_expand_gray_1_2_4_to_8(png_ptr);
    }

    if (png_get_valid(png_ptr, info_ptr, PNG_INFO_tRNS))
    {
        png_set_tRNS_to_alpha(png_ptr);
    }

    png_set_filler(png_ptr, 0xff, PNG_FILLER_AFTER);

    image->w = width;
    image->h = height;
    image->pixels = malloc(width * height * 4);
    if (image->pixels == NULL)
    {
        goto error;
    }

    line = image->pixels;
    for (y = 0; y < height; y++)
    {
        png_read_row(png_ptr, (uint8_t *) line, NULL);
        line += width;
    }
    png_read_end(png_ptr, info_ptr);
    png_destroy_read_struct(&png_ptr, &info_ptr, NULL);
    fclose(fp);
    return true;

error:
    png_destroy_read_struct(&png_ptr, NULL, NULL);
    return false;
}

// saves image as indexed PNG using nearest-color algorithm
bool saveIndexedPNG(const char *path, Image32* screen)
{
    uint32_t *vram32;
    int i, x, y;
    png_structp png_ptr;
    png_infop info_ptr;
    FILE *fp;
    uint8_t *line;

    fp = fopen(path, "wb");
    if (!fp) return false;
    png_ptr = png_create_write_struct(PNG_LIBPNG_VER_STRING, NULL, NULL, NULL);
    if (!png_ptr) return false;
    info_ptr = png_create_info_struct(png_ptr);
    if (!info_ptr)
    {
        png_destroy_write_struct(&png_ptr, (png_infopp)NULL);
        fclose(fp);
        return false;
    }

    png_init_io(png_ptr, fp);
    png_set_compression_level(png_ptr, Z_BEST_COMPRESSION);
    png_set_IHDR(png_ptr, info_ptr, screen->w, screen->h,
                 8, PNG_COLOR_TYPE_PALETTE, PNG_INTERLACE_NONE,
                 PNG_COMPRESSION_TYPE_DEFAULT, PNG_FILTER_TYPE_DEFAULT);
    png_set_PLTE(png_ptr, info_ptr, pal, pal_ncolors);
    png_write_info(png_ptr, info_ptr);
    line = (uint8_t*) malloc(screen->w);

    vram32 = screen->pixels;

    for (y = 0; y < screen->h; y++)
    {
        vram32 = screen->pixels + (y * screen->w);
        for (i = 0, x = 0; x < screen->w; x++)
        {
            uint32_t color = 0;
            uint8_t r = 0, g = 0, b = 0, a = 0, nearest = 1;

            color = vram32[x];
            r = color & 0xff;
            g = (color >> 8) & 0xff;
            b = (color >> 16) & 0xff;
            a = (color >> 24) & 0xff;

            if (screen->hasAlphaChannel && a == 0) nearest = 0;
            else
            {
                int j;
                int nearest_dist_sq = 9999999;
                /* If the source has an alpha mask, don't use the transparent color (0) for any
                 * pixels that aren't completely transparent. */
                for (j = screen->hasAlphaChannel ? 1 : 0; j < pal_ncolors; j++)
                {
                    int rdist = r - pal[j].red;
                    int gdist = g - pal[j].green;
                    int bdist = b - pal[j].blue;
                    int dist_sq = rdist*rdist + gdist*gdist + bdist*bdist;
                    if (dist_sq < nearest_dist_sq)
                    {
                        nearest_dist_sq = dist_sq;
                        nearest = j;
                    }
                }
            }

            line[i++] = nearest;
        }
        png_write_row(png_ptr, line);
    }
    free(line);
    line = NULL;
    png_write_end(png_ptr, info_ptr);
    png_destroy_write_struct(&png_ptr, (png_infopp)NULL);
    fclose(fp);
    return true;
}

// saves alpha mask of image
bool saveMask(const char* filename, Image32* screen)
{
    uint32_t *vram32;
    int i, x, y;
    png_structp png_ptr;
    png_infop info_ptr;
    FILE *fp;
    uint8_t *line;

    fp = fopen(filename, "wb");
    if (!fp) return false;
    png_ptr = png_create_write_struct(PNG_LIBPNG_VER_STRING, NULL, NULL, NULL);
    if (!png_ptr) return false;
    info_ptr = png_create_info_struct(png_ptr);
    if (!info_ptr)
    {
        png_destroy_write_struct(&png_ptr, (png_infopp)NULL);
        fclose(fp);
        return false;
    }
    png_init_io(png_ptr, fp);
    png_set_compression_level(png_ptr, Z_BEST_COMPRESSION);
    png_set_IHDR(png_ptr, info_ptr, screen->w, screen->h,
                 8, PNG_COLOR_TYPE_GRAY, PNG_INTERLACE_NONE,
                 PNG_COMPRESSION_TYPE_DEFAULT, PNG_FILTER_TYPE_DEFAULT);
    png_write_info(png_ptr, info_ptr);
    line = (uint8_t*) malloc(screen->w);

    vram32 = screen->pixels;

    for (y = 0; y < screen->h; y++)
    {
        vram32 = screen->pixels + (y * screen->w);
        for (i = 0, x = 0; x < screen->w; x++)
        {
            uint32_t color = vram32[x];
            uint8_t a = (color >> 24) & 0xff;
            line[i++] = a;
        }
        png_write_row(png_ptr, line);
    }
    free(line);
    line = NULL;
    png_write_end(png_ptr, info_ptr);
    png_destroy_write_struct(&png_ptr, (png_infopp)NULL);
    fclose(fp);
    return true;
}

// returns true if and only if alpha channel of img has at least one alpha
// value that isn't 0 or 255
bool needsmask(Image32 *img)
{
    uint32_t x, y, *color;
    for (y = 0; y < img->h; y++)
    {
        color = img->pixels + (y * img->w);
        for (x = 0; x < img->w; x++)
        {
            uint32_t alpha = ((*color) >> 24) & 0xff;
            assert(alpha >= 0 && alpha <= 255);
            if (alpha != 0 && alpha != 255) return true;
            color++;
        }
    }

    return false;
}

int main(int argc, char **argv)
{
    if (argc != 4 && argc != 5) // alpha masking is optional
    {
        fprintf(stderr, "Usage: %s palette source result [result_mask]\n", argv[0]);
        fprintf(stderr, "\n");
        fprintf(stderr, "palette: an indexed PNG or GIF with the target palette\n");
        fprintf(stderr, "source: the RGBA PNG to apply the palette to and generate the mask from\n");
        fprintf(stderr, "result: path to which to save the resulting image as an indexed PNG\n");
        fprintf(stderr, "result_mask: path to which to save the resulting alpha mask as a grayscale PNG\n");
        fprintf(stderr, "\n");
        fprintf(stderr, "The result_mask parameter can be omitted to skip producing an alpha mask.\n");
        fprintf(stderr, "Note that result and result_mask will be overwritten if the paths already exist.\n");
        return 1;
    }

    Image32 img = {0};

    if (!readPal(argv[1]))
    {
        fprintf(stderr, "error: failed to load palette image '%s'\n", argv[1]);
        goto error;
    }

    if (readSourcePNG(argv[2], &img))
    {
        printf("read image %s\n", argv[2]);
    }
    else
    {
        fprintf(stderr, "error: failed to load image %s\n", argv[2]);
        goto error;
    }

    if (!saveIndexedPNG(argv[3], &img))
    {
        fprintf(stderr, "error: failed to save result '%s'\n", argv[3]);
        goto error;
    } else printf("saved result to '%s'\n", argv[3]);

    if (img.hasAlphaChannel)
    {
        if (needsmask(&img))
        {
            if (!saveMask(argv[4], &img))
            {
                fprintf(stderr, "error: failed to save alpha mask '%s'\n", argv[4]);
                goto error;
            }
            else printf("saved alpha mask to '%s'\n", argv[4]);
        }
        else printf("no alpha mask needed (simple alpha channel)\n");
    } else printf("no alpha mask needed (source has no alpha channel)\n");

    free(img.pixels);

    return 0;

error:
    if (img.pixels) free(img.pixels);
    return 1;
}

