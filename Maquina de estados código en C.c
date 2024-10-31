#include <stdio.h>
#include <stdlib.h>

// Definición de estados de la máquina
typedef enum {
    ESTADO_INIT,
    ESTADO_CERRADO,
    ESTADO_ABIERTO,
    ESTADO_CERRANDO,
    ESTADO_ABRIENDO,
    ESTADO_ERROR
} Estado;

// Constantes para los errores y tiempo máximo de ejecución
#define TIEMPO_MAX 180
#define ERROR_OK 0
#define ERROR_LIMITE 1
#define ERROR_TIEMPO 2
#define ERROR_INICIO 3

// Estructura de control de I/O para la puerta
typedef struct {
    int sensorCerrado;      // Sensor de límite cerrado
    int sensorAbierto;      // Sensor de límite abierto
    int botonActivacion;    // Pulso de activación
    int motorAbrir;         // Motor en abrir
    int motorCerrar;        // Motor en cerrar
    int contadorTiempo;     // Contador de tiempo
    int ledAbrir;           // LED indicador de abriendo
    int ledCerrar;          // LED indicador de cerrando
    int ledError;           // LED indicador de error
    int codigoError;        // Código de error
    int datosListos;        // Confirma datos listos para iniciar
} ControlIO;

// Prototipos de las funciones para cada estado
Estado estado_init(ControlIO *io);
Estado estado_abierto(ControlIO *io);
Estado estado_abriendo(ControlIO *io);
Estado estado_cerrado(ControlIO *io);
Estado estado_cerrando(ControlIO *io);
Estado estado_error(ControlIO *io, int error);

// Función principal de la máquina de estados
int main() {
    Estado estadoActual = ESTADO_INIT;
    ControlIO io = {0}; // Inicializamos todos los valores de control a 0

    while (1) {
        switch (estadoActual) {
            case ESTADO_INIT:     estadoActual = estado_init(&io); break;
            case ESTADO_CERRADO:  estadoActual = estado_cerrado(&io); break;
            case ESTADO_ABIERTO:  estadoActual = estado_abierto(&io); break;
            case ESTADO_ABRIENDO: estadoActual = estado_abriendo(&io); break;
            case ESTADO_CERRANDO: estadoActual = estado_cerrando(&io); break;
            case ESTADO_ERROR:    estadoActual = estado_error(&io, io.codigoError); break;
        }
    }
    return 0;
}

// Estado INIT: inicialización del sistema
Estado estado_init(ControlIO *io) {
    io->motorAbrir = io->motorCerrar = 0;
    io->ledAbrir = io->ledCerrar = io->ledError = 0;
    io->contadorTiempo = 0;
    io->codigoError = ERROR_OK;

    printf("Inicializando el sistema...\n");

    if (io->sensorCerrado && io->sensorAbierto) {
        printf("ERROR: Sensores contradictorios detectados.\n");
        return ESTADO_ERROR;
    }
    
    if (!io->datosListos) {
        printf("Esperando datos iniciales...\n");
        return ESTADO_INIT;
    }

    if (io->sensorCerrado) {
        printf("Puerta en posición cerrada.\n");
        return ESTADO_CERRADO;
    }
    return ESTADO_CERRANDO;
}

// Estado ABIERTO: puerta completamente abierta
Estado estado_abierto(ControlIO *io) {
    io->motorAbrir = 0;
    io->ledAbrir = 0;
    printf("Puerta abierta.\n");

    if (io->botonActivacion) {
        io->botonActivacion = 0;
        return ESTADO_CERRANDO;
    }
    return ESTADO_ABIERTO;
}

// Estado ABRIENDO: puerta en proceso de apertura
Estado estado_abriendo(ControlIO *io) {
    io->motorAbrir = 1;
    io->ledAbrir = 1;
    io->contadorTiempo++;

    if (io->sensorAbierto) {
        printf("Puerta completamente abierta.\n");
        return ESTADO_ABIERTO;
    }

    if (io->contadorTiempo > TIEMPO_MAX) {
        printf("ERROR: Tiempo máximo de apertura excedido.\n");
        io->codigoError = ERROR_TIEMPO;
        return ESTADO_ERROR;
    }
    return ESTADO_ABRIENDO;
}

// Estado CERRADO: puerta completamente cerrada
Estado estado_cerrado(ControlIO *io) {
    io->motorCerrar = 0;
    io->ledCerrar = 0;
    printf("Puerta cerrada.\n");

    if (io->botonActivacion) {
        io->botonActivacion = 0;
        return ESTADO_ABRIENDO;
    }
    return ESTADO_CERRADO;
}

// Estado CERRANDO: puerta en proceso de cierre
Estado estado_cerrando(ControlIO *io) {
    io->motorCerrar = 1;
    io->ledCerrar = 1;
    io->contadorTiempo++;

    if (io->sensorCerrado) {
        printf("Puerta completamente cerrada.\n");
        return ESTADO_CERRADO;
    }

    if (io->contadorTiempo > TIEMPO_MAX) {
        printf("ERROR: Tiempo máximo de cierre excedido.\n");
        io->codigoError = ERROR_TIEMPO;
        return ESTADO_ERROR;
    }
    return ESTADO_CERRANDO;
}

// Estado ERROR: manejo de errores y mensajes al usuario
Estado estado_error(ControlIO *io, int error) {
    io->motorAbrir = io->motorCerrar = 0;
    io->ledAbrir = io->ledCerrar = 0;
    io->ledError = 1;

    switch (error) {
        case ERROR_LIMITE:
            printf("ERROR: Sensores de límite en conflicto.\n");
            break;
        case ERROR_TIEMPO:
            printf("ERROR: Tiempo máximo alcanzado. Revise el portón.\n");
            break;
        case ERROR_INICIO:
            printf("ERROR: Fallo en la inicialización del sistema.\n");
            break;
        default:
            printf("ERROR DESCONOCIDO: Revise el sistema.\n");
            break;
    }

    if (io->botonActivacion) {
        io->botonActivacion = 0;
        io->codigoError = ERROR_OK;
        printf("Restableciendo el sistema...\n");
        return ESTADO_INIT;
    }
    return ESTADO_ERROR;
}

