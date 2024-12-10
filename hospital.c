#include <stdio.h>

#include <stdlib.h>

#include <pthread.h>

#include <semaphore.h>

#include <unistd.h>

#include <signal.h>

#include <sys/wait.h>

#include <stdbool.h>

#include <mqueue.h>

#include <string.h>

// MACROS
#define MQ_NAME "/mq_cola" // Nombre de la cola de mensajes
#define SEM_NAME "/semaforo" // Nombre del semáforo
#define ARR_SIZE 128

struct mq_attr attributes = {
  .mq_flags = 0, // flags de la cola (ignorados por mq_open())
  .mq_maxmsg = 10, // Número máximo de mensajes en la cola
  .mq_curmsgs = 0, // Número actual de mensajes en la cola (ignorados por mq_open())
  .mq_msgsize = sizeof(char) * ARR_SIZE // Tamano máximo de cada mensaje
};

// VARIABLES GLOBALES
mqd_t mqd; // Cola de mensajes

sem_t sem_diag; // Semáforo para el diagnóstico
sem_t sem_farm; // Semáforo para la farmacia

int pacientes_dados_de_alta = 0; // Contador de pacientes dados de alta
int nuevos_pacientes = 0; // Contador de nuevos pacientes

pthread_t exp_thr, farm_thr, diag_thr; // Identificadores de los hilos

pid_t pid_hospital, pid_recepcion; // Identificadores de los procesos padre e hijo

// FUNCIONES

// Función para generar un número aleatorio entre un rango
int tiempo_aleatorio(int min, int max) {
  return rand() % (max - min + 1) + min;
}

// Función para manejar la senal SIGINT (interrupción)
void sigint(int sig) {
	 
  mq_close(mqd); // Cierra la cola de mensajes
  mq_unlink(MQ_NAME); // Elimina la cola de mensajes
  sem_destroy( &sem_farm); // Destruye el semáforo de farmacia
  sem_destroy( &sem_diag); // Destruye el semáforo de diagnóstico
  sem_unlink(SEM_NAME); // Elimina el semáforo
  printf("El padre ha terminado\n");
  exit(0);
}

// Función para finalizar el proceso hijo
void endProcess(int sig) {
  printf("El hijo ha terminado\n");
  exit(0);
}

void endHospital(int sig) {
  	 pthread_cancel(exp_thr);
  pthread_cancel(diag_thr);
  pthread_cancel(farm_thr);

  
      // El padre espera que los hilos finalicen
      pthread_join(exp_thr, NULL);
      pthread_join(diag_thr, NULL);
      pthread_join(farm_thr, NULL);

  printf("El hijo ha terminado\n");
  exit(0);
}


// Función para notificar a la recepción (envía la senal SIGUSR1)
void notificaRecepcion(int sig) {
  signal(SIGUSR1, notificaRecepcion); // Recibe senales SIGUSR1
  kill(pid_recepcion, SIGUSR1); // Envía la senal SIGUSR1 al proceso de recepción
}

// Función para actualizar el número de pacientes dados de alta
void pacientesConMedicacion(int sig) {
  signal(SIGUSR1, pacientesConMedicacion); // Espera para la senal de nuevo
  pacientes_dados_de_alta += 1; // Incrementa el contador de pacientes dados de alta
  printf("Pacientes dados de alta: %d\n", pacientes_dados_de_alta);
}

//----------------------------------------------------------------

// Función del hilo de exploración
void * exploracion(void * args) {
  printf("[ExploraciÃ³n] Comienzo mi ejecuciÃ³n...\n");
  // Abre la cola de mensajes en modo solo lectura
  mqd_t mqd = mq_open(MQ_NAME, O_RDONLY | O_CREAT, 0644, &attributes);

  while (1) {
    char paciente[ARR_SIZE];
    signal(SIGUSR1, SIG_IGN); // Ignora senales SIGUSR1
    printf("[ExploraciÃ³n] Esperando a un paciente...\n");
    mq_receive(mqd, paciente, sizeof(char) * ARR_SIZE, NULL); // Recibe un mensaje de la cola
    printf("[ExploraciÃ³n] Recibido paciente: %s. Realizando exploraciÃ³n...\n", paciente);

    // Simula el tiempo de exploración
    sleep(tiempo_aleatorio(1, 3));

    printf("[ExploraciÃ³n] ExploraciÃ³n completa con paciente %s. Notificando diagnÃ³stico...\n", paciente);

    // Desbloquea el semáforo para que el hilo de diagnóstico continúe
    sem_post( &sem_diag);
  }
}

// Función del hilo de diagnóstico
void * diagnostico(void * args) {
  printf("[DiagnÃ³stico] Comienzo mi ejecuciÃ³n...\n");

  while (1) {
    signal(SIGUSR1, SIG_IGN); // Ignora senales SIGUSR1
    sem_wait( &sem_diag); // Decrementa el valor del semáforo (espera hasta que se libere)

    printf("[DiagnÃ³stico] Realizando pruebas diagnÃ³sticas...\n");
    sleep(tiempo_aleatorio(5, 10)); // Simula el tiempo de las pruebas de diagnóstico

    printf("[DiagnÃ³stico] DiagnÃ³stico completado. Notificando farmacia...\n");

    // Desbloquea el semáforo de farmacia para continuar el proceso en farmacia
    sem_post( &sem_farm);
  }
}

// Función del hilo de farmacia
void * farmacia(void * args) {
  signal(SIGUSR1, SIG_IGN); // Ignora senales SIGUSR1
  printf("[Farmacia] Comienzo mi ejecuciÃ³n...\n");

  while (1) {
    sem_wait( &sem_farm); // Decrementa el semáforo, esperando que el diagnóstico esté listo

    printf("[Farmacia] Preparando medicaciÃ³n...\n");
    sleep(tiempo_aleatorio(1, 3)); // Simula el tiempo de preparación de la medicación

    printf("[Farmacia] MedicaciÃ³n lista. Enviando seÃ±al de alta...\n");

    // Notifica a la recepción de que la medicación está lista
    kill(pid_recepcion, SIGUSR1);
  }
}

void main(int argv, char * argc[]) {
  sem_init( &sem_diag, 0, 0); // Inicializa el semáforo para diagnóstico con valor 0 (bloqueado)
  sem_init( &sem_farm, 0, 0); // Inicializa el semáforo para farmacia con valor 0 (bloqueado)

  pid_recepcion = fork(); // Crea un proceso hijo para la recepción

  if (pid_recepcion != 0) { // Proceso padre
    pid_hospital = fork(); // Crea un proceso hijo para el hospital
    if (pid_hospital != 0) { // Proceso abuelo (padre de hospital)
      int status;
      signal(SIGINT, sigint); // Configura la senal SIGINT (Ctrl+C) para manejarla
      for (int i = 0; i < 2; i++) { // El padre espera a sus dos hijos (hospital y recepción)
        pid_t child_pid = wait( &status); // Espera quelos hijos terminen
      }
    } else {
      // Proceso hospital
      signal(SIGINT, endHospital); // Maneja la senal SIGINT
      signal(SIGUSR1, notificaRecepcion); // Configura la senal SIGUSR1 para notificar a la recepción
      printf("[Hospital] Comienzo mi ejecuciÃ³n...\n");

      // Crea los hilos para exploración, diagnóstico y farmacia
      int exp = pthread_create( &exp_thr, NULL, exploracion, NULL);
      if (exp != 0) {
        printf("ERROR; return code from pthread_create() is %d\n", exp);
        exit(EXIT_FAILURE);
      }

      int diag = pthread_create( &diag_thr, NULL, diagnostico, NULL);
      if (diag != 0) {
        printf("ERROR; return code from pthread_create() is %d\n", diag);
        exit(EXIT_FAILURE);
      }

      int farm = pthread_create( &farm_thr, NULL, farmacia, NULL);
      if (farm != 0) {
        printf("ERROR; return code from pthread_create() is %d\n", farm);
        exit(EXIT_FAILURE);
      }

      // El padre espera que los hilos finalicen
      pthread_join(exp_thr, NULL);
      pthread_join(diag_thr, NULL);
      pthread_join(farm_thr, NULL);
    }
  } else {
    // Proceso recepción
    printf("[RecepciÃ³n] Comienzo mi ejecuciÃ³n...\n");
    signal(SIGINT, endProcess); // Maneja la senal SIGINT
    signal(SIGUSR1, pacientesConMedicacion); // Configura la senal SIGUSR1 para contar pacientes dados de alta

    while (1) {
      char paciente[ARR_SIZE];
      sleep(tiempo_aleatorio(1, 10)); // Simula el tiempo de registro de nuevos pacientes
      sprintf(paciente, "El mensaje de la recepcion: %d", nuevos_pacientes += 1);
      printf("[RecepciÃ³n] Registrando nuevo paciente: %s...\n", paciente);

      // Abre la cola de mensajes en modo solo escritura
      mqd = mq_open(MQ_NAME, O_WRONLY | O_CREAT, 0644, &attributes); //El propietario del archivo puede leer y escribir en la cola de mensajes y el grupo y los otros usuarios solo pueden leer la cola de mensajes.

      // Envía el mensaje de registro a la cola
      mq_send(mqd, paciente, sizeof(char) * ARR_SIZE, 1);
    }
  }

  exit(0); // Finaliza el proceso
}