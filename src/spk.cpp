// in order to get function prototypes from glext.h,
// define GL_GLEXT_PROTOTYPES before including glext.h
#define GL_GLEXT_PROTOTYPES

#include <gtk/gtk.h>
#include <gtk/gtkgl.h>
#include <GL/glut.h>	// Header File For The GLUT Library
#include <GL/gl.h>		// Header File For The OpenGL32 Library
#include <GL/glu.h>		// Header File For The GLu32 Library
#include <GL/glx.h>		// Header file for the glx libraries.
#include "glext.h"
#include "glInfo.h"

#include <Cg/cg.h>		/* included in Cg toolkit for nvidia */
#include <Cg/cgGL.h>

#include <stdio.h>
#include <float.h>
#include <limits.h>
#include <sys/types.h>
#include <unistd.h>
#include <inttypes.h>
#include <sys/time.h>
#include <thread>
#include <algorithm>
#include <vector>
#include <sched.h>
#include <netinet/in.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>
#include <cstring>
#include <memory.h>
#include <math.h>
#include <arpa/inet.h>
#include <matio.h>
#include <armadillo>
#include <uuid.h>

#include <zmq.h>
#include "zmq_packet.h"

#include <basedir.h>
#include <basedir_fs.h>
#include "lconf.h"

#include <boost/multi_array.hpp>
#include <map>
#include <iostream>
#include <iomanip>
#include <atomic>
#include <mutex>

#include <libgen.h>

#include "gettime.h"
#include "cgVertexShader.h"
#include "vbo.h"
#include "vbo_raster.h"
#include "vbo_timeseries.h"
#include "firingrate.h"
#include "ks.h"
#include "spk.h"
#include "mmaphelp.h"
#include "fifohelp.h"
#include "channel.h"
#include "timesync.h"
#include "matStor.h"
#include "jacksnd.h"
#include "spikebuffer.h"
#include "util.h"
#include "lockfile.h"

#include "h5writer.h"
#include "h5spikewriter.h"

#include "fenv.h" // for debugging nan problems

//CG stuff. for the vertex shaders.
CGcontext   myCgContext;
CGprofile   myCgVertexProfile;
cgVertexShader		*g_vsFadeColor;
cgVertexShader		*g_vsThreshold;

using namespace std;
using namespace arma;

uuid_t	g_uuid;
char 	g_sess_start_t[sizeof("YYYY-MM-DDTHH:MM:SSZ")];

char	g_prefstr[256];

std::vector <void *> g_socks;

float	g_cursPos[2];
float	g_viewportSize[2] = {640, 480}; //width, height.

class Channel;

vector <VboTimeseries *> g_timeseries;
vector <VboRaster *> g_spikeraster;
vector <VboRaster *> g_eventraster;
u64 g_ec_stim = UINT_MAX;

float g_zoomSpan = 1.0;

bool g_vboInit = false;
float	g_rasterSpan = 10.f; // %seconds.

vector <Channel *> g_c;
vector <FiringRate *> g_boxcar;
vector <GaussianKernelSmoother *> g_ks;
TimeSyncClient *g_tsc;
GLuint 		g_base;            // base display list for the font set.

H5SpikeWriter	g_spikewriter;
gboolean 		g_saveUnsorted = true;
gboolean 		g_saveSpikeWF = true;

string g_boxcar_fifo_in  = "/tmp/boxcar_in.fifo";
string g_boxcar_fifo_out = "/tmp/boxcar_out.fifo";
string g_boxcar_mmap = "/tmp/boxcar.mmap";
double g_boxcar_bandwidth = 0.01; // seconds
bool g_boxcar_use_unsorted = false;
bool g_boxcar_enabled = false;

string g_gks_fifo_in  = "/tmp/gks_in.fifo";
string g_gks_fifo_out = "/tmp/gks_out.fifo";
string g_gks_mmap = "/tmp/gks.mmap";
double g_gks_bandwidth = 0.01; // seconds
bool g_gks_use_unsorted = false;
bool g_gks_enabled = false;


#if defined KHZ_24
double g_sr = 24414.0625;
#elif defined KHZ_48
double g_sr = 48828.1250;
#else
#error Bad sampling rate!
#endif

bool g_die = false;
double g_pause_time = -1.0;
gboolean g_pause = false;
gboolean g_autoChOffset = false;

gboolean g_showUnsorted = true;
gboolean g_showTemplate = true;
gboolean g_showPca 		= false;
gboolean g_showWFVgrid 	= true;
gboolean g_showISIhist 	= true;
gboolean g_showWFstd 	= true;

gboolean g_showContGrid = false;
gboolean g_showContThresh = true;

bool g_rtMouseBtn = false;

// for drawing circles around pca points
int 	g_polyChan = 0;
bool 	g_addPoly = false;

vector<int> g_channel {0,32,64,95};


int g_whichSpikePreEmphasis = 0;
enum EMPHASIS {
	EMPHASIS_NONE = 0,
	EMPHASIS_ABS,
	EMPHASIS_NEO
};

int g_whichAlignment = 0;
enum ALIGN {
	ALIGN_CROSSING = 0,
	ALIGN_MIN,
	ALIGN_MAX,
	ALIGN_ABS,
	ALIGN_SLOPE,
	ALIGN_NEO
};

float g_minISI = 1.3; //ms
float g_autoThreshold = -3.5; //standard deviations. default negative, w/e.
float g_neoThreshold = 8;
int g_spikesCols = 16;

int g_mode = MODE_RASTERS;
int g_drawmode[2] = {GL_POINTS, GL_LINE_STRIP};
int	g_drawmodep = 1;
int	g_blendmode[2] = {GL_ONE_MINUS_SRC_ALPHA, GL_ONE};
int g_blendmodep = 0;

//global labels..
GtkWidget *g_infoLabel;
GtkWidget *g_channelSpin[4] = {nullptr,nullptr,nullptr,nullptr};
GtkWidget *g_gainSpin[4] = {nullptr,nullptr,nullptr,nullptr};
GtkWidget *g_apertureSpin[4*NSORT];
GtkWidget *g_enabledChkBx[4] = {nullptr,nullptr,nullptr,nullptr};
GtkWidget *g_notebook;
int g_uiRecursion = 0; //prevents programmatic changes to the UI
// from causing commands to be sent to the headstage.

void saveState()
{
	printf("Saving Preferences to %s\n", g_prefstr);
	MatStor ms(g_prefstr); 	// no need to load before saving here
	for (auto &c : g_c)
		c->save(&ms);
	ms.setInt("channel", g_channel);

	ms.setStructValue("savemode", "unsorted_spikes", 0, (float)g_saveUnsorted);
	ms.setStructValue("savemode", "spike_waveforms", 0, (float)g_saveSpikeWF);

	ms.setStructValue("gui","draw_mode",0,(float)g_drawmodep);
	ms.setStructValue("gui","blend_mode",0,(float)g_blendmodep);

	ms.setStructValue("raster","show_grid",0,(float)g_showContGrid);
	ms.setStructValue("raster","show_threshold",0,(float)g_showContThresh);
	ms.setStructValue("raster","span",0,g_rasterSpan);

	ms.setStructValue("spike","pre_emphasis",0,(float)g_whichSpikePreEmphasis);
	ms.setStructValue("spike","alignment_mode",0,(float)g_whichAlignment);
	ms.setStructValue("spike","min_isi",0,g_minISI);
	ms.setStructValue("spike","auto_threshold",0,g_autoThreshold);
	ms.setStructValue("spike","neo_threshold",0,g_neoThreshold);
	ms.setStructValue("spike","cols",0,(float)g_spikesCols);

	ms.setStructValue("wf","show_unsorted",0,(float)g_showUnsorted);
	ms.setStructValue("wf","show_template",0,(float)g_showTemplate);
	ms.setStructValue("wf","show_pca",0,(float)g_showPca);
	ms.setStructValue("wf","show_grid",0,(float)g_showWFVgrid);
	ms.setStructValue("wf","show_isi",0,(float)g_showISIhist);
	ms.setStructValue("wf","show_std",0,(float)g_showWFstd);

	ms.setStructValue("wf","span",0,g_zoomSpan);

	ms.save();
}
void destroy(int)
{
	saveState(); 		// save the old values (do this first)
	g_die = true;		// tell threads to finish
	gtk_main_quit();	// tell gui thread to finish
	// now the rest of cleanup happens in main
}
static void die(void *ctx, int status)
{
	g_die = true;
	for (auto &sock : g_socks) {
		zmq_close(sock);
	}
	zmq_ctx_term(ctx);
	exit(status);
}
void BuildFont(void)
{
	Display *dpy;
	XFontStruct *fontInfo;  // storage for our font.

	g_base = glGenLists(96);                      // storage for 96 characters.

	// load the font.  what fonts any of you have is going
	// to be system dependent, but on my system they are
	// in /usr/X11R6/lib/X11/fonts/*, with fonts.alias and
	// fonts.dir explaining what fonts the .pcf.gz files
	// are.  in any case, one of these 2 fonts should be
	// on your system...or you won't see any text.

	// get the current display.  This opens a second
	// connection to the display in the DISPLAY environment
	// value, and will be around only long enough to load
	// the font.
	dpy = XOpenDisplay(nullptr); // default to DISPLAY env.
	fontInfo = XLoadQueryFont(dpy, "-adobe-helvetica-medium-r-normal--14-*-*-*-p-*-iso8859-1");
	if (fontInfo == nullptr) {
		fontInfo = XLoadQueryFont(dpy, "fixed");
		if (fontInfo == nullptr) {
			warn("no X font available?");
		}
	}
	// after loading this font info, this would probably be the time
	// to rotate, scale, or otherwise twink your fonts.

	// start at character 32 (space), get 96 characters
	// (a few characters past z), and store them starting at base.
	glXUseXFont(fontInfo->fid, 32, 96, g_base);

	// free that font's info now that we've got the display lists.
	XFreeFont(dpy, fontInfo);

	// close down the 2nd display connection.
	XCloseDisplay(dpy);
}
void KillFont(void)
{
	glDeleteLists(g_base, 96);  // delete all 96 characters.
}
void glPrint(char *text) // custom gl print routine.
{
	if (text == nullptr) { // if there's no text, do nothing.
		return;
	}
	glPushAttrib(GL_LIST_BIT);  // alert that we're about to offset the display lists with glListBase
	glListBase(g_base - 32);    // sets the base character to 32.

	// draws the display list text.
	glCallLists(strlen(text), GL_UNSIGNED_BYTE, text);
	glPopAttrib();  // undoes the glPushAttrib(GL_LIST_BIT);
}
//GLvoid printGLf(const char *fmt, ...)
//{
//	va_list ap;     /* our argument pointer */
//	char text[256];
//	if (fmt == NULL)    /* if there is no string to draw do nothing */
//		return;
//	va_start(ap, fmt);  /* make ap point to first unnamed arg */
//	/* FIXME: we *should* do boundschecking or something to prevent buffer
//	 * overflows/segmentations faults
//	 */
//	vsprintf(text, fmt, ap);
//	va_end(ap);
//	glPushAttrib(GL_LIST_BIT);
//	glListBase(g_base - 32);
//	glCallLists(strlen(text), GL_UNSIGNED_BYTE, text);
//	glPopAttrib();
//}
void updateCursPos(float x, float y)
{
	g_cursPos[0] = x/g_viewportSize[0];
	g_cursPos[1] = y/g_viewportSize[1];
	//convert to -1 to +1
	for (auto &g_cursPo : g_cursPos) {
		g_cursPo -= 0.5f;
		g_cursPo *= 2.f;
	}
	g_cursPos[1] *= -1; //zero at the top for gtk; bottom for opengl.
}
static gint motion_notify_event( GtkWidget *,
                                 GdkEventMotion *event )
{
	float x, y;
	int ix, iy;
	GdkModifierType state;
	if (event->is_hint) {
		gdk_window_get_pointer (event->window, &ix, &iy, &state);
		x = ix;
		y = iy;
	} else {
		x = event->x;
		y = event->y;
		state = (GdkModifierType)(event->state);
	}
	updateCursPos(x,y);
	if ((state & GDK_BUTTON1_MASK) && (g_mode == MODE_SORT)) {
		if (g_addPoly) {
			g_c[g_channel[g_polyChan]]->addPoly(g_cursPos);
			for (int i=1; i<4; i++) {
				int sames = 0;
				for (int j=1; j<4; j++) {
					if (g_channel[(g_polyChan+i)&3] == g_channel[(g_polyChan+i+j)&3])
						sames = 1;
				}
				if (!sames)
					g_c[g_channel[(g_polyChan+i)&3]]->resetPoly();
			}
		} else {
			g_c[g_channel[g_polyChan]]->mouse(g_cursPos);
		}
	}
	if ((state & GDK_BUTTON1_MASK) && (g_mode == MODE_SPIKES)) {
		warn("TODO: interact with channel");
	}
	if (state & GDK_BUTTON3_MASK)
		g_rtMouseBtn = true;
	else
		g_rtMouseBtn = false;
	return TRUE;
}
// forward declarations.
void updateChannelUI(int k);
static void getTemplateCB(GtkWidget *, gpointer _p);

static gint button_press_event( GtkWidget *,
                                GdkEventButton *event )
{
	updateCursPos(event->x,event->y);
	if (g_mode == MODE_SORT) {
		int u = 0;
		if (g_cursPos[0] > 0.0f)
			u += 1;
		if (g_cursPos[1] < 0.0f)
			u += 2;

		if (event->button == 1) { // left click
			g_polyChan = u;
			g_addPoly = false;
			if (event->type==GDK_2BUTTON_PRESS) {
				g_c[g_channel[u]]->computePca();
			} else if (event->type==GDK_3BUTTON_PRESS) {
				g_c[g_channel[u]]->resetPca();
			} else {
				if (!g_c[g_channel[u]]->mouse(g_cursPos)) {
					g_c[g_channel[u]]->resetPoly();
					g_c[g_channel[u]]->addPoly(g_cursPos);
					g_addPoly = true;
				}
			}
		}
		if (event->button == 3) { // right click
			if (g_c[g_channel[u]]->m_pcaVbo->m_polyW > 10 && g_mode == MODE_SORT) {

				u32 bits;

				GtkWidget *menu = gtk_menu_new();

				for (int j=0; j<NSORT; j++) {
					char buf[255];
					sprintf(buf, "set template %i", j+1);
					GtkWidget *menuitem = gtk_menu_item_new_with_label(buf);

					bits = 0;
					bits = u << 16; 	// left shift channel
					bits = bits | j; 	// OR in unit

					g_signal_connect(menuitem, "activate",
					                 G_CALLBACK(getTemplateCB), GUINT_TO_POINTER(bits));
					gtk_menu_shell_append(GTK_MENU_SHELL(menu), menuitem);
				}

				gtk_widget_show_all(menu);

				/* Note: event can be NULL here when called from view_onPopupMenu;
				 *  gdk_event_get_time() accepts a NULL argument */
				gtk_menu_popup(GTK_MENU(menu), nullptr, nullptr, nullptr, nullptr,
				               (event != nullptr) ? event->button : 0,
				               gdk_event_get_time((GdkEvent *)event));
			}
		}
	}
	if (g_mode == MODE_SPIKES) {
		int nc = (int)g_c.size();
		int spikesRows = nc / g_spikesCols;
		if (nc % g_spikesCols) spikesRows++;
		float xf = g_spikesCols;
		float yf = spikesRows;
		float x = (g_cursPos[0] + 1.f)/ 2.f;
		float y = (g_cursPos[1]*-1.f + 1.f)/2.f; //0,0 = upper left hand corner.
		int sc = (int)floor(x*xf);
		int sr = (int)floor(y*yf);
		if (event->button==1 && event->type==GDK_2BUTTON_PRESS) { // double (left) click
			int h =  sr*g_spikesCols + sc;
			if (h >= 0 && h < nc) {
				//shift channels down, like a priority queue.
				for (int i=g_channel.size()-1; i>0; i--) {
					g_channel[i] = g_channel[i-1];
				}
				g_channel[0] = h;
				debug("channel switched to %d", g_channel[0]+1);
				g_mode = MODE_SORT;
				gtk_notebook_set_current_page(GTK_NOTEBOOK(g_notebook), MODE_SORT);
			}
			//update the UI elements.
			for (size_t i=0; i<g_channel.size(); i++) {
				gtk_spin_button_set_value(GTK_SPIN_BUTTON(g_channelSpin[i]), g_channel[i]+1);
				updateChannelUI(i);
			}
		}

		if (event->button==3) { // (right click)
			int h =  sr*g_spikesCols + sc;
			if (h >= 0 && h < nc) {
				g_c[h]->toggleEnabled();
				for (size_t i=0; i<g_channel.size(); i++) {
					if (g_channel[i] == h) {
						updateChannelUI(i);
					}
				}
			}
		}
	}
	return TRUE;
}
static gboolean
expose1 (GtkWidget *da, GdkEventExpose *, gpointer )
{
	if (g_die) {
		return false;
	}

	GdkGLContext *glcontext = gtk_widget_get_gl_context (da);
	GdkGLDrawable *gldrawable = gtk_widget_get_gl_drawable (da);

	if (!gdk_gl_drawable_gl_begin (gldrawable, glcontext))
		g_assert_not_reached ();

	//copy over any new data.
	if (!g_pause) {
		for (auto &x : g_timeseries) {
			x->copy();
		}
		for (auto &x : g_spikeraster) { // spikes
			x->copy();
		}
		for (auto &x : g_eventraster) { // non-spike events
			x->copy();
		}
		for (auto &c : g_c) //and the waveform buffers
			c->copy();
	}

	/* draw in here */
	glMatrixMode(GL_MODELVIEW);
	glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
	glShadeModel(GL_FLAT);
	glBlendFunc(GL_SRC_ALPHA, g_blendmode[g_blendmodep]);
	//draw the cursor no matter what?
	if (0) {
		float x = g_cursPos[0] + 1.f/g_viewportSize[0];
		float y = g_cursPos[1] + 1.f/g_viewportSize[1];
		glBegin(GL_LINES);
		glColor4f(1.f, 1.f, 1.f, 0.75);
		glVertex3f( x-0.1, y, 0.f);
		glVertex3f( x+0.1, y, 0.f);
		glVertex3f( x, y-0.1, 0.f);
		glVertex3f( x, y+0.1, 0.f);
		glEnd();
	}

	float time = g_pause ? g_pause_time : (float)gettime();

	if (g_mode == MODE_RASTERS) {

		//glPushMatrix();
		//glScalef(1.f, 0.5f, 1.f); //don't think this does anythaaang.
		//glTranslatef(0.f,0.5f, 0.f);

		//draw zero lines for the 4 continuous channels.
		for (int k=0; k<NFBUF; k++) {
			glLineWidth(1.f);
			glBegin(GL_LINES);
			glColor4f(0.f, 0.5, 1.f, 0.75); //blue, center line
			glVertex3f( -1.f, (float)((3-k)*2+1)/8.f, 0.f);
			glVertex3f( 1.f, (float)((3-k)*2+1)/8.f, 0.f);
			if (g_showContGrid) {
				glColor4f(0.f, 0.8f, 0.75f, 0.5);//green, edge lines
				glVertex3f( -1.f, (float)((3-k)*2)/8.f, 0.f);
				glVertex3f( 1.f, (float)((3-k)*2)/8.f, 0.f);
			}
#ifndef EMG
			//draw threshold.
			if (g_showContThresh) {
				float th = g_c[g_channel[k]]->getThreshold();
				float gn = g_c[g_channel[k]]->getGain();
				glColor4f (1., 0.0f, 0.0f, 0.35);
				float y = (float)((3-k)*2+1)/8.f +
				          (th*gn)/(8.f);
				glVertex3f(-1.f, y, 0.f);
				glVertex3f( 1.f, y, 0.f);
			}
#endif
			glEnd();
		}

		int n = g_timeseries.size();
		for (int k=0; k<n; k++) {
			float yoffset = 1.f - (float)(k+1)/n + 1.f/(2.f*n);
			g_timeseries[k]->draw(g_drawmode[g_drawmodep], (float)n, yoffset);

			//labels.
			glColor4f(1.f, 1.f, 1.f, 0.5);
			glRasterPos2f(-1.f, yoffset +
			              2.f*2.f/g_viewportSize[1]); // 2px vertical offset
			//kearning is from the lower right hand corner.
			char buf[128];
			if (g_c[g_channel[k]]->m_chanName.length() > 0) {
				snprintf(buf, 128, "%c: %s", 'A'+k, g_c[g_channel[k]]->m_chanName.c_str());
			} else {
				snprintf(buf, 128, "%c: %d", 'A'+k, g_channel[k]+1);
			}
			glPrint(buf);
		}

		u32 nplot = g_timeseries[0]->m_nplot;

		//draw seconds / ms label here.
		for (u32 i=0; i<nplot; i+=SRATE_HZ/5) {
			float x = 2.f*i/nplot-1.f + 2.f/g_viewportSize[0];
			float y = 1.f - 13.f*2.f/g_viewportSize[1];
			glRasterPos2f(x,y);
			char buf[64];
			snprintf(buf, 64, "%3.2f", i/SRATE_HZ);
			glPrint(buf);
		}

		if (g_showContGrid) {
			glColor4f(0.f, 0.8f, 0.75f, 0.35);
			glBegin(GL_LINES);
			for (u32 i=0; i<nplot; i+=SRATE_HZ/10) {
				float x = 2.f*i/nplot-1.f;
				glVertex2f(x, 0.f);
				glVertex2f(x, 1.f);
			}
			glEnd();
		}


#ifndef EMG
		//rasters
		float vscale = g_c.size() + 1;
		for (auto &o : g_eventraster) {
			vscale += o->size();
		}

		glShadeModel(GL_FLAT);
		glPushMatrix();
		glScalef(1.f/g_rasterSpan, -1.f/vscale, 1.f);

		int lt = (int)time / (int)g_rasterSpan; // why these two lines, not:
		lt *= (int)g_rasterSpan;				// lt = (int)time ???
		float x = time - (float)lt;
		float adj = 0.f;
		float movtime = 0.25 + log10(g_rasterSpan);
		if (x < movtime) {
			x /= movtime;
			adj = 2.f*x*x*x -3.f*x*x + 1;
			adj *= g_rasterSpan;
		}
		glTranslatef((0 - (float)lt + adj), 1.f, 0.f);

		//draw current time.
		glColor4f (1., 0., 0., 0.5);
		glBegin(GL_LINES);
		glVertex3f( time, 0, 0.f);
		glVertex3f( time, vscale, 0.f);
		glColor4f (0.5, 0.5, 0.5, 0.5);
		//draw old times, every second.
		for (int t=(int)time; t > time-g_rasterSpan*2; t--) {
			glVertex3f( (float)t, 0, 0.f);
			glVertex3f( (float)t, vscale, 0.f);
		}
		glEnd();

		for (auto &o : g_spikeraster) {
			o->draw();
		}

		glTranslatef(0.f, g_c.size(), 0.f);
		for (auto &o : g_eventraster) {
			o->draw();
			glTranslatef(0.f, o->size(), 0.f);
		}

		//glEnable(GL_LINE_SMOOTH);
		glPopMatrix (); //so we don't have to worry about time.

		//draw current channel
		for (int k=0; k<4; k++) {
			glBegin(GL_LINE_STRIP);
			glColor4f (1., 0., 0., 0.5);
			float y = (float)(1.f+g_channel[k])/(-1.f*vscale);
			glVertex3f( -1.f, y, 0.f);
			glVertex3f( 1.f, y, 0.f);
			glEnd();
			//labels.
			glRasterPos2f(1.f - 2.f*35.f/g_viewportSize[1],
			              y+ 2.f*2.f/g_viewportSize[1]); //2 pixels vertical offset.
			// kerning is from the lower right hand corner.
			char buf[128] = {0,0,0,0};
			snprintf(buf, 128, "%c %d", 'A'+k, g_channel[k]);
			glPrint(buf);
		}

#endif
		//end VBO
	}

	if (g_mode == MODE_SORT || g_mode == MODE_SPIKES) {
		glPushMatrix();
		glEnableClientState(GL_VERTEX_ARRAY);
		float xo, yo, xz, yz;
		cgGLEnableProfile(myCgVertexProfile);
		if (g_blendmode[g_blendmodep] == GL_ONE)
			g_vsFadeColor->setParam(2,"ascale", 0.2f);
		else
			g_vsFadeColor->setParam(2,"ascale", 1.f);
		cgGLDisableProfile(myCgVertexProfile);
		if (g_mode == MODE_SORT) {
			for (int k=0; k<4; k++) {
				//2x2 array.
				xo = (k%2)-1.f;
				yo = 0.f-(k/2);
				xz = 1.f;
				yz = 1.f;
				g_c[g_channel[k]]->setLoc(xo, yo, xz, yz);
				g_c[g_channel[k]]->draw(g_drawmode[g_drawmodep], time, g_cursPos,
				                        g_rtMouseBtn, true);
			}
		}
		if (g_mode == MODE_SPIKES) {
			int nc = (int)g_c.size();
			int spikesRows = nc / g_spikesCols;
			if (nc % g_spikesCols) spikesRows++;
			float xf = g_spikesCols;
			float yf = spikesRows;
			xz = 2.f/xf;
			yz = 2.f/yf;
			for (int k=0; k<nc; k++) {
				xo = (k%g_spikesCols)/xf;
				yo = ((k/g_spikesCols)+1)/yf;
				g_c[k]->setLoc(xo*2.f-1.f, 1.f-yo*2.f, xz*2.f, yz);
				g_c[k]->draw(g_drawmode[g_drawmodep], time, g_cursPos,
				             g_rtMouseBtn, false);
			}
			//draw some lines.
			glBegin(GL_LINES);
			glColor4f(1.f, 1.f, 1.f, 0.4);
			for (int c=1; c<g_spikesCols; c++) {
				float cf = (float)c / g_spikesCols;
				glVertex2f(cf*2.f-1.f, -1.f);
				glVertex2f(cf*2.f-1.f, 1.f);
			}
			for (int r=1; r<spikesRows; r++) {
				float rf = (float)r / spikesRows;
				glVertex2f(-1.f, rf*2.f-1.f);
				glVertex2f( 1.f, rf*2.f-1.f);
			}
			glEnd();

			// label the current audio channel (A) red
			// and the other 3 channels (B-D) yellow
			// reverse typical loop order to draw ch A on top
			for (int k=3; k>=0; k--) {
				glLineWidth(2.f);
				glBegin(GL_LINE_STRIP);
				if (k==0)
					//glColor4f(1.f, 0.f, 0.f, 1.f);
					glColor4ub(239,59,44,255);
				else
					glColor4ub(255,237,160,255);
				//glColor4f(1.f, 1.f, 0.f, 1.f);
				int h = g_channel[k];
				xo = (h%g_spikesCols)/xf;
				xo *= 2.f;
				xo -= 1.f;
				yo = ((h/g_spikesCols)+1)/yf;
				yo *= -2.f;
				yo += 1.f;
				float xa, ya;
				xa = ya = 0.f;
				if (xo <= -1.f) xa = 2.0/g_viewportSize[0]; //one pixel.
				if (yo <= -1.f) ya = 2.0/g_viewportSize[1];
				glVertex2f(xo+xa, yo+ya);
				glVertex2f(xo+xa, yo+yz);
				glVertex2f(xo+xz, yo+yz);
				glVertex2f(xo+xz, yo+ya);
				glVertex2f(xo+xa, yo+ya);
				glEnd();
			}
		}
		glPopMatrix();
	}

	glDisableClientState(GL_VERTEX_ARRAY);

	if (gdk_gl_drawable_is_double_buffered (gldrawable))
		gdk_gl_drawable_swap_buffers (gldrawable);
	else
		glFlush ();

	gdk_gl_drawable_gl_end (gldrawable);

	return TRUE;
}

static gboolean
configure1(GtkWidget *da, GdkEventConfigure *, gpointer)
{
	GdkGLContext *glcontext = gtk_widget_get_gl_context (da);
	GdkGLDrawable *gldrawable = gtk_widget_get_gl_drawable (da);

	if (!gdk_gl_drawable_gl_begin (gldrawable, glcontext))
		g_assert_not_reached ();

	// save the viewport size.
	GtkAllocation allocation;
	gtk_widget_get_allocation(da, &allocation);

	glLoadIdentity();
	glViewport (0, 0, allocation.width, allocation.height);
	g_viewportSize[0] = (float)allocation.width;
	g_viewportSize[1] = (float)allocation.height;
	/*printf("allocation.width %d allocation_height %d\n",
		allocation.width, allocation.height); */
	glMatrixMode(GL_PROJECTION);
	glLoadIdentity();
	glOrtho (-1,1,-1,1,0,1);
	glEnable(GL_BLEND);
	//glEnable(GL_LINE_SMOOTH);
	//glEnable(GL_DEPTH_TEST);
	//glDepthMask(GL_TRUE);
	glMatrixMode(GL_MODELVIEW);
	glLoadIdentity();

	if (!g_vboInit) { //start it up!
		//start up Cg first.(glInfo seems to trample some structures)
		myCgContext = cgCreateContext();
		checkForCgError("creating Cg context\n");
		cgGLSetDebugMode(CG_FALSE);
		cgSetParameterSettingMode(myCgContext, CG_DEFERRED_PARAMETER_SETTING);
		checkForCgError("Cg parameter setting\n");

		myCgVertexProfile = cgGLGetLatestProfile(CG_GL_VERTEX);
		cgGLSetOptimalOptions(myCgVertexProfile);
		checkForCgError("selecting vertex profile");

		char linkname[4096];
		char *dname;
		// get directory of current exe
		ssize_t r = readlink("/proc/self/exe", linkname, 4096);
		if (r < 0) {
			perror("/proc/self/exe");
			exit(EXIT_FAILURE);
		}
		dname = dirname(linkname);
		string d(dname);

		string cgfile;

		cgfile = d + "/../cg/fadeColor.cg";
		g_vsFadeColor = new cgVertexShader(cgfile.c_str(),"fadeColor");
		g_vsFadeColor->addParams(5,"time","fade","col","off","ascale");

		cgfile = d + "/../cg/threshold.cg";
		g_vsThreshold = new cgVertexShader(cgfile.c_str(),"thresholdB");
		g_vsThreshold->addParams(3, "xzoom", "nchan", "yoffset");

		//now the vertex buffers.
		glInfo glInfo;
		glInfo.getInfo();
		printf("OpenGL (%s) version: %s\n",
		       glInfo.vendor.c_str(),
		       glInfo.version.c_str());
		printf("GLSL version: %s\n", glInfo.glslVersion.c_str());
		printf("Renderer: %s\n", glInfo.renderer.c_str());
		if (glInfo.isExtensionSupported("GL_ARB_vertex_buffer_object")) {
			printf("Video card supports GL_ARB_vertex_buffer_object.\n");
		} else {
			error("Video card does NOT support GL_ARB_vertex_buffer_object");
			exit(1);
		}

		for (auto &x : g_timeseries) {
			x->configure();
			x->setVertexShader(g_vsThreshold);
			x->setCGProfile(myCgVertexProfile);
			x->setNPlot(g_zoomSpan * SRATE_HZ);
		}

		for (auto &x : g_spikeraster) {
			x->configure();
		}
		for (auto &x : g_eventraster) {
			x->configure();
		}

		for (auto &x : g_c) {
			x->configure(g_vsFadeColor);
		}

		g_vboInit = true;

	}
	BuildFont(); //so we're in the right context?

	gdk_gl_drawable_gl_end (gldrawable);

	return TRUE;
}

static gboolean rotate(gpointer user_data)
{
	GtkWidget *da = GTK_WIDGET(user_data);
	GdkWindow *win = gtk_widget_get_window(da);

	GtkAllocation allocation;
	gtk_widget_get_allocation (da, &allocation);

	gdk_window_invalidate_rect(win, &allocation, FALSE);
	gdk_window_process_updates (win, FALSE);

	gtk_label_set_text(GTK_LABEL(g_infoLabel), g_tsc->getInfo().c_str());

	g_spikewriter.draw();

	return TRUE;
}
void destroyGUI(GtkWidget *, gpointer)
{
	destroy(SIGINT);
}
void sorter(int ch, void *sock)
{
	float 	wf_sp[3*NWFSAMP];
	float 	neo_sp[3*NWFSAMP];
	u32 	tk_sp[3*NWFSAMP];

	float threshold;
	if (g_whichSpikePreEmphasis == 2) {
		threshold = g_neoThreshold;
	} else {
		threshold = g_c[ch]->getThreshold(); // 1 -> 10mV.
	}


	while (g_c[ch]->m_spkbuf.getSpike(tk_sp, wf_sp, neo_sp, 3*NWFSAMP, threshold, NWFSAMP, g_whichSpikePreEmphasis)) {
		// ask for twice the width of a spike waveform so that we may align

		int a = floor(NWFSAMP/2.0);
		int b = floor(NWFSAMP/2.0)+NWFSAMP;

		int centering = a;
		float v;

		switch (g_whichAlignment) {
		case ALIGN_CROSSING:
			//centering = (float)NWFSAMP - g_c[ch]->getCentering();
			centering = g_c[ch]->getCentering() + a;
			break;
		case ALIGN_MIN:
			v = FLT_MAX;
			for (int i=a; i<b; i++) {
				if (v > wf_sp[i]) {
					v = wf_sp[i];
					centering = i + floor(NWFSAMP/8.0);
				}
			}
			break;
		case ALIGN_MAX:
			v = FLT_MIN;
			for (int i=a; i<b; i++) {
				if (v < wf_sp[i]) {
					v = wf_sp[i];
					centering = i + floor(NWFSAMP/8.0);
				}
			}

			break;
		case ALIGN_ABS:
			v = FLT_MIN;
			for (int i=a; i<b; i++) {
				if (v < fabs(wf_sp[i])) {
					v = fabs(wf_sp[i]);
					centering = i + floor(NWFSAMP/8.0);
				}
			}

			break;
		case ALIGN_SLOPE:
			v = FLT_MIN;
			for (int i=a; i<b; i++) {
				if (v < wf_sp[i+1]-wf_sp[i]) {
					v = wf_sp[i+1]-wf_sp[i];
					centering = i + floor(NWFSAMP/8.0);
				}
			}

			break;
		case ALIGN_NEO: {
			v = FLT_MIN;
			for (int i=a; i<b; i++) {
				if (v < neo_sp[i]) {
					v = neo_sp[i];
					centering = i + floor(NWFSAMP/8.0);
				}
			}
		}
		break;
		default:
			error("bad alignment type. exiting");
			exit(1);
		}

		size_t idx = centering-(int)floor(NWFSAMP/2.0);

		u32 tk = tk_sp[centering]; // alignment time

		int unit = 0; // unsorted.
		vec mse(NSORT);
		mse.zeros();
		for (int u=0; u<NSORT; u++) { // compare to template.
			for (int j=0; j<NWFSAMP; j++) {
				double r = wf_sp[idx+j] - g_c[ch]->m_template[u][j];
				mse(u) += r*r;
			}
		}
		mse /= NWFSAMP;
		uword z;
		double min_mse = mse.min(z);
		if (min_mse < g_c[ch]->getAperture(z)) {
			unit = z+1;
		}

		// check if this exceeds minimum ISI.
		// wftick is indexed to the start of the waveform.
		bool passed = true;
		if (unit > 0) { // sorted
			passed = (tk - g_c[ch]->m_lastSpike[unit-1]) > g_minISI*SRATE_KHZ;
		}

		if (passed) {
			long double the_time = g_tsc->getTime(tk);
			g_c[ch]->addWf(&wf_sp[idx], unit, the_time, true);
			g_c[ch]->updateISI(unit, tk); // does nothing for unit==0
			if (g_spikewriter.isEnabled() && (unit > 0 || g_saveUnsorted)) {
				SPIKE *s;
				s = new SPIKE;	// deleted by other thread
				s->ch = ch+1;	// 1-indexed
				s->un = unit;
				s->tk = tk;
				s->ts = the_time;
				if (g_saveSpikeWF) {
					s->nwf = NWFSAMP;
					s->wf = new float[s->nwf];
					for (size_t g=0; g<s->nwf; g++) {
						s->wf[g] = wf_sp[idx+g] * 1e4;
					}
				} else {
					s->nwf = 0;
					s->wf = new float[0];
				}
				g_spikewriter.add(s); // other thread deletes memory
			}
			if (unit < NUNIT) { // should always be true


				// XXX HACK HACK HACK FIX ME FIX ME XXX
				if (unit == 4 && ch == 0) {
					zmq_send(sock, "x", 1, 0);
				}

				if (unit > 0) { // for drawing, excluding unsorted
					int uu = unit-1;
					g_spikeraster[uu]->addEvent((float)the_time, ch);
				}

				if (g_boxcar_enabled) {
					if (g_boxcar_use_unsorted) {
						g_boxcar[ch*NUNIT+unit]->add(the_time);
					} else {
						g_boxcar[ch*NSORT+unit-1]->add(the_time);
					}
				}

				if (g_gks_enabled) {
					if (g_gks_use_unsorted) {
						g_ks[ch*NUNIT+unit]->add(the_time);
					} else {
						g_ks[ch*NSORT+unit-1]->add(the_time);
					}
				}
			}
		}
	}
}
void spikewrite()
{
	while (!g_die) {
		if (g_spikewriter.write()) //if it can write, it will.
			usleep(1e4); //poll qicker.
		else
			usleep(1e5);
	}
}
void worker(void *ctx, const char *zb, const char *ze, const char *zs)
{

	// Prepare our sockets

	void *neural_sock = zmq_socket(ctx, ZMQ_SUB);
	zmq_connect(neural_sock, zb);
	zmq_setsockopt(neural_sock, ZMQ_SUBSCRIBE, "", 0);

	void *events_sock = zmq_socket(ctx, ZMQ_SUB);
	zmq_connect(events_sock, ze);
	zmq_setsockopt(events_sock, ZMQ_SUBSCRIBE, "", 0);

	// for control input
	void *controller = zmq_socket(ctx, ZMQ_SUB);
	zmq_connect(controller, "inproc://controller");
	zmq_setsockopt(controller, ZMQ_SUBSCRIBE, "", 0);

	// spike socket
	void *spike_sock = zmq_socket(ctx, ZMQ_PUB);
	zmq_bind(spike_sock, zs);

	// init poll set
	zmq_pollitem_t items [] = {
		{ neural_sock, 	0, ZMQ_POLLIN, 0 },
		{ events_sock, 	0, ZMQ_POLLIN, 0 },
		{ controller, 	0, ZMQ_POLLIN, 0 }
	};

	while (true) {

		if (zmq_poll(items, 3, -1) == -1) {
			break;
		}

		if (items[0].revents & ZMQ_POLLIN) {
			zmq_msg_t header;
			zmq_msg_init(&header);
			zmq_msg_recv(&header, neural_sock, 0);
			zmq_packet_header *p = (zmq_packet_header *)zmq_msg_data(&header);
			u64 ns = p->ns;
			i64 tk = p->tk;

			zmq_msg_t body;
			zmq_msg_init(&body);
			zmq_msg_recv(&body, neural_sock, 0);
			float *f = (float *)zmq_msg_data(&body);

			auto x = new float[ns];

			// input data is scaled from TDT so that 32767 = 10mV.
			// send the data for one channel to jack
			for (int h=0; h<NFBUF; h++) {
				int ch = g_channel[h];
				float gain = g_c[ch]->getGain();
				for (size_t k=0; k<ns; k++) {
					// scale into a reasonable range for audio
					// and timeseries display
					x[k] = f[ch*ns+k] * gain / 1e4;
				}
				g_timeseries[h]->addData(x, ns); // timeseries trace
				if (h==0) {
#ifdef JACK
					jackAddSamples(x, x, ns);
#endif
				}
			}

			delete[] x;

			// package data for sorting / saving
			for (auto &ch : g_c) {
				for (size_t k=0; k<ns; k++) {
					auto samp = f[ch->m_ch*ns+k] / 1e4; // scale so 1 = +10 mV
					// 1 = +10mV; range = [-1 1] here.
					ch->m_spkbuf.addSample(tk+k, samp);
					// update the channel running stats (means and stddevs, etc).
					ch->m_wfstats(samp);
					// sort -- see if samples pass threshold. if so, copy.
					if (ch->getEnabled()) {
						sorter(ch->m_ch, spike_sock); // XXX put this into channel class?
					}
				}
			}

			zmq_msg_close(&header);
			zmq_msg_close(&body);
		}

		if (items[1].revents & ZMQ_POLLIN) {

			auto check_bit = [](u16 var, int n) -> bool {
				if (n < 0 || n > 15)
				{
					return false;
				}
				return (var) & (1<<n);
			};

			zmq_msg_t header;
			zmq_msg_init(&header);
			zmq_msg_recv(&header, events_sock, 0);
			zmq_packet_header *p = (zmq_packet_header *)zmq_msg_data(&header);
			u64 nc = p->nc;
			u64 ns = p->ns;
			i64 tk = p->tk;

			zmq_msg_t body;
			zmq_msg_init(&body);
			zmq_msg_recv(&body, events_sock, 0);
			i16 *ev = (i16 *)zmq_msg_data(&body);

			for (u64 ec=0; ec<nc; ec++) {
				for (u64 k=0; k<ns; k++) {
					i16 x = ev[ec*ns+k];
					if (x != 0) {
						if (ec == g_ec_stim) {
							for (int i=0; i<16; i++) {
								if (check_bit(x, i)) {
									g_eventraster[ec]->addEvent(g_tsc->getTime(tk+k), i);
								}
							}
						} else {
							g_eventraster[ec]->addEvent(g_tsc->getTime(tk+k), 0);
						}
					}
				}
			}

			zmq_msg_close(&header);
			zmq_msg_close(&body);
		}

		if (items[2].revents & ZMQ_POLLIN) {
			break;
		}

	}

	zmq_close(controller);
	zmq_close(events_sock);
	zmq_close(neural_sock);
	zmq_close(spike_sock);

}

void flush_pipe(int fid)
{
	fcntl(fid, F_SETFL, O_NONBLOCK);
	char *d = (char *)malloc(1024*8);
	int r = read(fid, d, 1024*8);
	printf("flushed %d bytes\n", r);
	free(d);
	int opts = fcntl(fid,F_GETFL);
	opts ^= O_NONBLOCK;
	fcntl(fid, F_SETFL, opts);
}

void boxcar_binner()
{
	// m = memmapfile('/tmp/boxcar.mmap', 'Format', {'uint16' [96*4] 'x'})
	// A = m.Data(1).x;

	printf("Boxcar Binner, ");
	auto nc = g_boxcar.size();
	printf("%lu features\n", nc);
	size_t length = nc*sizeof(u16);
	auto mmh = new mmapHelp(length, g_boxcar_mmap.c_str());
	volatile u16 *bin = (u16 *)mmh->m_addr;
	mmh->prinfo();

	auto pipe_out = new fifoHelp(g_boxcar_fifo_out.c_str());
	pipe_out->prinfo();

	auto pipe_in = new fifoHelp(g_boxcar_fifo_in.c_str());
	pipe_in->setR(); // so we can poll
	pipe_in->prinfo();

	for (size_t i=0; i<nc; i++) {
		bin[i] = 0;
	}
	flush_pipe(pipe_out->m_fd);

	while (!g_die) {
		if (pipe_in->Poll(1000)) {
			double reqTime = 0.0;
			int r = read(pipe_in->m_fd, &reqTime, 8); // send it the time you want to sample,
			double end = (reqTime > 0) ? reqTime : (double)gettime(); // < 0 to bin 'now'
			if (r >= 3) { // why 3 not 8?
				for (size_t i=0; i<nc; i++) {
					bin[i] = g_boxcar[i]->get_count_in_bin(end);
				}
				usleep(100); // seems reliable with this in place.
				write(pipe_out->m_fd, "go\n", 3);
			} else
				usleep(100000);
		}
	}
	delete mmh;
	delete pipe_in;
	delete pipe_out;
}

void gks_binner()
{
	// m = memmapfile('/tmp/gks.mmap', 'Format', {'uint16' [96*4] 'x'})
	// A = m.Data(1).x;

	printf("Gaussian Kernel Smoother Binner, ");
	auto nc = g_ks.size();

	// XXXXXXXX FIX ME
	// WHY IS THIS 960 RATHER THAN 96 OR 192???!?!?!?

	printf("%lu features\n", nc);
	size_t length = nc*sizeof(u16);
	auto mmh = new mmapHelp(length, g_gks_mmap.c_str());
	volatile u16 *bin = (u16 *)mmh->m_addr;
	mmh->prinfo();

	auto pipe_out = new fifoHelp(g_gks_fifo_out.c_str());
	pipe_out->prinfo();

	auto pipe_in = new fifoHelp(g_gks_fifo_in.c_str());
	pipe_in->setR(); // so we can poll
	pipe_in->prinfo();

	for (size_t i=0; i<nc; i++) {
		bin[i] = 0;
	}
	flush_pipe(pipe_out->m_fd);

	while (!g_die) {
		if (pipe_in->Poll(1000)) {
			double reqTime = 0.0;
			int r = read(pipe_in->m_fd, &reqTime, 8); // send it the time you want to sample,
			double end = (reqTime > 0) ? reqTime : (double)gettime(); // < 0 to bin 'now'
			if (r >= 3) { // why 3 not 8?
				for (size_t i=0; i<nc; i++) {
					bin[i] = g_ks[i]->get_rate(end);
				}
				usleep(100); // seems reliable with this in place.
				write(pipe_out->m_fd, "go\n", 3);
			} else
				usleep(100000);
		}
	}
	delete mmh;
	delete pipe_in;
	delete pipe_out;
}

void updateChannelUI(int k)
{
	//called when a channel changes -- update the UI elements accordingly.
	g_uiRecursion++;
	int ch = g_channel[k];
	gtk_spin_button_set_value(GTK_SPIN_BUTTON(g_gainSpin[k]), g_c[ch]->getGain());
	for (int i=0; i<NSORT; i++) {
		gtk_spin_button_set_value(GTK_SPIN_BUTTON(g_apertureSpin[k*NSORT+i]), g_c[ch]->getApertureUv(i));
	}
	gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(g_enabledChkBx[k]), g_c[ch]->getEnabled());
	g_uiRecursion--;
}
static void channelSpinCB(GtkWidget *spinner, gpointer p)
{
	int k = (int)((i64)p & 0xf);
	int ch = gtk_spin_button_get_value_as_int(GTK_SPIN_BUTTON(spinner));
	ch--; // make zero indexed
	int nc = (int)g_c.size();
	debug("channelSpinCB: %d", ch+1);
	if (ch < nc && ch >= 0 && ch != g_channel[k]) {
		g_channel[k] = ch;
		updateChannelUI(k); //update the UI too.
	}
	//if we are in sort mode, and k == 0, move the other channels ahead of us.
	//this allows more PCA points for sorting!
	if (k == 0 && g_autoChOffset) {
		for (size_t j=1; j<g_channel.size(); j++) {
			g_channel[j] = (g_channel[0] + j) % nc;
			//this does not recurse -- have to set the other stuff manually.
			g_uiRecursion++;
			gtk_spin_button_set_value(GTK_SPIN_BUTTON(g_channelSpin[j]), (double)g_channel[j]+1);
			g_uiRecursion--;
		}
		//loop over & update the UI afterward, so we don't have a race-case.
		for (int j=1; j<4; j++) {
			updateChannelUI(j);
		}
	}
}
static void gainSpinCB(GtkWidget *spinner, gpointer p)
{
	int x = (int)((i64)p & 0xf);
	if (!g_uiRecursion) {
		float gain = gtk_spin_button_get_value(GTK_SPIN_BUTTON(spinner));
		debug("ch %d (%d) gainSpinCB: %f", g_channel[x], x, gain);
		g_c[g_channel[x]]->setGain(gain);
		g_c[g_channel[x]]->resetPca();
	}
}
static void basic_checkbox_cb(GtkWidget *button, gpointer p)
{
	gboolean *b = (gboolean *)p;
	*b = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(button)) ?
	     true :
	     false;
}
static void basic_spinfloat_cb(GtkWidget *spinner, gpointer p)
{
	float *f = (float *)p;
	*f = (float)gtk_spin_button_get_value(GTK_SPIN_BUTTON(spinner));
}
static void basic_spinint_cb(GtkWidget *spinner, gpointer p)
{
	int *x = (int *)p;
	*x = gtk_spin_button_get_value_as_int(GTK_SPIN_BUTTON(spinner));
}
static void notebookPageChangedCB(GtkWidget *,
                                  gpointer, int page, gpointer)
{
	// special-case the save tab (don't change the view)
	g_mode = (page != 4) ? page : g_mode;
}
static GtkWidget *mk_spinner(const char *txt, GtkWidget *container,
                             float start, float min, float max, float step,
                             GtkCallback cb, gpointer data)
{
	GtkWidget *bx = gtk_hbox_new (FALSE, 1);

	GtkWidget *label = gtk_label_new (txt);
	gtk_box_pack_start (GTK_BOX (bx), label, TRUE, TRUE, 2);
	gtk_widget_show(label);
	GtkObject *adj = gtk_adjustment_new(start, min, max, step, step, 0.0);
	float climb = 0.0;
	int digits = 0;

	if (step <= 1e-6) {
		climb = 1e-7;
		digits = 7;
	} else if (step <= 1e-5) {
		climb = 1e-6;
		digits = 6;
	} else if (step <= 1e-4) {
		climb = 1e-5;
		digits = 5;
	} else if (step <= 1e-3) {
		climb = 1e-4;
		digits = 4;
	} else if (step <= 1e-2) {
		climb = 1e-3;
		digits = 3;
	} else if (step <= 0.1) {
		climb = 1e-2;
		digits = 2;
	} else if (step <= 0.99) {
		climb = 0.1;
		digits = 1;
	}

	GtkWidget *spinner = gtk_spin_button_new(
	                         GTK_ADJUSTMENT(adj), climb, digits);
	gtk_spin_button_set_wrap (GTK_SPIN_BUTTON(spinner), FALSE);
	gtk_box_pack_start(GTK_BOX(bx), spinner, TRUE, TRUE, 2);
	g_signal_connect(spinner, "value-changed", G_CALLBACK(cb), data);
	gtk_widget_show(spinner);

	gtk_box_pack_start (GTK_BOX (container), bx, TRUE, TRUE, 2);
	return spinner;
}
static GtkWidget *mk_radio(const char *txt, int ntxt,
                           GtkWidget *container, bool vertical,
                           const char *frameTxt, int radio_state, GtkCallback cb)
{
	GtkWidget *frame, *button, *modebox;

	frame = gtk_frame_new (frameTxt);
	gtk_box_pack_start (GTK_BOX (container), frame, FALSE, FALSE, 0);
	modebox = vertical ? gtk_vbox_new(FALSE, 2) : gtk_hbox_new(FALSE, 2);
	gtk_container_add (GTK_CONTAINER (frame), modebox);
	gtk_container_set_border_width (GTK_CONTAINER (modebox), 2);
	gtk_widget_show(modebox);

	char buf[256];
	strncpy(buf, txt, 256);
	buf[255] = '\0';
	char *a = strtok(buf, ",");

	button = gtk_radio_button_new_with_label (nullptr, (const char *)a );
	gtk_box_pack_start (GTK_BOX (modebox), button, TRUE, TRUE, 0);
	gtk_widget_show (button);
	g_signal_connect (GTK_OBJECT (button), "clicked",
	                  G_CALLBACK(cb), GINT_TO_POINTER(0));

	for (int i=1; i<ntxt; i++) {
		GSList *group = gtk_radio_button_get_group (GTK_RADIO_BUTTON (button));
		a = strtok(nullptr, ",");
		button = gtk_radio_button_new_with_label (group, (const char *)a );
		gtk_toggle_button_set_active(
		    GTK_TOGGLE_BUTTON(button),
		    i == radio_state);
		gtk_box_pack_start (GTK_BOX (modebox), button, TRUE, TRUE, 0);
		gtk_widget_show (button);
		g_signal_connect (GTK_OBJECT (button), "clicked",
		                  G_CALLBACK (cb), GINT_TO_POINTER(i));
	}
	return modebox;
}
static void mk_checkbox(const char *label, GtkWidget *container,
                        gboolean *checkstate, GtkCallback cb)
{
	GtkWidget *button = gtk_check_button_new_with_label(label);
	gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(button), *checkstate);
	g_signal_connect (button, "toggled", G_CALLBACK (cb), checkstate);
	gtk_box_pack_start (GTK_BOX(container), button, TRUE, TRUE, 0);
	gtk_widget_show(button);
}
static GtkWidget *mk_checkbox2(const char *label, GtkWidget *container,
                               bool initial_state, GtkCallback cb, gpointer data)
{
	GtkWidget *button = gtk_check_button_new_with_label(label);
	gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(button), initial_state);
	g_signal_connect (button, "toggled", G_CALLBACK (cb), data);
	gtk_box_pack_start (GTK_BOX(container), button, TRUE, TRUE, 0);
	gtk_widget_show(button);
	return button;
}
static GtkWidget *mk_button(const char *label, GtkWidget *container,
                            GtkCallback cb, gpointer data)
{
	GtkWidget *button = gtk_button_new_with_label(label);
	g_signal_connect(button, "clicked", G_CALLBACK(cb), data);
	gtk_box_pack_start(GTK_BOX(container), button, FALSE, FALSE, 1);
	return button;
}
// this guy is just like mk_radio but is a menu
static GtkWidget *mk_combobox(const char *txt, int ntxt, GtkWidget *container,
                              bool vertical, const char *labelText,
                              int box_state, GtkCallback cb)
{
	GtkWidget *bx, *frame, *combo;

	frame = gtk_frame_new(labelText);
	gtk_box_pack_start(GTK_BOX(container), frame, FALSE, FALSE, 0);
	bx = vertical ? gtk_vbox_new(FALSE, 2) : gtk_hbox_new(FALSE, 2);
	gtk_container_add(GTK_CONTAINER(frame), bx);
	gtk_container_set_border_width (GTK_CONTAINER(bx), 2);
	gtk_widget_show(bx);

	combo = gtk_combo_box_text_new();

	char buf[256];
	strncpy(buf, txt, 256);
	buf[255] = '\0';
	char *a = strtok(buf, ",");
	gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(combo), (const char *)a);

	for (int i=1; i<ntxt; i++) {
		a = strtok(nullptr, ",");
		gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(combo), (const char *)a);
	}

	g_signal_connect (GTK_OBJECT (combo), "changed", G_CALLBACK (cb), nullptr);

	gtk_combo_box_set_active(GTK_COMBO_BOX(combo), box_state);
	gtk_box_pack_start (GTK_BOX(bx), combo, TRUE, TRUE, 0);
	gtk_widget_show(combo);

	return combo;
}
void renderControlBlock(GtkWidget *container, int i)
{
	GtkWidget *frame, *bx;
	char buf[64];
	snprintf(buf, 64, "%c", 'A'+i);
	frame = gtk_frame_new(buf);
	gtk_box_pack_start(GTK_BOX(container), frame, FALSE, FALSE, 0);

	bx = gtk_vbox_new(TRUE, 1);
	gtk_container_add(GTK_CONTAINER(frame), bx);

	//channel spinner.
	g_channelSpin[i] = mk_spinner("ch   ", bx,
	                              g_channel[i]+1, 1, g_c.size(), 1,
	                              channelSpinCB, GINT_TO_POINTER(i));

	//right of that, a gain spinner. (need to update depending on ch)
	g_gainSpin[i] = mk_spinner("gain", bx,
	                           g_c[g_channel[i]]->getGain(),
	                           1, 128, 1,
	                           gainSpinCB, GINT_TO_POINTER(i));

	g_enabledChkBx[i] = mk_checkbox2("enabled", bx, g_c[g_channel[i]]->getEnabled(),
	[](GtkWidget *_button, gpointer _p) {
		int x = GPOINTER_TO_INT(_p);
		bool b = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(_button));
		g_c[g_channel[x]]->setEnabled(b);
	}, GINT_TO_POINTER(i));
}
static void setWidgetColor(GtkWidget *widget, unsigned char red, unsigned char green, unsigned char blue)
{
	GdkColor color;
	color.red = red*256;
	color.green = green*256;
	color.blue = blue*256;
	gtk_widget_modify_bg(widget, GTK_STATE_NORMAL, &color);
}
void renderSortingBlock(GtkWidget *container, int i)
{
	GtkWidget *frame, *bx;
	char buf[128];
	snprintf(buf, 128, "%c aperture", 'A'+i);
	frame = gtk_frame_new (buf);
	gtk_box_pack_start(GTK_BOX(container), frame, FALSE, FALSE, 1);

	bx = gtk_vbox_new(TRUE, 1);
	gtk_container_add (GTK_CONTAINER(frame), bx);

	for (int j=0; j<NSORT; j++) {
		GtkWidget *bx2 = gtk_hbox_new(FALSE, 1);
		gtk_container_add(GTK_CONTAINER(bx), bx2);

		u32 bits = 0;
		bits = i << 16; 	// left shift channel
		bits = bits | j; 	// OR in unit

		g_apertureSpin[i*NSORT+j] = mk_spinner("", bx2,
		                                       g_c[g_channel[i]]->getApertureUv(j), 0, 100, 0.1,
		[](GtkWidget *_spin, gpointer _p) {
			// unpack bits
			u32 u = GPOINTER_TO_UINT(_p);
			int ch = (u >> 16) & 0xFFFF;
			int un = u & 0xFFFF;
			if (ch < 4 && un < NSORT && !g_uiRecursion) {
				float a = gtk_spin_button_get_value(GTK_SPIN_BUTTON(_spin));

				// only update when the value has actually changed.
				if (g_c[g_channel[ch]]->getApertureUv(un) != a) {
					if (a >= 0 && a < 2000) {
						g_c[g_channel[ch]]->setApertureUv(un, a);
					}
				}
			}
		}, GUINT_TO_POINTER(bits));

		//a button for disable.
		GtkWidget *button = mk_button("off", bx2,
		[](GtkWidget *, gpointer _p) {
			// unpack bits
			u32 u = GPOINTER_TO_UINT(_p);
			int ch = (u >> 16) & 0xFFFF;
			int un = u & 0xFFFF;
			if (ch < 4 && un < NSORT && !g_uiRecursion) {
				gtk_spin_button_set_value(GTK_SPIN_BUTTON(g_apertureSpin[ch*NSORT+un]), 0);
				g_c[g_channel[ch]]->setApertureLocal(un, 0);
			}
		}, GUINT_TO_POINTER(bits));

		switch (j) {
		case 0:
			setWidgetColor(button, 166, 206, 227);	// blue
			break;
		case 1:
			setWidgetColor(button, 251, 154, 153);	// red
			break;
		case 2:
			setWidgetColor(button, 178, 223, 138);	// green
			break;
		case 3:
			setWidgetColor(button, 253, 191, 111);	// orange
			break;
		}

	}
}

auto get_cwd = []()
{
	char buf[512];
	return ( getcwd(buf, sizeof(buf)) ? string(buf) : string("") );
};
auto mk_legal_filename = [](string basedir, string prefix, string ext)
{
	string f;
	int count = 0;
	int res = -1;
	do {
		stringstream s;
		count++;
		s.str("");
		s << prefix << setfill('0') << setw(2) << count << ext;
		f = s.str();
		s.str("");
		s << basedir << "/" << f;
		string fn = s.str();
		res = ::access(fn.c_str(), F_OK);
	} while (!res);	// returns zero on success
	return f;
};
static void openSaveSpikesFile(GtkWidget *, gpointer parent_window)
{
	string d = get_cwd();
	string f = mk_legal_filename(d, "spk_", ".h5");

	GtkWidget *dialog;
	dialog = gtk_file_chooser_dialog_new ("Save Spikes File",
	                                      (GtkWindow *)parent_window,
	                                      GTK_FILE_CHOOSER_ACTION_SAVE,
	                                      GTK_STOCK_CANCEL, GTK_RESPONSE_CANCEL,
	                                      GTK_STOCK_SAVE, GTK_RESPONSE_ACCEPT,
	                                      NULL);
	gtk_file_chooser_set_do_overwrite_confirmation(
	    GTK_FILE_CHOOSER (dialog), TRUE);
	gtk_file_chooser_set_current_folder (GTK_FILE_CHOOSER (dialog),d.c_str());
	gtk_file_chooser_set_current_name (GTK_FILE_CHOOSER (dialog),f.c_str());
	if (gtk_dialog_run (GTK_DIALOG (dialog)) == GTK_RESPONSE_ACCEPT) {
		char *filename;
		filename = gtk_file_chooser_get_filename (GTK_FILE_CHOOSER (dialog));
		g_spikewriter.open(filename, g_c.size(), NSORT, NWFSAMP);
		g_free (filename);

		int max_str = 0;
		for (size_t i=0; i<g_c.size(); i++) {
			max_str = max_str > (int)g_c[i]->m_chanName.size() ?
			          max_str : g_c[i]->m_chanName.size();
		}
		auto name = new char[max_str*g_c.size()];
		for (size_t i=0; i<g_c.size(); i++) {
			strncpy(&name[i*max_str], g_c[i]->m_chanName.c_str(),
			        g_c[i]->m_chanName.size());
		}

		g_spikewriter.setVersion();

		char uuid[37];
		uuid_unparse(g_uuid, uuid);
		g_spikewriter.setUUID(uuid);

		time_t t_create;
		time(&t_create);
		char create_date[sizeof("YYYY-MM-DDTHH:MM:SSZ")];
		strftime(create_date, sizeof(create_date), "%FT%TZ", gmtime(&t_create));

		g_spikewriter.setFileCreateDate(create_date);
		g_spikewriter.setSessionStartTime(g_sess_start_t);
		g_spikewriter.setSessionDescription("spike data");

		g_spikewriter.setMetaData(g_sr, name, max_str);
		delete[] name;
		g_spikewriter.addDeviceDescription("RZ2", "TDT RZ2 BioAmp Processor");
		g_spikewriter.addDeviceDescription("PZ2", "TDT PZ2 Preamplifier");

	}
	gtk_widget_destroy (dialog);
}
// make this an anonymous function since it's only called once
static void getTemplateCB(GtkWidget *, gpointer _p)
{
	// unpack bits
	u32 u = GPOINTER_TO_UINT(_p);
	int i = (u >> 16) & 0xFFFF;
	int ch = g_channel[i];
	int un = u & 0xFFFF;

	debug("ch: %d\t un: %d", ch, un);

	g_c[ch]->updateTemplate(un);
	//update the UI.
	gtk_spin_button_set_value(GTK_SPIN_BUTTON(g_apertureSpin[i*NSORT+un]),
	                          g_c[ch]->getApertureUv(un));
	//remove the old poly, now that we've used it.
	g_c[ch]->resetPoly();
}

int main(int argc, char **argv)
{

	(void) signal(SIGINT, destroy);

	lockfile lf("/tmp/spk.lock");
	if (lf.lock()) {
		error("executable already running");
		return 1;
	}

	uuid_generate(g_uuid);

	g_tsc = new TimeSyncClient(); //tells us the ticks when things happen.

	string verstr = "v2.70";

	string titlestr = "spk ";
	titlestr += verstr;

#ifdef DEBUG
	feenableexcept(FE_DIVBYZERO|FE_INVALID|FE_OVERFLOW);  // Enable (some) floating point exceptions
	titlestr += " *** DEBUG ***";
#endif

	GtkWidget *window;
	GtkWidget *da1;
	GdkGLConfig *glconfig;
	GtkWidget *box1, *box2, *box3;
	GtkWidget *bx, *bx2, *v1, *label;
	GtkWidget *paned;
	GtkWidget *frame;

	string s;

	// load matlab preferences
	if (argc > 1)
		strncpy(g_prefstr, argv[1], 256);
	else
		strcpy(g_prefstr, "preferences.mat");
	printf("using %s for settings\n", g_prefstr);

	MatStor ms(g_prefstr);
	ms.load();

	xdgHandle xdg;
	xdgInitHandle(&xdg);
	char *confpath = xdgConfigFind("spk/spk.rc", &xdg);
	char *tmp = confpath;
	// confpath is "string1\0string2\0string3\0\0"

	luaConf conf;

	while (*tmp) {
		conf.loadConf(tmp);
		tmp += strlen(tmp) + 1;
	}
	if (confpath)
		free(confpath);
	xdgWipeHandle(&xdg);

	conf.getString("spk.boxcar.fifo_in", g_boxcar_fifo_in);
	conf.getString("spk.boxcar.fifo_out", g_boxcar_fifo_out);
	conf.getString("spk.boxcar.mmap", g_boxcar_mmap);
	conf.getDouble("spk.boxcar.bandwidth", g_boxcar_bandwidth);
	g_boxcar_use_unsorted = conf.getBool("spk.boxcar.use_unsorted");
	g_boxcar_enabled = conf.getBool("spk.boxcar.enabled");

	conf.getString("spk.gks.fifo_in", g_gks_fifo_in);
	conf.getString("spk.gks.fifo_out", g_gks_fifo_out);
	conf.getString("spk.gks.mmap", g_gks_mmap);
	conf.getDouble("spk.gks.bandwidth", g_gks_bandwidth);
	g_gks_use_unsorted = conf.getBool("spk.gks.use_unsorted");
	g_gks_enabled = conf.getBool("spk.gks.enabled");

	std::string zq = "ipc:///tmp/query.zmq";
	conf.getString("spk.query_socket", zq);
	printf("zmq query socket: %s\n", zq.c_str());

	std::string zt = "ipc:///tmp/time.zmq";
	conf.getString("spk.time_socket", zt);
	printf("zmq time socket: %s\n", zt.c_str());

	std::string zb = "ipc:///tmp/broadband.zmq";
	conf.getString("spk.broadband_socket", zb);
	printf("zmq broadband socket: %s\n", zb.c_str());

	std::string ze = "ipc:///tmp/events.zmq";
	conf.getString("spk.events_socket", ze);
	printf("zmq events socket: %s\n", ze.c_str());

	std::string zs = "ipc:///tmp/spk.zmq";
	conf.getString("spk.spike_socket", zs);
	printf("zmq spike socket: %s\n", zs.c_str());

	void *zcontext = zmq_ctx_new();
	if (zcontext == NULL) {
		error("zmq: could not create context");
		return 1;
	}

	// we don't need 1024 sockets
	if (zmq_ctx_set(zcontext, ZMQ_MAX_SOCKETS, 64) != 0) {
		error("zmq: could not set max sockets");
		die(zcontext, 1);
	}

	// controller socket
	void *controller = zmq_socket(zcontext, ZMQ_PUB);
	if (controller == NULL) {
		error("zmq: could not create socket");
		die(zcontext, 1);
	}
	g_socks.push_back(controller);

	if (zmq_bind(controller, "inproc://controller") != 0) {
		error("zmq: could not bind to socket");
		die(zcontext, 1);
	}

	// query socket
	void *query_sock = zmq_socket(zcontext, ZMQ_REQ);
	if (query_sock == NULL) {
		error("zmq: could not create socket");
		die(zcontext, 1);
	}
	g_socks.push_back(query_sock);

	int linger = 100;
	zmq_setsockopt(query_sock, ZMQ_LINGER, &linger, sizeof(linger));

	if (zmq_connect(query_sock, zq.c_str()) != 0) {
		error("zmq: could not connect to socket");
		die(zcontext, 1);
	}

	// XXX  TIME SOCK
	void *time_sock = zmq_socket(zcontext, ZMQ_REQ);
	if (time_sock == NULL) {
		error("zmq: could not create socket");
		die(zcontext, 1);
	}
	g_socks.push_back(time_sock);
	zmq_setsockopt(time_sock, ZMQ_LINGER, &linger, sizeof(linger));
	if (zmq_connect(time_sock, zt.c_str()) != 0) {
		error("zmq: could not connect to socket");
		die(zcontext, 1);
	}

	u64 nnc; // num neural channels
	zmq_send(query_sock, "NNC", 3, 0);
	if (zmq_recv(query_sock, &nnc, sizeof(u64), 0) == -1) {
		error("zmq: could not recv from query sock");
		die(zcontext, 1);
	}
	u64 nec; // num event channels
	zmq_send(query_sock, "NEC", 3, 0);
	if (zmq_recv(query_sock, &nec, sizeof(u64), 0) == -1) {
		error("zmq: could not recv from query sock");
		die(zcontext, 1);
	}
	u64 nac; // num analog channels
	zmq_send(query_sock, "NAC", 3, 0);
	if (zmq_recv(query_sock, &nac, sizeof(u64), 0) == -1) {
		error("zmq: could not recv from query sock");
		die(zcontext, 1);
	}
	u64 nic; // num ignored cahnnels
	zmq_send(query_sock, "NIC", 3, 0);
	if (zmq_recv(query_sock, &nic, sizeof(u64), 0) == -1) {
		error("zmq: could not recv from query sock");
		die(zcontext, 1);
	}

	zmq_send(query_sock, "START_TIME", 10, 0);
	if (zmq_recv(query_sock, g_sess_start_t, sizeof(g_sess_start_t), 0) == -1) {
		error("zmq: could not recv from query sock");
		die(zcontext, 1);
	}

	printf("session start:\t\t%s\n", 	g_sess_start_t);
	printf("neural channels:\t%zu\n", 	nnc);
	printf("events channels:\t%zu\n", 	nec);
	printf("analog channels:\t%zu\n", 	nac);
	printf("ignored channels:\t%zu\n", 	nic);

	if (nnc == 0) {
		error("No neural channels? Aborting!");
		die(zcontext, 1);
	}

	if (g_boxcar_enabled) {
		size_t numchans = nnc*NSORT;
		if (g_boxcar_use_unsorted) {
			numchans = nnc*NUNIT;
		}
		for (size_t i=0; i<numchans; i++) {
			// boxcar binner
			auto box = new FiringRate();
			box->set_bin_width(g_boxcar_bandwidth); // (seconds)
			g_boxcar.push_back(box);
		}
	}

	if (g_gks_enabled) {
		size_t numchans = nnc*NSORT;
		if (g_gks_use_unsorted) {
			numchans = nnc*NUNIT;
		}
		for (size_t i=0; i<numchans; i++) {
			// gaussian kernel smoother
			auto ks = new GaussianKernelSmoother();
			ks->set_bandwidth(g_gks_bandwidth); // (seconds)
			g_ks.push_back(ks);
		}
	}

	for (u64 ch=0; ch<nnc; ch++) {

		auto o = new Channel(ch, &ms);

		// NC : X : NAME
		zmq_send(query_sock, "NC", 2, ZMQ_SNDMORE);
		zmq_send(query_sock, &ch, sizeof(u64), ZMQ_SNDMORE);
		zmq_send(query_sock, "NAME", 4, 0);

		zmq_msg_t msg;
		zmq_msg_init(&msg);
		zmq_msg_recv(&msg, query_sock, 0);

		std::string str((char *)zmq_msg_data(&msg), zmq_msg_size(&msg));
		o->m_chanName = str;
		g_c.push_back(o);
		zmq_msg_close(&msg);
	}

	for (size_t i=0; i<g_channel.size(); i++) {
		g_channel[i] = ms.getInt(i, "channel", i*16);
		if (g_channel[i] < 0) g_channel[i] = 0;
		if (g_channel[i] >= (int)nnc) g_channel[i] = (int)nnc-1;
	}

	for (int i=0; i<NFBUF; i++) {
		g_timeseries.push_back(new VboTimeseries(NSAMP));
	}

	for (int i=0; i<NSORT; i++) {
		VboRaster *o = new VboRaster(nnc, NSBUF);
		switch (i) {
		case 0:
			o->setColor(0.122, 0.471, 0.706, 0.3);	// blue
			break;
		case 1:
			o->setColor(0.890, 0.102, 0.110, 0.3);	// red
			break;
		case 2:
			o->setColor(0.200, 0.628, 0.173, 0.3);	// green
			break;
		case 3:
			o->setColor(1.000, 0.489, 0.000, 0.3); // orange
			break;
		}
		g_spikeraster.push_back(o);
	}

	// nb we must not assume that the string is null terminated!
	auto is = [](zmq_msg_t *m, const char *c) {
		return strncmp((char *)zmq_msg_data(m), c, zmq_msg_size(m)) == 0;
	};

	for (u64 ch=0; ch<nec; ch++) {
		// EC : X : NAME
		zmq_send(query_sock, "EC", 2, ZMQ_SNDMORE);
		zmq_send(query_sock, &ch, sizeof(u64), ZMQ_SNDMORE);
		zmq_send(query_sock, "NAME", 4, 0);

		zmq_msg_t msg;
		zmq_msg_init(&msg);
		zmq_msg_recv(&msg, query_sock, 0);

		if (is(&msg, "stim")) {
			VboRaster *o = new VboRaster(16, 5*NSBUF);
			o->setColor(1.0, 1.0, 50.f/255.f, 0.75); // yellow
			g_eventraster.push_back(o);
			g_ec_stim = ch;
		} else {
			VboRaster *o = new VboRaster(1, 10*NSBUF);
			o->setColor(202.f/255.f, 178.f/255.f, 214.f/255.f, 0.95); // purple
			g_eventraster.push_back(o);
		}
		zmq_msg_close(&msg);
	}

	g_saveUnsorted 	= (bool)ms.getStructValue("savemode", "unsorted_spikes", 0, (float)g_saveUnsorted);
	g_saveSpikeWF  	= (bool)ms.getStructValue("savemode", "spike_waveforms", 0, (float)g_saveSpikeWF);

	g_drawmodep = (int) ms.getStructValue("gui", "draw_mode", 0, (float)g_drawmodep);
	g_blendmodep = (int) ms.getStructValue("gui", "blend_mode", 0, (float)g_blendmodep);

	g_showContGrid = (bool) ms.getStructValue("raster", "show_grid", 0, (float)g_showContGrid);
	g_showContThresh = (bool) ms.getStructValue("raster","show_threshold", 0, (float)g_showContThresh);
	g_rasterSpan = ms.getStructValue("raster", "span", 0, g_rasterSpan);

	g_whichSpikePreEmphasis = ms.getStructValue("spike", "pre_emphasis", 0, g_whichSpikePreEmphasis);
	g_whichAlignment = ms.getStructValue("spike", "alignment_mode", 0, g_whichAlignment);
	g_minISI = ms.getStructValue("spike", "min_isi", 0, g_minISI);
	g_autoThreshold = ms.getStructValue("spike", "auto_threshold", 0, g_autoThreshold);
	g_neoThreshold = ms.getStructValue("spike", "neo_threshold", 0, g_neoThreshold);
	g_spikesCols = (int)ms.getStructValue("spike", "cols", 0, (float)g_spikesCols);

	g_showUnsorted = (bool)ms.getStructValue("wf", "show_unsorted", 0, (float)g_showUnsorted);
	g_showTemplate = (bool)ms.getStructValue("wf", "show_template", 0, (float)g_showTemplate);
	g_showPca = (bool)ms.getStructValue("wf", "show_pca", 0, (float)g_showPca);
	g_showWFVgrid = (bool)ms.getStructValue("wf", "show_grid", 0, (float)g_showWFVgrid);
	g_showISIhist = (bool)ms.getStructValue("wf", "show_isi", 0, (float)g_showISIhist);
	g_showWFstd = (bool)ms.getStructValue("wf", "show_std", 0, (float)g_showWFstd);
	g_zoomSpan = ms.getStructValue("wf", "span", 0, g_zoomSpan);

	gtk_init (&argc, &argv);
	gtk_gl_init(&argc, &argv);

	window = gtk_window_new (GTK_WINDOW_TOPLEVEL);
	gtk_window_set_title (GTK_WINDOW (window), titlestr.c_str());
	gtk_window_set_default_size (GTK_WINDOW (window), 890, 650);
	da1 = gtk_drawing_area_new();
	gtk_widget_set_size_request(GTK_WIDGET(da1), 640, 650);

	paned = gtk_hpaned_new();
	gtk_container_add (GTK_CONTAINER (window), paned);

	v1 = gtk_vbox_new (FALSE, 0);
	gtk_widget_set_size_request(GTK_WIDGET(v1), 250, 650);

	bx = gtk_vbox_new (FALSE, 2);

	//add in a headstage channel # label
	g_infoLabel = gtk_label_new ("info: 0");
	gtk_misc_set_alignment (GTK_MISC (g_infoLabel), 0, 0);
	gtk_box_pack_start (GTK_BOX (bx), g_infoLabel, TRUE, TRUE, 0);
	gtk_widget_show(g_infoLabel);

	gtk_box_pack_start (GTK_BOX (v1), bx, FALSE, FALSE, 0);

	// render 4-channel control blocks.
	bx2 = gtk_hbox_new(TRUE, 1);
	gtk_box_pack_start(GTK_BOX(bx), bx2, FALSE, FALSE, 0);
	renderControlBlock(bx2, 0);
	renderControlBlock(bx2, 1);
	bx2 = gtk_hbox_new(TRUE, 1);
	gtk_box_pack_start(GTK_BOX(bx), bx2, FALSE, FALSE, 0);
	renderControlBlock(bx2, 2);
	renderControlBlock(bx2, 3);

	bx2 = gtk_hbox_new(FALSE, 2);
	gtk_box_pack_start(GTK_BOX(bx), bx2, FALSE, FALSE, 0);

	mk_checkbox("offset B,C,D", bx2, &g_autoChOffset, basic_checkbox_cb);

	//add a pause / go button (applicable to all)
	mk_checkbox("pause", bx2, &g_pause,
	[](GtkWidget *_button, gpointer) {
		g_pause = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(_button));
		g_pause_time = g_pause ? gettime() : -1.0;
	}
	           );

	//notebook region!
	g_notebook = gtk_notebook_new();
	gtk_notebook_set_tab_pos (GTK_NOTEBOOK (g_notebook), GTK_POS_LEFT);
	g_signal_connect(g_notebook, "switch-page",
	                 G_CALLBACK(notebookPageChangedCB), 0);
	//g_signal_connect(notebook, "select-page",
	//				 G_CALLBACK(notebookPageChangedCB), 0);
	gtk_box_pack_start(GTK_BOX(v1), g_notebook, TRUE, TRUE, 1);
	gtk_widget_show(g_notebook);



	// [ rasters | spikes | sort ]



	// RASTERS
	box1 = gtk_vbox_new(FALSE, 2);

	//add a gain set-all button.
	mk_button("Set all gains from A", box1,
	[](GtkWidget *, gpointer) {
		float g = gtk_spin_button_get_value(GTK_SPIN_BUTTON(g_gainSpin[0]));
		for (auto &c : g_c) {
			c->setGain(g);
			c->resetPca();
		}
		for (int i=1; i<4; i++) { // 0 is what we are reading from
			gtk_spin_button_set_value(GTK_SPIN_BUTTON(g_gainSpin[i]), g);
		}
	}, nullptr);

	frame = gtk_frame_new("Show?");
	gtk_box_pack_start(GTK_BOX(box1), frame, TRUE, TRUE, 0);

	box2 = gtk_hbox_new(TRUE, 0);
	gtk_widget_show(box2);
	gtk_container_add (GTK_CONTAINER (frame), box2);

	mk_checkbox("grid", box2, &g_showContGrid, basic_checkbox_cb);
	mk_checkbox("threshold", box2, &g_showContThresh, basic_checkbox_cb);

	frame = gtk_frame_new("Span");
	gtk_box_pack_start(GTK_BOX(box1), frame, TRUE, TRUE, 0);

	box2 = gtk_vbox_new(TRUE, 0);
	gtk_widget_show(box2);
	gtk_container_add (GTK_CONTAINER (frame), box2);

	mk_spinner("Raster", box2,
	           g_rasterSpan, 1.0, 100.0, 1.0,
	           basic_spinfloat_cb, (gpointer)&g_rasterSpan);

	mk_spinner("Waveform", box2, g_zoomSpan, 0.1, 2.7, 0.05,
	[](GtkWidget *_spin, gpointer) {
		// should be in seconds.
		float f = (float) gtk_spin_button_get_value(GTK_SPIN_BUTTON(_spin));
		g_zoomSpan = f;
		for (auto &x : g_timeseries)
			x->setNPlot(f * SRATE_HZ);
	}, nullptr);


	gtk_widget_show (box1);
	label = gtk_label_new("rasters");
	gtk_label_set_angle(GTK_LABEL(label), 90);
	gtk_notebook_insert_page(GTK_NOTEBOOK(g_notebook), box1, label, 0);
	// this concludes the rasters page.


	// SORT
	box1 = gtk_vbox_new (FALSE, 0);

	//4-channel control blocks.
	box2 = gtk_hbox_new(FALSE, 0);
	gtk_box_pack_start(GTK_BOX(box1), box2, TRUE, TRUE, 0);
	renderSortingBlock(box2, 0);
	renderSortingBlock(box2, 1);
	box2 = gtk_hbox_new (FALSE, 0);
	gtk_box_pack_start(GTK_BOX(box1), box2, TRUE, TRUE, 0);
	renderSortingBlock(box2, 2);
	renderSortingBlock(box2, 3);

	frame = gtk_frame_new ("Show?");
	gtk_box_pack_start (GTK_BOX(box1), frame, TRUE, TRUE, 0);

	box2 = gtk_hbox_new (FALSE, 0);
	gtk_widget_show(box2);
	gtk_container_add (GTK_CONTAINER (frame), box2);

	box3 = gtk_vbox_new (FALSE, 0);
	gtk_widget_show(box3);
	mk_checkbox("Unsorted", box3, &g_showUnsorted, basic_checkbox_cb);
	mk_checkbox("Template", box3, &g_showTemplate, basic_checkbox_cb);
	mk_checkbox("PCA", box3, &g_showPca, basic_checkbox_cb);
	gtk_box_pack_start (GTK_BOX (box2), box3, TRUE, TRUE, 0);

	box3 = gtk_vbox_new (FALSE, 0);
	gtk_widget_show(box3);
	mk_checkbox("grid", box3, &g_showWFVgrid, basic_checkbox_cb);
	mk_checkbox("ISI", box3, &g_showISIhist, basic_checkbox_cb);
	mk_checkbox("STD", box3, &g_showWFstd, basic_checkbox_cb);
	gtk_box_pack_start (GTK_BOX (box2), box3, TRUE, TRUE, 0);

	mk_button("calc PCA", box1,
	[](GtkWidget *, gpointer) {
		for (auto &x : g_channel) {
			g_c[x]->computePca();
		}
	}, nullptr);

	//this concludes sort page.
	gtk_widget_show (box1);
	label = gtk_label_new("sort");
	gtk_label_set_angle(GTK_LABEL(label), 90);
	gtk_notebook_insert_page(GTK_NOTEBOOK(g_notebook), box1, label, 1);
	// nb insert sort out of order


	// SPIKES
	box1 = gtk_vbox_new(FALSE, 0);

	mk_combobox("none,abs,NEO", 3, box1, false, "Spike Pre-emphasis",
	            g_whichSpikePreEmphasis,
	[](GtkWidget *_w, gpointer) {
		g_whichSpikePreEmphasis =
		    gtk_combo_box_get_active(GTK_COMBO_BOX(_w));
	}
	           );

	mk_combobox("crossing,min,max,abs,slope,neo", 6, box1, false, "Spike Alignment",
	            g_whichAlignment,
	[](GtkWidget *_w, gpointer) {
		g_whichAlignment = gtk_combo_box_get_active(GTK_COMBO_BOX(_w));
	}
	           );

	// for the sorting, we show all waveforms (up to a point..)
	//these should have a minimum enforced ISI.
	mk_spinner("min ISI, ms", box1, g_minISI,
	           0.2, 3.0, 0.01, basic_spinfloat_cb, (gpointer)&g_minISI);

	frame = gtk_frame_new ("Threshold");
	gtk_box_pack_start (GTK_BOX(box1), frame, FALSE, FALSE, 0);

	box2 = gtk_hbox_new (FALSE, 1);
	gtk_widget_show(box2);
	gtk_container_add(GTK_CONTAINER(frame), box2);

	box3 = gtk_vbox_new (TRUE, 1);
	gtk_widget_show(box3);
	gtk_box_pack_start(GTK_BOX(box2), box3, TRUE, TRUE, 0);

	mk_spinner("STD", box3, g_autoThreshold,
	           -10.0, 10.0, 0.05, basic_spinfloat_cb, (gpointer)&g_autoThreshold);

	mk_spinner("NEO", box3, g_neoThreshold,
	           0, 100, 1, basic_spinfloat_cb, (gpointer)&g_neoThreshold);

	box3 = gtk_vbox_new (TRUE, 1);
	gtk_widget_show(box3);
	gtk_box_pack_start(GTK_BOX(box2), box3, TRUE, TRUE, 0);

	mk_button("set selected", box3,
	[](GtkWidget *, gpointer) {
		g_c[g_channel[0]]->autoThreshold(g_autoThreshold);
	}, nullptr);
	mk_button("set all", box3,
	[](GtkWidget *, gpointer) {
		for (auto &c : g_c) {
			c->autoThreshold(g_autoThreshold);
		}
	}, nullptr);

	mk_spinner("Columns", box1, g_spikesCols, 3, 32, 1,
	           basic_spinint_cb, (gpointer)&g_spikesCols);

	//add draw mode (applicable to all)
	bx = gtk_hbox_new(TRUE, 0);
	gtk_widget_show(bx);
	gtk_box_pack_start(GTK_BOX(box1), bx, TRUE, TRUE, 0);

	mk_radio("points,lines", 2, bx, true, "draw mode", g_drawmodep,
	[](GtkWidget *_button, gpointer _p) {
		if (gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(_button))) {
			g_drawmodep = (int)((i64)_p & 0xf);
		}
	}
	        );

	mk_radio("normal,accum", 2, bx, true, "blend mode", g_blendmodep,
	[](GtkWidget *_button, gpointer _p) {
		if (gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(_button))) {
			g_blendmodep = (int)((i64)_p & 0xf);
		}
	}
	        );

	// end spikes page.
	gtk_widget_show (box1);
	label = gtk_label_new("spikes");
	gtk_label_set_angle(GTK_LABEL(label), 90);
	gtk_notebook_insert_page(GTK_NOTEBOOK(g_notebook), box1, label, 1);



	// bottom section, visible on all screens
	bx = gtk_vbox_new (FALSE, 3);

	gtk_box_pack_start (GTK_BOX (v1), bx, TRUE, TRUE, 0);

	GtkWidget *bxx1, *bxx2;

	s = "Save Spikes";
	frame = gtk_frame_new (s.c_str());
	gtk_box_pack_start (GTK_BOX (bx), frame, FALSE, FALSE, 1);
	bxx1 = gtk_hbox_new (TRUE, 0);
	gtk_container_add (GTK_CONTAINER (frame), bxx1);

	bxx2 = gtk_vbox_new(TRUE, 0);
	gtk_container_add(GTK_CONTAINER (bxx1), bxx2);
	mk_button("Start", bxx2, openSaveSpikesFile, nullptr);
	mk_checkbox("WF?", bxx2, &g_saveSpikeWF, basic_checkbox_cb);

	bxx2 = gtk_vbox_new(FALSE, 0);
	gtk_container_add(GTK_CONTAINER (bxx1), bxx2);
	mk_button("Stop", bxx2,
	[](GtkWidget *, gpointer) {
		g_spikewriter.close();
	}, nullptr);

	mk_checkbox("Unsorted?", bxx2, &g_saveUnsorted,	basic_checkbox_cb);

	mk_button("Save Preferences", bx,
	[](GtkWidget *, gpointer) {
		saveState();
	}, nullptr);

	bx = gtk_hbox_new (FALSE, 3);
	label = gtk_label_new ("");
	gtk_misc_set_alignment (GTK_MISC (label), 0, 0);
	gtk_box_pack_start (GTK_BOX (bx), label, FALSE, FALSE, 0);
	gtk_widget_show(label);
	gtk_box_pack_start (GTK_BOX (v1), bx, TRUE, TRUE, 0);
	g_spikewriter.registerWidget(label);

	gtk_paned_add1(GTK_PANED(paned), v1);
	gtk_paned_add2(GTK_PANED(paned), da1);

	gtk_widget_show (paned);

	g_signal_connect_swapped (window, "destroy",
	                          G_CALLBACK (destroyGUI), NULL);
	gtk_widget_set_events (da1, GDK_EXPOSURE_MASK);

	gtk_widget_show (window);

	/* prepare GL */
	glconfig = gdk_gl_config_new_by_mode (GdkGLConfigMode(
	        GDK_GL_MODE_RGB |
	        GDK_GL_MODE_DEPTH |
	        GDK_GL_MODE_DOUBLE));

	if (!glconfig)
		g_assert_not_reached ();

	if (!gtk_widget_set_gl_capability (da1, glconfig, nullptr, TRUE,
	                                   GDK_GL_RGBA_TYPE)) {
		g_assert_not_reached ();
	}

	g_signal_connect (da1, "configure-event",
	                  G_CALLBACK (configure1), NULL);
	g_signal_connect (da1, "expose-event",
	                  G_CALLBACK (expose1), NULL);
	g_signal_connect (G_OBJECT (da1), "motion_notify_event",
	                  G_CALLBACK (motion_notify_event), NULL);
	g_signal_connect (G_OBJECT (da1), "button_press_event",
	                  G_CALLBACK (button_press_event), NULL);

	gtk_widget_set_events (da1, GDK_EXPOSURE_MASK
	                       | GDK_LEAVE_NOTIFY_MASK
	                       | GDK_BUTTON_PRESS_MASK
	                       | GDK_POINTER_MOTION_MASK
	                       | GDK_POINTER_MOTION_HINT_MASK);

	//in order to receive keypresses, must be focusable!
	// http://forums.fedoraforum.org/archive/index.php/t-242963.html
	gtk_widget_set_can_focus(da1, true);

	string asciiart = "\033[1m";
	asciiart += "           _      \n";
	asciiart += " ___ _ __ | | _   \n";
	asciiart += "/ __| '_ \\| |/ / \n";
	asciiart += "\\__ \\ |_) |   < \n";
	asciiart += "|___/ .__/|_|\\_\\\n";
	asciiart += "    |_| ";
	asciiart += verstr;
	asciiart += "\033[0m";
	printf("%s\n\n",asciiart.c_str());

	printf("sampling rate: %f kHz\n", SRATE_KHZ);

	g_startTime = gettime();

	vector <thread> threads;

	threads.push_back(thread(worker, zcontext, zb.c_str(), ze.c_str(), zs.c_str()));
	threads.push_back(thread(spikewrite));
	if (g_boxcar_enabled) {
		threads.push_back(thread(boxcar_binner));
	}
	if (g_gks_enabled) {
		threads.push_back(thread(gks_binner));
	}

	gtk_widget_show_all(window);

	g_timeout_add(1000 / 30, rotate, da1);

	//jack.
#ifdef JACK
	warn("starting jack");
	jackInit("spk", JACKPROCESS_RESAMPLE);
	jackConnectFront();
	jackSetResample(SRATE_HZ/SAMPFREQ);
#endif

	gtk_main(); // gtk itself uses three threads, it seems

#ifdef JACK
	jackClose(0);
#endif

	KillFont();

	zmq_send(controller, "KILL", 4, 0);

	for (auto &thread : threads) {
		thread.join();
	}

	// these should automatically be closed when their destructor is called
	// however it should be safe to manually close after their thread is
	// joined and finished
	g_spikewriter.close();

	// clean out our standard vectors
	for (auto &o : g_c)
		delete o;
	for (auto &o : g_spikeraster)
		delete o;
	for (auto &o : g_timeseries)
		delete o;
	for (auto &o : g_boxcar)
		delete o;
	for (auto &o : g_ks)
		delete o;

	delete g_tsc;

	if (g_vsFadeColor)
		delete g_vsFadeColor;
	if (g_vsThreshold)
		delete g_vsThreshold;
	cgDestroyContext(myCgContext);

	lf.unlock();
}
