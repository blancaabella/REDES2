#ifndef SOCKET_H
#define SOCKET_H

/*
 * Funcion demonizar
 * INPUT : 
 * char * ident : identificador del servicio como cadena de caracteres
 * OUTPUT :
 * 0 en caso de ejecucion correcta, -1 en caso contrario
 */
int demonizar(char * ident);

/*
 * Funcion que inicia un socket de conexiones maximas
 * INPUT
 * 	puerto : numero de puerto por donde se escuchará el servidor
 * 	conexiones : numero maximo de conexiones a establecer
 * OUTPUT
 * 	descritor del socket
 */
int iniciar_socket (int puerto, int conexiones);

/*
 * Funcion que registra un socket en un puerto
 * INPUT
 * 	sockfd : parametr devuelto por la llamada a socket
 * OUTPUT
 * 	descriptor de la conexion
 */
int aceptar (int sockfd);

#endif
