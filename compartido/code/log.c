/*
 * log.c
 *
 *  Created on: 25/4/2016
 *      Author: utnso
 */

#include <stdio.h>
#include <stdlib.h>
#include <commons/string.h>
#include <commons/log.h>
#include "log.h"

void iniciarLog()
{
	log_info(bgLogger,"\n\n***** Log de %s *****\n\n",bgLogger->program_name);
}

/**
 * logLevel:
 * LOG_PRINT_NOTHING
 * LOG_PRINT_DEFAULT
 * LOG_PRINT_ALL
 */
void crearLogs(char* logname, char* procName, int logLevel)
{
	bool activeLoggerIsActive = true;
	bool bgLoggerIsActive = false;
	if(logLevel>=3 || logLevel <0){
		perror("LogLevel >=3 || logLevel <0.");
	}else{
		switch(logLevel){
		case 0: activeLoggerIsActive=false;break;
		case 1: break; // default.
		case 2: bgLoggerIsActive=true; break;
		}
	}
	activeLogger = log_create(string_from_format("%s.log",logname),procName,activeLoggerIsActive,LOG_LEVEL_INFO);
	bgLogger = log_create(string_from_format("%s.log",logname),procName,bgLoggerIsActive,LOG_LEVEL_DEBUG);
	iniciarLog();
}

void destruirLogs()
{
	log_destroy(activeLogger);
	log_destroy(bgLogger);
}
