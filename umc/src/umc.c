/*
 * umc.c
 *
 *  Created on: 16/4/2016
 *      Author: utnso
 */

#include <string.h>
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

#define PUERTO_UMC_NUCLEO 8081
#define MARCO 10
#define MARCO_SIZE 100 //en bytes
#define DEBUG true
#define PUERTO_SWAP 8082
#define ENTRADAS_TLB 10
#define RETARDO 5 //En MICRO SEGUNDOS: 1 micro = 1000 milisegundos

//Prototipos
void procesarHeader(int cliente, char *header);
//Fin prototipos

typedef struct tlbStruct{
	int pid,
		pagina;
	char* direccion;
}tlb_t;

typedef struct{
	 int pid,
		 paginaRequerida,
		 offset,
		 cantBytes;
}pedidoLectura_t;

/*
typedef struct pedidoMemoria{
	int pid;
	char* contenido;
}pedidoMemoria_t;
*/

typedef struct{ //No hace falta indicar el numero de la pagina, es la posicion
	int pid;
	int nroPagina;
	int marcoUtilizado;
	char bitPresencia;
	char bitModificacion;
	char bitUso; //Quizas vuele..
}tablaPagina_t;

//typedef struct{
//    int indice;
//    int uso;
//    void* direccionContenido;
//}marco_t;

int tamanioMemoria = MARCO * MARCO_SIZE;

typedef int ansisop_var_t;
int cliente;
t_log *activeLogger, *bgLogger;
char* memoria;
int retardo = RETARDO;

//t_queue* marcosLibres; Por el momento deprecated..

char* pedidoPaginaPid ;
char* pedidoPaginaTamanioContenido;

t_list* listaTablasPaginas;

tlb_t* tlb;

int vectorMarcosOcupados[MARCO]; //vectorMarcosOcupados[n]== 1 -> Esta ocupado

int tlbHabilitada = 1; //1 ON.  0 OFF

pthread_t SWAP;
pthread_t NUCLEO_CPU;


struct timeval newEspera()
{
	struct timeval espera;
	espera.tv_sec = 2; 				//Segundos
	espera.tv_usec = 500000; 		//Microsegundos
	return espera;
}

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

char* devolverPedidoPagina(pedidoLectura_t pedido);
void almacenarBytesEnUnaPagina(int nroPagina, int offset, int tamanio, int buffer); //TODO
void finalizarPrograma(int idPrograma); //TODO
void inicializarPrograma(int idPrograma, int paginasRequeridas); //TODO

void fRetardo();
void dumpEstructuraMemoria(); //TODO
void dumpContenidoMemoria(); //TODO
void flushTlb(); //TODO
void flushMemory(); //TODO
void recibirComandos();

void servidorCPUyNucleo();
int getHandshake();
void handshakearASwap();
void conectarASwap(); //MOTHER OF DECLARATIVIDAD...
void realizarConexionASwap();
void escucharPedidosDeSwap();
void conexionASwap();

void procesarHeader(int cliente, char *header);

void crearMemoriaYTlbYTablaPaginas();

//Fin prototipos





// ************** EMPIEZA .C *****************


//0. Funciones auxiliares a las funciones Principales


int estaEnTlb(pedidoLectura_t pedido){
	int i;
	for(i=0;i<ENTRADAS_TLB; i++){
		if(tlb[i].pid==pedido.pid && tlb[i].pagina==pedido.paginaRequerida){
			return 1;
		}
	}
	return 0;
}

int buscarEnTlb(pedidoLectura_t pedido){ //Repito codigo, i know, pero esta soluc no funciona para las dos, porque si se encuentra el pedido en tlb[0] y retornas 'i', "no estaria en tlb" cuando si
	int i;
	for(i=0;i<ENTRADAS_TLB; i++){
		if(tlb[i].pid==pedido.pid && tlb[i].pagina==pedido.paginaRequerida){
			return i;
		}
	}
	return 0;
}

int existePidEnListadeTablas(int pid){
	(list_get(listaTablasPaginas, pid)!=NULL)?1:0; //Va a la posicion de la lista de las tablas de paginas. ==NULL no existe el elemento
}

int existePaginaBuscadaEnTabla(int pag, t_list* tablaPaginaBuscada){
	(list_get(tablaPaginaBuscada,pag))?1:0;   //EN VERDAD DEVUELVE NULL SI NO HAY NADA? ...
}

//char* buscarEnTablaMarcos(int marcoBuscado, pedidoLectura_t pedido){ //Ver si necesito o no el pedido, me suena que tenia que hacer algo
//	marco_t* marcoEnTabla = list_get(tablaMarcos,marcoBuscado);
//	return (marcoEnTabla->direccionContenido);
//}

char* buscarMarco(int marcoBuscado, pedidoLectura_t pedido){
	int pos = marcoBuscado * MARCO_SIZE;
	return memoria[pos];
}

int buscarPrimerMarcoLibre(){
	int i;
	for(i=0;i<MARCO;i++){
		(vectorMarcosOcupados[i]==0)?:i;
	}
	return -1;
}

int cantidadMarcosLibres(){
	int i;
	int c;
	for(i=0;i<MARCO;i++){
		if(vectorMarcosOcupados[i]==0)
			c++;
	}
	return c;
}

// FIN 0


// 1. Funciones principales

char* buscarEnSwap(int marcoBuscado, pedidoLectura_t pedido){
	//TODO
}

char* agregarAMemoria(tablaPagina_t* paginaBuscada){
	//TODO
}

char* devolverPedidoPagina(pedidoLectura_t pedido){

	if(estaEnTlb(pedido) && tlbHabilitada){
		log_info(activeLogger,"Se encontro en la Tlb el pid: %d, pagina: %d",pedido.pid,pedido.paginaRequerida);
		int pos = buscarEnTlb(pedido);
		send_w(cliente, tlb[pos].direccion, 4);

	}
	else{
		log_info(activeLogger,"No se encontro en la Tlb el pid: %d, pagina: %d. Se buscara en la Lista de tablas de paginas",pedido.pid,pedido.paginaRequerida);

		if(existePidEnListadeTablas(pedido.pid)){ //Si existe la tabla de paginas dentro de la lista
			t_list* tablaPaginaBuscada = list_get(listaTablasPaginas, pedido.pid);

			if(existePaginaBuscadaEnTabla(pedido.paginaRequerida,tablaPaginaBuscada)){ //Si la pagina existe dentro de la tabla particular
				tablaPagina_t* paginaBuscada = list_get(tablaPaginaBuscada, pedido.paginaRequerida);

				if(paginaBuscada->bitPresencia){
					log_info(activeLogger,"Se encontro la pagina y esta en memoria! Devolviendo pag:%d de pid:%d",pedido.paginaRequerida,pedido.pid);
					char* devolucion = buscarMarco(paginaBuscada->marcoUtilizado,pedido);
					send_w(cliente, devolucion, 4);
				}
				else{
					char* devolucion = buscarEnSwap(paginaBuscada->marcoUtilizado,pedido);
					send_w(cliente, devolucion, 4);
					agregarAMemoria(paginaBuscada);
				}
			}
			else{
				send_w(cliente, intToChar(HeaderNoExistePagina), 4);
			}
		}
		else{
			send_w(cliente,  intToChar(HeaderNoExisteTablaDePag), 4);
		}
	}
}


void almacenarBytesEnUnaPagina(int nroPagina, int offset, int tamanio, int buffer){
}

void finalizarPrograma(int idPrograma){
}

void inicializarPrograma(int idPrograma, int paginasRequeridas){
}


//FIN 1


//2. Funciones que se mandan por consola

void fRetardo(){
	int nuevoRetardo;
	printf("Ingrese el nuevo valor de Retardo en milisegundos: ");
	scanf("%d",nuevoRetardo);
	retardo = nuevoRetardo*1000;
}
void dumpEstructuraMemoria(){
}
void dumpContenidoMemoria(){
}
void flushTlb(){
}
void flushMemory(){
}

void recibirComandos(){
	int funcion;
	do {
		printf("Funciones: 0.salir / 1.retardo / 2.dumpEstructuraMemoria / 3.dumpContenidoMemoria / 4.flushTlb / 5.flushMemory \n");
		printf("Funcion: ");
		scanf("%d ",&funcion);

		switch(funcion){
			case 1: fRetardo();
			case 2: dumpEstructuraMemoria();
			case 3: dumpContenidoMemoria();
			case 4: flushTlb();
			case 5: flushMemory();
			default: break;
		}
	}
	while(funcion!=0);
}
// FIN 2

// 3. Inicializar estructura de UMC

void crearMemoriaYTlbYTablaPaginas(){

	//marcosLibres = queue_create();

	//Creo memoria y la relleno
	memoria = malloc(tamanioMemoria);
	memset(memoria,'\0',tamanioMemoria);
	log_info(activeLogger,"Creada la memoria.");

	//Relleno TLB
	int i;
	for(i = 0; i<ENTRADAS_TLB; i++){
		tlb[i].pid=-1;
		tlb[i].pagina=-1;
		tlb[i].direccion=NULL;
	}
	log_info(activeLogger,"Creada la TLB y rellenada con ceros (0).");

	//Relleno tabla marcos DEPRECATED
//	int k;
//	for(k=0;k<MARCO;k++){
//		tablaMarcos[k].indice=k;
//		tablaMarcos[k].uso=0;
//		tablaMarcos[k].direccionContenido=NULL;
//		queue_push(marcosLibres,&tablaMarcos[k]);
//	}
}

// FIN 3

// 4. Procesar headers

void procesarHeader(int cliente, char *header){
	// Segun el protocolo procesamos el header del mensaje recibido
	char* payload;
	int payload_size;
	log_debug(bgLogger,"Llego un mensaje con header %d\n",charToInt(header));

	switch(charToInt(header)) {

	case HeaderError:
		log_error(activeLogger,"Header de Error\n");
		quitarCliente(cliente);
		break;

	case HeaderHandshake:
		log_debug(bgLogger,"Llego un handshake\n");
		payload_size=1;
		payload = malloc(payload_size);
		read(socketCliente[cliente] , payload, payload_size);
		log_debug(bgLogger,"Llego un mensaje con payload %d\n",charToInt(payload));
		if ( (charToInt(payload)==SOYCPU) || (charToInt(payload)==SOYNUCLEO) ){
			log_debug(bgLogger,"Es un cliente apropiado! Respondiendo handshake\n");
			send(socketCliente[cliente], intToChar(SOYUMC), 1, 0);
		}
		else {
			log_error(activeLogger,"No es un cliente apropiado! rechazada la conexion\n");
			log_warning(activeLogger,"Se quitará al cliente %d.",cliente);
			quitarCliente(cliente);
		}
		free(payload);
		break;

		case HeaderReservarEspacio:
			pedidoPaginaPid = recv_waitall_ws(cliente, sizeof(int));
			pedidoPaginaTamanioContenido = recv_waitall_ws(cliente, sizeof(int));
			//ES NECESARIO TENER EL PID DEL PROCESO Q NUCLEO QUIERE GUARDAR EN MEMORIA? SI: RECIBIR INT  NO: RECIBIR NADA
			log_info(activeLogger,"Nucleo me pidio memoria");

			int cantPaginasPedidas = ((float)charToInt(pedidoPaginaTamanioContenido) + MARCO_SIZE - 1) / MARCO_SIZE; //A+B-1 / B
			int pid = charToInt(pedidoPaginaPid);

			//Primero preguntar si swap tiene espacio..
			if(cantidadMarcosLibres()>=cantPaginasPedidas){ //Si alcanzan los marcos libres...
				t_list* tablaPaginas;
				int i;
				for(i=0;i<cantPaginasPedidas;i++){
//					marco_t* marcoNuevo;
//					marcoNuevo = queue_pop(marcosLibres);
//					marcoNuevo->uso=1;
					int marcoNuevo = buscarPrimerMarcoLibre();
					vectorMarcosOcupados[marcoNuevo]=1; //Lo marco como ocupado

					tablaPagina_t* nuevaPagina;
					nuevaPagina = malloc(sizeof(tablaPagina_t));
					nuevaPagina->pid = pid;
					nuevaPagina->nroPagina = i;
					nuevaPagina->marcoUtilizado = marcoNuevo;
					nuevaPagina->bitPresencia=1;
					nuevaPagina->bitModificacion=0;
					nuevaPagina->bitUso=1;

					list_add_in_index(listaTablasPaginas,pid,tablaPaginas);
				}

				send_w(cliente, headerToMSG(HeaderTeReservePagina), 1);
			}
			else{
				send_w(cliente, headerToMSG(HeaderErrorNoHayPaginas), 1);
			}

			//Hay que agregar a tlb la pagina nueva?

		case HeaderPedirContenidoPagina:
			log_info(activeLogger,"Se recibio pedido de pagina, por CPU");
			pedidoLectura_t pedido;
			pedido.pid = recv_waitall_ws(cliente, sizeof(int));
			pedido.paginaRequerida = recv_waitall_ws(cliente, sizeof(int));
			pedido.offset = recv_waitall_ws(cliente, sizeof(int));
			pedido.cantBytes = recv_waitall_ws(cliente, sizeof(int));
			devolverPedidoPagina(pedido);

		case HeaderGrabarPagina:
			log_info(activeLogger,"Se recibio pedido de grabar una pagina, por CPU");

		case HeaderLiberarRecursosPagina:
			log_info(activeLogger,"Se recibio pedido de liberar una pagina, por CPU");

		default:
			log_error(activeLogger,"Llego cualquier cosa.");
			log_error(activeLogger,"Llego el header numero %d y no hay una acción definida para él.",charToInt(header));
			log_warning(activeLogger,"Se quitará al cliente %d.",cliente);
			quitarCliente(cliente);
			break;
	}
}

// FIN 4

int main(void) {

	//tlb_t tlb[ENTRADAS_TLB];

	tlb = malloc(ENTRADAS_TLB * sizeof(tlb_t));

	crearLogs("Umc","Umc");
	log_info(activeLogger,"Soy umc de process ID %d.", getpid());

	crearMemoriaYTlbYTablaPaginas();

	pthread_create(&SWAP, NULL, (void*) conexionASwap, NULL);

	pthread_create(&NUCLEO_CPU, NULL, (void*) servidorCPUyNucleo, NULL); //OJO! A cada cpu hay que atenderla con un hilo

	recibirComandos(); //Otro hilo?

	free(memoria);

	return 0;
}











// 5.Server de los cpu y de nucleo
void servidorCPUyNucleo(){

	int mayorDescriptor, i;
	struct timeval espera = newEspera(); 		// Periodo maximo de espera del select
	char header[1];

	crearLogs("Umc","Umc");
	configurarServidor(PUERTO_UMC_NUCLEO);
	inicializarClientes();
	log_info(activeLogger,"Esperando conexiones ...");

	while(1){
		mayorDescriptor = incorporarSockets();
		select( mayorDescriptor + 1 , &socketsParaLectura , NULL , NULL , &espera);

		if (tieneLectura(socketNuevasConexiones))
			procesarNuevasConexiones();

		for (i = 0; i < getMaxClients(); i++){
			if (tieneLectura(socketCliente[i]))	{
				if (read( socketCliente[i] , header, 1) == 0)
					quitarCliente(i);
				else{
					log_debug(bgLogger,"LLEGO main %c\n",header);
					procesarHeader(i,header);
				}
			}
		}
	}
	destruirLogs();
}

int getHandshake()
{
	char* handshake = recv_nowait_ws(cliente,1);
	return charToInt(handshake);
}

// FIN 5


// 6. Conexion a Swap
void handshakearASwap(){
	char *hand = string_from_format("%c%c",HeaderHandshake,SOYUMC);
	send_w(cliente, hand, 2);

	log_debug(bgLogger,"Umc handshakeo.");
	if(getHandshake()!=SOYSWAP)
	{
		perror("Se esperaba que la umc se conecte con el swap.");
	}
	else
		log_debug(bgLogger,"Umc recibio handshake de Swap.");
}

void conectarASwap(){
	direccion = crearDireccionParaCliente(PUERTO_SWAP);
	cliente = socket_w();
	connect_w(cliente, &direccion);

	handshakearASwap();
}

void realizarConexionASwap()
{
	conectarASwap();
	log_info(activeLogger,"Conexion a swap correcta :).");
	handshakearASwap();
	log_info(activeLogger,"Handshake finalizado exitosamente.");
	log_debug(bgLogger,"Esperando algo para imprimir en pantalla.");
}

void escucharPedidosDeSwap(){
	char* header;
	while(true){
		if (cliente!=0){ // Solo si esta conextado
			//header = recv_waitall_ws(cliente,sizeof(char)); ESTO NO ME PERMITE CHEQUEAR SI SE DESCONECTO!!
			header = malloc(1);
			int bytesRecibidos = recv(cliente, header, 1, MSG_WAITALL);
			if (bytesRecibidos <= 0){
				 printf("SWAP se desconecto\n");
				 close(cliente);
				 cliente=0;
				 return;
			}
			else
				procesarHeader(cliente,header);

			free(header);
		}
	}
}

void conexionASwap(){ //Creada para unir las dos funciones y crear un hilo
	realizarConexionASwap();
	escucharPedidosDeSwap();
}
// FIN 6




/*Ari's tips
 Ro, yo lo veo así al asunto, las páginas no existen antes que te pidan algo,
 sólo vas a tener la tabla de marcos que es la memoria real,  la tabla de páginas
 para mi debería ser una lista, ya que para cada proceso vamos a saber en tiempo
 de ejecución cuantas páginas necesita. Una vez que te piden espacio creas una tabla
 de paginas(lista) y recorres el array de marcos y cada espacio que encontras
 (no necesariamente contiguo) agregas un nodo a la lista con presencia 1, una vez que
 terminaste el array de marcos y faltan páginas por ubicar llamas al swap para ver si
 existe espacio en el almacenamiento secundario (y el swap hace algo parecido y te responde),
 esos nodos van a tener presencia 0
finalmente respondes si pudiste o no ubicar la cantidad de páginas solicitadas
 */

/*
 Entonces, cada vez que me piden paginas creo una nueva lista de paginas para ese PID

 Los marcos sin usar los tengo en un cola, asi es mas rapdio, para agarrar uno: queue_pop(marcosLibres)

 struct marco{
     int indice;
     int uso;
     void* contenido;
}


 */