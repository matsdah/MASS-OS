#include <stdint.h>
#include "lib.h"
#include "moduleLoader.h"
#include "naiveConsole.h"

static void loadModule(uint8_t ** module, void * targetModuleAddress);
static uint32_t readUint32(uint8_t ** address);

// Carga N módulos desde el payload a direcciones destino
void loadModules(void * payloadStart, void ** targetModuleAddress){
	uint32_t i;
	uint8_t * currentModule = (uint8_t*)payloadStart;
	uint32_t moduleCount = readUint32(&currentModule);

	for(i = 0; i < moduleCount; i++){
		loadModule(&currentModule, targetModuleAddress[i]);
	}
}

// Copia un módulo (prefijado por su tamaño en bytes)
static void loadModule(uint8_t ** module, void * targetModuleAddress){
	uint32_t moduleSize = readUint32(module);
	memcpy(targetModuleAddress, *module, moduleSize);
	*module += moduleSize;
}

// Lee un uint32 little-endian y avanza el puntero
static uint32_t readUint32(uint8_t ** address){
	uint32_t result = *(uint32_t*)(*address);
	*address += sizeof(uint32_t);
	return result;
}
