/*
 * AutoZen
 * Steven James <pyro@linuxlabs.com>
 * Linux Labs http://www.linuxlabs.com
 * http://www.linuxlabs.com/software/AutoZen.html
 *
 * This is Free software, licensed under the GNU Public License V2.
 *
 * Version: 2.1
 */

#include <stdio.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <time.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <pthread.h>
#include <gtk/gtk.h>
#include <portaudio.h>


/* Messy, but should manage to include the OSS
	Headers for Linux and the various *BSDs
	+ Solaris. Thanks to Trevor Johnson for this */
 
/* "The nice thing about standards..." */
#if defined (__FreeBSD__)
#include <machine/soundcard.h>
#else
#if defined (__NetBSD__) || defined (__OpenBSD__)
#include <soundcard.h>          /* OSS emulation */
#undef ioctl
#else
/* BSDI, Linux, Solaris */
#include <sys/soundcard.h>
#endif                          /* __NetBSD__ or __OpenBSD__ */
#endif                          /* __FreeBSD__ */

//#define DEBUG
/////////////////////////////////////////////////////////////////////////
//
//		Global Widgets
//
/////////////////////////////////////////////////////////////////////////
GtkObject *adj_beat;
GtkWidget *scale_beat;

GtkObject *adj_base;
GtkWidget *scale_base;

GtkWidget *ClockTime;
GtkWidget *BeatFreq;

GtkWidget *BaseFreq;
GtkWidget *VolLabel;

GtkObject *adj_vol;

GtkWidget *PhaseLabel;
GtkObject *adj_colorboxphase;
GtkWidget *scale_colorboxphase;

GtkWidget *PlayPixWid;
GdkPixmap *PlayPix;
GdkBitmap *PlayBit;

GdkPixmap *PausePix;
GdkBitmap *PauseBit;

GdkPixmap *PausedPix;
GdkBitmap *PausedBit;

#define COLORBOX_X 640
#define COLORBOX_Y 480

GtkWidget *ColorBox;
GtkStyle  *ColorBox_default_style, *ColorBox_new_style;
GdkColor  ColorBox_new_color = {0, 0x8000, 0x0, 0x8000};
gint ColorBoxTOhandle;

#ifdef COLORBOX_DEFAULT_ON
int ColorBoxX = COLORBOX_X;
int ColorBoxY = COLORBOX_Y;

#else
int ColorBoxX = 0;
int ColorBoxY = 0;
#endif

gint volTOhandle;

char bQuit=0;

////////////////////////////////////////////////////////////////////////
//
//						Pixmaps for controls
//
////////////////////////////////////////////////////////////////////////

#include "record.xpm"
#include "stop.xpm"
#include "play.xpm"
#include "lila.xpm"
#include "pause.xpm"
#include "paused.xpm"


//////////////////////////////////
//
//	Waveform generation
//
/////////////////////////////////

#define SAMPLE_RATE 44100

#define MAX_HARMONICS 10
#define DEFAULT_HARMONICS 3

#define BEAT_MAX 40

int nHarmonics = DEFAULT_HARMONICS;

int *WaveTable;

double curval;

double harmonic_curtimeL[MAX_HARMONICS];
double harmonic_curtimeR[MAX_HARMONICS];

double curtime;
double curtime2;

volatile double increment=300;
volatile double detune=10.0;
volatile double volume = 0.0;
volatile double phase = 0.0;

//	Volume fade controls.
char Starting=1;
double VolumeTarget=50.0;
double VolumeDelta=0.5;
char Stopping=0;

/* sequencer variables */
volatile char playing=0;
char paused=0;
char *playname = NULL;
FILE *fSequence=NULL;
char szInstruction[1024];
volatile int EndSeconds=0;	/* number of seconds when the current instruction expires */
double target;
double dBeatIncrement;
int LastSeconds;

int count=0;
volatile int seconds=0;
char tmptime[7] = "00:00";

// functions 
gint volTimeOut(gpointer data);



void PrintHandler(const char *line) 
{

}


void Quit (GtkWidget *widget, gpointer data)
{

	if(Stopping) {
		bQuit=1;
		exit(0);
	}

	Stopping=1;
	volTOhandle = gtk_timeout_add(10,volTimeOut,NULL);
//	bQuit=1;
}

int InitSequencer(const char *fname)
{
	fSequence = fopen(fname,"r");
	if(fSequence) {
		seconds=0;	/* so that the nonexistant current instruction is expired */
		gtk_widget_set_sensitive(GTK_WIDGET(scale_beat),FALSE);
		gtk_pixmap_set( GTK_PIXMAP(PlayPixWid), PausePix, PauseBit);
		playing=1;
		return(1);
	}

	return(0);
}

void file_ok_sel (GtkWidget *w, GtkFileSelection *fs)
{
	InitSequencer(gtk_file_selection_get_filename (GTK_FILE_SELECTION (fs)));


#ifdef DEBUG
	g_print ("Now playing %s\n",gtk_file_selection_get_filename (GTK_FILE_SELECTION (fs)));
#endif

	gtk_object_destroy(GTK_OBJECT(fs));

}

void SetPause(void) 
{

	if(paused)
		gtk_pixmap_set( GTK_PIXMAP(PlayPixWid), PausedPix, PausedBit);
	else
		gtk_pixmap_set( GTK_PIXMAP(PlayPixWid), PausePix, PauseBit);
}

void Play (GtkWidget *widget, gpointer data)
{
	GtkWidget *filew;
     
	if(playing) {
		paused = 1- paused;    
		SetPause();
		return;
	}

	/* Create a new file selection widget */
	filew = gtk_file_selection_new ("File selection");

	gtk_file_selection_hide_fileop_buttons( GTK_FILE_SELECTION(filew));
         
	/* Connect the ok_button to file_ok_sel function */
	gtk_signal_connect (GTK_OBJECT (GTK_FILE_SELECTION (filew)->ok_button),
                             "clicked", (GtkSignalFunc) file_ok_sel, filew );
         
	/* Connect the cancel_button to destroy the widget */
	gtk_signal_connect_object (GTK_OBJECT (GTK_FILE_SELECTION
                                    (filew)->cancel_button),
                                    "clicked", (GtkSignalFunc) gtk_widget_destroy,
                                    GTK_OBJECT (filew));
         
	/* Lets set the filename, as if this were a save dialog, and we are giving
	a default filename */
	gtk_file_selection_set_filename (GTK_FILE_SELECTION(filew), 
										"~/.autozen/*.seq");
         
	gtk_file_selection_complete( GTK_FILE_SELECTION(filew), "~/.autozen/*.seq");
	gtk_widget_show(filew);
}


int CheckSequencer()
{
char *token;
int tmp;

#ifdef DEBUG
	printf("CheckSequencer: seconds = %u, EndSeconds = %u,increment = %d\n",seconds,EndSeconds,dBeatIncrement); 
#endif

	if(seconds >= EndSeconds) {	/* if instruction is expired */
		if(target) {
			detune = target;
			gtk_adjustment_set_value(GTK_ADJUSTMENT(adj_beat), (BEAT_MAX - detune));
			target=0;
			dBeatIncrement = 0;
		}

		fgets(szInstruction,sizeof(szInstruction),fSequence);	// fetch it in
#ifdef DEBUG
	printf("CheckSequencer: %s\n",szInstruction);
#endif
		token = strtok(szInstruction," ,\n");

		if(!strcmp(token,"SET")) {
			/* we are to set the frequency */
			token = strtok(NULL," ,\n");
			/* token is the frequency */
			detune = atof(token);
#ifdef DEBUG
	printf("CheckSequencer: SET %d\n",detune);
#endif
			gtk_adjustment_set_value(GTK_ADJUSTMENT(adj_beat), (BEAT_MAX - detune));
			target = seconds = EndSeconds = 0;
			return;
		}

		if(!strcmp(token,"VOLUME")) {
			token = strtok(NULL," ,\n");
			/* token is the volume */
			VolumeTarget = atof(token);
#ifdef DEBUG
	printf("CheckSequencer: VOLUME %d\n",VolumeTarget);
#endif
			if(Starting)
				gtk_timeout_remove( volTOhandle);
			gtk_adjustment_set_value(GTK_ADJUSTMENT(adj_vol), (100 - VolumeTarget));
			return;
		}

		if(!strcmp(token,"FADE")) {
			token = strtok(NULL," ,\n");
			/* token is the volume */
			VolumeTarget = atof(token);
			token = strtok(NULL," ,\n");
			/* token is the duration of the fade */
			Starting = atoi(token);
#ifdef DEBUG
	printf("CheckSequencer: FADE %f %u\n",VolumeTarget,Starting);
#endif
			VolumeDelta = ((VolumeTarget - volume) / Starting) /50;
			volTOhandle = gtk_timeout_add(10,volTimeOut,NULL);
			return;
		}

		if(!strcmp(token,"BASE")) {
			/* we are to set the frequency */
			token = strtok(NULL," ,\n");
			/* token is the frequency */
			increment = atof(token);
#ifdef DEBUG
	printf("CheckSequencer: SET %d\n",detune);
#endif
			gtk_adjustment_set_value(GTK_ADJUSTMENT(adj_base), (1000 - increment));
			return;
		}

		if(!strcmp(token,"HARMONICS")) {
			/* we are to set the number of harmonics */
			token = strtok(NULL," ,\n");
			tmp = atoi(token);
			if(tmp < MAX_HARMONICS)
				nHarmonics = tmp;
			
#ifdef DEBUG
	printf("CheckSequencer: HARMONICS %u\n",nHarmonics);
#endif
			return;
		}

		if(!strcmp(token,"SLIDE")) {
			token = strtok(NULL," ,\n");
			target = atof(token);
			token = strtok(NULL," ,\n");
			EndSeconds = atof(token);
			dBeatIncrement = target - detune;
			dBeatIncrement/= EndSeconds;
			seconds = LastSeconds = 0;
			return;
		}

		if(!strcmp(token,"HOLD")) {
			token = strtok(NULL," ,\n");
			target = detune;
			EndSeconds = atof(token);
			dBeatIncrement = 0;
			seconds = LastSeconds = 0;
			return;
		}

		if(!strcmp(token,"PAUSE")) {
			paused = 1;
			SetPause();
			return;
		}

		if(!strcmp(token,"EXIT")) {
			Quit(NULL,NULL);
			StopSequencer();
			return;
		}

		if(!strcmp(token,"END")) {
			StopSequencer();
			return;
		}
	}	/* end if seconds >= EndSeconds */

	if(paused) {
		EndSeconds++;	// delay the end!
		return;
	}	/* end if paused */

	detune += dBeatIncrement;
	gtk_adjustment_set_value(GTK_ADJUSTMENT(adj_beat), (BEAT_MAX - detune));
	return;
}	/* end CheckSequencer */
			
				
int StopSequencer(void)
{

	if(!playing)
		return(0);

	playing=0;
	seconds = LastSeconds = 0;

	target = dBeatIncrement = playing = paused = LastSeconds = seconds = EndSeconds = 0;

	gtk_widget_set_sensitive(GTK_WIDGET(scale_beat),TRUE);
	gtk_pixmap_set( GTK_PIXMAP(PlayPixWid), PlayPix, PlayBit);

	fclose(fSequence);
	fSequence = NULL;
	return(1);
}

	
	


int InitWaveTable(unsigned int SampleRate)
{
unsigned int i;
double increment = (2*M_PI) / SampleRate;
double Current=0;

	WaveTable = (int *) calloc(SampleRate*2,sizeof(int));

	for(i=0;i<SampleRate;i++,Current += increment) {
		WaveTable[i]= (int) floor( sin(Current) * 127);
	}

	return(1);
}	// end InitWaveTable

void value_change_no_invert(GtkWidget *widget, gpointer data)
{
	*((double *)data) = GTK_ADJUSTMENT(widget) ->value;

#ifdef DEBUG
    g_print ("Value Change: %f\n",*((double *)data));
#endif
}

void value_change(GtkWidget *widget, gpointer data)
{
	*((double *)data) = GTK_ADJUSTMENT(widget) ->upper;
	*((double *)data) -= GTK_ADJUSTMENT(widget) ->value;
	if(!playing)
		seconds=0;

#ifdef DEBUG
    g_print ("Value Change: %f\n",*((double *)data));
#endif
}

void label_change_value(GtkWidget *widget, gpointer label)
{
	double data;
	char tmp[10];
	char **tmp2;

	data = GTK_ADJUSTMENT(widget)->upper;
	data -= GTK_ADJUSTMENT(widget)->value;
	sprintf(tmp,"%02.1f",data);

	gtk_label_set(GTK_LABEL(label),tmp);
	
#ifdef DEBUG
    g_print ("Lable Value Change: %s\n",tmp);
#endif
}
	
	
     

gint delete_event(GtkWidget *widget, GdkEvent *event, gpointer data)
{
    g_print ("delete event occured\n");
    /* if you return FALSE in the "delete_event" signal handler,
     * GTK will emit the "destroy" signal.  Returning TRUE means
     * you don't want the window to be destroyed.
     * This is useful for popping up 'are you sure you want to quit ?'
     * type dialogs. */

    /* Change TRUE to FALSE and the main window will be destroyed with
     * a "delete_event". */

    return (TRUE);
}

void ColorBoxDestroy(GtkWidget *widget, gpointer data)
{
	
	gtk_timeout_remove( ColorBoxTOhandle );
}

/* another callback */
void destroy (GtkWidget *widget, gpointer data)
{
    gtk_main_quit ();
}

void IncrementCurtimes(double timeval[], int harmonics, double increment, double detune)
{
	int i;

	for(i=0;i<harmonics; i++) {
		timeval[i] += increment * pow(2,i) + detune;
		timeval[i] = fmod(timeval[i],SAMPLE_RATE);
	}
}

int ComputeSummation(double timeval[], int harmonics, double Volume)
{
	int i;
	int sigma=0;

	for(i=0; i<harmonics; i++) {
		sigma += (int) (WaveTable[ (int) floor(timeval[i])] /(1<<i));
	}

	sigma /=2;

	sigma +=128;

	return( floor( (Volume*sigma)/100));
}
	
void SetColorBox(double phase)
{

	ColorBox_new_color.red = floor(0xffff * phase);
	ColorBox_new_color.green = floor(0xffff * (1-phase));
	ColorBox_new_color.blue = 0x0;

	ColorBox_default_style = gtk_widget_get_style( GTK_WIDGET(ColorBox));

	ColorBox_new_style = gtk_style_copy(ColorBox_default_style);
	ColorBox_new_style->bg[GTK_STATE_INSENSITIVE] = ColorBox_new_color;
	ColorBox_new_style->fg[GTK_STATE_INSENSITIVE] = ColorBox_new_color;

	gtk_widget_set_style(GTK_WIDGET(ColorBox), ColorBox_new_style);

	gtk_widget_queue_draw(GTK_WIDGET(ColorBox));

// fprintf(stderr, "SetColorBox %f\n", phase);
}
	
		
double PhaseDifference(double CurtimeL[], double CurtimeR[])
{
	double a = CurtimeL[0];
	double b = CurtimeR[0];
	double res1, res2;
	

	a = fmod(a + SAMPLE_RATE + phase * SAMPLE_RATE, SAMPLE_RATE);
	if(a<b) {
		res1 = b-a;
		res2 = (a+SAMPLE_RATE)-b;
	} else {
		res1 = a-b;
		res2 = (b+SAMPLE_RATE)-a;
	}

	if(res1<res2)
		return(res1 / SAMPLE_RATE);

	return(res2 / SAMPLE_RATE);
}

gint ColorBoxTimeOut(gpointer data) {

//	SetColorBox( fmod( 2 * PhaseDifference(harmonic_curtimeL, harmonic_curtimeR) +phase +1.0, 1.0));
	SetColorBox( 2 * PhaseDifference(harmonic_curtimeL, harmonic_curtimeR) );
	return(TRUE);
}

	
void *SoundThread(void *v)
{
	int32_t iCur[2];
	int iCharIn;
	unsigned int SampleRate;
	int arg;
	char quit=0;
	int i,j;
	PaStream *stream;
	PaError err;

	err = Pa_Initialize ();
	if (err != paNoError) {
		fprintf (stderr, "PortAudio error: %s\n", Pa_GetErrorText (err));
		return 0;
	}

	// EXPERIMENTAL, shrink the buffer to improve response time!
	err = Pa_OpenDefaultStream (&stream,
					0, /* no input channels */
					2, /* stereo output */
					paInt32, /* 32 bit floating point output */
					SAMPLE_RATE,
					0, /* frames per buffer, i.e. the number
						of sample frames that PortAudio will
						request from the callback. Many apps
						may want to use
						paFramesPerBufferUnspecified, which
						tells PortAudio to pick the best,
						possibly changing, buffer size.*/
					NULL, NULL);


	if (err != paNoError) {
		fprintf (stderr, "PortAudio error: %s\n", Pa_GetErrorText (err));
		return 0;
	}
	InitWaveTable(SampleRate);

	curtime=curtime2=0;
	
	while(!bQuit) {
		for(j=0;j<60*SAMPLE_RATE;j++) {

			IncrementCurtimes(harmonic_curtimeL, nHarmonics, increment, 0.0);
			IncrementCurtimes(harmonic_curtimeR, nHarmonics, increment, detune);

			iCur[0] = ComputeSummation(harmonic_curtimeL, nHarmonics, volume);
			iCur[1] = ComputeSummation(harmonic_curtimeR, nHarmonics, volume);
			
			Pa_WriteStream (stream, iCur, 1);

			count++;	// bump the sample counter!
		}

		seconds++;

	}	// end while		

	Pa_Terminate ();
	
	return NULL;
}

gint volTimeOut(gpointer data) {

	if(Stopping) {
		gtk_adjustment_set_value(GTK_ADJUSTMENT(adj_vol), 100 - (volume-0.5));
		if(volume <= 0.0) {
			bQuit=1;
			exit(0);
		}
		return(1);
	}

	if(Starting) {
		gtk_adjustment_set_value(GTK_ADJUSTMENT(adj_vol), 100 - (volume+VolumeDelta));
		if(abs(volume-VolumeTarget) <= abs(VolumeDelta)) {
			Starting=0;
			gtk_adjustment_set_value(GTK_ADJUSTMENT(adj_vol), 100 - VolumeTarget);
		}
	} else {
		gtk_timeout_remove( volTOhandle);
	}

	return(1);
}

gint TimeOut(gpointer data)
{
	struct tm *tmst;
	time_t tttime;
	char tod[10];
	
	tttime = time(NULL);
	tmst = localtime(&tttime);

	sprintf(tod,"%02u:%02u:%02u",tmst->tm_hour,tmst->tm_min,tmst->tm_sec);

	
	sprintf(tmptime,"%02u:%02u",seconds/60,seconds%60);
	gtk_label_set(GTK_LABEL(data),tmptime);
	gtk_label_set(GTK_LABEL(ClockTime),tod);

	if(playing)
		CheckSequencer();

	return(TRUE);
}


GtkWidget *create_HelpWindow (const char *text)
{
	GtkWidget *HelpWindow;
	GtkWidget *vbox1;
	GtkWidget *help_text;
	GtkWidget *HelpQuit;
	GtkTextBuffer *help_buf;

	HelpWindow = gtk_window_new (GTK_WINDOW_TOPLEVEL);
	gtk_object_set_data (GTK_OBJECT (HelpWindow), "HelpWindow", HelpWindow);
	gtk_window_set_title (GTK_WINDOW (HelpWindow), "AutoZen Help");
	gtk_window_position (GTK_WINDOW (HelpWindow), GTK_WIN_POS_CENTER);
	gtk_window_set_policy (GTK_WINDOW (HelpWindow), TRUE, TRUE, FALSE);

	vbox1 = gtk_vbox_new (FALSE, 0);
	gtk_object_set_data (GTK_OBJECT (HelpWindow), "vbox1", vbox1);
	gtk_widget_show (vbox1);
	gtk_container_add (GTK_CONTAINER (HelpWindow), vbox1);

	help_text = gtk_text_view_new ();
	gtk_text_view_set_wrap_mode (GTK_TEXT_VIEW (help_text), GTK_WRAP_WORD);
	
	help_buf = gtk_text_buffer_new (NULL);
	gtk_text_buffer_insert_at_cursor (help_buf, text, -1);
	gtk_text_view_set_buffer (GTK_TEXT_VIEW (help_text), help_buf);
	g_object_unref (help_buf);

	gtk_object_set_data (GTK_OBJECT (HelpWindow), "help_text", help_text);
	gtk_widget_show (help_text);
	gtk_box_pack_start (GTK_BOX (vbox1), help_text, TRUE, TRUE, 6);
//	gtk_widget_set_sensitive (help_text, FALSE);
	GTK_WIDGET_UNSET_FLAGS (help_text, GTK_CAN_FOCUS);
	gtk_widget_realize (help_text);
	

	HelpQuit = gtk_button_new_with_label ("Close");
	gtk_object_set_data (GTK_OBJECT (HelpWindow), "HelpQuit", HelpQuit);
	gtk_widget_show (HelpQuit);
	gtk_box_pack_start (GTK_BOX (vbox1), HelpQuit, FALSE, FALSE, 0);
	gtk_signal_connect_object (GTK_OBJECT (HelpQuit), "clicked",
		GTK_SIGNAL_FUNC (gtk_widget_destroy),
		GTK_OBJECT (HelpWindow));

	return HelpWindow;
}

void Help(GtkWidget *widget, gpointer data)
{
    GtkWidget *window;

    window = create_HelpWindow("AutoZen is a program to alter your brain waves through sound in order to help you to reach an altered state of consciousness. Full details are available at http://www.linuxlabs.com/autozen.html");

	gtk_widget_show(window);
}

GtkWidget *FreqTime;

GtkWidget *CreateColorBox()
{
	    ColorBox = gtk_button_new();
	
		gtk_widget_set_usize( GTK_WIDGET(ColorBox), ColorBoxX,ColorBoxY);
	
		ColorBox_default_style = gtk_widget_get_style( GTK_WIDGET(ColorBox));
	
		ColorBox_new_style = gtk_style_copy(ColorBox_default_style);
		ColorBox_new_style->bg[GTK_STATE_NORMAL] = ColorBox_new_color;
		ColorBox_new_style->fg[GTK_STATE_NORMAL] = ColorBox_new_color;
	
		gtk_widget_set_style(GTK_WIDGET(ColorBox), ColorBox_new_style);
	
		gtk_widget_set_sensitive(GTK_WIDGET(ColorBox), FALSE);
	
	    gtk_widget_show (ColorBox);

		return(ColorBox);
}


GtkWidget *CreateAutoZen(GtkWidget *window)
{
    /* GtkWidget is the storage type for widgets */
	GtkStyle  *wstyle;

	GtkWidget *main_table;

    GtkWidget *button;
	GtkWidget *beat_label;

	GtkWidget *base_label;

	GtkWidget *scale_vol;
	GtkWidget *vol_label;

	GtkWidget *lable;
	GtkWidget *ClockLable;
	GtkWidget *FreqLable;
	GtkWidget *ColorBoxPhaseLable;

	GtkWidget *RecordBox;

	GtkWidget *RecordButton;
	GtkWidget *RecPixWid;
	GdkPixmap *RecordPix;
	GdkBitmap *RecordBit;

	GtkWidget *StopButton;
	GtkWidget *StopPixWid;
	GdkPixmap *StopPix;
	GdkBitmap *StopBit;

	GtkWidget *PlayButton;

	GtkWidget *LiLaPixWid;
	GdkPixmap *LiLaPix;
	GdkBitmap *LiLaBit;

	GtkWidget *LiLaEvent;


	wstyle = gtk_widget_get_style(window);

//	the control layout table
	main_table = gtk_table_new(9,6,FALSE);

	gtk_table_set_col_spacing(GTK_TABLE(main_table), 0, 5);
	gtk_table_set_col_spacing(GTK_TABLE(main_table), 1, 5);
//	gtk_table_set_col_spacing(GTK_TABLE(main_table), 6, 5);

//	app name
//	lable = gtk_label_new("Autozen 1.1");
//	gtk_table_attach_defaults(GTK_TABLE(main_table),lable,1,6,0,1);
//	gtk_widget_show (lable);
		
	// LiLa Logo
	LiLaPix = gdk_pixmap_create_from_xpm_d( window->window,  &LiLaBit, &(wstyle->bg[GTK_STATE_NORMAL]), (gchar **) &lila );

	LiLaPixWid = gtk_pixmap_new( LiLaPix, LiLaBit );

	gtk_widget_show( LiLaPixWid );


	LiLaEvent = gtk_button_new();
	gtk_button_set_relief( GTK_BUTTON(LiLaEvent), GTK_RELIEF_NONE);


	gtk_widget_show( LiLaEvent );

	gtk_container_add( GTK_CONTAINER(LiLaEvent), LiLaPixWid );
	
	gtk_table_attach_defaults(GTK_TABLE(main_table), LiLaEvent,0,7,0,1);

    gtk_signal_connect (GTK_OBJECT (LiLaEvent), "clicked",
                        GTK_SIGNAL_FUNC (Help), NULL);

//	clock 
	ClockTime = gtk_label_new("00:00:00");
	gtk_table_attach_defaults(GTK_TABLE(main_table),ClockTime,2,4,1,2);
    gtk_widget_show (ClockTime);

	ClockLable = gtk_label_new("Time");
	gtk_table_attach_defaults(GTK_TABLE(main_table),ClockLable,2,4,2,3);
    gtk_widget_show (ClockLable);

//	freqTime 
	FreqTime = gtk_label_new("00:00:00");
	gtk_table_attach_defaults(GTK_TABLE(main_table),FreqTime,2,4,3,4);
    gtk_widget_show (FreqTime);

	FreqLable = gtk_label_new("Time in Freq");
	gtk_table_attach_defaults(GTK_TABLE(main_table),FreqLable,2,4,4,5);
    gtk_widget_show (FreqLable);


////////////////
//
//	The recorder controls
//
///////////////

	RecordBox = gtk_hbox_new(FALSE,0);
	wstyle = gtk_widget_get_style( window );

	RecordPix = gdk_pixmap_create_from_xpm_d( window->window,  &RecordBit, &wstyle->bg[GTK_STATE_NORMAL], (gchar **) &xpm_record );

	RecPixWid = gtk_pixmap_new( RecordPix, RecordBit );
	gtk_widget_show( RecPixWid );
	// record button
	RecordButton = gtk_button_new();
	gtk_container_add( GTK_CONTAINER(RecordButton), RecPixWid );

    gtk_widget_show (RecordButton);
	gtk_box_pack_start(GTK_BOX(RecordBox),RecordButton,FALSE,FALSE,0);

// stop button
	StopPix = gdk_pixmap_create_from_xpm_d( window->window,  &StopBit, &wstyle->bg[GTK_STATE_NORMAL], (gchar **) &xpm_stop );

	StopPixWid = gtk_pixmap_new( StopPix, StopBit );
	gtk_widget_show( StopPixWid );
	StopButton = gtk_button_new();
    gtk_signal_connect (GTK_OBJECT (StopButton), "clicked",
                        GTK_SIGNAL_FUNC (StopSequencer), NULL);

	gtk_container_add( GTK_CONTAINER(StopButton), StopPixWid );
    gtk_widget_show (StopButton);

	gtk_box_pack_start(GTK_BOX(RecordBox),StopButton,FALSE,FALSE,0);

// play button
	PlayPix = gdk_pixmap_create_from_xpm_d( window->window,  &PlayBit, &wstyle->bg[GTK_STATE_NORMAL], (gchar **) &xpm_play );

	PlayPixWid = gtk_pixmap_new( PlayPix, PlayBit );
	gtk_widget_show( PlayPixWid );
	PlayButton = gtk_button_new();

    gtk_signal_connect (GTK_OBJECT (PlayButton), "clicked",
                        GTK_SIGNAL_FUNC (Play), NULL);

	gtk_container_add( GTK_CONTAINER(PlayButton), PlayPixWid );
    gtk_widget_show (PlayButton);
	gtk_box_pack_start(GTK_BOX(RecordBox),PlayButton,FALSE,FALSE,0);

	gtk_table_attach_defaults(GTK_TABLE(main_table),RecordBox,2,4,5,6);
    gtk_widget_show (RecordBox);

// Pause widget for play button
	PausePix = gdk_pixmap_create_from_xpm_d( window->window,  &PauseBit, &wstyle->bg[GTK_STATE_NORMAL], (gchar **) &xpm_pause );
	PausedPix = gdk_pixmap_create_from_xpm_d( window->window,  &PausedBit, &wstyle->bg[GTK_STATE_NORMAL], (gchar **) &xpm_paused );


//	the Quit button 

    button = gtk_button_new_with_label ("Quit");

    gtk_signal_connect (GTK_OBJECT (button), "clicked",
                        GTK_SIGNAL_FUNC (Quit), NULL);
    
	gtk_table_attach_defaults(GTK_TABLE(main_table),button,2,4,6,7);
    
    gtk_widget_show (button);

//	set up beat slider

	BeatFreq  = gtk_label_new("10.0");
	gtk_table_attach_defaults(GTK_TABLE(main_table),BeatFreq,0,1,1,2);
    gtk_widget_show (BeatFreq);

	adj_beat = gtk_adjustment_new(BEAT_MAX - 10,0,BEAT_MAX,0.1,0.1,0);
    gtk_signal_connect (GTK_OBJECT (adj_beat), "value_changed",
                        GTK_SIGNAL_FUNC (value_change), (gpointer) &detune);

    gtk_signal_connect (GTK_OBJECT (adj_beat), "value_changed",
                        GTK_SIGNAL_FUNC (label_change_value), BeatFreq);

	scale_beat = gtk_vscale_new(GTK_ADJUSTMENT(adj_beat));
	gtk_scale_set_draw_value( GTK_SCALE(scale_beat), FALSE);
	gtk_table_attach_defaults(GTK_TABLE(main_table),scale_beat,0,1,2,6);

    gtk_widget_show (scale_beat);
	beat_label = gtk_label_new("Beat");
	gtk_table_attach_defaults(GTK_TABLE(main_table),beat_label,0,1,6,7);
    gtk_widget_show (beat_label);

//	set up base slider
	BaseFreq = gtk_label_new("300.0");
	gtk_table_attach_defaults(GTK_TABLE(main_table),BaseFreq,1,2,1,2);
    gtk_widget_show (BaseFreq);

	adj_base = gtk_adjustment_new(700,50,1000,1,10,0);
    gtk_signal_connect (GTK_OBJECT (adj_base), "value_changed",
                        GTK_SIGNAL_FUNC (value_change), (gpointer) &increment);

    gtk_signal_connect (GTK_OBJECT (adj_base), "value_changed",
                        GTK_SIGNAL_FUNC (label_change_value), BaseFreq);

	scale_base = gtk_vscale_new(GTK_ADJUSTMENT(adj_base));
	gtk_scale_set_draw_value( GTK_SCALE(scale_base), FALSE);
	gtk_table_attach_defaults(GTK_TABLE(main_table),scale_base,1,2,2,6);
    gtk_widget_show (scale_base);
	base_label = gtk_label_new("Base");
	gtk_table_attach_defaults(GTK_TABLE(main_table),base_label,1,2,6,7);
    gtk_widget_show (base_label);

//	set up volume slider
	VolLabel = gtk_label_new("50.0");
	gtk_table_attach_defaults(GTK_TABLE(main_table),VolLabel,5,6,1,2);
    gtk_widget_show (VolLabel);
	adj_vol = gtk_adjustment_new(100-volume,0,100,1,10,0);
    gtk_signal_connect (GTK_OBJECT (adj_vol), "value_changed",
                        GTK_SIGNAL_FUNC (value_change), (gpointer) &volume);

    gtk_signal_connect (GTK_OBJECT (adj_vol), "value_changed",
                        GTK_SIGNAL_FUNC (label_change_value), VolLabel);

	scale_vol = gtk_vscale_new(GTK_ADJUSTMENT(adj_vol));
	gtk_scale_set_draw_value( GTK_SCALE(scale_vol), FALSE);
	gtk_table_attach_defaults(GTK_TABLE(main_table),scale_vol,5,6,2,6);
//    gtk_container_add (GTK_CONTAINER (window), scale_vol);
    gtk_widget_show (scale_vol);
	vol_label = gtk_label_new("Vol");
	gtk_table_attach_defaults(GTK_TABLE(main_table),vol_label,5,6,6,7);
    gtk_widget_show (vol_label);

    /* and the window */

//	the ColorBox button 
	if(ColorBoxX && ColorBoxY) {
//		PhaseLabel = gtk_label_new("50.0");
//		gtk_table_attach_defaults(GTK_TABLE(main_table),VolLabel,5,6,1,2);
//	    gtk_widget_show (VolLabel);
		adj_colorboxphase = gtk_adjustment_new(0,-0.5,0.5,0.01,0.1,0);
	    gtk_signal_connect (GTK_OBJECT (adj_colorboxphase), "value_changed",
	                        GTK_SIGNAL_FUNC (value_change_no_invert), (gpointer) &phase);
	
//	    gtk_signal_connect (GTK_OBJECT (adj_vol), "value_changed",
//	                        GTK_SIGNAL_FUNC (label_change_value), VolLabel);
	
		scale_colorboxphase = gtk_hscale_new(GTK_ADJUSTMENT(adj_colorboxphase));
//		gtk_scale_set_draw_value( GTK_SCALE(scale_colorboxphase), FALSE);
		gtk_scale_set_digits( GTK_SCALE(scale_colorboxphase), 2);
		gtk_table_attach_defaults(GTK_TABLE(main_table),scale_colorboxphase,0,6,7,8);
	    gtk_widget_show (scale_colorboxphase);
//		vol_label = gtk_label_new("Vol");
//		gtk_table_attach_defaults(GTK_TABLE(main_table),vol_label,5,6,6,7);
//	    gtk_widget_show (vol_label);
	}


    gtk_widget_show (main_table);

	return(main_table);
}
     
void SetupSequenceDirs(void)
{
	struct stat st;
	int res;
	char fname[500];

	sprintf(fname,"%s/.autozen",getenv("HOME"));

	res = stat(fname,&st);
	if(res<0) {
		mkdir(fname,0700);
		strcat(fname,"/public.seq");
		symlink(PUBLIC_SEQUENCES, fname);
	}
}
	
	
int main (int argc, char *argv[])
{
    GtkWidget *window;
    GtkWidget *CBwindow;
	pthread_t stp;
	int	next_arg=1;

    gtk_init (&argc, &argv);


	while(next_arg < argc && argv[next_arg][0] == '-') {

		if(!strcasecmp(argv[next_arg], "-colorbox")) {
			next_arg++;
			if(argc>next_arg && atoi(argv[next_arg])) {
				ColorBoxX = atoi(argv[next_arg++]);
				ColorBoxY = atoi(argv[next_arg++]);
			} else {
				ColorBoxX = COLORBOX_X;
				ColorBoxY = COLORBOX_Y;
			}
		} else if(!strcasecmp(argv[next_arg], "-harmonics")) {
			next_arg++;
			if(argc>next_arg && atoi(argv[next_arg]))
				nHarmonics = atoi(argv[next_arg++]);
				if(nHarmonics > MAX_HARMONICS)
					nHarmonics = MAX_HARMONICS;
		} else if(!strcasecmp(argv[next_arg], "-help") || !strcasecmp(argv[next_arg], "-h")) {
			printf("%s [-colorbox [<x> <y>]] [-harmonics <n>] [<sequence>]\n",argv[0]);
			exit(0);
		}
		
			
	}

	SetupSequenceDirs();

    window = gtk_window_new (GTK_WINDOW_TOPLEVEL);

	gtk_window_set_wmclass( GTK_WINDOW(window), "autozen", "AutoZen");

    gtk_signal_connect (GTK_OBJECT (window), "delete_event",
                        GTK_SIGNAL_FUNC (delete_event), NULL);

    gtk_signal_connect (GTK_OBJECT (window), "destroy",
                        GTK_SIGNAL_FUNC (destroy), NULL);

    /* sets the border width of the window. */
    gtk_container_border_width (GTK_CONTAINER (window), 10);

	gtk_window_set_title( GTK_WINDOW(window), "AutoZen 2.1");

	gtk_window_set_position( GTK_WINDOW(window) ,GTK_WIN_POS_CENTER);

    gtk_widget_show (window);

    gtk_container_add (GTK_CONTAINER (window), CreateAutoZen(window));

	if(ColorBoxX && ColorBoxY) {
    	CBwindow = gtk_window_new (GTK_WINDOW_TOPLEVEL);
    	gtk_signal_connect (GTK_OBJECT (CBwindow), "destroy",
                        GTK_SIGNAL_FUNC (ColorBoxDestroy), NULL);

    	gtk_widget_show (CBwindow);
    	gtk_container_add (GTK_CONTAINER (CBwindow), CreateColorBox());
	}

	///////////////////////////////////////////
	//
	//	GUI setup done, now check args for a sequence to play
	//
	//////////////////////////////////////////

	pthread_create(&stp,NULL,SoundThread,NULL);
	gtk_timeout_add(1000,TimeOut,FreqTime);
	volTOhandle = gtk_timeout_add(10,volTimeOut,FreqTime);

	if(ColorBoxX && ColorBoxY)
		ColorBoxTOhandle = gtk_timeout_add(10,ColorBoxTimeOut,FreqTime);

	if(argc >next_arg) {
		InitSequencer(argv[next_arg]);
		CheckSequencer();
	}

	///////////////////	Main Loop ///////////////
    gtk_main ();

    return 0;
}

