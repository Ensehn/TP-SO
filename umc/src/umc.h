/*
 * umc.h
 *
 *  Created on: 11/5/2016
 *      Author: utnso
 */

#ifndef UMC_H_
#define UMC_H_

#include <stdio.h>
#include <stdlib.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <errno.h>
#include <unistd.h>
#include <sys/types.h>
#include <netinet/in.h>
#include <sys/time.h>
#include <pthread.h>
#include <stdbool.h>
#include <string.h>
#include <commons/string.h>
#include <commons/log.h>
#include <commons/config.h>
#include <commons/collections/dictionary.h>
#include <commons/collections/queue.h>
#include <commons/collections/list.h>
#include <math.h>
#include "handshake.h"
#include "header.h"
#include "cliente-servidor.h"
#include "log.h"
#include "commonTypes.h"
#include <math.h>
#include "CUnit/Basic.h"


typedef struct customConfig {
	int puerto_umc_nucleo;
	int puerto_swap;
	int puerto_cpu;

	int cantidad_marcos;
	int tamanio_marco;

	int entradas_tlb;
	int retardo;
	char* ip_swap;
} customConfig_t;

customConfig_t config;
t_config* configUmc;


typedef struct tlbStruct{
	int pid,
		pagina,
		marcoUtilizado;
}tlb_t;

typedef struct{
	 int pid,
		 paginaRequerida,
		 offset,
		 cantBytes,
		 buffer;
}pedidoLectura_t;

typedef struct{ //No hace falta indicar el numero de la pagina, es la posicion
	int nroPagina;
	int marcoUtilizado;
	char bitPresencia;
	char bitModificacion;
	char bitUso; //Quizas vuele..
}tablaPagina_t;


//Globales servidor
int socketCPU, socketNucleo;
int activadoCPU, activadoNucleo; //No hace falta iniciarlizarlas. Lo hacer la funcion permitir reutilizacion ahora.
struct sockaddr_in direccionCPU, direccionNucleo;
unsigned int tamanioDireccionCPU, tamanioDireccionNucleo;

typedef int ansisop_var_t;
int cliente;
t_log *activeLogger, *bgLogger;
char* memoria;


char* pedidoPaginaPid ;
char* pedidoPaginaTamanioContenido;

t_list* listaTablasPaginas;
t_list* tabla5;

tlb_t* tlb;

unsigned int* vectorMarcosOcupados; //vectorMarcosOcupados[n]== 1 -> Esta ocupado

int tamanioMemoria;

int tlbHabilitada = 1; //1 ON.  0 OFF

pthread_t conexSwap;
pthread_t conexNucleo;
pthread_t conexCpu;
pthread_t consolaUmc;

t_log *dump;



//Prototipos

int estaEnTlb(pedidoLectura_t pedido);
int buscarEnTlb(pedidoLectura_t pedido);
int existePidEnListadeTablas(int pid);
int existePaginaBuscadaEnTabla(int pag, t_list* tablaPaginaBuscada);
char* buscarMarco(int marcoBuscado, pedidoLectura_t pedido);
int buscarPrimerMarcoLibre();
int cantidadMarcosLibres();

char* buscarEnSwap(int marcoBuscado, pedidoLectura_t pedido); //TODO
char* agregarAMemoria(tablaPagina_t* paginaBuscada); //TODO

char* devolverPedidoPagina(pedidoLectura_t pedido);   // todos estos volver a devolver void, devuelven cosas para testear
char* almacenarBytesEnUnaPagina(pedidoLectura_t pedido); //TODO
void finalizarPrograma(int idPrograma); //TODO
void inicializarPrograma(int idPrograma, int paginasRequeridas); //TODO
int reservarPagina(int,int);

void inicializarTlb();
void fRetardo();
void dumpEstructuraMemoria(); //TODO
void dumpContenidoMemoria(); //TODO
void flushTlb(); //TODO
void flushMemory(); //TODO
void recibirComandos();

void servidorCPUyNucleoExtendido();
void servidorCPUyNucleo();
int  getHandshake();
void handshakearASwap();
void conectarASwap(); //MOTHER OF DECLARATIVIDAD...
void realizarConexionASwap();
void escucharPedidosDeSwap();
void conexionASwap();

void procesarHeader(int cliente, char *header);

void crearMemoriaYTlbYTablaPaginas();

//Fin prototipos






#endif /* UMC_H_ */
