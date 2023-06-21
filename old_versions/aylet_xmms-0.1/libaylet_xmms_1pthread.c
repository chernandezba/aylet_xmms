#include <stdio.h>
#include <xmms/plugin.h>

#include <string.h>

#include <sys/types.h>
#include <sys/wait.h>
#include <sys/stat.h>
#include <unistd.h>
#include <signal.h>
#include <errno.h>

#include <gtk/gtk.h>
#include <time.h>

//#include <gthread.h>


#define DEBUG

#define VERSION "0.1"
#define FECHA "31/03/2003"
#define TEMPFILE "/tmp/aylet_xmms.pcm"
//#define TAMANYO_BUFFER 22050*2*2 /*44100*2*2 */
#define TAMANYO_BUFFER 40000 /*44100*2*2 */ //hay un minimo en el buffer para q funcione xmms

InputPlugin plugin_aylet;


//

int pausado;
//int leidos; //bytes leidos del archivo temporal
int detener; //orden de parar la reproduccion
char *title;
struct stat estado_archivo;
FILE *file;
void *buf;
time_t tiempo_inicial;
//int entrada_aylet;
int salida_aylet;
int tuberias[2];
pid_t pid_hijo=0;
int reproduciendo=0;
int notificado_final=1;
char *archivo;
pthread_t decode_thread;
static GtkWidget *about_window = NULL;
char about_window_text[] = "Aylet_XMMS Input Plugin "VERSION"\nby Cesar Hernandez ("FECHA")\n"
                           "<chernandezba@hotmail.com>\n\n"
									 "Extensiones soportadas:\n\n";

char *extensiones[]={
	".ay",
	//".mp3",".ogg",".wav",
	NULL
};


void parar(void);

void borra_archivo_temporal(void)
{

	file=fopen(TEMPFILE,"w");
	fclose (file);

}


void tratamiento_senyales (int s) {

	int status;
	pid_t pid;

	struct stat estado_archivo2;


	switch (s) {
		case SIGCHLD:
#ifdef DEBUG
			fprintf (stderr,"libaylet_xmms: Recibida senyal SIGCHLD\n");
#endif
			pid=waitpid(-1,&status,WNOHANG);
#ifdef DEBUG
			fprintf (stderr,"libaylet_xmms: waitpid : pid=%d\n",pid);
#endif
			if (pid>0) {
				if (pid==pid_hijo) {
#ifdef DEBUG
					fprintf (stderr,"libaylet_xmms: aylet finalizado\n");
#endif
					pid_hijo=0;

					if (!stat(TEMPFILE,&estado_archivo2)) {
/*						plugin_aylet.set_info(title, (estado_archivo.st_size*10)/441/4 ,
														44100*32, 44100,2);*/
						plugin_aylet.set_info(title, (estado_archivo.st_size*10)/441/2 ,
														44100*16, 44100,2);

					}
					//parar();
				}
			}
		break;
		default:
		break;
	}
}



void registrar_senyales(void)
{
	if (signal(SIGCHLD,tratamiento_senyales)==SIG_ERR) {
	  printf ("libaylet_xmms: Error al registrar senyal SIGCHLD\n");
	}
}

int detecta(char *filename)
{

	char *ext;
	int i;
	char *e;

#ifdef DEBUG
	//printf ("libaylet_xmms: detecta : %s\n",filename);
#endif
	ext=strrchr(filename,'.');
	if(ext)	{
		for (i=0;;i++) {
			e=extensiones[i];
#ifdef DEBUG
			//printf ("libaylet_xmms: %s\n",e);
#endif
			if (e==NULL) return 0;
			if(!strcasecmp(ext,e))	{
#ifdef DEBUG
				//printf ("libaylet_xmms: OK : %s\n",filename);
#endif
				return 1;
			}
		}
	}
	return 0;

}

int leer_archivo(void)
{
	int count,i;
	int buffer_cero;
	char *p;
	
	int leidos;

	buffer_cero=0;

	/*
	Aqui debemos intentar llenar el buffer de lectura. Pueden pasar varias cosas:
	-Que no haya audio
	-Que se haya llegado al final del archivo
	-Que aylet todavia no haya escrito
	*/

	/*do {
		if (stat(TEMPFILE,&estado_archivo)) return 0;
		if (estado_archivo.st_size<=TAMANYO_BUFFER+leidos) {
#ifdef DEBUG
			printf("libaylet_xmms: Esperando a llenar el buffer\n");
#endif
			xmms_usleep(100000);
		}
		else break;
		buffer_cero++;
		if (buffer_cero==20) {
#ifdef DEBUG
			printf("libaylet_xmms: 20 reintentos esperando a llenar el buffer. No audio?\n");
#endif
			break;
			//detener=1;
			//return 0; //10 reintentos
		}
	} while (pid_hijo); //si aylet ha acabado, es inutil esperar que llene el buffer!*/

	//count=fread(buf,1,TAMANYO_BUFFER,salida_aylet /*file */);
	//sleep(2);
	//xmms_usleep(100000);
	leidos=0;

	do {
		xmms_usleep(100000);
		count=read(salida_aylet,buf+leidos,TAMANYO_BUFFER-leidos /*file */);
	/*if (count<TAMANYO_BUFFER) {
		//Rellenar el resto
		p=&buf[count];
		for (i=TAMANYO_BUFFER-count;i;i--,p++) *p=0;
	}*/

#ifdef DEBUG
		fprintf(stderr,"libaylet_xmms: Leidos %d bytes\n",count);
#endif


		if (count==0) {
			buffer_cero++;
		}
		else {
			leidos +=count;
			buffer_cero=0;
		}

	} while (leidos<TAMANYO_BUFFER && buffer_cero<20);

	if (buffer_cero==20) {
#ifdef DEBUG
		fprintf(stderr,"libaylet_xmms: 20 reintentos esperando a llenar el buffer. No audio?\n");
#endif
	}

	if (leidos<TAMANYO_BUFFER /*&& feof(file)*/ ) {

		//Rellenar el resto
		p=&buf[count+leidos];
		for (i=TAMANYO_BUFFER-count;i;i--,p++) *p=0;

#ifdef DEBUG
		fprintf(stderr,"libaylet_xmms: Final del archivo count=%d TAMANYO_BUFFER=%d\n",
		count,TAMANYO_BUFFER);
#endif
		detener=1;

		//plugin_aylet.set_info(title, (leidos/441/4) * 10 ,44100*32, 44100,2);

	}
	//leidos +=count;
#ifdef DEBUG
	printf("libaylet_xmms: Leidos %d bytes\n",count);
#endif
	return count;

}


void salir(void)
{
	parar();
}

void *play_loop(void *arg)
{
	//pid_t pid;
	//int status;
	int count;
	int visualizados;
	int kk=0;

	// file=fopen(TEMPFILE,"r"); de mplayer

#ifdef DEBUG
	printf("libaylet_xmms: Abierto archivo, reproduciendo\n");

#endif

	xmms_usleep(1000000);

	do {

	//Leer un trozo del archivo
		//xmms_usleep(1000000);
		count=leer_archivo();

		if (count) {
			//plugin_aylet.output->write_audio(buf, count);
			plugin_aylet.output->write_audio(buf, TAMANYO_BUFFER);
			/*#define INC TAMANYO_BUFFER
			for (visualizados=0;visualizados<count;visualizados+=INC)
			plugin_aylet.add_vis_pcm(kk+=INC,FMT_S16_LE,2,INC,buf+visualizados);
		//	plugin_aylet.add_vis_pcm(plugin_aylet.output->written_time(),FMT_S16_LE,
              //                       2,count,buf);
			*/
			//while(plugin_aylet.output->buffer_playing()) {
				//usleep((TAMANYO_BUFFER/22)*1000); //Esperar una 32ª parte del tiempo

				/*plugin_aylet.add_vis_pcm(plugin_aylet.output->written_time(),FMT_S16_LE,
                                     2,count,buf+kk);
												 kk+=512*4;*/
#ifdef DEBUG
				//fprintf(stderr,"libaylet_xmms: bucle buffer_playing()\n");
#endif

			//}
		}

	}	while (!detener);
	// fclose(file);
#ifdef DEBUG
	printf ("\nlibaylet_xmms: Cerrado archivo temporal\n");
#endif


	//xmms_usleep (10000);
	while (pid_hijo>0);
#ifdef DEBUG
	printf ("\nlibaylet_xmms: pthread_exit\n");
#endif
	parar();
	pthread_exit(NULL);

}

void reproducir(char *filename)
{

	int i;

	pausado=0;

	//if(plugin_aylet.output->open_audio(FMT_S16_LE,44100,2)==0)
	if(plugin_aylet.output->open_audio(FMT_U8,44100,2)==0)
	{
		//free(wav_file);
		//wav_file=NULL;
		return;
	}

	detener=0;

	if ((buf=malloc(TAMANYO_BUFFER))==NULL) return;

	borra_archivo_temporal();

	for (i=strlen(filename);i>=0;i--) {
		if (filename[i]=='/') break;
	}

	title=g_strdup(&filename[i+1]);

	//plugin_aylet.set_info(title, -1/*5000*/ ,44100*32, 44100,2);
	plugin_aylet.set_info(title, -1/*5000*/ ,44100*16, 44100,2);


	time(&tiempo_inicial);
	//leidos=0;


#ifdef DEBUG
	printf ("libaylet_xmms: Ejecutando aylet archivo : %s\n",filename);
#endif
	if (pipe(tuberias)<0) {
		printf ("libaylet_xmms: Error al hacer pipe\n");
		return;
	}

	pid_hijo=fork();
	if (pid_hijo==-1) {
		printf ("libaylet_xmms: Error al hacer fork\n");
		return;
	}
	if (pid_hijo) {
		/*entrada_aylet=dup(tuberias[1]);
		close(tuberias[0]);
		close(tuberias[1]);*/

		salida_aylet=dup(tuberias[0]);
		close(tuberias[0]);
		close(tuberias[1]);


		reproduciendo=1;
		notificado_final=0;

		//xmms_usleep(1000000); //Esperar un tiempo razonable, a que aylet llene el archivo
		pthread_create(&decode_thread, NULL, play_loop, NULL);

		return;
	}
	else {
		close(1);
		/*dup(tuberias[0]);
		close(tuberias[1]);
		close(tuberias[0]);*/

		dup(tuberias[1]);
		close(tuberias[1]);
		close(tuberias[0]);

//
close(0);
//

		execl("/usr/bin/aylet","/usr/bin/aylet",
		"-s",filename,NULL);


	//aylet archivo -ao pcm -nowaveheader -aofile /tmp/aylet_xmms.pcm
		perror ("libaylet_xmms: Error al ejecutar aylet : ");
	}

}


void pausa(short paused)
{
	//if (pid_hijo>0) write (entrada_aylet,"pause\n",strlen("pause\n"));
	plugin_aylet.output->pause(paused);
#ifdef DEBUG
	printf ("libaylet_xmms: pausa= %d\n",paused);
#endif
	pausado=paused;

}

int da_tiempo (void)
{

  if(reproduciendo)
		return plugin_aylet.output->output_time();

 return -1;

/*
	time_t tiempo_ahora;

	if (!reproduciendo && !notificado_final) {
		notificado_final=1;
		return -1;
	}

	time(&tiempo_ahora);

	return difftime(tiempo_ahora,tiempo_inicial);
	//return 600;*/
}

void posicionar_lectura(int segundos)
{

	//leidos=segundos*44100*4;
	//leidos=segundos*44100*2;
	//fseek(file,leidos,SEEK_SET);
}

void busqueda (int time)
{
	char buffer[80];
	/*if (pid_hijo>0) {
		sprintf (buffer,"seek %d\n",time);
		write (entrada_aylet,buffer,strlen(buffer));
	}*/
	if (!pid_hijo) {
#ifdef DEBUG
		printf ("libaylet_xmms: seek = %d\n",time); //en segundos
#endif
		posicionar_lectura(time);
		plugin_aylet.output->flush(time*1000);

	}
}

void inicio(void)
{
	registrar_senyales();
}

void about (void)
{

	//Extraido de XMMS meta-input plugin by Mikael Bouillot
	//<mikael.bouillot@bigfoot.com

	GtkWidget *dialog_vbox1;
	GtkWidget *hbox1;
	GtkWidget *label1;
	GtkWidget *dialog_action_area1;
	GtkWidget *about_exit;
	char *p = NULL, *q;
	int i,e;

	if (!about_window)
	{
		about_window = gtk_dialog_new();
		gtk_object_set_data(GTK_OBJECT(about_window), "about_window", about_window);
		gtk_window_set_title(GTK_WINDOW(about_window), "Acerca libaylet_xmms "VERSION);
		gtk_window_set_policy(GTK_WINDOW(about_window), FALSE, FALSE, FALSE);
		gtk_window_set_position(GTK_WINDOW(about_window), GTK_WIN_POS_MOUSE);
		gtk_signal_connect(GTK_OBJECT(about_window), "destroy", GTK_SIGNAL_FUNC(gtk_widget_destroyed), &about_window);
		gtk_container_border_width(GTK_CONTAINER(about_window), 10);

		dialog_vbox1 = GTK_DIALOG(about_window)->vbox;
		gtk_object_set_data(GTK_OBJECT(about_window), "dialog_vbox1", dialog_vbox1);
		gtk_widget_show(dialog_vbox1);
		gtk_container_border_width(GTK_CONTAINER(dialog_vbox1), 5);

		hbox1 = gtk_hbox_new(FALSE, 0);
		gtk_object_set_data(GTK_OBJECT(about_window), "hbox1", hbox1);
		gtk_widget_show(hbox1);
		gtk_box_pack_start(GTK_BOX(dialog_vbox1), hbox1, TRUE, TRUE, 0);
		gtk_container_border_width(GTK_CONTAINER(hbox1), 5);
		gtk_widget_realize(about_window);

		for (i=0,e=0;extensiones[i]!=NULL; i++)
		{
			if (!p)
			{
				p = strdup(extensiones[i]);
			}
			else {
				q = malloc(strlen(p)+strlen(extensiones[i])+3);
				strcpy(q,p);
				e++;
				if (e==4) {
					e=0;
					if (extensiones[i]!=NULL) strcat(q,",\n");
				}
				else {
					if (extensiones[i]!=NULL)
						strcat(q,", ");
				}
				strcat(q,extensiones[i]);
				p=q;
			}
		}
		q =malloc(strlen(p)+strlen(about_window_text)+1);
		strcpy(q,about_window_text);
		strcat(q,p);
		p = q;
		q=NULL;

		label1 = gtk_label_new(p);
		gtk_object_set_data(GTK_OBJECT(about_window), "label1", label1);
		gtk_widget_show(label1);
		gtk_box_pack_start(GTK_BOX(hbox1), label1, TRUE, TRUE, 0);

		dialog_action_area1 = GTK_DIALOG(about_window)->action_area;
		gtk_object_set_data(GTK_OBJECT(about_window), "dialog_action_area1", dialog_action_area1);
		gtk_widget_show(dialog_action_area1);
		gtk_container_border_width(GTK_CONTAINER(dialog_action_area1), 10);

		about_exit = gtk_button_new_with_label("Ok");
		gtk_signal_connect_object(GTK_OBJECT(about_exit), "clicked",
			GTK_SIGNAL_FUNC(gtk_widget_destroy),
			GTK_OBJECT(about_window));

		gtk_object_set_data(GTK_OBJECT(about_window), "about_exit", about_exit);
		gtk_widget_show(about_exit);
		gtk_box_pack_start(GTK_BOX(dialog_action_area1), about_exit, TRUE, TRUE, 0);



		gtk_widget_show(about_window);
	}
	else
	{
		gdk_window_raise(about_window->window);
	}
}

void info_cancion (char *filename, char **title, int *length)
{
	// Function to grab the title string

	int i;

	*length = -1; //1000;

	for (i=strlen(filename);i>=0;i--) {
		if (filename[i]=='/') break;
	}

	*title=g_strdup(&filename[i+1]);
}


InputPlugin plugin_aylet=
{
	0, //void *handle;
	0, //char *filename;		//* Filled in by xmms
	"aylet_xmms plugin "VERSION,	//* The description that is shown in the preferences box
	inicio,  //void (*init) (void);	//* Called when the plugin is loaded
	about,  //void (*about) (void);	//* Show the about box
	0,  //void (*configure) (void);
	detecta, //int (*is_our_file) (char *filename);	//* Return 1 if the plugin can handle the file
	0, //GList *(*scan_dir) (char *dirname);	//* Look in Input/cdaudio/cdaudio.c to see how
	//* to use this
	reproducir, //void (*play_file) (char *filename);	//* Guess what...
	parar, //void (*stop) (void);	//* Tricky one
	0, //pausa, //void (*pause) (short paused);//* Pause or unpause
	0, //busqueda, //void (*seek) (int time);	//* Seek to the specified time
	0, //void (*set_eq) (int on, float preamp, float *bands);	//* Set the equalizer, most plugins won't be able to do this
	da_tiempo, //int (*get_time) (void);	//* Get the time, usually returns the output plugins output time
	0, //void (*get_volume) (int *l, int *r);	//* Input-plugin specific volume functions, just provide a NULL if
	0, //void (*set_volume) (int l, int r);	//*  you want the output plugin to handle it
	salir, //void (*cleanup) (void);			//* Called when xmms exit
	0, //InputVisType (*get_vis_type) (void); //* OBSOLETE, DO NOT USE!
	0, //void (*add_vis_pcm) (int time, AFormat fmt, int nch, int length, void *ptr); //* Send data to the visualization plugins
											//Preferably 512 samples/block
	0, //void (*set_info) (char *title, int length, int rate, int freq, int nch);	//* Fill in the stuff that is shown in the player window
											 //  set length to -1 if it's unknown. Filled in by xmms
	0, //void (*set_info_text) (char *text);	//* Show some text in the song title box in the main window,
						   //call it with NULL as argument to reset it to the song title.
						   //Filled in by xmms
	0, //info_cancion, //void (*get_song_info) (char *filename, char **title, int *length);	//* Function to grab the title string
	0, //void (*file_info_box) (char *filename);		//* Bring up an info window for the filename passed in
	0 //OutputPlugin *output;	//* Handle to the current output plugin. Filled in by xmms
};


InputPlugin *get_iplugin_info(void)
{
	return &plugin_aylet;
}

void parar(void)
{
	int v;

#ifdef DEBUG
		fprintf (stderr,"libaylet_xmms: parar()\n");
#endif

	if (pid_hijo>0) {
#ifdef DEBUG
		fprintf (stderr,"libaylet_xmms: Cerrando aylet pid=%d \n",pid_hijo);
#endif
		v=kill(pid_hijo,SIGTERM); //SIGINT
		pid_hijo=0;
		if (v) perror("libaylet_xmms: Error al parar aylet : ");
	}



	if (reproduciendo) {
#ifdef DEBUG
		fprintf (stderr,"libaylet_xmms: parar reproduccion\n");
#endif
		if (pausado) pausa(0);
		detener=1;
		reproduciendo=0;
		pthread_join(decode_thread,NULL);
#ifdef DEBUG
		fprintf (stderr,"libaylet_xmms: retornado de pthread_join\n");
#endif
		plugin_aylet.output->close_audio();
		free(buf);

		borra_archivo_temporal();

	}



}
