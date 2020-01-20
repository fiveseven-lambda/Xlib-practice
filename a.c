#include <X11/Xlib.h>
#include <X11/keysym.h>
#include <unistd.h>
#include <stdio.h>
#include <math.h>
#include <sys/time.h>

int compare(double *t, double t0, int i, int j){
	if(t[i] <= t0){
		return j;
	}else if(t[j] <= t0){
		return i;
	}else if(t[i] < t[j]){
		return i;
	}else{
		return j;
	}
}

int main(){
	Display *display = XOpenDisplay(NULL);
	if(display == NULL){
		fputs("error: XOpenDisplay failed\n", stderr);
		return -1;
	}

#define PRINT(val, format) printf(#val " : " format "\n", val)

	int defaultscreen = XDefaultScreen(display);

	int width = XDisplayWidth(display, defaultscreen);
	int height = XDisplayHeight(display, defaultscreen);

	Window rootWindow = XRootWindow(display, defaultscreen);

	int defaultDepth = XDefaultDepth(display, defaultscreen);

	GC defaultGC = XDefaultGC(display, defaultscreen);

	unsigned long black = XBlackPixel(display, defaultscreen);
	unsigned long white = XWhitePixel(display, defaultscreen);

	Window window = XCreateSimpleWindow(
		display,
		rootWindow,
		0, 0,
		width, height,
		0, black,
		white
	);

	XSelectInput(display, window, ExposureMask | KeyPressMask | ButtonPressMask | ButtonReleaseMask | StructureNotifyMask);

	XMapWindow(display, window);
	XEvent event;

	Pixmap buffer = XCreatePixmap(display, window, width, height, defaultDepth);

	XGCValues xgc;

	xgc.foreground = black;
	GC blackGC = XCreateGC(display, buffer, GCForeground, &xgc);

	xgc.foreground = white;
	GC whiteGC = XCreateGC(display, buffer, GCForeground, &xgc);

#define N 256
	struct{
		double t0;
		double x, y;
		double vx, vy;
	} circles[N];
	int num_circle = 0;
	XArc arcs[N];
	XSegment segments[N];

#define N_SEG (N * 4)
	double t[N_SEG];
	for(int i = 0; i < N_SEG; ++i){
		t[i] = -1;
	}
	int t_seg[N_SEG];

	enum { mode_setting, mode_moving } mode = mode_setting;

	struct timeval tv_prev;

	for(;;){
		if(XPending(display)){
			XNextEvent(display, &event);
			switch(event.type){
				case Expose:
					if(mode == mode_setting){
						XFillRectangle(display, buffer, whiteGC, 0, 0, width, height);
						for(int i = 0; i < num_circle; ++i){
							arcs[i].x = width * circles[i].x - 10;
							arcs[i].y = height * circles[i].y - 10;
							segments[i].x1 = width * circles[i].x;
							segments[i].y1 = height * circles[i].y;
							segments[i].x2 = width * (circles[i].x + circles[i].vx);
							segments[i].y2 = height * (circles[i].y + circles[i].vy);
						}
						XDrawArcs(display, buffer, blackGC, arcs, num_circle);
						XDrawSegments(display, buffer, blackGC, segments, num_circle);
					}
					XCopyArea(display, buffer, window, defaultGC, 0, 0, width, height, 0, 0);
					break;
				case DestroyNotify:
					XFreeGC(display, blackGC);
					XFreeGC(display, whiteGC);
					XFreePixmap(display, buffer);
					XCloseDisplay(display);
					return 0;
				case ConfigureNotify:
					width = event.xconfigure.width;
					height = event.xconfigure.height;
					break;
				case ButtonPress:
					circles[num_circle].x = (double)event.xbutton.x / width;
					circles[num_circle].y = (double)event.xbutton.y / height;

					arcs[num_circle].width = arcs[num_circle].height = 20;
					arcs[num_circle].angle1 = 0;
					arcs[num_circle].angle2 = 360 * 64;

					++num_circle;

					XClearArea(display, window, 0, 0, 0, 0, True);
					break;
				case ButtonRelease:
					circles[num_circle - 1].vx = (double)event.xbutton.x / width - circles[num_circle - 1].x;
					circles[num_circle - 1].vy = (double)event.xbutton.y / height - circles[num_circle - 1].y;
					XClearArea(display, window, 0, 0, 0, 0, True);
					break;
				case KeyPress:
					switch(XLookupKeysym(&event.xkey, (_Bool)(event.xkey.state & ShiftMask) ^ (_Bool)(event.xkey.state & LockMask))){
						case XK_space:
							mode = mode_moving;
							XSelectInput(display, window, ExposureMask | KeyPressMask | StructureNotifyMask);
							gettimeofday(&tv_prev, NULL);
							{
								double t0 = (tv_prev.tv_sec + tv_prev.tv_usec * .000001);
								for(int i = 0; i < num_circle; ++i){
									circles[i].t0 = t0;
								}

								for(int i = 0; i < num_circle; ++i){
									t[i * 4] = t0 - circles[i].x / circles[i].vx;
									t[i * 4 + 1] = t0 - circles[i].y / circles[i].vy;
									t[i * 4 + 2] = t0 - (circles[i].x - 1) / circles[i].vx;
									t[i * 4 + 3] = t0 - (circles[i].y - 1) / circles[i].vy;
								}

								for(int i = 0; i < N_SEG / 2; ++i){
									t_seg[i + N_SEG / 2] = compare(t, t0, i * 2, i * 2 + 1);
								}
								for(int i = N_SEG / 2 - 1; i >= 1; --i){
									t_seg[i] = compare(t, t0, t_seg[i * 2], t_seg[i * 2 + 1]);
								}
							}

							break;
						case XK_Return:
							XDestroyWindow(display, window);
							break;
					}
					break;
			}
		}else if(mode == mode_moving){
			struct timeval tv;
			gettimeofday(&tv, NULL);
			if(tv.tv_sec + tv.tv_usec * .000001 > t[t_seg[1]]){
				int crashed = t_seg[1] / 4;

				circles[crashed].x += circles[crashed].vx * (t[t_seg[1]] - circles[crashed].t0);
				circles[crashed].y += circles[crashed].vy * (t[t_seg[1]] - circles[crashed].t0);
				circles[crashed].t0 = t[t_seg[1]];

				if(t_seg[1] % 2 == 0){
					circles[crashed].vx = -circles[crashed].vx;
				}else{
					circles[crashed].vy = -circles[crashed].vy;
				}

				int i;
				t[i = crashed * 4] = circles[crashed].t0 - circles[crashed].x / circles[crashed].vx;
				i /= 2;
				t_seg[i + N / 2] = compare(t, circles[crashed].t0, i * 2, i * 2 + 1);
				for(i = (i + N / 2) / 2; i >= 1; i /= 2) t_seg[i] = compare(t, circles[crashed].t0, t_seg[i * 2], t_seg[i * 2 + 1]);

				t[i = crashed * 4 + 1] = circles[crashed].t0 - circles[crashed].y / circles[crashed].vy;
				i /= 2;
				t_seg[i + N / 2] = compare(t, circles[crashed].t0, i * 2, i * 2 + 1);
				for(i = (i + N / 2) / 2; i >= 1; i /= 2) t_seg[i] = compare(t, circles[crashed].t0, t_seg[i * 2], t_seg[i * 2 + 1]);

				t[i = crashed * 4 + 2] = circles[crashed].t0 - (circles[crashed].x - 1) / circles[crashed].vx;
				i /= 2;
				t_seg[i + N / 2] = compare(t, circles[crashed].t0, i * 2, i * 2 + 1);
				for(i = (i + N / 2) / 2; i >= 1; i /= 2) t_seg[i] = compare(t, circles[crashed].t0, t_seg[i * 2], t_seg[i * 2 + 1]);

				t[i = crashed * 4 + 3] = circles[crashed].t0 - (circles[crashed].y - 1) / circles[crashed].vy;
				i /= 2;
				t_seg[i + N / 2] = compare(t, circles[crashed].t0, i * 2, i * 2 + 1);
				for(i = (i + N / 2) / 2; i >= 1; i /= 2) t_seg[i] = compare(t, circles[crashed].t0, t_seg[i * 2], t_seg[i * 2 + 1]);
			}
			if(tv.tv_sec > tv_prev.tv_sec || tv.tv_usec > tv_prev.tv_usec + 25000){
				XFillRectangle(display, buffer, whiteGC, 0, 0, width, height);
				for(int i = 0; i < num_circle; ++i){
					double t = (tv.tv_sec + tv.tv_usec * .000001) - circles[i].t0;
					arcs[i].x = (width * (circles[i].x + circles[i].vx * t)) - 10;
					arcs[i].y = (height * (circles[i].y + circles[i].vy * t)) - 10;
				}
				XDrawArcs(display, buffer, blackGC, arcs, num_circle);
				XClearArea(display, window, 0, 0, 0, 0, True);
				tv_prev = tv;
			}
		}
	}
}
