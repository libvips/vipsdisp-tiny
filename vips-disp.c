/* Tiny display-an-image demo program. 
 *
 * This is not supposed to be a complete image viewer, it's just to 
 * show how to display a VIPS image (or the result of a VIPS computation) in a
 * window.
 *
 * Compile with:

	cc -g -Wall `pkg-config vips gtk+-2.0 --cflags --libs` \
		vips-disp.c -o vips-disp

 */

#include <stdio.h>

#include <gtk/gtk.h>
#include <vips/vips.h>

/* Just to demo progress feedback. This should be used to update a widget
 * somewhere.
 */
static void
image_preeval( VipsImage *image, VipsProgress *progress, const char *filename )
{
	printf( "load starting for %s ...\n", filename );
}

static void
image_eval( VipsImage * image, VipsProgress *progress, const char *filename )
{
	static int previous_precent = -1;

	if( progress->percent != previous_precent ) {
		printf( "%s %s: %d%% complete\r", 
			g_get_prgname(), filename, progress->percent );
		previous_precent = progress->percent;
	}
}

static void
image_posteval( VipsImage *image, VipsProgress *progress, const char *filename )
{
	printf( "\nload done in %g seconds\n", 
		g_timer_elapsed( progress->start, NULL ) );
}

static VipsImage *
load_image( const char *filename )
{
	VipsImage *image;

	if( !(image = vips_image_new_from_file( filename, NULL ))) 
		return NULL;

	/* Attach an eval callback: this will tick down if we open this image
	 * via a temp file.
	 */
	vips_image_set_progress( image, TRUE ); 
	g_signal_connect( image, "preeval",
		G_CALLBACK( image_preeval ), (void *) filename);
	g_signal_connect( image, "eval",
		G_CALLBACK( image_eval ), (void *) filename);
	g_signal_connect( image, "posteval",
		G_CALLBACK( image_posteval ), (void *) filename);

	return image;
}

typedef struct _Update {
	GtkWidget *drawing_area;
	VipsRect rect;
} Update;

/* The main GUI thread runs this when it's idle and there are tiles that need
 * painting. 
 */
static gboolean
render_cb( Update *update )
{
  gtk_widget_queue_draw_area( update->drawing_area,
			      update->rect.left, update->rect.top,
			      update->rect.width, update->rect.height );

  g_free( update );

  return( FALSE );
}

/* Come here from the vips_sink_screen() background thread when a tile has been
 * calculated. We can't paint the screen directly since the main GUI thread
 * might be doing something. Instead, we add an idle callback which will be
 * run by the main GUI thread when it next hits the mainloop.
 */
static void
render_notify( VipsImage *image, VipsRect *rect, void *client )
{
	GtkWidget *drawing_area = GTK_WIDGET( client );
	Update *update = g_new( Update, 1 );

	update->rect = *rect;
	update->drawing_area = drawing_area;

	g_idle_add( (GSourceFunc) render_cb, update );
}

/* Make the image for display from the raw disc image. Could do
 * anything here, really. Uncomment sections to try different effects. Convert
 * to 8-bit RGB would be a good idea.
 */
static VipsImage *
build_display_image( VipsImage *in, GtkWidget *drawing_area )
{
	VipsImage *image;
	VipsImage *x;

	/* Edit these to add or remove things from the pipeline we build. These
	 * should be wired up to something in a GUI.
	 */
	const gboolean zoom_in = FALSE;
	const gboolean zoom_out = TRUE;

	/* image represents the head of the pipeline. Hold a ref to it as we
	 * work.
	 */
	image = in;
	g_object_ref( image ); 

	if( zoom_out ) {
		if( vips_subsample( image, &x, 4, 4, NULL ) ) {
			g_object_unref( image );
			return( NULL ); 
		}
		g_object_unref( image );
		image = x;
	}

	if( zoom_in ) {
		if( vips_zoom( image, &x, 4, 4, NULL ) ) {
			g_object_unref( image );
			return( NULL ); 
		}
		g_object_unref( image );
		image = x;
	}

	/* This won't work for CMYK, you need to mess about with ICC profiles
	 * for that, but it will do everything else.
	 */
	if( vips_colourspace( image, &x, VIPS_INTERPRETATION_sRGB, NULL ) ) {
		g_object_unref( image );
		return( NULL ); 
	}
	g_object_unref( image );
	image = x;

	/* Drop any alpha.
	 */
	if( vips_extract_band( image, &x, 0, "n", 3, NULL ) ) {
		g_object_unref( image );
		return( NULL ); 
	}
	g_object_unref( image );
	image = x;

	x = vips_image_new();
	if( vips_sink_screen( image, x, NULL, 128, 128, 400, 0, 
		render_notify, drawing_area ) ) {
		g_object_unref( image );
		g_object_unref( x );
		return( NULL );
	}
	g_object_unref( image );
	image = x;

	return( image );
}

static void
expose_rect( GtkWidget *drawing_area, VipsRegion *region, GdkRectangle *expose )
{
	VipsRect image;
	VipsRect area;
	VipsRect clip;
	guchar *buf;
	int lsk;

	/* Clip against the image size ... we don't want to try painting 
	 * outside the image area.
	 */
	image.left = 0;
	image.top = 0;
	image.width = region->im->Xsize;
	image.height = region->im->Ysize;
	area.left = expose->x;
	area.top = expose->y;
	area.width = expose->width;
	area.height = expose->height;
	vips_rect_intersectrect( &image, &area, &clip );
	if( vips_rect_isempty( &clip ) )
		return;

	if( vips_region_prepare( region, &clip ) )
		return;
	buf = (guchar *) VIPS_REGION_ADDR( region, clip.left, clip.top );
	lsk = VIPS_REGION_LSKIP( region );

	gdk_draw_rgb_image( GTK_WIDGET( drawing_area )->window,
		GTK_WIDGET( drawing_area )->style->white_gc,
		clip.left, clip.top, clip.width, clip.height,
		GDK_RGB_DITHER_MAX, buf, lsk );
}

static gboolean
expose_cb( GtkWidget *drawing_area, GdkEventExpose *event, VipsRegion *region )
{
	GdkRectangle *expose;
	int i, n;

	gdk_region_get_rectangles( event->region, &expose, &n );
	for( i = 0; i < n; i++ )
		expose_rect( drawing_area, region, &expose[i] );
	g_free( expose );

	return( TRUE );
}

int
main( int argc, char **argv )
{
	VipsImage *image;
	VipsImage *display;
	VipsRegion *region;

	GtkWidget *window;
	GtkWidget *scrolled_window;
	GtkWidget *drawing_area;

	if( VIPS_INIT( argv[0] ) )
		vips_error_exit( "unable to start VIPS" );
	gtk_init( &argc, &argv );

	if( argc != 2 )
		vips_error_exit( "usage: %s <filename>", argv[0] );

	if( !(image = load_image( argv[1] )) )
		vips_error_exit( "unable to load %s", argv[1] );

	window = gtk_window_new( GTK_WINDOW_TOPLEVEL );
	g_signal_connect( window, "destroy", 
		G_CALLBACK( gtk_main_quit ), NULL );

	scrolled_window = gtk_scrolled_window_new( NULL, NULL );
	gtk_container_add( GTK_CONTAINER( window ), scrolled_window );

	drawing_area = gtk_drawing_area_new();
	if( !(display = build_display_image( image, drawing_area )) ||
		!(region = vips_region_new( display )) )
		vips_error_exit( "unable to build display image" );
	g_signal_connect( drawing_area, "expose_event", 
		G_CALLBACK( expose_cb ), region );
	gtk_widget_set_size_request( drawing_area, 
		display->Xsize, display->Ysize );
	gtk_scrolled_window_add_with_viewport( 
		GTK_SCROLLED_WINDOW( scrolled_window ), drawing_area );

	gtk_window_set_default_size( GTK_WINDOW( window ), 250, 250 );
	gtk_widget_show_all( window );

	gtk_main();

	g_object_unref( region ); 
	g_object_unref( display ); 
	g_object_unref( image ); 

	return( 0 );
}
