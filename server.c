#define _GNU_SOURCE

#include <unistd.h>
#include <syslog.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <fcntl.h>
#include <time.h>
#include <errno.h>
#include <assert.h>
#include <string.h>
#include <signal.h>

#include "confuse.h"
#include "demonizar.h"
#include "socket.h"
#include "picohttpparser.h"

// Definicion de macros
#define MAXBUF 4096
#define MAX_RESPONSE 8192
#define MAXTAM 256

#define BAD_REQUEST 400
#define OK 200
#define NOT_FOUND 404

#define OPTIONS 0
#define GET 1
#define POST 2

#define PYTHON 1
#define PHP 2

#define OKAY 1
#define ERROR 0

// Definicion de varibale global necesaria para señal sigint
static char * server_root, *server_signature;
FILE * file;
int output, connfd;

/*
 * Estructura que contiene los parametros necesarios para dar una respuesta a la peticion
 */
typedef struct {
	char * method; // GET/POST/OPTIONS
	char * path; // Ruta de datos de la solicitud
	char * originPath; // Ruta inicial, necesario para options
	int minor_version; // Indica si el protocolo HTTP es 1.0 o 1.1
	int type; // Indica el tipo de respuesta que se va a dar a la peticion 
	int pret; // Longitud de la peticion
} request;


/*
 * Funcion que libera la estructura request
 * INPUT:
 * 	request* req : estructura a liberar
 */
void free_req(request* req){
	free(req->method);
	free(req->path);
	free(req->originPath);
	free(req);
}

/*
 * Funcion que captura la señal SIGINT
 * INPUT:
 * 	int signo : identificador de señal
 */
void sig_handler(int sign){
	if (sign == SIGINT){
	    free(server_root);
		free(server_signature);
		close(output);
		close(connfd);
        exit(EXIT_SUCCESS);
    }
}

/*
 * Funcion que expresa la fecha y hora actuales en el formato indicado
 * INPUT
 * 	char* fecha : puntero en el que almacenar la fecha expresada
 */
void getDateGMT(char *fecha){
	time_t tiempo = time(0);
	struct tm *tlocal = gmtime(&tiempo);
	strftime(fecha,128,"%a, %d %b %Y %H:%M:%S %Z",tlocal);
}


/*
 * Funcion que obtiene la fecha de ultima modificacion de un archivo
 * INPUT
 * 	char* path : ruta de datos del archivo
 * 	char* fecha : puntero en el que almacenar la fecha
 */
void getLastModified(char * path, char *fecha){
	struct stat s;
	stat(path, &s);
	strftime(fecha,128, "%a, %d %b %Y %H:%M:%S %Z", gmtime(&s.st_mtime));
	
}


/*
 * Funcion que calcula el tamaño del contenido de la ruta de datos
 * INPUT 
 * 	char* path : ruta de datos
 * OUTPUT
 * 	long tamaño
 */
long getContentLenght(char* path){
	struct stat s;
	long size;
	stat(path, &s);
	size= s.st_size;
	return size;
}


/*
 * Funcion que envia el cuerpo de la respuesta
 * INPUT
 *  int file: archivo a enviar
 *  int connfd: descriptor de la conexion
 */
void sendBody(int file, int connfd){
	char body[MAX_RESPONSE];
	int ret;
	
	while((ret=read(file,body,MAX_RESPONSE))>0){  // Lee del fichero hasta que se termina y lo escribe
		write(connfd, body, ret);
	}
}


/*
 * Funcion que envia la cabecera de una peticion tipo GET
 * INPUT
 * 	int connfd: descriptor de la conexion
 * 	request* req : estructura con algunos campos importantes de la peticion
 * 	char* server_signature : tipo de servidor y nombre
 * 	char* ext : extension que indica el tipo de contenido
 * 	int method: indica si el metodo es get o options
 * 	int script: ndica si el path hace referencia a un archivo de tipo script
 */
void cabecera_GET_OPTIONS(int connfd, request* req, char* server_signature, char* ext, int method, int script){
	char* info=NULL;
	char response[MAX_RESPONSE];
	char fecha[128];
	size_t response_len=0;

	info = (char *)malloc(MAXTAM*sizeof(char));
	
	// Linea inicial del mensaje
	sprintf(response, "HTTP/1.%d %d OK\r\n", req->minor_version, req->type);
	
	// Cabecera del mensaje
	// Date
	getDateGMT(fecha);
	sprintf(info, "Date: %s\r\n", fecha);
	strcat(response, info);
	//server
	sprintf(info, "Server: %s\r\n", server_signature); // Nombre y version del servidor fichero de config);
	strcat(response, info);
	
	// Solo se puede hacer post de archivos tipo script y si ext es null el path no es un archivo y damos todas las opciones
	if(method == OPTIONS){
		if (ext == NULL || script) {
			strcat(response, "Allow: GET, POST, OPTIONS\r\n\r\n");
		} else {
			strcat(response, "Allow: GET, OPTIONS\r\n\r\n");
		}
	} else { // method == GET
		// Last modif
		getLastModified(req->path, fecha);
		sprintf(info, "Last-Modified: %s\r\n", fecha);
		strcat(response, info);
		// Content lenght
		sprintf(info, "Content-Lenght: %ld\r\n", getContentLenght(req->path));
		strcat(response, info);
		// Extension 
		// Han quedado descartadas antes de llamar a la funcion las extensiones .py y .php	
		strcat(response, "Content-Type: ");
		if (!strcmp(ext, ".txt"))
			strcat(response, "text/plain\r\n\r\n");
		else if (!strcmp(ext, ".html") || !strcmp(ext, ".htm"))
			strcat(response, "text/html\r\n\r\n");
		else if (!strcmp(ext, ".gif"))
			strcat(response, "image/gif\r\n\r\n");
		else if (!strcmp(ext, ".jpeg") || !strcmp(ext, ".jpg"))
			strcat(response, "image/jpeg\r\n\r\n");
		else if (!strcmp(ext, ".mpeg") || !strcmp(ext, ".mpg"))
			strcat(response, "video/mpeg\r\n\r\n");
		else if (!strcmp(ext, ".doc") || !strcmp(ext, ".docx"))
			strcat(response, "application/msword\r\n\r\n");
		else if (!strcmp(ext, ".pdf"))
			strcat(response, "application/pdf\r\n\r\n"); 
		else {
			strcat(response, "not-included\r\n\r\n");
		}
	}
	response_len = strlen(response);
	write(connfd, (void*)response, response_len*sizeof(char)); // Escribimos la respuesta en el fichero de configuracion
	
	// Liberamos recursos
	free(info);

}


/*
 * Funcion que envia la cabecera de un get o un post de un archivo tipo script
 * INPUT:
 *	 int connfd: descriptor de la conexion
 *	 request* req : estructura con algunos campos importantes de la peticion
 *	 char* server_signature : tipo de servidor y nombre
 *	 long length : longitud del fichero
 */
void cabecera_SCRIPT(int connfd,request * req,char* server_signature,long length){
	char* info=NULL;
	char response[MAX_RESPONSE];
	char fecha[128];
	size_t response_len=0;

	info = (char *)malloc(MAXTAM*sizeof(char));
	
	// Linea inicial del mensaje
	sprintf(response, "HTTP/1.%d %d OK\r\n", req->minor_version, req->type);
	
	// Cabecera del mensaje
	// Date
	getDateGMT(fecha);
	sprintf(info, "Date: %s\r\n", fecha);
	strcat(response, info);
	//server
	sprintf(info, "Server: %s\r\n", server_signature); // Nombre y version del servidor fichero de config);
	strcat(response, info);
	
	// Last modif
	getLastModified(req->path, fecha);
	sprintf(info, "Last-Modified: %s\r\n", fecha);
	strcat(response, info);
	// Content lenght
	sprintf(info, "Content-Lenght: %ld\r\n", length);
	strcat(response, info);
	// Extension 
	strcat(response, "Content-Type: ");
	strcat(response, "text/html\r\n\r\n");

	response_len = strlen(response);
	write(connfd, (void*)response, response_len*sizeof(char)); // Escribimos la respuesta en el fichero de configuracion
	
	// Liberamos recursos
	free(info);
}


/*
 * Funcion que ejecuta un script
 * INPUT:
 *	 int connfd: descriptor de la conexion
 *	 request* req: estructura con algunos campos importantes de la peticion
 *	 char* server_signature : tipo de servdor y nombre
 *	 char* buf : peticion parseada
 *	 int method : indica si es get o post
 *	 int script : indica si es php o python
 */
void fscript (int connfd, request * req, char * param, char *server_signature, char *buf, int method, int script){
	char command[MAXTAM], response[MAX_RESPONSE];
	int ret, pipe;
	char * body;
	long length;
	
	// Escribimos el comando a ejecutar
	if(method == GET){
		if(script == PYTHON)
			sprintf(command, "python %s %s", req->path, param);
		else  // PHP
			sprintf(command, "php %s %s", req->path, param);
	} else { // POST
		body = buf + req->pret;
		if(script == PYTHON)
			sprintf(command, "echo \"%s\" | python %s %s", body, req->path, param);
		else // PHP
			sprintf(command, "echo \"%s\" | php %s %s", body, req->path, param);
	}

	file = popen (command, "r"); // Inicia el pipe
	pipe = fileno(file); // Devuelve el descriptor del fihero asociado a la conexion del pipe
	output = open(".", O_TMPFILE | O_RDWR); // Crea un archivo temporal en el que se pueda escribir
	
	while ((ret = read(pipe, response, MAX_RESPONSE)) > 0){ // Escribimos la respuesta en el fichero creado
		write(output, response, ret);
	}
	
	// Calculamos la longitud de un fichero dado su descriptor
	length = lseek(output, 0, SEEK_END); // Coloca el puntero al final del fichero obteniendo asi la longitud
	lseek(output, 0, SEEK_SET); // Vuelta al estado inicial

	// Creamos cabecera
	cabecera_SCRIPT(connfd, req, server_signature, length);
	sendBody(output, connfd);
	
	// Cierre del descriptor y del fichero
	close(output);
	pclose(file);
		
}


/*
 * Funcion que responde a una peticion GET, POST u OPTIONS
 * INPUT
 * 	request* req : puntero a la estructura que contiene el path, el method, 
 *                 el tipo de peticion y la version
 * 	int connfd : descriptor de la conexion
 *  char * server_signature : nombre y el tipo de servidor
 */
void response(char* buf, request* req, int connfd, char * server_signature){
	size_t response_len=0;
	char response[MAX_RESPONSE]="", *ext=NULL;
	int file;
	int flag = 0, method, script = 0;
	char * param;
	

	if(req->type == BAD_REQUEST){ // El servidor no pudo interpretar la petición correctamente, probablemente debido a un error de sintaxis. 
		sprintf(response, "HTTP/1.%d %d Bad Request\r\n", req->minor_version, req->type);
		response_len = strlen(response);
		write(connfd, response, response_len*sizeof(char));
		return;
	}  
	
	// Identificamos el metodo
	if (!strcmp("GET", req->method)){
		method = GET;
	} else if (!strcmp("POST", req->method)){
		method = POST;
	} else if (!strcmp("OPTIONS", req->method)){
		method = OPTIONS;
	} else {
		strcat(response, "Not possible method\nAllow: GET, POST, OPTIONS\r\n\r\n");
		response_len = strlen(response);
		write(connfd, (void*)response, response_len*sizeof(char)); // Escribimos la respuesta en el fichero de configuracion
		return;
	}
	
	// Parseo de la extension del path
	ext=strrchr(req->path, '.');
	// Parseo de los argumentos del path
	param = strchrnul(req->path, '?');
	if (param[0] != '\0') {
		param[0] = '\0';
		param++;
	}
	if (ext != NULL){ // En caso de extension correcta, identificamos si es de tipo python o php
		if (!strncmp(ext, ".py", 3)){
			script = PYTHON;
		} else if (!strncmp(ext, ".php", 4)){
			script = PHP;
		}
	}

	if(strcmp("*",req->originPath)){ // Si el path inicial no es una interrogacion, entonces abrimos el fichero pedido
		flag = 1;
		file = open(req->path, O_RDONLY); // Abrimos el fichero en modo lectura
		if (file < 0) { // El servidor no ha podido encontrar el recurso solicitado.
			req->type = NOT_FOUND;
			sprintf (response, "HTTP/1.%d %d Not Found\r\n", req->minor_version, req->type);
			response_len = strlen(response);
			write(connfd, (void*)response, response_len*sizeof(char));
			return;
		}
	}
	
	if (method && flag && script){ // Caso de metodo POST o GET con script
		fscript(connfd, req, param, server_signature, buf, method, script); // Ejecuta el script
 	} else if (method == GET && flag){
 		cabecera_GET_OPTIONS(connfd, req, server_signature, ext, method, script);
 		sendBody(file, connfd); // Enviar mensaje
 	
	} else if(method == POST && flag){
		req->type = BAD_REQUEST;
		sprintf (response, "HTTP/1.%d %d Bad Request\r\n", req->minor_version, req->type);
		response_len = strlen(response);
		write(connfd, (void*)response, response_len*sizeof(char));
		close(file);
		return;
	} else { // Peticion OPTIONS
		cabecera_GET_OPTIONS(connfd, req, server_signature, ext, method, script);
	}
	
	if(flag == 1)
		close(file); // Cerramos fichero
}


/*
 * Funcion que parsea una peticion http
 * INPUT
 * 	char* buf : solicitud a parsear
 *  int rrecv : numero de bytes leidos de connfd por recv
 *  request* req : estrcutura con los datos de la solicitud
 *  char* server_root : directorio raiz
 * OUTPUT:
 * 	ERROR si error parseando la solicitud, OKAY en caso contrario
 */
int parseo(char* buf, int rrecv, request* req, char * server_root){
	char *method, *path, *info=NULL;
	int pret, minor_version;
	struct phr_header headers[100];
	size_t method_len, path_len, num_headers;
	unsigned int i;
	
	req->type = OK; // Empezamos dandola por valida

	// Leer solicitud
	num_headers = sizeof(headers) / sizeof(headers[0]);
	pret = phr_parse_request(buf, rrecv, (const char **) &method, &method_len, (const char **) &path, &path_len, &minor_version, headers, &num_headers, 0); // Guarda en las variables los datos necesarios para realizar la peticion

	// Control de errores
	if(pret == -1){
		req->type = BAD_REQUEST; // Solicitud mal expresada
		syslog(LOG_ERR,"Error parseando la solicitud\n");
	}
	
	
	// Guarda valores obtenidos de la solicitud y los almacenamos en la estructura
	info = (char *)malloc(MAXTAM*sizeof(char)); // Auxiliar para imprimir informacion
	
	req->pret = pret;
	sprintf(info, "request is %d bytes long\n", req->pret);
	syslog(LOG_INFO, "%s", (const char *) info);
	
	req->method = (char *) malloc((int)(method_len+1) * sizeof(char));	
	sprintf(req->method, "%.*s", (int)method_len, method);
	sprintf(info, "method is %s\n",req->method);
	syslog(LOG_INFO, "%s", (const char *) info);
	
	req->path = (char *) malloc(((int)(path_len+1) + strlen(server_root))* sizeof(char));
	req->originPath = (char *) malloc((int)(path_len+1)* sizeof(char));
	sprintf(req->originPath, "%.*s", (int)path_len, path);
	sprintf(req->path, "%s%s", server_root, req->originPath);
	sprintf(info, "path is %s\n", req->path);
	syslog(LOG_INFO, "%s", (const char *) info);
	

	req->minor_version = minor_version;
	sprintf(info, "HTTP version is 1.%d\n", req->minor_version);
	syslog(LOG_INFO, "%s", (const char *) info);
	
	sprintf(info, "headers:\n");
	syslog(LOG_INFO, "%s", (const char *) info);
	
	// Guarda todas las cabeceras de la peticion
	for (i = 0; i != num_headers; ++i) {
	    sprintf(info, "%.*s: %.*s\n", (int)headers[i].name_len, headers[i].name,
		   (int)headers[i].value_len, headers[i].value);
		syslog(LOG_INFO, "%s", (const char *) info);
	}
	free(info);
	
	return OKAY;
}


/*
 * Funcion que parsea una peticion http
 * INPUT
 * 	char* buf : solicitud a parsear
 *  int rrecv : numero de bytes leidos de connfd por recv
 *  request* req : estrcutura con los datos de la solicitud
 *  char* server_root : directorio raiz
 * OUTPUT:
 * 	ERROR si error parseando la solicitud, OKAY en caso contrario
 */
void * servicio (void * arg){
	char buf[MAXBUF];
	int connfd = 0;
	int ret = 0;
	char * server_root, *server_signature;
	request* req = NULL;
	req = (request *) malloc(sizeof(request));
	
	// Inicializamos variables
	void **args = arg;
	connfd = *((int *)args[0]);
	server_root = args[1];
	server_signature = args[2];
	
	
	pthread_detach(pthread_self()); // Configura liberación de recursos cuando termina el hilo
	ret = recv(connfd, buf, MAXBUF,0); // lee el descriptor de conexion
	parseo(buf, ret, req, server_root); // Parsea la peticion guardando los valores necesarios para la respuesta en la estructura req
	response(buf, req, connfd, server_signature); // Responde a la peticion
	
	close(connfd); // Cerramos el descriptor de la conexion
	free_req(req); // Liberamos recursos 
	return NULL;
}

/*
 * Funcion que lee el fichero de configuracion y guarda su contenido
 * INPUT
 * 	char ** server_root : puntero al string que contendra la ruta del servidor
 * 	int * max_clients : puntero a entero que contendra el numero maximo de conexiones a establecer
 *  int * listen_port : puntero a entero que contendra el numero del puerto
 *                      por donde se escuchará el servidor
 *  char ** server_signature : puntero al string que contendra el nombre y el tipo de servidor
 */
void file_conf(char ** server_root, int * max_clients, int * listen_port, char ** server_signature){
    cfg_opt_t opts[] = {
        CFG_SIMPLE_STR("server_root", server_root),
        CFG_SIMPLE_INT("max_clients", max_clients),
        CFG_SIMPLE_INT("listen_port", listen_port),
        CFG_SIMPLE_STR("server_signature", server_signature),
        CFG_END()
    };
    cfg_t *cfg;
    
    cfg = cfg_init(opts, 0); // Inicializa cfg con los valores de opts
    cfg_parse(cfg, "file.conf"); // Parsea el fichero "file.conf" guardando los valores deseados
    
    cfg_free(cfg); // Libera recurso

}

// Funcion principal
int main(int argc, char const *argv[]){
	int sockfd;
	pthread_t tid;
	static int  max_clients, listen_port;
	void *arg[3];
	
	file_conf(&server_root, &max_clients, &listen_port, &server_signature); // Leemos el fichero de configuracion y guardamos los datos deseados
	arg[1] = (void *)server_root;
	arg[2] = (void *)server_signature;
	
	if(signal(SIGINT, sig_handler) == SIG_ERR){ // Error al asignar la señal handler
		free(server_root);
    	free(server_signature);
		return -1;	
	} 
	
	/*if(demonizar("SERVIDOR DEMON") < 0){
		free(server_root);
    	free(server_signature);
		return -1;	
	}*/
	
	
	sockfd = iniciar_socket(listen_port,max_clients); // Iniciamos la conexion
	if(sockfd < 0){
		free(server_root);
    	free(server_signature);
		return -1;	
	}
	
	while(1){ 
		connfd = aceptar(sockfd); // Registra un socket en un puerto
		arg[0] = (void *)&connfd;
		if(connfd < 0) { // Error de conexion
			free(server_root);
			free(server_signature);
		 	return -1;
		}
		pthread_create(&tid, NULL, &servicio, (void *)arg);
		
	}
	
	// Liberamos recursos
	free(server_root);
    free(server_signature);
	return 0;
}
