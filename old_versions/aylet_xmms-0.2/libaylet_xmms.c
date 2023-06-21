/*

    libaylet_xmms.c

    Copyright (c) 2005 Cesar Hernandez <chernandezba@hotmail.com>

    This file is part of aylet_xmms

    aylet_xmms is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program; if not, write to the Free Software
    Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA


*/


#include <stdio.h>
#include <xmms/plugin.h>

#include <string.h>

#include <sys/types.h>
#include <sys/wait.h>
#include <sys/stat.h>
#include <unistd.h>
#include <signal.h>
#include <errno.h>
#include <stdlib.h>

#include <gtk/gtk.h>
#include <time.h>

#include <pthread.h>


//#define DEBUG

#define VERSION "0.2"
#define FECHA "27/02/2005"
#define TEMPFILE "/tmp/aylet_xmms.pcm"
#define TAMANYO_BUFFER 5000*2*2 /*44100*2*2 */

InputPlugin plugin_aylet;



int pausado;
int leidos; //bytes leidos del archivo temporal
int detener; //orden de parar la reproduccion
char *title;
struct stat estado_archivo;
FILE *file;
void *buf,*buf_aylet;
time_t tiempo_inicial;
int salida_aylet;
int tuberias[2];
pid_t pid_hijo=0;
int reproduciendo=0;
int notificado_final=1;
char *archivo;
pthread_t decode_thread,read_thread;
static GtkWidget *about_window = NULL;
char about_window_text[] = "aylet_XMMS Input Plugin "VERSION"\nby Cesar Hernandez ("FECHA")\n"
                           "<chernandezba@hotmail.com>\n\n"
													 "Extensiones soportadas:\n\n";

char *extensiones[]={
	".ay",
	NULL
};

void pon_info_cancion(void)
{

	struct stat estado_archivo2;
	if (!stat(TEMPFILE,&estado_archivo2)) {
			plugin_aylet.set_info(title, (estado_archivo.st_size*10)/441/2 ,
			44100*16, 44100,2);
	}
}


void parar(void);

void borra_archivo_temporal(void)
{

	file=fopen(TEMPFILE,"w");
	fclose (file);

}

void crea_archivo_temporal(void)
{

	file=fopen(TEMPFILE,"w");

}



void tratamiento_senyales (int s) {

	int status;
	pid_t pid;




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

					pon_info_cancion();

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

	buffer_cero=0;

	/*
	Aqui debemos intentar llenar el buffer de lectura. Pueden pasar varias cosas:
	-Que no haya audio
	-Que se haya llegado al final del archivo
	-Que aylet todavia no haya escrito
	*/

	do {
		if (stat(TEMPFILE,&estado_archivo)) return 0;
		if (estado_archivo.st_size<=TAMANYO_BUFFER+leidos) {
#ifdef DEBUG
			printf("libaylet_xmms: Esperando a llenar el buffer\n");
#endif
			usleep(100000);
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
	} while (pid_hijo); //si aylet ha acabado, es inutil esperar que llene el buffer!

	count=fread(buf,1,TAMANYO_BUFFER,file);
	if (count<TAMANYO_BUFFER) {
		//Rellenar el resto
		p=&buf[count];
		for (i=TAMANYO_BUFFER-count;i;i--,p++) *p=0;
	}
	if (count<TAMANYO_BUFFER && feof(file)) {
#ifdef DEBUG
		printf("libaylet_xmms: Final del archivo\n");
#endif
		detener=1;

		//plugin_aylet.set_info(title, (leidos/441/4) * 10 ,44100*32, 44100,2);

	}
	leidos +=count;
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
	int count;

	file=fopen(TEMPFILE,"r");

	if (file==NULL) {
		fprintf (stderr,"\nlibaylet_xmms: Error al abrir archivo temporal\n");
	}
	else {



	#ifdef DEBUG
		printf("libaylet_xmms: Abierto archivo, reproduciendo\n");

	#endif

		do {

		//Leer un trozo del archivo
			count=leer_archivo();

			if (count) {
#ifdef DEBUG
				printf ("antes write_audio\n");
#endif
				plugin_aylet.output->write_audio(buf, TAMANYO_BUFFER);
#ifdef DEBUG
				printf ("despues write_audio\n");
#endif				
				
				while(plugin_aylet.output->buffer_free()<TAMANYO_BUFFER) {
						  
					usleep((TAMANYO_BUFFER/44/2/32)*1000); //Esperar una 32ª parte del tiempo
#ifdef DEBUG
					printf ("usleep buffer_playing\n");
#endif					
				}
			}

		}	while (!detener);
		fclose(file);
	#ifdef DEBUG
		printf ("\nlibaylet_xmms: Cerrado archivo temporal\n");
	#endif



		while (pid_hijo>0);
	#ifdef DEBUG
		printf ("\nlibaylet_xmms: pthread_exit\n");
	#endif
	}
	parar();
	pthread_exit(NULL);

}

void *leer_aylet(void *arg)
{
	//Se va leyendo la salida de aylet y se envia a un archivo temporal

	int count;
	int buffer_cero;

	FILE *f_write;

	f_write=fopen(TEMPFILE,"w");


	if (f_write==NULL) {
		fprintf (stderr,"\nlibaylet_xmms: Error al crear archivo temporal\n");
	}
	else {

		buffer_cero=0;

#ifdef DEBUG
		fprintf (stderr,"\nlibaylet_xmms: Creado archivo temporal\n");
#endif


		do {

			count=read(salida_aylet,buf_aylet,TAMANYO_BUFFER);

			if (count) {
				fwrite(buf_aylet,1,count,f_write);
				buffer_cero=0;
			}
			else {
				buffer_cero++;
				usleep(100000);
			}
#ifdef DEBUG
//			fprintf (stderr,"\nlibaylet_xmms: leer_aylet: leidos %d bytes\n",count);
#endif

			usleep(1000); //100000

		} while (!feof(f_write) && buffer_cero!=10); //10 reintentos con buffer cero (1 segundo)

#ifdef DEBUG
		fprintf (stderr,"\nlibaylet_xmms: Fin pthread leer_aylet\n");
#endif
	}

	pon_info_cancion();
	pid_hijo=0;



	pthread_exit(NULL);
}


void reproducir(char *filename)
{

	int i;

	pausado=0;

	if(plugin_aylet.output->open_audio(FMT_U8,44100,2)==0)
	{
		return;
	}

	detener=0;

	if ((buf=malloc(TAMANYO_BUFFER))==NULL) return;
	if ((buf_aylet=malloc(TAMANYO_BUFFER))==NULL) return;

	borra_archivo_temporal();

	for (i=strlen(filename);i>=0;i--) {
		if (filename[i]=='/') break;
	}

	title=g_strdup(&filename[i+1]);

	plugin_aylet.set_info(title, -1/*5000*/ ,44100*16, 44100,2);


	time(&tiempo_inicial);
	leidos=0;


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

	crea_archivo_temporal();

	if (pid_hijo) {

		salida_aylet=dup(tuberias[0]);
		close(tuberias[0]);
		close(tuberias[1]);

		reproduciendo=1;
		notificado_final=0;

		pthread_create(&read_thread, NULL, leer_aylet, NULL);

		//Esperar un tiempo razonable, a que aylet llene el archivo
		usleep(1000000);
#ifdef DEBUG
		printf ("despues usleep inicial\n");
#endif		
		pthread_create(&decode_thread, NULL, play_loop, NULL);

		return;
	}
	else {

	//Se crean dos pthreads
	//uno, lee la salida de aylet y la envia a un archivo temporal
	//la otra, lee del archivo temporal y lo envia a xmms


		close(1);
		dup(tuberias[1]);
		close(tuberias[1]);
		close(tuberias[0]);


		execl("/usr/bin/aylet","/usr/bin/aylet",
		"-s",filename,NULL);


		perror ("libaylet_xmms: Error al ejecutar aylet : ");
	}

}


void pausa(short paused)
{
	//if (pid_hijo>0) write (salida_aylet,"pause\n",strlen("pause\n"));
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

}

void posicionar_lectura(int segundos)
{

	leidos=segundos*44100*2;
	fseek(file,leidos,SEEK_SET);
}

void busqueda (int time)
{
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
	pausa, //void (*pause) (short paused);//* Pause or unpause
	busqueda, //void (*seek) (int time);	//* Seek to the specified time
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
	info_cancion, //void (*get_song_info) (char *filename, char **title, int *length);	//* Function to grab the title string
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
		printf ("libaylet_xmms: parar()\n");
#endif

	if (pid_hijo>0) {
#ifdef DEBUG
		printf ("libaylet_xmms: Cerrando aylet pid=%d \n",pid_hijo);
#endif
		v=kill(pid_hijo,SIGTERM); //SIGINT
		pid_hijo=0;
		if (v) perror("libaylet_xmms: Error al parar aylet : ");
	}



	if (reproduciendo) {
#ifdef DEBUG
		printf ("libaylet_xmms: parar reproduccion\n");
#endif
		if (pausado) pausa(0);
		detener=1;
		reproduciendo=0;
		pthread_join(decode_thread,NULL);
		pthread_join(read_thread,NULL);
#ifdef DEBUG
		printf ("libaylet_xmms: retornado de pthread_join\n");
#endif
		plugin_aylet.output->close_audio();
		free(buf);

		borra_archivo_temporal();

	}



}
