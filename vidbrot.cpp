//
// vidbrot - GL mandelbrot demo showing remapping of the complex plane with video
//
// This work is released under the Artistic License/GPL, Feb 2010
//

#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>
#include <limits.h>
#include <errno.h>
#include <string.h>
#include <math.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/time.h>
#include <sys/stat.h>
#include <sys/mman.h>
#include <sys/ioctl.h>
#include <linux/videodev2.h>
#define GL_GLEXT_PROTOTYPES
#include <GL/openglut.h>

//
// typedefs and defines
//

template <typename T> void clear( T &x ) { memset( &x, 0, sizeof(x) ); }
template <typename T> void clear( T &x, int count ) { memset( &x, 0, sizeof(x) * count ); }

#define FAIL(x) do { printf x; printf( "\n" ); exit( 1 ); } while (0)
#define DBUG(x) do { printf x; printf( "\n" ); } while (0)

#define CHECK_GLERROR() \
do { \
    GLenum err = glGetError(); \
    if (err != GL_NO_ERROR) \
    	printf( "%s(%d): GL Error %s\n", __FILE__, __LINE__, gluErrorString( err ) ); \
} while (0)

#define MK_SPECIALKEY(x) (0x100|x)
#define SPECIALKEY(x) MK_SPECIALKEY(GLUT_KEY_##x)

// list of menu label, value, case statement and command to invoke
#define LIST_COMMANDS(_) \
_("Animate Iteration Counts  [i]",'i',case 'i':,(animate_iters ^= true)) \
_("Iterate 1  [1]",'1',case '1':,((animate_iters = false), (iterations = iter_max = 1))) \
_("Iterate 2  [2]",'2',case '2':,((animate_iters = false), (iterations = iter_max = 2))) \
_("Iterate 3  [3]",'3',case '3':,((animate_iters = false), (iterations = iter_max = 3))) \
_("Iterate 4  [4]",'4',case '4':,((animate_iters = false), (iterations = iter_max = 4))) \
_("Iterate 5  [5]",'5',case '5':,((animate_iters = false), (iterations = iter_max = 5))) \
_("Iterate 6  [6]",'6',case '6':,((animate_iters = false), (iterations = iter_max = 6))) \
_("Iterate 7  [7]",'7',case '7':,((animate_iters = false), (iterations = iter_max = 7))) \
_("Iterate 8  [8]",'8',case '8':,((animate_iters = false), (iterations = iter_max = 8))) \
_("Iterate 16 [9]",'9',case '9':,((animate_iters = false), (iterations = iter_max = 16))) \
_("Iterate 100  [0]",'0',case '0':,((animate_iters = false), (iterations = iter_max = 100))) \
_("Animate Translation  [Space]",' ',case ' ':,(animate_translation ^= true)) \
_("Translation 1  [m]",'m',case 'm':,((animate_translation = false), (trans_scale = M_PI / 3.0))) \
_("Translation 0  [n]",'n',case 'n':,((animate_translation = false), (trans_scale = 0.0))) \
_("Animate Translation Phase  [t]",'t',case 't':,(animate_translation_phase ^= true)) \
_("Reset Translation Phase  [T]",'T',case 'T':,(trans_phase = M_PI/4)) \
_("Toggle mirror  [b]",'b',case 'b':,(mirror ^= true)) \
_("Toggle poles  [p]",'p',case 'p':,(showpoles ^= true)) \
_("Reset Zoom  [r]",'r',case 'r':,((cx = 0), (cy = -0.5), (zoom = 1.5))) \
_("Exit  [Esc]",27,case 27:,exit(0))

// tweakable constants
static const bool verbose = false;
static const bool use_mipmaps = true;
static const bool use_aniso = true;
static const char *WINDOW_TITLE = "VidBrot";

//
// vid_capture - manage video device capture
//

class vid_capture
{
private:
    int			fd;
    int			n_buffers;
    struct buffer
    {
	void			*start;
	size_t			length;
	struct v4l2_buffer	info;
    };
    buffer		*buffers;
    char		*dev_name;
    struct v4l2_format	fmt;

    // xioctl - perform an ioctl, retrying for EINTRs
    int xioctl( int request, void *arg )
    {
	int r;
	do r = ioctl( fd, request, arg ); while ((-1 == r) && (EINTR == errno)) ;
	return r;
    }

    void errno_exit( const char *s )
    {
	FAIL(( "%s error %d (%s)", s, errno, strerror( errno ) ));
    }

public:
    vid_capture( int n_buffers = 4 ) : fd(-1), n_buffers(n_buffers), dev_name(NULL)
    {
	buffers = new buffer[n_buffers];
	clear( *buffers, n_buffers );
	clear( fmt );
	if (verbose) DBUG(( "Creating vid_capture with %d buffers", n_buffers ));
    }

    ~vid_capture()
    {
	if (buffers)
	{
	    unmap();
	    delete [] buffers;
	}
	if (fd >= 0) close( fd );
	fd = -1;
    }

    void open( const char *name )
    {
	struct stat st; 

	if (-1 == stat( name, &st )) FAIL(( "%s not found", name ));
	if (!S_ISCHR( st.st_mode )) FAIL(( "%s is not a device", name ));
	fd = ::open( name, O_RDWR | O_NONBLOCK, 0 );
	if (-1 == fd) FAIL(( "failed to open %s", name ));
	if (dev_name) delete [] dev_name;
	dev_name = new char[strlen( name ) + 1];
	strcpy( dev_name, name );
	if (verbose) DBUG(( "Opened \"%s\"", dev_name ));
    }

    void open( int dev_num )
    {
	char name[32];
        sprintf( name, "/dev/video%d", dev_num );
	open( name );
    }

    void init( int width = 640, int height = 480 )
    {
        struct v4l2_capability cap;
        if (-1 == xioctl( VIDIOC_QUERYCAP, &cap ))
	{
	    if (EINVAL == errno) FAIL(( "%s is not a linux video device", dev_name ));
	    errno_exit( "VIDIOC_QUERYCAP" );
        }
        if (!(cap.capabilities & V4L2_CAP_VIDEO_CAPTURE))
	    FAIL(( "%s is not a linux video capture device", dev_name ));
	if (!(cap.capabilities & V4L2_CAP_STREAMING))
	    FAIL(( "%s does not support streaming I/O", dev_name ));

        // select video input, video standard and tune here

        struct v4l2_cropcap cropcap;
        clear( cropcap );
        cropcap.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        if (0 == xioctl( VIDIOC_CROPCAP, &cropcap ))
	{
	    struct v4l2_crop crop;
	    crop.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
	    crop.c = cropcap.defrect; // reset to default
	    // ignore errors
	    xioctl( VIDIOC_S_CROP, &crop );
        }

        clear( fmt );
        fmt.type                = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        fmt.fmt.pix.width       = width;
        fmt.fmt.pix.height      = height;
        fmt.fmt.pix.pixelformat = V4L2_PIX_FMT_YUYV;
        fmt.fmt.pix.field       = V4L2_FIELD_INTERLACED;
        if (-1 == xioctl( VIDIOC_S_FMT, &fmt )) errno_exit( "VIDIOC_S_FMT" );
	if (V4L2_PIX_FMT_YUYV != fmt.fmt.pix.pixelformat) FAIL(( "YUYV unsupported" ));
	// VIDIOC_S_FMT may change width and height, must query!

        // buggy driver paranoia
        unsigned int min = fmt.fmt.pix.width * 2;
        if (fmt.fmt.pix.bytesperline < min) fmt.fmt.pix.bytesperline = min;
        min = fmt.fmt.pix.bytesperline * fmt.fmt.pix.height;
        if (fmt.fmt.pix.sizeimage < min) fmt.fmt.pix.sizeimage = min;

	// ready to map
	if (verbose) DBUG(( "Ready to map (%dx%d)", fmt.fmt.pix.width, fmt.fmt.pix.height ));
    }

    int width() { return( fmt.fmt.pix.width ); }
    int height() { return( fmt.fmt.pix.height ); }
    int bytesperline() { return( fmt.fmt.pix.bytesperline ); }
    int bytesperframe() { return( fmt.fmt.pix.sizeimage ); }

    void unmap()
    {
	for (int i = 0; i < n_buffers; ++i)
	    if (buffers[i].length > 0)
	    {
		munmap( buffers[i].start, buffers[i].length );
		buffers[i].start = NULL;
		buffers[i].length = 0;
	    }
    }

    bool map()
    {
        struct v4l2_requestbuffers req;

        clear( req );
	if (verbose) DBUG(( "Requesting %d buffers", n_buffers ));
        req.count               = n_buffers;
        req.type                = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        req.memory              = V4L2_MEMORY_MMAP;

        if (-1 == xioctl( VIDIOC_REQBUFS, &req ))
	{
	    if (EINVAL == errno) FAIL(( "%s does not support memory mapping", dev_name ));
	    errno_exit( "VIDIOC_REQBUFS" );
        }
        if (req.count < 2) FAIL(( "insufficient buffer memory on %s (%d buffers available)", dev_name, req.count ));

	unmap();

        for (int i = 0; i < req.count; ++i)
	{
	    clear( buffers[i].info );
	    buffers[i].info.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
	    buffers[i].info.memory = V4L2_MEMORY_MMAP;
	    buffers[i].info.index = i;

	    if (-1 == xioctl( VIDIOC_QUERYBUF, &buffers[i].info )) errno_exit( "VIDIOC_QUERYBUF" );

	    buffers[i].length = buffers[i].info.length;
	    buffers[i].start = mmap( NULL, buffers[i].length, PROT_READ | PROT_WRITE, MAP_SHARED, fd, buffers[i].info.m.offset );
	    if (MAP_FAILED == buffers[n_buffers].start)
		errno_exit( "mmap" );
        }
    }

    void start()
    {
	for (int i = 0; i < n_buffers; ++i)
	{
	    if (buffers[i].length > 0)
	    {
		clear( buffers[i].info );
		buffers[i].info.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
		buffers[i].info.memory = V4L2_MEMORY_MMAP;
		buffers[i].info.index = i;

		if (-1 == xioctl( VIDIOC_QBUF, &buffers[i].info )) errno_exit( "VIDIOC_QBUF" );
	    }
	}
	v4l2_buf_type type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
	if (-1 == xioctl( VIDIOC_STREAMON, &type )) errno_exit( "VIDIOC_STREAMON" );
    }
    
    void stop()
    {
	v4l2_buf_type type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
	if (-1 == xioctl( VIDIOC_STREAMOFF, &type )) errno_exit( "VIDIOC_STREAMOFF" );
	// do we need to dequeue buffers here, or are they automatically dequeued?
    }

    bool wait()
    {
	bool ready = false;
	while (!ready)
	{
	    fd_set fds;
	    FD_ZERO( &fds );
	    FD_SET( fd, &fds );

	    struct timeval tv;
	    tv.tv_sec = 2;
	    tv.tv_usec = 0;

	    int r = select( fd + 1, &fds, NULL, NULL, &tv );

	    if (-1 == r)
	    {
		if (EINTR == errno) continue;
		errno_exit( "select" );
	    }
	    // timeout
	    if (0 == r) break;

	    ready = true;
	}
	return( ready );
    }

    int get()
    {
        struct v4l2_buffer buf;

	clear( buf );
	buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
	buf.memory = V4L2_MEMORY_MMAP;

	if (-1 == xioctl( VIDIOC_DQBUF, &buf ))
	{
	    switch (errno)
	    {
	    case EAGAIN: return( -1 );
	    case EIO: // (could ignore EIO, see spec) fall through...
	    default: errno_exit( "VIDIOC_DQBUF" );
	    }
	}

	if (buf.index >= n_buffers) FAIL(( "Buffer %d out of range 0..%d", buf.index, n_buffers ));
	return( buf.index );
    }

    void *data( int i )
    {
	if (i < 0 || i >= n_buffers) return( NULL );
	return( buffers[i].start );
    }

    void release( int i )
    {
	if (i < 0 || i >= n_buffers) return;
	if (-1 == xioctl( VIDIOC_QBUF, &buffers[i].info )) errno_exit( "VIDIOC_QBUF" );
    }
};

//
// globals
//

static int scr_w = 640;
static int scr_h = 480;
static GLfloat vid_aspect, scr_aspect;

static bool mirror = false;
static bool showpoles = false;
static bool animate_translation = false;
static bool animate_translation_phase = false;
static bool animate_iters = false;
static bool dragging = false;
static int drag_pt[2];
static bool juliaing = false;
static int julia_pt[2];

// mandelbrot center
static GLfloat cx = 0;
static GLfloat cy = -0.5;
// julia seed point
static GLfloat jx = 0;
static GLfloat jy = 0;
// zoom to edges of rect
static GLfloat zoom = 1.5;
static float trans_scale = M_PI / 3.0;
static float trans_phase = M_PI / 4.0;
static int iter_max = 1.0;
static int iter_dir = 1.0;
static int iterations = iter_max;

static GLfloat max_aniso = 1;
static vid_capture *vidcap = NULL;

static GLuint yuv_tex = 0;			// YUYV source texture
static GLuint rgb_tex = 0;			// converted RGB texture
static GLuint feedback_tex = 0;			// feedback rendering buffer
static GLuint fb = 0;				// FBO for YUV->RGB convert
static GLuint feedback_fb = 0;			// FBO for feedback rendering path
static GLuint yuv_prog = 0;			// program for YUYV->RGB conversion
static GLuint yuv_buf = 0;			// PBO for video data copy to yuv_tex
static GLuint mand_prog = 0;			// program to show mandelbrot set mapping
static GLuint mandpole_prog = 0;		// program to show mandelbrot set poles
static GLuint julia_prog = 0;			// program to show julia set mapping
static GLuint juliapole_prog = 0;		// program to show julia set poles

//
// CheckFramebufferStatus - see if we setup the framebuffer correctly or not
//

void CheckFramebufferStatus()
{
    switch (glCheckFramebufferStatusEXT( GL_FRAMEBUFFER_EXT ))
    {
    case GL_FRAMEBUFFER_COMPLETE_EXT:
        break;
    case GL_FRAMEBUFFER_UNSUPPORTED_EXT:
        printf( "Unsupported framebuffer format\n" );
        break;
    case GL_FRAMEBUFFER_INCOMPLETE_ATTACHMENT_EXT:
        printf( "Framebuffer incomplete, incomplete attachment\n" );
    	break;
    case GL_FRAMEBUFFER_INCOMPLETE_MISSING_ATTACHMENT_EXT:
        printf( "Framebuffer incomplete, missing attachment\n" );
        break;
    case GL_FRAMEBUFFER_INCOMPLETE_DUPLICATE_ATTACHMENT_EXT:
        printf( "Framebuffer incomplete, duplicate attachment\n" );
        break;
    case GL_FRAMEBUFFER_INCOMPLETE_DIMENSIONS_EXT:
        printf( "Framebuffer incomplete, attached images must have same dimensions\n" );
        break;
    case GL_FRAMEBUFFER_INCOMPLETE_FORMATS_EXT:
        printf( "Framebuffer incomplete, attached images must have same format\n" );
        break;
    case GL_FRAMEBUFFER_INCOMPLETE_DRAW_BUFFER_EXT:
        printf( "Framebuffer incomplete, missing draw buffer\n" );
        break;
    case GL_FRAMEBUFFER_INCOMPLETE_READ_BUFFER_EXT:
        printf( "Framebuffer incomplete, missing read buffer\n" );
        break;
    default:
    	FAIL(( "Unknown frambuffer status 0x%04x!\n", glCheckFramebufferStatusEXT( GL_FRAMEBUFFER_EXT ) ));
    }
}

//
// elapsed_ms - return milliseconds of elapsed time since last call
//

float elapsed_ms()
{
    static struct timeval otp;
    static bool bCalled = false;
    struct timeval tp;

    gettimeofday( &tp, NULL );

    float ms = bCalled ? (((tp.tv_sec - otp.tv_sec) * 1.0e3f) + ((tp.tv_usec - otp.tv_usec) * 1.0e-3f)) : 0.0f;
    bCalled = true;
    otp = tp;
    return( ms );
}

//
// make_frag_prog - create a fragment-program only shader
//

GLuint make_frag_prog( const GLchar *pcszShader )
{
    GLuint hShader = glCreateShader( GL_FRAGMENT_SHADER );
    if (!hShader) FAIL(( "Can't create shader!" ));

    const GLchar *ppcszShader[1] = { pcszShader };

    glShaderSource( hShader, 1, ppcszShader, NULL );
    glCompileShader( hShader );
    GLint nStatus;
    glGetShaderiv( hShader, GL_COMPILE_STATUS, &nStatus );
    if (!nStatus)
    {
    	char szBuff[10240];
	glGetShaderInfoLog( hShader, sizeof(szBuff), NULL, szBuff );
	FAIL(( "Shader failed to compile:\n%s", szBuff ));
    }
    //printf( "Shader compiled okay!\n" );

    GLuint hProgram = glCreateProgram();
    if (!hProgram) FAIL(( "Can\'t create program!" ));

    glAttachShader( hProgram, hShader );

    glLinkProgram( hProgram );
    glGetProgramiv( hProgram, GL_LINK_STATUS, &nStatus );
    if (!nStatus)
    {
    	char szBuff[10240];
	glGetProgramInfoLog( hProgram, sizeof(szBuff), NULL, szBuff );
	FAIL(( "Program failed to link:\n%s", szBuff ));
    }
    //printf( "Program linked okay!\n" );

    return( hProgram );
}

//
// setviewport - setup viewport and projection
//

void setviewport( int w, int h )
{
    glViewport( 0, 0, w, h );
    glMatrixMode( GL_PROJECTION );
    glLoadIdentity();
    glOrtho( -1, 1, -1, 1, -1, 1 );
}

//
// reshape - handle GLUT window resizing
//

void reshape( int w, int h )
{
    scr_w = w;
    scr_h = h;
    scr_aspect = h / GLfloat(w);
    setviewport( w, h );
}

//
// display - handle GLUT repaints
//

void display()
{
    static float frame_time = 0;
    static int n_frames = 0;
    frame_time += elapsed_ms();
    ++n_frames;
    if (frame_time > 1000)
    {
	char szBuff[256];
	sprintf( szBuff, "%s [%.2f fps]", WINDOW_TITLE, 1000.0f * n_frames / frame_time );
	glutSetWindowTitle( szBuff );
	frame_time = 0;
	n_frames = 0;
    }

    // only fetch the video frame if we're using it
    if (!showpoles)
    {
	//
	// copy the video frame into the pbo (supposedly this saves a driver-side copy,
	// but this is probably a wash since we end up doing the copy anyway)
	//

	glBindBuffer( GL_PIXEL_UNPACK_BUFFER, yuv_buf );
	void *pbo = glMapBuffer( GL_PIXEL_UNPACK_BUFFER, GL_WRITE_ONLY );
	CHECK_GLERROR();

	vidcap->wait();
	int frameid = vidcap->get();
	while (frameid >= 0)
	{
	    // skip frames if we're behind to reduce latency
	    int nextframeid = vidcap->get();
	    if (nextframeid < 0) memcpy( pbo, vidcap->data( frameid ), vidcap->bytesperframe() );
	    vidcap->release( frameid );
	    frameid = nextframeid;
	}

	glUnmapBuffer( GL_PIXEL_UNPACK_BUFFER );
	CHECK_GLERROR();

	glBindTexture( GL_TEXTURE_2D, yuv_tex );
	CHECK_GLERROR();

	// define the texture using data at offset 0 in the PBO
	// (note we could probably have this step running in another thread)
	glPixelStorei( GL_UNPACK_ALIGNMENT, 1 );
	glTexSubImage2D( GL_TEXTURE_2D, 0, 0, 0, vidcap->width() / 2, vidcap->height(), GL_RGBA, GL_UNSIGNED_BYTE, 0 );
	CHECK_GLERROR();

	glBindBuffer( GL_PIXEL_UNPACK_BUFFER, 0 );

	//
	// perform YUYV->RGB conversion into the RGB texture (via FBO)
	//

	glBindFramebuffer( GL_FRAMEBUFFER, fb );
	glBindTexture( GL_TEXTURE_2D, rgb_tex );
	glFramebufferTexture2D( GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, rgb_tex, 0 );

	CheckFramebufferStatus();

	glViewport( 0, 0, vidcap->width(), vidcap->height() );
	glMatrixMode( GL_PROJECTION );
	glLoadIdentity();
	glOrtho( 0, 1, 0, 1, 0, 1 );

	glMatrixMode( GL_MODELVIEW );
	glLoadIdentity();

	glUseProgram( yuv_prog );

	glBindTexture( GL_TEXTURE_2D, yuv_tex );
	glTexEnvi( GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_REPLACE );
	glEnable( GL_TEXTURE_2D );

	glBegin( GL_TRIANGLES );
	    glTexCoord2f( -1,  1 ); glVertex2f( -1,  1 );
	    glTexCoord2f(  1,  1 ); glVertex2f(  1,  1 );
	    glTexCoord2f(  1, -1 ); glVertex2f(  1, -1 );
	glEnd();
	CHECK_GLERROR();
    }

    //
    // render the RGB texture to the screen
    //

    glBindFramebufferEXT( GL_FRAMEBUFFER_EXT, 0 );

    reshape( scr_w, scr_h );

    glMatrixMode( GL_MODELVIEW );
    glLoadIdentity();

    GLuint prog =
	juliaing ?
	    (showpoles ? juliapole_prog : julia_prog)
	    : (showpoles ? mandpole_prog : mand_prog);

    glUseProgram( prog );
    if (juliaing) glUniform2f( glGetUniformLocation( prog, "c" ), jx, jy );

    if (animate_translation)
    {
	trans_scale += M_PI / 500.0f;
	if (trans_scale >= 2 * M_PI) trans_scale -= 2 * M_PI;
    }
    if (animate_translation_phase)
    {
	trans_phase += M_PI / 500.0f;
	if (trans_phase >= 2 * M_PI) trans_phase -= 2 * M_PI;
    }
    float trans = 2.0f * cosf( trans_scale );
    trans = trans * trans * trans;
    // default to 1,1 (traditional mandlebrot)
    float tpx = trans * cosf( trans_phase ) * M_SQRT2;
    float tpy = trans * sinf( trans_phase ) * M_SQRT2;
    glUniform2f( glGetUniformLocation( prog, "trans_scale" ), tpx, tpy );
    if (animate_iters)
    {
	iterations += iter_dir;
	if (iterations >= iter_max)
	{
	    iterations = iter_max;
	    iter_dir = -iter_dir;
	}
	else if (iterations <= 1)
	{
	    iterations = 1;
	    iter_dir = -iter_dir;
	}
    }
    glUniform1f( glGetUniformLocation( prog, "iter_scale" ), 1.0f / iterations );

    glBindTexture( GL_TEXTURE_2D, rgb_tex );
    glTexEnvi( GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_REPLACE );
    if (mirror)
    {
	glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_MIRRORED_REPEAT );
	glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_MIRRORED_REPEAT );
    }
    else
    {
	glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT );
	glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT );
    }
    glEnable( GL_TEXTURE_2D );

    GLfloat left = cx - zoom;
    GLfloat right = cx + zoom;
    GLfloat top = cy - zoom * scr_aspect;
    GLfloat bottom = cy + zoom * scr_aspect;

    glBegin( GL_TRIANGLES );
	glTexCoord2f( left - (2 * zoom),  top );
	glVertex2f( -3,  1 );
	glTexCoord2f( right, top );
	glVertex2f(  1,  1 );
	glTexCoord2f( right, bottom + (2 * zoom) * scr_aspect );
	glVertex2f(  1, -3 );
    glEnd();
    CHECK_GLERROR();

    glutSwapBuffers();
    glutPostRedisplay();
}

//
// command - handle keyboard, menu or special key commands
//

void command( int cmd )
{
    switch (cmd)
    {
    #define MK_CMD_CASE(label,value,case,cmd) \
    case cmd; break;
    LIST_COMMANDS(MK_CMD_CASE)
    }
}

//
// keyboard - handle GLUT keypresses
//

void keyboard( unsigned char c, int x, int y )
{
    command( c );
}

//
// special - handle other GLUT keypresses
//

void special( int c, int x, int y )
{
    command( MK_SPECIALKEY(c) );
}

//
// set_julia_pos - map the window coordinates into the complex plane
//

static void set_julia_pos( int x, int y )
{
    int dx = x - julia_pt[0];
    int dy = y - julia_pt[1];
    // backwards, yech
    jy = cx + (2 * zoom / scr_w) * dx;
    jx = cy + (2 * zoom / scr_h) * dy * scr_aspect;
    // it appears you can't update uniforms unless the appropriate
    // program is being used...
}

//
// motion - handle GLUT mouse motion
//

void motion( int x, int y )
{
    if (dragging)
    {
    	int dx = x - drag_pt[0];
    	int dy = y - drag_pt[1];
	cx -= (2 * zoom / scr_w) * dx;
	cy -= (2 * zoom / scr_h) * dy * scr_aspect;
    	drag_pt[0] = x;
    	drag_pt[1] = y;
    }

    if (juliaing)
    {
	set_julia_pos( x, y );
    }
}

//
// mouse - handle GLUT mousing
//

void mouse( int button, int state, int x, int y )
{
    switch (button)
    {
    case GLUT_LEFT_BUTTON:
    	dragging = (state == GLUT_DOWN);
	drag_pt[0] = x;
	drag_pt[1] = y;
	break;

    case GLUT_MIDDLE_BUTTON:
	juliaing = (state == GLUT_DOWN);
	// first point is at current center
	julia_pt[0] = scr_w / 2;
	julia_pt[1] = scr_h / 2;
	set_julia_pos( x, y );
    	break;

    // GLUT_RIGHT_BUTTON used by menu!

    case 3: // scollwheel
    	if (state == GLUT_DOWN)
	{
	    zoom *= 0.9;
	    if (zoom < 1e-9) zoom = 1e-9;
	}
	break;

    case 4: // scrollwheel
    	if (state == GLUT_DOWN)
	{
	    zoom *= 1.1;
	    if (zoom > 1e+3) zoom = 1e+3;
	}
    	break;

    default:
    	if (verbose) fprintf( stderr, "%d %d (%d,%d)\n", button, state, x, y );
	break;
    }
}

//
// init_gl - setup GL once the video capture device is started
//

void init_gl()
{
    if (use_aniso)
    {
	glGetFloatv( GL_MAX_TEXTURE_MAX_ANISOTROPY_EXT, &max_aniso );
	if (verbose) fprintf( stderr, "MAX_ANISO: %f\n", max_aniso );
	//glGenFramebuffersEXT( 1, &fb );
    }

    glActiveTexture( GL_TEXTURE0 );
    glGenTextures( 1, &yuv_tex );
    glBindTexture( GL_TEXTURE_2D, yuv_tex );
    CHECK_GLERROR();

    glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR );
    glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR );
    glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE );
    glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE );
    CHECK_GLERROR();

    glTexImage2D( GL_TEXTURE_2D, 0, GL_RGBA, vidcap->width() / 2, vidcap->height(), 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL );

    yuv_prog = make_frag_prog(
    	"uniform sampler2D yuv_tex;\n"
	"uniform vec2 size;\n"
	"uniform vec2 scale;\n"
	"\n"
	"void main( void )\n"
	"{\n"
	"   vec2 xy = floor( gl_TexCoord[0].xy * size );\n"
	"   vec2 sp = (xy + vec2(0.5, 0.5)) * scale;\n"
	"\n"
	"   float y;\n"
	"   if (fract( xy.x * 0.5 ) < 0.5)\n"
	"   {\n"
	"       y = texture2D( yuv_tex, vec2(sp.x + 0.5 * scale.x, sp.y) ).r;\n"
	"   }\n"
	"   else\n"
	"   {\n"
	"       y = texture2D( yuv_tex, vec2(sp.x - 0.5 * scale.x, sp.y) ).b;\n"
	"   }\n"
	"\n"
	"   vec4 uv = texture2D( yuv_tex, sp );\n"
	"   float u = uv.g - 0.5;\n"
	"   float v = uv.a - 0.5;\n"
	"   y = 1.1643 * (y - 0.0625);\n"
	"   float r = y + 1.5958 * v;\n"
	"   float g = y - 0.39173 * u - 0.81290 * v;\n"
	"   float b = y + 2.017 * u;\n"
	"   gl_FragColor.rgb = vec3(r, g, b);\n"
	"}\n"
    );

    glUseProgram( yuv_prog );
    glUniform1i( glGetUniformLocation( yuv_prog, "yuv_tex" ), 0 );
    glUniform2f( glGetUniformLocation( yuv_prog, "size" ), GLfloat(vidcap->width()), GLfloat(vidcap->height()) );
    glUniform2f( glGetUniformLocation( yuv_prog, "scale" ), 1.0 / GLfloat(vidcap->width()), 1.0 / GLfloat(vidcap->height()) );

    // setup a pixel buffer object (PBO) to stream video data into
    glGenBuffers( 1, &yuv_buf );
    glBindBuffer( GL_PIXEL_UNPACK_BUFFER, yuv_buf );
    glBufferData( GL_PIXEL_UNPACK_BUFFER, vidcap->bytesperframe(), NULL, GL_STREAM_DRAW );
    glBindBuffer( GL_PIXEL_UNPACK_BUFFER, 0 );
    
    // setup FBO and RGB texture
    glGenFramebuffers( 1, &fb );

    glGenTextures( 1, &rgb_tex );
    glBindTexture( GL_TEXTURE_2D, rgb_tex );
    CHECK_GLERROR();

    glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, use_mipmaps ? GL_LINEAR_MIPMAP_LINEAR : GL_LINEAR );
    glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR );
    CHECK_GLERROR();

    if (use_mipmaps) glTexParameteri( GL_TEXTURE_2D, GL_GENERATE_MIPMAP_SGIS, GL_TRUE );
    CHECK_GLERROR();

    if (use_aniso) glTexParameterf( GL_TEXTURE_2D, GL_TEXTURE_MAX_ANISOTROPY_EXT, max_aniso );
    CHECK_GLERROR();

    glTexImage2D( GL_TEXTURE_2D, 0, GL_RGB, vidcap->width(), vidcap->height(), 0, GL_RGB, GL_UNSIGNED_BYTE, NULL );

    vid_aspect = vidcap->width() / GLfloat(vidcap->height());

    mand_prog = make_frag_prog(
    	"uniform sampler2D rgb_tex;\n"
	"uniform vec2 trans_scale;\n"
	"uniform float vid_aspect;\n"
	"uniform float iter_scale;\n"
	"\n"
	"void main( void )\n"
	"{\n"
    	"   vec2 p = gl_TexCoord[0].yx;\n"
    	"   vec2 c = trans_scale * p;\n"
	"   float s = 0;\n"
	"   vec3 rgb = 0.0;\n"
	"\n"
	"   while (s < 1.0)\n"
	"   {\n"
	"       p = vec2( p.x * p.x - p.y * p.y + c.x, 2.0 * p.x * p.y + c.y );\n"
	"   	rgb += texture2D( rgb_tex, vec2(p.y + 0.5, (p.x * vid_aspect) + 0.5) );\n"
	"   	s += iter_scale;\n"
	"   }\n"
	"\n"
	"   //gl_FragColor.rgb = texture2D( rgb_tex, vec2(p.y + 0.5, (p.x * vid_aspect) + 0.5) );\n"
	"   gl_FragColor.rgb = rgb * iter_scale;\n"
	"}\n"
    );

    glUseProgram( mand_prog );
    glUniform1i( glGetUniformLocation( mand_prog, "rgb_tex" ), 0 );
    glUniform1f( glGetUniformLocation( mand_prog, "vid_aspect" ), vid_aspect );

    mandpole_prog = make_frag_prog(
	"uniform vec2 trans_scale;\n"
	"uniform float iter_scale;\n"
	"\n"
	"void main( void )\n"
	"{\n"
    	"   vec2 p = gl_TexCoord[0].yx;\n"
    	"   vec2 c = trans_scale * p;\n"
	"   float s = 0;\n"
	"\n"
	"   while (s < 1.0)\n"
	"   {\n"
	"       p = vec2( p.x * p.x - p.y * p.y + c.x, 2.0 * p.x * p.y + c.y );\n"
	"   	s += iter_scale;\n"
	"   }\n"
	"\n"
	"   float len = length(p);\n"
	"   float r = (len > 0) ? (1 / len) : 0;\n"
	"   p *= r;\n"
	"   gl_FragColor.rg = 0.5 * (p + 1);\n"
	"   gl_FragColor.b = (r < 1) ? r : len;\n"
	"}\n"
    );

    julia_prog = make_frag_prog(
    	"uniform sampler2D rgb_tex;\n"
	"uniform vec2 trans_scale;\n"
	"uniform float vid_aspect;\n"
	"uniform float iter_scale;\n"
    	"uniform vec2 c;\n"
	"\n"
	"void main( void )\n"
	"{\n"
    	"   vec2 p = gl_TexCoord[0].yx;\n"
    	"   vec2 cc = trans_scale * c;\n"
	"   float s = 0;\n"
	"   vec3 rgb = 0.0;\n"
	"\n"
	"   while (s < 1.0)\n"
	"   {\n"
	"       p = vec2( p.x * p.x - p.y * p.y + cc.x, 2.0 * p.x * p.y + cc.y );\n"
	"       //p = vec2( p.x * cc.x - p.y * cc.y + p.x, 2.0 * p.x * cc.y + p.y );\n"
	"   	rgb += texture2D( rgb_tex, vec2(p.y + 0.5, (p.x * vid_aspect) + 0.5) );\n"
	"   	s += iter_scale;\n"
	"   }\n"
	"\n"
	"   gl_FragColor.rgb = rgb * iter_scale;\n"
	"}\n"
    );

    glUseProgram( julia_prog );
    glUniform1i( glGetUniformLocation( julia_prog, "rgb_tex" ), 0 );
    glUniform1f( glGetUniformLocation( julia_prog, "vid_aspect" ), vid_aspect );

    juliapole_prog = make_frag_prog(
	"uniform vec2 trans_scale;\n"
	"uniform float iter_scale;\n"
    	"uniform vec2 c;\n"
	"\n"
	"void main( void )\n"
	"{\n"
    	"   vec2 p = gl_TexCoord[0].yx;\n"
    	"   vec2 cc = trans_scale * c;\n"
	"   float s = 0;\n"
	"\n"
	"   while (s < 1.0)\n"
	"   {\n"
	"       p = vec2( p.x * p.x - p.y * p.y + cc.x, 2.0 * p.x * p.y + cc.y );\n"
	"       //p = vec2( p.x * cc.x - p.y * cc.y + p.x, 2.0 * p.x * cc.y + p.y );\n"
	"   	s += iter_scale;\n"
	"   }\n"
	"\n"
	"   float len = length(p);\n"
	"   float r = (len > 0) ? (1 / len) : 0;\n"
	"   p *= r;\n"
	"   gl_FragColor.rg = 0.5 * (p + 1);\n"
	"   gl_FragColor.b = (r < 1) ? r : len;\n"
	"}\n"
    );
}

//
//
//

static void show_usage( const char *name )
{
    fprintf( stderr,
	"usage: %s [-d<devnum>]\n"
	"-d <devnum> = select /dev/video<devnum>, default is 0\n",
	name );
    exit( 0 );
}

//
// main - process all image filenames and setup GLUT/GL/DevIL
//

int main( int argc, char *argv[] )
{
    glutInit( &argc, argv );

    int vid_dev = 0;
    for (int i = 1; i < argc; ++i)
    {
	if (argv[i][0] == '-') switch (argv[i][1])
	{
	case 'd':
	    if (argv[i][2]) vid_dev = atoi( &argv[i][2] );
	    else if (i < argc - 1) vid_dev = atoi( argv[++i] );
	    break;
	case 'h':
	    show_usage( argv[0] );
	    break;
	}
	else show_usage( argv[0] );
    }

    glutInitDisplayMode( GLUT_DOUBLE | GLUT_RGB );
    glutInitWindowSize( scr_w, scr_h );
    glutCreateWindow( WINDOW_TITLE );
    glutReshapeFunc( reshape );
    glutDisplayFunc( display );
    glutKeyboardFunc( keyboard );
    glutSpecialFunc( special );
    glutMouseFunc( mouse );
    glutMotionFunc( motion );

    glutCreateMenu( command );
    #define MK_MENU(label,value,case,cmd) \
    glutAddMenuEntry( label, value );
    LIST_COMMANDS(MK_MENU)
    glutAttachMenu( GLUT_RIGHT_BUTTON );

    vidcap = new vid_capture( 4 );
    vidcap->open( vid_dev );
    vidcap->init( scr_w, scr_h );
    vidcap->map();
    vidcap->start();

    init_gl();

    glutMainLoop();

    vidcap->stop();
    vidcap->unmap();
    delete vidcap;
    
    return( 0 );
}
