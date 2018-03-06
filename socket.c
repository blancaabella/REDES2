#include <unistd.h>
#include <syslog.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>

#include "socket.h"




/*
 * Funcion demonizar
 * INPUT : 
 * 	char * ident : identificador del servicio como cadena de caracteres
 * OUTPUT :
 * 	0 en caso de ejecucion correcta, -1 en caso contrario
 */
int demonizar(char * ident) {
	pid_t pid;
	int i = 0;
	
	// crear proceso hijo
	if((pid = fork()) < 0){ // error
		perror ("error creando hijo");
		exit(EXIT_FAILURE);
		
	} else if (pid > 0) { // proceso padre
		exit(EXIT_SUCCESS); // terminar padre
		
	} else { // proceso hijo
		if (setsid() < 0) {
			perror ("error setsid");
			exit(EXIT_FAILURE);
		}
		
		umask ((mode_t) 0); // Cambiar la máscara de modo de ficheros para que sean accesibles a cualquiera
		
		if(chdir("/") == -1){ // Establece el directorio raíz / como directorio de trabajo
			perror ("error chdir");
			exit(EXIT_FAILURE);
		}
		
		for (i = 0; i < getdtablesize(); i++){ // Cierre de todos los descriptores de fichero que pueda haber abiertos
            		if(close(i) == -1){
				perror ("error close");
				exit(EXIT_FAILURE);
			}
        }
		openlog(ident, 0, LOG_DAEMON); //Abrimos el log del sistema para su uso posterior
		syslog(LOG_INFO, "El proceso ha sido demonizado.\n");
		return 0;
	}
}
	

/*
 * Funcion que inicia un socket de conexiones maximas
 * INPUT
 * 	puerto : numero de puerto por donde se escuchará el servidor
 * 	conexiones : numero maximo de conexiones a establecer
 * OUTPUT
 * 	descritor del socket
 */
int iniciar_socket (int puerto, int conexiones) {
	int sockfd;
	struct sockaddr_in socketDir;
	
	//socket
	if((sockfd = socket(AF_INET, SOCK_STREAM, 0)) == -1){ //creamos socket de flujo con protocolos ARPA de internet
		syslog(LOG_ERR, "Error creando socket.\n");
		exit(EXIT_FAILURE);
	}
	syslog(LOG_INFO, "Socket creado con éxito.\n");

	//bind
	socketDir.sin_family = AF_INET; // dominio de comunicacion del socket. Usamos AF_INET porque los procesos estan en distintos sistemas y unidos mediante una red TCP/IP.
	socketDir.sin_port = htons(puerto); // puerto al que se asocia el socket
	socketDir.sin_addr.s_addr = htons(INADDR_ANY); // direccio IP a la que se asocia el socket. Usamos INADR_ANY para permitir conexiones de cualquier direccion
	
	if(bind(sockfd, (struct sockaddr *) &socketDir, sizeof(socketDir)) == -1) {
		syslog(LOG_ERR, "Error asociando socket con puerto mediante bind.\n");
		exit(EXIT_FAILURE);
	}
	syslog(LOG_INFO, "Socket asociado con éxito.\n");

	//listen
	if(listen(sockfd, conexiones) < 0) { //Habilita socket para que pueda recibir conexiones
		syslog(LOG_ERR, "Error habilitando conexiones.\n");	
		exit(EXIT_FAILURE);	
	}
	syslog(LOG_INFO, "Conexiones habilitadas con éxito.\n");
	return sockfd;
}


/*
 * Funcion que registra un socket en un puerto
 * INPUT
 * 	sockfd : parametr devuelto por la llamada a socket
 * OUTPUT
 * 	descriptor de la conexion
 */
int aceptar (int sockfd){
	int connfd;
	socklen_t addrlen;
	struct sockaddr addr;

	addrlen = sizeof(addr);

	if((connfd = accept(sockfd, &addr, &addrlen)) == -1){ // El servidor queda en estado de espera y no retorna hasta que intenten conectarse
		syslog(LOG_ERR, "Error aceptando conexion.\n");		
		exit(EXIT_FAILURE);	
	}
	syslog(LOG_INFO, "Conexion %d aceptada con éxito.\n", connfd);

	return connfd;
}










