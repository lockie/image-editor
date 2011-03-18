
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include <fltk/FL_API.h>
#include <fltk/InvisibleBox.h>
#include <fltk/file_chooser.h>
#include <fltk/filename.h>
#include <fltk/SharedImage.h>
#include <fltk/Image.h>
#include <fltk/ask.h>
#include <fltk/ProgressBar.h>
#include <fltk/MenuBuild.h>
#include <fltk/Window.h>
#include <fltk/run.h>

using namespace fltk;


static ProgressBar* bar = NULL;
static InvisibleBox* image_box = NULL;
static Image* image = NULL;

void open_cb(Widget*, void*)
{
	const char* filename = file_chooser("Select image file to open",
		"Image Files (*.{bmp,jpg,png})",
		".");

	if(!filename)
		return;
	delete image;
	image = NULL;
	const char* extension = filename_ext(filename);
	if(strcmp(extension, ".bmp") == 0)
		image = bmpImage::get(filename);
	if(strcmp(extension, ".jpg") == 0)
		image = jpegImage::get(filename);
	if(strcmp(extension, ".png") == 0)
		image = pngImage::get(filename);

	if(!image)
	{
		message("%s: wrong format", filename);
		return;
	}
	image_box->image(image);
	image_box->redraw();
}

void quit_cb(Widget*, void*)
{
	exit(0);
}

static bool working = false;

void rolling_avg_cb(Widget*, void*)
{
	if(!image)
		return;
	if(working)
		return;

	const char* Nstr = input("Input window size", "10");
	if(!Nstr)
		return;
	int N = atoi(Nstr);
	if(N == 0)
		return;

	working = true;

	Image* newimage = new Image;
	image->forceARGB32();
	newimage->setimage( image->buffer(),
		RGB32,
		image->buffer_width(),
		image->buffer_height() );

	for(int y = 0; y < image->buffer_height() - N; y++)
	{
		bar->position(100.0 * (double) y / image->buffer_height());
		bar->redraw();
		fltk::wait(0.001);
		for(int x = 0; x < image->buffer_width() - N; x++)
		{
			long rsum = 0, gsum = 0, bsum = 0;
			for(int j = 0; j < N; j++)
				for(int i = 0; i < N; i++)
				{
					size_t index = (y + j) * image->buffer_width() * 4
						+ (x + i) * 4;
					char r = image->buffer()[index + 0];
					rsum += r;
					char g = image->buffer()[index + 1];
					gsum += g;
					char b = image->buffer()[index + 2];
					bsum += b;
				}
			uchar newpixel[4];
			newpixel[0] = rsum / (N * N);
			newpixel[1] = gsum / (N * N);
			newpixel[2] = bsum / (N * N);
			newpixel[3] = 0;
			newimage->setpixels(&newpixel[0], Rectangle(x, y, 1, 1));
		}
	}

	((SharedImage*)image)->remove();
	delete image;
	newimage->buffer_changed();
	image = newimage;
	image_box->image(image);
	bar->position(0);
	image_box->redraw();

	working = false;
}

void upscale_nn_cb(Widget*, void*)
{
	if(!image)
		return;
	if(working)
		return;

	const char* Nstr = input("Scale factor", "2");
	if(!Nstr)
		return;
	int N = atoi(Nstr);
	if(N == 0)
		return;

	working = true;

	Image* newimage = new Image;
	image->forceARGB32();
	newimage->setsize(image->buffer_width() * N, image->buffer_height() * N);
	newimage->setpixeltype(RGB32);

	for(int y = 0; y < image->buffer_height() * N; y++)
	{
		bar->position(100.0 * (double) y / (image->buffer_height() * N));
		bar->redraw();
		fltk::wait(0.001);
		for(int x = 0; x < image->buffer_width() * N; x++)
		{
			size_t index = (y / N) * image->buffer_width() * 4
				+ (x / N) * 4;
			char r = image->buffer()[index + 0];
			char g = image->buffer()[index + 1];
			char b = image->buffer()[index + 2];

			uchar newpixel[4];
			newpixel[0] = r;
			newpixel[1] = g;
			newpixel[2] = b;
			newpixel[3] = 0;
			newimage->setpixels(&newpixel[0], Rectangle(x, y, 1, 1));
		}
	}

	((SharedImage*)image)->remove();
	delete image;
	newimage->buffer_changed();
	image = newimage;
	image_box->image(image);
	bar->position(0);
	image_box->redraw();

	working = false;
}

static void build_menus(MenuBar* menu, Widget* w)
{
	ItemGroup* g;
	menu->user_data(w);
	menu->begin();
	g = new fltk::ItemGroup( "&File" );
	g->begin();
//	new Item( "&New File",        0, (Callback *)new_cb );
	new Item( "&Open File...",    COMMAND + 'o', (Callback*)open_cb );
	new Divider;
//	new Item( "&Save File",       COMMAND + 's', (Callback*)save_cb);
//	new Item( "Save File &As...", COMMAND + SHIFT + 's', (Callback *)saveas_cb);
	new Divider;
	new Item( "&Quit", COMMAND + 'q', (Callback*)quit_cb, 0 );
	g->end();
	g = new ItemGroup( "&Edit" );
	g->begin();
	new Item( "&Rolling average",  COMMAND + 'r', (Callback*)rolling_avg_cb);
	new Divider;
	new Item( "Upscale &NN",      COMMAND + 'n', (Callback*)upscale_nn_cb);
	g->end();
	menu->end();
}


int main(int argc, char **argv)
{
	register_images();

	Window window(800, 650);
	window.begin();
	MenuBar menubar(0, 0, 600, 25);
	build_menus(&menubar, &window);
	InvisibleBox box(0, 30, 800, 600);
	bar = new ProgressBar(0, 630, 800, 15);
	image_box = &box;
	window.resizable(box);
	window.end();

	window.show(argc, argv);
	return run();
}

