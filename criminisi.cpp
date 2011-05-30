
#include <cassert>
#include <cmath>
#include <cfloat>
#include <vector>

#include <fltk/Image.h>

using namespace std;
using namespace fltk;


static const int patch_size = 9;

extern Image* filter(double* kernel, int kern_height, int kern_width, const Image* img);

// Градиеннты через фильтр Собеля
//

double x_gradient_kernel[9] = 	
	{+1, 0, -1, 
	 +2, 0, -2,
	 +1, 0, -1};
double y_gradient_kernel[9] = 
	{+1, +2, +1,
	 0, 0, 0,
	 -1, -2, -1};

Image* gradientX(const Image* img)
{
	return filter(x_gradient_kernel, 3, 3, img);
}

Image* gradientY(const Image* img)
{
	return filter(y_gradient_kernel, 3, 3, img);
}

double* conv(bool* arr, int w, int h, double* kernel)
{
	double* res = new double[w*h];
	for(int y = 0; y < h; y++)
		for(int x = 0; x < w; x++)
		{
			double r = 0;
			for(int i = -1; i <= 1; i++)
			{
				int y_idx = y + i;
				if(y_idx < 0)
					y_idx += h;
				if(y_idx >= h)
					y_idx -= h;
				for(int j = -1; j <= 1; j++)
				{
					int x_idx = x + j;
					if(x_idx < 0)
						x_idx += w;
					if(x_idx >= w)
						x_idx -= w;

					int kern_idx = (j + 1)*3 + i + 1;
					r += kernel[kern_idx] * (int)arr[y_idx*w + x_idx];
				}
			}
			res[y*w + x] = r;
		}
	return res;
}

typedef pair<int, int> coord;  // first == x, second == y

Image* inpaint_criminisi(const Image* image, const Image* mask)
{
	assert(image->buffer_width() == mask->buffer_width());
	assert(image->buffer_height() == mask->buffer_height());

	image->forceARGB32();
	mask->forceARGB32();

	int w = image->buffer_width(), h = image->buffer_height();
	int picsize = w * h;
	
	Image* res = new Image;
	res->setimage( image->buffer(),
		RGB32,
		image->buffer_width(),
		image->buffer_height() );

	double* C = new double[picsize];
	bool* SourceRegion = new bool[picsize];

	for(int i = 0; i < h; i++)
		for(int j = 0; j < w; j++)
			if(mask->buffer()[i*w*4+j*4 + 0] == 0 &&
			 mask->buffer()[i*w*4+j*4 + 1] == 0 &&
			 mask->buffer()[i*w*4+j*4 + 2] == 0)
			{
				SourceRegion[i*w + j] = false;
				C[i*w+j] = 0;
			} else {
				SourceRegion[i*w + j] = true;
				C[i*w+j] = 1;
			}

	// calculate transposed gradient
	Image* ix = gradientX(image);
	Image* iy = gradientY(image);
	double* Ix = new double[picsize];
	double* Iy = new double[picsize];
	uchar *xbuf = ix->buffer(), *ybuf = iy->buffer();
	for(int i = 0; i < h; i++)
		for(int j = 0; j < w; j++)
		{
			int idx = i*w*4 + j*4;
			Ix[j*h + i] = (xbuf[idx]+xbuf[idx+1]+xbuf[idx+2]) / (3. * 255.);
			Iy[j*h + i] = (ybuf[idx]+ybuf[idx+1]+ybuf[idx+2]) / (3. * 255.);
		}
	delete iy; delete ix;

	vector<coord> dOmega;
	Image* dR = new Image;
	dR->setsize(w, h);
	for(;;)
	{
		// Update fill front
		uchar whitepix[4];
		uchar blackpix[4];
		whitepix[0] = whitepix[1] = whitepix[2] = 255;
		whitepix[3] = blackpix[0] = blackpix[1] = blackpix[2] = blackpix[3] = 0;
		for(int i = 0; i < h; i++)
			for(int j = 0; j < w; j++)
				if(SourceRegion[i*w+j])
					dR->setpixels(&whitepix[0], Rectangle(j, i, 1, 1));
				else
					dR->setpixels(&blackpix[0], Rectangle(j, i, 1, 1));
		double edgedet[9] = { 1, 1, 1, 1, -8, 1, 1, 1, 1 };
		Image* edges = filter(edgedet, 3, 3, dR);
		dOmega.clear();
		for(int j = 0; j < w; j++)
			for(int i = 0; i < h; i++)
				if(edges->buffer()[i*w*4+j*4] > 0)
					dOmega.push_back(coord(j, i));

		if(dOmega.empty())
			break;
		printf("%d points left\n", dOmega.size());
		
		double* Nx = conv(SourceRegion, w, h, x_gradient_kernel);
		double* Ny = conv(SourceRegion, w, h, y_gradient_kernel);
		vector<pair<double, double>> N;
		for(size_t i = 0; i < dOmega.size(); i++)
		{
			pair<double, double> p;
			p.first  = Nx[dOmega[i].second*w + dOmega[i].first];
			p.second = Ny[dOmega[i].second*w + dOmega[i].first];
			N.push_back(p);
		}
//		delete Ny; delete Nx;
		for(size_t i = 0; i < N.size(); i++)
		{
			double factor = sqrt(N[i].first*N[i].first + N[i].second*N[i].second);
			if(factor != 0)
			{
				N[i].first  /= factor;
				N[i].second /= factor;
			}
		}

		// Compute priorities for all points from front,
		//  find the patch with maximum priority
		double max_priority = 0;
		size_t phi_p = -1;
		double new_confidence;
		for(size_t p = 0; p < dOmega.size(); p++)
		{
			// calculate confidence term
			double c = 0;
			for(int x = dOmega[p].first - patch_size / 2; x < dOmega[p].first + patch_size / 2; x++)
				for(int y = dOmega[p].second - patch_size / 2; y < dOmega[p].second + patch_size / 2; y++)
					if(x > 0 && y > 0 && x < w && y < h)
						c += C[y*w + x];
			c /= (patch_size * patch_size);

			// calculate data term
			double d = abs(Ix[dOmega[p].second * w + dOmega[p].first] * N[p].first + 
				Iy[dOmega[p].second * w + dOmega[p].first] * N[p].second) + 0.001;

			double P = c * d;
			if(P > max_priority)
			{
				max_priority = P;
				phi_p = p;
				new_confidence = c;
			}
		}

		// Find the exemplar that minimizes distance
		int p_x = dOmega[phi_p].first;
		int p_y = dOmega[phi_p].second;
		double best_sse = DBL_MAX;
		int best_x, best_y;
		for(int y = patch_size / 2; y < h - patch_size / 2; y++)
			for(int x = patch_size / 2; x < w - patch_size / 2; x++)
			{
				if(abs(p_x - x) <= patch_size || abs(p_y - y) <= patch_size)
					goto bad_patch;
				double sse = 0;
				for(int i = -patch_size / 2; i <= patch_size / 2; i++)
					for(int j = -patch_size / 2; j <= patch_size / 2; j++)
					{
						int x_idx = x + i;
						int y_idx = y + j;

						if(!SourceRegion[y_idx * w + x_idx])
							goto bad_patch;
						if(p_x + i >= w || p_y + j >= h)
							continue;
						if(SourceRegion[(p_y + j)*w + p_x + i])
						{
							uchar* p1 = &res->buffer()[y_idx*w*4+x_idx*4];
							uchar* p2 = &res->buffer()[(p_y+j)*w*4+(p_x+i)*4];
							sse += (double(p1[0]-p2[0])*double(p1[0]-p2[0])+
								double(p1[1]-p2[1])*double(p1[1]-p2[1])+
								double(p1[2]-p2[2])*double(p1[2]-p2[2]));
						}
					}
				if(sse < best_sse)
				{
					best_sse = sse;
					best_x = x;
					best_y = y;
				}
bad_patch:		sse = 0;
		}
	
		// Copy image data from it
		const uchar* image_buffer = res->buffer();
		for(int i = -patch_size / 2; i <= patch_size / 2; i++)
		{
			for(int j = -patch_size / 2; j <= patch_size / 2; j++)
			{
				int x_idx = best_x + i;
				int y_idx = best_y + j;
				if(p_x+i >= w || p_y+j >= h)
					continue;
				uchar newpix[4];
				newpix[0] = image_buffer[y_idx*w*4+x_idx*4];
				newpix[1] = image_buffer[y_idx*w*4+x_idx*4+1];
				newpix[2] = image_buffer[y_idx*w*4+x_idx*4+2];
				newpix[3] = 0;
				res->setpixels(&newpix[0], Rectangle(p_x+i, p_y+j, 1, 1));
				SourceRegion[(p_y+j)*w+p_x+i] = true;
			}
		}

		// Update confidence values
		for(int i = -patch_size / 2; i <= patch_size / 2; i++)
			for(int j = -patch_size / 2; j <= patch_size / 2; j++)
			{
				int x_idx = p_x + i;
				int y_idx = p_y + j;
				C[y_idx*w+x_idx] = new_confidence;
			}

		// Propagate isophote values
		for(int i = -patch_size / 2; i <= patch_size / 2; i++)
			for(int j = -patch_size / 2; j <= patch_size / 2; j++)
			{
				int x_idx = p_x + i;
				int y_idx = p_y + j;
				Ix[y_idx*w+x_idx] = Ix[(best_y+j)*w+best_x+i];
				Iy[y_idx*w+x_idx] = Iy[(best_y+j)*w+best_x+i];
			}
	}
	return res;
}