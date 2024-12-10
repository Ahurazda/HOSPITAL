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
#define SEM_NAME "/semaforo" // Nombre del sem�foro
#define ARR_SIZE 128

struct mq_attr attributes = {
  .mq_flags = 0, // flags de la cola (ignorados por mq_open())
  .mq_maxmsg = 10, // N�mero m�ximo de mensajes en la cola
  .mq_curmsgs = 0, // N�mero actual de mensajes en la cola (ignorados por mq_open())
  .mq_msgsize = sizeof(char) * ARR_SIZE // Tamano m�ximo de cada mensaje
};

// VARIABLES GLOBALES
mqd_t mqd; // Cola de mensajes

sem_t sem_diag; // Sem�foro para el diagn�stico
sem_t sem_farm; // Sem�foro para la farmacia

int pacientes_dados_de_alta = 0; // Contador de pacientes dados de alta
int nuevos_pacientes = 0; // Contador de nuevos pacientes

pthread_t exp_thr, farm_thr, diag_thr; // Identificadores de los hilos

pid_t pid_hospital, pid_recepcion; // Identificadores de los procesos padre e hijo

// FUNCIONES

// Funci�n para generar un n�mero aleatorio entre un rango
int tiempo_aleatorio(int min, int max) {
  return rand() % (max - min + 1) + min;
}

// Funci�n para manejar la senal SIGINT (interrupci�n)
void sigint(int sig) {
	 
  mq_close(mqd); // Cierra la cola de mensajes
  mq_unlink(MQ_NAME); // Elimina la cola de mensajes
  sem_destroy( &sem_farm); // Destruye el sem�foro de farmacia
  sem_destroy( &sem_diag); // Destruye el sem�foro de diagn�stico
  sem_unlink(SEM_NAME); // Elimina el sem�foro
  printf("El padre ha terminado\n");
  exit(0);
}

// Funci�n para finalizar el proceso hijo
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


// Funci�n para notificar a la recepci�n (env�a la senal SIGUSR1)
void notificaRecepcion(int sig) {
  signal(SIGUSR1, notificaRecepcion); // Recibe senales SIGUSR1
  kill(pid_recepcion, SIGUSR1); // Env�a la senal SIGUSR1 al proceso de recepci�n
}

// Funci�n para actualizar el n�mero de pacientes dados de alta
void pacientesConMedicacion(int sig) {
  signal(SIGUSR1, pacientesConMedicacion); // Espera para la senal de nuevo
  pacientes_dados_de_alta += 1; // Incrementa el contador de pacientes dados de alta
  printf("Pacientes dados de alta: %d\n", pacientes_dados_de_alta);
}

//----------------------------------------------------------------

// Funci�n del hilo de exploraci�n
void * exploracion(void * args) {
  printf("[Exploración] Comienzo mi ejecución...\n");
  // Abre la cola de mensajes en modo solo lectura
  mqd_t mqd = mq_open(MQ_NAME, O_RDONLY | O_CREAT, 0644, &attributes);

  while (1) {
    char paciente[ARR_SIZE];
    signal(SIGUSR1, SIG_IGN); // Ignora senales SIGUSR1
    printf("[Exploración] Esperando a un paciente...\n");
    mq_receive(mqd, paciente, sizeof(char) * ARR_SIZE, NULL); // Recibe un mensaje de la cola
    printf("[Exploración] Recibido paciente: %s. Realizando exploración...\n", paciente);

    // Simula el tiempo de exploraci�n
    sleep(tiempo_aleatorio(1, 3));

    printf("[Exploración] Exploración completa con paciente %s. Notificando diagnóstico...\n", paciente);

    // Desbloquea el sem�foro para que el hilo de diagn�stico contin�e
    sem_post( &sem_diag);
  }
}

// Funci�n del hilo de diagn�stico
void * diagnostico(void * args) {
  printf("[Diagnóstico] Comienzo mi ejecución...\n");

  while (1) {
    signal(SIGUSR1, SIG_IGN); // Ignora senales SIGUSR1
    sem_wait( &sem_diag); // Decrementa el valor del sem�foro (espera hasta que se libere)

    printf("[Diagnóstico] Realizando pruebas diagnósticas...\n");
    sleep(tiempo_aleatorio(5, 10)); // Simula el tiempo de las pruebas de diagn�stico

    printf("[Diagnóstico] Diagnóstico completado. Notificando farmacia...\n");

    // Desbloquea el sem�foro de farmacia para continuar el proceso en farmacia
    sem_post( &sem_farm);
  }
}

// Funci�n del hilo de farmacia
void * farmacia(void * args) {
  signal(SIGUSR1, SIG_IGN); // Ignora senales SIGUSR1
  printf("[Farmacia] Comienzo mi ejecución...\n");

  while (1) {
    sem_wait( &sem_farm); // Decrementa el sem�foro, esperando que el diagn�stico est� listo

    printf("[Farmacia] Preparando medicación...\n");
    sleep(tiempo_aleatorio(1, 3)); // Simula el tiempo de preparaci�n de la medicaci�n

    printf("[Farmacia] Medicación lista. Enviando señal de alta...\n");

    // Notifica a la recepci�n de que la medicaci�n est� lista
    kill(pid_recepcion, SIGUSR1);
  }
}

void main(int argv, char * argc[]) {
  sem_init( &sem_diag, 0, 0); // Inicializa el sem�foro para diagn�stico con valor 0 (bloqueado)
  sem_init( &sem_farm, 0, 0); // Inicializa el sem�foro para farmacia con valor 0 (bloqueado)

  pid_recepcion = fork(); // Crea un proceso hijo para la recepci�n

  if (pid_recepcion != 0) { // Proceso padre
    pid_hospital = fork(); // Crea un proceso hijo para el hospital
    if (pid_hospital != 0) { // Proceso abuelo (padre de hospital)
      int status;
      signal(SIGINT, sigint); // Configura la senal SIGINT (Ctrl+C) para manejarla
      for (int i = 0; i < 2; i++) { // El padre espera a sus dos hijos (hospital y recepci�n)
        pid_t child_pid = wait( &status); // Espera quelos hijos terminen
      }
    } else {
      // Proceso hospital
      signal(SIGINT, endHospital); // Maneja la senal SIGINT
      signal(SIGUSR1, notificaRecepcion); // Configura la senal SIGUSR1 para notificar a la recepci�n
      printf("[Hospital] Comienzo mi ejecución...\n");

      // Crea los hilos para exploraci�n, diagn�stico y farmacia
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
    // Proceso recepci�n
    printf("[Recepción] Comienzo mi ejecución...\n");
    signal(SIGINT, endProcess); // Maneja la senal SIGINT
    signal(SIGUSR1, pacientesConMedicacion); // Configura la senal SIGUSR1 para contar pacientes dados de alta

    while (1) {
      char paciente[ARR_SIZE];
      sleep(tiempo_aleatorio(1, 10)); // Simula el tiempo de registro de nuevos pacientes
      sprintf(paciente, "El mensaje de la recepcion: %d", nuevos_pacientes += 1);
      printf("[Recepción] Registrando nuevo paciente: %s...\n", paciente);

      // Abre la cola de mensajes en modo solo escritura
      mqd = mq_open(MQ_NAME, O_WRONLY | O_CREAT, 0644, &attributes); //El propietario del archivo puede leer y escribir en la cola de mensajes y el grupo y los otros usuarios solo pueden leer la cola de mensajes.

      // Env�a el mensaje de registro a la cola
      mq_send(mqd, paciente, sizeof(char) * ARR_SIZE, 1);
    }
  }

  exit(0); // Finaliza el proceso
}