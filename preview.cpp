/*
*   demosauce - icecast source client
*
*   this source is published under the gpl license. google it yourself.
*   also, this is beerware! you are strongly encouraged to invite the
*   authors of this software to a beer when you happen to meet them.
*   copyright MMXI by maep
*/

#include <cstdlib>
#include <string>
#include <iostream>

#include <vector>

#include <boost/filesystem.hpp>
#include <boost/lexical_cast.hpp>

#include <png.h>

#include "logror.h"
#include "basssource.h"
#include "avsource.h"

static const int IMG_WIDTH = 500;
static const int IMG_HEIGHT = 93;

float replay_gain = 0;
float loop_time = 0;
std::string input_file;
std::string output_dir;

uint32_t bg_map[IMG_HEIGHT];
uint32_t fg_map[IMG_HEIGHT];

void exit_fail(std::string msg)
{
    std::cerr << msg << std::endl;
    exit(EXIT_FAILURE);
}

void safe_png(std::string name, uint32_t* buffer, uint32_t width, uint32_t height)
{
	/* create file */
	FILE *fp = fopen(name.c_str(), "wb");
	if (!fp)
		exit_fail("File could not be opened for writing");

	/* initialize stuff */
	png_ptr = png_create_write_struct(PNG_LIBPNG_VER_STRING, NULL, NULL, NULL);

	if (!png_ptr)
		abort_("[write_png_file] png_create_write_struct failed");

	info_ptr = png_create_info_struct(png_ptr);
	if (!info_ptr)
		abort_("[write_png_file] png_create_info_struct failed");

	if (setjmp(png_jmpbuf(png_ptr)))
		abort_("[write_png_file] Error during init_io");

	png_init_io(png_ptr, fp);

	/* write header */
	if (setjmp(png_jmpbuf(png_ptr)))
		abort_("[write_png_file] Error during writing header");

	png_set_IHDR(png_ptr, info_ptr, width, height,
		     bit_depth, color_type, PNG_INTERLACE_NONE,
		     PNG_COMPRESSION_TYPE_BASE, PNG_FILTER_TYPE_BASE);

	png_write_info(png_ptr, info_ptr);

	/* write bytes */
	if (setjmp(png_jmpbuf(png_ptr)))
		abort_("[write_png_file] Error during writing bytes");

	png_write_image(png_ptr, row_pointers);

	/* end write */
	if (setjmp(png_jmpbuf(png_ptr)))
		abort_("[write_png_file] Error during end of write");

	png_write_end(png_ptr, NULL);

    /* cleanup heap allocation */
	for (y=0; y<height; y++)
		free(row_pointers[y]);

	free(row_pointers);
    fclose(fp);

    //~ std::vector<uint32_t*> rows;
    //~ for (uint32_t i = 0; i < height; ++i)
        //~ rows.push_back(buffer + i * width);
}

void read_params(int argc, char* argv[])
{
    if (argc < 4)
    {
        std::cout << "syntax: preview <gain> <loop time> <input file> [output dir]\n";
        exit(EXIT_FAILURE);
    }
    for (int i = 1; i < argc; ++i)
        switch (i)
        {
            case 1:
                replay_gain = boost::lexical_cast<float>(argv[i]);
                break;
            case 2:
                loop_time = boost::lexical_cast<float>(argv[i]);
                break;
            case 3:
                input_file = argv[i];
                break;
            case 4:
                input_file = argv[i];
                break;
        }
}

uint32_t make_color(uint8_t r, uint8_t g, uint8_t b, uint8_t a)
{
    return (r << 24) | (g << 16) | (b << 8) | a;
}

void gen_map(uint32_t* buf, uint32_t color0, uint32_t color1)
{
    uint8_t startr = (color0 >> 24) & 0xff;
    uint8_t startg = (color0 >> 16) & 0xff;
    uint8_t startb = (color0 >> 8) & 0xff;
    uint8_t starta = color0 & 0xff;
    float deltar = static_cast<float>((color1 >> 24) & 0xff) / startr;
    float deltag = static_cast<float>((color1 >> 16) & 0xff) / startg;
    float deltab = static_cast<float>((color1 >> 8) & 0xff) / startb;
    float deltaa = static_cast<float>(color1 & 0xff) / starta;

    size_t center = IMG_HEIGHT / 2;
    for (size_t i = 0; i < center; ++i)
    {
        uint32_t color = make_color(startr + i * deltar,
            startg + i * deltag,
            startb + i * deltab,
            starta + i * deltaa);
        buf[i] = color;
        buf[IMG_HEIGHT - i - 1] = color;
    }
    buf[center] = color1;
}

void scan_song(std::string file_name)
{
	BassSource bass_source;
	AvSource av_source;
	bool bass_loaded = bass_source.load(file_name, true);
	bool av_loaded = bass_loaded ? false : av_source.load(file_name);

	AbstractSource* source = 0;
	if (bass_loaded)
		source = static_cast<AbstractSource*>(&bass_source);
	if (av_loaded)
		source = static_cast<AbstractSource*>(&av_source);

	uint32_t chan = source->channels();
	uint32_t samplerate = source->samplerate();

	if ((!av_loaded && !bass_loaded) || samplerate == 0 || chan < 1 || chan > 2)
		FATAL("can't read file");

    std::vector<float> peaks;
	AudioStream stream;
    uint32_t chunksize = samplerate;
    uint32_t chunk_counter = 0;
    float max_l = 0; // left channel

    while (!stream.end_of_stream)
    {
        source->process(stream, 48000);
        float const* in = stream.buffer(0);
        // optimize if bored :)
        for (size_t i = stream.frames(); i; --i)
        {
            float const value = fabs(*in++);
            if (value >  max_l)
                max_l = value;

            ++in;
            if (++chunk_counter >= chunksize)
            {
                peaks.push_back(max_l);
                max_l = 0;
                chunk_counter = 0;
            }
        }
    }
    if (peaks.size() > IMG_WIDTH)
        ; // chrunch_peaks()

    std::vector<uint32_t> pic_buff;
    pic_buff.reserve(IMG_WIDTH * IMG_HEIGHT);

    for (size_t y = 0; y < IMG_HEIGHT; ++y)
    {
        float line_peak = 1.0 - static_cast<float>(y) / ( .5 * IMG_HEIGHT);
        for (size_t x = 0; x < IMG_WIDTH ; ++x)
            pic_buff[y * x + x] = peaks[x] > line_peak ? fg_map[y] : bg_map[y];
    }

    save_png("test.png", &pic_buf[0], IMG_WIDTH, IMG_HEIGHT)

}

int main(int argc, char* argv[])
{
    read_params(argc, argv);
    gen_map(bg_map, make_color(0x8a, 0xa3, 0xdc, 0x16), make_color(0x60, 0x73, 0x9d, 0xff));
    gen_map(fg_map, make_color(0x00, 0x06, 0x52, 0xff), make_color(0x00, 0x06, 0xaa, 0fff));
    scan_song(input_file);
	return EXIT_SUCCESS;
}
