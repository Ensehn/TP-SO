/*
 * cpu.c
 *  Created on: 16/4/2016
 *      Author: utnso
 */

#include "cpu.h"

/*----- Operaciones sobre el PC y avisos por quantum -----*/
void setearPC(t_PCB* pcb, int pc) {
	log_info(activeLogger, "Actualizando PC de |%d| a |%d|.", pcb->PC, pc);
	pcb->PC = pc;
}

void incrementarPC(t_PCB* pcb) {
	setearPC(pcb, (pcb->PC) + 1);
}

void informarInstruccionTerminada() {
	// Le aviso a nucleo que termino una instruccion, para que calcule cuanto quantum le queda al proceso ansisop.
	enviarHeader(cliente_nucleo,headerTermineInstruccion);
	// Acá nucleo tiene que mandarme el header que corresponda, segun si tengo que seguir ejecutando instrucciones o tengo que desalojar.
	// Eso se procesa en otro lado, porque la ejeución de instrucciones esta anidada en un while
	// por lo que no tengo que recibir el header aca
}

void instruccionTerminada(char* instr) {
	log_debug(activeLogger, "La instruccion |%s| finalizó OK.", instr);
	informarInstruccionTerminada();
}

void desalojarProceso() {
	char* pcb = NULL;
	int size = serializar_PCB(pcb, pcbActual);
	send_w(cliente_nucleo, pcb, size); //Envio a nucleo el PCB con el PC actualizado.
	// Nucleo no puede hacer pbc->pc+=quantum porque el quantum puede variar en tiempo de ejecución.

	free(pcb);
}

/*--------FUNCIONES----------*/
void esperar_programas() {
	log_debug(bgLogger, "Esperando programas de nucleo.");
	char* header;
	if (config.DEBUG_NO_PROGRAMS) {
		log_debug(activeLogger,
				"DEBUG NO PROGRAMS activado! Ignorando programas...");
	}else{
		while (!terminar) {	//mientras no tenga que terminar
			header = recv_waitall_ws(cliente_nucleo, 1);
			procesarHeader(header);
			free(header);
		}
	}
	log_debug(bgLogger, "Ya no se esperan programas de nucleo.");
}

void procesarHeader(char *header) {

	log_debug(bgLogger, "Llego un mensaje con header %d.", charToInt(header));

	switch (charToInt(header)) {

	case HeaderError:
		log_error(activeLogger, "Header de Error.");
		break;

	case HeaderHandshake:
		log_error(activeLogger,
				"Segunda vez que se recibe un headerHandshake acá!.");
		exit(EXIT_FAILURE);
		break;

	case HeaderPCB:
		obtenerPCB(); //inicio el proceso de aumentar el PC, pedir a UMC sentencia...
		break;

	case HeaderSentencia:
		obtener_y_parsear();
		break;

	case HeaderDesalojarProceso:
		desalojarProceso();
		break;

	case HeaderExcepcion:
		lanzar_excepcion_overflow();
		break;

	default:
		log_error(activeLogger, "Llego cualquier cosa.");
		log_error(activeLogger,
				"Llego el header numero |%d| y no hay una accion definida para el.",
				charToInt(header));
		exit(EXIT_FAILURE);
		break;
	}
}

void pedir_tamanio_paginas() {
	if (!config.DEBUG_IGNORE_UMC) {

		enviarHeader(cliente_umc,HeaderTamanioPagina); //le pido a umc el tamanio de las paginas
		char* tamanio = recv_nowait_ws(cliente_umc, sizeof(int)); //recibo el tamanio de las paginas

		tamanioPaginas = char4ToInt(tamanio);
		log_debug(activeLogger, "El tamaño de paginas es: |%d|",tamanioPaginas);
		free(tamanio);

	} else {
		tamanioPaginas = -1;
		log_debug(activeLogger,
				"UMC DEBUG ACTIVADO! tamanioPaginas va a valer -1.");
	}
}


int longitud_sentencia(t_sentencia* sentencia) {
	return sentencia->offset_fin - sentencia->offset_inicio; // para la otra interpretacion de esto, sumar 1 aca y listo!
}

/**
 * Recibo el offset absoluto y lo transformo en (numeroPagina, destino->offset_inicio, destino->offset_fin).
 * Ej: (21,40) -> (5,1,20)
 */
int obtener_offset_relativo(t_sentencia* fuente, t_sentencia* destino) {
	int offsetAbsoluto = fuente->offset_inicio;
	int paginaInicio = (int) (offsetAbsoluto / tamanioPaginas); // Pagina donde inicia la sentencia
	int offsetRelativo = offsetAbsoluto % tamanioPaginas; //obtengo el offset relativo

	destino->offset_inicio = offsetRelativo;
	destino->offset_fin = offsetRelativo + longitud_sentencia(fuente);

	return paginaInicio;
}

/**
 * Envia a UMC: pag, offest y tamaño, es decir, un t_pedido.
 */
void enviar_solicitud(int pagina, int offset, int size) {
	t_pedido pedido;
	pedido.offset = offset;
	pedido.pagina = pagina;
	pedido.size = size;

	char* solicitud = string_new();
	int tamanio = serializar_pedido(solicitud, &pedido);

	send_w(cliente_umc, solicitud, tamanio);
	log_info(activeLogger,"Solicitud enviada: (nroPag,offsetInicio,tamaño) = (%d,%d,%d)", pagina, offset, size);
	free(solicitud);
}

t_sentencia* obtener_sentencia_relativa(int* paginaInicioSentencia){
	t_sentencia* sentenciaAbsoluta = list_get(pcbActual->indice_codigo, pcbActual->PC);	//obtengo el offset de la sentencia
	t_sentencia* sentenciaRelativa = malloc(sizeof(t_sentencia));
	(*paginaInicioSentencia) = obtener_offset_relativo(sentenciaAbsoluta, sentenciaRelativa); //obtengo el offset relativo
	free(sentenciaAbsoluta);
	return sentenciaRelativa;
}

/**
 * Si no le ponia el _ me daba error :(
 */
int maximo_(int a, int b){
	return a>b?a:b;
}

/**
 * Segun la longitud restante, determina si la proxima pagina a pedir corresponde
 * completamente a la sentencia que estamos manejando. Si correspondiese solo una parte o fuese
 * toda de otra sentencia, retorna false.
 */
bool paginaCompleta(t_sentencia* sentenciaRelativa, int longitud_restante){
	return (sentenciaRelativa->offset_fin > tamanioPaginas
					|| longitud_restante >= tamanioPaginas);
}

/**
 * Pide una pagina entera a UMC
 */
void pedirPaginaCompleta(int pagina){
	enviar_solicitud(pagina, 0, tamanioPaginas);
}

void pedirPrimeraSentencia(t_sentencia* sentenciaRelativa, int pagina, int* longitud_restante){
	int tamanioPrimeraSentencia = maximo_(*longitud_restante,
				tamanioPaginas - sentenciaRelativa->offset_inicio); //llega hasta su final o hasta que se termine la pagina, lo mas pequeño
	enviar_solicitud(pagina, sentenciaRelativa->offset_inicio, tamanioPrimeraSentencia);
	(*longitud_restante) -= tamanioPrimeraSentencia;
}

void pedirUltimaSentencia(t_sentencia* sentenciaRelativa, int pagina, int longitud_restante){
	enviar_solicitud(pagina, 0, longitud_restante); //Desde el inicio, con tamaño identico a lo que me falta leer.
}

/**
 * Envia a UMC: cantidad de paginas (cantRecvs) que voy a pedir,
 * t_pedido_1 <---- Setendo el offset de inicio correctamente, y el de fin tambien si terminase en esta pagina.
 * t_pedido_2, ...., t_pedido_n-1 <---- paginas que se piden completas
 * t_pedido_n <---- Si no es la pagina completa, setea el offset fin correcto para no pedir de mas.
 */
void pedir_sentencia() {	//pedir al UMC la proxima sentencia a ejecutar
	log_info(activeLogger, "Iniciando pedido de sentencia...");
	int paginaAPedir; // Lo inicializa obtener_sentencia_relativa
	t_sentencia* sentenciaRelativa = obtener_sentencia_relativa(&paginaAPedir);
	int longitud_restante = longitud_sentencia(sentenciaRelativa); //longitud de la sentencia que aun no pido
	enviarHeader(cliente_umc, HeaderSolicitudSentencia); //envio el header
	
	// Pido la primera pagina, empezando donde corresponde y terminando donde corresponda.
	pedirPrimeraSentencia(sentenciaRelativa, paginaAPedir, &longitud_restante);
	paginaAPedir++;
	
	//Si falta pedir paginas, pido todas las paginas que correspondan totalmente a la sentencia que quiero
	while(paginaCompleta(sentenciaRelativa,longitud_restante)){
		pedirPaginaCompleta(paginaAPedir);
		longitud_restante -= tamanioPaginas; // Le resto el tamaño de la pagina que pedi, la cual esta completa.
		paginaAPedir++;
	}
	
	// Si quedase una pagina sin pedir, por no estar completa la ultima pagina, la pido.
	pedirUltimaSentencia(sentenciaRelativa, paginaAPedir, longitud_restante);
	paginaAPedir++;

	log_info(activeLogger, "Pedido de sentencia finalizado.");
	log_info(activeLogger, "Se pidieron %d paginas (estando la primera y la ultima no necesariamente completas)", paginaAPedir);
	free(sentenciaRelativa);
}

void obtenerPCB() {		//recibo el pcb que me manda nucleo
	char* pcb = recv_waitall_ws(cliente_nucleo, sizeof(t_PCB));
	deserializar_PCB(pcbActual, pcb);//reemplazo en el pcb actual de cpu que tiene como variable global

	stack = pcbActual->SP;

	free(pcb);
}

void enviarPCB() {
	char* pcb = string_new();
	serializar_PCB(pcb, pcbActual);

	send_w(cliente_nucleo, pcb, sizeof(t_PCB));
	free(pcb);
}

/**
 * Llamo a la funcion analizadorLinea del parser y logeo
 */
void parsear(char* const sentencia) {
	log_info(activeLogger, "Ejecutando la sentencia |%s|...", sentencia);
	analizadorLinea(sentencia, &funciones, &funcionesKernel);
}

/**
 * Recibo la sentencia previamente pedida.
 */
char* recibir_sentencia(){
	char* tamanioSentencia = recv_waitall_ws(cliente_umc, sizeof(int));
	int tamanio = char4ToInt(tamanioSentencia);
	free(tamanioSentencia);
	return recv_waitall_ws(cliente_umc, tamanio);
}

void obtener_y_parsear() {
	pedir_sentencia();
	char* sentencia = recibir_sentencia();
	parsear(sentencia);
	free(sentencia);
}


void finalizar_proceso(){ //voy a esta funcion cuando ejecuto la ultima instruccion o hay una excepcion
	enviarHeader(cliente_nucleo, HeaderTerminoProceso);
	enviarPCB();		//nucleo deberia recibir el PCB para elminar las estructuras
}

/**
 * Lanza excepcion por stack overflow y termina el proceso.
 */
void lanzar_excepcion_overflow(){
	log_info(activeLogger,"Stack overflow! se intentó leer una dirección inválida.");
	log_info(activeLogger,"Terminando la ejecución del programa actual...");

	finalizar_proceso();

	log_info(activeLogger,"Proceso terminado!");
}



// ***** Funciones de inicializacion y finalizacion ***** //
void cargarConfig() {
	t_config* configCPU;
	configCPU = config_create("cpu.cfg");
	config.puertoNucleo = config_get_int_value(configCPU, "PUERTO_NUCLEO");
	config.ipNucleo = config_get_string_value(configCPU, "IP_NUCLEO");
	config.puertoUMC = config_get_int_value(configCPU, "PUERTO_UMC");
	config.ipUMC = config_get_string_value(configCPU, "IP_UMC");
	config.DEBUG_IGNORE_UMC = config_get_int_value(configCPU, "DEBUG_IGNORE_UMC");
	config.DEBUG_NO_PROGRAMS = config_get_int_value(configCPU, "DEBUG_NO_PROGRAMS");
	config.DEBUG_RAISE_LOG_LEVEL = config_get_int_value(configCPU, "DEBUG_RAISE_LOG_LEVEL");
	config.DEBUG_RUN_TEST = config_get_int_value(configCPU, "DEBUG_RUN_TEST");
}

void inicializar() {
	cargarConfig();
	pcbActual = malloc(sizeof(t_PCB));
	crearLogs(string_from_format("cpu_%d", getpid()), "CPU", config.DEBUG_RAISE_LOG_LEVEL);
	log_info(activeLogger, "Soy CPU de process ID %d.", getpid());
	inicializar_primitivas();
}

void finalizar() {
	log_info(activeLogger,"Finalizando proceso cpu");

	if (!config.DEBUG_NO_PROGRAMS) {
		log_debug(activeLogger, "Destruyendo pcb...");
		pcb_destroy(pcbActual);
		log_debug(activeLogger, "PCB Destruido...");
	}
	liberar_primitivas();
	destruirLogs();

	close(cliente_nucleo);
	close(cliente_umc);
	exit(EXIT_SUCCESS);

	log_info(activeLogger,"Proceso cpu finalizado");
}

/*------------otras------------*/
/**
 * Para sigusr1 => terminar=1
 *
 */
void handler(int sign) {
	if (sign == SIGUSR1) {
		log_info(activeLogger, "Recibi SIGUSR1! Adios a todos!");
		terminar = true; //Setea el flag para que termine CPU al terminar de ejecutar la instruccion
		log_info(activeLogger, "Esperando a que termine la ejecucion del programa actual...");
	}
}

/**
 * Inicializa los tests si estan habilitados (1) por configuracion.
 */
void correrTests(){
	if(config.DEBUG_RUN_TEST){
		testear(test_cpu);
	}
}

int main() {

	signal(SIGUSR1,handler); //el progama sabe que cuando se recibe SIGUSR1,se ejecuta handler

	inicializar();

	//tests
	correrTests();

	//conectarse a umc
	establecerConexionConUMC();

	//conectarse a nucleo
	establecerConexionConNucleo();

	pedir_tamanio_paginas();

	//CPU se pone a esperar que nucleo le envie PCB
	esperar_programas();

	finalizar();
	return EXIT_SUCCESS;
}
