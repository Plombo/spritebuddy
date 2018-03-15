# SpriteBuddy

A simple command line application to apply a palette to an PNG of any color type, creating an indexed PNG with the given palette. If the source image has an alpha channel, SpriteBuddy can also create an "alpha mask" image that OpenBOR can use together with the indexed image for detailed alpha blending.

## Compiling

`gcc -g -O2 spritebuddy.c -o spritebuddy -lpng -lz`

Building requires zlib-dev and libpng-dev.

## Usage

`spritebuddy palette source result [result_mask]`

* `palette`: an indexed PNG with the target palette
* `source`: the PNG to apply the palette to and generate the mask from; can be RGB, RGBA, or indexed
* `result`: path to which to save the resulting image as an indexed PNG
* `result_mask`: (optional) path to which to save the resulting alpha mask as a grayscale PNG

The `result_mask` parameter can be omitted to skip producing an alpha mask.

If `result` or `result_mask` already exist, they will be overwritten without a prompt, so be careful.

## License

Licensed under the 3-clause BSD license; see the LICENSE file for details.

