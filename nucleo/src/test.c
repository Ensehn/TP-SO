/*
 * test.c
 *
 *  Created on: 25/5/2016
 *      Author: utnso
 */
#include <CUnit/Basic.h>
#include "nucleo.h"

void test_cicloDeVidaProcesos() {
	log_debug(bgLogger, "INICIO test_cicloDeVidaProcesos");
	int consola = 1, cpu = 2;
	queue_push(colaCPU, (void*) cpu);

	t_proceso* proceso = (t_proceso*) crearProceso(consola);

	cambiarEstado(proceso,READY);

	ejecutarProceso(proceso->PCB->PID, (int) queue_pop(colaCPU));

	CU_ASSERT_EQUAL(proceso->estado, EXEC);
	CU_ASSERT_EQUAL(proceso->cpu, 2)
	CU_ASSERT_TRUE(queue_is_empty(colaCPU));

	t_IO* io = malloc(sizeof(t_IO));
	io->retardo = 1;
	io->cola = queue_create();
	io->estado = INACTIVE;
	dictionary_put(tablaIO, "ScannerTest1", io);

	bloquearProceso(proceso->PCB->PID, "ScannerTest1");

	CU_ASSERT_FALSE(queue_is_empty(colaCPU));
	CU_ASSERT_EQUAL(proceso->estado, BLOCK);

	//queue_pop(io->cola);
	io->estado = ACTIVE;
	t_bloqueo* info = malloc(sizeof(t_bloqueo));
	info->IO = io;
	info->PID = proceso->PCB->PID;
	bloqueo(info);

	//desbloquearProceso(proceso->PCB->PID);
	CU_ASSERT_EQUAL(proceso->estado, READY);

	finalizarProceso(proceso->PCB->PID);

	CU_ASSERT_EQUAL(proceso->estado, EXIT);
	CU_ASSERT_FALSE(queue_is_empty(colaSalida));

	destruirProceso(proceso->PCB->PID);
	CU_ASSERT_TRUE(list_is_empty(listaProcesos));

	queue_clean(colaSalida);
	queue_clean(colaListos);
	queue_clean(colaCPU);
	list_clean(listaProcesos);
	// NO TENEMOS MANERA DE AVISARLE A LA QUEUE QUE SAQUE EL PID DE LA POSICION DONDE SE ENCUENTRA
	// HABRA QUE CHEQUEAR CADA VEZ QUE SE SACA UN ELEMENTO SI ESTE REALMENTE EXISTE EN EL SISTEMA
	//CU_ASSERT_TRUE(queue_is_empty(colaSalida));
	//CU_ASSERT_TRUE(queue_is_empty(colaListos));
	dictionary_remove(tablaIO, "ScannerTest1");
	queue_destroy(io->cola);
	free(io);
	log_debug(bgLogger, "FIN test_cicloDeVidaProcesos");
}
void test_bloqueosIO() {
	log_debug(bgLogger, "INICIO test_bloqueosIO()");
	int consola = 1, cpu = 2;
	queue_push(colaCPU, (void*) cpu);

	t_IO* io = malloc(sizeof(t_IO));
	io->retardo = 1;
	io->cola = queue_create();
	io->estado = INACTIVE;
	dictionary_put(tablaIO, "ScannerTest2", io);

	t_proceso* proceso = (t_proceso*) crearProceso(consola);
	proceso->estado = READY;

	ejecutarProceso(proceso->PCB->PID, (int) queue_pop(colaCPU));
	bloquearProceso(proceso->PCB->PID, "ScannerTest2");
	dictionary_iterator(tablaIO, (void*) planificarIO);
	CU_ASSERT_EQUAL(io->estado, ACTIVE);
	sleep(io->retardo * 2);
	CU_ASSERT_EQUAL(proceso->estado, READY);
	CU_ASSERT_EQUAL(io->estado, INACTIVE);
	dictionary_remove(tablaIO, "ScannerTest2");
	queue_destroy(io->cola);
	free(io);
	finalizarProceso(proceso->PCB->PID);
	destruirProceso(proceso->PCB->PID);
	queue_clean(colaSalida);
	queue_clean(colaListos);
	queue_clean(colaCPU);
	list_clean(listaProcesos);
	log_debug(bgLogger, "FIN test_bloqueosIO()");
}
void test_obtenerMetadata() {
	log_debug(bgLogger, "INICIO test_obtenerMetadata()");
	t_proceso* proceso = malloc(sizeof(t_proceso));
	t_sentencia* sentencia;
	proceso->PCB = pcb_create();
	asignarMetadataProceso(proceso,
			"begin\nvariables a, b\na = 3\n:salto1\nb = 5\n:salto2\na = b + 12\nend\n");
	sentencia = (t_sentencia*) list_get(proceso->PCB->indice_codigo, 0);
	CU_ASSERT_EQUAL(sentencia->offset_inicio, 6);
	CU_ASSERT_EQUAL(sentencia->offset_fin, 6 + 15);
	sentencia = (t_sentencia*) list_get(proceso->PCB->indice_codigo, 1);
	CU_ASSERT_EQUAL(sentencia->offset_inicio, 21);
	CU_ASSERT_EQUAL(sentencia->offset_fin, 21 + 6);
	CU_ASSERT_EQUAL(
			(*(int* )dictionary_get(proceso->PCB->indice_etiquetas, "salto1")),
			2);
	CU_ASSERT_EQUAL(
			(*(int* )dictionary_get(proceso->PCB->indice_etiquetas, "salto2")),
			3);
	free(sentencia);
	pcb_destroy(proceso->PCB);
	free(proceso);
	log_debug(bgLogger, "FIN test_obtenerMetadata()");
}
int test_nucleo() {
	log_info(activeLogger, "INICIANDO TESTS DE NUCLEO");
	CU_initialize_registry();
	CU_pSuite suite_nucleo = CU_add_suite("Suite de Nucleo", NULL, NULL);
	CU_add_test(suite_nucleo, "Test del ciclo de vida de los procesos",
			test_cicloDeVidaProcesos);
	CU_add_test(suite_nucleo, "Test de obtencion de la metadata",
			test_obtenerMetadata);
	CU_add_test(suite_nucleo, "Test de bloqueos [Puede tardar un poco]",
			test_bloqueosIO);
	CU_basic_set_mode(CU_BRM_VERBOSE);
	CU_basic_run_tests();
	CU_cleanup_registry();
	log_info(activeLogger, "FINALIZADO TESTS DE NUCLEO");
	return CU_get_error();
}
int testear(int(*suite)(void)){
	if (suite() != CUE_SUCCESS) {
		printf("%s", CU_get_error_msg());
		return EXIT_FAILURE;
	}
	return 0;
}